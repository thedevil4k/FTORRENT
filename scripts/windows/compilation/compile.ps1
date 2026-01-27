<#
.SYNOPSIS
    FTorrent Compilation Script for Windows (PowerShell version)
.DESCRIPTION
    Compiles FTorrent, copies necessary DLLs from vcpkg, and bundles assets.
.PARAMETER OutputDir
    The directory where the compiled files will be saved.
.PARAMETER VcpkgRoot
    The root directory of vcpkg installation.
#>

param (
    [string]$OutputDir = "D:\FTorrent-compilation",
    [string]$VcpkgRoot = "C:\vcpkg"
)

$ErrorActionPreference = "Stop"

Write-Host "`n====================================================================" -ForegroundColor Cyan
Write-Host "                FTorrent Compilation Script (PS1)" -ForegroundColor Cyan
Write-Host "====================================================================`n" -ForegroundColor Cyan

# Configuration
$SourceDir = Resolve-Path "$PSScriptRoot\..\..\.."
$VcpkgToolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"

Write-Host "Source Directory: $SourceDir"
Write-Host "Output Directory: $OutputDir"
Write-Host "vcpkg Root: $VcpkgRoot"
Write-Host ""

# Check if vcpkg exists
if (-not (Test-Path (Join-Path $VcpkgRoot "vcpkg.exe"))) {
    Write-Error "vcpkg not found at $VcpkgRoot. Please install vcpkg or update the path."
}

# Check if toolchain file exists
if (-not (Test-Path $VcpkgToolchain)) {
    Write-Error "vcpkg toolchain file not found at $VcpkgToolchain"
}

# Search for Visual Studio Environment if cl is not in path
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    Write-Host "[INFO] Compiler (cl) not found in PATH. Searching for Visual Studio..." -ForegroundColor Gray
    
    $VsWherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $VsPath = & $VsWherePath -latest -products * -property installationPath
    
    $VcvarsPath = Join-Path $VsPath "VC\Auxiliary\Build\vcvars64.bat"
    
    if (Test-Path $VcvarsPath) {
        Write-Host "[INFO] Activating VS environment..." -ForegroundColor Gray
        # We need to run vcvars and import the env variables into the current PS session
        $tempFile = [IO.Path]::GetTempFileName()
        cmd /c " `"$VcvarsPath`" && set > `"$tempFile`" "
        Get-Content $tempFile | Foreach-Object {
            if ($_ -match "^(.*?)=(.*)$") {
                Set-Item "env:$($matches[1])" $matches[2]
            }
        }
        Remove-Item $tempFile
    }
    else {
        Write-Error "Visual Studio compiler not found. Please install VS Build Tools with C++ components."
    }
}

# Check for CMake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    # Try to find it in VS if not in PATH
    $VsCmakeBin = Join-Path $VsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    $VsCmakePath = Join-Path $VsCmakeBin "cmake.exe"
    
    if (Test-Path $VsCmakePath) {
        $env:Path = "$VsCmakeBin;" + $env:Path
        Write-Host "[OK] CMake found in VS extensions." -ForegroundColor Green
    }
    else {
        Write-Error "CMake not found. Please install it or ensure it is part of your VS installation."
    }
}

# 1/6 Creating build directory
Write-Host "[1/6] Creating build directory..." -ForegroundColor Yellow
$BuildDir = Join-Path $SourceDir "build"
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}
Set-Location $BuildDir

# 2/6 Configuring with CMake
Write-Host "[2/6] Configuring with CMake..." -ForegroundColor Yellow
cmake .. -DCMAKE_TOOLCHAIN_FILE="$VcpkgToolchain" -DCMAKE_BUILD_TYPE=Release

# 3/6 Building
Write-Host "[3/6] Building FTorrent (Release)..." -ForegroundColor Yellow
cmake --build . --config Release

# 4/6 Creating output directory
Write-Host "[4/6] Creating output directory..." -ForegroundColor Yellow
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

# 5/6 Copying executable and dependencies
Write-Host "[5/6] Copying executable and dependencies..." -ForegroundColor Yellow
$ExeSource = Join-Path $BuildDir "Release\FTorrent.exe"

if (Test-Path $ExeSource) {
    Copy-Item -Path $ExeSource -Destination $OutputDir -Force
    Write-Host "  [OK] FTorrent.exe" -ForegroundColor Green
}
else {
    Write-Error "FTorrent.exe not found in $ExeSource!"
}

# Copy DLLs from vcpkg
$VcpkgBin = Join-Path $VcpkgRoot "installed\x64-windows\bin"
if (Test-Path $VcpkgBin) {
    Copy-Item -Path (Join-Path $VcpkgBin "*.dll") -Destination $OutputDir -Force
    Write-Host "  [OK] DLLs copied from vcpkg" -ForegroundColor Green
}
else {
    Write-Host "  [WARNING] vcpkg bin directory not found ($VcpkgBin)" -ForegroundColor Magenta
}

# 6/6 Copying assets
Write-Host "[6/6] Copying assets..." -ForegroundColor Yellow
$AssetsSource = Join-Path $SourceDir "src\assets"
if (Test-Path $AssetsSource) {
    $AssetsDest = Join-Path $OutputDir "assets"
    if (-not (Test-Path $AssetsDest)) { New-Item -ItemType Directory -Path $AssetsDest | Out-Null }
    Copy-Item -Path "$AssetsSource\*" -Destination $AssetsDest -Recurse -Force
    Write-Host "  [OK] Assets copied to $AssetsDest" -ForegroundColor Green
}
else {
    Write-Host "  [WARNING] Assets directory not found at $AssetsSource" -ForegroundColor Magenta
}

# Create README.txt
Write-Host "Creating README.txt..." -ForegroundColor Gray
$ReadmeContent = @"
FTorrent - Portable Installation
==================================

This is a portable version of FTorrent.

To run:
  - Double-click FTorrent.exe

Settings will be stored in:
  %APPDATA%\FTorrent\settings.ini

Downloaded torrents will be saved to:
  The path configured in Preferences (default: Downloads folder)

For more information, visit: https://github.com/yourusername/FTorrent

Version: 0.1.0
Compiled: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
"@
$ReadmeContent | Out-File -FilePath (Join-Path $OutputDir "README.txt") -Encoding utf8

Write-Host "`n====================================================================" -ForegroundColor Green
Write-Host "                    COMPILATION SUCCESSFUL!" -ForegroundColor Green
Write-Host "====================================================================`n" -ForegroundColor Green
Write-Host "FTorrent has been compiled and is ready to use."
Write-Host "Output directory: $OutputDir"
Write-Host ""

$ConfirmOpen = Read-Host "Do you want to open the output folder? (Y/N)"
if ($ConfirmOpen -eq "Y" -or $ConfirmOpen -eq "y") {
    explorer $OutputDir
}

Write-Host "`nYou can now run FTorrent from: $(Join-Path $OutputDir 'FTorrent.exe')"
pause
