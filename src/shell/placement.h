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
    Edge  edge         = Edge::Right;

    // Quanto occupa sul bordo lungo, in % dell'area di lavoro. **0 = auto**:
    // la barra e' lunga quanto il suo contenuto e non un dito di piu'. E' il
    // default perche' una barra che occupa il 60 % dello schermo per mostrare
    // otto icone non e' minimal, e' solo grande.
    float extentPct    = 0.f;
    float maxExtentPct = 85.f;   // limite dell'auto: oltre, si va in overflow

    float thicknessDip = 44.f;   // spessore, in DIP
    int   triggerPx    = 4;      // spessore della zona sensibile sul bordo
    int   align        = 0;      // -1 inizio, 0 centro, +1 fine

    // La linguetta: quanti pixel della barra restano visibili a riposo. Serve
    // a farsi trovare da chi non sa che la barra c'e'. Con `peek = false` la
    // finestra sparisce del tutto e resta solo la zona sensibile.
    bool  peek         = true;
    int   peekPx       = 5;
};

struct Placement {
    HMONITOR monitor = nullptr;
    UINT     dpi     = 96;
    RECT     work{};      // area di lavoro del monitor (taskbar esclusa)
    SIZE     sizePx{};    // dimensione della finestra, in pixel

    RECT revealed{};      // posizione a barra aperta
    RECT hidden{};         // posizione a riposo: ne restano dentro `peekPx` pixel
    RECT trigger{};        // la zona sensibile: tutto il bordo, in coordinate schermo
    int  peekPx = 0;       // quanto sporge a riposo; 0 = fuori schermo del tutto

    // La barra non e' inchiodata al centro del bordo: scorre lungo di esso per
    // andare incontro al cursore. `revealed` e `hidden` sono calcolati con la
    // barra centrata, e chi disegna passa la posizione vera a Slide().
    bool horizontal   = true;   // il bordo lungo e' orizzontale
    int  alongDefault = 0;      // coordinata lungo il bordo, a barra centrata
    int  alongMin     = 0;      // limiti entro cui puo' scorrere senza uscire
    int  alongMax     = 0;

    bool valid() const { return monitor != nullptr && sizePx.cx > 0 && sizePx.cy > 0; }
};

// Il monitor sotto il cursore, o il primario se il cursore non e' su nessuno.
HMONITOR MonitorUnderCursor();
HMONITOR PrimaryMonitor();

// Calcola tutto per un monitor. `contentExtentDip` e' quanto misura l'albero
// dei widget sull'asse lungo, e serve solo quando `extentPct` e' 0 (auto). Se
// il monitor non e' valido torna un Placement non valido: il chiamante non deve
// indovinare.
Placement Compute(const PlacementConfig& cfg, HMONITOR monitor, float contentExtentDip = 0.f);

// La posizione della finestra a un dato avanzamento dello scorrimento:
// 0 = chiusa, 1 = aperta. E' un'interpolazione fra hidden e revealed, quindi
// lo scorrimento e' un SetWindowPos e non richiede di ridisegnare la
// superficie: e' li' che si guadagna lo "zero ridisegni durante l'animazione".
// `alongPx` e' la coordinata lungo il bordo: la x su un bordo orizzontale, la y
// su uno laterale. Viene limitata ai valori ammessi, cosi' chi chiama puo'
// passare la posizione del cursore senza doverla ritagliare a mano.
RECT Slide(const Placement& p, float t, int alongPx);

}  // namespace omni::shell
