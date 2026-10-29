[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64',
    [string]$PlatformToolset = 'v143',
    [string]$VcpkgInstalledDir = '',
    [string]$VcpkgTriplet = 'x64-windows-static'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$msbuildCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe'),
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
)
$msbuild = $msbuildCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $msbuild) {
    throw 'MSBuild x64 не найден.'
}

$common = @(
    '/m',
    "/p:PlatformToolset=$PlatformToolset",
    '/p:WindowsTargetPlatformVersion=10.0.19041.0',
    '/v:minimal'
)
if ($VcpkgInstalledDir) {
    $common += "/p:VcpkgInstalledDir=$VcpkgInstalledDir"
    $common += "/p:VcpkgTriplet=$VcpkgTriplet"
}

$skinProjects = Get-ChildItem (Join-Path $PSScriptRoot 'Skins') -Recurse -Filter '*.vcxproj' | Sort-Object FullName
foreach ($skin in $skinProjects) {
    & $msbuild $skin.FullName '/p:Configuration=Resource' '/p:Platform=Win32' @common
    if ($LASTEXITCODE -ne 0) {
        throw "Сборка скина завершилась ошибкой: $($skin.Name)"
    }
}

$componentProjects = @(
    (Join-Path $PSScriptRoot 'Lib\Lib.vcxproj'),
    (Join-Path $PSScriptRoot 'StartMenu\StartMenuDLL\StartMenuDLL.vcxproj'),
    (Join-Path $PSScriptRoot 'StartMenu\StartMenuHelper\StartMenuHelper.vcxproj')
)
foreach ($component in $componentProjects) {
    & $msbuild $component "/p:Configuration=$Configuration" "/p:Platform=$Platform" @common
    if ($LASTEXITCODE -ne 0) {
        throw "Сборка компонента завершилась ошибкой: $component"
    }
}

$startMenu = Join-Path $PSScriptRoot 'StartMenu\StartMenu.vcxproj'
& $msbuild $startMenu "/p:Configuration=$Configuration" "/p:Platform=$Platform" '/p:BuildProjectReferences=false' @common
if ($LASTEXITCODE -ne 0) {
    throw 'Сборка встроенного OpenGlass Start Menu завершилась ошибкой.'
}

$output = Join-Path $repoRoot "build\bin\$Configuration`64"
Write-Host "Готово. Бинарные файлы: $output"
