// main.cpp — entry point dell'host.
//
// Fase 0 (docs/roadmap.md): single-instance, percorsi, log, message loop.
// Non c'e' ancora nessuna finestra visibile: la barra arriva nella fase 1.
//
// Il message loop e' il thread UI, e resta l'unico posto dove vive lo stato
// dell'interfaccia (architecture.md §15). Tutto cio' che arrivera' da fuori —
// WinRT, named pipe, PDH, COM — passera' di qui via PostMessage.
#include "core/common.h"
#include "core/log.h"
#include "core/paths.h"

#include <objbase.h>

namespace omni {
namespace {

constexpr wchar_t kHostClass[] = L"OmniBarHost";

// Finestra message-only: non si vede, non compare in Alt-Tab, esiste solo per
// ricevere i WM_APP_* e i broadcast di sistema.
LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_APP_QUIT:
            PostQuitMessage(0);
            return 0;

        case WM_ENDSESSION:
            // Windows sta chiudendo: qui andranno i ripristini garantiti
            // (finestre nascoste, AppBar registrate, helper elevato).
            log::Info(L"WM_ENDSESSION: chiusura richiesta dal sistema");
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND CreateHostWindow(HINSTANCE inst) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc   = HostProc;
    wc.hInstance     = inst;
    wc.lpszClassName = kHostClass;
    if (!RegisterClassExW(&wc)) {
        log::LastError(L"RegisterClassExW(OmniBarHost)");
        return nullptr;
    }

    HWND hwnd = CreateWindowExW(0, kHostClass, kAppName, 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, inst, nullptr);
    if (!hwnd) log::LastError(L"CreateWindowExW(HWND_MESSAGE)");
    return hwnd;
}

int Run(HINSTANCE inst) {
    const ULONGLONG startTick = GetTickCount64();

    // Single instance. Il mutex vive per tutta la durata del processo: se la
    // seconda istanza lo trova, sveglia la prima invece di aprirne un'altra.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowExW(HWND_MESSAGE, nullptr, kHostClass, nullptr))
            PostMessageW(existing, WM_APP_TRAY, 0, 0);   // "eccomi": la fase 1 la mostrera'
        CloseHandle(mutex);
        return 0;
    }

    if (!paths::Init()) {
        // Senza percorsi non c'e' nemmeno un log su cui scriverlo.
        MessageBoxW(nullptr, L"Impossibile determinare le cartelle di OmniBar.",
                    kAppName, MB_ICONERROR | MB_OK);
        CloseHandle(mutex);
        return 2;
    }

    log::Open(log::Level::Info);
    log::Info(std::wstring(L"OmniBar ") + kVersion + L" — avvio");
    log::Info(std::wstring(L"Dati in: ") + paths::Root() +
              (paths::Portable() ? L"  (portable)" : L"  (%APPDATA%)"));

    // COM in STA: e' quello che vogliono shell, WIC e il grosso delle API che
    // useremo. Le estensioni girano fuori processo e non ci riguardano.
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) log::Hresult(L"CoInitializeEx", hr);

    HWND host = CreateHostWindow(inst);
    if (!host) {
        log::Error(L"Nessuna finestra host: si esce.");
        CoUninitialize();
        log::Close();
        CloseHandle(mutex);
        return 3;
    }

    log::Info(L"Pronto in " + std::to_wstring(GetTickCount64() - startTick) + L" ms");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    log::Info(L"OmniBar — uscita");
    DestroyWindow(host);
    CoUninitialize();
    log::Close();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}

}  // namespace
}  // namespace omni

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    return omni::Run(inst);
}
