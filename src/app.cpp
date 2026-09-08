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
constexpr UINT kOpenMs       = 300;   // aprire puo' prendersi tempo: si guarda
constexpr UINT kCloseMs      = 190;   // chiudere no: e' un'uscita, non un ingresso
constexpr UINT kUnhoverMs    = 400;
constexpr float kOutsideMarginDip = 6.f;  // tolleranza attorno alla barra aperta

// Apertura: parte decisa, supera di poco l'arrivo e rientra. E' quel rientro a
// far sembrare il movimento fluido invece che meccanico — un ease-out puro si
// posa in modo corretto ma inerte, come una cosa spenta che si ferma. Con
// l'oltrepasso sembra che la barra abbia una massa.
//
// L'oltrepasso e' volutamente piccolo: su cinquanta punti di corsa vale due
// pixel. Deve sentirsi, non vedersi.
float EaseOutBack(float t) {
    t = std::clamp(t, 0.f, 1.f);
    constexpr float kOvershoot = 1.20f;
    const float inv = t - 1.f;
    return 1.f + inv * inv * ((kOvershoot + 1.f) * inv + kOvershoot);
}

// Chiusura: accelera e se ne va. Nessun oltrepasso — rientrando ci sarebbe da
// vedere solo un rimbalzo verso lo schermo di una cosa che sta uscendo.
float EaseInOut(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t < 0.5f ? 4.f * t * t * t
                    : 1.f - std::pow(-2.f * t + 2.f, 3.f) / 2.f;
}

// La dissolvenza che accompagna lo scorrimento in apertura: la barra non arriva
// solo da fuori, emerge. Legata alla posizione e non al tempo, cosi' resta in
// fase con il movimento anche se la curva oltrepassa l'arrivo.
float FadeForSlide(float slide) {
    const float t = std::clamp(slide / 0.55f, 0.f, 1.f);
    const float smooth = t * t * (3.f - 2.f * t);   // smoothstep
    return 0.45f + 0.55f * smooth;
}

bool PointIn(const RECT& r, POINT p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

const wchar_t* EdgeName(Edge e) {
    switch (e) {
        case Edge::Bottom: return L"basso";
        case Edge::Top:    return L"alto";
        case Edge::Left:   return L"sinistra";
        case Edge::Right:  return L"destra";
    }
    return L"?";
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
    ApplyEdge();
    RefreshPlacement(true);

    // Si parte nascosti, gia' fuori schermo, e senza mai rubare il focus.
    slide_ = slideTarget_ = 0.f;
    ApplySlide();
    shell::SetClickThrough(hwnd_, true);
    shell::ShowNoActivate(hwnd_);
    Redraw();

    SetTimer(hwnd_, IDT_CURSOR, kCursorTickMs, nullptr);

    log::Info(std::wstring(L"Barra pronta — bordo a ") + EdgeName(placementCfg_.edge) +
              L", " + std::to_wstring(placement_.sizePx.cx) + L"x" +
              std::to_wstring(placement_.sizePx.cy) + L" px, a riposo");
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
    // Il primo fotogramma con gia' la dissolvenza giusta: altrimenti la barra
    // lampeggia a piena opacita' per un tick prima di cominciare a emergere.
    if (animating_) renderer_.Repaint(FadeForSlide(slide_));
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
    const ULONGLONG now      = GetTickCount64();
    const bool      opening  = slideTarget_ > slideFrom_;
    const float     duration = static_cast<float>(opening ? kOpenMs : kCloseMs);
    const float     elapsed  = static_cast<float>(now - animStart_);
    const float     t        = std::clamp(elapsed / duration, 0.f, 1.f);

    const float eased = opening ? EaseOutBack(t) : EaseInOut(t);
    slide_ = slideFrom_ + (slideTarget_ - slideFrom_) * eased;
    ApplySlide();

    // In apertura la barra emerge; in chiusura resta piena e scivola via, cosi'
    // il passaggio finale alla linguetta non e' un lampo.
    if (opening) renderer_.Repaint(FadeForSlide(slide_));

    if (t >= 1.f) {
        slide_     = slideTarget_;
        animating_ = false;
        KillTimer(hwnd_, IDT_ANIM);
        ApplySlide();
        // Finito lo scorrimento all'indietro, la superficie passa dalla barra
        // alla linguetta: un solo ridisegno, a movimento fermo.
        if (state_ == BarState::Hidden) Redraw();
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
    ApplyEdge();
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

bool App::Vertical() const {
    return placementCfg_.edge == Edge::Left || placementCfg_.edge == Edge::Right;
}

void App::ApplyEdge() {
    const bool vertical = Vertical();
    root_.direction = vertical ? ui::Direction::Column : ui::Direction::Row;
    // Sui bordi laterali la barra e' larga quaranta punti: le etichette non ci
    // stanno, e troncarle si legge come un bug. Spariscono, restano le icone.
    renderer_.SetCompact(vertical);
}

float App::ContentExtentDip() const {
    const ui::Metrics m = renderer_.Metrics();
    const ui::SizeF   s = ui::Measure(root_, m);
    return (Vertical() ? s.h : s.w) + m.padAlong * 2.f;
}

void App::RefreshPlacement(bool force) {
    HMONITOR monitor = (state_ == BarState::Hidden || force)
                           ? shell::MonitorUnderCursor()
                           : placement_.monitor;
    if (!monitor) monitor = shell::PrimaryMonitor();

    if (!force && placement_.valid() && placement_.monitor == monitor) return;

    // La barra e' lunga quanto il suo contenuto: si misura l'albero prima di
    // sapere dove metterla, non dopo.
    const shell::Placement next = shell::Compute(placementCfg_, monitor, ContentExtentDip());
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
    // I due margini vanno sugli assi giusti: quello lungo alle estremita', dove
    // ci sono gli angoli arrotondati, quello corto sui fianchi.
    const float padX = Vertical() ? m.padCross : m.padAlong;
    const float padY = Vertical() ? m.padAlong : m.padCross;

    const ui::RectF bounds{padX, padY,
                           std::max(0.f, w - padX * 2.f),
                           std::max(0.f, h - padY * 2.f)};

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

    // A riposo si disegna solo la linguetta. Durante lo scorrimento no: li' la
    // barra deve gia' esserci tutta, altrimenti non e' un'apertura, e' una
    // comparsa.
    const bool resting = (state_ == BarState::Hidden) && !animating_;
    state.mode    = resting ? render::DrawMode::Handle : render::DrawMode::Bar;
    state.peekDip = static_cast<float>(placement_.peekPx) * 96.f /
                    static_cast<float>(placement_.dpi ? placement_.dpi : 96);

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

    root_ = Group(Direction::Row, 4.f, {
        Label(L"OmniBar", Emphasis::Dim),      // sparisce in compatto
        Separator(),
        Button("demo.folder", L"folder", L"Cartella",   Internal(L"demo.noop")),
        Button("demo.camera", L"camera", L"Cattura",    Internal(L"demo.noop")),
        Button("demo.copy",   L"copy",   L"Copia",      Internal(L"demo.noop")),
        Separator(),
        Button("demo.prev",   L"prev",   L"Precedente", Internal(L"demo.noop")),
        Button("demo.play",   L"play",   L"Riproduci",  Internal(L"demo.noop")),
        Button("demo.next",   L"next",   L"Successivo", Internal(L"demo.noop")),
        Separator(),
        Toggle("demo.record", L"record", L"Registra", false, Internal(L"demo.noop")),
        Separator(),
        Toggle("bar.pin",     L"pin",      L"Tieni aperta", false, Internal(L"bar.pin")),
        Button("bar.menu",    L"settings", L"Menu",         Internal(L"bar.menu")),
    });
    root_.align = ui::Align::Center;

    // Un pallino, per vedere che si disegna dove deve.
    if (ui::Widget* rec = ui::Find(root_, "demo.record")) rec->badge = -1;
}

}  // namespace omni
