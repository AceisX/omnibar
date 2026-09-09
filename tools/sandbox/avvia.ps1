# avvia.ps1 - apre la sandbox di prova, dall'host.
#
#   .\tools\sandbox\avvia.ps1              apre la sandbox
#   .\tools\sandbox\avvia.ps1 -Ricarica    ricompila e fa ripartire la barra
#                                          dentro la sandbox gia' aperta
#
# La ricarica non riapre la macchina: tocca un file marcatore che lo script
# dentro la sandbox sta sorvegliando. Riaprire la sandbox costa mezzo minuto,
# toccare un file costa niente, e su un'interfaccia che si rifinisce a colpi di
# due punti in piu' o in meno la differenza si sente.
param(
    [switch]$Ricarica,
    [switch]$Compila = $true
)

$ErrorActionPreference = 'Stop'
$radice = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not (Test-Path "$env:WINDIR\System32\WindowsSandbox.exe")) {
    Write-Host "Windows Sandbox non e' installato." -ForegroundColor Yellow
    Write-Host "Impostazioni > Sistema > Funzionalita' facoltative > Altre funzionalita' di Windows"
    Write-Host "  -> spunta 'Sandbox di Windows', poi riavvia."
    exit 1
}

if ($Compila) {
    Push-Location $radice
    try { & .\build.ps1 -Config release } finally { Pop-Location }
}

$marcatore = Join-Path $radice 'dist\ricarica.flag'
Set-Content -Path $marcatore -Value (Get-Date -Format 'o') -Encoding ascii

if ($Ricarica) {
    Write-Host "Marcatore aggiornato: la sandbox ricarichera' la barra entro un secondo."
    exit 0
}

# I percorsi dentro un .wsb devono essere assoluti: il formato non conosce
# variabili. Invece di lasciarli scritti a mano — e sbagliati appena la cartella
# si sposta — si riscrivono qui su quelli veri di questa copia del repository.
# Riga per riga e senza espressioni regolari: qui dentro ci sono backslash,
# parentesi e barre verticali, cioe' tre modi diversi di sbagliare una regex per
# una sostituzione che di suo e' banale.
$wsb    = Join-Path $PSScriptRoot 'omnibar.wsb'
$righe  = Get-Content $wsb -Encoding UTF8
$scritto = $false

$righe = $righe | ForEach-Object {
    if ($_ -match '^\s*<HostFolder>.*</HostFolder>\s*$') {
        $indent = $_.Substring(0, $_.IndexOf('<'))
        $vecchio = $_.Trim()
        $sotto = if ($vecchio -like '*tools*sandbox*') { 'tools\sandbox' } else { 'dist' }
        $nuovo = "$indent<HostFolder>$radice\$sotto</HostFolder>"
        if ($nuovo -ne $_) { $script:scritto = $true }
        $nuovo
    } else { $_ }
}

if ($scritto) {
    Set-Content $wsb -Value $righe -Encoding UTF8
    Write-Host "Percorsi del .wsb aggiornati su: $radice"
}

Write-Host "Apro la sandbox... (il primo avvio richiede una ventina di secondi)"
Start-Process 'WindowsSandbox.exe' -ArgumentList "`"$wsb`""
