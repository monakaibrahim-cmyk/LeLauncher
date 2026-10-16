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

REM -----------------------------------------------------------------------------
REM 1. Locate vswhere.exe across standard 32-bit, 64-bit, and PATH locations
REM -----------------------------------------------------------------------------
set "VSWHERE="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
) else if exist "%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe" (
    set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
) else (
    for /f "usebackq tokens=*" %%P in (`where vswhere.exe 2^>nul`) do set "VSWHERE=%%P"
)

REM -----------------------------------------------------------------------------
REM 2. Discover Visual Studio & MSVC Toolchain
REM -----------------------------------------------------------------------------
set "MSVC_ACTIVE=0"
where cl >nul 2>nul
if %ERRORLEVEL% EQU 0 set "MSVC_ACTIVE=1"

set "VS_DIR="
set "VS_MAJOR="
set "VS_DIR_NO_VC="

if defined VSWHERE (
    REM Search all product editions (including BuildTools) for VC Tools x86/x64
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_DIR=%%i"
    )
    for /f "usebackq tokens=1 delims=." %%v in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion`) do (
        set "VS_MAJOR=%%v"
    )

    REM If not found with VC tools requirement, check if VS is installed at all
    if not defined VS_DIR (
        for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do (
            set "VS_DIR_NO_VC=%%i"
        )
        if defined VS_DIR_NO_VC (
            if exist "!VS_DIR_NO_VC!\VC\Auxiliary\Build\vcvars64.bat" (
                set "VS_DIR=!VS_DIR_NO_VC!"
            )
            for /f "usebackq tokens=1 delims=." %%v in (`"%VSWHERE%" -latest -products * -property installationVersion`) do (
                set "VS_MAJOR=%%v"
            )
        )
    )
)

REM If MSVC is not active in current session, try initializing via vcvars64.bat
if "%MSVC_ACTIVE%"=="0" if defined VS_DIR (
    if exist "!VS_DIR!\VC\Auxiliary\Build\vcvars64.bat" (
        echo [INFO] Detected Visual Studio at !VS_DIR!
        set "VCPKG_VISUAL_STUDIO_PATH=!VS_DIR!"
        echo [INFO] Initializing MSVC x64 developer environment...
        call "!VS_DIR!\VC\Auxiliary\Build\vcvars64.bat"
        where cl >nul 2>nul
        if !ERRORLEVEL! EQU 0 set "MSVC_ACTIVE=1"
    )
)

REM If MSVC is now active, proceed to MSVC build
if "%MSVC_ACTIVE%"=="1" (
    if not defined VCPKG_VISUAL_STUDIO_PATH if defined VS_DIR set "VCPKG_VISUAL_STUDIO_PATH=!VS_DIR!"
    if not defined VCPKG_VISUAL_STUDIO_PATH if defined VSINSTALLDIR set "VCPKG_VISUAL_STUDIO_PATH=%VSINSTALLDIR%"
    if defined VCPKG_VISUAL_STUDIO_PATH (
        if "!VCPKG_VISUAL_STUDIO_PATH:~-1!"=="\" set "VCPKG_VISUAL_STUDIO_PATH=!VCPKG_VISUAL_STUDIO_PATH:~0,-1!"
        if "!VCPKG_VISUAL_STUDIO_PATH:~-1!"=="/" set "VCPKG_VISUAL_STUDIO_PATH=!VCPKG_VISUAL_STUDIO_PATH:~0,-1!"
    )
    goto :DO_MSVC_BUILD
)

REM -----------------------------------------------------------------------------
REM 3. If Visual Studio is present but lacks C++ workload, explain how to fix
REM -----------------------------------------------------------------------------
if defined VS_DIR_NO_VC (
    echo.
    echo ===============================================================================
    echo  [WARNING] Visual Studio Found, but C++ Toolchain is Missing!
    echo ===============================================================================
    echo  Installation path: !VS_DIR_NO_VC!
    echo  The C++ compiler tools (vcvars64.bat) were not found in this installation.
    echo.
    echo  To fix this:
    echo   1. Launch 'Visual Studio Installer' from your Windows Start Menu.
    echo   2. Click 'Modify' on your Visual Studio or Build Tools installation.
    echo   3. Under 'Workloads', check 'Desktop development with C++'.
    echo   4. Click 'Modify' in the lower-right corner to install.
    echo ===============================================================================
    echo.
)

REM -----------------------------------------------------------------------------
REM 4. Fallback: Check for standard MSYS2 installations
REM -----------------------------------------------------------------------------
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
    if !ERRORLEVEL! EQU 0 (
        echo [SUCCESS] Build complete! Executable: build\LeLauncher.exe
        if "%DO_PACKAGE%"=="1" call :PACKAGE_BUNDLE
        exit /b 0
    ) else (
        echo [ERROR] Build failed.
        exit /b 1
    )
)

REM -----------------------------------------------------------------------------
REM 5. Fallback: If cmake and gcc/clang are in PATH, try general build
REM -----------------------------------------------------------------------------
where cmake >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set "HAS_CC=0"
    where gcc >nul 2>nul
    if !ERRORLEVEL! EQU 0 set "HAS_CC=1"
    where clang >nul 2>nul
    if !ERRORLEVEL! EQU 0 set "HAS_CC=1"

    if "!HAS_CC!"=="1" goto :DO_BUILD
)

REM -----------------------------------------------------------------------------
REM 6. No suitable build environment found - Guide the user
REM -----------------------------------------------------------------------------
echo.
echo ===============================================================================
echo  Build Tools Not Found on this PC!
echo ===============================================================================
echo Visual Studio or MSYS2 is required to compile LeLauncher on Windows.
echo.
echo Option 1 (Recommended - MSVC):
echo   1. Download and run Visual Studio Installer from:
echo      https://visualstudio.microsoft.com/downloads/
echo   2. Select 'Desktop development with C++' workload.
echo   3. Re-run setup.bat.
echo.
echo Option 2 (MSYS2 UCRT64):
echo   1. Install MSYS2 to C:\msys64 from https://www.msys2.org/
echo   2. Re-run setup.bat (it will automatically install GTK4 and GCC).
echo ===============================================================================
exit /b 1

:DO_MSVC_BUILD
echo [INFO] Configuring with MSVC and CMake...
set "VCPKG_CMAKE="
if exist "vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_CMAKE=vcpkg\scripts\buildsystems\vcpkg.cmake"
if not defined VCPKG_CMAKE if defined VCPKG_ROOT if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" set "VCPKG_CMAKE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not defined VCPKG_CMAKE if defined VCPKG_INSTALLATION_ROOT if exist "%VCPKG_INSTALLATION_ROOT%\scripts\buildsystems\vcpkg.cmake" set "VCPKG_CMAKE=%VCPKG_INSTALLATION_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not defined VCPKG_CMAKE if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_CMAKE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake"

REM Ensure vcpkg tools (pkgconf, glib) are discoverable in PATH
if exist "vcpkg_installed\x64-windows\tools\pkgconf" set "PATH=%CD%\vcpkg_installed\x64-windows\tools\pkgconf;!PATH!"
if exist "vcpkg_installed\x64-windows\tools\glib" set "PATH=%CD%\vcpkg_installed\x64-windows\tools\glib;!PATH!"
if defined VCPKG_INSTALLATION_ROOT (
    if exist "%VCPKG_INSTALLATION_ROOT%\installed\x64-windows\tools\pkgconf" set "PATH=%VCPKG_INSTALLATION_ROOT%\installed\x64-windows\tools\pkgconf;!PATH!"
    if exist "%VCPKG_INSTALLATION_ROOT%\installed\x64-windows\tools\glib" set "PATH=%VCPKG_INSTALLATION_ROOT%\installed\x64-windows\tools\glib;!PATH!"
)

REM Determine optimal CMake generator based on detected Visual Studio version
set "CMAKE_GEN="
set "IS_MULTI_CONFIG=1"

REM Check VS_MAJOR if discovered via vswhere
if defined VS_MAJOR (
    if "!VS_MAJOR!"=="17" set "CMAKE_GEN=-G ""Visual Studio 17 2022"" -A x64"
    if "!VS_MAJOR!"=="16" set "CMAKE_GEN=-G ""Visual Studio 16 2019"" -A x64"
    if "!VS_MAJOR!"=="15" set "CMAKE_GEN=-G ""Visual Studio 15 2017"" -A x64"
)

REM If not resolved, check VisualStudioVersion environment variable
if not defined CMAKE_GEN if defined VisualStudioVersion (
    if "%VisualStudioVersion:~0,2%"=="17" set "CMAKE_GEN=-G ""Visual Studio 17 2022"" -A x64"
    if "%VisualStudioVersion:~0,2%"=="16" set "CMAKE_GEN=-G ""Visual Studio 16 2019"" -A x64"
    if "%VisualStudioVersion:~0,2%"=="15" set "CMAKE_GEN=-G ""Visual Studio 15 2017"" -A x64"
)

REM If still not resolved and ninja is available, use Ninja
if not defined CMAKE_GEN (
    where ninja >nul 2>nul
    if !ERRORLEVEL! EQU 0 (
        set "CMAKE_GEN=-G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE%"
        set "IS_MULTI_CONFIG=0"
    )
)

REM Default to Visual Studio 17 2022 if still unset
if not defined CMAKE_GEN set "CMAKE_GEN=-G ""Visual Studio 17 2022"" -A x64"

echo [INFO] Selected CMake Generator: !CMAKE_GEN!

REM Run CMake configuration
if defined VCPKG_CMAKE (
    echo [INFO] Using vcpkg toolchain: !VCPKG_CMAKE!
    cmake -B build !CMAKE_GEN! -DCMAKE_TOOLCHAIN_FILE="!VCPKG_CMAKE!"
) else (
    cmake -B build !CMAKE_GEN!
)

REM If Visual Studio generator failed with 'could not find any instance', attempt Ninja fallback
if !ERRORLEVEL! NEQ 0 (
    where ninja >nul 2>nul
    if !ERRORLEVEL! EQU 0 (
        echo [WARN] Visual Studio generator failed. Retrying configuration with Ninja...
        set "IS_MULTI_CONFIG=0"
        if defined VCPKG_CMAKE (
            cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_TOOLCHAIN_FILE="!VCPKG_CMAKE!"
        ) else (
            cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
        )
    )
)

if !ERRORLEVEL! NEQ 0 (
    echo [ERROR] CMake configuration failed.
    exit /b 1
)

echo [INFO] Compiling LeLauncher with MSVC (%BUILD_TYPE%)...
if "!IS_MULTI_CONFIG!"=="1" (
    cmake --build build --config %BUILD_TYPE% --parallel
) else (
    cmake --build build --parallel
)

if !ERRORLEVEL! EQU 0 (
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
cmake --build build --config %BUILD_TYPE% --parallel
if !ERRORLEVEL! EQU 0 (
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
