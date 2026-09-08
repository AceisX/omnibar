// trigger.h — decide se un movimento del mouse sul bordo e' un'apertura o un
// passaggio.
//
// E' la parte che decide se la barra e' piacevole o insopportabile dopo due
// giorni (docs/architecture.md §8.1). Un cursore che sfiora il bordo mentre va
// altrove non deve aprire niente; un cursore che ci si ferma sopra deve aprire
// subito.
//
// Due soglie insieme, non una: il cursore deve restare nella zona per
// `delayMs` E aver percorso meno di `travelPx` in quel tempo. Con la sola
// soglia di tempo, un cursore che scivola lungo il bordo verso la taskbar apre
// la barra ogni volta. Con la sola soglia di distanza, un cursore fermo sul
// bordo apre la barra istantaneamente anche quando ci e' finito per sbaglio.
//
// Logica pura, senza Win32: si prova con dei numeri, non con un mouse. E'
// l'unico modo di regolare queste soglie senza impazzire.
#pragma once
#include "core/common.h"

namespace omni::shell {

class TriggerDetector {
public:
    struct Config {
        int delayMs  = 180;  // quanto deve restare fermo nella zona
        int travelPx = 24;   // quanto puo' muoversi in quel tempo senza azzerare
    };

    explicit TriggerDetector(Config cfg = {}) : cfg_(cfg) {}

    void SetConfig(Config cfg) { cfg_ = cfg; }
    void Reset();

    // Da chiamare a ogni tick del polling del cursore. Torna true nell'istante
    // in cui la condizione di apertura e' soddisfatta; poi resta false finche'
    // il cursore non esce e rientra, cosi' il chiamante non deve ricordarsi
    // niente.
    bool Update(bool insideZone, POINT cursor, ULONGLONG nowMs);

    // Quanto manca all'apertura, 0-1. Serve a mostrare un'attesa se un giorno
    // si vorra': oggi non la usa nessuno, ma e' l'unico posto che la sa.
    float Progress(ULONGLONG nowMs) const;

private:
    Config    cfg_;
    bool      inside_    = false;
    bool      fired_     = false;
    ULONGLONG dwellFrom_ = 0;
    POINT     last_{};
    float     travel_    = 0.f;
};

}  // namespace omni::shell
