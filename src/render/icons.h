// icons.h — nomi logici delle icone -> glifi di Segoe Fluent Icons.
//
// Le icone sono glifi di un font di sistema, non file. Costano zero da
// caricare, scalano a qualunque DPI e seguono il colore del testo.
//
// Perche' nomi logici e non codepoint diretti nella configurazione: perche'
// "sigma" resta "sigma" anche se un giorno le icone smettono di essere un font,
// e perche' un TOML pieno di \uE8xx non lo scrive nessuno a mano.
//
// Un nome sconosciuto NON diventa un glifo di ripiego: Glyph() torna nullptr e
// il renderer disegna la prima lettera del nome nel font di testo. Un quadratino
// vuoto non dice niente; una "S" al posto di "sigma" dice a chi ha scritto il
// file che il nome non e' quello giusto.
#pragma once
#include "core/common.h"

namespace omni::render::icons {

// Il glifo per un nome logico, o nullptr se il nome non e' nel catalogo.
const wchar_t* Glyph(std::wstring_view name);

// Segoe Fluent Icons su Windows 11, Segoe MDL2 Assets come ripiego su
// Windows 10. I codepoint del catalogo esistono in entrambi.
const wchar_t* FontFamily();

}  // namespace omni::render::icons
