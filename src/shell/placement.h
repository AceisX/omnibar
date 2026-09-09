// placement.h — dove sta la barra e quanto e' grande.
//
// Regola ereditata da MiniBar e non negoziabile: i rettangoli non si
// memorizzano mai. Si ricalcolano a ogni notifica della taskbar, a ogni cambio
// di DPI e a ogni cambio di risoluzione. Su Windows 11 con monitor a DPI
// diversi i valori memorizzati arrivano sfalsati, e una barra fuori posto e'
// peggio di una barra assente.
//
// **La finestra non si muove mai.** Occupa tutto il bordo, per tutta la sua
// lunghezza, e resta li'. Ad aprirsi e chiudersi e' cio' che ci viene disegnato
// dentro, non la finestra.
//
// Prima scorreva: entrava dal bordo, e per giunta inseguiva il cursore lungo di
// esso. Erano due movimenti che l'occhio legge come "l'oggetto si sposta", e un
// componente di sistema non si sposta — sta dov'e' e cambia forma. Toglierli ha
// anche cancellato una categoria intera di problemi: niente piu' rettangoli
// interpolati, niente SetWindowPos a ogni fotogramma, niente coordinate che
// devono restare in fase fra loro.
#pragma once
#include "core/common.h"

namespace omni::shell {

struct PlacementConfig {
    Edge edge = Edge::Right;

    // Lo spessore della barra aperta, in DIP. E' anche lo spessore della
    // finestra: a riposo il resto della superficie e' trasparente.
    float thicknessDip = 44.f;

    // Lunghezza del pannello aperto: 0 = quanto il contenuto, altrimenti una
    // percentuale dell'area di lavoro. La finestra copre comunque tutto il
    // bordo: questo dice quanto ne occupa la parte disegnata.
    float extentPct    = 0.f;
    float maxExtentPct = 85.f;
    int   align        = 0;      // -1 inizio, 0 centro, +1 fine

    // La linea sempre visibile sul bordo, e la sporgenza al suo centro.
    float lineDip      = 2.f;    // spessore della linea
    float nubThickDip  = 7.f;    // quanto sporge la sporgenza verso il centro
    // Piu' alta di prima: a cinquantaquattro punti si leggeva come un difetto
    // della linea, non come una maniglia. Deve avere una lunghezza propria.
    float nubLenDip    = 78.f;

    int   triggerPx    = 6;      // spessore della zona sensibile sul bordo
};

struct Placement {
    HMONITOR monitor = nullptr;
    UINT     dpi     = 96;
    RECT     work{};        // area di lavoro del monitor (taskbar esclusa)
    SIZE     sizePx{};      // dimensione della finestra, in pixel

    RECT rect{};            // la finestra: fissa, per tutto il bordo
    RECT trigger{};         // la zona sensibile, in coordinate schermo

    bool horizontal  = true;
    int  thicknessPx = 0;

    bool valid() const { return monitor != nullptr && sizePx.cx > 0 && sizePx.cy > 0; }
};

// Il monitor sotto il cursore, o il primario se il cursore non e' su nessuno.
HMONITOR MonitorUnderCursor();
HMONITOR PrimaryMonitor();

// Scrive nel log tutti gli schermi che Windows presenta, con area di lavoro e
// DPI, e segna quello scelto. Su una macchina con piu' monitor e' la prima cosa
// da guardare quando la barra compare dove non dovrebbe: dice se il problema e'
// nella nostra scelta o in cosa Windows sta riportando.
void LogMonitors(HMONITOR chosen);

// Calcola tutto per un monitor. Se il monitor non e' valido torna un Placement
// non valido: il chiamante non deve indovinare.
Placement Compute(const PlacementConfig& cfg, HMONITOR monitor);

}  // namespace omni::shell
