// paths.h — dove vivono configurazione, estensioni, stato e log.
//
// Due modalita' (architecture.md §14): se accanto all'eseguibile c'e'
// `portable.flag`, tutto sta li' e non si tocca %APPDATA%. Altrimenti si usa
// %APPDATA%\OmniBar\, che e' il default perche' l'obiettivo e' l'installazione
// di massa. La scelta si fa una volta all'avvio e non cambia piu'.
#pragma once
#include "common.h"

namespace omni::paths {

// Da chiamare una volta all'avvio, prima di qualunque altra funzione qui.
// Crea le cartelle che mancano. Falso se non e' stato possibile: in quel caso
// l'host parte comunque, in sola lettura, e lo dice nel log.
bool Init();

bool Portable();  // valida solo dopo Init()

const std::wstring& ExeDir();      // cartella dell'eseguibile, con backslash finale
const std::wstring& Root();        // radice dei dati (portable o %APPDATA%\OmniBar\)
const std::wstring& Profiles();    // Root() + "profiles\"
const std::wstring& Extensions();  // Root() + "extensions\"
const std::wstring& State();       // Root() + "state\"   -- dati, non impostazioni
const std::wstring& Logs();        // Root() + "logs\"

std::wstring ConfigFile();         // Root() + "omnibar.toml"

}  // namespace omni::paths
