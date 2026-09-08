// common.h — definizioni condivise da tutti gli strati dell'host.
//
// Regola di dipendenza (docs/architecture.md §5.2): gli strati si vedono solo
// verso il basso. `core` non conosce nessuno; tutti conoscono `core`.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace omni {

inline constexpr wchar_t kAppName[]    = L"OmniBar";
inline constexpr wchar_t kAppId[]      = L"OmniBar";
inline constexpr wchar_t kMutexName[]  = L"Local\\OmniBar.SingleInstance";
inline constexpr wchar_t kVersion[]    = L"0.1.0-dev";

// Messaggi interni. Gli eventi che arrivano da fuori (WinRT, named pipe, PDH,
// COM, watcher del filesystem) girano su thread di threadpool e non toccano mai
// lo stato: impacchettano i dati e li rimandano al thread UI con PostMessage.
// Vedi architecture.md §15.
enum : UINT {
    WM_APP_QUIT       = WM_APP + 1,   // richiesta di chiusura ordinata
    WM_APP_FOREGROUND = WM_APP + 2,   // EVENT_SYSTEM_FOREGROUND
    WM_APP_CONFIG     = WM_APP + 3,   // la configurazione su disco e' cambiata
    WM_APP_EXT        = WM_APP + 4,   // messaggio da un'estensione (lParam = payload*)
    WM_APP_TRAY       = WM_APP + 5,   // callback icona di notifica
    WM_APP_APPBAR     = WM_APP + 6,   // callback SHAppBarMessage
};

// Il bordo su cui vive la barra.
enum class Edge { Bottom, Top, Left, Right };

// Stato della barra (architecture.md §8). L'ordine non ha significato: le
// transizioni valide le decide la macchina a stati, non un confronto.
enum class BarState { Hidden, Peek, Revealed, Pinned, Attention, Expanded, Suppressed };

// Le zone della barra, in ordine di priorita' decrescente (architecture.md §7.3).
enum class Zone { Alert, Pinned, Context, Overflow, Handle };

// Utilita' di stringa condivise.
bool ContainsNoCase(std::wstring_view haystack, std::wstring_view needle);
bool EqualsNoCase(std::wstring_view a, std::wstring_view b);

// UTF-8 <-> UTF-16. Il confine e' netto: dentro l'host tutto e' wstring, sul
// filo (TOML, JSON-RPC, log) tutto e' UTF-8.
std::string  ToUtf8(std::wstring_view s);
std::wstring FromUtf8(std::string_view s);

}  // namespace omni
