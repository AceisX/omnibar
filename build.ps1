# build.ps1 - configura e compila OmniBar con la toolchain portable in tools\.
param(
    [ValidateSet('release', 'debug')][string]$Config = 'release',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

. (Join-Path $root 'tools\env.ps1')

if ($Clean) {
    Remove-Item -Recurse -Force (Join-Path $root "build\$Config") -ErrorAction SilentlyContinue
}

Push-Location $root
try {
    cmake --preset $Config
    if ($LASTEXITCODE -ne 0) { throw "configure fallito" }
    cmake --build --preset $Config
    if ($LASTEXITCODE -ne 0) { throw "build fallita" }

    $exe = Join-Path $root 'dist\omnibar.exe'
    if (Test-Path $exe) {
        $kb = [math]::Round((Get-Item $exe).Length / 1KB, 1)
        Write-Host ""
        Write-Host "OK  $exe  ($kb KB)"
    }
}
finally { Pop-Location }
