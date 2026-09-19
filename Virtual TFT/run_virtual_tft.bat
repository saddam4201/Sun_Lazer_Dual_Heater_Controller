@echo off
setlocal enabledelayedexpansion
title Sun Lazer - Virtual 2.8" TFT Display Launcher
cd /d "%~dp0"

echo =====================================================================
echo   Sun Lazer Dual Heater Controller - Virtual 2.8" TFT (ILI9341 UART)
echo =====================================================================
echo.
echo Controls and Hotkeys:
echo   [1] or [Up Arrow]    : UP (Navigate menu / jog up)
echo   [2] or [Down Arrow]  : DOWN (Navigate menu / jog down)
echo   [3] or [Left Arrow]  : LEFT (Back / cancel)
echo   [4] or [Right Arrow] : RIGHT (Next / select)
echo   [5] or [Enter/Space] : OK (Confirm / start process)
echo.

:: Detect Python command
set PYTHON_CMD=
where python >nul 2>nul
if !ERRORLEVEL! EQU 0 (
    set PYTHON_CMD=python
) else (
    where py >nul 2>nul
    if !ERRORLEVEL! EQU 0 (
        set PYTHON_CMD=py -3
    )
)

if "%PYTHON_CMD%"=="" (
    echo [ERROR] Python not found on system PATH.
    echo Please install Python 3 from https://www.python.org/
    echo Make sure to check "Add Python to PATH" during installation.
    echo.
    pause
    exit /b 1
)

:: Check for pyserial dependency
%PYTHON_CMD% -c "import serial" >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [WARNING] 'pyserial' package not found. Installing via pip...
    %PYTHON_CMD% -m pip install pyserial
    echo.
)

:: Launch the Virtual TFT app
echo [INFO] Starting Virtual TFT Application...
%PYTHON_CMD% tft_display_app.py

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Application exited with error code %ERRORLEVEL%.
    pause
)
