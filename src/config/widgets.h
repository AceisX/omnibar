// widgets.h — costruisce l'albero dei widget da una descrizione TOML.
//
// E' il tier dichiarativo (docs/extension-api.md §1.1): il modo di aggiungere
// bottoni alla barra senza scrivere codice. Copre la maggior parte dei casi
// reali, che sono "un bottone, una scorciatoia".
//
// Il risultato e' esattamente lo stesso `ui::Widget` che produce un modulo
// built-in in C++. Da li' in giu' nessuno sa piu' chi l'ha creato, ed e' il
// punto: il vocabolario e' uno solo (architettura §6).
//
// Ogni cosa che non torna diventa un problema con la sua riga, e il widget che
// la contiene viene saltato. Il resto della barra si costruisce lo stesso: un
// bottone scritto male non deve poter svuotare la barra.
#pragma once
#include "config/toml.h"
#include "ui/widget.h"

namespace omni::config {

// Costruisce i figli della barra dall'array `[[widget]]`. Se l'array non c'e' o
// e' vuoto, torna un elenco vuoto e il chiamante decide cosa metterci.
std::vector<ui::Widget> BuildWidgets(const toml::Value& list,
                                     std::vector<toml::Error>& problems);

// Un'azione da una tabella `{ kind = "...", ... }`. Esposta perche' e' la parte
// che si sbaglia piu' facilmente e va provata da sola.
ui::Action BuildAction(const toml::Value& v, std::vector<toml::Error>& problems);

}  // namespace omni::config
