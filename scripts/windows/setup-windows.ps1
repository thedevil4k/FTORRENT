# FTorrent Unified Windows Setup Script
# Coordina la instalación de vcpkg, dependencias y NSIS.

$ScriptDir = Split-Path $MyInvocation.MyCommand.Path -Parent
$SetupDir = Join-Path $ScriptDir "setup"

Write-Host "=== FTorrent: Setup Automático de Windows ===" -ForegroundColor Cyan
Write-Host ""

# 1. Comprobar Git (fundamental para vcpkg)
Write-Host "[1/5] Verificando Git..." -NoNewline
if (Get-Command git -ErrorAction SilentlyContinue) {
    Write-Host " OK" -ForegroundColor Green
}
else {
    Write-Host " NO ENCONTRADO" -ForegroundColor Red
    Write-Host "ERROR: Git es necesario para descargar dependencias. Instálalo desde https://git-scm.com/" -ForegroundColor Yellow
    exit 1
}

# 2. Configurar Entorno de Visual Studio
Write-Host "[2/5] Configurando compilador MSVC..." -NoNewline
. (Join-Path $SetupDir "setup-dev-env.ps1") | Out-Null
if (Get-Command cl -ErrorAction SilentlyContinue) {
    Write-Host " OK" -ForegroundColor Green
}
else {
    Write-Host " FALLÓ" -ForegroundColor Red
    Write-Host "ERROR: No se pudo activar el entorno de Visual Studio. Asegúrate de tenerlo instalado." -ForegroundColor Yellow
    exit 1
}

# 3. Instalar vcpkg y Dependencias (FLTK, libtorrent)
Write-Host "[3/5] Instalando vcpkg y librerías..." -ForegroundColor Cyan
& (Join-Path $SetupDir "install-vcpkg-deps.ps1") -VcpkgRoot "C:\vcpkg" 

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: La instalación de dependencias falló." -ForegroundColor Red
    exit 1
}

# 4. Verificar o instalar NSIS (para el instalador de Windows)
Write-Host "[4/5] Verificando NSIS (Necesario para el instalador)..." -NoNewline
$nsisFound = $false
if (Get-Command makensis -ErrorAction SilentlyContinue) {
    $nsisFound = $true
}
elseif (Test-Path "${env:ProgramFiles(x86)}\NSIS\makensis.exe") {
    $env:Path += ";${env:ProgramFiles(x86)}\NSIS"
    $nsisFound = $true
}

if ($nsisFound) {
    Write-Host " OK" -ForegroundColor Green
}
else {
    Write-Host " NO ENCONTRADO" -ForegroundColor Yellow
    Write-Host "Descargando e instalando NSIS de forma automática..." -ForegroundColor Cyan
    
    $nsisUrl = "https://sourceforge.net/projects/nsis/files/NSIS%203/3.10/nsis-3.10-setup.exe/download"
    $tempNsis = Join-Path $env:TEMP "nsis-setup.exe"
    
    Invoke-WebRequest -Uri $nsisUrl -OutFile $tempNsis
    Write-Host "Iniciando instalación silenciosa de NSIS... Espera un momento." -ForegroundColor Cyan
    Start-Process -FilePath $tempNsis -ArgumentList "/S" -Wait
    
    if (Test-Path "${env:ProgramFiles(x86)}\NSIS\makensis.exe") {
        $env:Path += ";${env:ProgramFiles(x86)}\NSIS"
        Write-Host " NSIS instalado correctamente." -ForegroundColor Green
    }
    else {
        Write-Host " FALLÓ la instalación automática. Por favor instala NSIS manualmente desde https://nsis.sourceforge.io/" -ForegroundColor Red
    }
}

# 5. Verificación Final
Write-Host ""
& (Join-Path $SetupDir "check-requirements.ps1")

Write-Host "=== Setup finalizado ===" -ForegroundColor Green
Write-Host "Ya puedes compilar el proyecto o crear el instalador:" -ForegroundColor White
Write-Host "  Para compilar: .\scripts\windows\compilation\build-windows.ps1" -ForegroundColor Gray
Write-Host "  Para el instalador: .\scripts\windows\installers\create-win-installer.ps1" -ForegroundColor Gray
Write-Host ""
