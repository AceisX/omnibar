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

// Toglie il colore lasciando la stessa luminosita' percepita. I coefficienti
// sono quelli della luminanza: sostituirli con una media dei tre canali fa
// diventare i blu troppo chiari e i rossi troppo scuri.
constexpr Color Desaturate(const Color& c) {
    const float y = c.r * 0.2126f + c.g * 0.7152f + c.b * 0.0722f;
    return Color{y, y, y, c.a};
}

// Miscela lineare fra due colori, 0 = a, 1 = b.
constexpr Color Mix(const Color& a, const Color& b, float t) {
    return Color{a.r + (b.r - a.r) * t,
                 a.g + (b.g - a.g) * t,
                 a.b + (b.b - a.b) * t,
                 a.a + (b.a - a.a) * t};
}

// Il colore d'accento scelto dall'utente nelle impostazioni di Windows, con le
// sue varianti chiare e scure. Non e' una preferenza nostra: e' la sua, e una
// barra che vuole passare per un componente di sistema non ha motivo di
// inventarsi una tavolozza propria.
//
// Se il sistema non risponde si torna ai valori di ripiego, che sono l'accento
// predefinito di Windows 11.
struct SystemAccent {
    Color base;
    Color light;   // due gradini verso il chiaro
    Color dark;    // un gradino verso lo scuro
    bool  fromSystem = false;
};
SystemAccent ReadSystemAccent();

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

    // L'avatar. E' l'unico punto di colore della barra, ed e' voluto: fra icone
    // tutte monocrome, una faccia colorata si legge come qualcuno invece che
    // come l'ennesimo comando. Due tinte per la sfera piu' l'occhio e il
    // riflesso; non seguono il tema chiaro/scuro perche' non sono interfaccia,
    // sono un personaggio.
    Color avatarTop;
    Color avatarBottom;
    Color avatarEye;
    Color avatarGlint;

    // Quasi la meta' dello spessore: il lato rivolto verso lo schermo diventa
    // una semicirconferenza e la barra si legge come una pastiglia appoggiata
    // al bordo, non come un pannello incastrato.
    float cornerRadius     = 20.f;
    float widgetRadius     = 8.f;
    float borderWidth      = 1.f;
    // Quanto e' velato il FONDO della barra — non tutta la finestra.
    //
    // La differenza conta. Velando l'intera finestra diventano trasparenti
    // anche le icone e l'avatar: la barra si posa meglio su quello che ha
    // sotto, ma il contenuto perde contrasto proprio dove serve, e su uno
    // sfondo movimentato le icone cominciano a confondersi con quello che
    // traspare. Velando solo il fondo si ottiene lo stesso effetto di
    // leggerezza con il contenuto che resta pieno — ed e' cosi' che si
    // comportano i pannelli di Windows.
    float opacity          = 0.85f;

    // Il font del testo e quello delle icone: Segoe Fluent Icons c'e' su
    // Windows 11, Segoe MDL2 Assets e' il ripiego su Windows 10.
    const wchar_t* fontText = L"Segoe UI Variable Display";
    const wchar_t* fontIcon = L"Segoe Fluent Icons";
    float fontSize          = 12.5f;

    static Theme Dark();
    static Theme Light();

    // Applica l'accento di sistema al tema: colora l'accento della barra e la
    // sfera dell'avatar. Chiamata a ogni cambio di tema o di colorazione.
    void ApplyAccent(const SystemAccent& accent);

    // Porta `opacity` dentro il colore di fondo. Va chiamata dopo aver
    // eventualmente cambiato l'opacita', e prima di dare il tema al renderer.
    void ApplyOpacity();

    // Le metriche di layout che discendono dal tema. Il layout non deve
    // conoscere il tema: riceve solo questi numeri.
    ui::Metrics metrics() const;
};

// Tema chiaro delle applicazioni (AppsUseLightTheme). Su Windows 11 e' distinto
// da quello della shell: si puo' avere taskbar scura e applicazioni chiare, e
// la barra e' un'applicazione.
bool AppsUseLightTheme();

}  // namespace omni::render
