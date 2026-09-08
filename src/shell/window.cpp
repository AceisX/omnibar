#include "shell/window.h"

#include "core/log.h"
#include "core/paths.h"

#include <shellapi.h>

namespace omni::shell {
namespace {

constexpr wchar_t kBarClass[] = L"OmniBarWindow";
constexpr UINT    kTrayId     = 1;

std::wstring ExePath() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) { buf.resize(n); break; }
        buf.resize(buf.size() * 2);
    }
    return buf;
}

void AppendRadio(HMENU menu, UINT id, const wchar_t* text, bool checked) {
    AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : 0), id, text);
}

}  // namespace

bool RegisterBarClass(HINSTANCE inst, WNDPROC proc) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc   = proc;
    wc.hInstance     = inst;
    wc.lpszClassName = kBarClass;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    if (RegisterClassExW(&wc)) return true;
    log::LastError(L"RegisterClassExW(OmniBarWindow)");
    return false;
}

HWND CreateBarWindow(HINSTANCE inst, void* userData) {
    const DWORD ex = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED;
    HWND hwnd = CreateWindowExW(ex, kBarClass, kAppName, WS_POPUP,
                                0, 0, 10, 10, nullptr, nullptr, inst, userData);
    if (!hwnd) log::LastError(L"CreateWindowExW(barra)");
    return hwnd;
}

void SetClickThrough(HWND hwnd, bool on) {
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const LONG_PTR want = on ? (ex | WS_EX_TRANSPARENT) : (ex & ~WS_EX_TRANSPARENT);
    if (want != ex) SetWindowLongPtrW(hwnd, GWL_EXSTYLE, want);
}

void ShowNoActivate(HWND hwnd) {
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
}

void MoveNoActivate(HWND hwnd, const RECT& r) {
    SetWindowPos(hwnd, HWND_TOPMOST, r.left, r.top,
                 r.right - r.left, r.bottom - r.top,
                 SWP_NOACTIVATE | SWP_NOREDRAW);
}

UINT DpiFor(HWND hwnd) {
    const UINT dpi = GetDpiForWindow(hwnd);
    return dpi ? dpi : 96;
}

bool AddTrayIcon(HWND hwnd, HICON icon) {
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd             = hwnd;
    nid.uID              = kTrayId;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon            = icon;
    wcscpy_s(nid.szTip, kAppName);
    if (Shell_NotifyIconW(NIM_ADD, &nid)) return true;
    log::LastError(L"Shell_NotifyIconW(NIM_ADD)");
    return false;
}

void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = hwnd;
    nid.uID  = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

UINT ShowTrayMenu(HWND hwnd, POINT screenPt, const MenuState& state) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return 0;

    HMENU edges = CreatePopupMenu();
    AppendRadio(edges, IDM_EDGE_BOTTOM, L"In basso", state.edge == Edge::Bottom);
    AppendRadio(edges, IDM_EDGE_TOP,    L"In alto",  state.edge == Edge::Top);
    AppendRadio(edges, IDM_EDGE_LEFT,   L"A sinistra", state.edge == Edge::Left);
    AppendRadio(edges, IDM_EDGE_RIGHT,  L"A destra",  state.edge == Edge::Right);

    AppendMenuW(menu, MF_STRING, IDM_REVEAL, L"Mostra la barra");
    AppendMenuW(menu, MF_STRING | (state.pinned ? MF_CHECKED : 0), IDM_PIN,
                L"Tienila aperta");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(edges), L"Bordo");
    AppendMenuW(menu, MF_STRING | (state.autostart ? MF_CHECKED : 0), IDM_AUTOSTART,
                L"Avvia con Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_OPEN_CONFIG, L"Apri la cartella di configurazione");
    AppendMenuW(menu, MF_STRING, IDM_OPEN_LOG,    L"Apri il log");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Esci");

    // SetForegroundWindow prima di TrackPopupMenu e' l'unico modo perche' il
    // menu si chiuda cliccando fuori: e' documentato, ed e' l'unica volta in
    // cui la barra tocca il foreground — per un menu che l'utente ha aperto.
    SetForegroundWindow(hwnd);
    const UINT cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                    screenPt.x, screenPt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    DestroyMenu(edges);
    DestroyMenu(menu);
    return cmd;
}

bool AutostartEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;

    const LSTATUS st = RegQueryValueExW(key, kAppId, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}

bool SetAutostart(bool on) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return false;

    LSTATUS st;
    if (on) {
        const std::wstring quoted = L"\"" + ExePath() + L"\"";
        st = RegSetValueExW(key, kAppId, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(quoted.c_str()),
                            static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t)));
    } else {
        st = RegDeleteValueW(key, kAppId);
        if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}

}  // namespace omni::shell
