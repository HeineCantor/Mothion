<#
.SYNOPSIS
    Starts Mothion, building it first if the sources changed since the last build.

.DESCRIPTION
    Rebuilds with build-windows.ps1 (same options) when mothion.exe is missing
    or older than any file it is built from (src, resources, cmake, the
    CMakeLists), then starts it.

.EXAMPLE
    .\scripts\run-windows.ps1
.EXAMPLE
    .\scripts\run-windows.ps1 -SkipTests
#>
# The options of build-windows.ps1, passed on when a build is needed.
param(
    [string]$QtVersion = '6.10.2',
    [string]$QtRoot = 'C:\Qt',
    [ValidateSet('RelWithDebInfo', 'Release', 'Debug')]
    [string]$Config = 'RelWithDebInfo',
    [switch]$SkipTests,
    [switch]$DevTools
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo "build\$Config\mothion.exe"

function Test-UpToDate {
    if (-not (Test-Path $exe)) { return $false }
    $built = (Get-Item $exe).LastWriteTimeUtc
    $inputs = @('src', 'resources', 'cmake') | ForEach-Object { Join-Path $repo $_ } | Where-Object { Test-Path $_ } |
        ForEach-Object { Get-ChildItem $_ -Recurse -File }
    $inputs += Get-Item (Join-Path $repo 'CMakeLists.txt')
    $newer = $inputs | Where-Object { $_.LastWriteTimeUtc -gt $built } | Select-Object -First 1
    return -not $newer
}

if (Test-UpToDate) {
    Write-Host '==> Mothion is up to date' -ForegroundColor Cyan
} else {
    Write-Host '==> Sources changed since the last build: building' -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot 'build-windows.ps1') -QtVersion $QtVersion -QtRoot $QtRoot -Config $Config `
        -SkipTests:$SkipTests -DevTools:$DevTools
    # A change the build had nothing to do for leaves the exe's date behind: mark it as current.
    (Get-Item $exe).LastWriteTime = Get-Date
}
Start-Process $exe
