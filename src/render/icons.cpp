#include "render/icons.h"

#include <algorithm>
#include <array>

namespace omni::render::icons {
namespace {

struct Entry {
    const wchar_t* name;
    const wchar_t* glyph;
};

// Catalogo. I nomi sono quelli che si scrivono nei file di configurazione:
// descrivono cosa fa il bottone, non che aspetto ha l'icona, cosi' restano
// validi se un giorno l'icona cambia.
//
// I glifi si scrivono come escape, non come carattere: sono nell'area a uso
// privato di Unicode, in un editor si vedono come quadratini e nessuno
// potrebbe rivedere questa tabella. L'escape si legge, si cerca nel catalogo di
// Segoe Fluent Icons e si verifica.
constexpr std::array kIcons = std::to_array<Entry>({

    // Barra e sistema
    {L"settings",     L"\uE713"},
    {L"pin",          L"\uE718"},
    {L"unpin",        L"\uE77A"},
    {L"close",        L"\uE711"},
    {L"more",         L"\uE712"},
    {L"menu",         L"\uE700"},
    {L"add",          L"\uE710"},
    {L"remove",       L"\uE738"},
    {L"delete",       L"\uE74D"},
    {L"refresh",      L"\uE72C"},
    {L"search",       L"\uE721"},
    {L"accept",       L"\uE8FB"},
    {L"check",        L"\uE73E"},
    {L"info",         L"\uE946"},
    {L"warning",      L"\uE7BA"},
    {L"error",        L"\uE783"},

    // Multimedia
    {L"play",         L"\uE768"},
    {L"pause",        L"\uE769"},
    {L"prev",         L"\uE892"},
    {L"next",         L"\uE893"},
    {L"volume",       L"\uE767"},
    {L"mute",         L"\uE74F"},

    // File e documenti
    {L"folder",       L"\uE8B7"},
    {L"document",     L"\uE8A5"},
    {L"page",         L"\uE7C3"},
    {L"copy",         L"\uE8C8"},
    {L"paste",        L"\uE77F"},
    {L"save",         L"\uE74E"},
    {L"download",     L"\uE896"},
    {L"upload",       L"\uE898"},

    // Cattura e schermo
    {L"camera",       L"\uE722"},
    {L"zoom",         L"\uE71E"},
    {L"fullscreen",   L"\uE740"},
    {L"crop",         L"\uE7A8"},

    // Modifica
    {L"edit",         L"\uE70F"},
    {L"undo",         L"\uE7A7"},
    {L"redo",         L"\uE7A6"},
    {L"color",        L"\uE790"},

    // Navigazione
    {L"up",           L"\uE70E"},
    {L"down",         L"\uE70D"},
    {L"left",         L"\uE76B"},
    {L"right",        L"\uE76C"},

    // Stato
    {L"record",       L"\uE7C8"},
    {L"stop",         L"\uE71A"},
    {L"clock",        L"\uE823"},
    {L"lightbulb",    L"\uEA80"},
});

}  // namespace

const wchar_t* Glyph(std::wstring_view name) {
    const auto it = std::find_if(kIcons.begin(), kIcons.end(),
                                 [&](const Entry& e) { return name == e.name; });
    return it != kIcons.end() ? it->glyph : nullptr;
}

const wchar_t* FontFamily() {
    // Non si interroga il sistema: DirectWrite fa gia' il fallback da solo se la
    // famiglia non c'e', e su Windows 10 finisce su Segoe MDL2 Assets, che ha
    // gli stessi codepoint. Una query in piu' all'avvio non comprerebbe niente.
    return L"Segoe Fluent Icons";
}

}  // namespace omni::render::icons
