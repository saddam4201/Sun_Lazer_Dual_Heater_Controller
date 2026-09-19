@echo off
setlocal enabledelayedexpansion
title Sun Lazer - ESP32 Compile and Flash
cd /d "%~dp0"

echo =====================================================================
echo   Sun Lazer Dual Heater Controller - Compile ^& Flash Tool
echo =====================================================================
echo.

:: 1. Locate PlatformIO executable
set PIO_CMD=
where pio >nul 2>nul
if !ERRORLEVEL! EQU 0 (
    set PIO_CMD=pio
    goto :found_pio
)

where platformio >nul 2>nul
if !ERRORLEVEL! EQU 0 (
    set PIO_CMD=platformio
    goto :found_pio
)

if exist "%USERPROFILE%\.platformio\penv\Scripts\platformio.exe" (
    set "PIO_CMD=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
    goto :found_pio
)

if exist "%USERPROFILE%\.platformio\penv\Scripts\pio.exe" (
    set "PIO_CMD=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"
    goto :found_pio
)

for /d %%D in ("%LOCALAPPDATA%\Programs\Python\Python*") do (
    if exist "%%D\Scripts\platformio.exe" (
        set "PIO_CMD=%%D\Scripts\platformio.exe"
        goto :found_pio
    )
)

echo [ERROR] PlatformIO was not found on your system!
echo Please ensure PlatformIO is installed via VS Code or Python.
echo Checked locations:
echo   - System PATH (pio / platformio)
echo   - %USERPROFILE%\.platformio\penv\Scripts\platformio.exe
echo.
pause
exit /b 1

:found_pio
echo [INFO] Using PlatformIO: "!PIO_CMD!"
echo.
echo ---------------------------------------------------------------------
echo [ACTION] Compiling and Flashing to ESP32...
echo ---------------------------------------------------------------------
echo.

:: 2. Directly compile and upload to ESP32
"!PIO_CMD!" run --target upload

if !ERRORLEVEL! EQU 0 (
    echo.
    echo =====================================================================
    echo [SUCCESS] Firmware compiled and flashed successfully!
    echo =====================================================================
) else (
    echo.
    echo =====================================================================
    echo [ERROR] Flashing failed! Check USB connection and COM port.
    echo =====================================================================
)

echo.
pause
