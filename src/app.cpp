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

// Il polling del cursore e' adattivo. Lontano dal bordo bastano 10 Hz — due
// syscall, invisibili. Vicino al bordo si sale a 125 Hz, perche' li' ogni tick
// perso e' latenza percepita: con il solo passo lento, fra "il cursore arriva"
// e "la barra se ne accorge" potevano passare cento millisecondi, prima ancora
// che cominciasse l'attesa di conferma.
constexpr UINT kSlowTickMs = 100;
constexpr UINT kFastTickMs = 8;

constexpr UINT kOpenMs    = 200;   // era 300: si vedeva, e vedere un'apertura vuol dire che e' lenta
constexpr UINT kCloseMs   = 160;
constexpr UINT kAnimTickMs = 8;
constexpr UINT kUnhoverMs  = 400;

constexpr float kOutsideMarginDip = 6.f;

// Entro questa distanza dal bordo la pastiglia insegue il cursore; entro la
// seconda, piu' stretta, si allunga e sporge di piu'. Due soglie e non una:
// seguire da lontano e' un accenno discreto, crescere da lontano sarebbe
// un'animazione che parte ogni volta che passi da quella parte dello schermo.
constexpr float kFollowDip = 190.f;
constexpr float kGrowDip   = 80.f;

// La pastiglia a riposo, e quanto si allunga quando il cursore si avvicina.
constexpr float kHandleDip     = 46.f;
constexpr float kHandleGrowDip = 30.f;

// Le gocce. La prima insegue in fretta e si assottiglia avvicinandosi — e' la
// tensione superficiale: piu' la tiri, piu' si stringe. La seconda e' piu'
// bassa, piu' larga e piu' lenta, e fa da scia.
constexpr float kDropMaxDip   = 17.f;
constexpr float kDropWideDip  = 30.f;   // semiampiezza da lontano
constexpr float kDropTightDip = 15.f;   // semiampiezza da vicino
constexpr float kDropFastTau  = 45.f;
constexpr float kDropSlowTau  = 150.f;

// Costante di tempo dell'inseguimento. Piu' e' bassa piu' e' reattivo; sotto i
// 40 ms smette di sembrare un liquido e comincia a sembrare un incollaggio.
constexpr float kFollowTauMs = 65.f;

// Apertura: parte decisa, supera di poco l'arrivo e rientra. E' quel rientro a
// far sembrare il movimento fluido invece che meccanico — un ease-out puro si
// posa in modo corretto ma inerte, come una cosa spenta che si ferma. Con
// l'oltrepasso sembra che la barra abbia una massa.
float EaseOutBack(float t) {
    t = std::clamp(t, 0.f, 1.f);
    constexpr float kOvershoot = 1.15f;
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

float SmoothStep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// Inseguimento indipendente dalla frequenza dei tick: con un fattore fisso per
// tick, la barra inseguirebbe piu' in fretta quando il polling e' veloce, cioe'
// proprio quando cambia passo. Cosi' invece la sensazione resta la stessa.
float Approach(float current, float target, float dtMs, float tauMs) {
    if (tauMs <= 0.f) return target;
    const float k = 1.f - std::exp(-dtMs / tauMs);
    return current + (target - current) * k;
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
    trigger_.SetConfig({70, 40});

    // Si parte nascosti, gia' fuori schermo, e senza mai rubare il focus.
    slide_ = slideTarget_ = 0.f;
    along_ = alongTarget_ = static_cast<float>(placement_.alongDefault);
    ApplySlide();
    shell::SetClickThrough(hwnd_, true);
    shell::ShowNoActivate(hwnd_);
    Redraw();

    SetCursorTick(kSlowTickMs);

    log::Debug(L"placement: superficie " + std::to_wstring(placement_.sizePx.cx) + L"x" +
              std::to_wstring(placement_.sizePx.cy) + L"  barra " +
              std::to_wstring(placement_.thicknessPx) + L"  goccia " +
              std::to_wstring(placement_.bulgeRoomPx) + L"  hidden.left " +
              std::to_wstring(placement_.hidden.left) + L"  work.right " +
              std::to_wstring(placement_.work.right));
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

void App::SetCursorTick(UINT intervalMs) {
    if (cursorTickMs_ == intervalMs) return;
    cursorTickMs_ = intervalMs;
    SetTimer(hwnd_, IDT_CURSOR, intervalMs, nullptr);
}

float App::EdgeDistanceDip(POINT cursor) const {
    if (!placement_.valid()) return 1e9f;
    const RECT& w = placement_.work;
    int px = 0;
    switch (placementCfg_.edge) {
        case Edge::Bottom: px = w.bottom - cursor.y; break;
        case Edge::Top:    px = cursor.y - w.top;    break;
        case Edge::Left:   px = cursor.x - w.left;   break;
        case Edge::Right:  px = w.right - cursor.x;  break;
    }
    // Fuori dall'area di lavoro sull'altro asse la barra non deve reagire: il
    // cursore e' sul bordo destro ma a meta' di un altro monitor.
    const bool alongOk = placement_.horizontal ? (cursor.x >= w.left && cursor.x < w.right)
                                               : (cursor.y >= w.top && cursor.y < w.bottom);
    if (!alongOk) return 1e9f;

    return static_cast<float>(std::max(0, px)) * 96.f /
           static_cast<float>(placement_.dpi ? placement_.dpi : 96);
}

int App::CursorAlong(POINT cursor) const {
    return placement_.horizontal ? cursor.x : cursor.y;
}

void App::UpdateAttraction(POINT cursor, float dtMs) {
    const float dist = EdgeDistanceDip(cursor);

    // Due soglie: `follow` fa inseguire, `grow` fa allungare e sporgere. La
    // seconda e' molto piu' stretta, altrimenti la barra si metterebbe a
    // gonfiarsi ogni volta che si passa da quella parte dello schermo.
    const float follow = 1.f - SmoothStep(0.f, kFollowDip, dist);
    grow_ = 1.f - SmoothStep(0.f, kGrowDip, dist);

    const int winLen = placement_.horizontal ? placement_.sizePx.cx : placement_.sizePx.cy;
    const int along  = CursorAlong(cursor);

    if (follow > 0.01f) {
        alongTarget_ = static_cast<float>(
            std::clamp(along - winLen / 2, placement_.alongMin, placement_.alongMax));
    }

    along_ = Approach(along_, alongTarget_, dtMs, kFollowTauMs);

    // Dove sta la pastiglia dentro la finestra. Vicino alle estremita' dello
    // schermo la finestra non puo' scorrere oltre: senza questo la pastiglia
    // resterebbe indietro proprio dove il cursore e' piu' facile da portare.
    const float winStart = along_;
    const float target   = winLen > 0
        ? std::clamp((static_cast<float>(along) - winStart) / static_cast<float>(winLen), 0.f, 1.f)
        : 0.5f;
    shapeCenter_ = follow > 0.01f ? Approach(shapeCenter_, target, dtMs, kFollowTauMs)
                                  : Approach(shapeCenter_, 0.5f, dtMs, kFollowTauMs * 3.f);

    // Avvicinandosi, la barra sporge un paio di pixel in piu': e' il richiamo,
    // e vale piu' di qualunque animazione dopo, perche' arriva prima.
    if (!animating_) slide_ = 0.12f * grow_;

    // Le gocce vivono in coordinate della superficie: il cursore in schermo
    // meno l'origine della finestra.
    const float local = static_cast<float>(along) - along_;
    dropFast_ = Approach(dropFast_, local, dtMs, kDropFastTau);
    dropSlow_ = Approach(dropSlow_, local, dtMs, kDropSlowTau);
}

void App::OnCursorTick() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return;

    const ULONGLONG now = GetTickCount64();
    const float dtMs = lastTick_ ? static_cast<float>(now - lastTick_) : static_cast<float>(kSlowTickMs);
    lastTick_ = now;

    if (state_ == BarState::Hidden) {
        // Da chiusa la barra segue il monitor sotto il cursore: su un portatile
        // con un monitor esterno, apparire sull'altro schermo e' inutile.
        RefreshPlacement(false);
        if (!placement_.valid()) return;

        // `near` non si puo' usare come nome: windows.h la definisce ancora
        // come macro vuota, retaggio dei puntatori a 16 bit, e la
        // dichiarazione sparirebbe lasciando un errore incomprensibile.
        const float dist = EdgeDistanceDip(cursor);

        // Passo veloce solo dove serve, con isteresi sulla soglia: un cursore
        // fermo proprio sul confine riarmerebbe il timer di sistema molte volte
        // al secondo per niente.
        const bool wasFast = (cursorTickMs_ == kFastTickMs);
        const bool atEdge  = wasFast ? (dist < kFollowDip * 1.25f) : (dist < kFollowDip);
        SetCursorTick(atEdge ? kFastTickMs : kSlowTickMs);

        UpdateAttraction(cursor, dtMs);
        ApplySlide();

        // Si ridisegna solo se la forma e' cambiata davvero. Inseguire il
        // cursore a 125 Hz ridisegnando ogni volta sarebbe lavoro sprecato per
        // frazioni di pixel.
        const float alongDip = kHandleDip + kHandleGrowDip * grow_;
        if (std::fabs(alongDip - drawnAlong_) > 0.3f ||
            std::fabs(shapeCenter_ - drawnCenter_) > 0.002f) {
            Redraw();
        }

        if (trigger_.Update(PointIn(placement_.trigger, cursor), cursor, now)) Reveal();
        return;
    }

    SetCursorTick(kSlowTickMs);

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
    Redraw();   // la forma cambia a ogni fotogramma: e' lei l'animazione

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

    // Dove sta il cursore lungo la barra: serve all'ingrandimento delle icone.
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
    const bool moved = (next.monitor != placement_.monitor) || sizeChanged;
    placement_ = next;

    if (sizeChanged) {
        renderer_.Resize(static_cast<UINT>(placement_.sizePx.cx),
                         static_cast<UINT>(placement_.sizePx.cy), placement_.dpi);
        Relayout();
        drawnAlong_ = drawnCenter_ = -1.f;   // la forma va comunque ridisegnata
    }

    // Cambiato monitor o geometria, l'inseguimento non puo' continuare da dove
    // era: quelle coordinate appartenevano a un altro schermo.
    if (moved) {
        along_ = alongTarget_ = static_cast<float>(placement_.alongDefault);
        shapeCenter_ = 0.5f;
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

    // Lo spazio delle gocce non e' barra: il contenuto va spostato dentro lo
    // spessore vero, altrimenti le icone galleggerebbero davanti al bordo.
    const float room = static_cast<float>(placement_.bulgeRoomPx) * scale;
    float offX = 0.f, offY = 0.f;
    switch (placementCfg_.edge) {
        case Edge::Right:  offX = room; break;   // la barra sta a destra della superficie
        case Edge::Bottom: offY = room; break;
        case Edge::Left:                         // la barra sta gia' all'inizio
        case Edge::Top:    break;
    }

    const float availW = Vertical() ? (w - room) : w;
    const float availH = Vertical() ? h : (h - room);

    const ui::RectF bounds{offX + padX, offY + padY,
                           std::max(0.f, availW - padX * 2.f),
                           std::max(0.f, availH - padY * 2.f)};

    ui::Layout(root_, bounds, m);

    // I rettangoli sono cambiati: l'hover va ricalcolato al prossimo movimento
    // del mouse, non tenuto da quello di prima.
    hoveredId_.clear();
}

void App::Redraw() {
    if (!renderer_.Valid() || !placement_.valid()) return;

    const float scale   = 96.f / static_cast<float>(placement_.dpi ? placement_.dpi : 96);
    const float fullDip = static_cast<float>(placement_.horizontal ? placement_.sizePx.cx
                                                                  : placement_.sizePx.cy) * scale;

    // La pastiglia a riposo, gia' cresciuta di quanto il cursore e' vicino.
    const float handleDip = kHandleDip + kHandleGrowDip * grow_;

    // L'allungamento parte solo dopo lo sporgere: sotto quella soglia lo
    // scorrimento e' il richiamo, non l'apertura, e la forma non deve muoversi.
    const float expand  = SmoothStep(0.15f, 1.f, slide_);
    const float along   = handleDip + (fullDip - handleDip) * expand;

    const float scaleBack = static_cast<float>(placement_.dpi ? placement_.dpi : 96) / 96.f;

    render::DrawState state;
    state.hovered      = hoveredId_;
    state.pressed      = pressedId_;
    state.opacity      = 1.f;
    state.edge         = placementCfg_.edge;
    state.shapeAlong   = along;
    state.shapeCenter  = shapeCenter_;
    state.barThickness = static_cast<float>(placement_.thicknessPx) / scaleBack;
    state.cursorAlong  = cursorAlong_;
    state.magnify      = magnify_;

    // Le gocce si vedono solo a barra chiusa o quasi: aperta, il richiamo
    // l'hanno gia' fatto le icone, e un profilo che continua a ondeggiare
    // mentre stai cercando di premere un bottone e' solo rumore.
    const float dropStrength = grow_ * (1.f - SmoothStep(0.05f, 0.45f, slide_));
    if (dropStrength > 0.02f) {
        // Piu' il cursore e' vicino, piu' la goccia e' alta e stretta: e' la
        // tensione superficiale, piu' la tiri piu' si stringe.
        const float wideBase = kDropWideDip + (kDropTightDip - kDropWideDip) * grow_;

        // Non piu' larga della forma che la ospita: a riposo la pastiglia e'
        // corta, e una goccia larga quanto lei non sarebbe una goccia.
        const float wMax = std::max(6.f, along * 0.26f);

        state.bulges[0] = {dropFast_, kDropMaxDip * dropStrength,
                           std::min(wideBase, wMax)};
        state.bulges[1] = {dropSlow_, kDropMaxDip * 0.45f * dropStrength,
                           std::min(wideBase * 1.15f, wMax)};
        state.bulgeCount = 2;
    }
    // Le icone compaiono quando c'e' spazio per contenerle, non prima: dentro
    // la pastiglia corta sarebbero un ammasso.
    state.contentAlpha = SmoothStep(0.45f, 0.95f, slide_);

    drawnAlong_  = handleDip;
    drawnCenter_ = shapeCenter_;
    // A riposo questa riga non deve comparire piu' di una volta ogni tanto: se
    // il log si riempie di disegni a barra ferma, la guardia sopra e' rotta ed
    // e' li' che se ne va la CPU.
    if (log::Enabled(log::Level::Debug)) {
        log::Debug(L"disegno: lunghezza " + std::to_wstring(static_cast<int>(along)) +
                   L"  centro " + std::to_wstring(shapeCenter_) +
                   L"  scorrimento " + std::to_wstring(slide_));
    }

    renderer_.Draw(root_, state);
}

void App::ApplySlide() {
    if (!placement_.valid()) return;

    const RECT r = shell::Slide(placement_, slide_, static_cast<int>(along_ + 0.5f));

    // Solo se si e' mossa davvero. A riposo il tick del cursore passa di qui
    // dieci volte al secondo con lo stesso rettangolo, e una SetWindowPos su
    // una finestra topmost non e' gratis nemmeno quando non sposta niente: era
    // lo 0,2 % di un core speso per riscrivere la stessa posizione.
    if (r.left == lastRect_.left && r.top == lastRect_.top &&
        r.right == lastRect_.right && r.bottom == lastRect_.bottom)
        return;

    lastRect_ = r;
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
