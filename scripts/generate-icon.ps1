param(
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [Parameter(Mandatory = $true)][string]$SourcePath
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function New-IconPng([System.Drawing.Image]$Source, [int]$Size) {
    $bitmap = [System.Drawing.Bitmap]::new($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $destination = [System.Drawing.Rectangle]::new(0, 0, $Size, $Size)
        $graphics.DrawImage($Source, $destination, 0, 0, $Source.Width, $Source.Height, [System.Drawing.GraphicsUnit]::Pixel)
    } finally {
        $graphics.Dispose()
    }

    $stream = [System.IO.MemoryStream]::new()
    try {
        $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        return ,$stream.ToArray()
    } finally {
        $stream.Dispose()
        $bitmap.Dispose()
    }
}

$resolvedSource = (Resolve-Path -LiteralPath $SourcePath).Path
$source = [System.Drawing.Image]::FromFile($resolvedSource)
try {
    $sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
    $images = [System.Collections.Generic.List[byte[]]]::new()
    foreach ($size in $sizes) { $images.Add((New-IconPng $source $size)) }

    $directory = Split-Path -Parent $OutputPath
    if ($directory) { [System.IO.Directory]::CreateDirectory($directory) | Out-Null }
    $file = [System.IO.File]::Open($OutputPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
    $icon = [System.IO.BinaryWriter]::new($file)
    try {
        $icon.Write([uint16]0)
        $icon.Write([uint16]1)
        $icon.Write([uint16]$sizes.Count)
        $offset = 6 + 16 * $sizes.Count
        for ($index = 0; $index -lt $sizes.Count; ++$index) {
            $size = $sizes[$index]
            $icon.Write([byte]($(if ($size -eq 256) { 0 } else { $size })))
            $icon.Write([byte]($(if ($size -eq 256) { 0 } else { $size })))
            $icon.Write([byte]0)
            $icon.Write([byte]0)
            $icon.Write([uint16]1)
            $icon.Write([uint16]32)
            $icon.Write([uint32]$images[$index].Length)
            $icon.Write([uint32]$offset)
            $offset += $images[$index].Length
        }
        foreach ($image in $images) { $icon.Write($image) }
    } finally {
        $icon.Dispose()
        $file.Dispose()
    }
} finally {
    $source.Dispose()
}
