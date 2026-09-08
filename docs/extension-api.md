# OmniBar — API delle estensioni

> Bozza del protocollo, versione `0.1-draft`. Finché non esce la fase 5 può cambiare senza
> preavviso. Le decisioni di fondo — vocabolario chiuso, out-of-process, capability
> dichiarate — non cambiano: sono nell'[architettura](architecture.md) §6 e §12.

Regola che governa tutto il resto: **un'estensione non disegna. Descrive.** Non riceve una
finestra, un contesto grafico o un canvas. Emette un albero di widget da un vocabolario
chiuso, e l'host lo renderizza con il proprio tema, il proprio DPI e la propria
accessibilità.

---

## 1 · I due tier

### 1.1 · Dichiarativo — nessun codice

Un solo file TOML. L'host lo legge, non avvia nessun processo. Copre i comandi rapidi, che
sono la maggioranza dei casi reali.

```toml
[extension]
id      = "com.esempio.excel"
name    = "Excel"
version = "1.0.0"
author  = "Nome Cognome"
tier    = "declarative"

[[profile]]
match = { process = "EXCEL.EXE" }

[[profile.widget]]
type    = "button"
icon    = "sigma"
tooltip = "Somma automatica"
action  = { kind = "keystroke", keys = "Alt+=" }

[[profile.widget]]
type    = "button"
icon    = "percent"
tooltip = "Formato percentuale"
action  = { kind = "keystroke", keys = "Ctrl+Shift+5" }

[[profile.widget]]
type    = "button"
icon    = "freeze"
tooltip = "Blocca riquadri"
action  = { kind = "macro", steps = [
  { kind = "keystroke", keys = "Alt+W" },
  { kind = "keystroke", keys = "F" },
  { kind = "keystroke", keys = "F" },
] }
```

Un'estensione dichiarativa può usare solo azioni statiche: `keystroke`, `shell`, `url`,
`macro`, `internal`. Non può avere stato, non può leggere niente, non può cambiare i propri
widget in base a qualcosa. Se le serve, passa al tier `process`.

### 1.2 · Processo — JSON-RPC su named pipe

```toml
[extension]
id      = "com.esempio.obs"
name    = "OBS Studio"
version = "0.1.0"
tier    = "process"
entry   = "obs-adapter.exe"      # relativo alla cartella dell'estensione

[capabilities]
network = ["ws://127.0.0.1:4455"]  # solo questo endpoint

[activation]
processes     = ["obs64.exe"]      # parte quando OBS parte
idle_stop_sec = 30                 # si spegne quando OBS chiude
```

L'host avvia il processo passandogli il nome della pipe in `--pipe`, e comunica in JSON-RPC
2.0, un messaggio per riga (`Content-Length` non serve: la pipe è a messaggi).

**Attivazione pigra.** Con `activation` l'estensione esiste solo quando il suo contesto è
acceso. Senza `activation` gira sempre, e va motivato: con cinquanta estensioni installate,
a riposo devono girare zero processi.

---

## 2 · Ciclo di vita

```
  host trova il manifest
      │
      ▼
  trigger di attivazione acceso ──► avvia processo ──► initialize
      │                                                    │
      │                              context/changed ◄─────┤
      │                              widget/event    ◄─────┤
      │                              widget/update   ─────►│
      │                                                    │
      ▼                                                    │
  trigger spento + idle_stop_sec ──► shutdown ──► (kill dopo 3 s)
```

Un'estensione che crasha viene riavviata con backoff esponenziale. Alla terza volta in un
minuto resta spenta, e il suo posto sulla barra mostra il motivo — in italiano, non
`error 3`.

---

## 3 · Metodi

### Host → estensione

| Metodo | Parametri | Note |
|---|---|---|
| `initialize` | `protocol`, `capabilities`, `theme`, `dpi`, `locale`, `host_version` | prima chiamata, risponde con le zone che intende occupare |
| `context/changed` | `ContextSnapshot` (§4) | a ogni cambio di foreground o di tag rilevante |
| `widget/event` | `id`, `kind`, `value?`, `modifiers?` | `kind`: `click`, `change`, `hover`, `drop`, `contextmenu` |
| `theme/changed` | `theme`, `dpi` | |
| `shutdown` | — | chiusura ordinata; poi `TerminateProcess` dopo il timeout |

### Estensione → host

| Metodo | Parametri | Note |
|---|---|---|
| `widget/update` | `zone`, `widget` (albero completo o patch) | l'host fa il diff |
| `attention/request` | `prompt` (widget), `timeout_ms`, `default` | porta la barra in `Attention` |
| `attention/resolve` | `id` | chiude la richiesta |
| `tag/set` | `tag`, `on` | accende o spegne un tag di contesto |
| `action/invoke` | `Action` | passa dal controllo permessi come tutto il resto |
| `notify` | `level`, `text` | non bloccante |
| `log` | `level`, `text` | finisce nel log dell'host, con il prefisso dell'estensione |

Ogni chiamata dell'host ha un timeout (`budget_ms`, default 200 ms per `widget/event`,
1000 ms per `initialize`). Superarlo non è un errore fatale: l'estensione viene marcata
lenta, e se persiste viene sospesa.

---

## 4 · ContextSnapshot

```json
{
  "process_name": "PHOTOSHOP.EXE",
  "process_path": "C:\\Program Files\\Adobe\\...\\Photoshop.exe",
  "aumid": null,
  "window_class": "Photoshop",
  "window_title": "logo-cliente.psd @ 66,7% (RGB/8)",
  "monitor": { "id": 1, "dpi": 144, "width": 2560, "height": 1440 },
  "is_fullscreen": false,
  "document_path": "D:\\lavoro\\logo-cliente.psd",
  "explorer_folder": null,
  "explorer_selection": [],
  "tags": ["on_battery"]
}
```

I campi opzionali arrivano solo se qualche produttore sa riempirli, e solo se almeno una
regola li usa: leggere il documento aperto costa, e non si paga per niente.

---

## 5 · Vocabolario dei widget

| Tipo | Campi principali | Eventi |
|---|---|---|
| `group` | `direction`, `gap`, `align`, `children` | — |
| `button` | `icon`, `label`, `tooltip`, `enabled`, `badge`, `action` | `click`, `contextmenu` |
| `toggle` | `icon`, `label`, `on`, `action` | `change` |
| `segmented` | `options[]`, `selected` | `change` |
| `slider` | `value`, `min`, `max`, `step`, `label` | `change` |
| `meter` | `value` (0-1), `label`, `thresholds[]` | `click` |
| `sparkline` | `samples[]`, `max?` | `click` |
| `progress` | `value` o `indeterminate` | — |
| `label` | `text`, `emphasis`, `marquee` | — |
| `image` | `source` (path o base64), `fit` | `click` |
| `swatch` | `rgba` | `click` |
| `separator`, `spacer` | `size?` | — |
| `panel` | `title`, `children`, `size` | apre lo stato `Expanded` |
| `list` / `grid` | `items[]`, `selectable`, `draggable` | `click`, `drop` |
| `prompt` | `text`, `detail`, `options[]`, `timeout_ms`, `default` | `click` |

Ogni widget ha `id` (unico nell'albero dell'estensione) e opzionalmente `a11y` per
sovrascrivere il nome che gli screen reader leggono. In assenza, il nome accessibile si
deriva da `label` o `tooltip`: **un widget senza nessuno dei tre è un errore di
validazione**, non un widget muto.

I widget non elencati non esistono. Aggiungerne uno è una decisione architetturale: va
motivata, disegnata per tutti i temi e i DPI, e coperta da golden-image test.

---

## 6 · Azioni e capability

| Kind | Forma | Capability |
|---|---|---|
| `internal` | `{ kind, name, args? }` | nessuna |
| `keystroke` | `{ kind, keys, target? }` | `input` |
| `shell` | `{ kind, exe, args[] }` | `exec` |
| `url` | `{ kind, url }` | `open` |
| `rpc` | `{ kind, method, params? }` | implicita |
| `macro` | `{ kind, steps[] }` | l'unione di quelle dei passi |

**`shell` non accetta una riga di comando.** Prende un eseguibile e un array di argomenti,
sempre separati. Non c'è modo di scrivere una command injection in un file di
configurazione, e questo non è negoziabile per comodità.

**`keystroke` ha un target esplicito.** La finestra di foreground viene catturata *prima*
che la barra la tocchi; se al momento dell'invio il target è cambiato, l'azione si rifiuta
invece di mandare tasti a caso.

| Capability | Cosa viene detto all'utente |
|---|---|
| `input` | "Potrà premere tasti al posto tuo nella finestra attiva." |
| `exec` | "Potrà avviare programmi sul tuo computer." |
| `open` | "Potrà aprire link e applicazioni." |
| `clipboard` | "Potrà leggere e scrivere gli appunti." |
| `files` | "Potrà leggere i file nelle cartelle indicate." |
| `network` | "Potrà collegarsi a: …" (l'elenco esatto del manifest) |

L'utente non deve sapere cosa significa "capability". Deve sapere cosa può succedere.

---

## 7 · Un'estensione minima, in Node

```js
import { createInterface } from 'node:readline';
import { connect } from 'node:net';

const pipe = process.argv[process.argv.indexOf('--pipe') + 1];
const sock = connect(pipe);
const rl = createInterface({ input: sock });

const send = (msg) => sock.write(JSON.stringify(msg) + '\n');

rl.on('line', (line) => {
  const msg = JSON.parse(line);

  if (msg.method === 'initialize') {
    send({ jsonrpc: '2.0', id: msg.id, result: { zones: ['context'] } });
    send({ jsonrpc: '2.0', method: 'widget/update', params: {
      zone: 'context',
      widget: { type: 'button', id: 'ciao', label: 'Ciao',
                action: { kind: 'rpc', method: 'saluta' } },
    } });
  }

  if (msg.method === 'widget/event' && msg.params.id === 'ciao') {
    send({ jsonrpc: '2.0', method: 'notify',
           params: { level: 'info', text: 'Ciao!' } });
  }

  if (msg.method === 'shutdown') process.exit(0);
});
```

---

## 8 · Cosa un'estensione non può fare

Non è un elenco di limitazioni temporanee: è il perimetro.

- Disegnare pixel arbitrari, in nessun punto della barra.
- Imitare l'interfaccia dell'host (chiedere una password, fingersi le impostazioni).
- Occupare la zona `alert` senza passare da `attention/request`, che ha timeout e default.
- Parlare con l'helper elevato. Mai, in nessun caso.
- Ottenere una capability non dichiarata nel manifest, o non approvata dall'utente.
- Impedire alla barra di rispondere: se non risponde entro il budget, la barra va avanti
  senza di lei.
