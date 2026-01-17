# Script para configurar el entorno de desarrollo en la sesion actual de PowerShell
# Busca Visual Studio/Build Tools e importa el Developer Shell

$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Host "Error: No se encontro vswhere.exe. Asegurate de tener Visual Studio o Build Tools instalado." -ForegroundColor Red
    return
}

$vsPath = & $vswhere -latest -products * -property installationPath
if (-not $vsPath) {
    Write-Host "Error: No se encontro una instalacion de Visual Studio o Build Tools." -ForegroundColor Red
    return
}

Write-Host "Configurando entorno desde: $vsPath" -ForegroundColor Cyan

$devShellPath = Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
if (-not (Test-Path $devShellPath)) {
    Write-Host "Error: No se encontro Microsoft.VisualStudio.DevShell.dll" -ForegroundColor Red
    return
}

Import-Module $devShellPath
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevShellIndex 1 -Arch amd64

# Intentar anadir CMake de VS al PATH de esta sesion si no esta
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $vsCmakePath = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    if (Test-Path $vsCmakePath) {
        $env:Path = "$vsCmakePath;" + $env:Path
        Write-Host "CMake detectado en VS y aniadido al PATH temporalmente." -ForegroundColor Green
    }
}

Write-Host "OK: Entorno configurado. 'cl' y 'cmake' ahora estan disponibles en esta terminal." -ForegroundColor Green
