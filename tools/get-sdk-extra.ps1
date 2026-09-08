# get-sdk-extra.ps1 — aggiunge alla toolchain portable altri MSI del Windows SDK.
param([Parameter(Mandatory)][string[]]$Msi)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Root = $PSScriptRoot  # la cartella tools\ di questo repo, ovunque stia
$Dl = Join-Path $Root '_dl'
$sdkDir = Join-Path $Dl 'sdk'
$Dest = Join-Path $Root 'msvc'

function Fetch($url, $out) {
  if (Test-Path $out) { return }
  curl.exe -sS -L --retry 4 -o "$out.part" $url
  if ($LASTEXITCODE -ne 0) { throw "download fallito: $url" }
  # L'antivirus può tenere il file aperto per qualche istante dopo il download.
  for ($i = 0; $i -lt 20; $i++) {
    try { Move-Item "$out.part" $out -Force; return } catch { Start-Sleep -Milliseconds 300 }
  }
  throw "impossibile rinominare $out.part"
}

$man = Get-Content -Raw (Join-Path $Dl 'VisualStudio.vsman') | ConvertFrom-Json -AsHashtable -Depth 100
$pkgs = @{}
foreach ($p in $man['packages']) {
  $id = $p['id'].ToLowerInvariant()
  if (-not $pkgs.ContainsKey($id)) { $pkgs[$id] = [System.Collections.ArrayList]::new() }
  [void]$pkgs[$id].Add($p)
}
$sdkVer = (Get-Content -Raw (Join-Path $Dest 'sdk-version.txt')).Split('.')[2]
$comp = $pkgs["microsoft.visualstudio.component.windows11sdk.$sdkVer"][0]
$sdk = $pkgs[($comp['dependencies'].Keys | Select-Object -First 1).ToLowerInvariant()][0]

$allCabs = @{}
$msiFiles = [System.Collections.Generic.List[string]]::new()
foreach ($pl in $sdk['payloads']) {
  $n = $pl['fileName']
  if (-not $n.StartsWith('Installers\')) { continue }
  $b = $n.Substring(11)
  if ($b.EndsWith('.cab')) { $allCabs[$b] = $pl['url']; continue }
  if ($Msi -contains $b) {
    $out = Join-Path $sdkDir $b
    Fetch $pl['url'] $out
    $msiFiles.Add($out)
    Write-Host "msi  $b"
  }
}
if ($msiFiles.Count -ne $Msi.Count) { Write-Warning "trovati $($msiFiles.Count) MSI su $($Msi.Count) richiesti" }

$needed = [System.Collections.Generic.HashSet[string]]::new()
foreach ($m in $msiFiles) {
  $text = [System.Text.Encoding]::GetEncoding('latin1').GetString([System.IO.File]::ReadAllBytes($m))
  foreach ($c in $allCabs.Keys) { if ($text.Contains($c)) { [void]$needed.Add($c) } }
}
Write-Host "cab: $($needed.Count)"
foreach ($c in $needed) { Fetch $allCabs[$c] (Join-Path $sdkDir $c) }

foreach ($m in $msiFiles) {
  $p = Start-Process msiexec.exe -ArgumentList @('/a', "`"$m`"", '/qn', "TARGETDIR=`"$Dest`"") -Wait -PassThru -NoNewWindow
  if ($p.ExitCode -ne 0) { throw "msiexec fallito ($($p.ExitCode)) su $m" }
  Write-Host "ok   $([System.IO.Path]::GetFileName($m))"
}
Get-ChildItem $Dest -Filter '*.msi' -File -EA SilentlyContinue | Remove-Item -Force
Write-Host 'fatto.'
