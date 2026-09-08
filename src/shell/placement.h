// placement.h — dove sta la barra, su quale monitor, e dove scorre.
//
// Regola ereditata da MiniBar e non negoziabile: i rettangoli non si
// memorizzano mai. Si ricalcolano a ogni notifica della taskbar, a ogni cambio
// di DPI e a ogni cambio di risoluzione. Su Windows 11 con monitor a DPI
// diversi i valori memorizzati arrivano sfalsati, e una barra fuori posto e'
// peggio di una barra assente.
#pragma once
#include "core/common.h"

namespace omni::shell {

struct PlacementConfig {
    Edge  edge         = Edge::Bottom;
    float extentPct    = 60.f;   // quanto occupa sul bordo lungo, in % dell'area di lavoro
    float thicknessDip = 52.f;   // spessore, in DIP
    int   triggerPx    = 3;      // quanti pixel restano visibili da nascosta
    int   align        = 0;      // -1 inizio, 0 centro, +1 fine
};

struct Placement {
    HMONITOR monitor = nullptr;
    UINT     dpi     = 96;
    RECT     work{};      // area di lavoro del monitor (taskbar esclusa)
    SIZE     sizePx{};    // dimensione della finestra, in pixel

    RECT revealed{};      // posizione a barra aperta
    RECT hidden{};        // posizione a barra chiusa: sporge di triggerPx
    RECT trigger{};       // la zona sensibile sul bordo, in coordinate schermo

    bool valid() const { return monitor != nullptr && sizePx.cx > 0 && sizePx.cy > 0; }
};

// Il monitor sotto il cursore, o il primario se il cursore non e' su nessuno.
HMONITOR MonitorUnderCursor();
HMONITOR PrimaryMonitor();

// Calcola tutto per un monitor. Se il monitor non e' valido torna un Placement
// non valido: il chiamante non deve indovinare.
Placement Compute(const PlacementConfig& cfg, HMONITOR monitor);

// La posizione della finestra a un dato avanzamento dello scorrimento:
// 0 = chiusa, 1 = aperta. E' un'interpolazione fra hidden e revealed, quindi
// lo scorrimento e' un SetWindowPos e non richiede di ridisegnare la
// superficie: e' li' che si guadagna lo "zero ridisegni durante l'animazione".
RECT Slide(const Placement& p, float t);

}  // namespace omni::shell
