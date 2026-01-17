# FLTorrent Build Script
# Este script automatiza la compilación del proyecto

param(
    [string]$VcpkgRoot = "C:\vcpkg",
    [string]$BuildType = "Release"
)

Write-Host "=== FLTorrent Build Script ===" -ForegroundColor Cyan
Write-Host ""

# Verificar que cl.exe y cmake están disponibles
if (-not (Get-Command cl -ErrorAction SilentlyContinue) -or -not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "Compilador o CMake no detectados. Intentando configurar entorno de VS..." -ForegroundColor Yellow
    if (Test-Path ".\setup-dev-env.ps1") {
        . .\setup-dev-env.ps1
    }
}

# Verificar que vcpkg existe
if (-not (Test-Path $VcpkgRoot)) {
    Write-Host "ERROR: vcpkg no encontrado en $VcpkgRoot" -ForegroundColor Red
    Write-Host "Por favor, instala vcpkg primero o especifica la ruta correcta con -VcpkgRoot" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Para instalar vcpkg:" -ForegroundColor Yellow
    Write-Host "  cd C:\" -ForegroundColor Gray
    Write-Host "  git clone https://github.com/microsoft/vcpkg.git" -ForegroundColor Gray
    Write-Host "  cd vcpkg" -ForegroundColor Gray
    Write-Host "  .\bootstrap-vcpkg.bat" -ForegroundColor Gray
    exit 1
}

$ToolchainFile = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"

if (-not (Test-Path $ToolchainFile)) {
    Write-Host "ERROR: vcpkg toolchain no encontrado en $ToolchainFile" -ForegroundColor Red
    exit 1
}

Write-Host "Usando vcpkg desde: $VcpkgRoot" -ForegroundColor Green
Write-Host "Tipo de compilación: $BuildType" -ForegroundColor Green
Write-Host ""

# Crear directorio build
$BuildDir = "build"
if (-not (Test-Path $BuildDir)) {
    Write-Host "Creando directorio build..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# Configurar con CMake
Write-Host "Configurando proyecto con CMake..." -ForegroundColor Yellow
Push-Location $BuildDir

cmake .. -DCMAKE_TOOLCHAIN_FILE=$ToolchainFile -DCMAKE_BUILD_TYPE=$BuildType

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "ERROR: Configuración de CMake falló" -ForegroundColor Red
    Write-Host ""
    Write-Host "Posibles soluciones:" -ForegroundColor Yellow
    Write-Host "1. Verifica que las dependencias estén instaladas:" -ForegroundColor Gray
    Write-Host "   vcpkg install fltk:x64-windows libtorrent:x64-windows" -ForegroundColor Gray
    Write-Host "2. Ejecuta vcpkg integrate install" -ForegroundColor Gray
    Pop-Location
    exit 1
}

# Compilar
Write-Host ""
Write-Host "Compilando FLTorrent..." -ForegroundColor Yellow
cmake --build . --config $BuildType

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "ERROR: Compilación falló" -ForegroundColor Red
    Pop-Location
    exit 1
}

Pop-Location

Write-Host ""
Write-Host "=== Compilación exitosa ===" -ForegroundColor Green
Write-Host ""
Write-Host "Ejecutable generado en: build\$BuildType\FLTorrent.exe" -ForegroundColor Cyan
Write-Host ""
Write-Host "Para ejecutar:" -ForegroundColor Yellow
Write-Host "  .\build\$BuildType\FLTorrent.exe" -ForegroundColor Gray
