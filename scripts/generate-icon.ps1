param(
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [Parameter(Mandatory = $true)][string]$SourcePath
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function New-IconBitmap([System.Drawing.Image]$Source, [int]$Size) {
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

    return $bitmap
}

function New-IconPng([System.Drawing.Image]$Source, [int]$Size) {
    $bitmap = New-IconBitmap $Source $Size

    $stream = [System.IO.MemoryStream]::new()
    try {
        $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        return ,$stream.ToArray()
    } finally {
        $stream.Dispose()
        $bitmap.Dispose()
    }
}

function New-IconDib([System.Drawing.Image]$Source, [int]$Size) {
    $bitmap = New-IconBitmap $Source $Size
    $stream = [System.IO.MemoryStream]::new()
    $writer = [System.IO.BinaryWriter]::new($stream)
    $data = $null
    try {
        $pixelBytes = $Size * $Size * 4
        $writer.Write([uint32]40)
        $writer.Write([int32]$Size)
        $writer.Write([int32]($Size * 2))
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]0)
        $writer.Write([uint32]$pixelBytes)
        $writer.Write([int32]0)
        $writer.Write([int32]0)
        $writer.Write([uint32]0)
        $writer.Write([uint32]0)

        $rectangle = [System.Drawing.Rectangle]::new(0, 0, $Size, $Size)
        $data = $bitmap.LockBits($rectangle, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                                 [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $row = [byte[]]::new($Size * 4)
        $maskStride = [int]([Math]::Ceiling($Size / 32.0) * 4)
        $maskRows = [System.Collections.Generic.List[byte[]]]::new()
        for ($y = $Size - 1; $y -ge 0; --$y) {
            [System.Runtime.InteropServices.Marshal]::Copy(
                [IntPtr]::Add($data.Scan0, $y * $data.Stride), $row, 0, $row.Length)
            $writer.Write($row)

            $mask = [byte[]]::new($maskStride)
            for ($x = 0; $x -lt $Size; ++$x) {
                if ($row[$x * 4 + 3] -lt 128) {
                    $byteIndex = $x -shr 3
                    $mask[$byteIndex] = [byte]($mask[$byteIndex] -bor (0x80 -shr ($x % 8)))
                }
            }
            $maskRows.Add($mask)
        }
        $bitmap.UnlockBits($data)
        $data = $null
        foreach ($mask in $maskRows) { $writer.Write($mask) }
        $writer.Flush()
        return ,$stream.ToArray()
    } finally {
        if ($null -ne $data) { $bitmap.UnlockBits($data) }
        $writer.Dispose()
        $stream.Dispose()
        $bitmap.Dispose()
    }
}

$resolvedSource = (Resolve-Path -LiteralPath $SourcePath).Path
$source = [System.Drawing.Image]::FromFile($resolvedSource)
try {
    $sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
    $images = [System.Collections.Generic.List[byte[]]]::new()
    foreach ($size in $sizes) {
        # Native DIB frames are understood by every Windows small-icon path.
        # Larger frames remain PNG-compressed to keep the executable compact.
        $images.Add($(if ($size -le 48) { New-IconDib $source $size } else { New-IconPng $source $size }))
    }

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
