@echo off
setlocal enabledelayedexpansion

echo ===============================================================================
echo  LeLauncher Automated Setup ^& Build for Windows (GTK 4 Edition)
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
echo [INFO] UI Engine:          GTK 4 (Embedded Binary UI)

if "%DO_CLEAN%"=="1" (
    if exist "build" (
        echo [INFO] Cleaning existing build directory...
        rmdir /s /q "build"
    )
)

REM 1. Check for active MSVC in PATH
where cl >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [INFO] Detected active MSVC toolchain in PATH.
    goto :DO_MSVC_BUILD
)

REM 2. Discover Visual Studio installation via vswhere
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_DIR=%%i"
    )
    if defined VS_DIR (
        if exist "!VS_DIR!\VC\Auxiliary\Build\vcvars64.bat" (
            echo [INFO] Detected Visual Studio at !VS_DIR!
            echo [INFO] Initializing MSVC x64 developer environment...
            call "!VS_DIR!\VC\Auxiliary\Build\vcvars64.bat"
            goto :DO_MSVC_BUILD
        )
    )
)

REM 3. If cmake is available in PATH, build directly
where cmake >nul 2>nul
if %ERRORLEVEL% EQU 0 goto :DO_BUILD

REM 4. Check for standard MSYS2 installations as fallback
set "MSYS2_ROOT="
if exist "C:\msys64\ucrt64.exe" set "MSYS2_ROOT=C:\msys64"
if not defined MSYS2_ROOT if exist "C:\msys2\ucrt64.exe" set "MSYS2_ROOT=C:\msys2"

if defined MSYS2_ROOT (
    echo [INFO] Detected MSYS2 at %MSYS2_ROOT%
    echo [INFO] Ensuring toolchain and GTK 4 dependencies are installed...
    "%MSYS2_ROOT%\usr\bin\bash.exe" -lc "pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-gtk4"
    
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
echo Please install Visual Studio 2022 (with Desktop Development with C++) or MSYS2.
exit /b 1

:DO_MSVC_BUILD
echo [INFO] Configuring with MSVC and CMake...
set "VCPKG_CMAKE="
if exist "vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_CMAKE=vcpkg\scripts\buildsystems\vcpkg.cmake"
if not defined VCPKG_CMAKE if defined VCPKG_ROOT if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" set "VCPKG_CMAKE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not defined VCPKG_CMAKE if defined VCPKG_INSTALLATION_ROOT if exist "%VCPKG_INSTALLATION_ROOT%\scripts\buildsystems\vcpkg.cmake" set "VCPKG_CMAKE=%VCPKG_INSTALLATION_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not defined VCPKG_CMAKE if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_CMAKE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake"

if defined VCPKG_CMAKE (
    echo [INFO] Using vcpkg toolchain: !VCPKG_CMAKE!
    cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="!VCPKG_CMAKE!"
) else (
    cmake -B build -G "Visual Studio 17 2022" -A x64
)
cmake --build build --config %BUILD_TYPE%
if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] Build complete with MSVC!
    if "%DO_PACKAGE%"=="1" call :PACKAGE_BUNDLE
    exit /b 0
) else (
    echo [ERROR] MSVC build failed.
    exit /b 1
)

:DO_BUILD
echo [INFO] Configuring build with CMake...
cmake -B build -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
cmake --build build --config %BUILD_TYPE%
if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] Build complete!
    if "%DO_PACKAGE%"=="1" call :PACKAGE_BUNDLE
    exit /b 0
) else (
    echo [ERROR] Compilation failed.
    exit /b 1
)

:PACKAGE_BUNDLE
echo [INFO] Packaging distribution bundle...
set "DIST_DIR=dist\LeLauncher-Windows-x64"
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%"

if exist "build\%BUILD_TYPE%\LeLauncher.exe" (
    copy /y "build\%BUILD_TYPE%\LeLauncher.exe" "%DIST_DIR%\" >nul
) else if exist "build\LeLauncher.exe" (
    copy /y "build\LeLauncher.exe" "%DIST_DIR%\" >nul
)

REM Bundle runtime DLLs
if exist "build\%BUILD_TYPE%\*.dll" copy /y "build\%BUILD_TYPE%\*.dll" "%DIST_DIR%\" >nul
if exist "build\*.dll" copy /y "build\*.dll" "%DIST_DIR%\" >nul
if exist "vcpkg_installed\x64-windows\bin\*.dll" copy /y "vcpkg_installed\x64-windows\bin\*.dll" "%DIST_DIR%\" >nul

REM Bundle GTK 4 schemas
if exist "vcpkg_installed\x64-windows\share\glib-2.0\schemas" (
    mkdir "%DIST_DIR%\share\glib-2.0\schemas" >nul 2>&1
    copy /y "vcpkg_installed\x64-windows\share\glib-2.0\schemas\*.*" "%DIST_DIR%\share\glib-2.0\schemas\" >nul
)

if exist "WoW.mfil" copy /y "WoW.mfil" "%DIST_DIR%\" >nul
if exist "icon.ico" copy /y "icon.ico" "%DIST_DIR%\" >nul
if exist "data" xcopy /e /i /y "data" "%DIST_DIR%\data" >nul

powershell -NoProfile -Command "Compress-Archive -Path '%DIST_DIR%\*' -DestinationPath '%DIST_DIR%.zip' -Force" 2>nul
if exist "%DIST_DIR%.zip" echo [SUCCESS] Portable archive created: %DIST_DIR%.zip

echo [SUCCESS] Distribution folder created at %DIST_DIR%
exit /b 0
