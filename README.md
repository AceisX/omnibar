# OmniBar

**La barra contestuale di Windows.** Nascosta su un bordo dello schermo, si apre quando ci
passi sopra, e cambia contenuto in base al programma che stai usando.

> 🚧 **Progetto in costruzione.** Non c'è ancora niente da scaricare. Quello che c'è, per ora,
> è l'architettura: [`docs/architecture.md`](docs/architecture.md) e
> [`docs/roadmap.md`](docs/roadmap.md).

---

## L'idea

Se lavori su un monitor solo — o su un portatile, che è la stessa cosa — passi la giornata a
fare Alt-Tab per premere un bottone e tornare indietro. Apri OBS per iniziare a registrare.
Apri il mixer per cambiare le cuffie. Apri due finestre affiancate per spostare tre file.
Cambi finestra per dire "sì" a un agente IA.

OmniBar mette quei bottoni su un bordo dello schermo, dove non occupano spazio finché non
servono. E cambia da sola: se davanti hai Photoshop mostra i comandi di Photoshop, se hai
Excel mostra quelli di Excel, se OBS sta registrando mostra lo stop.

È l'idea della Touch Bar dei MacBook, senza hardware dedicato e aperta a chiunque voglia
aggiungerci qualcosa.

## Cosa saprà fare

| | |
|---|---|
| 🎵 **Multimedia** | Play, pausa, traccia, volume di sistema e per-app, cambio cuffie/casse |
| 📊 **Sistema** | CPU, RAM, GPU, disco, rete, batteria — e le temperature dove è possibile leggerle onestamente |
| 🤖 **IA** | Approvare i permessi di un agente senza cambiare finestra; tradurre, riscrivere e spiegare la selezione in qualsiasi programma |
| 📁 **File Shelf** | Trascini dei file sul bordo, li parcheggi, navighi altrove, li trascini fuori |
| 📸 **Cattura** | Screenshot di una regione, e OCR offline: dal pixel al testo negli appunti |
| 🎬 **OBS** | Registra, stop, cambio scena, replay buffer |
| 📝 **Office e Adobe** | Comandi rapidi per Word, Excel, Photoshop, Illustrator |
| 🗂️ **Esplora file** | Cartella corrente, selezione, ordinamento, rinomina in blocco |
| 🪟 **Finestre** | Snap in zone, mezzo schermo, modalità focus |

E qualsiasi altra cosa: **una estensione può essere solo un file di testo con dei bottoni.**

## Come si estende

Il tier più semplice non richiede di scrivere codice. Un file TOML:

```toml
[extension]
id   = "com.esempio.excel"
tier = "declarative"

[[profile]]
match = { process = "EXCEL.EXE" }

[[profile.widget]]
type   = "button"
icon   = "sigma"
tooltip = "Somma automatica"
action = { kind = "keystroke", keys = "Alt+=" }
```

Il tier avanzato è un processo che parla JSON-RPC su una named pipe — in qualsiasi
linguaggio. Un'estensione **non disegna mai pixel**: descrive un albero di widget, e l'host
lo renderizza. È così che 40 estensioni di 40 autori diversi sembrano tutte OmniBar, e che
una che crasha non porta giù la barra.

Il protocollo è in [`docs/extension-api.md`](docs/extension-api.md).

## I vincoli, che sono il punto

OmniBar è pensata per essere installabile ovunque, anche accanto a un anti-cheat, senza
allarmare un antivirus e senza rallentare niente.

- **Nessuna injection, nessun hook globale, nessun driver kernel.** Mai.
- **Non ruba mai il focus**, nemmeno quando chiede attenzione.
- **Costo a riposo ≈ zero**: nessun render loop, e le estensioni che non servono non girano.
- **Nessun account, nessuna telemetria, nessuna rete non richiesta.**
- **Portable**: può girare da una chiavetta senza scrivere fuori dalla propria cartella.
- Ogni permesso di un'estensione è dichiarato e approvato esplicitamente.

I dettagli, e il perché di ognuno, sono nell'[architettura](docs/architecture.md).

## Stato

| Fase | | |
|---|---|---|
| 0 | Fondamenta — repo, build, config | 🚧 in corso |
| 1 | La barra — bordo, layer UI, estensioni dichiarative | ⬜ |
| 2 | Il contesto — profili per app, media, volume | ⬜ |
| 3 | Moduli — sysmon, cattura, shelf, finestre | ⬜ |
| 4 | IA — approvazioni, utilizzo, azioni sulla selezione | ⬜ |
| 5 | Ecosistema — SDK e adapter OBS/Office/Adobe/Explorer | ⬜ |
| 6 | Prodotto — editor visuale, installer, firma | ⬜ |

La [roadmap](docs/roadmap.md) spiega cosa collauda ogni fase e quando si considera finita.

## Compilazione

Requisiti: Windows 11 x64, e la toolchain portable che si ricostruisce da pacchetti
ufficiali senza installare niente.

```powershell
.\tools\get-msvc.ps1     # MSVC, Windows SDK, CMake, Ninja in tools\
.\build.ps1              # -> dist\omnibar.exe
```

## Parentela con MiniBar

OmniBar nasce da [MiniBar](https://github.com/AceisX/minibar-now-playing-overlay), una
barretta musicale per Windows 11 da 7 MB di RAM e 300 KB di eseguibile. Ne eredita il
nucleo — rendering Direct2D software, AppBar, foreground watcher, SMTC, Core Audio — e
soprattutto i vincoli, che su un'app sempre residente contano più delle funzioni.

MiniBar resta un prodotto a sé: se ti serve solo controllare la musica, quella fa quello e
costa meno.

## Licenza

MIT.
