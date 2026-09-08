# make-icon.ps1 - genera assets\minibar.ico (PNG multi-risoluzione).
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$out = Join-Path (Split-Path $PSScriptRoot) 'assets\minibar.ico'
New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$frames = @()

foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.Clear([System.Drawing.Color]::Transparent)

    # Piastrella scura arrotondata.
    $r = [int][math]::Max(2, $s * 0.22)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($s - $d, 0, $d, $d, 270, 90)
    $path.AddArc($s - $d, $s - $d, $d, $d, 0, 90)
    $path.AddArc(0, $s - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $bg = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 20, 20, 22))
    $g.FillPath($bg, $path)

    # Tre barre verdi tipo equalizzatore.
    $green = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 29, 185, 84))
    $bw = [math]::Max(1.0, $s * 0.12)
    $gap = [math]::Max(1.0, $s * 0.09)
    $total = $bw * 3 + $gap * 2
    $x0 = ($s - $total) / 2.0
    $heights = @(0.34, 0.56, 0.44)
    for ($i = 0; $i -lt 3; $i++) {
        $h = $s * $heights[$i]
        $x = $x0 + $i * ($bw + $gap)
        $y = ($s - $h) / 2.0
        $g.FillRectangle($green, $x, $y, $bw, $h)
    }

    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $frames += , @{ size = $s; data = $ms.ToArray() }
    $ms.Dispose()
}

# Assemblaggio ICO: header + directory + PNG in coda.
$n = $frames.Count
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)
$bw.Write([uint16]0)   # reserved
$bw.Write([uint16]1)   # type = icona
$bw.Write([uint16]$n)

$offset = 6 + 16 * $n
foreach ($f in $frames) {
    $dim = if ($f.size -ge 256) { 0 } else { $f.size }
    $bw.Write([byte]$dim)
    $bw.Write([byte]$dim)
    $bw.Write([byte]0)     # palette
    $bw.Write([byte]0)     # reserved
    $bw.Write([uint16]1)   # planes
    $bw.Write([uint16]32)  # bpp
    $bw.Write([uint32]$f.data.Length)
    $bw.Write([uint32]$offset)
    $offset += $f.data.Length
}
foreach ($f in $frames) { $bw.Write($f.data) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($out, $ms.ToArray())
$bw.Dispose()

Write-Host ("icona scritta: {0} ({1:N0} byte, {2} risoluzioni)" -f $out, (Get-Item $out).Length, $n)
