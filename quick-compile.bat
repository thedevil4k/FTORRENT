@echo off
setlocal enabledelayedexpansion
REM ====================================================================
REM FLTorrent - Quick Compile Script
REM Usage: quick-compile.bat [output_directory]
REM Example: quick-compile.bat D:\MyBuild
REM ====================================================================

set "OUTPUT=%~1"
if "%OUTPUT%"=="" set "OUTPUT=D:\FLTorrent Compilation"

echo Compiling FLTorrent to: %OUTPUT%
echo.

REM Check for compiler and CMake
where cl >nul 2>nul
set CL_FOUND=%errorlevel%

if %CL_FOUND% neq 0 (
    echo [INFO] Searching for Visual Studio...
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do (
        set "VS_PATH=%%i"
    )
    
    if exist "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" (
        echo [INFO] Activating VS environment...
        call "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat"
    ) else (
        echo ERROR: Visual Studio compiler not found in: !VS_PATH!
        echo Make sure "Desktop development with C++" is installed.
        pause
        exit /b 1
    )
)

REM Re-check CMake
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    set "VS_CMAKE=!VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    if exist "!VS_CMAKE!\cmake.exe" (
        set "PATH=!VS_CMAKE!;!PATH!"
    ) else (
        echo ERROR: CMake not found.
        pause
        exit /b 1
    )
)


REM Create and enter build directory
if not exist build mkdir build
cd build

REM Configure
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :error

REM Build
cmake --build . --config Release
if errorlevel 1 goto :error

REM Create output directory
if not exist "%OUTPUT%" mkdir "%OUTPUT%"

REM Copy files
copy /Y Release\FLTorrent.exe "%OUTPUT%\"
xcopy /Y /Q C:\vcpkg\installed\x64-windows\bin\*.dll "%OUTPUT%\" 2>nul
if not exist "%OUTPUT%\assets" mkdir "%OUTPUT%\assets"
xcopy /Y /E /Q "..\src\assets\*.*" "%OUTPUT%\assets\"

echo.
echo SUCCESS! FLTorrent compiled to: %OUTPUT%
echo.
explorer "%OUTPUT%"
goto :end

:error
echo.
echo ERROR: Compilation failed!
pause
exit /b 1

:end
pause
