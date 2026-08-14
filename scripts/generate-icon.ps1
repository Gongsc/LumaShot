param([Parameter(Mandatory = $true)][string]$OutputPath)
$ErrorActionPreference = 'Stop'

function New-IconDib([int]$Size) {
    $stream = [System.IO.MemoryStream]::new()
    $writer = [System.IO.BinaryWriter]::new($stream)
    $maskStride = [int](([Math]::Floor(($Size + 31) / 32)) * 4)
    $pixelBytes = $Size * $Size * 4
    $writer.Write([uint32]40)
    $writer.Write([int32]$Size)
    $writer.Write([int32]($Size * 2))
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]0)
    $writer.Write([uint32]$pixelBytes)
    $writer.Write([int32]3780)
    $writer.Write([int32]3780)
    $writer.Write([uint32]0)
    $writer.Write([uint32]0)

    $margin = [Math]::Max(1, [int][Math]::Round($Size * 0.18))
    $edge = [Math]::Max(1, [int][Math]::Round($Size * 0.07))
    $arm = [Math]::Max(3, [int][Math]::Round($Size * 0.25))
    $center = ($Size - 1) / 2.0
    for ($y = $Size - 1; $y -ge 0; --$y) {
        for ($x = 0; $x -lt $Size; ++$x) {
            $t = ($x + ($Size - 1 - $y)) / [Math]::Max(1.0, 2.0 * ($Size - 1))
            $red = [int](25 + 205 * [Math]::Max(0.0, ($t - 0.48) / 0.52))
            $green = [int](80 + 100 * (1.0 - [Math]::Abs($t - 0.5) * 2.0))
            $blue = [int](120 + 115 * (1.0 - $t))
            $nearLeft = [Math]::Abs($x - $margin) -lt $edge
            $nearRight = [Math]::Abs($x - ($Size - 1 - $margin)) -lt $edge
            $nearTop = [Math]::Abs($y - $margin) -lt $edge
            $nearBottom = [Math]::Abs($y - ($Size - 1 - $margin)) -lt $edge
            $verticalArm = ($y -ge $margin -and $y -lt ($margin + $arm)) -or ($y -le ($Size - 1 - $margin) -and $y -gt ($Size - 1 - $margin - $arm))
            $horizontalArm = ($x -ge $margin -and $x -lt ($margin + $arm)) -or ($x -le ($Size - 1 - $margin) -and $x -gt ($Size - 1 - $margin - $arm))
            $corner = (($nearLeft -or $nearRight) -and $verticalArm) -or (($nearTop -or $nearBottom) -and $horizontalArm)
            $radius = [Math]::Sqrt(($x - $center) * ($x - $center) + ($y - $center) * ($y - $center))
            if ($corner -or $radius -lt $Size * 0.105) { $red = 255; $green = 255; $blue = 255 }
            $writer.Write([byte]$blue)
            $writer.Write([byte]$green)
            $writer.Write([byte]$red)
            $writer.Write([byte]255)
        }
    }
    $writer.Write([byte[]]::new($maskStride * $Size))
    $writer.Flush()
    $bytes = $stream.ToArray()
    $writer.Dispose()
    $stream.Dispose()
    return $bytes
}

$sizes = @(16, 24, 32, 48)
$images = [System.Collections.Generic.List[byte[]]]::new()
foreach ($size in $sizes) { $images.Add((New-IconDib $size)) }
$directory = Split-Path -Parent $OutputPath
if ($directory) { [System.IO.Directory]::CreateDirectory($directory) | Out-Null }
$file = [System.IO.File]::Open($OutputPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
$icon = [System.IO.BinaryWriter]::new($file)
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
$icon.Dispose()
$file.Dispose()
