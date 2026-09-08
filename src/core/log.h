// log.h — log su file, UTF-8, una riga per evento.
//
// Non e' un sistema di logging generico: e' il minimo che serve a capire cosa
// e' successo su una macchina che non e' la nostra. Scrive in append su
// logs\omnibar.log, ruota quando supera qualche centinaio di KB, e non alloca
// niente quando il livello e' sotto la soglia.
//
// Costo a riposo: il log NON e' un canale caldo. Niente log dentro il loop di
// disegno, niente log a ogni tick. Se una riga viene scritta piu' di una volta
// al secondo a regime, e' un bug.
#pragma once
#include "common.h"

namespace omni::log {

enum class Level { Trace, Debug, Info, Warn, Error };

// Apre il file (in paths::Logs()). Se fallisce, le funzioni sotto diventano
// no-op silenziose: un problema col log non deve impedire l'avvio.
void Open(Level minimum = Level::Info);
void Close();

void SetLevel(Level minimum);
bool Enabled(Level level);

void Write(Level level, std::wstring_view text);

inline void Trace(std::wstring_view t) { if (Enabled(Level::Trace)) Write(Level::Trace, t); }
inline void Debug(std::wstring_view t) { if (Enabled(Level::Debug)) Write(Level::Debug, t); }
inline void Info (std::wstring_view t) { if (Enabled(Level::Info )) Write(Level::Info , t); }
inline void Warn (std::wstring_view t) { if (Enabled(Level::Warn )) Write(Level::Warn , t); }
inline void Error(std::wstring_view t) { if (Enabled(Level::Error)) Write(Level::Error, t); }

// Come Error, ma aggiunge il messaggio di sistema di GetLastError()/HRESULT.
void LastError(std::wstring_view what);
void Hresult(std::wstring_view what, HRESULT hr);

}  // namespace omni::log
