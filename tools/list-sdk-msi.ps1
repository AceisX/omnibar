$ErrorActionPreference = 'Stop'
$manifest = Join-Path $PSScriptRoot '_dl\VisualStudio.vsman'
if (-not (Test-Path $manifest)) {
  throw "manca $manifest : lo scarica get-msvc.ps1, esegui prima quello"
}
$man = Get-Content -Raw $manifest | ConvertFrom-Json -AsHashtable -Depth 100
$pkgs = @{}
foreach ($p in $man['packages']) {
  $id = $p['id'].ToLowerInvariant()
  if (-not $pkgs.ContainsKey($id)) { $pkgs[$id] = [System.Collections.ArrayList]::new() }
  [void]$pkgs[$id].Add($p)
}
$comp = $pkgs['microsoft.visualstudio.component.windows11sdk.26100'][0]
$dep = ($comp['dependencies'].Keys | Select-Object -First 1)
$sdk = $pkgs[$dep.ToLowerInvariant()][0]
foreach ($pl in $sdk['payloads']) {
  $n = $pl['fileName']
  if ($n.StartsWith('Installers\') -and $n.EndsWith('.msi')) {
    '{0,10:N1} MB  {1}' -f ($pl['size'] / 1MB), $n.Substring(11)
  }
}
