// theme.h — i token di design, e le metriche che ne discendono.
//
// Un colore si definisce qui e in nessun altro posto. Se una funzione di
// disegno contiene una costante di colore, e' un bug: significa che quel pezzo
// non seguira' il tema chiaro, o il contrasto elevato, o qualunque cosa venga
// dopo.
#pragma once
#include "core/common.h"

#include "ui/layout.h"

namespace omni::render {

struct Color {
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f;

    static constexpr Color Rgb(uint32_t hex, float alpha = 1.f) {
        return Color{static_cast<float>((hex >> 16) & 0xFF) / 255.f,
                     static_cast<float>((hex >> 8) & 0xFF) / 255.f,
                     static_cast<float>(hex & 0xFF) / 255.f,
                     alpha};
    }

    constexpr Color withAlpha(float alpha) const { return Color{r, g, b, alpha}; }
};

struct Theme {
    bool dark = true;

    Color background;    // il fondo della barra
    Color border;        // il bordo esterno
    Color text;
    Color textDim;
    Color textDisabled;
    Color hover;         // velatura sotto il widget sotto il cursore
    Color pressed;
    Color accent;        // toggle attivo, stato Attention, badge
    Color accentText;    // testo sopra l'accento
    Color separator;

    float cornerRadius     = 12.f;
    float widgetRadius     = 7.f;
    float borderWidth      = 1.f;
    float opacity          = 0.96f;

    // Il font del testo e quello delle icone: Segoe Fluent Icons c'e' su
    // Windows 11, Segoe MDL2 Assets e' il ripiego su Windows 10.
    const wchar_t* fontText = L"Segoe UI Variable Display";
    const wchar_t* fontIcon = L"Segoe Fluent Icons";
    float fontSize          = 12.5f;

    static Theme Dark();
    static Theme Light();

    // Le metriche di layout che discendono dal tema. Il layout non deve
    // conoscere il tema: riceve solo questi numeri.
    ui::Metrics metrics() const;
};

// Tema chiaro delle applicazioni (AppsUseLightTheme). Su Windows 11 e' distinto
// da quello della shell: si puo' avere taskbar scura e applicazioni chiare, e
// la barra e' un'applicazione.
bool AppsUseLightTheme();

}  // namespace omni::render
