# OmniBar — Roadmap

Ogni fase produce qualcosa di **usabile**, non un pezzo di infrastruttura da mettere via in
attesa del resto. Ogni fase collauda un meccanismo architetturale prima che ci si costruisca
sopra: se il meccanismo è sbagliato, si scopre in quella fase e non tre mesi dopo.

I vincoli della [§2 dell'architettura](architecture.md) e i budget della §3 valgono da subito:
non sono qualcosa da "sistemare alla fine".

---

## Fase 0 — Fondamenta

**Collauda:** niente. Prepara il terreno.

> **Anticipato dalla fase 6:** la firma del codice. Su Windows 11 con Smart App Control attivo
> un eseguibile non firmato viene bloccato all'avvio, quindi senza firma il programma non è
> provabile — nemmeno da chi lo scrive. Vedi [security.md §6](security.md).

- [x] Repo, licenza MIT, CI GitHub Actions
- [x] Toolchain portable in `tools\` (MSVC, CMake, Ninja da pacchetti ufficiali)
- [x] CMake, preset locale (Ninja) e preset CI (Visual Studio), CRT statica
- [x] `core`: entry point, single-instance, message loop, logging con rotazione, percorsi
      portable/`%APPDATA%`
- [x] Il gate della CI sulla dimensione dell'eseguibile
- [x] Ambiente di prova in Windows Sandbox (`tools\sandbox\`)
- [ ] **`config`: parser TOML, schema, validazione con errori leggibili, hot-reload** ← in corso
- [ ] `core`: crash handler
- [ ] Il gate della CI su RAM e tempo di avvio
- [ ] Template di issue e PR
- [ ] **Firma del codice** — bloccante per provare su macchine con Smart App Control

---

## Fase 1 — La barra

**Collauda:** il layer UI e la macchina a stati del bordo. È la fase più importante di tutte:
tutto il resto ci sta sopra.

**Forma e comportamento**

- [x] `shell`: finestra sui quattro bordi, DPI per-monitor, multi-monitor, tema chiaro/scuro
- [x] Macchina a stati `Hidden → Revealed → Pinned`
- [x] La finestra non si muove mai: copre tutto il bordo, e ad aprirsi è ciò che ci si disegna
- [x] Profilo continuo — linea sottile, spalle raccordate, pannello — come una figura sola
- [x] Lunghezza automatica: la barra è lunga quanto il suo contenuto
- [x] Zona trigger con soglia doppia tempo + distanza, isteresi in uscita
- [x] Polling del cursore adattivo (10 Hz lontano, 125 Hz vicino) con isteresi sulla soglia
- [x] Animazione con risoluzione del timer alzata solo mentre serve
- [ ] Gli stati `Peek` (superato: è una variante di disegno) e `Suppressed`
- [ ] Esclusioni esplicite per hot corner e snap layout
- [ ] Hotkey globale di apertura, navigazione completa da tastiera

**Disegno**

- [x] `render`: Direct2D software + `UpdateLayeredWindow`, con ridisegno parziale e area sporca
- [x] Temi chiaro e scuro come token, mai colori cablati nel disegno
- [x] **Colori dalla personalizzazione di Windows**, con cambio a caldo
- [x] Opacità sul fondo e non sulla finestra, così il contenuto resta pieno
- [x] Icone come glifi di Segoe Fluent Icons, indirizzate per nome logico
- [x] Modalità compatta sui bordi laterali: spariscono le etichette, restano le icone

**Layer UI**

- [x] `ui`: albero widget, motore di layout flex a due passate, hit-test, animazioni
- [x] Vocabolario: `group`, `button`, `toggle`, `label`, `separator`, `spacer`, `badge`, `avatar`
- [x] Hover e pressione identificati per id, non per puntatore
- [ ] `ui`: provider UIAutomation

**Azioni e sistema**

- [x] `action`: `internal`, `keystroke`, `shell`, `url`, `macro` + controllo dei permessi
- [x] Tray, menu contestuale, autostart
- [ ] **Estensioni dichiarative (TOML): il tier senza codice** ← subito dopo il parser

**Qualità**

- [ ] **Simulatore**: la barra in una finestra normale con contesti finti
- [ ] Golden-image test dei widget

Verificato a schermo: tutti e quattro i bordi, tema chiaro e scuro, DPI 96, lunghezza adattata
al contenuto, apertura e chiusura, click, menu del tray. A riposo: 4,7 MB e 0 ms di CPU su 45
secondi; a regime ~17 MB, stabile.

Le azioni `keystroke` e `shell` compilano ed esistono ma non sono ancora state provate su un
bersaglio reale: lo saranno con i primi profili dichiarativi.

**Fatto quando:** si può scrivere un TOML con dei bottoni, salvarlo, e avere una barra che si
apre sul bordo e li esegue.

---

## Fase 2 — Il contesto

**Collauda:** il context engine e la risoluzione dei profili. È la fase che trasforma "una
barra" in "OmniBar".

- [ ] `context`: foreground watcher, `ContextSnapshot`, tag con sorgenti dichiarate
- [ ] Rule matcher e risoluzione dei profili (§7.2)
- [ ] Zone `pinned` / `context` / `overflow` / `handle`, con collasso quando lo spazio manca
- [ ] Transizione animata fra profili, con continuità dei widget che hanno lo stesso `id`
- [ ] Modulo **media** — portato da MiniBar (SMTC, copertina, tinta, timeline interpolata)
- [ ] Modulo **volume** — portato da MiniBar (sistema, per-app, cambio dispositivo di output)
- [ ] Replay log dei contesti
- [ ] Profili dichiarativi di default per Word, Excel, PowerPoint, browser, Esplora file

**Fatto quando:** aprire Excel cambia i bottoni della barra, e la musica si controlla da
qualunque contesto.

---

## Fase 3 — I moduli built-in

**Collauda:** l'API dei moduli, i widget di dato (`meter`, `sparkline`), i pannelli espansi e
il drag&drop.

- [ ] Vocabolario v1 completo: `meter`, `sparkline`, `slider`, `segmented`, `progress`, `image`, `swatch`, `panel`, `list`, `grid`
- [ ] Stato `Expanded` e pannelli
- [ ] Modulo **sysmon** — PDH per CPU/RAM/disco/rete/GPU, NVML/ADLX/IGCL a runtime, temperature come da §10.1
- [ ] Modulo **capture** — screenshot di regione e finestra, OCR offline della regione
- [ ] Modulo **shelf** — `IDropTarget`, miniature, persistenza, drag in uscita
- [ ] Modulo **power** — batteria, piano energetico, luminosità, night light
- [ ] Modulo **window** — snap in zone, sposta su metà/terzo, modalità focus
- [ ] Modulo **discover** — il provider generico via UI Automation (§10.2): comandi e
      scorciatoie letti da qualunque app, e profilo proposto in automatico. Va **prima** degli
      adapter, non dopo: cambia cosa agli adapter resta da fare

**Fatto quando:** la barra è già utile tutti i giorni anche senza estensioni esterne.

---

## Fase 4 — L'IA

**Collauda:** lo stato `Attention` e la LocalAPI. È il differenziale del prodotto.

- [ ] `localapi`: named pipe + HTTP su loopback, con autenticazione di sessione
- [ ] CLI `omnibar` (`ask`, `notify`, `tag`, `profile`)
- [ ] Widget `prompt` e stato `Attention` con timeout e default sicuro
- [x] **L'agente**: ultimo slot della barra, disegnato in Direct2D, segue il cursore con la
      testa e con gli occhi, sbatte le ciglia, e cliccandolo si mette in silenzio (§13.3)
- [ ] Le altre espressioni: "sta pensando", "fatto", "qualcosa non va"
- [ ] Hook `PreToolUse` per Claude Code: approvazioni sulla barra (§13.1)
- [ ] Regole "Sempre" visibili e revocabili dalle impostazioni
- [ ] `UsageProvider`: `claude-code-local`, e gli altri dietro configurazione (§13.2)
- [ ] Azioni IA sulla selezione: traduci, riscrivi, riassumi, spiega (§11.3)
- [ ] Modulo **clipboard** con cronologia e pin

**Fatto quando:** si può lavorare con un agente per un'ora senza mai cambiare finestra per
approvare qualcosa.

---

## Fase 5 — L'ecosistema

**Collauda:** l'ExtHost e l'SDK. È il pezzo più grosso, e viene per ultimo perché ha bisogno
che tutto il resto sia stabile.

- [ ] `exthost`: manifest, lifecycle con lazy start/stop, named pipe, JSON-RPC, timeout, budget
- [ ] Job object, permessi, prompt di consenso in italiano comprensibile
- [ ] SDK e schema JSON dei widget generati da un IDL unico
- [ ] Binding di riferimento: Node, Python, C#
- [ ] Adapter **OBS** (obs-websocket v5): registra, stop, scena, replay buffer
- [ ] Adapter **Explorer** (`IShellWindows`): cartella corrente, selezione, ordina, rinomina in blocco
- [ ] Adapter **Office** (COM): Excel e Word, comandi rapidi e macro
- [ ] Adapter **Adobe** (COM, fallback scorciatoie): Photoshop e Illustrator, export rapido, azioni
- [ ] Fuzzing del JSON-RPC e dei file di configurazione
- [ ] Documentazione "scrivi la tua estensione in 20 minuti"

**Fatto quando:** una persona esterna al progetto pubblica un'estensione senza chiedere niente
a nessuno.

---

## Fase 6 — Il prodotto

**Collauda:** che sia installabile e usabile da chi non legge documentazione.

- [ ] Editor visuale dei profili — processo separato, avviato su richiesta. Deve coprire i
      quattro requisiti di [architettura §7.4](architecture.md): stile, contenuto per
      applicazione, distinzione fra fissi e contestuali, e il fatto che **l'utente normale non
      debba mai aprire il TOML**. La scelta fra nativo e WebView2 si fa **qui**, quando si sa
      quanto è complesso davvero: fino a questo punto le due strade sono identiche
- [ ] Prima esecuzione guidata: bordo, profili suggeriti in base ai programmi installati
- [ ] Installer, winget, scoop
- [ ] `omnibar-helper.exe` elevato, opzionale, con la sua revisione di sicurezza
- [ ] Aggiornamento con conferma dell'utente, mai silenzioso
- [ ] Documentazione utente, sito, galleria delle estensioni

**Fatto quando:** si può consigliare a qualcuno che non sa cosa sia un file TOML.

---

## Cose deliberatamente rimandate

Non perché non siano buone, ma perché ognuna è un progetto a sé e adesso ruberebbe ossigeno:

- Sincronizzazione cloud dei profili
- Marketplace ospitato delle estensioni
- Input alternativi: gamepad, MIDI, Stream Deck che pilotano le stesse azioni
- Modalità riunione (mute microfono globale, toggle webcam, push-to-talk)
- Snippet e text expansion
- Timer / pomodoro con DND automatico
- Color picker e righello a schermo
- Companion mobile

Ognuna di queste, quando arriverà, deve essere **un'estensione**. Se una di esse richiedesse
di toccare l'host, vuol dire che l'architettura ha un buco e va sistemata l'architettura.
