# FTorrent Windows Build Script
# This script compiles the project using CMake and Visual Studio.

$ProjectRoot = Resolve-Path "$PSScriptRoot\.."
$BuildDir = "$ProjectRoot\build_windows"

Write-Host "--- FTorrent Windows Build ---" -ForegroundColor Cyan

if (-not (Test-Path $BuildDir)) { 
    New-Item -ItemType Directory -Path $BuildDir 
}

Set-Location $BuildDir

Write-Host "Configuring project..." -ForegroundColor Yellow
cmake .. -DCMAKE_BUILD_TYPE=Release

Write-Host "Starting build..." -ForegroundColor Yellow
cmake --build . --config Release -j $env:NUMBER_OF_PROCESSORS

Write-Host "`nBuild finished. Executable is in: $BuildDir\bin" -ForegroundColor Green
