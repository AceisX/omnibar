// watcher.h — avvisa quando la configurazione cambia su disco.
//
// Serve a una cosa sola: salvi il file e la barra si aggiorna, senza riavviarla.
// Su qualcosa che si rifinisce a colpi di due punti in piu' o in meno, la
// differenza fra "salva e guarda" e "salva, chiudi, riapri, guarda" e' la
// differenza fra provare dieci valori e provarne due.
//
// Vive su un thread suo e **non tocca nessuno stato**: quando qualcosa cambia
// manda un messaggio alla finestra e basta. E' la disciplina della §15
// dell'architettura — chi arriva da fuori impacchetta e rimanda al thread UI —
// ed e' il motivo per cui in tutto il programma non c'e' un lock.
#pragma once
#include "core/common.h"

#include <thread>

namespace omni::config {

class Watcher {
public:
    ~Watcher() { Stop(); }

    // Comincia a sorvegliare `dir`. A ogni modifica manda `msg` a `hwnd`.
    bool Start(const std::wstring& dir, HWND hwnd, UINT msg);
    void Stop();

private:
    std::thread thread_;
    HANDLE      stop_ = nullptr;   // evento manuale: sveglia il thread per farlo uscire
};

}  // namespace omni::config
