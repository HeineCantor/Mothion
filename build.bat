@echo off
rem Installs what is missing and builds Mothion (see scripts\build-windows.ps1).
rem Options are passed on, e.g.:  build.bat -Run   build.bat -SkipTests -DevTools
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-windows.ps1" %*
if errorlevel 1 (
    echo.
    echo Build failed: see the messages above.
    pause
    exit /b 1
)
