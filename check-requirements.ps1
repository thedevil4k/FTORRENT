# Script para verificar requisitos de FTorrent

Write-Host "=== Verificación de Requisitos FTorrent ===" -ForegroundColor Cyan
Write-Host ""

$AllOk = $true

# 1. Verificar vcpkg
Write-Host "[1/4] Verificando vcpkg..." -NoNewline
if (Test-Path "C:\vcpkg\vcpkg.exe") {
    Write-Host " OK (Encontrado en C:\vcpkg)" -ForegroundColor Green
}
else {
    Write-Host " NO ENCONTRADO" -ForegroundColor Red
    Write-Host "      Por favor, asegúrate de que vcpkg está instalado en C:\vcpkg" -ForegroundColor Gray
    $AllOk = $false
}

# Intentar encontrar Visual Studio Build Tools
$vsInstallPath = ""
if (Test-Path "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe") {
    $vsInstallPath = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
}

# 2. Verificar CMake
Write-Host "[2/4] Verificando CMake..." -NoNewline
$cmake = where.exe cmake 2>$null
if ($cmake) {
    $version = cmake --version | Select-Object -First 1
    Write-Host " OK ($version)" -ForegroundColor Green
}
else {
    # Buscar en rutas de Visual Studio
    $vsCmake = Join-Path $vsInstallPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if ($vsInstallPath -and (Test-Path $vsCmake)) {
        Write-Host " OK (Encontrado en VS: $vsInstallPath)" -ForegroundColor Green
        # Sugerir añadir al PATH
        Write-Host "      Nota: CMake se encontró en la carpeta de VS pero no está en tu PATH global." -ForegroundColor Gray
    }
    else {
        Write-Host " NO ENCONTRADO" -ForegroundColor Yellow
        Write-Host "      CMake no está en el PATH ni en la ruta estándar de VS." -ForegroundColor Gray
        $AllOk = $false
    }
}

# 3. Verificar Compilador (cl.exe)
Write-Host "[3/4] Verificando compilador (cl.exe)..." -NoNewline
$cl = where.exe cl 2>$null
if ($cl) {
    Write-Host " OK" -ForegroundColor Green
}
else {
    if ($vsInstallPath) {
        Write-Host " INSTALADO PERO NO EN EL PATH" -ForegroundColor Yellow
        Write-Host "      Se detectó Visual Studio en: $vsInstallPath" -ForegroundColor Gray
        Write-Host "      Para que funcione, debes abrir 'Developer PowerShell for VS 2022' desde el menú Inicio." -ForegroundColor Cyan
    }
    else {
        Write-Host " NO ENCONTRADO" -ForegroundColor Red
        Write-Host "      No se detectó Visual Studio Build Tools o Community." -ForegroundColor Gray
        $AllOk = $false
    }
}

# 4. Verificar Librerías en vcpkg
if (Test-Path "C:\vcpkg\vcpkg.exe") {
    Write-Host "[4/4] Verificando dependencias en vcpkg..." -ForegroundColor Cyan
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
else {
    Write-Host "[4/4] No se puede verificar dependencias sin vcpkg." -ForegroundColor Red
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
