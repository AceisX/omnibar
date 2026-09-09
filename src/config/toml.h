// toml.h — lettore di un sottoinsieme di TOML.
//
// Perche' scritto invece che preso da una libreria: l'host non ha dipendenze
// oltre al Windows SDK (architettura §4), e soprattutto gli errori li vogliamo
// nostri. Un parser generico dice `expected '='`; qui serve dire *quale file*,
// *quale riga* e *cosa ci si aspettava*, in una lingua che chi ha scritto il
// file capisca — perche' la configurazione la scrivera' gente che non
// programma.
//
// Cosa capisce:
//   commenti (#), tabelle [a.b], array di tabelle [[a.b]], chiavi puntate
//   (a.b = 1), stringhe base ("..." con escape) e letterali ('...'), interi,
//   decimali, booleani, array anche su piu' righe, tabelle in linea {a = 1}.
//
// Cosa NON capisce, di proposito: date e orari, interi in altre basi, stringhe
// multilinea. Non servono a un file di configurazione di una barra, e ogni
// costrutto in piu' e' codice che puo' sbagliare su input che nessuno scrivera'.
//
// **L'ordine delle chiavi si conserva.** Non e' un dettaglio: la barra dispone i
// widget nell'ordine in cui sono scritti, e un giorno un editor dovra' poter
// riscrivere questo file senza rimescolarlo. Una mappa ordinata per nome
// renderebbe impossibili entrambe le cose.
#pragma once
#include "core/common.h"

namespace omni::toml {

enum class Type { None, String, Integer, Float, Boolean, Array, Table };

class Value {
public:
    Type type = Type::None;
    int  line = 0;   // dove compare nel file: serve ai messaggi d'errore

    std::wstring str;
    int64_t      integer = 0;
    double       number  = 0.0;
    bool         boolean = false;

    std::vector<Value> array;

    // Coppie in ordine di comparsa, non una mappa: vedi l'intestazione.
    std::vector<std::pair<std::string, Value>> table;

    bool valid() const { return type != Type::None; }

    // Accesso a un campo della tabella; ritorna un valore non valido se manca.
    const Value& operator[](std::string_view key) const;

    // Accesso per percorso puntato: `find("bar.edge")`.
    const Value& find(std::string_view path) const;

    // Letture con valore di ripiego. Un numero scritto come intero si legge
    // anche come decimale: nel file "1" e "1.0" devono valere uguale, perche'
    // chi scrive non deve sapere come li distinguiamo noi.
    std::wstring asString(std::wstring_view fallback = {}) const;
    double       asNumber(double fallback = 0.0) const;
    int64_t      asInt(int64_t fallback = 0) const;
    bool         asBool(bool fallback = false) const;
};

struct Error {
    int          line = 0;
    std::wstring message;
};

struct Document {
    Value              root;
    std::vector<Error> errors;
    bool ok() const { return errors.empty(); }
};

// Analizza del testo UTF-8. Non si ferma al primo errore: li raccoglie tutti e
// riprende dalla riga successiva, perche' correggere un file di configurazione
// un errore alla volta, con un riavvio in mezzo, e' una tortura.
Document Parse(std::string_view utf8);

// Legge un file. Se non esiste, torna un documento vuoto senza errori: un file
// di configurazione assente non e' un guasto, sono i default.
Document ParseFile(const std::wstring& path);

}  // namespace omni::toml
