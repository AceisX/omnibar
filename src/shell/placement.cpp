#include "shell/placement.h"

#include <shellscalingapi.h>

#include <algorithm>

namespace omni::shell {
namespace {

int Dip(float dip, UINT dpi) {
    return static_cast<int>(dip * static_cast<float>(dpi) / 96.f + 0.5f);
}

UINT DpiFor(HMONITOR monitor) {
    UINT x = 96, y = 96;
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y)) && x) return x;
    return 96;
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

}  // namespace

HMONITOR MonitorUnderCursor() {
    POINT pt{};
    if (!GetCursorPos(&pt)) return PrimaryMonitor();
    return MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
}

HMONITOR PrimaryMonitor() {
    const POINT origin{0, 0};
    return MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
}

Placement Compute(const PlacementConfig& cfg, HMONITOR monitor, float contentExtentDip) {
    Placement p;
    if (!monitor) return p;

    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(monitor, &mi)) return p;

    p.monitor = monitor;
    p.dpi     = DpiFor(monitor);
    p.work    = mi.rcWork;

    const int workW = p.work.right - p.work.left;
    const int workH = p.work.bottom - p.work.top;
    if (workW <= 0 || workH <= 0) return p;

    const int  thickness  = Dip(cfg.thicknessDip, p.dpi);
    const bool horizontal = (cfg.edge == Edge::Bottom || cfg.edge == Edge::Top);
    const int  span       = horizontal ? workW : workH;

    // Lunghezza: o una percentuale dello schermo, o — ed e' il default — quanto
    // misura il contenuto, con un tetto oltre il quale si va in overflow.
    int extent;
    if (cfg.extentPct > 0.f) {
        extent = static_cast<int>(static_cast<float>(span) *
                                  std::clamp(cfg.extentPct, 10.f, 100.f) / 100.f);
    } else {
        const int wanted = Dip(std::max(0.f, contentExtentDip), p.dpi);
        const int cap    = static_cast<int>(static_cast<float>(span) *
                                            std::clamp(cfg.maxExtentPct, 20.f, 100.f) / 100.f);
        // Un minimo c'e' comunque: una barra piu' corta del suo spessore non
        // sembra una barra, sembra un errore.
        extent = std::clamp(wanted, thickness * 2, cap);
    }

    p.sizePx = horizontal ? SIZE{extent, thickness} : SIZE{thickness, extent};

    // Posizione lungo il bordo, secondo l'allineamento.
    const int origin = horizontal ? p.work.left : p.work.top;
    int along = origin;
    if (cfg.align == 0)      along = origin + (span - extent) / 2;
    else if (cfg.align > 0)  along = origin + (span - extent);

    const int trigger = std::max(1, cfg.triggerPx);

    // Quanto della finestra resta dentro l'area di lavoro a riposo. Con la
    // linguetta accesa e' `peekPx`; senza, e' zero e la finestra esce
    // completamente dal monitor.
    const int peek = cfg.peek ? std::max(2, cfg.peekPx) : 0;
    p.peekPx = peek;

    // Tre rettangoli, e i sistemi di riferimento sono diversi apposta:
    //
    //  - `revealed` sta nell'AREA DI LAVORO: la barra aperta si ferma sopra la
    //    taskbar invece di coprirla.
    //  - `hidden` e' posizionato perche' ne restino dentro esattamente `peek`
    //    pixel. Il resto della finestra sborda oltre l'area di lavoro e finisce
    //    sopra la taskbar — ma li' la superficie e' interamente trasparente
    //    (DrawMode::Handle), e i pixel a alpha zero non si vedono e non
    //    ricevono click. Senza linguetta la finestra esce dal monitor del tutto.
    //  - `trigger` e' solo una zona per il polling del cursore: nessuna
    //    finestra ci vive dentro, quindi non intercetta click ne' disturba i
    //    gesti di bordo di Windows.

    const RECT screen = mi.rcMonitor;

    switch (cfg.edge) {
        case Edge::Bottom: {
            const int top = peek ? p.work.bottom - peek : screen.bottom;
            p.revealed = {along, p.work.bottom - thickness, along + extent, p.work.bottom};
            p.hidden   = {along, top, along + extent, top + thickness};
            p.trigger  = {along, p.work.bottom - trigger, along + extent, p.work.bottom};
            break;
        }

        case Edge::Top: {
            const int bottom = peek ? p.work.top + peek : screen.top;
            p.revealed = {along, p.work.top, along + extent, p.work.top + thickness};
            p.hidden   = {along, bottom - thickness, along + extent, bottom};
            p.trigger  = {along, p.work.top, along + extent, p.work.top + trigger};
            break;
        }

        case Edge::Left: {
            const int right = peek ? p.work.left + peek : screen.left;
            p.revealed = {p.work.left, along, p.work.left + thickness, along + extent};
            p.hidden   = {right - thickness, along, right, along + extent};
            p.trigger  = {p.work.left, along, p.work.left + trigger, along + extent};
            break;
        }

        case Edge::Right: {
            const int left = peek ? p.work.right - peek : screen.right;
            p.revealed = {p.work.right - thickness, along, p.work.right, along + extent};
            p.hidden   = {left, along, left + thickness, along + extent};
            p.trigger  = {p.work.right - trigger, along, p.work.right, along + extent};
            break;
        }
    }

    return p;
}

RECT Slide(const Placement& p, float t) {
    // Si accetta un po' oltre l'arrivo: la curva di apertura supera l'1 e
    // rientra, ed e' quel rientro a far sembrare il movimento fluido invece
    // che meccanico. Un limite c'e' comunque, perche' un errore di calcolo non
    // deve poter spedire la barra in mezzo allo schermo.
    t = std::clamp(t, -0.2f, 1.25f);
    const float x = Lerp(static_cast<float>(p.hidden.left), static_cast<float>(p.revealed.left), t);
    const float y = Lerp(static_cast<float>(p.hidden.top),  static_cast<float>(p.revealed.top),  t);

    RECT r;
    r.left   = static_cast<LONG>(x + 0.5f);
    r.top    = static_cast<LONG>(y + 0.5f);
    r.right  = r.left + p.sizePx.cx;
    r.bottom = r.top + p.sizePx.cy;
    return r;
}

}  // namespace omni::shell
