// config.h — le impostazioni della barra, lette dal TOML.
//
// Due principi, entrambi dalla §14 dell'architettura.
//
// **Un file sbagliato non deve spegnere la barra.** Ogni valore fuori posto
// viene segnalato e sostituito col default; il resto del file si applica lo
// stesso. Una barra che sparisce perche' hai scritto `opacity = 1.5` e' un bug,
// non un messaggio d'errore.
//
// **I nostri valori sono default, non decisioni** (§7.4). Tutto quello che c'e'
// qui e' sovrascrivibile, e la finestra di configurazione della fase 6 scrivera'
// in questo stesso formato.
#pragma once
#include "core/common.h"
#include "config/toml.h"
#include "ui/widget.h"

namespace omni::config {

enum class ThemeMode { Auto, Dark, Light };

struct Config {
    // ── [bar] ──
    Edge  edge          = Edge::Right;
    float extentPct     = 0.f;     // 0 = lunga quanto il contenuto
    float maxExtentPct  = 85.f;
    float thicknessDip  = 44.f;
    float lineDip       = 2.f;
    float nubThickDip   = 7.f;
    float nubLenDip     = 78.f;
    int   triggerPx     = 6;
    float cornerRadius  = 20.f;
    float opacity       = 0.85f;
    ThemeMode theme     = ThemeMode::Auto;
    int   align         = 0;       // -1 inizio, 0 centro, +1 fine

    // Su quale schermo appare. Seguire il cursore e' il default perche' su piu'
    // monitor la barra serve dove stai guardando, non dove sta il monitor
    // principale.
    bool  monitorFollowsCursor = true;

    // ── [reveal] ──
    int  delayMs    = 90;
    int  travelPx   = 40;
    int  unhoverMs  = 200;
    bool onHover    = true;

    // ── [colors] ──
    bool  accentFromSystem = true;
    float accentR = 0.f, accentG = 0.f, accentB = 0.f;   // usati solo se non e' "system"
    float soften  = 0.20f;

    // ── [log] ──
    int logLevel = 2;   // 0 trace, 1 debug, 2 info, 3 warn, 4 error

    // ── [[widget]] ──
    //
    // I widget della barra, nell'ordine in cui sono scritti. Vuoto significa
    // "non e' stato deciso niente": in quel caso la barra usa il proprio
    // contenuto di partenza, invece di comparire vuota.
    std::vector<ui::Widget> widgets;
};

// Legge il file, se c'e'. Cio' che non e' scritto resta al default, cio' che e'
// scritto male viene segnalato in `problems` e resta al default.
Config Load(const std::wstring& path, std::vector<toml::Error>& problems);

// Scrive nel log gli eventuali problemi, con file e riga. Una funzione a parte
// perche' la stessa lista serve anche alla finestra di configurazione, che li
// mostrera' invece di scriverli.
void Report(const std::wstring& path, const std::vector<toml::Error>& problems);

}  // namespace omni::config
