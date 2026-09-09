# Ambiente di prova in Windows Sandbox

Serve a una cosa sola: **provare un eseguibile appena compilato e non firmato.**
Su questa macchina Smart App Control blocca i binari senza reputazione, e con esso
attivo OmniBar non è avviabile nemmeno da chi la scrive
([security.md §6](../../docs/security.md)). Dentro la sandbox quel controllo non
si applica.

Windows Sandbox è incluso in Windows 11 **Pro**, Enterprise ed Education. Non c'è
niente da scaricare e non lascia traccia: alla chiusura la macchina sparisce.

## Attivazione, una volta sola

*Impostazioni → Sistema → Funzionalità facoltative → Altre funzionalità di Windows*
→ spunta **Sandbox di Windows** → riavvia.

Oppure, da PowerShell come amministratore:

```powershell
Enable-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM -All
```

## Uso

```powershell
.\tools\sandbox\avvia.ps1              # compila e apre la sandbox con la barra dentro
.\tools\sandbox\avvia.ps1 -Ricarica    # ricompila e fa ripartire la barra senza riaprire
```

La ricarica non riapre la macchina: aggiorna un file marcatore che lo script dentro
la sandbox sorveglia. Riaprire costa mezzo minuto, toccare un file costa niente — e
su un'interfaccia che si rifinisce a colpi di due punti in più o in meno, la
differenza si sente.

## Perché l'eseguibile viene copiato invece di essere lanciato dov'è

Un eseguibile in esecuzione è un file bloccato, e quel file vive sull'host. Se la
sandbox lo lanciasse dalla cartella condivisa terrebbe in ostaggio `dist\omnibar.exe`,
e la build successiva fallirebbe con `cannot open file` — cioè esattamente il
problema che questa sandbox deve risolvere. `dentro.ps1` lo copia su `C:\run\`
prima di avviarlo, così l'host resta libero di ricompilare mentre la barra gira.

## Cosa si può verificare qui, e cosa no

| Si può | Non si può |
|---|---|
| geometria, misure, colori, temi | **la fluidità reale del movimento** |
| il comportamento: apertura, chiusura, click, menu | il comportamento sopra giochi e finestre a schermo intero |
| il DPI e i quattro bordi | l'interazione con le app vere (Office, Adobe, OBS) |

Il limite sulla fluidità è strutturale, non un difetto della configurazione: la
sandbox disegna su uno schermo virtuale e la sua immagine arriva all'host lungo un
percorso che ha tempi propri. I fotogrammi che si vedono nella finestra della
sandbox **non** sono i fotogrammi che la barra produce.

Quindi: qui si verifica che le cose siano al posto giusto e della misura giusta;
**se il movimento sia piacevole si giudica solo sull'host**, con gli occhi di chi la
usa.
