@echo off
setlocal enabledelayedexpansion
title TFT Display Launcher
cd /d "%~dp0"

echo =====================================================================
echo   Starting Virtual 2.8" TFT Display (ILI9341 UART)
echo =====================================================================
echo.
echo Controls and Hotkeys:
echo   [v]  : Toggle between Original ST7920 Screen and Modern Dashboard
echo   [m]  : Cycle Color Themes (Blue, Amber, OLED, Green, etc.)
echo   [f]  : Toggle Font Style (Modern Vector vs Retro 5x7)
echo   [c]  : Clear Display Canvas
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
    if exist "dist\Virtual_TFT_Display.exe" (
        echo [INFO] Python not found. Launching standalone Virtual_TFT_Display.exe...
        start "" "dist\Virtual_TFT_Display.exe"
        exit /b 0
    )
    echo [ERROR] Neither Python nor standalone executable found.
    echo Please install Python 3 from https://www.python.org/
    echo or ensure dist\Virtual_TFT_Display.exe exists.
    echo.
    pause
    exit /b 1
)

:: Check for pyserial dependency
%PYTHON_CMD% -c "import serial" >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [WARNING] 'pyserial' package not found. Installing...
    %PYTHON_CMD% -m pip install pyserial
    echo.
)

:: Launch the Virtual TFT app
%PYTHON_CMD% tft_display_app.py

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Application exited with error code %ERRORLEVEL%.
    pause
)
