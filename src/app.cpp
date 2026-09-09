#include "app.h"

#include "core/log.h"
#include "core/paths.h"
#include "shell/window.h"
#include "ui/layout.h"

#include <shellapi.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <timeapi.h>    // timeBeginPeriod

#include <algorithm>
#include <cmath>

namespace omni {
namespace {

// Il polling del cursore e' adattivo. Lontano dal bordo bastano 10 Hz — due
// syscall, invisibili. Vicino al bordo si sale a 125 Hz, perche' li' ogni tick
// perso e' latenza percepita: con il solo passo lento, fra "il cursore arriva"
// e "la barra se ne accorge" potevano passare cento millisecondi, prima ancora
// che cominciasse l'attesa di conferma.
// Il polling del cursore e' adattivo. Lontano dal bordo bastano 10 Hz — due
// syscall, invisibili. Vicino al bordo si sale, perche' li' ogni tick perso e'
// latenza percepita.
constexpr UINT kSlowTickMs = 100;
constexpr UINT kFastTickMs = 8;

// Reattivita' e tranquillita' non sono la stessa manopola. La reattivita' si
// sente da QUANDO il movimento comincia — la decidono l'attesa di conferma e il
// passo del polling. La tranquillita' si sente da COME si posa, e quella vuole
// tempo.
constexpr UINT kOpenMs     = 260;
constexpr UINT kCloseMs    = 200;
constexpr UINT kAnimTickMs = 8;
constexpr UINT kUnhoverMs  = 420;

constexpr float kOutsideMarginDip = 6.f;

// Entro questa distanza dal bordo si passa al polling veloce. Non succede
// niente di visibile: serve solo a non perdere l'istante in cui il cursore
// arriva.
constexpr float kNearEdgeDip = 160.f;

// Apertura e chiusura: ease-out puro, senza oltrepasso.
//
// L'oltrepasso c'era, ed era sbagliato qui. Su un oggetto che entra da fuori
// campo un rimbalzo racconta una massa; su un oggetto fermo che si apre non
// racconta niente e si legge come un tic. Un componente di sistema non
// rimbalza.
float EaseOut(float t) {
    t = std::clamp(t, 0.f, 1.f);
    const float inv = 1.f - t;
    return 1.f - inv * inv * inv;
}

float SmoothStep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
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

    // Soglie di apertura. Erano 180 ms di attesa piu' fino a 100 di polling:
    // mezzo secondo prima che succedesse qualcosa. Adesso il richiamo della
    // pastiglia da' un riscontro immediato, quindi la conferma puo' essere
    // molto piu' breve senza diventare nervosa.
    trigger_.SetConfig({90, 40});

    // Si parte a riposo, senza mai rubare il focus. La finestra si posiziona
    // una volta sola: da qui in poi non si muove piu'.
    open_ = openTarget_ = 0.f;
    ApplyPlacement();
    shell::SetClickThrough(hwnd_, true);
    shell::ShowNoActivate(hwnd_);
    Redraw();

    SetCursorTick(kSlowTickMs);

    log::Debug(L"placement: finestra " + std::to_wstring(placement_.rect.left) + L"," +
               std::to_wstring(placement_.rect.top) + L" " +
               std::to_wstring(placement_.sizePx.cx) + L"x" +
               std::to_wstring(placement_.sizePx.cy) + L"  contenuto " +
               std::to_wstring(static_cast<int>(contentLen_)) + L" DIP");
    log::Info(std::wstring(L"Barra pronta — bordo a ") + EdgeName(placementCfg_.edge) +
              L", " + std::to_wstring(placement_.sizePx.cx) + L"x" +
              std::to_wstring(placement_.sizePx.cy) + L" px, a riposo");
    return true;
}

void App::Shutdown() {
    if (timerBoosted_) {
        timeEndPeriod(1);
        timerBoosted_ = false;
    }
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
    Relayout();
    StartAnimation(1.f);
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
        Relayout();
        StartAnimation(1.f);
    } else {
        SetState(BarState::Revealed);
        outsideSince_ = 0;
    }
    Redraw();
}

// ── Eventi ───────────────────────────────────────────────────────────────────

void App::SetCursorTick(UINT intervalMs) {
    if (cursorTickMs_ == intervalMs) return;
    cursorTickMs_ = intervalMs;
    SetTimer(hwnd_, IDT_CURSOR, intervalMs, nullptr);
}

float App::EdgeDistanceDip(POINT cursor) const {
    if (!placement_.valid()) return 1e9f;
    const RECT& w = placement_.work;

    // Fuori dall'area di lavoro sull'altro asse la barra non deve reagire: il
    // cursore e' sul bordo destro ma a meta' di un altro monitor.
    const bool alongOk = placement_.horizontal ? (cursor.x >= w.left && cursor.x < w.right)
                                               : (cursor.y >= w.top && cursor.y < w.bottom);
    if (!alongOk) return 1e9f;

    int px = 0;
    switch (placementCfg_.edge) {
        case Edge::Bottom: px = w.bottom - cursor.y; break;
        case Edge::Top:    px = cursor.y - w.top;    break;
        case Edge::Left:   px = cursor.x - w.left;   break;
        case Edge::Right:  px = w.right - cursor.x;  break;
    }
    return static_cast<float>(std::max(0, px)) * 96.f /
           static_cast<float>(placement_.dpi ? placement_.dpi : 96);
}

void App::OnCursorTick() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return;
    const ULONGLONG now = GetTickCount64();

    if (state_ == BarState::Hidden) {
        // Da chiusa la barra segue il monitor sotto il cursore: su un portatile
        // con un monitor esterno, apparire sull'altro schermo e' inutile.
        RefreshPlacement(false);
        if (!placement_.valid()) return;

        const float dist = EdgeDistanceDip(cursor);

        // Isteresi sulla soglia: un cursore fermo proprio sul confine
        // riarmerebbe il timer di sistema molte volte al secondo per niente.
        const bool wasFast = (cursorTickMs_ == kFastTickMs);
        const bool atEdge  = wasFast ? (dist < kNearEdgeDip * 1.3f) : (dist < kNearEdgeDip);
        SetCursorTick(atEdge ? kFastTickMs : kSlowTickMs);

        // A riposo non si ridisegna nulla: la linea e la sporgenza non si
        // muovono, quindi non c'e' niente da aggiornare. Prima la barra
        // inseguiva il cursore e ridisegnava di continuo — quel movimento e'
        // proprio cio' che stonava, e toglierlo ha tolto anche il costo.
        if (trigger_.Update(PointIn(placement_.trigger, cursor), cursor, now)) Reveal();
        return;
    }

    SetCursorTick(kSlowTickMs);

    if (state_ == BarState::Revealed) {
        RECT bar{};
        GetWindowRect(hwnd_, &bar);
        const int margin = static_cast<int>(kOutsideMarginDip *
                                            static_cast<float>(placement_.dpi) / 96.f);

        // La finestra copre tutto il bordo, quindi non basta chiedere se il
        // cursore e' dentro: conta se e' dentro il PANNELLO, che occupa solo la
        // parte centrale. Altrimenti la barra resterebbe aperta per sempre.
        RECT panel = bar;
        const int lenPx = static_cast<int>(contentLen_ *
                                           static_cast<float>(placement_.dpi) / 96.f);
        if (placement_.horizontal) {
            const int c = (bar.left + bar.right) / 2;
            panel.left = c - lenPx / 2;  panel.right = c + lenPx / 2;
        } else {
            const int c = (bar.top + bar.bottom) / 2;
            panel.top = c - lenPx / 2;   panel.bottom = c + lenPx / 2;
        }

        if (PointIn(Inflate(panel, margin), cursor)) {
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
    const bool      opening  = openTarget_ > openFrom_;
    const float     duration = static_cast<float>(opening ? kOpenMs : kCloseMs);
    const float     elapsed  = static_cast<float>(now - animStart_);
    const float     t        = std::clamp(elapsed / duration, 0.f, 1.f);

    open_ = openFrom_ + (openTarget_ - openFrom_) * EaseOut(t);
    Redraw();

    if (t >= 1.f) {
        open_      = openTarget_;
        animating_ = false;
        KillTimer(hwnd_, IDT_ANIM);

        if (timerBoosted_) {
            timeEndPeriod(1);
            timerBoosted_ = false;
        }
        Redraw();
    }
}

void App::OnMouseMove(POINT clientPx) {
    const ui::RectF pt = ToDip(clientPx);

    // Dove sta il cursore lungo la barra: serve all'ingrandimento delle icone.
    // E' in coordinate della superficie, come i rettangoli del layout.
    cursorAlong_ = Vertical() ? pt.y : pt.x;
    magnify_     = 1.f;

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
    open_ = openTarget_ = 0.f;
    ApplyEdge();
    RefreshPlacement(true);
    Relayout();
    ApplyPlacement();
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

    const shell::Placement next = shell::Compute(placementCfg_, monitor);
    if (!next.valid()) {
        log::Warn(L"Placement non calcolabile: monitor non valido");
        return;
    }

    const bool changed = (next.sizePx.cx != placement_.sizePx.cx ||
                          next.sizePx.cy != placement_.sizePx.cy ||
                          next.dpi != placement_.dpi ||
                          next.monitor != placement_.monitor);
    placement_ = next;

    if (changed) {
        renderer_.Resize(static_cast<UINT>(placement_.sizePx.cx),
                         static_cast<UINT>(placement_.sizePx.cy), placement_.dpi);
        Relayout();
        ApplyPlacement();
    }
}

void App::Relayout() {
    if (!placement_.valid()) return;

    const float scale = 96.f / static_cast<float>(placement_.dpi ? placement_.dpi : 96);
    const float w = static_cast<float>(placement_.sizePx.cx) * scale;
    const float h = static_cast<float>(placement_.sizePx.cy) * scale;

    const ui::Metrics m = renderer_.Metrics();

    // La barra aperta e' lunga quanto il suo contenuto, e sta al centro del
    // bordo. La finestra invece copre tutto il bordo: il contenuto va disposto
    // nel tratto centrale, non su tutta la superficie.
    contentLen_ = std::min(ContentExtentDip(), Vertical() ? h : w);

    const float padX = Vertical() ? m.padCross : m.padAlong;
    const float padY = Vertical() ? m.padAlong : m.padCross;

    ui::RectF bounds;
    if (Vertical()) {
        bounds = ui::RectF{padX, (h - contentLen_) * 0.5f + padY,
                           std::max(0.f, w - padX * 2.f),
                           std::max(0.f, contentLen_ - padY * 2.f)};
    } else {
        bounds = ui::RectF{(w - contentLen_) * 0.5f + padX, padY,
                           std::max(0.f, contentLen_ - padX * 2.f),
                           std::max(0.f, h - padY * 2.f)};
    }

    ui::Layout(root_, bounds, m);

    // I rettangoli sono cambiati: l'hover va ricalcolato al prossimo movimento
    // del mouse, non tenuto da quello di prima.
    hoveredId_.clear();
}

void App::Redraw() {
    if (!renderer_.Valid() || !placement_.valid()) return;

    const float scale = static_cast<float>(placement_.dpi ? placement_.dpi : 96) / 96.f;

    render::DrawState state;
    state.hovered       = hoveredId_;
    state.pressed       = pressedId_;
    state.edge          = placementCfg_.edge;
    state.opacity       = 1.f;
    state.openT         = open_;
    state.lineDip       = placementCfg_.lineDip;
    state.nubThickDip   = placementCfg_.nubThickDip;
    state.nubLenDip     = placementCfg_.nubLenDip;
    state.barThickDip   = static_cast<float>(placement_.thicknessPx) / scale;
    state.contentLenDip = contentLen_;

    // Le icone compaiono quando c'e' spazio per contenerle, non prima: dentro
    // la sporgenza sarebbero un ammasso.
    state.contentAlpha  = SmoothStep(0.55f, 0.98f, open_);
    state.cursorAlong   = cursorAlong_;
    state.magnify       = magnify_;

    renderer_.Draw(root_, state);
}

void App::ApplyPlacement() {
    if (!placement_.valid()) return;
    shell::MoveNoActivate(hwnd_, placement_.rect);
}

void App::StartAnimation(float target) {
    if (std::fabs(target - open_) < 0.001f) {
        open_ = target;
        Redraw();
        return;
    }
    openFrom_   = open_;
    openTarget_ = target;
    animStart_  = GetTickCount64();

    if (!animating_) {
        animating_ = true;

        // `SetTimer` non sa fare meno della risoluzione del timer di sistema,
        // che di norma e' ~15,6 ms: chiedere 8 ms dava 62 fotogrammi al secondo
        // con spaziatura irregolare rispetto al refresh dello schermo, ed e'
        // quello a leggersi come movimento a scatti. Si alza la risoluzione,
        // ma solo per la durata dell'animazione: tenerla alta sempre costerebbe
        // batteria a un programma che sta acceso tutto il giorno.
        if (!timerBoosted_ && timeBeginPeriod(1) == TIMERR_NOERROR) timerBoosted_ = true;

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
