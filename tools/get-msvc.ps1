# get-msvc.ps1 — scarica MSVC + Windows SDK come toolchain portable, senza installer.
# Richiede PowerShell 7 (tools\pwsh\pwsh.exe). Nessun privilegio di amministratore.
# Output: tools\msvc\  (VC\Tools\MSVC\<ver>  +  Windows Kits\10)

param(
  [string]$Root = $PSScriptRoot,  # la cartella tools\ di questo repo, ovunque stia
  [string]$Host_ = 'x64',
  [string]$Target = 'x64'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Dl   = Join-Path $Root '_dl'
$Dest = Join-Path $Root 'msvc'
New-Item -ItemType Directory -Force $Dl, $Dest | Out-Null

function Fetch($url, $out) {
  if (Test-Path $out) { return }
  $tmp = "$out.part"
  curl.exe -sS -L --retry 4 --retry-delay 2 -o $tmp $url
  if ($LASTEXITCODE -ne 0) { throw "download fallito: $url" }
  Move-Item $tmp $out -Force
}

Write-Host '[1/6] channel manifest...'
$ch = Invoke-RestMethod 'https://aka.ms/vs/17/release/channel' -UseBasicParsing
$manUrl = ($ch.channelItems | Where-Object id -eq 'Microsoft.VisualStudio.Manifests.VisualStudio').payloads[0].url

$manFile = Join-Path $Dl 'VisualStudio.vsman'
Write-Host '[2/6] manifest VS (29 MB)...'
Fetch $manUrl $manFile

Write-Host '[3/6] parsing manifest...'
$man = Get-Content -Raw -LiteralPath $manFile | ConvertFrom-Json -AsHashtable -Depth 100

$pkgs = @{}
foreach ($p in $man['packages']) {
  $id = $p['id'].ToLowerInvariant()
  if (-not $pkgs.ContainsKey($id)) { $pkgs[$id] = [System.Collections.ArrayList]::new() }
  [void]$pkgs[$id].Add($p)
}
Write-Host "      pacchetti: $($pkgs.Count)"

# --- versioni disponibili -----------------------------------------------------
$msvcVers = [System.Collections.Generic.List[string]]::new()
$sdkVers  = [System.Collections.Generic.List[string]]::new()
foreach ($id in $pkgs.Keys) {
  if ($id -match '^microsoft\.vc\.(\d+\.\d+\.\d+\.\d+)\.tools\.hostx64\.targetx64\.base$') {
    $msvcVers.Add($Matches[1])
  } elseif ($id -match '^microsoft\.visualstudio\.component\.windows11sdk\.(\d+)$') {
    $sdkVers.Add($Matches[1])
  }
}
if ($msvcVers.Count -eq 0) { throw 'nessuna versione MSVC trovata nel manifest' }
if ($sdkVers.Count -eq 0)  { throw 'nessuna versione Windows SDK trovata nel manifest' }

$msvcVer = ($msvcVers | Sort-Object { [version]$_ } | Select-Object -Last 1)
$sdkVer  = ($sdkVers  | Sort-Object { [int]$_ }     | Select-Object -Last 1)
Write-Host "      MSVC $msvcVer  ·  Windows SDK 10.0.$sdkVer.0"

# --- MSVC ---------------------------------------------------------------------
$want = @(
  "microsoft.vc.$msvcVer.tools.host$Host_.target$Target.base"
  "microsoft.vc.$msvcVer.tools.host$Host_.target$Target.res.base"
  "microsoft.vc.$msvcVer.crt.headers.base"
  "microsoft.vc.$msvcVer.crt.$Target.desktop.base"
  # Le import lib (msvcrt.lib, oldnames.lib, vcruntime.lib) stanno nel pacchetto
  # "store", non in "desktop": senza di esso il linker non trova OLDNAMES.lib.
  "microsoft.vc.$msvcVer.crt.$Target.store.base"
  "microsoft.vc.$msvcVer.crt.source.base"
)

Write-Host '[4/6] MSVC: download + estrazione...'
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($id in $want) {
  if (-not $pkgs.ContainsKey($id)) { Write-Host "      (assente, salto) $id"; continue }
  # I pacchetti localizzati esistono in piu' lingue: vogliamo l'inglese,
  # altrimenti la diagnostica del compilatore esce in una lingua a caso.
  $pkg = $pkgs[$id] | Where-Object { $_['language'] -eq 'en-US' } | Select-Object -First 1
  if (-not $pkg) { $pkg = $pkgs[$id] | Where-Object { -not $_['language'] } | Select-Object -First 1 }
  if (-not $pkg) { $pkg = $pkgs[$id][0] }
  foreach ($pl in $pkg['payloads']) {
    $fn = [System.IO.Path]::GetFileName($pl['fileName'].Replace('\', '/'))
    $out = Join-Path $Dl "vsix_${id}_$fn"
    Fetch $pl['url'] $out
    $zip = [System.IO.Compression.ZipFile]::OpenRead($out)
    try {
      foreach ($e in $zip.Entries) {
        if (-not $e.FullName.StartsWith('Contents/')) { continue }
        if ($e.FullName.EndsWith('/')) { continue }
        $rel = [uri]::UnescapeDataString($e.FullName.Substring(9))
        $tgt = Join-Path $Dest ($rel -replace '/', '\')
        # Già estratto e identico: non toccarlo (evita lock dell'antivirus).
        if ((Test-Path -LiteralPath $tgt) -and (Get-Item -LiteralPath $tgt).Length -eq $e.Length) { continue }
        New-Item -ItemType Directory -Force ([System.IO.Path]::GetDirectoryName($tgt)) | Out-Null
        for ($try = 0; $try -lt 10; $try++) {
          try { [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e, $tgt, $true); break }
          catch { if ($try -eq 9) { throw }; Start-Sleep -Milliseconds 400 }
        }
      }
    } finally { $zip.Dispose() }
  }
  Write-Host "      ok  $id"
}

# --- Windows SDK --------------------------------------------------------------
Write-Host '[5/6] Windows SDK: download MSI + CAB...'
$comp = $pkgs["microsoft.visualstudio.component.windows11sdk.$sdkVer"][0]
$depName = ($comp['dependencies'].Keys | Select-Object -First 1)
$sdkPkg = $pkgs[$depName.ToLowerInvariant()][0]

$msiWanted = @(
  'Windows SDK Desktop Headers x86-x86_en-us.msi'
  'Windows SDK Desktop Libs x64-x86_en-us.msi'
  "Windows SDK Desktop Tools $Target-x86_en-us.msi"
  'Windows SDK for Windows Store Apps Headers-x86_en-us.msi'
  'Windows SDK for Windows Store Apps Libs-x86_en-us.msi'
  'Windows SDK for Windows Store Apps Metadata-x86_en-us.msi'
  'Universal CRT Headers Libraries and Sources-x86_en-us.msi'
)

$sdkDir = Join-Path $Dl 'sdk'
New-Item -ItemType Directory -Force $sdkDir | Out-Null

$allCabs = @{}
$msiFiles = [System.Collections.Generic.List[string]]::new()
foreach ($pl in $sdkPkg['payloads']) {
  $name = $pl['fileName']
  if (-not $name.StartsWith('Installers\')) { continue }
  $base = $name.Substring('Installers\'.Length)
  if ($base.EndsWith('.cab')) { $allCabs[$base] = $pl['url']; continue }
  if ($msiWanted -contains $base) {
    $out = Join-Path $sdkDir $base
    Fetch $pl['url'] $out
    $msiFiles.Add($out)
    Write-Host "      msi  $base"
  }
}

# I CAB necessari si ricavano dai riferimenti dentro ogni MSI.
$needed = [System.Collections.Generic.HashSet[string]]::new()
foreach ($m in $msiFiles) {
  $bytes = [System.IO.File]::ReadAllBytes($m)
  $text = [System.Text.Encoding]::GetEncoding('latin1').GetString($bytes)
  foreach ($cab in $allCabs.Keys) {
    if ($text.Contains($cab)) { [void]$needed.Add($cab) }
  }
}
Write-Host "      cab necessari: $($needed.Count)"
foreach ($cab in $needed) { Fetch $allCabs[$cab] (Join-Path $sdkDir $cab) }

Write-Host '[6/6] Windows SDK: estrazione (msiexec /a)...'
$kitDir = $Dest
foreach ($m in $msiFiles) {
  $p = Start-Process msiexec.exe -ArgumentList @('/a', "`"$m`"", '/qn', "TARGETDIR=`"$kitDir`"") -Wait -PassThru -NoNewWindow
  if ($p.ExitCode -ne 0) { throw "msiexec fallito ($($p.ExitCode)) su $m" }
  Write-Host "      ok  $([System.IO.Path]::GetFileName($m))"
}
Get-ChildItem $Dest -Filter '*.msi' -File -ErrorAction SilentlyContinue | Remove-Item -Force

Write-Host ''
Write-Host "FATTO. MSVC $msvcVer, SDK 10.0.$sdkVer.0 in $Dest"
"$msvcVer"      | Out-File (Join-Path $Dest 'msvc-version.txt') -Encoding ascii -NoNewline
"10.0.$sdkVer.0" | Out-File (Join-Path $Dest 'sdk-version.txt')  -Encoding ascii -NoNewline
