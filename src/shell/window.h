// window.h — creazione della finestra della barra, click-through, tray, menu.
//
// La finestra e' WS_POPUP + topmost + noactivate + toolwindow + layered:
// niente focus, niente Alt-Tab, niente voce nella taskbar, alpha per-pixel.
// `WS_EX_NOACTIVATE` non e' un'ottimizzazione, e' il vincolo §2.2: la barra non
// ruba MAI il focus, nemmeno quando chiede attenzione.
#pragma once
#include "core/common.h"

namespace omni::shell {

bool RegisterBarClass(HINSTANCE inst, WNDPROC proc);
HWND CreateBarWindow(HINSTANCE inst, void* userData);

// A riposo e mentre scorre la barra e' click-through, cosi' i 3 px che sporgono
// dal bordo non mangiano mai un click destinato a cio' che c'e' sotto. Diventa
// cliccabile solo da aperta.
void SetClickThrough(HWND hwnd, bool on);

void ShowNoActivate(HWND hwnd);

// Sposta senza attivare e senza ridisegnare la superficie: e' cosi' che lo
// scorrimento costa zero ridisegni.
void MoveNoActivate(HWND hwnd, const RECT& r);

UINT DpiFor(HWND hwnd);

bool AddTrayIcon(HWND hwnd, HICON icon);
void RemoveTrayIcon(HWND hwnd);

struct MenuState {
    Edge edge      = Edge::Bottom;
    bool pinned    = false;
    bool autostart = false;
};

// Torna l'id del comando scelto, 0 se annullato.
UINT ShowTrayMenu(HWND hwnd, POINT screenPt, const MenuState& state);

// Autostart: una voce in HKCU\...\Run, che e' l'unico posto che non richiede
// privilegi ne' un installer.
bool AutostartEnabled();
bool SetAutostart(bool on);

}  // namespace omni::shell
