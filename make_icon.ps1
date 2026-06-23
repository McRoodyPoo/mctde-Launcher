# Builds launcher\mctde.ico as a valid multi-resolution (PNG-framed) icon.
#
#   .\make_icon.ps1                 # draws the stand-in mask
#   .\make_icon.ps1 -Png mask.png   # uses your artwork (any square PNG, transparent ok)
#
# Drop your real mask art in as a PNG and re-run with -Png to replace the stand-in.
param([string]$Png = "")

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"
$sizes = 16, 24, 32, 48, 64, 128, 256
$outIco = Join-Path $PSScriptRoot "mctde.ico"

function New-Frame([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = "AntiAlias"
    $g.InterpolationMode = "HighQualityBicubic"
    $g.Clear([System.Drawing.Color]::Transparent)

    if ($Png -and (Test-Path $Png)) {
        $src = [System.Drawing.Image]::FromFile((Resolve-Path $Png))
        $bmpSrc = New-Object System.Drawing.Bitmap($src)
        # Solid opaque black, full square edge-to-edge (no transparency, no rounded corners).
        $g.Clear([System.Drawing.Color]::FromArgb(255, 0, 0, 0))
        # Trim the mask's transparent margin and scale it to fill the icon as much as possible.
        $b = Get-AlphaBounds $bmpSrc
        $pad = [int]($s * 0.02)
        $avail = $s - 2 * $pad
        $scale = $avail / [Math]::Max($b.W, $b.H)
        $dw = [int]($b.W * $scale); $dh = [int]($b.H * $scale)
        $dx = [int](($s - $dw) / 2); $dy = [int](($s - $dh) / 2)
        $dest = New-Object System.Drawing.Rectangle($dx, $dy, $dw, $dh)
        $g.DrawImage($bmpSrc, $dest, $b.X, $b.Y, $b.W, $b.H, [System.Drawing.GraphicsUnit]::Pixel)
        $bmpSrc.Dispose(); $src.Dispose()
    } else {
        Draw-Tile $g $s
        Draw-Mask $g $s
    }
    $g.Dispose()
    return $bmp
}

# Bounding box of non-transparent pixels, so we can crop a mask's empty margin.
function Get-AlphaBounds([System.Drawing.Bitmap]$bm) {
    $minX = $bm.Width; $minY = $bm.Height; $maxX = -1; $maxY = -1
    for ($y = 0; $y -lt $bm.Height; $y++) {
        for ($x = 0; $x -lt $bm.Width; $x++) {
            if ($bm.GetPixel($x, $y).A -gt 16) {
                if ($x -lt $minX) { $minX = $x }; if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }; if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }
    if ($maxX -lt 0) { return @{ X = 0; Y = 0; W = $bm.Width; H = $bm.Height } }
    return @{ X = $minX; Y = $minY; W = ($maxX - $minX + 1); H = ($maxY - $minY + 1) }
}

function Draw-Tile([System.Drawing.Graphics]$g, [int]$s) {
    $r = [Math]::Max(2, [int]($s * 0.18))
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($s - $d, 0, $d, $d, 270, 90)
    $path.AddArc($s - $d, $s - $d, $d, $d, 0, 90)
    $path.AddArc(0, $s - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 18, 18, 22))
    $g.FillPath($brush, $path)
    $brush.Dispose(); $path.Dispose()
}

# Stand-in: a symmetric white mask (flame crown, face, two eye slits, nose).
function Draw-Mask([System.Drawing.Graphics]$g, [int]$s) {
    $white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
    $bg = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 18, 18, 22))
    $cx = $s / 2.0
    function P($x, $y) { New-Object System.Drawing.PointF([single]($x * $s), [single]($y * $s)) }

    # face teardrop
    $face = New-Object System.Drawing.Drawing2D.GraphicsPath
    $facePts = @((P 0.50 0.16), (P 0.78 0.30), (P 0.72 0.74), (P 0.50 0.90), (P 0.28 0.74), (P 0.22 0.30))
    $face.AddClosedCurve([System.Drawing.PointF[]]$facePts, 0.4)
    $g.FillPath($white, $face); $face.Dispose()

    # flame crown (three points)
    $crown = New-Object System.Drawing.Drawing2D.GraphicsPath
    $crownPts = @((P 0.30 0.30), (P 0.34 0.06), (P 0.42 0.26), (P 0.50 0.02), (P 0.58 0.26), (P 0.66 0.06), (P 0.70 0.30))
    $crown.AddPolygon([System.Drawing.PointF[]]$crownPts)
    $g.FillPath($white, $crown); $crown.Dispose()

    # eye slits (cut back to tile color)
    $g.FillPolygon($bg, [System.Drawing.PointF[]]@((P 0.34 0.46), (P 0.45 0.44), (P 0.44 0.52), (P 0.35 0.53)))
    $g.FillPolygon($bg, [System.Drawing.PointF[]]@((P 0.66 0.46), (P 0.55 0.44), (P 0.56 0.52), (P 0.65 0.53)))
    # nose
    $g.FillPolygon($bg, [System.Drawing.PointF[]]@((P 0.485 0.50), (P 0.515 0.50), (P 0.52 0.70), (P 0.48 0.70)))

    $white.Dispose(); $bg.Dispose()
}

# --- assemble multi-image ICO with PNG-compressed frames ---
$frames = @()
foreach ($s in $sizes) {
    $bmp = New-Frame $s
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $frames += , @{ size = $s; bytes = $ms.ToArray() }
    $bmp.Dispose(); $ms.Dispose()
}

$fs = New-Object System.IO.FileStream($outIco, [System.IO.FileMode]::Create)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$frames.Count)   # ICONDIR
$offset = 6 + 16 * $frames.Count
foreach ($f in $frames) {
    $dim = if ($f.size -ge 256) { 0 } else { $f.size }
    $bw.Write([Byte]$dim); $bw.Write([Byte]$dim)         # width, height (0 => 256)
    $bw.Write([Byte]0); $bw.Write([Byte]0)               # colors, reserved
    $bw.Write([UInt16]1); $bw.Write([UInt16]32)          # planes, bpp
    $bw.Write([UInt32]$f.bytes.Length)                   # bytesInRes
    $bw.Write([UInt32]$offset)                           # imageOffset
    $offset += $f.bytes.Length
}
foreach ($f in $frames) { $bw.Write($f.bytes) }
$bw.Flush(); $bw.Close(); $fs.Close()
Write-Output "Wrote $outIco ($($frames.Count) frames)"
