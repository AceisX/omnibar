#include "render/theme.h"

namespace omni::render {

Theme Theme::Dark() {
    Theme t;
    t.dark         = true;
    t.background   = Color::Rgb(0x1F1F1F, 0.94f);
    t.border       = Color::Rgb(0xFFFFFF, 0.09f);
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
    t.background   = Color::Rgb(0xF7F7F7, 0.95f);
    t.border       = Color::Rgb(0x000000, 0.10f);
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
    ui::Metrics m;
    m.padding      = 8.f;
    m.buttonMin    = 34.f;
    m.buttonPadX   = 10.f;
    m.iconSize     = 16.f;
    m.labelGap     = 6.f;
    m.separatorLen = 1.f;
    m.separatorPad = 5.f;
    return m;  // measureText la riempie il renderer, che sa misurare
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
