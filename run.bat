@echo off
rem Starts Mothion, building it first if the sources changed (see scripts\run-windows.ps1).
rem Options are passed on to the build, e.g.:  run.bat -SkipTests
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run-windows.ps1" %*
if errorlevel 1 (
    echo.
    echo Could not start Mothion: see the messages above.
    pause
    exit /b 1
)
