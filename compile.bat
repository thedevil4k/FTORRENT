@echo off
setlocal enabledelayedexpansion

REM ====================================================================
REM FLTorrent Compilation Script for Windows
REM ====================================================================
REM This script compiles FLTorrent and copies the executable to a
REM specified directory with all necessary DLLs.
REM ====================================================================

echo.
echo ====================================================================
echo                 FLTorrent Compilation Script
echo ====================================================================
echo.

REM Configuration
set "SOURCE_DIR=%~dp0"
set "VCPKG_ROOT=C:\vcpkg"
set "VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"

REM Default output directory (can be changed by user)
set "OUTPUT_DIR=D:\fltorrent-compilation"

REM Parse command line arguments
if not "%~1"=="" (
    set "OUTPUT_DIR=%~1"
)

echo Source Directory: %SOURCE_DIR%
echo Output Directory: %OUTPUT_DIR%
echo vcpkg Root: %VCPKG_ROOT%
echo.

REM Check if vcpkg exists
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo ERROR: vcpkg not found at %VCPKG_ROOT%
    echo Please install vcpkg or update VCPKG_ROOT variable in this script.
    pause
    exit /b 1
)

REM Check if toolchain file exists
if not exist "%VCPKG_TOOLCHAIN%" (
    echo ERROR: vcpkg toolchain file not found
    echo Expected at: %VCPKG_TOOLCHAIN%
    pause
    exit /b 1
)

REM Check for compiler and CMake
where cl >nul 2>nul
set CL_FOUND=%errorlevel%
where cmake >nul 2>nul
set CMAKE_FOUND=%errorlevel%

if %CL_FOUND% neq 0 (
    echo [INFO] Compiler (cl) not found in PATH. Searching for Visual Studio...
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do (
        set "VS_PATH=%%i"
    )
    
    if exist "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" (
        echo [INFO] Activating VS environment...
        call "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat"
    ) else (
        echo ERROR: Visual Studio compiler not found. Please install VS Build Tools with C++ components.
        pause
        exit /b 1
    )
)

REM Re-check CMake after activating VS environment (VS usually includes it)
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo [INFO] CMake not found in PATH. Searching in VS...
    set "VS_CMAKE=!VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    if exist "!VS_CMAKE!\cmake.exe" (
        set "PATH=!VS_CMAKE!;!PATH!"
        echo [OK] CMake found in VS extensions.
    ) else (
        echo ERROR: CMake not found. It is recommended to install it or include it in VS Build Tools.
        pause
        exit /b 1
    )
)

echo [1/5] Creating build directory...
if not exist "%SOURCE_DIR%build" (
    mkdir "%SOURCE_DIR%build"
)
cd /d "%SOURCE_DIR%build"

echo.
echo [2/5] Configuring with CMake...
cmake .. -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" -DCMAKE_BUILD_TYPE=Release

if errorlevel 1 (
    echo.
    echo ERROR: CMake configuration failed!
    echo.
    echo Possible causes:
    echo - Missing dependencies (run: .\install-vcpkg-deps.ps1)
    echo - vcpkg integration issue (run: vcpkg integrate install)
    echo.
    pause
    exit /b 1
)

echo.
echo [3/5] Building FLTorrent (Release)...
cmake --build . --config Release

if errorlevel 1 (
    echo.
    echo ERROR: Build failed!
    echo Check the error messages above.
    pause
    exit /b 1
)

echo.
echo [4/5] Creating output directory...
if not exist "%OUTPUT_DIR%" (
    mkdir "%OUTPUT_DIR%"
)

echo.
echo [5/5] Copying executable and dependencies...

REM Copy executable
if exist "Release\FLTorrent.exe" (
    copy /Y "Release\FLTorrent.exe" "%OUTPUT_DIR%\"
    echo   [OK] FLTorrent.exe
) else (
    echo   [ERROR] FLTorrent.exe not found!
    pause
    exit /b 1
)

REM Copy required DLLs from vcpkg
echo.
echo Copying DLL dependencies...

set "VCPKG_BIN=%VCPKG_ROOT%\installed\x64-windows\bin"

if exist "%VCPKG_BIN%" (
    REM Copy all DLLs (safer approach)
    xcopy /Y /Q "%VCPKG_BIN%\*.dll" "%OUTPUT_DIR%\" 2>nul
    echo   [OK] DLLs copied from vcpkg
) else (
    echo   [WARNING] vcpkg bin directory not found
    echo   %VCPKG_BIN%
)

REM Create a basic README
echo Creating README.txt...
(
    echo FLTorrent - Portable Installation
    echo ==================================
    echo.
    echo This is a portable version of FLTorrent.
    echo.
    echo To run:
    echo   - Double-click FLTorrent.exe
    echo.
    echo Settings will be stored in:
    echo   %%APPDATA%%\FLTorrent\settings.ini
    echo.
    echo Downloaded torrents will be saved to:
    echo   The path configured in Preferences ^(default: Downloads folder^)
    echo.
    echo For more information, visit: https://github.com/yourusername/FLTorrent
    echo.
    echo Version: 0.1.0
    echo Compiled: %date% %time%
) > "%OUTPUT_DIR%\README.txt"

echo   [OK] README.txt created

echo.
echo ====================================================================
echo                    COMPILATION SUCCESSFUL!
echo ====================================================================
echo.
echo FLTorrent has been compiled and is ready to use.^

echo.
echo Output directory: %OUTPUT_DIR%
echo.
echo Files created:
dir /B "%OUTPUT_DIR%\FLTorrent.exe" 2>nul
echo + DLL dependencies
echo + README.txt
echo.

REM Ask if user wants to open the output folder
set /p "OPEN_FOLDER=Do you want to open the output folder? (Y/N): "
if /i "%OPEN_FOLDER%"=="Y" (
    explorer "%OUTPUT_DIR%"
)

echo.
echo You can now run FLTorrent from: %OUTPUT_DIR%\FLTorrent.exe
echo.
pause
