# env.ps1 - attiva la toolchain portable nella sessione corrente.
#   . .\tools\env.ps1     (nota il punto iniziale: dev'essere "dot-sourced")

$Tools = $PSScriptRoot   # la cartella di questo script, ovunque stia il repo
$Msvc  = Join-Path $Tools 'msvc'
$VcVer = (Get-Content -Raw (Join-Path $Msvc 'msvc-version-dir.txt') -ErrorAction SilentlyContinue)
if (-not $VcVer) {
    $VcVer = (Get-ChildItem (Join-Path $Msvc 'VC\Tools\MSVC') -Directory |
              Sort-Object Name | Select-Object -Last 1).Name
}
$VcVer = $VcVer.Trim()
$SdkVer = (Get-Content -Raw (Join-Path $Msvc 'sdk-version.txt')).Trim()

$VcDir  = Join-Path $Msvc "VC\Tools\MSVC\$VcVer"
$KitInc = Join-Path $Msvc "Windows Kits\10\Include\$SdkVer"
$KitLib = Join-Path $Msvc "Windows Kits\10\Lib\$SdkVer"
$KitBin = Join-Path $Msvc "Windows Kits\10\bin\$SdkVer\x64"

$env:INCLUDE = @(
    (Join-Path $VcDir 'include')
    (Join-Path $KitInc 'ucrt')
    (Join-Path $KitInc 'shared')
    (Join-Path $KitInc 'um')
    (Join-Path $KitInc 'winrt')
    (Join-Path $KitInc 'cppwinrt')
) -join ';'

$env:LIB = @(
    (Join-Path $VcDir 'lib\x64')
    (Join-Path $KitLib 'ucrt\x64')
    (Join-Path $KitLib 'um\x64')
) -join ';'

$env:PATH = @(
    (Join-Path $VcDir 'bin\Hostx64\x64')
    $KitBin
    (Join-Path $Tools 'cmake\bin')
    (Join-Path $Tools 'ninja')
    (Join-Path $Tools 'git\cmd')
    (Join-Path $Tools 'gh\bin')
    $env:PATH
) -join ';'

# gh tiene configurazione e token su E invece che in %APPDATA%
$env:GH_CONFIG_DIR = Join-Path $Tools 'gh\config'

$env:VSLANG = '1033'  # diagnostica del compilatore in inglese
$env:MINIBAR_VC_VERSION  = $VcVer
$env:MINIBAR_SDK_VERSION = $SdkVer

Write-Host "toolchain portable attiva - MSVC $VcVer, Windows SDK $SdkVer"
