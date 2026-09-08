// main.cpp — entry point dell'host.
//
// Single-instance, percorsi, log, COM, poi App prende in mano tutto. Il message
// loop di questo thread e' il thread UI: e' l'unico posto dove vive lo stato
// dell'interfaccia (docs/architecture.md §15).
#include "app.h"
#include "core/common.h"
#include "core/log.h"
#include "core/paths.h"

#include <objbase.h>

namespace omni {
namespace {

int Run(HINSTANCE inst) {
    const ULONGLONG startTick = GetTickCount64();

    // Single instance. Il mutex vive per tutta la durata del processo: se la
    // seconda istanza lo trova, sveglia la prima invece di aprirne un'altra.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(L"OmniBarWindow", nullptr))
            PostMessageW(existing, WM_APP_TRAY, 0, WM_LBUTTONUP);
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

    // COM in STA: e' quello che vogliono WIC, la shell e il grosso delle API
    // che useremo. Le estensioni girano fuori processo e non ci riguardano.
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) log::Hresult(L"CoInitializeEx", hr);

    int exitCode = 0;
    {
        App app;
        if (!app.Init(inst)) {
            log::Error(L"Inizializzazione fallita: si esce");
            app.Shutdown();
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
        exitCode = static_cast<int>(msg.wParam);

        app.Shutdown();
    }

    log::Info(L"OmniBar — uscita");
    CoUninitialize();
    log::Close();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return exitCode;
}

}  // namespace
}  // namespace omni

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    return omni::Run(inst);
}
