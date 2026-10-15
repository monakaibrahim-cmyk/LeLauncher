@echo off
setlocal enabledelayedexpansion

echo ===============================================================================
echo  LeLauncher Automated Setup ^& Build for Windows
echo ===============================================================================

set "BUILD_TYPE=Release"
if "%~1"=="--debug" (
    set "BUILD_TYPE=Debug"
)

REM Check if pkg-config and gtk4 are already available in PATH
where pkg-config >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    pkg-config --exists gtk4 >nul 2>nul
    if %ERRORLEVEL% EQU 0 (
        echo [INFO] GTK 4 development libraries detected in PATH.
        goto :DO_BUILD
    )
)

REM Check for standard MSYS2 installations
set "MSYS2_ROOT="
if exist "C:\msys64\ucrt64.exe" set "MSYS2_ROOT=C:\msys64"
if not defined MSYS2_ROOT if exist "C:\msys2\ucrt64.exe" set "MSYS2_ROOT=C:\msys2"

if defined MSYS2_ROOT (
    echo [INFO] Detected MSYS2 at %MSYS2_ROOT%
    echo [INFO] Ensuring GTK 4, toolchain, and build dependencies are installed...
    "%MSYS2_ROOT%\usr\bin\bash.exe" -lc "pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-gtk4"
    
    echo [INFO] Configuring and building via MSYS2 UCRT64 environment...
    set "MSYSTEM=UCRT64"
    set "CHERE_INVOKING=enabled_from_arguments"
    "%MSYS2_ROOT%\usr\bin\bash.exe" -lc "cd '%CD%' && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE% && cmake --build build"
    if %ERRORLEVEL% EQU 0 (
        echo [SUCCESS] Build complete! Executable: build\LeLauncher.exe
        exit /b 0
    ) else (
        echo [ERROR] Build failed.
        exit /b 1
    )
)

echo.
echo ===============================================================================
echo  GTK 4 Development Libraries Not Found on this PC!
echo ===============================================================================
echo  To build LeLauncher on Windows:
echo.
echo  1. Install MSYS2 from https://www.msys2.org/
echo  2. Open 'MSYS2 UCRT64' terminal and run:
echo       pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake ^
echo                  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-gtk4
echo  3. Re-run this script: setup.bat
echo ===============================================================================
echo.
exit /b 1

:DO_BUILD
echo [INFO] Configuring build with CMake (%BUILD_TYPE%)...
cmake -B build -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake configuration failed.
    exit /b 1
)

echo [INFO] Compiling LeLauncher...
cmake --build build --config %BUILD_TYPE%
if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] Build complete! Executable located in build\
    exit /b 0
) else (
    echo [ERROR] Compilation failed.
    exit /b 1
)
