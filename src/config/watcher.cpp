#include "config/watcher.h"

#include "core/log.h"

namespace omni::config {

bool Watcher::Start(const std::wstring& dir, HWND hwnd, UINT msg) {
    Stop();

    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_) return false;

    HANDLE cartella = CreateFileW(
        dir.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (cartella == INVALID_HANDLE_VALUE) {
        log::LastError(L"non riesco a sorvegliare la cartella di configurazione");
        CloseHandle(stop_);
        stop_ = nullptr;
        return false;
    }

    HANDLE stop = stop_;
    thread_ = std::thread([cartella, stop, hwnd, msg] {
        // I/O sovrapposto invece di una lettura bloccante: solo cosi' il thread
        // puo' aspettare CONTEMPORANEAMENTE una modifica e l'ordine di uscire.
        // Con una lettura bloccante, chiudere il programma vorrebbe dire
        // aspettare che qualcuno tocchi il file, oppure terminare un thread di
        // forza — e un thread terminato di forza in mezzo a un'operazione sul
        // filesystem e' un modo per lasciare handle appesi.
        HANDLE evento = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!evento) { CloseHandle(cartella); return; }

        std::vector<BYTE> buffer(8 * 1024);

        for (;;) {
            OVERLAPPED ov{};
            ov.hEvent = evento;
            ResetEvent(evento);

            DWORD ignorato = 0;
            const BOOL avviata = ReadDirectoryChangesW(
                cartella, buffer.data(), static_cast<DWORD>(buffer.size()), FALSE,
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME |
                    FILE_NOTIFY_CHANGE_SIZE,
                &ignorato, &ov, nullptr);
            if (!avviata) break;

            HANDLE attese[] = {stop, evento};
            const DWORD esito = WaitForMultipleObjects(2, attese, FALSE, INFINITE);

            if (esito == WAIT_OBJECT_0) {          // ci hanno detto di smettere
                CancelIoEx(cartella, &ov);
                DWORD trasferiti = 0;
                GetOverlappedResult(cartella, &ov, &trasferiti, TRUE);
                break;
            }
            if (esito != WAIT_OBJECT_0 + 1) break;

            DWORD trasferiti = 0;
            if (!GetOverlappedResult(cartella, &ov, &trasferiti, FALSE)) break;

            // Non si guarda nemmeno QUALE file e' cambiato: il messaggio dice
            // soltanto "qualcosa e' cambiato", e a decidere cosa rileggere ci
            // pensa il thread UI. Filtrare qui vorrebbe dire duplicare qui la
            // conoscenza di quali file contano.
            PostMessageW(hwnd, msg, 0, 0);
        }

        CloseHandle(evento);
        CloseHandle(cartella);
    });

    return true;
}

void Watcher::Stop() {
    if (stop_) SetEvent(stop_);
    if (thread_.joinable()) thread_.join();
    if (stop_) {
        CloseHandle(stop_);
        stop_ = nullptr;
    }
}

}  // namespace omni::config
