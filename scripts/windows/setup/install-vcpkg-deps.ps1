# Script para instalar dependencias de FTorrent usando vcpkg

param(
    [string]$VcpkgRoot = "C:\vcpkg"
)

Write-Host "=== Instalador de Dependencias FTorrent ===" -ForegroundColor Cyan
Write-Host ""

# Verificar si vcpkg existe
if (-not (Test-Path $VcpkgRoot)) {
    Write-Host "vcpkg no encontrado en $VcpkgRoot. Instalando automáticamente..." -ForegroundColor Yellow
    
    Write-Host ""
    Write-Host "Clonando vcpkg..." -ForegroundColor Yellow
    
    $parentDir = Split-Path $VcpkgRoot -Parent
    if (-not (Test-Path $parentDir)) {
        New-Item -ItemType Directory -Force -Path $parentDir | Out-Null
    }
    
    Push-Location $parentDir
    
    git clone https://github.com/microsoft/vcpkg.git
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: No se pudo clonar vcpkg" -ForegroundColor Red
        Pop-Location
        exit 1
    }
    
    Push-Location "vcpkg"
    
    Write-Host "Bootstrapping vcpkg..." -ForegroundColor Yellow
    .\bootstrap-vcpkg.bat
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Bootstrap falló" -ForegroundColor Red
        Pop-Location
        Pop-Location
        exit 1
    }
    
    Pop-Location
    Pop-Location
    
    Write-Host "vcpkg instalado correctamente!" -ForegroundColor Green
}

$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"

if (-not (Test-Path $VcpkgExe)) {
    Write-Host "ERROR: vcpkg.exe no encontrado" -ForegroundColor Red
    exit 1
}

Write-Host "Usando vcpkg desde: $VcpkgRoot" -ForegroundColor Green
Write-Host ""

# Integrar vcpkg con el sistema
Write-Host "Integrando vcpkg con Visual Studio..." -ForegroundColor Yellow
& $VcpkgExe integrate install

Write-Host ""
Write-Host "Instalando dependencias (esto puede tardar varios minutos)..." -ForegroundColor Yellow
Write-Host ""

# Instalar FLTK
Write-Host "[1/2] Instalando FLTK..." -ForegroundColor Cyan
& $VcpkgExe install fltk:x64-windows

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Instalación de FLTK falló" -ForegroundColor Red
    exit 1
}

Write-Host "FLTK instalado correctamente!" -ForegroundColor Green
Write-Host ""

# Instalar libtorrent
Write-Host "[2/2] Instalando libtorrent-rasterbar..." -ForegroundColor Cyan
Write-Host "(Esto puede tardar 10-20 minutos dependiendo de tu sistema)" -ForegroundColor Yellow
& $VcpkgExe install libtorrent:x64-windows

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Instalación de libtorrent falló" -ForegroundColor Red
    exit 1
}

Write-Host "libtorrent-rasterbar instalado correctamente!" -ForegroundColor Green
Write-Host ""

# Verificar instalación
Write-Host "=== Verificando instalación ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "Paquetes instalados:" -ForegroundColor Yellow
& $VcpkgExe list | Select-String -Pattern "(fltk|libtorrent)"

Write-Host ""
Write-Host "=== Instalación completada ===" -ForegroundColor Green
Write-Host ""
Write-Host "Ahora puedes compilar el proyecto ejecutando:" -ForegroundColor Yellow
Write-Host "  .\build.ps1" -ForegroundColor Gray
Write-Host ""
Write-Host "O manualmente:" -ForegroundColor Yellow
Write-Host "  mkdir build" -ForegroundColor Gray
Write-Host "  cd build" -ForegroundColor Gray
Write-Host "  cmake .. -DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot\scripts\buildsystems\vcpkg.cmake" -ForegroundColor Gray
Write-Host "  cmake --build . --config Release" -ForegroundColor Gray
