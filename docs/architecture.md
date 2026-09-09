# OmniBar — Architettura

> La barra contestuale di Windows: nascosta su un bordo, si apre al passaggio del mouse,
> cambia contenuto in base al programma in primo piano, e si lascia estendere da chiunque.
> Questo documento spiega **perché** le cose sono fatte così e **quali vincoli** non si
> violano mai. È il documento di riferimento del progetto: se una feature non trova posto
> qui dentro, non entra nel codice.

| | |
|---|---|
| **Target** | Windows 11 x64 (Windows 10 22H2 best-effort) |
| **Stack** | C++20 · Win32 · Direct2D/DirectWrite · C++/WinRT · DirectComposition (opzionale) |
| **Dipendenze** | Solo Windows SDK per l'host. Le estensioni sono libere. |
| **Licenza** | MIT |
| **Eredità** | Riusa e generalizza il nucleo di [MiniBar](https://github.com/AceisX/minibar-now-playing-overlay) |

---

## 1 · L'idea

Una barra che sta su un bordo dello schermo, invisibile finché non serve, e che **cambia
contenuto in base a cosa stai facendo**. Non un launcher, non una dock, non un pannello di
widget: un *telecomando contestuale* per il computer.

Il modello mentale è la Touch Bar dei MacBook — ma senza hardware dedicato, su qualunque PC
Windows, e aperta a chiunque voglia aggiungerci qualcosa.

Il pubblico primario è chi lavora **su un monitor solo**, o su un laptop: le persone per cui
ogni finestra aperta è spazio rubato, per cui il drag&drop fra due cartelle è un supplizio, e
che passano la giornata a fare Alt-Tab per premere un bottone e tornare indietro. La barra dà
loro quei bottoni senza rubare spazio: a riposo occupa cinque pixel sul bordo, ed è lunga
quanto il suo contenuto — non una percentuale dello schermo.

Tre principi da cui discende tutto il resto:

- **Il costo a riposo è zero.** La barra è sempre in esecuzione, quindi deve costare come se
  non lo fosse. Nessun render loop, nessun polling costoso, moduli e plugin spenti finché il
  loro contesto non si accende. Se una feature non rispetta il budget della §3, non entra.
- **Mai toccare gli altri processi.** Niente injection, niente hook globali, niente driver
  kernel. Tutto ciò che OmniBar sa degli altri programmi lo impara da API pubbliche
  out-of-process. È ciò che la rende sicura accanto a un anti-cheat e installabile senza far
  suonare Defender.
- **L'host renderizza, le estensioni descrivono.** Un'estensione non riceve mai una superficie
  di disegno. Dichiara un albero di widget; l'host lo disegna. Da qui vengono coerenza visiva,
  isolamento dai crash, sicurezza, e la libertà di scrivere un plugin in qualsiasi linguaggio.

---

## 2 · Vincoli non negoziabili

1. **Nessuna DLL injection, nessun hook su DirectX/Vulkan, nessun `SetWindowsHookEx` globale,
   nessun driver kernel.** Sono i pattern che EAC, BattlEye e Vanguard trattano come cheat, e
   quelli che rendono un installer sospetto. Tutto passa da API "da fuori":
   `SetWinEventHook` in `WINEVENT_OUTOFCONTEXT`, `GetCursorPos`, Core Audio, PDH, COM
   automation, WinRT.
2. **La barra non ruba mai il focus.** Nemmeno quando chiede attenzione. `WS_EX_NOACTIVATE`
   sempre, `SW_SHOWNOACTIVATE` sempre. Una barra che ti porta via il cursore dal testo che
   stai scrivendo viene disinstallata il giorno stesso.
3. **Un'estensione non può degradare la barra.** Gira fuori processo, con timeout e budget di
   messaggi. Se non risponde, il suo pannello mostra un errore e il resto continua.
4. **Ogni capability è dichiarata e approvata.** Eseguire comandi, simulare tastiera, leggere
   gli appunti, uscire in rete: sono privilegi che un'estensione dichiara nel manifest e che
   l'utente concede esplicitamente. Non esistono per default.
5. **Nessun account obbligatorio, nessuna telemetria, nessuna rete non richiesta.** L'app
   funziona offline. Le uniche connessioni sono quelle che un'estensione dichiara e che
   l'utente ha approvato.
6. **Portable prima di tutto.** L'app deve poter girare da una chiavetta senza scrivere nulla
   fuori dalla propria cartella. L'installer è una comodità, non un requisito.
7. **Accettato per design:** sopra il fullscreen *esclusivo* la barra non si vede. Nessun
   workaround. Il borderless windowed, che è quello che usano quasi tutti i giochi oggi,
   funziona.

### 2.1 · L'unica eccezione: l'helper elevato

OmniBar gira **non elevata**. Conseguenza di UIPI: non può mandare input a finestre di
processi elevati (Task Manager, regedit, un'app lanciata come amministratore). Per chi ne ha
bisogno esiste `omnibar-helper.exe`, **opzionale**, installato a parte, che gira elevato e fa
**solo** ciò per cui servono i privilegi.

Le regole che lo rendono accettabile invece che un buco:

- **Vocabolario chiuso.** Il helper accetta un insieme fisso di comandi tipizzati
  (`send_input`, `read_sensor`, …). Non esiste un comando "esegui questa stringa". Mai.
- **Named pipe con ACL sul solo utente corrente**, più un token di sessione generato all'avvio
  e passato all'host: un altro processo dell'utente non può parlargli.
- **Nessun avvio automatico per default.** Parte su richiesta, si spegne dopo l'inattività.
- **Non carica estensioni.** Le estensioni non parlano mai con il helper: passano dall'host,
  che filtra.
- Il codice del helper sta in una cartella a sé, è piccolo per costruzione, e ogni riga che ci
  si aggiunge è una decisione di sicurezza da motivare in `docs/security.md`.

---

## 3 · Budget prestazioni

Requisiti, non speranze. La CI misura e fallisce se si sfora.

| Metrica | Target | Note |
|---|---|---|
| RAM host appena avviato | **< 10 MB** | misurato 4,2 MB |
| RAM host a regime, nessun modulo attivo | **< 25 MB** | misurato ~17 MB dopo ore d'uso e decine di aperture, **stabile**: 17,02 → 16,94 MB su 12 aperture, handle invariati. Il numero appena avviato non e' il numero vero: le cache di Direct2D e DirectWrite si riempiono usandola |
| RAM host a regime, moduli built-in attivi | **< 60 MB** | con sysmon, media, shelf |
| CPU a riposo (barra nascosta) | **≤ 0,2 %** | misurato 0,10 % su 60 s, cursore lontano. Il contatore di Windows ha un quanto di 15,6 ms: sotto quella soglia la misura è rumore |
| CPU durante l'animazione di apertura | < 2 % di un core | 0 % se il backend di composizione muove la visual |
| GPU a riposo | 0 % | nessun lavoro fra un ridisegno e l'altro |
| Avvio → barra pronta a reagire | **< 250 ms** | processo lanciato → zona trigger attiva |
| Latenza hover → barra visibile | **< 80 ms** | percepita come istantanea |
| Latenza click → azione partita | **< 30 ms** | |
| Dimensione host | ≤ 4 MB | eseguibile singolo, CRT statica |
| Impatto sul frametime di un gioco | non misurabile | PresentMon con e senza barra |

Il principio che li rende raggiungibili è quello di MiniBar portato alle estreme conseguenze:
**zero lavoro se non cambia niente**, esteso a **zero processi se non servono**. Con cinquanta
estensioni installate, a riposo ne girano zero.

---

## 4 · Decisioni e motivi

| Decisione | Scelta | Motivo |
|---|---|---|
| Linguaggio / UI | C++20, Win32 + Direct2D | La barra è sempre residente: il costo a riposo è il vincolo dominante. WebView2 costa 60-100 MB e un processo figlio; .NET costa un runtime; Electron è fuori discussione. Il nativo sta in pochi MB e parla direttamente col sistema. |
| Rendering | D2D software + `UpdateLayeredWindow` | MiniBar ha misurato la composizione DComp a 37,7 MB / 26 thread contro 8,7 MB / 11 thread. Il dubbio era che su una barra animata il verdetto si ribaltasse, perché con `UpdateLayeredWindow` un'animazione costa un memcpy della superficie per frame. **Non si ribalta:** lo scorrimento di apertura è un `SetWindowPos` fra due rettangoli, e una finestra layered che si sposta non ridisegna nulla — zero memcpy, zero ridisegni. Il vantaggio che ci si aspettava da DComp si ottiene senza allocare un device D3D. Il backend di composizione resta previsto ma non ha più un motivo: lo avrà quando ci sarà da animare un *contenuto*, non una posizione. |
| Modello UI | Layer retained-mode proprietario: albero widget, layout flex, animazioni, hit-test, UIA | È l'investimento fondante: ci sta sopra tutto il resto. Fatto bene, un modulo nuovo costa mezza giornata invece di una settimana. E senza provider UIAutomation l'app non è installabile "di default per tutti". |
| Estensioni | Dichiarano un albero di widget, non disegnano | Coerenza visiva, isolamento dai crash, sicurezza (nessuna finta finestra di login sulla barra), qualsiasi linguaggio, un solo renderer da ottimizzare. |
| Transport estensioni | JSON-RPC 2.0 su named pipe, un processo per estensione | Language-agnostic, crash-isolato, supervisionabile, con timeout. Se sa scrivere JSON su una pipe, è un plugin. |
| Estensioni senza codice | Tier dichiarativo: un TOML con bottoni → scorciatoia / comando / URL | Copre l'80% dei casi (comandi rapidi di Office, Adobe, browser) con zero righe di codice, e rende la barra customizzabile da chi non programma. |
| Contesto | `SetWinEventHook` out-of-context + snapshot tipizzato + rule matcher | Reagire al cambio di foreground è pulito e gratuito. Il contesto non è solo "quale app": include stato (registrazione in corso, batteria, IA in attesa). |
| Configurazione | TOML, per profilo, con hot-reload | L'INI di MiniBar non regge profili annidati e liste di widget. TOML è leggibile e scrivibile a mano, che resta un requisito. |
| Posizione config | `%APPDATA%\OmniBar\`, modalità portable accanto all'eseguibile | Portable resta possibile; il default segue le convenzioni di Windows perché l'obiettivo è l'installazione di massa. |
| Icone | Segoe Fluent Icons di sistema + set proprio come path vettoriali compilati | Nessun file immagine da caricare, scalano a ogni DPI, costano nulla. |
| Build | CMake + Ninja + MSVC, CRT statica, toolchain portable in `tools\` | Ereditata da MiniBar: eseguibile singolo senza VC Redist, ambiente ricostruibile da pacchetti ufficiali senza installare niente. |

---

## 5 · Architettura

### 5.1 · Modello a processi

```
omnibar.exe                    host — sempre in esecuzione, non elevato
  ├── omnibar-ext-<id>.exe     un processo per estensione attiva (lazy start/stop)
  ├── omnibar-editor.exe       editor dei profili — avviato su richiesta, chiuso subito
  └── omnibar-helper.exe       opzionale, elevato, vocabolario chiuso (§2.1)
```

L'host è l'unico processo residente. Tutto il resto nasce quando serve e muore quando non
serve più. Un'estensione che crasha viene riavviata con backoff; alla terza volta in un minuto
resta spenta e il suo pannello mostra il perché.

### 5.2 · Strati dell'host

| Strato | Responsabilità |
|---|---|
| `core` | Event loop, scheduler dei timer, bus dei messaggi, logging, crash handler, single-instance. |
| `shell` | Finestre, macchina a stati del bordo (§8), zona trigger, DPI per-monitor, multi-monitor, tema, tray, hotkey globali, AppBar quando si riserva spazio. |
| `render` | Backend di disegno (layered software / composizione), atlas delle icone, testo, temi. |
| `ui` | Layer retained-mode: albero widget, layout flex, animazioni, hit-test, tastiera, provider UIAutomation. È il vocabolario della §6. |
| `context` | Foreground watcher, `ContextSnapshot`, sorgenti dei tag, rule matcher, risoluzione del profilo attivo. |
| `action` | `ActionBus`: esecuzione tipizzata di keystroke / shell / url / internal / rpc / macro, con controllo dei permessi e rate limit. |
| `modules` | Moduli built-in in-process: media, volume, sysmon, capture, shelf, window-manager, clipboard, notes. |
| `exthost` | Supervisore delle estensioni: manifest, lifecycle, named pipe, JSON-RPC, timeout, permessi, sandbox del processo. |
| `config` | TOML: schema, validazione con errori leggibili, merge dei livelli, hot-reload, migrazione di versione. |
| `localapi` | Endpoint locale per strumenti esterni: hook di Claude Code, CLI `omnibar`, script. |

**Regola di dipendenza:** gli strati si vedono solo verso il basso, e `modules` / `exthost`
non conoscono `render`. Un modulo produce widget, non pixel — esattamente come un'estensione.
Così un modulo built-in può essere estratto in un'estensione (o viceversa) senza riscritture.

### 5.3 · Il flusso di un frame

```
evento (foreground / timer / pipe / input)
   → aggiorna stato del produttore (modulo o estensione)
   → produttore emette albero widget
   → LayoutEngine fa il diff con l'albero precedente
   → se qualcosa è cambiato: layout → disegno della sola regione sporca → present
   → altrimenti: niente
```

Il diff dell'albero è ciò che rende sostenibile avere widget che si aggiornano spesso: un
`meter` della CPU che cambia valore ridisegna il proprio rettangolo, non la barra.

---

## 6 · Il modello dei widget

È il cuore del progetto. Un produttore (modulo o estensione) restituisce un albero JSON di
nodi presi da un **vocabolario chiuso**. L'host lo valida, ne fa il diff, lo dispone e lo
disegna.

### 6.1 · Vocabolario v1

| Widget | A cosa serve | Stato |
|---|---|---|
| `button` | azione singola; icona, etichetta, o entrambe | `enabled`, `pressed`, `badge` |
| `toggle` | on/off persistente | `on` |
| `segmented` | scelta fra 2-5 opzioni | `selected` |
| `slider` | valore continuo (volume, opacità, dimensione pennello) | `value`, `min`, `max` |
| `meter` | percentuale con colore soglia (CPU, RAM, batteria) | `value`, `thresholds` |
| `sparkline` | serie storica breve | ring buffer, N campioni |
| `progress` | avanzamento determinato o indeterminato | `value` o `indeterminate` |
| `label` | testo, con marquee se sborda | `text`, `emphasis` |
| `image` | miniatura (copertina, anteprima file) | sorgente scalata a display size |
| `swatch` | colore | `rgba` |
| `badge` | pallino/contatore su un altro widget | numero o punto |
| `group` | contenitore con direzione, gap, allineamento | flex |
| `separator`, `spacer` | struttura | |
| `panel` | contenitore che apre lo stato `Expanded` (§8) | può contenere `list`, `grid` |
| `list` / `grid` | solo dentro `panel`: righe/celle selezionabili e trascinabili | virtualizzate |
| `prompt` | domanda con risposte (Consenti / Nega / Sempre) — forza `Attention` | timeout, default |

Aggiungere un widget al vocabolario è una decisione architetturale, non una feature: va
motivata, disegnata per tutti i temi e i DPI, e coperta da golden-image test.

### 6.2 · Un albero, in concreto

```json
{
  "id": "sysmon",
  "widget": {
    "type": "group", "direction": "row", "gap": 8,
    "children": [
      { "type": "meter", "id": "cpu", "label": "CPU", "value": 0.34,
        "thresholds": [0.7, 0.9] },
      { "type": "sparkline", "id": "cpu.hist", "samples": [12, 18, 34, 29] },
      { "type": "meter", "id": "gpu", "label": "GPU", "value": 0.71 },
      { "type": "button", "id": "open", "icon": "chart", "tooltip": "Dettagli",
        "action": { "kind": "internal", "name": "sysmon.expand" } }
    ]
  }
}
```

L'host risponde agli eventi con `widget/event`:

```json
{ "method": "widget/event",
  "params": { "id": "cpu", "kind": "click", "modifiers": ["ctrl"] } }
```

### 6.3 · Perché un vocabolario chiuso e non "disegna quello che vuoi"

| Se le estensioni disegnassero | Con il vocabolario chiuso |
|---|---|
| 40 plugin, 40 stili diversi | tutto sembra OmniBar, e cambia con il tema |
| un plugin lento blocca il frame | un plugin lento fa scadere il suo timeout, il frame no |
| un plugin può disegnare una finta richiesta di password | non ha pixel su cui farlo |
| ottimizzare il rendering è impossibile | un solo renderer da ottimizzare |
| il plugin deve conoscere D2D e i DPI | il plugin scrive JSON |
| l'accessibilità è irrecuperabile | l'albero widget *è* l'albero UIA |

L'ultimo punto da solo basterebbe: con un vocabolario chiuso l'accessibilità si ottiene una
volta nell'host, per tutte le estensioni, per sempre.

---

## 7 · Il context engine

### 7.1 · Lo snapshot

Prodotto a ogni cambio di foreground (evento, non polling) e a ogni cambio di tag:

```
ContextSnapshot {
  process_name        "PHOTOSHOP.EXE"
  process_path        "C:\Program Files\Adobe\...\Photoshop.exe"
  aumid               (per le app impacchettate)
  window_class        "Photoshop"
  window_title        "logo-cliente.psd @ 66,7% (RGB/8)"
  monitor             HMONITOR + DPI
  is_fullscreen       bool
  document_path       opzionale, da un adapter o da UIA
  explorer_folder     opzionale, quando il foreground è Esplora file
  explorer_selection  opzionale, i file selezionati
  tags[]              recording · in_call · on_battery · ai_waiting · dnd · low_disk …
}
```

I **tag** sono la parte che distingue OmniBar da una semplice barra per-app: il contesto non
è soltanto *quale programma*, è anche *cosa sta succedendo*. "OBS sta registrando" e "l'IA
aspetta una risposta" sono contesti quanto "Photoshop è davanti". Ogni tag ha una sorgente
dichiarata (un modulo o un'estensione) e un costo: le sorgenti costose si attivano solo se
almeno una regola le usa.

### 7.2 · Risoluzione del profilo

Le regole si valutano dalla più specifica alla più generica; la prima che matcha vince, e le
regole dell'utente battono sempre quelle di default:

```
1. regola utente con match su processo + titolo/tag
2. regola utente con match su processo
3. profilo dell'estensione per quel processo
4. profilo di categoria (browser, office, editor grafico, IDE, gioco)
5. profilo di default
```

Il risultato è un **Profilo**: quali produttori occupano quali zone, in quale ordine, con
quale layout. Il cambio di profilo è animato con un cross-fade breve, e i widget con lo stesso
`id` mantengono la propria posizione invece di sparire e riapparire.

### 7.3 · Le zone

La barra è divisa in zone con priorità diversa. Con poco spazio (bordo laterale, schermo
piccolo) le zone a priorità bassa collassano nell'overflow.

| Zona | Contenuto | Priorità |
|---|---|---|
| `alert` | richieste che pretendono risposta: approvazione IA, temperatura critica | massima, sopra tutto |
| `pinned` | ciò che l'utente vuole sempre: media, meter, orologio | alta |
| `context` | i produttori del profilo attivo | media, è quella che cambia |
| `overflow` | ciò che non ci sta, dietro un bottone | bassa |
| `handle` | pin, impostazioni, indicatore di stato | fissa |

---

## 7.4 · Il modello di personalizzazione

Quattro requisiti, e non sono desideri: decidono la forma della configurazione, del parser e
dell'editor. Vanno tenuti presenti da qui in avanti, perché sono facili da rispettare adesso e
molto costosi da recuperare dopo.

### 1 · Lo stile è dell'utente

Colori, opacità, spessore, raggi, bordo, lunghezza, dimensione delle icone: tutto
sovrascrivibile. I nostri valori sono **default**, non decisioni.

Il colore di partenza arriva già dalla personalizzazione di Windows (§13.3), che è il modo
giusto di avere un default: non è una nostra scelta imposta a tutti, è la scelta che l'utente
ha già fatto altrove.

### 2 · Il contenuto è dell'utente, per ogni applicazione

Si può decidere cosa c'è nella barra **di default** e cosa c'è **per ogni singolo programma**.
E si può scegliere fra tre modi, per ciascun programma:

| Modo | Cosa succede |
|---|---|
| **Esplicito** | l'utente elenca i widget per quel programma |
| **Uno per tutti** | nessuna barra per-programma: vale sempre quella di default |
| **Automatico** | il sistema propone da sé i widget, leggendo comandi e scorciatoie dell'app (§10.2) |

Il terzo modo è ciò che rende utile la barra su un programma che nessuno ha mai adattato, ed è
il motivo per cui il provider generico viene **prima** degli adapter e non dopo.

### 3 · Fisso e contestuale restano distinti, e la linea la traccia l'utente

Ci sono due categorie di widget, e non devono mai confondersi:

- **fissi** — ci sono sempre, qualunque programma tu stia usando (media, volume, sistema,
  l'agente);
- **contestuali** — cambiano con il programma in primo piano.

La barra li tiene in zone diverse (§7.3) proprio per questo. Ma **quali** widget stiano
nell'una o nell'altra lo decide l'utente: un widget non è fisso o contestuale per natura, lo è
per scelta. Chi vuole gli strumenti di Photoshop sempre disponibili deve poterli inchiodare
nella zona fissa, e chi non vuole vedere il volume quando gioca deve poterlo togliere.

Perché la distinzione conti davvero, deve **vedersi**: la zona che cambia e quella che resta
non possono sembrare la stessa cosa, altrimenti a ogni cambio di programma sembra che la barra
si sia riorganizzata da sola.

### 4 · Si configura da una finestra, non da un file

Il file TOML è il formato, non l'interfaccia. Deve restare leggibile e scrivibile a mano —
serve a chi vuole versionare i propri profili o generarli da uno script — ma **l'utente normale
non deve mai aprirlo**.

La finestra di configurazione è la fase 6, e viene per ultima di proposito: si progetta bene
solo quando si sa cosa c'è davvero da configurare. Ma la configurazione va progettata **da
subito** come se quell'editor esistesse già, perché un formato pensato solo per essere scritto
a mano è un formato che l'editor poi non riesce a modificare senza distruggere i commenti e
l'ordine.

---

## 8 · La macchina a stati della barra

```
              hover sul bordo (soglia tempo + distanza)
   Hidden ──────────────────────────────────────────► Revealed
     ▲                                                  │ │
     │        uscita cursore + isteresi                 │ │  click sul pin
     └──────────────────────────────────────────────────┘ ▼
                                                        Pinned ──► (AppBar: riserva spazio)
   qualunque stato ──[ un produttore chiede attenzione ]──► Attention
   Revealed/Pinned ──[ un widget apre il suo panel ]──────► Expanded
   qualunque stato ──[ fullscreen escl. / presentazione / DND ]──► Suppressed
```

| Stato | Cosa si vede | Note |
|---|---|---|
| `Hidden` | una linguetta di pochi pixel sul bordo — il bordo della barra che sporge — o niente con `peek = false` | default |
| `Revealed` | la barra aperta | si richiude da sola quando il cursore esce, con isteresi |
| `Pinned` | aperta e fissata | opzionalmente registra l'AppBar e riserva spazio |
| `Attention` | aperta, con un bordo che pulsa e il widget `prompt` in zona `alert` | **non si richiude da sola**; non ruba il focus |
| `Expanded` | un pannello più grande sopra la barra | grafici, shelf, liste |
| `Suppressed` | niente, nemmeno la zona trigger | fullscreen esclusivo, presentazione, DND |

### 8.0 · Il velo sta sul fondo, non sulla finestra

La barra è velata all'85 %, ma **l'85 % ce l'ha il fondo, non l'intera finestra.**

La differenza non è un dettaglio di implementazione. Velando tutta la finestra — cioè
abbassando l'alpha di `UpdateLayeredWindow` — diventano trasparenti anche le icone e l'avatar:
la barra si posa meglio su quello che ha sotto, ma il contenuto perde contrasto proprio dove
serve, e su uno sfondo movimentato le icone cominciano a confondersi con ciò che traspare.

Velando il solo colore di fondo si ottiene la stessa leggerezza con il contenuto che resta
pieno. È così che si comportano i pannelli di Windows, ed è il motivo per cui l'opacità nella
configurazione agisce sul fondo e non sul present.

Resta un limite da conoscere: su uno sfondo chiaro una barra chiara e velata perde definizione,
e a tenerla staccata rimane il solo bordo. È il prezzo della trasparenza, e non lo si paga con
un numero più alto — lo si paga scegliendo se si vuole leggerezza o contrasto.

### 8.1 · Una linea ferma che si apre

A riposo la barra è **una linea di due punti che corre per tutto il bordo**, con una piccola
sporgenza arrotondata al centro. Non si muove. Al passaggio del cursore la sporgenza si apre
nella barra, e richiudendosi torna sporgenza.

Ci si è arrivati scartando due strade, e vale la pena scrivere perché:

- **La barra inseguiva il cursore** lungo il bordo, scorrendo per andargli incontro.
- **Il profilo si gonfiava** in due gocce che seguivano il cursore con ritardi diversi.

Entrambe erano fluide e nessuna delle due era giusta, per la stessa ragione: **un componente
di sistema non si sposta.** Sta dov'è e cambia forma. Un oggetto che si muove verso di te
chiede attenzione — va benissimo per una notifica, è sbagliato per una cosa che sta sul bordo
dello schermo tutto il giorno. La barra deve essere trovabile, non insistente.

La linea fissa risolve anche il problema che la pastiglia corta aveva: si vedeva poco e non
si capiva cosa fosse. Una linea che corre per tutto il bordo si legge subito come "qui c'è
qualcosa", senza muoversi di un pixel.

**A riposo e da aperta si disegnano le stesse due cose** — la linea e un pannello arrotondato
al suo centro — con misure diverse. Non c'è una forma che entra da fuori campo né una che ne
sostituisce un'altra: c'è una cosa ferma che si apre. Una sola progressione governa spessore,
lunghezza e comparsa delle icone, così non possono sfasarsi.

**Nessun oltrepasso nella curva.** C'era, e su un oggetto che entrava da fuori raccontava una
massa; su un oggetto fermo che si apre non racconta niente e si legge come un tic.

### 8.1.1 · La fluidità è un problema di temporizzazione, non di grafica

Perché il movimento sembrasse "forzato" c'era una causa misurabile, e non era la curva.

L'animazione era guidata da `SetTimer` a 8 ms. **`SetTimer` non sa fare 8 ms:** è agganciato
alla risoluzione del timer di sistema, di norma ~15,6 ms. I timestamp di un'apertura reale lo
dicono senza margine di dubbio:

```
54.382 → 54.398 → 54.413 → 54.429 → 54.445 → 54.460      (16 ms, non 8)
```

Sessantadue fotogrammi al secondo, con spaziatura irregolare rispetto al refresh dello
schermo. Nessuna curva, per quanto ben scelta, sopravvive a una sorgente che batte storto.

La cura è alzare la risoluzione del timer con `timeBeginPeriod(1)` **solo per la durata
dell'animazione** — tenerla alta sempre costerebbe batteria a un programma che sta acceso
tutto il giorno.

Resta un passo più avanti, se non bastasse: far **animare al compositore** invece che a noi
(DirectComposition o Windows.UI.Composition). Lì l'interpolazione la fa il sistema sul proprio
thread, in fase con il refresh, e non può saltare fotogrammi perché la nostra applicazione è
occupata. Costa un device D3D — la cifra misurata da MiniBar è ~30 MB e una quindicina di
thread — quindi si valuta **dopo** aver verificato che la temporizzazione corretta non basti.
Ottimizzare il motore prima di aver sistemato l'orologio sarebbe stato lavoro sprecato.

### 8.2 · Perché il reveal è più difficile di quanto sembri### 8.2 · Perché il reveal è più difficile di quanto sembri

Tre problemi che decidono se la barra è piacevole o insopportabile dopo due giorni:

- **Falsi positivi.** Un cursore che sfiora il bordo mentre va altrove non deve aprire niente.
  Serve una soglia doppia: il cursore deve restare nella zona trigger per `reveal_delay_ms` **e**
  aver percorso meno di `reveal_travel_px` in quel tempo. Un cursore lanciato che rimbalza sul
  bordo ha una velocità alta: si scarta.
- **Conflitti con Windows.** La zona trigger non deve coprire l'area della taskbar (nemmeno se
  in auto-hide), gli hot corner, né il bordo dove Windows apre gli snap layout. Le esclusioni si
  ricalcolano a ogni `ABN_POSCHANGED` e a ogni cambio di risoluzione: mai memorizzate.
- **Il bordo occupato dalla taskbar è un problema diverso, non una taratura.** Su un bordo
  libero la zona sensibile può essere di pochi pixel: il cursore ci sbatte contro e si ferma da
  solo, perché oltre non c'è schermo — è il trucco su cui si reggono tutti i bersagli sui bordi.
  Con la taskbar davanti quel trucco sparisce: il bordo della zona sta in mezzo allo schermo, e
  per colpirlo bisogna **fermarsi** nel punto giusto invece di lanciare il mouse. Sei pixel
  diventano impossibili. Quando il nostro bordo è occupato — si vede confrontando il rettangolo
  del monitor con l'area di lavoro — la zona si allarga a venti punti.
- **Il laptop.** Al bordo dello schermo col trackpad ci si arriva male. Serve una scorciatoia
  globale che apra la barra e ci metta il focus da tastiera, e la barra deve essere navigabile
  interamente con le frecce e Invio, senza mai toccare il mouse.

---

## 9 · Azioni e permessi

Ogni cosa cliccabile produce un'`Action` tipizzata. L'`ActionBus` la valida contro i permessi
del produttore prima di eseguirla.

| Kind | Cosa fa | Capability richiesta |
|---|---|---|
| `internal` | chiama una funzione dell'host (volume, media, screenshot, cambio profilo) | nessuna |
| `keystroke` | `SendInput` verso la finestra target | `input` |
| `shell` | esegue un eseguibile con argomenti (mai una stringa di shell) | `exec` |
| `url` | apre un URL o un URI scheme | `open` |
| `rpc` | chiama un metodo dell'estensione che ha prodotto il widget | implicita |
| `macro` | sequenza di azioni con attese, con lo stesso controllo su ognuna | l'unione |

Note che sono decisioni di sicurezza, non dettagli:

- `shell` prende **eseguibile + array di argomenti**, mai una riga di comando da concatenare.
  Non esiste un modo di scrivere una command injection nella configurazione.
- `keystroke` non manda mai input "in generale": ha un target esplicito (la finestra di
  foreground al momento del click, catturata prima che la barra la tocchi), e si rifiuta se il
  target è cambiato nel frattempo.
- Le capability sono nel manifest dell'estensione, e la richiesta all'utente **dice cosa
  significano**: non "questa estensione richiede exec", ma "potrà avviare programmi sul tuo PC".
- Rate limit per produttore: nessuna estensione può inondare l'`ActionBus`.

---

## 10 · Moduli built-in

I moduli vivono nell'host per motivi di latenza o perché usano API che non ha senso duplicare.
Espongono la stessa interfaccia delle estensioni (producono widget, ricevono eventi), quindi
la linea fra "modulo" ed "estensione" è una scelta di packaging, non di architettura.

| Modulo | Fonte dati | Note |
|---|---|---|
| `media` | SMTC (`Windows.Media.Control`) | **portato da MiniBar**: sessioni, metadati, copertina, tinta dominante, comandi, interpolazione della timeline |
| `volume` | Core Audio (`IAudioSessionManager2`, `ISimpleAudioVolume`) | **portato da MiniBar**: volume di sistema, per-app, cambio dispositivo di output |
| `sysmon` | PDH (CPU, RAM, disco, rete, `GPU Engine`), NVML / ADLX / IGCL caricate dinamicamente | temperature: vedi §10.1 |
| `capture` | `Windows.Graphics.Capture`, `Windows.Media.Ocr` | screenshot di regione/finestra, registrazione breve, OCR offline della regione |
| `shelf` | `IDropTarget` + shell COM | area di sosta per il drag&drop dei file (§11.1) |
| `window` | `SetWindowPos`, `EnumWindows`, virtual desktop API | snap in zone, sposta su metà/terzo, modalità focus |
| `clipboard` | `AddClipboardFormatListener` | cronologia locale, pin, mai in rete |
| `power` | `GetSystemPowerStatus`, power scheme API | batteria, piano energetico, luminosità, night light |
| `notes` | file locale | blocco note volante ancorato alla barra |

### 10.1 · Le temperature: dove finisce l'onestà tecnica

Leggere la temperatura della **CPU** in modo affidabile su Windows richiede l'accesso a MSR o
al Super I/O: cioè un driver ring-0. Gli strumenti che lo fanno (LibreHardwareMonitor, HWiNFO)
ne spediscono uno. Per OmniBar è escluso dal vincolo §2.1: un driver kernel firmato è un
problema di sicurezza, di firma, di falsi positivi anti-cheat e di installer, tutto in una
volta.

La strategia, in ordine di preferenza:

1. **GPU** — NVML (NVIDIA), ADLX (AMD), IGCL (Intel), caricate con `LoadLibrary` a runtime. Se
   la libreria non c'è, il widget non appare. Nessuna dipendenza a build time.
2. **CPU e sensori scheda madre** — se l'utente ha **già** HWiNFO o LibreHardwareMonitor in
   esecuzione, se ne leggono i valori dalla loro shared memory / dal loro web server locale.
   Zero driver nostri: si usa quello che l'utente ha già scelto di installare.
3. **Se non c'è nessuna delle due** — il widget della temperatura non compare, e le
   impostazioni spiegano in una riga perché e cosa installare. Meglio un'assenza spiegata di un
   numero inventato.

### 10.2 · Il provider generico: capire un'app senza averla mai vista

Il modello "un adapter per ogni programma" ha un difetto che si vede solo dopo: **finché
qualcuno non scrive l'adapter, la barra su quel programma non sa fare niente.** Con quaranta
programmi installati, trentacinque restano vuoti. E siccome nessuno scriverà mai un adapter
per il gestionale aziendale di chi legge, per molti utenti la barra resterebbe vuota per
sempre.

Su Windows esiste però un canale che sa parlare di **qualunque** applicazione con
un'interfaccia accessibile: **UI Automation**. È l'API che usano gli screen reader — quindi
out-of-process, senza injection, senza privilegi, dentro i vincoli della §2.

Cosa si può sapere di un'app **senza aver scritto una riga per lei**:

| | |
|---|---|
| I suoi comandi | menu, barre degli strumenti, schede della ribbon, con il loro **nome** |
| Come invocarli | pattern `Invoke`, `Toggle`, `Value`, `Selection`, `ExpandCollapse` |
| **La loro scorciatoia** | proprietà `AcceleratorKey` e `AccessKey` |
| Il testo e la selezione | pattern `Text`: cosa c'è scritto, cosa è selezionato, dov'è il cursore |
| Quando qualcosa cambia | eventi di focus, di proprietà, di struttura |

La terza riga è quella che vale più delle altre messe insieme: **UIA dice quale scorciatoia
ha ogni comando.** Significa che la barra può *generare da sola* il profilo dichiarativo di
un programma che nessuno ha mai adattato — leggerne i comandi, leggerne le scorciatoie, e
proporli come bottoni. L'adapter scritto a mano smette di essere il prerequisito e diventa un
miglioramento.

Il modello si ribalta:

| Prima | Con il provider generico |
|---|---|
| niente finché qualcuno non scrive l'adapter | **supporto di base ovunque**, subito |
| l'adapter è il prerequisito | l'adapter aggiunge ciò che il generico non vede |
| le app sconosciute sono invisibili | le app sconosciute si spiegano da sole |

Gli altri canali universali, che non sono UIA ma valgono lo stesso principio — **uno vale per
tutte le app**:

| Canale | Cosa raccoglie da *tutte* le applicazioni |
|---|---|
| `SetWinEventHook` out-of-context | foreground, focus, finestre create, menu aperti, allarmi |
| `UserNotificationListener` | tutte le notifiche toast, di qualunque app |
| SMTC | riproduzione multimediale, di qualunque player |
| Core Audio | volume e picco per applicazione, chi sta suonando |
| Appunti | tutto ciò che l'utente copia |
| Shell COM | cartella corrente, selezione, file recenti |
| Registro dei verbi | i comandi che ogni app registra nel menu contestuale |

**I limiti, detti per intero.** UIA può essere lento su alberi grandi: va interrogato su un
thread a parte, con cache, in modo pigro, e mai a ogni cambio di finestra senza pensarci —
un provider generico che blocca la barra sarebbe peggio di nessun provider. Alcune
applicazioni espongono poco o niente: giochi, Win32 vecchi, Electron con l'accessibilità
spenta. I nomi dei comandi arrivano nella lingua dell'applicazione, non in quella della barra.
E il lettore di notifiche richiede un consenso esplicito ed è sensibile per la privacy: è una
capability, non un default.

**Cosa NON è.** Non è un modo per "leggere tutto quello che le app mandano": non esiste un
rubinetto unico, e prometterlo sarebbe disonesto. È un insieme di canali pubblici che,
sommati, coprono una parte sorprendente del problema — e soprattutto coprono il caso peggiore,
quello dell'applicazione che nessuno ha previsto.

---

## 11 · I moduli che rispondono al "monitor solo"

Sono le funzioni pensate specificamente per il pubblico primario, e vale la pena spiegarle
perché non sono ovvie.

### 11.1 · File Shelf

Trascini dei file verso il bordo: la barra si apre e li accoglie. Restano parcheggiati, con
miniatura. Navighi dove vuoi, anche chiudendo la finestra di partenza. Poi li trascini fuori.

Su un monitor solo, spostare file fra due cartelle costa o due finestre affiancate (metà
schermo ciascuna) o taglia/incolla con la memoria di dove eri. La shelf toglie il problema, e
non esiste su Windows.

Dettagli: la shelf tiene **riferimenti**, non copie, finché non si trascina fuori (e allora è
Windows a fare la copia/spostamento con la sua semantica). Sopravvive al riavvio. Un file
sparito dal disco resta come voce barrata invece di svanire in silenzio. Accetta anche testo e
immagini dagli appunti, non solo file.

### 11.2 · Cattura e OCR

`Windows.Media.Ocr` è nel sistema, funziona offline, non costa niente e riconosce decine di
lingue. Selezione di una regione → testo negli appunti. È la funzione che chi ha un monitor
solo usa venti volte al giorno: copiare un dato da una finestra a un'altra senza poterle vedere
insieme.

Da lì il passo successivo è naturale: la stessa regione → "manda all'IA", "traduci",
"spiegami".

### 11.3 · Azioni IA sulla selezione

Selezioni del testo in *qualsiasi* programma, premi un bottone della barra: OmniBar prende la
selezione (copia negli appunti, salvando e ripristinando il contenuto precedente), la manda al
modello configurato, e rimette il risultato. Traduci, riscrivi, riassumi, correggi, spiega.

Funziona ovunque **senza integrazione per app**, ed è il senso vero di "barra per l'era
dell'IA": non un'altra chat, ma il modello dove stai già lavorando.

---

## 12 · Estensioni

### 12.1 · I due tier

**Dichiarativo** — nessun processo, nessun codice. Un TOML che l'host legge:

```toml
[extension]
id      = "com.omnibar.excel"
name    = "Excel"
version = "1.0.0"
tier    = "declarative"

[[profile]]
match = { process = "EXCEL.EXE" }

[[profile.widget]]
type = "button"; icon = "sigma"; tooltip = "Somma automatica"
action = { kind = "keystroke", keys = "Alt+=" }

[[profile.widget]]
type = "button"; icon = "percent"; tooltip = "Formato percentuale"
action = { kind = "keystroke", keys = "Ctrl+Shift+5" }

[[profile.widget]]
type = "button"; icon = "freeze"; tooltip = "Blocca riquadri"
action = { kind = "macro", steps = [
  { kind = "keystroke", keys = "Alt+W" },
  { kind = "keystroke", keys = "F" },
  { kind = "keystroke", keys = "F" } ] }
```

**Processo** — un eseguibile che parla JSON-RPC 2.0 su una named pipe:

```toml
[extension]
id      = "com.omnibar.obs"
tier    = "process"
entry   = "obs-adapter.exe"

[capabilities]
network = ["ws://127.0.0.1:4455"]     # solo questo endpoint, niente altro

[activation]
processes = ["obs64.exe"]              # lazy: parte quando OBS parte
idle_stop_sec = 30                     # e si spegne quando OBS chiude
```

### 12.2 · Il protocollo

| Direzione | Metodo | Significato |
|---|---|---|
| host → ext | `initialize` | versione del protocollo, capability concesse, tema, DPI, locale |
| host → ext | `context/changed` | nuovo `ContextSnapshot` |
| host → ext | `widget/event` | click, cambio valore, hover, drop |
| host → ext | `shutdown` | chiusura ordinata, poi kill dopo timeout |
| ext → host | `widget/update` | nuovo albero (o patch) per una zona |
| ext → host | `attention/request` | chiedi lo stato `Attention` con un `prompt` |
| ext → host | `tag/set` | accendi o spegni un tag di contesto |
| ext → host | `action/invoke` | esegui un'azione — passa dal controllo permessi |
| ext → host | `notify` | messaggio non bloccante |

Ogni chiamata ha un timeout. Un'estensione che non risponde entro `budget_ms` viene marcata
lenta; se persiste, sospesa. Il suo pannello dice cosa è successo, in italiano, non "error 3".

### 12.3 · Sandbox del processo estensione

Nessun privilegio in più dell'host, e quando possibile qualcuno in meno: job object con limiti
di memoria e CPU, nessuna ereditarietà di handle, working directory nella propria cartella,
variabili d'ambiente ripulite. Le capability di rete sono documentate nel manifest ma
**non** applicate a livello di sistema in v1: è una promessa verificabile leggendo il manifest,
non una gabbia. Questo va detto all'utente senza girarci intorno.

---

## 13 · LocalAPI e integrazione con l'IA

Un endpoint locale (named pipe + un piccolo HTTP su loopback per gli strumenti che parlano solo
HTTP) permette a strumenti esterni di usare la barra.

### 13.1 · Approvazioni

Claude Code ha un sistema di **hook**: un hook `PreToolUse` può invocare un comando esterno che
decide se permettere, negare o chiedere. L'integrazione è diretta:

```
Claude Code vuole eseguire uno strumento
   → hook PreToolUse chiama `omnibar ask --tool Bash --detail "rm -rf build/"`
   → il CLI parla alla LocalAPI
   → la barra entra in Attention e mostra un widget prompt: [Consenti] [Nega] [Sempre]
   → l'utente risponde sulla barra, senza cambiare finestra
   → il CLI restituisce la decisione, l'hook la passa a Claude Code
```

Valore: le approvazioni sono la cosa che rompe di più il flusso quando si lavora con un agente,
e su un monitor solo costano un Alt-Tab ognuna. Sulla barra costano un click senza spostare lo
sguardo dal lavoro.

Vincoli: il prompt ha sempre un **timeout con default sicuro** (nega), mostra *sempre* il testo
integrale di ciò che sta approvando, e "Sempre" scrive una regola visibile e revocabile — mai
una scorciatoia opaca.

### 13.2 · Utilizzo e limiti

Non esiste un'API pubblica che dica quanto resta del piano di un abbonamento Claude. Va detto
chiaramente invece di promettere un numero. L'architettura è quindi un'interfaccia
`UsageProvider` con più implementazioni, ognuna onesta su cosa sa:

| Provider | Da dove | Cosa sa davvero |
|---|---|---|
| `claude-code-local` | dati di sessione locali di Claude Code | token e costo delle sessioni su questa macchina |
| `claude-code-otel` | metriche OpenTelemetry che Claude Code può emettere | uso nel tempo, se l'utente abilita la telemetria locale |
| `anthropic-admin` | Admin API, con API key dell'utente | spesa e uso dell'organizzazione via API |
| `generic-openai` etc. | API key dell'utente | uso via API di altri provider |

Nessuno di questi legge la quota di un abbonamento consumer. Il widget mostra ciò che sa, con
l'etichetta di dove viene, e non inventa il resto.

### 13.3 · L'avatar

In fondo alla barra, **ultimo slot**, c'è la faccia dell'agente. È un widget come gli altri —
`WidgetType::Avatar` — ma sta sempre in coda e non cambia mai: tutto quello che gli sta sopra
dipende dal programma in primo piano, lui no. È il posto più stabile della barra.

È **la faccia dell'agente** — quella che chiede il permesso quando un modello vuole fare
qualcosa (§13.1), che segnala quando c'è qualcosa da decidere, e da cui si apre il pannello
dell'IA. Una cosa che chiede permesso deve stare sempre nello stesso punto, altrimenti la si
cerca invece di guardarla.

**Si vede quando la barra è aperta.** Non serve che sia visibile a riposo: quando l'agente ha
qualcosa da chiedere è la barra ad aprirsi da sola (stato `Attention`, §8), e l'avatar è già
lì. Un elemento sempre visibile fuori dalla barra sarebbe stato una seconda cosa da guardare.

**Perché è un tipo del vocabolario e non un bottone con un'icona.** Ha uno stato che nessun
altro widget ha — dove guarda, se sta sbattendo le ciglia, se ha qualcosa da chiedere — ed è
l'unico elemento del progetto che deve leggersi come *qualcuno* invece che come *qualcosa*.
Un'icona che ammicca sarebbe un'icona rotta. Essendo un widget, posizione, hit-test e click
arrivano dal layout come per tutti gli altri, senza codice a parte.

Lo stato lo racconta **l'anello**, non un pallino: quando c'è qualcosa da decidere l'anello
diventa dell'accento e si ispessisce. È l'unico segnale del progetto che deve funzionare con
la coda dell'occhio, e sei pixel di pallino in un angolo non lo fanno.

#### La licenza, che qui decide il progetto

L'avatar nasce da [bible-strong-avatar-lab](https://github.com/smontlouis/bible-strong-avatar-lab),
uno studio di autoring che compone avatar procedurali da primitive geometriche e li esporta
come definizioni `.avatar.json`.

**Quel progetto è AGPL-3.0. OmniBar è MIT.** Incorporarne il codice obbligherebbe l'intero
progetto a diventare AGPL, con obblighi che si estendono all'uso in rete. Non è una cosa che
si fa per un avatar.

La strada praticabile separa nettamente le due cose:

| | |
|---|---|
| **Si usa** | lo studio, come strumento a sé, per disegnare l'avatar. Il risultato del disegno è opera di chi lo disegna |
| **Si esporta** | la definizione delle forme — un formato di dati, e i formati di dati non sono coperti da copyright |
| **Si scrive** | il disegnatore in C++/Direct2D, nostro. Primitive geometriche, arrotondamenti, rotazioni: è esattamente ciò che Direct2D sa fare |
| **Non si tocca** | il loro codice. Nessuna riga nel nostro binario, nessun collegamento, nessuna derivazione |

#### Com'è disegnato

**Un quadrato con gli angoli molto smussati, e dentro due soli occhi.**

Trenta punti di lato sono pochi, e questo detta ogni scelta: niente bocca (a questa scala
diventa una macchia), niente naso, niente sopracciglia, nessun dettaglio che al 100 % di DPI
finirebbe su meno di due pixel.

Ci si è arrivati scartando la prima versione, che era un cerchio con le sopracciglia. Non
funzionava per due motivi:

- **La forma tonda non c'entrava niente con il resto.** La barra è fatta di rettangoli
  arrotondati; in mezzo a quelli un cerchio si legge come una cosa incollata da un'altra
  applicazione. Il raggio a un terzo del lato la fa leggere come una piastrella — che è il
  linguaggio di Windows — e la mette in famiglia con la barra.
- **Le sopracciglia erano troppo poco spazio per troppo significato.** Due trattini di un paio
  di pixel che, a seconda di come cadono, fanno sembrare la faccia arrabbiata. Sono state
  aggiustate una volta e restavano fragili: un dettaglio che si rompe a ogni cambio di DPI non
  è un dettaglio, è un rischio.

Quello che resta:

- **Il volume** lo fa una sfumatura radiale con l'origine spostata in alto a sinistra. Senza,
  la forma resta una tessera piatta e non una testa.
- **Gli occhi portano tutta l'espressione**, ognuno con larghezza, altezza e inclinazione
  proprie. È il modello dei generatori di avatar geometrici, ed è l'unico che regge a questa
  scala: un occhio più alto e tondo legge come attenzione, uno più basso come calma,
  un'inclinazione verso l'interno come domanda. Tre numeri per occhio, e niente che possa
  rompersi.
- **Il riflesso** è un punto bianco di un punto e mezzo, e da solo fa la differenza fra due
  buchi e due occhi: è lui a darli per bagnati, cioè vivi. Sparisce con la palpebra — un
  puntino sospeso su un occhio chiuso fa sembrare rotto tutto il resto.

L'umore è un **continuo, non un interruttore**: il passaggio fra riposo e attesa si anima in
poco più di un decimo di secondo. Una faccia che cambia espressione di scatto non è una
faccia, sono due immagini.

**Gli occhi stanno sopra la metà.** Erano appena sotto, ed è bastato quello per farli leggere
come rivolti in basso anche quando guardavano dritto: un viso reale ha gli occhi intorno al
45 % dell'altezza, e sotto la linea mediana l'occhio dice "sto guardando per terra" a
prescindere da dove punta davvero.

#### Cliccarlo lo mette in silenzio

Un click sull'agente lo **silenzia**: smette di chiedere permessi e di segnalare, diventa
grigio e si ferma. Un altro click lo riattiva.

È il gesto più naturale che ci sia su una faccia che ti interrompe, e non ha bisogno di essere
spiegato — nessuna voce di menu, nessuna impostazione da cercare mentre stai lavorando. Ed è
anche l'unica cosa che quel widget può fare che nessun altro può: le richieste dell'agente
passano tutte da lì, quindi lì si spengono.

"Grigio e fermo" non è solo l'aspetto: da silenziato **non si aggiorna nemmeno lo sguardo**, e
i timer restano spenti. È ciò che rende credibile che non stia più guardando — e costa meno di
quando è attivo, il che è giusto per uno stato che si sceglie per essere lasciati in pace.
Gli occhi si socchiudono senza chiudersi: chiusi sembrerebbe addormentato, e uno che dorme lo
si sveglia, mentre uno zittito no.

È anche l'unico punto di colore della barra, ed è voluto: fra icone tutte monocrome, una faccia
colorata si legge come qualcuno invece che come l'ennesimo comando.

**Il colore non lo scegliamo noi.** Viene dall'accento che l'utente ha impostato in Windows,
letto da `UISettings` insieme alle sue varianti chiara e scura, e cambia a caldo quando lui lo
cambia. Vale per tutto: toggle accesi, anello di richiamo, sfera dell'avatar. Una barra che
vuole passare per un componente di sistema non ha motivo di inventarsi una tavolozza propria —
e un colore scelto da noi sarebbe *sbagliato* per chiunque abbia gusti diversi dai nostri.

Due dettagli che discendono da lì:

- Sul fondo scuro si usa la variante chiara dell'accento, sul fondo chiaro quella scura.
  Prendere sempre la stessa vorrebbe dire che metà degli utenti non vede il proprio colore.
- **L'occhio non è nero**: è l'accento portato quasi a fondo. Un nero puro su una sfera
  colorata sembra un buco; una tinta scura dello stesso colore sembra parte della faccia.

Resta sovrascrivibile dalla configurazione, per chi vuole un personaggio di un colore diverso
dal resto dell'interfaccia.

#### Cosa fa, e quanto costa

**Guarda il cursore e sbatte le ciglia.**

Lo sguardo insegue con un ritardo di ~110 ms, e il ritardo è il punto. Fra la direzione del
cursore e dove l'occhio è arrivato c'è tutta la differenza fra uno sguardo e un indicatore:
sotto i cinquanta millisecondi sembra incollato al mouse, sopra i duecento sembra distratto.
Quando il cursore gli arriva addosso gli occhi tornano al centro invece di strabuzzare.

Il battito è rapido — 45 ms per chiudere, 85 per riaprire — perché un occhio vero fa così, e
allungarlo lo fa sembrare sonnolenza invece che un battito. L'intervallo è casuale fra 2,2 e
5,6 secondi, e una volta su quattro ne fa **due di fila**: non è un vezzo, è ciò che rompe la
regolarità, e la regolarità è la cosa che fa capire che dietro c'è un timer.

Chiude l'occhio schiacciandolo, non facendolo sparire: un occhio che svanisce si legge come un
errore di disegno, uno che si appiattisce si legge come una palpebra.

**L'evidenziazione va spenta a mano quando il cursore se ne va.** Una finestra riceve
`WM_MOUSEMOVE` finché il cursore ci sta sopra, e quando esce non riceve più niente: l'ultimo
widget illuminato resterebbe illuminato per sempre. Il controllo sta nel polling del cursore, e
sta **in cima**, prima di qualunque ramo che possa uscire — la prima versione stava sotto il
ramo della barra chiusa e funzionava solo nel caso in cui era stata scritta.

**Lo sguardo si divide fra la testa e gli occhi.** Muovere solo le pupille dentro una faccia
immobile è il modo più rapido per ottenere qualcosa che sembra un quadro che ti segue con lo
sguardo: inquietante, non vivo. Una testa vera si orienta — si sposta un poco verso quello che
guarda e si inclina di conseguenza — e gli occhi fanno il resto del percorso. Diviso così, la
stessa deflessione totale si legge come "si è girato" invece che "ha spostato gli occhi".

All'apertura lo sguardo **si allinea di scatto** invece di partire da fermo. A barra chiusa
l'avatar non si vede, quindi gli occhi non si muovono; senza questo comparirebbero centrati e
poi si girerebbero verso di te, come se ti stesse cercando. Ma non ti stava cercando: era lì
che guardava, semplicemente non lo vedevi.

**Il costo è zero quando non si muove.** Due cose lo garantiscono:

- I timer dello sguardo e del battito **esistono solo mentre servono**, e solo a barra aperta.
  A cursore fermo, occhi aperti o barra chiusa non c'è nessun timer acceso: misurato 0 ms di
  CPU su 45 secondi.
- Quando invece si muove, **si ridisegna il solo disco**, e si ricopia sullo schermo la sola
  area che occupa (`UpdateLayeredWindowIndirect` con area sporca). La superficie della barra è
  alta quanto lo schermo: ridisegnarla tutta per spostare due pupille di mezzo punto
  costerebbe centottanta kilobyte a fotogramma per un disegno che ne cambia tre, e a quel
  prezzo un avatar che segue il cursore non è un dettaglio simpatico, è una barra che consuma.

Verificato a schermo: con il cursore in alto a sinistra le pupille puntano lì; campionando i
pixel dentro la faccia per dieci secondi, il conteggio scende a zero durante i battiti.

---

## 14 · Configurazione

```
%APPDATA%\OmniBar\
  omnibar.toml              impostazioni globali
  profiles\<nome>.toml      profili utente
  extensions\<id>\          estensioni installate
  state\                    shelf, cronologia appunti, note (dati, non impostazioni)
```

In modalità portable la stessa struttura vive accanto all'eseguibile: se esiste
`portable.flag`, si usa quella e non si tocca `%APPDATA%`.

Merge dei livelli, dal più debole al più forte: default compilati → profili delle estensioni →
`omnibar.toml` → profili utente → override della sessione corrente.

Hot-reload su `ReadDirectoryChangesW`. Un file con un errore **non viene applicato**: si tiene
la versione precedente e si mostra l'errore con file, riga e cosa ci si aspettava. Una barra
che sparisce per un TOML sbagliato è un bug, non un messaggio d'errore.

---

## 15 · Threading

La disciplina di MiniBar, invariata perché ha funzionato:

- **Un solo thread UI** con il message loop. Tutto lo stato dell'interfaccia vive lì.
- Gli eventi che arrivano da fuori — WinRT, named pipe, PDH, COM, watcher del filesystem —
  girano su thread di threadpool e **non toccano mai lo stato**: impacchettano i dati e li
  rimandano al thread UI con `PostMessage`, con un numero di generazione che scarta i messaggi
  in volo di un contesto ormai sostituito.
- Il lavoro lungo (decodifica di miniature, OCR, COM automation) va su un worker pool con
  cancellazione; il risultato torna sul thread UI come tutto il resto.
- Un solo timer per le animazioni, attivo solo mentre qualcosa si muove.

Risultato: zero lock, zero race.

---

## 16 · Test

| Livello | Cosa |
|---|---|
| **Simulatore** | La barra gira in una finestra normale con contesti finti, iniettati da uno script. Si sviluppa il pannello di Photoshop senza Photoshop, e si prova lo stato `Attention` senza aspettare che un agente chieda un permesso. È anche ciò che rende testabile la UI in CI. |
| **Golden image** | Ogni widget, in ogni tema, a 100/125/150/200 % di DPI, confrontato pixel per pixel a ogni commit. |
| **Replay dei contesti** | Log ring-buffer degli snapshot; un bug di contesto si riproduce rigiocando il log invece di ricreare la situazione a mano. |
| **Unità** | Rule matcher, merge della configurazione, diff dell'albero widget, parser TOML, validazione delle azioni. |
| **Budget** | La CI misura RAM, tempo di avvio e dimensione dell'eseguibile e fallisce se si sfora la §3. |
| **Fuzzing** | Sul JSON-RPC delle estensioni e sui file di configurazione: sono i due ingressi non fidati. |

---

## 17 · Rischi e mitigazioni

| Rischio | Mitigazione |
|---|---|
| Il reveal si attiva quando non lo vuoi | Soglia doppia tempo + distanza (§8.1), isteresi in uscita, zone di esclusione ricalcolate, possibilità di disattivare l'hover e usare solo l'hotkey |
| Conflitto con la taskbar in auto-hide o con gli snap layout | Rettangoli mai memorizzati: ricalcolo a ogni `ABN_*`, `WM_DPICHANGED` e cambio risoluzione |
| Un'estensione lenta o che crasha | Fuori processo, timeout, budget, riavvio con backoff, sospensione dopo ripetuti crash |
| Un'estensione ostile | Vocabolario widget chiuso (niente pixel arbitrari), capability dichiarate e approvate, `shell` senza stringhe di comando, rate limit |
| Adobe abbandona COM per UXP | Ogni adapter degrada a scorciatoie da tastiera; l'adapter è un'estensione sostituibile senza toccare l'host |
| Nessun modo onesto di leggere le quote dell'abbonamento IA | Interfaccia `UsageProvider` con etichetta della fonte; si mostra ciò che si sa, non si inventa |
| Temperature CPU senza driver | GPU da NVML/ADLX/IGCL, CPU solo se l'utente ha già HWiNFO/LHM, altrimenti il widget non compare e le impostazioni spiegano perché |
| L'helper elevato diventa un buco | Vocabolario chiuso, pipe con ACL, token di sessione, nessun avvio automatico, revisione di sicurezza per ogni comando aggiunto |
| Falso positivo anti-cheat o antivirus | Nessuna injection né hook né driver per design; stesso profilo di EarTrumpet o del flyout del volume |
| Il progetto diventa ingestibile | Il vocabolario widget e il modello a produttori sono un limite volontario: una feature che non ci sta dentro va ripensata, non aggiunta di lato |

---

## 18 · Fuori scope

- Visibilità sopra il fullscreen esclusivo (per design, §2).
- Un launcher / ricerca universale stile Raycast: è un prodotto a sé e compete con PowerToys Run.
- Piattaforme diverse da Windows. L'architettura non lo esclude, il progetto sì.
- Sincronizzazione cloud dei profili, account, marketplace ospitato — non in v1.
- Sostituire la taskbar o il menu Start.
- Qualunque cosa richieda un hook, un'iniezione o un driver.

> Ogni volta che una feature sembra richiedere un'eccezione alla §2, la risposta giusta è quasi
> sempre "no": o rientra nei vincoli, o non fa parte di OmniBar.

---

## 19 · Riferimenti API

| Area | API |
|---|---|
| Finestre e bordo | `CreateWindowExW`, `WS_EX_NOACTIVATE/TOPMOST/TOOLWINDOW/LAYERED`, `SetWindowPos`, `UpdateLayeredWindow`, `SHAppBarMessage` (`ABM_*`, `ABN_*`), `WM_DPICHANGED`, `WM_SETTINGCHANGE` |
| Composizione | `DCompositionCreateDevice`, `IDCompositionTarget`, `IDCompositionVisual`, `WS_EX_NOREDIRECTIONBITMAP` |
| Rendering | Direct2D, DirectWrite, WIC |
| Accessibilità | UI Automation provider (`IRawElementProviderSimple` e affini) |
| Contesto | `SetWinEventHook` (`EVENT_SYSTEM_FOREGROUND`, `WINEVENT_OUTOFCONTEXT`), `GetWindowThreadProcessId`, `QueryFullProcessImageNameW`, `GetApplicationUserModelId` |
| Explorer | `IShellWindows`, `IServiceProvider`, `IShellBrowser`, `IFolderView2`, `IShellItemArray` |
| Media | `Windows.Media.Control` (SMTC) |
| Audio | `IMMDeviceEnumerator`, `IAudioSessionManager2`, `IAudioSessionControl2`, `ISimpleAudioVolume`, `IPolicyConfig` (cambio device) |
| Sistema | PDH (`PdhOpenQuery`, contatori `Processor Information`, `Memory`, `GPU Engine`), `GetSystemPowerStatus` |
| GPU vendor | NVML, AMD ADLX, Intel IGCL — tutte caricate a runtime |
| Cattura e OCR | `Windows.Graphics.Capture`, `Windows.Media.Ocr` |
| Drag & drop | `RegisterDragDrop`, `IDropTarget`, `IDataObject`, `SHCreateDataObject` |
| Appunti | `AddClipboardFormatListener` |
| IPC | `CreateNamedPipeW` con SDDL, JSON-RPC 2.0 |
| Input | `SendInput`, `RegisterHotKey` |
