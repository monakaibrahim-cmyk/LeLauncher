cmake_minimum_required(VERSION 3.23)
project(LeLauncher LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
    pkg_check_modules(GTK4 QUIET gtk4)
endif()

find_program(GLIB_COMPILE_RESOURCES glib-compile-resources)
find_program(BLUEPRINT_COMPILER blueprint-compiler HINTS ~/.local/bin)

# ==============================================================================
# GTK 4 & GResource Requirements (Mandatory - No CLI mode)
# ==============================================================================
if (NOT GTK4_FOUND OR NOT GLIB_COMPILE_RESOURCES)
    message(STATUS "")
    message(STATUS "###############################################################################")
    message(STATUS "# MISSING REQUIRED DEPENDENCY: GTK 4 Development Stack                        #")
    message(STATUS "###############################################################################")
    message(STATUS "")
    message(STATUS "LeLauncher requires GTK 4 and GLib resource compilation tools to build.")
    message(STATUS "Please install them via your platform package manager:")
    message(STATUS "")
    message(STATUS "  - Ubuntu / Debian / Linux Mint / Pop!_OS:")
    message(STATUS "      sudo apt update && sudo apt install -y libgtk-4-dev libglib2.0-dev-bin pkg-config")
    message(STATUS "")
    message(STATUS "  - Fedora / RHEL / CentOS / Rocky:")
    message(STATUS "      sudo dnf install -y gtk4-devel glib2-devel pkgconf-pkg-config")
    message(STATUS "")
    message(STATUS "  - Arch Linux / Manjaro:")
    message(STATUS "      sudo pacman -S --needed gtk4 glib2-devel pkgconf")
    message(STATUS "")
    message(STATUS "  - openSUSE:")
    message(STATUS "      sudo zypper install -y gtk4-devel glib2-devel pkg-config")
    message(STATUS "")
    message(STATUS "  - Windows (MSYS2 UCRT64):")
    message(STATUS "      pacman -S mingw-w64-ucrt-x86_64-gtk4 mingw-w64-ucrt-x86_64-pkgconf")
    message(STATUS "")
    message(STATUS "Or run the automated bootstrap script in the repository:")
    message(STATUS "      ./setup.sh")
    message(STATUS "###############################################################################")
    message(STATUS "")
    message(FATAL_ERROR "GTK 4 and glib-compile-resources are required to build LeLauncher.")
endif()

# ==============================================================================
# Include Directories
# ==============================================================================
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core
    ${CMAKE_CURRENT_SOURCE_DIR}/include/network
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui
    ${CMAKE_CURRENT_BINARY_DIR}
    ${GTK4_INCLUDE_DIRS}
)

# ==============================================================================
# Core Library (Data structures & Dat Container)
# ==============================================================================
add_library(lelauncher_core STATIC
    src/core/dat.c
    src/core/logger.c
)
target_include_directories(lelauncher_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core
)
target_link_libraries(lelauncher_core PUBLIC ${GTK4_LIBRARIES})

set(APP_SOURCES src/main.c)
set(APP_LIBS lelauncher_core)

# ==============================================================================
# Self-Contained Static libcurl (Cross-Platform & Windows static support)
# ==============================================================================
include(FetchContent)

# Static build configuration
set(BUILD_CURL_EXE OFF CACHE INTERNAL "Do not build curl CLI binary")
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "Build static libraries")
set(BUILD_STATIC_LIBS ON CACHE INTERNAL "Build static curl library")
set(CURL_STATICLIB ON CACHE INTERNAL "Enable CURL_STATICLIB")
set(BUILD_TESTING OFF CACHE INTERNAL "Disable testing")
set(CURL_DISABLE_TESTS ON CACHE INTERNAL "Disable curl tests")
set(CURL_DISABLE_MANUAL ON CACHE INTERNAL "Disable curl manual")
set(ENABLE_MANUAL OFF CACHE INTERNAL "Disable curl manual")
set(CURL_DISABLE_LDAP ON CACHE INTERNAL "Disable LDAP")
set(CURL_DISABLE_LDAPS ON CACHE INTERNAL "Disable LDAPS")
set(CURL_HIDDEN_SYMBOLS OFF CACHE INTERNAL "Do not hide symbols for static linking")

# Windows cross-platform TLS configuration:
# When targeting Windows (MSVC, MinGW, or cross-compilation), use native Schannel!
# This eliminates any dependency on external OpenSSL DLLs/libs on Windows.
if (WIN32)
    set(CURL_USE_SCHANNEL ON CACHE INTERNAL "Use Windows native Schannel TLS")
    set(CURL_USE_OPENSSL OFF CACHE INTERNAL "Disable OpenSSL on Windows in favor of Schannel")
endif()

FetchContent_Declare(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_10_1
    GIT_SHALLOW TRUE
)

message(STATUS "Fetching and configuring self-contained static libcurl...")
FetchContent_MakeAvailable(curl)

# ==============================================================================
# Network Library (Downloader, Manifest, Auth API & Live Feed)
# ==============================================================================
add_library(lelauncher_network STATIC
    src/network/downloader.c
    src/network/mfil.c
    src/network/api.c
    src/network/crypto.c
    src/network/hwid.c
    src/network/live_feed.c
)

# Link static curl, math library, and GTK/GLib libraries
target_link_libraries(lelauncher_network PUBLIC libcurl_static ${GTK4_LIBRARIES} m)

# Static curl requires CURL_STATICLIB defined for all consumers
target_compile_definitions(lelauncher_network PUBLIC CURL_STATICLIB=1)

# On Windows, link required system libraries
if (WIN32)
    target_link_libraries(lelauncher_network PUBLIC
        ws2_32
        crypt32
        wldap32
        bcrypt
        normaliz
    )
endif()

list(APPEND APP_LIBS lelauncher_network)
target_compile_definitions(lelauncher_core PUBLIC HAVE_CURL=1)

# ==============================================================================
# GUI Module: GTK 4 + Blueprint
# ==============================================================================

# 1. Compile Blueprint to XML (with fallback to pre-generated UI)
if (BLUEPRINT_COMPILER)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/window.ui
        COMMAND ${BLUEPRINT_COMPILER} compile
                --output ${CMAKE_CURRENT_BINARY_DIR}/window.ui
                ${CMAKE_CURRENT_SOURCE_DIR}/data/ui/window.blp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data/ui/window.blp
        COMMENT "Compiling Blueprint UI: window.blp -> window.ui"
    )
else()
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/window.ui
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/data/ui/window.ui
                ${CMAKE_CURRENT_BINARY_DIR}/window.ui
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data/ui/window.ui
        COMMENT "Copying fallback UI: window.ui"
    )
endif()

# 2. Compile GResource to C
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/resources.c
    COMMAND ${GLIB_COMPILE_RESOURCES}
            --sourcedir=${CMAKE_CURRENT_BINARY_DIR}
            --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/data/ui
            --target=${CMAKE_CURRENT_BINARY_DIR}/resources.c
            --generate-source
            ${CMAKE_CURRENT_SOURCE_DIR}/data/ui/resources.gresource.xml
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/window.ui
            ${CMAKE_CURRENT_SOURCE_DIR}/data/ui/resources.gresource.xml
    COMMENT "Generating GResource: resources.c"
)

list(APPEND APP_SOURCES
    src/ui/app_window.c
    ${CMAKE_CURRENT_BINARY_DIR}/resources.c
)
list(APPEND APP_LIBS ${GTK4_LIBRARIES})

# Windows Resource Compilation (.rc)
if (WIN32)
    enable_language(RC)
    list(APPEND APP_SOURCES data/LeLauncher.rc)
endif()

# ==============================================================================
# Executable Target (Pure Game Launcher GUI)
# ==============================================================================
add_executable(${CMAKE_PROJECT_NAME}
    ${APP_SOURCES}
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${GTK4_INCLUDE_DIRS}
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    ${APP_LIBS}
)
