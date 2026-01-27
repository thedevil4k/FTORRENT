<#
.SYNOPSIS
    FTorrent Windows Installer Generation Script
.DESCRIPTION
    This script ensures the project is compiled and then generates an NSIS installer and a ZIP package.
.PARAMETER VcpkgRoot
    The root directory of vcpkg installation.
#>

param (
    [string]$VcpkgRoot = "C:\vcpkg"
)

$ErrorActionPreference = "Stop"

Write-Host "`n====================================================================" -ForegroundColor Cyan
Write-Host "                FTorrent Installer Generation" -ForegroundColor Cyan
Write-Host "====================================================================`n" -ForegroundColor Cyan

$ProjectRoot = Resolve-Path "$PSScriptRoot\..\..\.."
$BuildDir = Join-Path $ProjectRoot "build_installer"
$VcpkgToolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"

# 1. Check dependencies
if (-not (Test-Path $VcpkgToolchain)) {
    Write-Error "vcpkg toolchain not found at $VcpkgToolchain. Please install vcpkg or specify -VcpkgRoot."
}

# 2. Search for Visual Studio Environment if cl is not in path
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    Write-Host "[INFO] Compiler (cl) not found in PATH. Searching for Visual Studio..." -ForegroundColor Gray
    
    $VsWherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $VsWherePath)) {
        Write-Error "vswhere.exe not found at $VsWherePath. Is Visual Studio installed?"
    }

    $VsPath = & $VsWherePath -latest -products * -property installationPath
    $VcvarsPath = Join-Path $VsPath "VC\Auxiliary\Build\vcvars64.bat"
    
    if (Test-Path $VcvarsPath) {
        Write-Host "[INFO] Activating VS environment..." -ForegroundColor Gray
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
        Write-Error "Visual Studio compiler environment (vcvars64.bat) not found."
    }
}

# 3. Create build directory for installer
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}
Set-Location $BuildDir

# 4. Configure and Build (Pre-compilation)
# We use a dedicated build folder for the installer to ensure clinical separation
Write-Host "[1/3] Configuring project for installer..." -ForegroundColor Yellow
cmake "$ProjectRoot" -DCMAKE_TOOLCHAIN_FILE="$VcpkgToolchain" -DCMAKE_BUILD_TYPE=Release

Write-Host "[2/3] Building FTorrent (Release)..." -ForegroundColor Yellow
cmake --build . --config Release

# 5. Generate Installer with CPack
Write-Host "[3/3] Generating NSIS Installer and ZIP package..." -ForegroundColor Yellow

# Check if cpack is available
if (-not (Get-Command cpack -ErrorAction SilentlyContinue)) {
    Write-Error "cpack command not found. Ensure CMake is correctly installed and in your PATH."
}

& cpack -G "NSIS;ZIP"

Write-Host "`n====================================================================" -ForegroundColor Green
Write-Host "                INSTALLER GENERATED SUCCESSFULLY!" -ForegroundColor Green
Write-Host "====================================================================`n" -ForegroundColor Green

$OutputFiles = Get-ChildItem -Path $BuildDir -Filter "FTorrent-*" | Where-Object { $_.Extension -match "exe|zip" }

if ($OutputFiles) {
    Write-Host "Created files:" -ForegroundColor White
    $OutputFiles | ForEach-Object { Write-Host "  - $($_.Name)" -ForegroundColor Green }
}

Write-Host "`nLocation: $BuildDir"

$ConfirmOpen = Read-Host "`nDo you want to open the installer folder? (Y/N)"
if ($ConfirmOpen -eq "Y" -or $ConfirmOpen -eq "y") {
    explorer $BuildDir
}

pause
