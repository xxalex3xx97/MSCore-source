# Renders cpp\manager\mscore.ico (16,24,32,48,64,128,256; PNG-compressed entries) with System.Drawing.
# Usage: powershell -File scripts\make-icon.ps1[-PreviewDir <dir>]   (PreviewDir: also saves each frame as PNG)
param([string]$PreviewDir = '')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$out   = Join-Path (Split-Path -Parent $PSScriptRoot) 'cpp\manager\mscore.ico'
$sizes = 16, 24, 32, 48, 64, 128, 256
$dark  = [System.Drawing.ColorTranslator]::FromHtml('#14181F')
$cyan  = [System.Drawing.ColorTranslator]::FromHtml('#22D3EE')
$viol  = [System.Drawing.ColorTranslator]::FromHtml('#8B5CF6')

function New-Frame([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        $g.Clear([System.Drawing.Color]::Transparent)

        # rounded-square background (corner radius 22%)
        $d = [single]($s * 0.44)
        $sz = [single]$s
        $p = New-Object System.Drawing.Drawing2D.GraphicsPath
        $p.AddArc([single]0, [single]0, $d, $d, 180, 90)
        $p.AddArc($sz - $d, [single]0, $d, $d, 270, 90)
        $p.AddArc($sz - $d, $sz - $d, $d, $d, 0, 90)
        $p.AddArc([single]0, $sz - $d, $d, $d, 90, 90)
        $p.CloseFigure()
        $bg = New-Object System.Drawing.SolidBrush $dark
        $g.FillPath($bg, $p)

        # pointy-top hexagon, cyan -> violet diagonal gradient
        $R = $s * 0.36
        $pts = @()
        for ($k = 0; $k -lt 6; $k++) {
            $a = (-90 + 60 * $k) * [Math]::PI / 180
            $pts += [System.Drawing.PointF]::new([single]($s / 2.0 + $R * [Math]::Cos($a)), [single]($s / 2.0 + $R * [Math]::Sin($a)))
        }
        $grad = [System.Drawing.Drawing2D.LinearGradientBrush]::new([System.Drawing.PointF]::new([single]0, [single]0), [System.Drawing.PointF]::new($sz, $sz), $cyan, $viol)

        if ($s -le 24) {
            # small sizes: solid gem, no inner glyph (crisp at 16 px)
            $g.FillPolygon($grad, [System.Drawing.PointF[]]$pts)
        } else {
            $w = [single][Math]::Max(2.0, $s * 0.085)
            $pen = [System.Drawing.Pen]::new([System.Drawing.Brush]$grad, $w)
            $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
            $g.DrawPolygon($pen, [System.Drawing.PointF[]]$pts)
            $pen.Dispose()

            # white bold M, centred
            $font = [System.Drawing.Font]::new('Segoe UI', [single]($s * 0.40), [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
            $sf = [System.Drawing.StringFormat]::new()
            $sf.Alignment = [System.Drawing.StringAlignment]::Center
            $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
            $g.DrawString('M', $font, [System.Drawing.Brushes]::White, [System.Drawing.RectangleF]::new(0, 0, $sz, $sz), $sf)
            $font.Dispose()
            $sf.Dispose()
        }
        $grad.Dispose(); $bg.Dispose(); $p.Dispose()
    } finally {
        $g.Dispose()
    }
    return $bmp
}

$frames = foreach ($s in $sizes) {
    $bmp = New-Frame $s
    if ($PreviewDir) {
        New-Item -ItemType Directory -Force $PreviewDir | Out-Null
        $bmp.Save((Join-Path $PreviewDir "mscore-$s.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    }
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    [pscustomobject]@{ Size = $s; Bytes = $ms.ToArray() }
}

# ICO container: ICONDIR + one ICONDIRENTRY per frame + PNG payloads (Vista+ format, 256 stored as 0)
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$frames.Count)
$offset = 6 + 16 * $frames.Count
foreach ($f in $frames) {
    $dim = if ($f.Size -ge 256) { 0 } else { $f.Size }
    $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$f.Bytes.Length); $bw.Write([uint32]$offset)
    $offset += $f.Bytes.Length
}
foreach ($f in $frames) { $bw.Write($f.Bytes) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($out, $ms.ToArray())
$bw.Dispose()
"{0}  frames: {1}  {2:N0} B" -f $out, ($sizes -join ','), (Get-Item $out).Length
