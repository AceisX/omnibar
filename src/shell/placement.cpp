#include "shell/placement.h"

#include "core/log.h"

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

namespace {

struct Elenco {
    HMONITOR              scelto;
    std::vector<std::wstring> righe;
};

BOOL CALLBACK RaccogliMonitor(HMONITOR h, HDC, LPRECT, LPARAM param) {
    auto* e = reinterpret_cast<Elenco*>(param);

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(h, &mi)) return TRUE;

    const RECT& m = mi.rcMonitor;
    const RECT& w = mi.rcWork;

    std::wstring riga = std::wstring(mi.szDevice) + L"  schermo " +
                        std::to_wstring(m.right - m.left) + L"x" +
                        std::to_wstring(m.bottom - m.top) + L" a " +
                        std::to_wstring(m.left) + L"," + std::to_wstring(m.top) +
                        L"  lavoro " + std::to_wstring(w.right - w.left) + L"x" +
                        std::to_wstring(w.bottom - w.top) +
                        L"  DPI " + std::to_wstring(DpiFor(h));
    if (mi.dwFlags & MONITORINFOF_PRIMARY) riga += L"  [primario]";
    if (h == e->scelto)                    riga += L"  <-- la barra sta qui";

    e->righe.push_back(std::move(riga));
    return TRUE;
}

}  // namespace

void LogMonitors(HMONITOR chosen) {
    if (!log::Enabled(log::Level::Debug)) return;

    Elenco e{chosen, {}};
    EnumDisplayMonitors(nullptr, nullptr, RaccogliMonitor, reinterpret_cast<LPARAM>(&e));

    log::Debug(L"schermi visti da Windows: " + std::to_wstring(e.righe.size()));
    for (const std::wstring& r : e.righe) log::Debug(L"  " + r);
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

    // Quanto e' occupato il NOSTRO bordo da qualcos'altro — quasi sempre la
    // taskbar, ma vale per qualunque barra agganciata.
    const RECT screen = mi.rcMonitor;
    int occupato = 0;
    switch (cfg.edge) {
        case Edge::Bottom: occupato = screen.bottom - p.work.bottom; break;
        case Edge::Top:    occupato = p.work.top - screen.top;       break;
        case Edge::Left:   occupato = p.work.left - screen.left;     break;
        case Edge::Right:  occupato = screen.right - p.work.right;   break;
    }

    // Se quel bordo e' libero, la zona sensibile puo' essere sottilissima: il
    // cursore ci sbatte contro e si ferma da solo, perche' oltre non c'e'
    // schermo. E' il trucco su cui si reggono tutti i bersagli sui bordi.
    //
    // Se invece c'e' la taskbar, quel trucco non c'e' piu': il bordo della
    // nostra zona sta in mezzo allo schermo, e per colpirlo bisogna FERMARSI
    // nel punto giusto invece di lanciare il mouse. Sei pixel diventano
    // impossibili. Con la taskbar davanti la zona si allarga: non e' una
    // taratura, e' un problema diverso.
    int trigger = std::max(1, cfg.triggerPx);
    if (occupato > 0) trigger = std::max(trigger, Dip(20.f, p.dpi));

    p.horizontal  = (cfg.edge == Edge::Bottom || cfg.edge == Edge::Top);
    p.thicknessPx = thickness;

    // La finestra copre tutto il bordo, per tutta la sua lunghezza. Non si
    // muove mai: sta sopra l'area di lavoro e la sua superficie e' quasi tutta
    // trasparente, e i pixel a alpha zero non si vedono e non ricevono click.
    // La barra aperta non deve nemmeno riservare spazio: si appoggia sopra.
    switch (cfg.edge) {
        case Edge::Bottom:
            p.rect    = {p.work.left, p.work.bottom - thickness, p.work.right, p.work.bottom};
            p.trigger = {p.work.left, p.work.bottom - trigger,   p.work.right, p.work.bottom};
            break;

        case Edge::Top:
            p.rect    = {p.work.left, p.work.top, p.work.right, p.work.top + thickness};
            p.trigger = {p.work.left, p.work.top, p.work.right, p.work.top + trigger};
            break;

        case Edge::Left:
            p.rect    = {p.work.left, p.work.top, p.work.left + thickness, p.work.bottom};
            p.trigger = {p.work.left, p.work.top, p.work.left + trigger,   p.work.bottom};
            break;

        case Edge::Right:
            p.rect    = {p.work.right - thickness, p.work.top, p.work.right, p.work.bottom};
            p.trigger = {p.work.right - trigger,   p.work.top, p.work.right, p.work.bottom};
            break;
    }

    p.sizePx = SIZE{p.rect.right - p.rect.left, p.rect.bottom - p.rect.top};
    return p;
}

}  // namespace omni::shell
