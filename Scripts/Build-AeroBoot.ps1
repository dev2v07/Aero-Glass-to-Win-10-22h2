[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })]
    [string] $Edk2Path,

    [ValidateSet('DEBUG', 'RELEASE', 'NOOPT')]
    [string] $BuildTarget = 'RELEASE',

    [string] $ToolChain = 'VS2022'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$edk2Root = (Resolve-Path -LiteralPath $Edk2Path).Path
$buildCommand = Join-Path $edk2Root 'BaseTools\BinWrappers\Win32\build.bat'
if (-not (Test-Path -LiteralPath $buildCommand)) {
    $buildCommand = Join-Path $edk2Root 'BaseTools\BinWrappers\Win32\build.exe'
}
if (-not (Test-Path -LiteralPath $buildCommand)) {
    throw "EDK II build tool was not found under: $edk2Root\BaseTools\BinWrappers\Win32"
}

$dsc = Join-Path $repositoryRoot 'AeroBoot\AeroBoot.dsc'
if (-not (Test-Path -LiteralPath $dsc -PathType Leaf)) {
    throw "AeroBoot DSC was not found: $dsc"
}

$env:WORKSPACE = $repositoryRoot
$env:EDK_TOOLS_PATH = Join-Path $edk2Root 'BaseTools'
$env:PACKAGES_PATH = $edk2Root
Push-Location $repositoryRoot
try {
    & $buildCommand '-p' $dsc '-a' 'X64' '-t' $ToolChain '-b' $BuildTarget
    if ($LASTEXITCODE -ne 0) {
        throw "EDK II build failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
