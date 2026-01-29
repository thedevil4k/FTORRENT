# Script para verificar requisitos de FTorrent

Write-Host "=== Verificación de Requisitos FTorrent ===" -ForegroundColor Cyan
Write-Host ""

$AllOk = $true

# 1. Verificar Git
Write-Host "[1/5] Verificando Git..." -NoNewline
if (Get-Command git -ErrorAction SilentlyContinue) {
    $gitVer = git --version
    Write-Host " OK ($gitVer)" -ForegroundColor Green
}
else {
    Write-Host " NO ENCONTRADO" -ForegroundColor Red
    $AllOk = $false
}

# 2. Verificar vcpkg
Write-Host "[2/5] Verificando vcpkg..." -NoNewline
if (Test-Path "C:\vcpkg\vcpkg.exe") {
    Write-Host " OK (Encontrado en C:\vcpkg)" -ForegroundColor Green
}
else {
    Write-Host " NO ENCONTRADO" -ForegroundColor Red
    $AllOk = $false
}

# 3. Verificar CMake
Write-Host "[3/5] Verificando CMake..." -NoNewline
$cmake = where.exe cmake 2>$null
if ($cmake) {
    $version = cmake --version | Select-Object -First 1
    Write-Host " OK ($version)" -ForegroundColor Green
}
else {
    Write-Host " NO ENCONTRADO" -ForegroundColor Yellow
    $AllOk = $false
}

# 4. Verificar NSIS
Write-Host "[4/5] Verificando NSIS (makensis.exe)..." -NoNewline
if (Get-Command makensis -ErrorAction SilentlyContinue) {
    Write-Host " OK" -ForegroundColor Green
}
elseif (Test-Path "${env:ProgramFiles(x86)}\NSIS\makensis.exe") {
    Write-Host " OK (Encontrado en ruta estándar)" -ForegroundColor Green
}
else {
    Write-Host " NO ENCONTRADO" -ForegroundColor Yellow
    $AllOk = $false
}

# 5. Verificar Librerías en vcpkg
if (Test-Path "C:\vcpkg\vcpkg.exe") {
    Write-Host "[5/5] Verificando dependencias en vcpkg..." -ForegroundColor Cyan
    $packages = & C:\vcpkg\vcpkg.exe list
    
    $deps = @("fltk", "libtorrent")
    foreach ($dep in $deps) {
        Write-Host "      - $($dep): " -NoNewline
        if ($packages -match $dep) {
            Write-Host "INSTALADO" -ForegroundColor Green
        }
        else {
            Write-Host "FALTA" -ForegroundColor Red
            $AllOk = $false
        }
    }
}

Write-Host ""
if ($AllOk) {
    if (-not $cl) {
        Write-Host "--- ATENCION ---" -ForegroundColor Yellow
        Write-Host "Tienes las herramientas instaladas, pero NO activadas en esta terminal." -ForegroundColor Yellow
        Write-Host "Para activarlas, ejecuta: . .\setup-dev-env.ps1" -ForegroundColor Cyan
        Write-Host "O simplemente usa .\build.ps1, que lo hara por ti." -ForegroundColor Gray
    }
    else {
        Write-Host "OK: Todo esta listo para compilar." -ForegroundColor Green
    }
}
else {
    Write-Host "ERROR: Faltan algunos requisitos. Revisa los mensajes arriba." -ForegroundColor Red
    Write-Host "Si tienes VS Build Tools instalado, asegurate de incluir 'C++ Build Tools' y 'CMake'." -ForegroundColor Gray
}
Write-Host ""
