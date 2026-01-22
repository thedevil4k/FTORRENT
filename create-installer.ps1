# FTorrent Installer Creation Script (One-Click)
# Requires: CMake, vcpkg, NSIS (for .exe installer)

$ErrorActionPreference = "Stop"

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "   FTorrent - Installer Creation Tool     " -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

# 1. Environment Check
$VCPKG_ROOT = "C:\vcpkg"
if (-not (Test-Path $VCPKG_ROOT)) {
    $VCPKG_ROOT = Read-Host "vcpkg not found at C:\vcpkg. Please enter vcpkg path"
}

# Try to find CMake if not in path
if (-not (Get-Command "cmake" -ErrorAction SilentlyContinue)) {
    Write-Host "[INFO] CMake not found in PATH. Searching in Visual Studio..." -ForegroundColor Gray
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -property installationPath
        $vsCMake = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path $vsCMake) {
            $env:PATH = "$(Split-Path $vsCMake);$env:PATH"
            Write-Host "[INFO] Found CMake in Visual Studio: $vsCMake" -ForegroundColor Gray
        }
        else {
            Write-Host "[ERROR] CMake not found in Visual Studio Path: $vsPath" -ForegroundColor Red
            exit 1
        }
    }
    else {
        Write-Host "[ERROR] Neither CMake nor Visual Studio Installer found." -ForegroundColor Red
        exit 1
    }
}

# Check for NSIS (needed for CPack EXE)
if (-not (Get-Command "makensis" -ErrorAction SilentlyContinue)) {
    Write-Host "[WARNING] NSIS (makensis.exe) not found in PATH." -ForegroundColor Yellow
    Write-Host "You can still create a ZIP package, but the .exe installer will fail." -ForegroundColor Yellow
    Write-Host "Download NSIS from: https://nsis.sourceforge.io/" -ForegroundColor Cyan
}

# 2. Configuration
$BuildDir = "out-installer"
$BuildType = "Release"
$Architecture = "x64-windows" # Change to x86-windows for 32-bit

Write-Host "`n[1/4] Cleaning previous builds..." -ForegroundColor Gray
if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
New-Item -ItemType Directory -Path $BuildDir | Out-Null

# 3. Configure
Write-Host "[2/4] Configuring project ($Architecture)..." -ForegroundColor Gray
Set-Location $BuildDir
# Ensure variables are expanded correctly by quoting them
cmake -DCMAKE_BUILD_TYPE="$BuildType" `
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET="$Architecture" ..

# Check if configuration succeeded
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] CMake configuration failed." -ForegroundColor Red
    Set-Location ..
    exit 1
}

# 4. Build
Write-Host "[3/4] Compiling FTorrent..." -ForegroundColor Gray
cmake --build . --config "$BuildType" --parallel

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Build failed." -ForegroundColor Red
    Set-Location ..
    exit 1
}

# 5. Pack
Write-Host "[4/4] Generating Installer and Portable ZIP..." -ForegroundColor Gray
# Use CPack to generate the files defined in CMakeLists.txt
cpack -C "$BuildType"

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] CPack failed. Make sure NSIS is installed for the .exe installer." -ForegroundColor Red
    Set-Location ..
    exit 1
}

# 6. Finalize
Write-Host "`n==========================================" -ForegroundColor Green
Write-Host "         BUILD SUCCESSFUL!                " -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Green

$OutputPath = Get-ChildItem -Path "." -Include "FTorrent-*.exe", "FTorrent-*.zip"
Write-Host "Created packages:" -ForegroundColor White
$OutputPath | ForEach-Object { Write-Host " - $($_.Name)" -ForegroundColor Cyan }

Write-Host "`nOpening output folder..." -ForegroundColor Gray
explorer .
Set-Location ..

Write-Host "`nDone! Press any key to exit."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
