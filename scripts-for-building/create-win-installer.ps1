# FTorrent Windows Installer Script
# This script generates the NSIS (EXE) installer for Windows.

$ProjectRoot = Resolve-Path "$PSScriptRoot\.."
$BuildDir = "$ProjectRoot\build_windows"

Write-Host "--- FTorrent Windows Installer Generation ---" -ForegroundColor Cyan

# Ensure build exists
if (-not (Test-Path "$BuildDir\CMakeCache.txt")) {
    Write-Host "Project not configured. Running build-win.ps1 first..." -ForegroundColor Yellow
    & "$PSScriptRoot\build-win.ps1"
}

Set-Location $BuildDir

Write-Host "Generating installer (NSIS)..." -ForegroundColor Yellow
cpack -G "NSIS;ZIP"

Write-Host "`nInstaller generated in: $BuildDir" -ForegroundColor Green
