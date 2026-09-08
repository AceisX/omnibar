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

Placement Compute(const PlacementConfig& cfg, HMONITOR monitor) {
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

    const int thickness = Dip(cfg.thicknessDip, p.dpi);
    const float pct     = std::clamp(cfg.extentPct, 10.f, 100.f) / 100.f;
    const bool  horizontal = (cfg.edge == Edge::Bottom || cfg.edge == Edge::Top);

    const int extent = static_cast<int>(static_cast<float>(horizontal ? workW : workH) * pct);

    p.sizePx = horizontal ? SIZE{extent, thickness} : SIZE{thickness, extent};

    // Posizione lungo il bordo, secondo l'allineamento.
    const int span   = horizontal ? workW : workH;
    const int origin = horizontal ? p.work.left : p.work.top;
    int along = origin;
    if (cfg.align == 0)      along = origin + (span - extent) / 2;
    else if (cfg.align > 0)  along = origin + (span - extent);

    const int trigger = std::max(1, cfg.triggerPx);
    const RECT screen = mi.rcMonitor;

    // Tre rettangoli, tre sistemi di riferimento diversi, e non e' un caso:
    //
    //  - `revealed` sta nell'AREA DI LAVORO: la barra aperta si ferma sopra la
    //    taskbar invece di coprirla.
    //  - `hidden` sta fuori dal MONITOR: da chiusa la finestra e' interamente
    //    fuori dallo schermo. Fermarla appena oltre l'area di lavoro
    //    significherebbe lasciarne un pezzo sopra la taskbar — ed essendo
    //    topmost, la coprirebbe.
    //  - `trigger` sta nell'area di lavoro ed e' solo una zona per il polling
    //    del cursore: nessuna finestra ci vive dentro, quindi non intercetta
    //    click ne' disturba i gesti di bordo di Windows.

    switch (cfg.edge) {
        case Edge::Bottom:
            p.revealed = {along, p.work.bottom - thickness, along + extent, p.work.bottom};
            p.hidden   = {along, screen.bottom, along + extent, screen.bottom + thickness};
            p.trigger  = {along, p.work.bottom - trigger, along + extent, p.work.bottom};
            break;

        case Edge::Top:
            p.revealed = {along, p.work.top, along + extent, p.work.top + thickness};
            p.hidden   = {along, screen.top - thickness, along + extent, screen.top};
            p.trigger  = {along, p.work.top, along + extent, p.work.top + trigger};
            break;

        case Edge::Left:
            p.revealed = {p.work.left, along, p.work.left + thickness, along + extent};
            p.hidden   = {screen.left - thickness, along, screen.left, along + extent};
            p.trigger  = {p.work.left, along, p.work.left + trigger, along + extent};
            break;

        case Edge::Right:
            p.revealed = {p.work.right - thickness, along, p.work.right, along + extent};
            p.hidden   = {screen.right, along, screen.right + thickness, along + extent};
            p.trigger  = {p.work.right - trigger, along, p.work.right, along + extent};
            break;
    }

    return p;
}

RECT Slide(const Placement& p, float t) {
    t = std::clamp(t, 0.f, 1.f);
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
