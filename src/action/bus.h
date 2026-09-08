// bus.h — esecuzione delle azioni, con il controllo dei permessi davanti.
//
// Ogni cosa cliccabile produce un'Action tipizzata, e passa da qui. Due regole
// di sicurezza (docs/security.md §2.2 e §2.3) sono nella forma dei dati, non in
// un controllo che si puo' dimenticare:
//
//  - `shell` prende eseguibile e argomenti separati, mai una riga di comando.
//    Non c'e' modo di scrivere una command injection in un file di
//    configurazione, perche' non c'e' un campo in cui scriverla.
//  - `keystroke` ha un bersaglio esplicito, catturato prima che la barra tocchi
//    il foreground, e verificato al momento dell'invio. Se nel frattempo e'
//    cambiato, l'azione si rifiuta: una macro che parte nella finestra
//    sbagliata puo' cancellare il lavoro di qualcuno.
#pragma once
#include "ui/widget.h"

#include <functional>

namespace omni::action {

struct Context {
    // La finestra che era in primo piano quando l'utente ha premuto. La
    // cattura la fa il chiamante, prima di qualunque cosa possa spostare il
    // foreground.
    HWND target = nullptr;
};

// Le capability che un'azione richiede: servono a chiedere il consenso e a
// rifiutare cio' che non e' stato concesso. I widget dell'host non ne hanno
// bisogno; quelli delle estensioni si'.
enum class Capability : uint32_t {
    None      = 0,
    Input     = 1 << 0,
    Exec      = 1 << 1,
    Open      = 1 << 2,
    Clipboard = 1 << 3,
    Files     = 1 << 4,
    Network   = 1 << 5,
};

constexpr uint32_t operator|(Capability a, Capability b) {
    return static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
}

// Cosa serve per eseguire questa azione (per una macro, l'unione dei passi).
uint32_t Required(const ui::Action& action);

// Le azioni `internal` non sono dati: sono funzioni dell'host. Il bus non le
// conosce, le rimanda a chi le implementa.
using InternalHandler = std::function<bool(std::wstring_view name)>;

class Bus {
public:
    void SetInternalHandler(InternalHandler handler) { internal_ = std::move(handler); }

    // `granted` sono le capability concesse al produttore del widget. Per i
    // moduli built-in si passa tutto: sono l'host.
    bool Invoke(const ui::Action& action, const Context& ctx, uint32_t granted);

private:
    bool Keystroke(const ui::Action& a, const Context& ctx);
    bool Shell(const ui::Action& a);
    bool Url(const ui::Action& a);

    InternalHandler internal_;
};

// Traduce "Ctrl+Shift+5" in tasti virtuali. Esposta perche' e' la parte che si
// sbaglia piu' facilmente e va provata da sola.
struct KeyCombo {
    bool ctrl = false, alt = false, shift = false, win = false;
    WORD vk   = 0;
    bool valid() const { return vk != 0; }
};
KeyCombo ParseCombo(std::wstring_view text);

}  // namespace omni::action
