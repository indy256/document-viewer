# Convert the generated master artwork into desktop icon formats (Windows/.NET).
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$iconDirectory = Join-Path $PSScriptRoot '../resources/icons'
$source = [System.Drawing.Image]::FromFile((Join-Path $iconDirectory 'DocumentViewer-master.png'))
$images = @{}
try {
    foreach ($size in @(16, 24, 32, 48, 64, 128, 256, 512, 1024)) {
        $bitmap = New-Object System.Drawing.Bitmap($size, $size)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $buffer = New-Object System.IO.MemoryStream
        try {
            $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.DrawImage($source, (New-Object System.Drawing.Rectangle(0, 0, $size, $size)))
            $bitmap.Save($buffer, [System.Drawing.Imaging.ImageFormat]::Png)
            $images[$size] = $buffer.ToArray()
        } finally {
            $buffer.Dispose()
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
} finally { $source.Dispose() }
[System.IO.File]::WriteAllBytes((Join-Path $iconDirectory 'DocumentViewer.png'), $images[512])

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$stream = [System.IO.File]::Create((Join-Path $iconDirectory 'DocumentViewer.ico'))
$writer = New-Object System.IO.BinaryWriter($stream)
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    foreach ($size in $sizes) {
        $dimension = [byte]($size % 256)
        $writer.Write($dimension)
        $writer.Write($dimension)
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$images[$size].Length)
        $writer.Write([uint32]$offset)
        $offset += $images[$size].Length
    }
    foreach ($size in $sizes) { $writer.Write([byte[]]$images[$size]) }
} finally { $writer.Dispose() }

function Write-BigEndian([System.IO.BinaryWriter]$Writer, [uint32]$Value) {
    $bytes = [BitConverter]::GetBytes($Value)
    if ([BitConverter]::IsLittleEndian) { [Array]::Reverse($bytes) }
    $Writer.Write($bytes)
}
$types = [ordered]@{ icp4 = 16; icp5 = 32; icp6 = 64; ic07 = 128; ic08 = 256; ic09 = 512; ic10 = 1024 }
$length = 8
foreach ($size in $types.Values) { $length += 8 + $images[$size].Length }
$stream = [System.IO.File]::Create((Join-Path $iconDirectory 'DocumentViewer.icns'))
$writer = New-Object System.IO.BinaryWriter($stream)
try {
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('icns'))
    Write-BigEndian $writer $length
    foreach ($type in $types.Keys) {
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes($type))
        Write-BigEndian $writer (8 + $images[$types[$type]].Length)
        $writer.Write([byte[]]$images[$types[$type]])
    }
} finally { $writer.Dispose() }
Write-Host "Wrote PNG, ICO, and ICNS icons to $iconDirectory"
