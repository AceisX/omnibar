#include "app.h"

#include "core/log.h"
#include "core/paths.h"
#include "shell/window.h"
#include "ui/layout.h"

#include <shellapi.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM

#include <algorithm>
#include <cmath>

namespace omni {
namespace {

constexpr UINT kCursorTickMs = 100;   // 10 Hz, come MiniBar: due syscall, invisibili
constexpr UINT kAnimTickMs   = 8;
constexpr UINT kSlideMs      = 190;
constexpr UINT kUnhoverMs    = 400;
constexpr float kOutsideMarginDip = 6.f;  // tolleranza attorno alla barra aperta

// Ease-out cubico: parte veloce e si posa. Su un'animazione di apertura conta
// piu' l'inizio della fine — l'utente giudica la reattivita' dai primi 50 ms.
float EaseOut(float t) {
    t = std::clamp(t, 0.f, 1.f);
    const float inv = 1.f - t;
    return 1.f - inv * inv * inv;
}

bool PointIn(const RECT& r, POINT p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

RECT Inflate(const RECT& r, int by) {
    return RECT{r.left - by, r.top - by, r.right + by, r.bottom + by};
}

}  // namespace

// ── Ciclo di vita ────────────────────────────────────────────────────────────

bool App::Init(HINSTANCE inst) {
    inst_ = inst;

    if (!shell::RegisterBarClass(inst, &App::WndProc)) return false;

    hwnd_ = shell::CreateBarWindow(inst, this);
    if (!hwnd_) return false;

    if (!renderer_.Init(hwnd_, shell::DpiFor(hwnd_))) {
        log::Error(L"Renderer non inizializzato: si esce");
        return false;
    }
    ApplyTheme();

    actions_.SetInternalHandler([this](std::wstring_view name) {
        return OnInternalAction(name);
    });

    // Icona provvisoria: assets/omnibar.ico arriva con la fase 6, e una icona
    // finta disegnata adesso sarebbe da rifare.
    shell::AddTrayIcon(hwnd_, LoadIconW(nullptr, IDI_APPLICATION));

    BuildTree();
    RefreshPlacement(true);

    // Si parte nascosti, gia' fuori schermo, e senza mai rubare il focus.
    slide_ = slideTarget_ = 0.f;
    ApplySlide();
    shell::SetClickThrough(hwnd_, true);
    shell::ShowNoActivate(hwnd_);
    Redraw();

    SetTimer(hwnd_, IDT_CURSOR, kCursorTickMs, nullptr);

    log::Info(L"Barra pronta — bordo in basso, nascosta");
    return true;
}

void App::Shutdown() {
    if (hwnd_) {
        KillTimer(hwnd_, IDT_CURSOR);
        KillTimer(hwnd_, IDT_ANIM);
        shell::RemoveTrayIcon(hwnd_);
    }
    renderer_.Shutdown();
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) return self->Handle(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            if (wp == IDT_CURSOR) OnCursorTick();
            else if (wp == IDT_ANIM) OnAnimTick();
            return 0;

        case WM_MOUSEMOVE:
            OnMouseMove(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;

        case WM_LBUTTONDOWN:
            OnMouseDown(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;

        case WM_LBUTTONUP:
            OnMouseUp(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;

        case WM_RBUTTONUP: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(hwnd_, &pt);
            OnTrayMenu(pt);
            return 0;
        }

        case WM_APP_TRAY:
            if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP) {
                POINT pt{};
                GetCursorPos(&pt);
                if (LOWORD(lp) == WM_LBUTTONUP) Reveal();
                else                            OnTrayMenu(pt);
            }
            return 0;

        case WM_APP_QUIT:
            PostQuitMessage(0);
            return 0;

        case WM_DPICHANGED:
        case WM_DISPLAYCHANGE:
            // I rettangoli non si memorizzano mai: si ricalcola tutto.
            RefreshPlacement(true);
            Relayout();
            Redraw();
            return 0;

        case WM_SETTINGCHANGE:
            ApplyTheme();
            RefreshPlacement(true);  // l'area di lavoro cambia con la taskbar
            Relayout();
            Redraw();
            return 0;

        case WM_ENDSESSION:
            log::Info(L"WM_ENDSESSION: chiusura richiesta dal sistema");
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ── Macchina a stati ─────────────────────────────────────────────────────────

const wchar_t* StateName(BarState s) {
    switch (s) {
        case BarState::Hidden:     return L"Hidden";
        case BarState::Peek:       return L"Peek";
        case BarState::Revealed:   return L"Revealed";
        case BarState::Pinned:     return L"Pinned";
        case BarState::Attention:  return L"Attention";
        case BarState::Expanded:   return L"Expanded";
        case BarState::Suppressed: return L"Suppressed";
    }
    return L"?";
}

void App::SetState(BarState next) {
    if (state_ == next) return;

    // Le transizioni di stato si tracciano sempre: sono poche, rare, e sono la
    // prima cosa che serve sapere quando la barra "fa una cosa strana".
    log::Info(std::wstring(L"stato: ") + StateName(state_) + L" -> " + StateName(next));
    state_ = next;

    // Click-through in ogni stato tranne quelli in cui la barra e' davvero
    // usabile: cosi' non intercetta mai un click che non le appartiene.
    const bool interactive = (next == BarState::Revealed || next == BarState::Pinned ||
                              next == BarState::Attention || next == BarState::Expanded);
    shell::SetClickThrough(hwnd_, !interactive);

    if (!interactive) {
        hoveredId_.clear();
        pressedId_.clear();
    }
}

void App::Reveal() {
    if (state_ == BarState::Pinned) return;

    // Il bersaglio delle azioni si cattura adesso, prima che il mouse arrivi
    // sulla barra e prima di qualunque cosa possa spostare il foreground.
    actionTarget_ = GetForegroundWindow();

    RefreshPlacement(false);
    SetState(BarState::Revealed);
    outsideSince_ = 0;
    StartAnimation(1.f);
    Relayout();
    Redraw();
}

void App::Hide() {
    if (pinned_) return;
    SetState(BarState::Hidden);
    trigger_.Reset();
    outsideSince_ = 0;
    StartAnimation(0.f);
}

void App::TogglePin() {
    pinned_ = !pinned_;
    if (pinned_) {
        SetState(BarState::Pinned);
        StartAnimation(1.f);
        Relayout();
    } else {
        SetState(BarState::Revealed);
        outsideSince_ = 0;
    }
    Redraw();
}

// ── Eventi ───────────────────────────────────────────────────────────────────

void App::OnCursorTick() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return;
    const ULONGLONG now = GetTickCount64();

    if (state_ == BarState::Hidden) {
        // Da chiusa la barra segue il monitor sotto il cursore: su un portatile
        // con un monitor esterno, apparire sull'altro schermo e' inutile.
        RefreshPlacement(false);

        const bool inZone = placement_.valid() && PointIn(placement_.trigger, cursor);

        if (log::Enabled(log::Level::Trace)) {
            log::Trace(L"cursore " + std::to_wstring(cursor.x) + L"," + std::to_wstring(cursor.y) +
                       L"  zona " + std::to_wstring(placement_.trigger.left) + L"," +
                       std::to_wstring(placement_.trigger.top) + L".." +
                       std::to_wstring(placement_.trigger.right) + L"," +
                       std::to_wstring(placement_.trigger.bottom) +
                       (inZone ? L"  DENTRO" : L""));
        }

        if (trigger_.Update(inZone, cursor, now)) Reveal();
        return;
    }

    if (state_ == BarState::Revealed) {
        RECT bar{};
        GetWindowRect(hwnd_, &bar);
        const int margin = static_cast<int>(kOutsideMarginDip *
                                            static_cast<float>(placement_.dpi) / 96.f);

        if (PointIn(Inflate(bar, margin), cursor)) {
            outsideSince_ = 0;
        } else if (outsideSince_ == 0) {
            outsideSince_ = now;
        } else if (now - outsideSince_ >= kUnhoverMs) {
            // Isteresi: uscire per un attimo mentre si punta un bottone non
            // deve chiudere la barra sotto il cursore.
            Hide();
        }
    }
}

void App::OnAnimTick() {
    const ULONGLONG now = GetTickCount64();
    const float elapsed = static_cast<float>(now - animStart_);
    const float t       = std::clamp(elapsed / static_cast<float>(kSlideMs), 0.f, 1.f);

    slide_ = slideFrom_ + (slideTarget_ - slideFrom_) * EaseOut(t);
    ApplySlide();

    if (t >= 1.f) {
        slide_     = slideTarget_;
        animating_ = false;
        KillTimer(hwnd_, IDT_ANIM);
        ApplySlide();
    }
}

void App::OnMouseMove(POINT clientPx) {
    const ui::RectF pt = ToDip(clientPx);
    const ui::Widget* hit = ui::HitTest(root_, pt.x, pt.y);
    const std::string id = hit ? hit->id : std::string{};
    if (id == hoveredId_) return;
    hoveredId_ = id;
    Redraw();
}

void App::OnMouseDown(POINT clientPx) {
    const ui::RectF pt = ToDip(clientPx);
    const ui::Widget* hit = ui::HitTest(root_, pt.x, pt.y);
    pressedId_ = hit ? hit->id : std::string{};
    if (!pressedId_.empty()) {
        SetCapture(hwnd_);
        Redraw();
    }
}

void App::OnMouseUp(POINT clientPx) {
    if (pressedId_.empty()) return;
    ReleaseCapture();

    const ui::RectF pt = ToDip(clientPx);
    const ui::Widget* hit = ui::HitTest(root_, pt.x, pt.y);
    const std::string pressedId = std::move(pressedId_);
    pressedId_.clear();

    // Il click vale solo se si rilascia sullo stesso widget su cui si e'
    // premuto: e' cosi' che si annulla un click sbagliato trascinando via.
    ui::Widget* target = ui::Find(root_, pressedId);
    if (hit && target && hit->id == pressedId && target->action.valid()) {
        if (target->type == ui::WidgetType::Toggle) target->on = !target->on;

        const action::Context ctx{actionTarget_};
        // I widget dell'host hanno tutte le capability: sono l'host. Quelli
        // delle estensioni avranno solo cio' che l'utente ha concesso.
        actions_.Invoke(target->action, ctx, 0xFFFFFFFFu);
    }
    Redraw();
}

void App::OnTrayMenu(POINT screenPt) {
    shell::MenuState state;
    state.edge      = placementCfg_.edge;
    state.pinned    = pinned_;
    state.autostart = shell::AutostartEnabled();

    switch (shell::ShowTrayMenu(hwnd_, screenPt, state)) {
        case IDM_EDGE_BOTTOM: placementCfg_.edge = Edge::Bottom; break;
        case IDM_EDGE_TOP:    placementCfg_.edge = Edge::Top;    break;
        case IDM_EDGE_LEFT:   placementCfg_.edge = Edge::Left;   break;
        case IDM_EDGE_RIGHT:  placementCfg_.edge = Edge::Right;  break;

        case IDM_REVEAL:      Reveal(); return;
        case IDM_PIN:         TogglePin(); return;

        case IDM_AUTOSTART:
            shell::SetAutostart(!state.autostart);
            return;

        case IDM_OPEN_CONFIG:
            ShellExecuteW(nullptr, L"open", paths::Root().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;

        case IDM_OPEN_LOG: {
            const std::wstring file = paths::Logs() + L"omnibar.log";
            ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }

        case IDM_EXIT:
            PostMessageW(hwnd_, WM_APP_QUIT, 0, 0);
            return;

        default:
            return;
    }

    // Cambio di bordo: si ricalcola tutto e si riparte da chiusa.
    pinned_ = false;
    SetState(BarState::Hidden);
    slide_ = slideTarget_ = 0.f;
    RefreshPlacement(true);
    Relayout();
    ApplySlide();
    Redraw();
}

bool App::OnInternalAction(std::wstring_view name) {
    if (name == L"bar.pin")   { TogglePin(); return true; }
    if (name == L"bar.hide")  { pinned_ = false; Hide(); return true; }
    if (name == L"bar.menu")  {
        POINT pt{};
        GetCursorPos(&pt);
        OnTrayMenu(pt);
        return true;
    }
    if (name == L"app.quit")  { PostMessageW(hwnd_, WM_APP_QUIT, 0, 0); return true; }
    // I bottoni dell'albero di prova non fanno niente: esistono per far vedere
    // che il click arriva dove deve. Spariranno con i profili veri.
    if (name == L"demo.noop") {
        log::Info(L"click su un bottone dell'albero di prova");
        return true;
    }
    if (name == L"app.config") {
        ShellExecuteW(nullptr, L"open", paths::Root().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return true;
    }

    log::Warn(std::wstring(L"Azione interna sconosciuta: ") + std::wstring(name));
    return false;
}

// ── Geometria e disegno ──────────────────────────────────────────────────────

void App::RefreshPlacement(bool force) {
    HMONITOR monitor = (state_ == BarState::Hidden || force)
                           ? shell::MonitorUnderCursor()
                           : placement_.monitor;
    if (!monitor) monitor = shell::PrimaryMonitor();

    if (!force && placement_.valid() && placement_.monitor == monitor) return;

    const shell::Placement next = shell::Compute(placementCfg_, monitor);
    if (!next.valid()) {
        log::Warn(L"Placement non calcolabile: monitor non valido");
        return;
    }

    const bool sizeChanged = (next.sizePx.cx != placement_.sizePx.cx ||
                              next.sizePx.cy != placement_.sizePx.cy ||
                              next.dpi != placement_.dpi);
    placement_ = next;

    if (sizeChanged) {
        renderer_.Resize(static_cast<UINT>(placement_.sizePx.cx),
                         static_cast<UINT>(placement_.sizePx.cy), placement_.dpi);
        Relayout();
    }
}

void App::Relayout() {
    if (!placement_.valid()) return;

    const float scale = 96.f / static_cast<float>(placement_.dpi);
    const float w = static_cast<float>(placement_.sizePx.cx) * scale;
    const float h = static_cast<float>(placement_.sizePx.cy) * scale;

    const ui::Metrics m = renderer_.Metrics();
    const ui::RectF bounds{m.padding, m.padding,
                           std::max(0.f, w - m.padding * 2.f),
                           std::max(0.f, h - m.padding * 2.f)};

    root_.direction = (placementCfg_.edge == Edge::Left || placementCfg_.edge == Edge::Right)
                          ? ui::Direction::Column
                          : ui::Direction::Row;

    ui::Layout(root_, bounds, m);

    // I rettangoli sono cambiati: l'hover va ricalcolato al prossimo movimento
    // del mouse, non tenuto da quello di prima.
    hoveredId_.clear();
}

void App::Redraw() {
    if (!renderer_.Valid()) return;
    render::DrawState state;
    state.hovered = hoveredId_;
    state.pressed = pressedId_;
    state.opacity = 1.f;
    state.edge    = placementCfg_.edge;
    renderer_.Draw(root_, state);
}

void App::ApplySlide() {
    if (!placement_.valid()) return;
    const RECT r = shell::Slide(placement_, slide_);
    if (log::Enabled(log::Level::Debug)) {
        log::Debug(L"slide " + std::to_wstring(slide_) + L" -> " +
                   std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L" " +
                   std::to_wstring(r.right - r.left) + L"x" + std::to_wstring(r.bottom - r.top));
    }
    shell::MoveNoActivate(hwnd_, r);
}

void App::StartAnimation(float target) {
    if (std::fabs(target - slide_) < 0.001f) {
        slide_ = target;
        ApplySlide();
        return;
    }
    slideFrom_   = slide_;
    slideTarget_ = target;
    animStart_   = GetTickCount64();
    if (!animating_) {
        animating_ = true;
        SetTimer(hwnd_, IDT_ANIM, kAnimTickMs, nullptr);
    }
}

ui::RectF App::ToDip(POINT clientPx) const {
    const float scale = 96.f / static_cast<float>(placement_.dpi ? placement_.dpi : 96);
    return ui::RectF{static_cast<float>(clientPx.x) * scale,
                     static_cast<float>(clientPx.y) * scale, 0.f, 0.f};
}

void App::ApplyTheme() {
    renderer_.SetTheme(render::AppsUseLightTheme() ? render::Theme::Light()
                                                  : render::Theme::Dark());
}

// ── L'albero ─────────────────────────────────────────────────────────────────

void App::BuildTree() {
    // Albero di prova, cablato: serve a collaudare vocabolario, layout,
    // hit-test e azioni finche' non c'e' il parser TOML (fase 1, prossimo
    // passo). Nessuno di questi bottoni sopravvivera': li sostituiranno i
    // profili dichiarativi e i moduli.
    using namespace ui;

    root_ = Group(Direction::Row, 6.f, {
        Label(L"OmniBar", Emphasis::Dim),
        Separator(),
        Button("demo.folder",  L"folder",   L"Cartella",  Internal(L"demo.noop")),
        Button("demo.camera",  L"camera",   L"Cattura",   Internal(L"demo.noop")),
        Button("demo.copy",    L"copy",     L"Copia",     Internal(L"demo.noop")),
        Toggle("demo.record",  L"record",   L"Registra",  false, Internal(L"demo.noop")),
        Separator(),
        Button("demo.prev",    L"prev",     L"Precedente", Internal(L"demo.noop")),
        Button("demo.play",    L"play",     L"Riproduci",  Internal(L"demo.noop")),
        Button("demo.next",    L"next",     L"Successivo", Internal(L"demo.noop")),
        Spacer(),
        TextButton("demo.site", L"github.com/AceisX/omnibar",
                   Url(L"https://github.com/AceisX/omnibar")),
        Separator(),
        Toggle("bar.pin",      L"pin",      L"Tieni aperta", false, Internal(L"bar.pin")),
        Button("bar.menu",     L"settings", L"Menu",         Internal(L"bar.menu")),
        Button("bar.hide",     L"close",    L"Nascondi",     Internal(L"bar.hide")),
    });
    root_.align = ui::Align::Center;

    // Un badge, per vedere che si disegna dove deve.
    if (ui::Widget* rec = ui::Find(root_, "demo.record")) rec->badge = -1;
}

}  // namespace omni
