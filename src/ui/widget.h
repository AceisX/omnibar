// widget.h — il vocabolario dei widget e le azioni che possono produrre.
//
// E' il contratto centrale del progetto (docs/architecture.md §6): moduli ed
// estensioni non disegnano, dichiarano un albero di questi nodi e l'host lo
// renderizza. La stessa struttura la produce un modulo built-in in C++, un file
// TOML dichiarativo o un'estensione che parla JSON-RPC: da qui in giu' non si
// distingue chi l'ha creata, ed e' esattamente il punto.
//
// Aggiungere un tipo qui e' una decisione architetturale, non una feature: va
// motivata, disegnata per tutti i temi e i DPI, e coperta da golden-image test.
#pragma once
#include "core/common.h"

#include <memory>

namespace omni::ui {

// ── Geometria, in DIP ────────────────────────────────────────────────────────
// Tutto il layer UI ragiona in DIP. La conversione in pixel avviene una volta
// sola, nel renderer, con il DPI del monitor su cui sta la barra.

struct SizeF { float w = 0.f, h = 0.f; };

struct RectF {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;

    float  right()  const { return x + w; }
    float  bottom() const { return y + h; }
    bool   contains(float px, float py) const {
        return px >= x && px < right() && py >= y && py < bottom();
    }
    bool empty() const { return w <= 0.f || h <= 0.f; }
};

// ── Azioni ───────────────────────────────────────────────────────────────────

enum class ActionKind {
    None,
    Internal,   // una funzione dell'host: nessuna capability richiesta
    Keystroke,  // SendInput verso il target catturato prima del click
    Shell,      // eseguibile + argomenti, MAI una riga di comando (security.md §2.2)
    Url,
    Rpc,        // metodo dell'estensione che ha prodotto il widget
    Macro,      // sequenza; le capability sono l'unione di quelle dei passi
};

struct Action {
    ActionKind   kind = ActionKind::None;

    std::wstring name;               // Internal: nome della funzione. Rpc: metodo.
    std::wstring keys;               // Keystroke: "Ctrl+Shift+5"
    std::wstring exe;                // Shell: eseguibile
    std::vector<std::wstring> args;  // Shell: argomenti, gia' separati
    std::wstring url;                // Url

    std::vector<Action> steps;       // Macro

    bool valid() const { return kind != ActionKind::None; }
};

// ── Widget ───────────────────────────────────────────────────────────────────

enum class WidgetType {
    Group,
    Button,
    Toggle,
    Label,
    Separator,
    Spacer,
    // Fase 3: Meter, Sparkline, Slider, Segmented, Progress, Image, Swatch,
    // Panel, List, Grid, Prompt.
};

enum class Direction { Row, Column };

// Come i figli si dispongono sull'asse trasversale.
enum class Align { Start, Center, End, Stretch };

// Enfasi del testo: decide colore e peso, non la dimensione.
enum class Emphasis { Normal, Dim, Strong };

struct Widget {
    WidgetType   type = WidgetType::Group;
    std::string  id;                  // unico nell'albero del produttore

    // Contenuto
    std::wstring label;
    std::wstring tooltip;
    std::wstring icon;                // nome logico, risolto in render/icons.h
    std::wstring a11y;                // sovrascrive il nome per gli screen reader

    // Stato
    bool  enabled  = true;
    bool  on       = false;           // Toggle
    int   badge    = 0;               // > 0 disegna un contatore, < 0 un pallino
    Emphasis emphasis = Emphasis::Normal;

    // Group
    Direction direction = Direction::Row;
    float     gap       = 6.f;
    Align     align     = Align::Center;
    std::vector<Widget> children;

    // Spacer / Separator: dimensione fissa; per lo Spacer 0 significa "cresci".
    float size = 0.f;

    Action action;

    // ── Risultato del layout, riempito da Layout(): non si scrive a mano ──
    RectF rect;

    // Il nome che leggono gli screen reader. Un widget interattivo che non ha
    // ne' a11y, ne' label, ne' tooltip e' un errore di validazione: qui torna
    // vuoto, e il validatore lo segnala.
    const std::wstring& accessibleName() const {
        if (!a11y.empty())    return a11y;
        if (!label.empty())   return label;
        return tooltip;
    }

    bool interactive() const {
        return (type == WidgetType::Button || type == WidgetType::Toggle) && enabled;
    }
};

// ── Costruttori comodi ───────────────────────────────────────────────────────
// Servono ai moduli built-in, che dichiarano alberi in C++. Il TOML e il
// JSON-RPC producono gli stessi nodi passando da un parser.

Widget Group(Direction dir, float gap, std::vector<Widget> children);
Widget Button(std::string id, std::wstring icon, std::wstring tooltip, Action action);
Widget TextButton(std::string id, std::wstring label, Action action);
Widget Toggle(std::string id, std::wstring icon, std::wstring tooltip, bool on, Action action);
Widget Label(std::wstring text, Emphasis emphasis = Emphasis::Normal);
Widget Separator();
Widget Spacer(float size = 0.f);

Action Internal(std::wstring name);
Action Url(std::wstring url);

// Cerca un nodo per id nell'albero. Nullptr se non c'e'.
Widget*       Find(Widget& root, std::string_view id);
const Widget* Find(const Widget& root, std::string_view id);

}  // namespace omni::ui
