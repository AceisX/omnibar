#include "render/theme.h"

#include <winrt/Windows.UI.ViewManagement.h>

#include <algorithm>

namespace omni::render {

Theme Theme::Dark() {
    Theme t;
    t.dark         = true;
    t.background   = Color::Rgb(0x1B1B1B, 0.98f);
    t.border       = Color::Rgb(0xFFFFFF, 0.12f);
    t.text         = Color::Rgb(0xF2F2F2);
    t.textDim      = Color::Rgb(0xF2F2F2, 0.62f);
    t.textDisabled = Color::Rgb(0xF2F2F2, 0.30f);
    t.hover        = Color::Rgb(0xFFFFFF, 0.08f);
    t.pressed      = Color::Rgb(0xFFFFFF, 0.14f);
    t.accent       = Color::Rgb(0x4CC2FF);
    t.accentText   = Color::Rgb(0x0A0A0A);
    t.separator    = Color::Rgb(0xFFFFFF, 0.12f);
    return t;
}

Theme Theme::Light() {
    Theme t;
    t.dark         = false;
    t.background   = Color::Rgb(0xFAFAFA, 0.98f);
    t.border       = Color::Rgb(0x000000, 0.14f);
    t.text         = Color::Rgb(0x1A1A1A);
    t.textDim      = Color::Rgb(0x1A1A1A, 0.62f);
    t.textDisabled = Color::Rgb(0x1A1A1A, 0.32f);
    t.hover        = Color::Rgb(0x000000, 0.06f);
    t.pressed      = Color::Rgb(0x000000, 0.11f);
    t.accent       = Color::Rgb(0x005FB8);
    t.accentText   = Color::Rgb(0xFFFFFF);
    t.separator    = Color::Rgb(0x000000, 0.13f);
    return t;
}

ui::Metrics Theme::metrics() const {
    // Numeri piccoli, di proposito. La barra deve leggersi come un accessorio
    // sul bordo, non come una seconda taskbar: con un bottone da 30 punti e
    // sei di margine lo spessore totale sta in 42, meno di una riga di testo.
    ui::Metrics m;
    // Il margine alle estremita' e' quasi il doppio di prima. Non allarga la
    // barra: la fa cominciare piu' su e finire piu' giu' della prima e
    // dell'ultima icona, che e' cio' che la fa sembrare un oggetto invece di
    // una fila di bottoni con un contorno.
    m.padAlong     = 22.f;
    m.padCross     = 7.f;
    m.buttonMin    = 30.f;
    m.buttonPadX   = 9.f;
    m.iconSize     = 13.f;
    m.labelGap     = 6.f;
    m.separatorLen = 1.f;
    m.separatorPad = 4.f;
    return m;  // measureText la riempie il renderer, che sa misurare
}

namespace {

Color FromWinRT(const winrt::Windows::UI::Color& c) {
    return Color{static_cast<float>(c.R) / 255.f,
                 static_cast<float>(c.G) / 255.f,
                 static_cast<float>(c.B) / 255.f,
                 1.f};
}

}  // namespace

SystemAccent ReadSystemAccent() {
    using namespace winrt::Windows::UI::ViewManagement;

    // Ripiego: l'accento predefinito di Windows 11. Serve se il sistema non
    // risponde — e serve anche perche' una barra senza colore d'accento
    // sarebbe una barra senza stato acceso.
    SystemAccent a;
    a.base  = Color::Rgb(0x0078D4);
    a.light = Color::Rgb(0x99EBFF);
    a.dark  = Color::Rgb(0x005A9E);

    try {
        UISettings ui;
        a.base  = FromWinRT(ui.GetColorValue(UIColorType::Accent));
        a.light = FromWinRT(ui.GetColorValue(UIColorType::AccentLight2));
        a.dark  = FromWinRT(ui.GetColorValue(UIColorType::AccentDark1));
        a.fromSystem = true;
    } catch (...) {
        // UISettings puo' fallire su installazioni ridotte o se l'apartment
        // non e' quello che si aspetta. Non e' un errore da segnalare: si usa
        // il ripiego e la barra funziona uguale.
    }
    return a;
}

// Quanto si smorza il colore dell'accento prima di usarlo. Il colore scelto
// dall'utente e' pensato per campiture grandi — la barra del titolo, il menu
// Start — e su superfici piccole come un toggle o una faccia da trenta punti
// arriva piu' forte del dovuto. Toglierne un quinto lo riporta al peso giusto
// senza cambiargli tinta: resta riconoscibile come "il suo colore".
constexpr float kSoften = 0.20f;

namespace {
Color Soften(const Color& c) { return Mix(c, Desaturate(c), kSoften); }
}  // namespace

void Theme::ApplyOpacity() {
    background = background.withAlpha(opacity);
}

void Theme::ApplyAccent(const SystemAccent& a) {
    // L'accento della barra: sul fondo scuro serve la variante chiara, sul
    // fondo chiaro quella scura. Prendere sempre la stessa vorrebbe dire che
    // meta' degli utenti non vede il proprio colore.
    accent     = Soften(dark ? a.light : a.dark);
    accentText = dark ? Color::Rgb(0x0A0A0A) : Color::Rgb(0xFFFFFF);

    // La sfera dell'avatar: dalla variante chiara alla base, cosi' la
    // sfumatura ha volume e resta riconoscibile come "il colore dell'utente".
    // Su tema scuro si parte piu' chiari, perche' li' la sfera deve staccarsi
    // dal fondo invece di fondersi.
    avatarTop    = Soften(dark ? Mix(a.light, Color::Rgb(0xFFFFFF), 0.25f) : a.light);
    avatarBottom = Soften(dark ? a.base : Mix(a.base, a.dark, 0.45f));

    // L'occhio non e' nero: e' l'accento portato quasi a fondo. Un nero puro su
    // una sfera colorata sembra un buco, una tinta scura dello stesso colore
    // sembra parte della faccia.
    avatarEye   = Mix(Mix(a.base, a.dark, 0.7f), Color::Rgb(0x000000), 0.72f);
    avatarGlint = Color::Rgb(0xFFFFFF, 0.88f);
}

bool AppsUseLightTheme() {
    DWORD value = 0, size = sizeof(value);
    const LSTATUS st = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return st == ERROR_SUCCESS && value != 0;
}

}  // namespace omni::render
