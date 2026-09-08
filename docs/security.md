# OmniBar — Sicurezza

> OmniBar è un programma sempre in esecuzione che può premere tasti al posto tuo, avviare
> altri programmi e caricare codice di terze parti. Questo documento dice cosa può fare, cosa
> non può fare per costruzione, e come si prendono le decisioni quando le due cose vanno in
> conflitto.

---

## 1 · Il modello di minaccia

Da chi ci si difende, in ordine di probabilità reale:

| Attaccante | Scenario | Difesa |
|---|---|---|
| **Un'estensione ostile o compromessa** | l'utente installa un plugin che sembra utile | vocabolario widget chiuso, capability dichiarate, out-of-process, `shell` senza stringhe di comando |
| **Un'estensione semplicemente sbagliata** | un plugin che crasha, si impianta, o consuma tutto | timeout, budget, job object, riavvio con backoff, sospensione |
| **Un file di configurazione ostile** | l'utente copia un profilo trovato online | parser che non esegue niente, azioni tipizzate, consenso per le capability |
| **Un processo locale ostile** | malware già sulla macchina che vuole usare OmniBar come leva | pipe con ACL sul solo utente, token di sessione, nessun endpoint di rete in ascolto verso l'esterno |
| **Escalation via helper elevato** | il pezzo che gira come amministratore | vocabolario chiuso, nessun comando arbitrario, nessun accesso dalle estensioni |

Fuori dal modello: un attaccante che ha già l'esecuzione di codice come l'utente non ha
bisogno di OmniBar per fare quello che OmniBar fa. Non si finge di difendersi da lui.

---

## 2 · Le difese strutturali

Non sono controlli: sono cose che **non si possono esprimere**.

### 2.1 · Un'estensione non ha pixel

Il vocabolario widget chiuso (§6 dell'architettura) non è solo una scelta estetica. Un
plugin che potesse disegnare liberamente sulla barra potrebbe disegnare una finestra di
Windows finta, una richiesta di password, un finto avviso di sicurezza — in un pezzo di
schermo che l'utente ha imparato a considerare fidato.

Con il vocabolario chiuso non ha una superficie su cui farlo. Non c'è un controllo da
aggirare: la capacità non esiste.

### 2.2 · `shell` non accetta una riga di comando

```toml
# Questo è l'unico modo di scrivere un'azione shell:
action = { kind = "shell", exe = "ffmpeg.exe", args = ["-i", "in.mp4", "out.webm"] }

# Questo non esiste, e non esisterà:
action = { kind = "shell", command = "ffmpeg -i in.mp4 && curl ..." }
```

Eseguibile e argomenti sono sempre separati, e passano a `CreateProcessW` senza mai transitare
per un interprete. Non c'è modo di scrivere una command injection in un file di
configurazione. Questa regola non si rilassa per comodità, mai.

### 2.3 · `keystroke` ha un target, e verifica

Il bersaglio è la finestra di foreground catturata **prima** che la barra la tocchi. Al
momento dell'invio si verifica che sia ancora quella. Se nel frattempo è cambiata — l'utente
ha fatto Alt-Tab, una finestra è saltata davanti — l'azione **si rifiuta** invece di mandare
tasti a caso in un'altra applicazione. Una macro che parte nella finestra sbagliata può
cancellare il lavoro di qualcuno.

### 2.4 · Le capability si dichiarano, si spiegano, si concedono

L'utente non deve sapere cosa sia una "capability". Deve sapere cosa può succedere:

| Nel manifest | Cosa legge l'utente |
|---|---|
| `input` | "Potrà premere tasti al posto tuo nella finestra attiva." |
| `exec` | "Potrà avviare programmi sul tuo computer." |
| `open` | "Potrà aprire link e applicazioni." |
| `clipboard` | "Potrà leggere e scrivere gli appunti." |
| `files` | "Potrà leggere i file in: …" (le cartelle esatte) |
| `network` | "Potrà collegarsi a: …" (gli endpoint esatti) |

Un permesso concesso è revocabile dalle impostazioni, e la revoca ha effetto immediato: se
l'estensione era attiva, viene riavviata senza.

### 2.5 · Ciò che il manifest promette e ciò che il sistema impone

Onestà su un limite della v1: `network` e `files` nel manifest sono **promesse verificabili
leggendo il manifest**, non gabbie applicate dal sistema operativo. Un processo estensione
ostile può aprire un socket che non ha dichiarato.

Cosa **è** applicato in v1: nessun privilegio in più dell'host, job object con limiti di
memoria e CPU, nessuna ereditarietà di handle, working directory confinata, ambiente
ripulito, nessun accesso all'helper elevato.

Questo va detto all'utente nella finestra di consenso, non nascosto in una nota a piè di
pagina. Il confinamento vero (AppContainer o simili) è un obiettivo, non una cosa fatta.

---

## 3 · L'helper elevato

`omnibar-helper.exe` è l'unica parte che gira con privilegi di amministratore, ed esiste solo
perché UIPI impedisce a un processo non elevato di mandare input a una finestra elevata.

**È opzionale.** Chi non ne ha bisogno non lo installa, e OmniBar funziona identica a meno di
quella singola cosa.

Le regole, tutte insieme perché valgono solo insieme:

1. **Vocabolario chiuso e tipizzato.** Il helper accetta un insieme fisso di comandi
   (`send_input`, `read_sensor`, …), ognuno con parametri tipizzati e validati. **Non esiste un
   comando che esegua una stringa.** Aggiungerne uno non è una feature: è una modifica a questo
   documento, con la motivazione scritta.
2. **Named pipe con ACL sul solo utente corrente**, più un token di sessione generato all'avvio
   e consegnato solo all'host. Un altro processo dello stesso utente non può parlargli.
3. **Nessun avvio automatico.** Parte quando serve, si spegne dopo l'inattività. Non è un
   servizio che gira sempre come SYSTEM.
4. **Non carica estensioni, non legge configurazione arbitraria, non tocca il disco** se non per
   il proprio log.
5. **Le estensioni non ci parlano mai.** Una richiesta che ha bisogno del helper passa
   dall'host, che decide. Non c'è un percorso che vada da un plugin di terze parti a codice
   elevato.
6. **Codice separato e piccolo per costruzione.** Vive in una cartella sua. Ogni riga che ci si
   aggiunge è una decisione di sicurezza.

---

## 4 · Ciò che OmniBar non fa, per design

- Non inietta DLL in nessun processo.
- Non installa hook globali (`SetWindowsHookEx` a livello di sistema).
- Non aggancia DirectX o Vulkan.
- Non installa driver kernel — nemmeno per leggere le temperature (architecture.md §10.1).
- Non manda niente in rete che l'utente non abbia chiesto: nessuna telemetria, nessun
  controllo aggiornamenti silenzioso, nessun account.
- Non legge gli appunti se non quando un'azione lo richiede, e la cronologia degli appunti
  resta locale.
- Non si aggiorna da sola senza conferma.

Il primo blocco non è solo igiene: sono i pattern che gli anti-cheat trattano come cheat e che
gli antivirus trattano come sospetti. Un programma che vuole stare su milioni di macchine non
può assomigliare a quella roba.

---

## 5 · La LocalAPI

L'endpoint locale (fase 4) permette a strumenti esterni — la CLI, gli hook di Claude Code — di
chiedere qualcosa alla barra.

- **Named pipe** come trasporto principale, con ACL sul solo utente.
- **HTTP su `127.0.0.1`** solo per gli strumenti che non sanno parlare con una pipe, mai in
  ascolto su un'interfaccia esterna, con un token di sessione obbligatorio scritto in un file
  leggibile solo dall'utente.
- Le richieste che pretendono una risposta dell'utente (`ask`) hanno **sempre** un timeout con
  default sicuro — negare — e mostrano **sempre** il testo integrale di ciò che si sta
  approvando. Una richiesta di approvazione che tronca ciò che sta approvando è peggio di
  nessuna richiesta.
- La regola "Sempre" scrive una riga visibile e revocabile nelle impostazioni. Non esistono
  consensi permanenti invisibili.

---

## 6 · Segnalare un problema

Finché il progetto non ha una release pubblica: aprire una issue. Dopo la prima release, una
security policy con contatto privato e finestra di divulgazione — e questo paragrafo verrà
sostituito.
