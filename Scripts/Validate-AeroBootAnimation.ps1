[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $AnimationDirectory
)

$ErrorActionPreference = 'Stop'
$files = 0..104 | ForEach-Object {
    $path = Join-Path $AnimationDirectory (('activity{0:D3}.bmp' -f $_))
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing frame: $path" }
    $path
}

foreach ($path in $files) {
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 54 -or $bytes[0] -ne 0x42 -or $bytes[1] -ne 0x4D) { throw "Invalid BMP signature: $path" }
    $width = [BitConverter]::ToInt32($bytes, 18)
    $height = [BitConverter]::ToInt32($bytes, 22)
    $bits = [BitConverter]::ToUInt16($bytes, 28)
    $compression = [BitConverter]::ToUInt32($bytes, 30)
    if ($width -ne 200 -or [Math]::Abs($height) -ne 200 -or $bits -ne 24 -or $compression -ne 0) {
        throw "Unsupported frame format: $path ($width x $height, $bits bpp, compression $compression)"
    }
}

[pscustomobject]@{
    Frames = $files.Count
    Width = 200
    Height = 200
    BitsPerPixel = 24
    FrameRate = '15 FPS'
    Duration = '7 seconds'
    Directory = (Resolve-Path -LiteralPath $AnimationDirectory).Path
}
