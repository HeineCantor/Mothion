<#
.SYNOPSIS
    Installs what Mothion needs on Windows, when missing, and builds it.

.DESCRIPTION
    Checks for, and installs when missing:
      - CMake (winget)
      - Visual Studio 2022 with the C++ tools, or the Build Tools with the
        C++ workload (winget; the installer asks for administrator rights)
      - Qt for MSVC 2022 64-bit with its Multimedia module (the audio track),
        downloaded with aqtinstall (which needs Python, also installed with
        winget if missing)
      - ffmpeg (winget; File > Export video needs it, and a failure here only
        warns)
    Then configures and builds the project, runs the tests and copies Qt's
    DLLs next to mothion.exe, so the program starts on its own.

.EXAMPLE
    .\scripts\build-windows.ps1
.EXAMPLE
    .\scripts\build-windows.ps1 -Run -SkipTests
#>
param(
    [string]$QtVersion = '6.10.2',
    [string]$QtRoot = 'C:\Qt',
    [ValidateSet('RelWithDebInfo', 'Release', 'Debug')]
    [string]$Config = 'RelWithDebInfo',
    [switch]$SkipTests,  # build only
    [switch]$DevTools,   # also build mothion_brushsheet
    [switch]$Run         # start Mothion at the end
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repo 'build'
$qtDir = Join-Path $QtRoot "$QtVersion\msvc2022_64"
$qtModules = @('qtmultimedia') # besides the base: what aqtinstall is asked for

function Step([string]$message) { Write-Host "==> $message" -ForegroundColor Cyan }

function Invoke-Checked([string]$what, [scriptblock]$command) {
    & $command
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE)." }
}

# Programs installed in this session are only on the PATH of new shells.
function Update-Path {
    $env:Path = @(
        [Environment]::GetEnvironmentVariable('Path', 'Machine'),
        [Environment]::GetEnvironmentVariable('Path', 'User'),
        (Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links'),
        (Join-Path $env:ProgramFiles 'CMake\bin')
    ) -join ';'
}

function Install-WithWinget([string]$id, [string[]]$extra = @()) {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw "winget not found: install 'App Installer' from the Microsoft Store, then run this script again."
    }
    & winget install --id $id --exact --accept-package-agreements --accept-source-agreements @extra
    Update-Path
}

# A real Python, not the Microsoft Store shortcut that only opens the Store.
function Find-Python {
    foreach ($candidate in @('py', 'python')) {
        if (Get-Command $candidate -ErrorAction SilentlyContinue) {
            try {
                & $candidate -c 'import sys' 2>$null
                if ($LASTEXITCODE -eq 0) { return $candidate }
            } catch { } # the Store shortcut writes to stderr and fails
        }
    }
    return $null
}

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $query = @('-products', '*', '-version', '[17.0,18.0)',
               '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath')
    # Prefer a release installation; fall back to a preview one.
    $path = & $vswhere @query | Select-Object -First 1
    if (-not $path) { $path = & $vswhere @query -prerelease | Select-Object -First 1 }
    return $path
}

Update-Path

# --- Mothion must not be running: Windows does not let the build replace it ---
$running = Get-Process mothion -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and $_.Path.StartsWith($buildDir, [StringComparison]::OrdinalIgnoreCase) }
if ($running) { throw 'Close Mothion first: the build cannot replace a running mothion.exe.' }

# --- CMake ---
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Step 'Installing CMake'
    Install-WithWinget 'Kitware.CMake'
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw 'CMake is still not available; open a new terminal and try again.' }
}

# --- ffmpeg (Export video) ---
if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    Step 'Installing ffmpeg (for Export video)'
    try { Install-WithWinget 'Gyan.FFmpeg' } catch { Write-Warning $_.Exception.Message }
    if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
        Write-Warning 'ffmpeg is not available yet: Export video will work once it is on the PATH (a new terminal may be needed).'
    }
}

# --- Visual Studio C++ tools ---
$vs = Find-VisualStudio
if (-not $vs) {
    Step 'Installing Visual Studio 2022 Build Tools with the C++ workload (this takes a while)'
    Install-WithWinget 'Microsoft.VisualStudio.2022.BuildTools' @(
        '--override', '--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
    $vs = Find-VisualStudio
    if (-not $vs) { throw 'The Visual Studio C++ tools are still missing; check the Visual Studio Installer.' }
}
Step "C++ tools: $vs"

# --- Qt ---
# The base and each module: a Qt installed before the audio track lacks its Multimedia, and gets it now.
$qtMissing = -not (Test-Path (Join-Path $qtDir 'lib\cmake\Qt6\Qt6Config.cmake')) -or
    -not (Test-Path (Join-Path $qtDir 'lib\cmake\Qt6Multimedia\Qt6MultimediaConfig.cmake'))
if ($qtMissing) {
    Step "Installing Qt $QtVersion for MSVC 2022 64-bit ($($qtModules -join ', ')) into $QtRoot"
    $python = Find-Python
    if (-not $python) {
        Install-WithWinget 'Python.Python.3.12'
        $python = Find-Python
        if (-not $python) { throw 'Python is still not available; open a new terminal and try again.' }
    }
    Invoke-Checked 'Installing aqtinstall' { & $python -m pip install --user --upgrade --quiet aqtinstall }
    Invoke-Checked 'Downloading Qt' {
        & $python -m aqt install-qt windows desktop $QtVersion win64_msvc2022_64 -m @qtModules --outputdir $QtRoot
    }
}
Step "Qt: $qtDir"
$env:Path = "$qtDir\bin;$env:Path" # Qt's DLLs for the tests

# --- Configure and build ---
$cmakeArgs = @('-S', $repo, '-B', $buildDir, "-DCMAKE_PREFIX_PATH=$qtDir",
               "-DMOTHION_BUILD_DEVTOOLS=$(if ($DevTools) { 'ON' } else { 'OFF' })")
if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    # First configure: pick the generator and the Visual Studio installation found above.
    $cmakeArgs += @('-G', 'Visual Studio 17 2022', '-A', 'x64', "-DCMAKE_GENERATOR_INSTANCE=$vs")
}
Step 'Configuring'
Invoke-Checked 'Configuring' { & cmake @cmakeArgs }
Step "Building ($Config)"
Invoke-Checked 'Building' { & cmake --build $buildDir --config $Config --parallel }

if (-not $SkipTests) {
    Step 'Running the tests'
    Invoke-Checked 'Tests' { & ctest --test-dir $buildDir -C $Config --output-on-failure }
}

# --- Qt's DLLs next to the executable ---
$exe = Join-Path $buildDir "$Config\mothion.exe"
Step 'Copying the Qt DLLs next to mothion.exe'
$env:VCINSTALLDIR = Join-Path $vs 'VC\' # lets windeployqt add the C++ runtime installer
Invoke-Checked 'windeployqt' {
    & (Join-Path $qtDir 'bin\windeployqt.exe') --no-translations --no-system-dxc-compiler --no-system-d3d-compiler `
        --verbose 0 $exe
}

Write-Host ''
Write-Host "Mothion is ready: $exe" -ForegroundColor Green
if ($Run) { Start-Process $exe }
