// layout.h — misura e disposizione dell'albero dei widget.
//
// Due passate, come qualunque motore flex: Measure chiede a ogni nodo quanto
// vuole essere, Arrange gli dice dove sta davvero. Il risultato finisce in
// Widget::rect, in DIP, e da li' lo leggono sia il renderer sia l'hit-test —
// che devono vedere gli stessi rettangoli, altrimenti si clicca un bottone e se
// ne illumina un altro.
//
// Il layout non conosce Direct2D. Sa misurare il testo solo attraverso la
// callback in Metrics, che gliela passa il renderer: e' l'unico punto in cui
// `ui` dipende da qualcosa che sa disegnare, ed e' una funzione, non un tipo.
#pragma once
#include "ui/widget.h"

#include <functional>

namespace omni::ui {

// Costanti di disegno che il layout deve conoscere per misurare. Sono in DIP e
// arrivano dal tema: qui non ci sono numeri magici, solo il loro trasporto.
struct Metrics {
    float padding      = 8.f;   // margine interno della barra
    float buttonMin    = 34.f;  // lato minimo di un bottone con la sola icona
    float buttonPadX   = 10.f;  // padding orizzontale di un bottone con testo
    float iconSize     = 16.f;
    float labelGap     = 6.f;   // fra icona e testo dentro un bottone
    float separatorLen = 1.f;   // spessore della linea
    float separatorPad = 5.f;   // aria ai due lati del separatore

    // Larghezza del testo in DIP. `icon` distingue il font delle icone da
    // quello del testo: hanno metriche diverse e non si possono misurare uguali.
    std::function<float(std::wstring_view text, bool icon)> measureText;
};

// Misura quanto il nodo vorrebbe essere, figli compresi.
SizeF Measure(const Widget& w, const Metrics& m);

// Dispone l'albero dentro `bounds` e scrive i rettangoli in Widget::rect.
void Layout(Widget& root, const RectF& bounds, const Metrics& m);

// Il widget interattivo piu' profondo che contiene il punto, o nullptr.
// Il punto e' in DIP, nello stesso sistema di Layout().
Widget* HitTest(Widget& root, float x, float y);

}  // namespace omni::ui
