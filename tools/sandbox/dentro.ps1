# dentro.ps1 - gira DENTRO la sandbox, avviato dal LogonCommand di omnibar.wsb.
#
# Fa una cosa sola, e la fa in un ciclo: quando il marcatore `ricarica.flag`
# cambia data, ferma la barra, ricopia l'eseguibile e la riavvia.
#
# Perche' copiare invece di eseguire dalla cartella condivisa: un eseguibile in
# esecuzione e' un file bloccato, e quel file vive sull'host. La sandbox
# terrebbe in ostaggio dist\omnibar.exe e la build successiva fallirebbe con
# "cannot open file" — che e' esattamente il problema che questa sandbox deve
# risolvere. Copiandolo su disco locale, l'host resta libero di ricompilare
# mentre la barra gira.

$ErrorActionPreference = 'SilentlyContinue'

$sorgente = 'C:\omnibar-dist\omnibar.exe'
$marcatore = 'C:\omnibar-dist\ricarica.flag'
$locale   = 'C:\run\omnibar.exe'

New-Item -ItemType Directory -Force 'C:\run' | Out-Null

# 'mai' e non stringa vuota: cosi' il primo giro avvia sempre la barra, anche
# se il marcatore non esiste ancora.
$ultima = 'mai'

while ($true) {
    $ora = if (Test-Path $marcatore) { (Get-Item $marcatore).LastWriteTime.Ticks.ToString() } else { 'assente' }

    if ($ora -ne $ultima) {
        $ultima = $ora

        Get-Process omnibar -ErrorAction SilentlyContinue | Stop-Process -Force
        Start-Sleep -Milliseconds 250

        if (Test-Path $sorgente) {
            Copy-Item $sorgente $locale -Force
            Start-Process $locale
        }
    }

    Start-Sleep -Milliseconds 600
}
