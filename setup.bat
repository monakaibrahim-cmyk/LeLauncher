@echo off
setlocal enabledelayedexpansion

echo ===============================================================================
echo  LeLauncher Automated Setup ^& Build for Windows (WotLK Edition)
echo ===============================================================================

set "BUILD_TYPE=Release"
set "DO_PACKAGE=0"
set "DO_CLEAN=0"

for %%A in (%*) do (
    if /I "%%A"=="--debug" set "BUILD_TYPE=Debug"
    if /I "%%A"=="--release" set "BUILD_TYPE=Release"
    if /I "%%A"=="--clean" set "DO_CLEAN=1"
    if /I "%%A"=="--package" set "DO_PACKAGE=1"
)

echo [INFO] Build Configuration: %BUILD_TYPE%
echo [INFO] UI Engine:          Pure Raylib + Raygui (100% Self-Contained)

if "%DO_CLEAN%"=="1" (
    if exist "build" (
        echo [INFO] Cleaning existing build directory...
        rmdir /s /q "build"
    )
)

REM If cmake is available in PATH, build directly
where cmake >nul 2>nul
if %ERRORLEVEL% EQU 0 goto :DO_BUILD

REM Check for standard MSYS2 installations
set "MSYS2_ROOT="
if exist "C:\msys64\ucrt64.exe" set "MSYS2_ROOT=C:\msys64"
if not defined MSYS2_ROOT if exist "C:\msys2\ucrt64.exe" set "MSYS2_ROOT=C:\msys2"

if defined MSYS2_ROOT (
    echo [INFO] Detected MSYS2 at %MSYS2_ROOT%
    echo [INFO] Ensuring toolchain and build dependencies are installed...
    "%MSYS2_ROOT%\usr\bin\bash.exe" -lc "pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja"
    
    echo [INFO] Configuring and building via MSYS2 UCRT64 environment...
    set "MSYSTEM=UCRT64"
    set "CHERE_INVOKING=enabled_from_arguments"
    "%MSYS2_ROOT%\usr\bin\bash.exe" -lc "cd '%CD%' && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE% && cmake --build build"
    if %ERRORLEVEL% EQU 0 (
        echo [SUCCESS] Build complete! Executable: build\LeLauncher.exe
        if "%DO_PACKAGE%"=="1" call :PACKAGE_BUNDLE
        exit /b 0
    ) else (
        echo [ERROR] Build failed.
        exit /b 1
    )
)

echo.
echo ===============================================================================
echo  Build Tools Not Found on this PC!
echo ===============================================================================
echo  To build LeLauncher on Windows:
echo  1. Install MSYS2 from https://www.msys2.org/
echo  2. Open 'MSYS2 UCRT64' terminal and run:
echo       pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
echo  3. Re-run: setup.bat
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
    if "%DO_PACKAGE%"=="1" call :PACKAGE_BUNDLE
    exit /b 0
) else (
    echo [ERROR] Compilation failed.
    exit /b 1
)

:PACKAGE_BUNDLE
echo [INFO] Packaging Windows distribution bundle...
set "DIST_DIR=dist\LeLauncher-Windows-x64"
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
if exist "dist\LeLauncher-Windows-x64.zip" del /f /q "dist\LeLauncher-Windows-x64.zip"
if not exist "dist" mkdir "dist"
mkdir "%DIST_DIR%"
if exist "build\LeLauncher.exe" copy "build\LeLauncher.exe" "%DIST_DIR%\" >nul
if exist "build\Release\LeLauncher.exe" copy "build\Release\LeLauncher.exe" "%DIST_DIR%\" >nul
if exist "WoW.mfil" copy "WoW.mfil" "%DIST_DIR%\" >nul
if exist "data" xcopy /e /i /y "data" "%DIST_DIR%\data" >nul
powershell -NoProfile -Command "Compress-Archive -Path 'dist\LeLauncher-Windows-x64' -DestinationPath 'dist\LeLauncher-Windows-x64.zip' -Force"
echo [SUCCESS] Distribution package created: dist\LeLauncher-Windows-x64.zip
exit /b 0
