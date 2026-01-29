@echo off
:: FTorrent Windows Setup Launcher
:: Este archivo permite ejecutar el setup de PowerShell saltandose la restriccion de Execution Policy.

echo --- FTorrent Windows Setup Launcher ---
echo Cargando entorno de PowerShell...

set SCRIPT_DIR=%~dp0
set PS_SCRIPT=%SCRIPT_DIR%setup-windows.ps1

if not exist "%PS_SCRIPT%" (
    echo ERROR: No se encontro %PS_SCRIPT%
    pause
    exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%"

if %ERRORLEVEL% neq 0 (
    echo.
    echo Eror: El setup de FTorrent ha fallado.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Proceso finalizado correctamente.
pause
