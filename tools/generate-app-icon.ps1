$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$destination = Join-Path $PSScriptRoot '../icon/app'
[System.IO.Directory]::CreateDirectory($destination) | Out-Null
function Fill-RoundedRectangle($graphics, $color, $x, $y, $width, $height, $radius) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $diameter = $radius * 2
    $path.AddArc($x, $y, $diameter, $diameter, 180, 90)
    $path.AddArc(($x + $width - $diameter), $y, $diameter, $diameter, 270, 90)
    $path.AddArc(($x + $width - $diameter), ($y + $height - $diameter), $diameter, $diameter, 0, 90)
    $path.AddArc($x, ($y + $height - $diameter), $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml($color))
    $graphics.FillPath($brush, $path)
    $brush.Dispose()
    $path.Dispose()
}
$master = New-Object System.Drawing.Bitmap 1024, 1024
$graphics = [System.Drawing.Graphics]::FromImage($master)
$graphics.Clear([System.Drawing.Color]::Transparent)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.ScaleTransform(4, 4)
Fill-RoundedRectangle $graphics '#2563eb' 8 8 240 240 52
Fill-RoundedRectangle $graphics '#93c5fd' 45 66 146 146 16
Fill-RoundedRectangle $graphics '#ffffff' 65 44 150 150 16
Fill-RoundedRectangle $graphics '#dbeafe' 79 58 122 112 8
$sun = New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml('#f59e0b'))
$graphics.FillEllipse($sun, 152, 72, 28, 28)
$sun.Dispose()
$mountain = New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml('#2563eb'))
$graphics.FillPolygon($mountain, [System.Drawing.PointF[]]@([System.Drawing.PointF]::new(79,144), [System.Drawing.PointF]::new(115,100), [System.Drawing.PointF]::new(164,160), [System.Drawing.PointF]::new(79,160)))
$mountain.Dispose()
$foreground = New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml('#0891b2'))
$graphics.FillPolygon($foreground, [System.Drawing.PointF[]]@([System.Drawing.PointF]::new(128,160), [System.Drawing.PointF]::new(166,119), [System.Drawing.PointF]::new(201,154), [System.Drawing.PointF]::new(201,170), [System.Drawing.PointF]::new(79,170), [System.Drawing.PointF]::new(79,160)))
$foreground.Dispose()
$graphics.Dispose()
$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$frames = @()
foreach ($size in $sizes) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    $canvas = [System.Drawing.Graphics]::FromImage($bitmap)
    $canvas.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $canvas.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $canvas.DrawImage($master, 0, 0, $size, $size)
    $canvas.Dispose()
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $frames += ,$stream.ToArray()
    if ($size -eq 256) { $bitmap.Save((Join-Path $destination 'album.png'), [System.Drawing.Imaging.ImageFormat]::Png) }
    $stream.Dispose()
    $bitmap.Dispose()
}
$master.Dispose()
$file = [System.IO.File]::Create((Join-Path $destination 'album.ico'))
$writer = New-Object System.IO.BinaryWriter $file
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $dimension = $sizes[$i] % 256
        $writer.Write([byte]$dimension)
        $writer.Write([byte]$dimension)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$frames[$i].Length)
        $writer.Write([uint32]$offset)
        $offset += $frames[$i].Length
    }
    foreach ($frame in $frames) { $writer.Write([byte[]]$frame) }
} finally { $writer.Dispose(); $file.Dispose() }
Write-Output 'Generated album.ico (16-256 px) and album.png.'
