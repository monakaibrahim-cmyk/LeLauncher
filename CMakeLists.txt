cmake_minimum_required(VERSION 3.23)
project(LeLauncher LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ==============================================================================
# GUI Backend Selection (AUTO, RAYGUI, GTK)
# ==============================================================================
set(GUI_BACKEND "AUTO" CACHE STRING "GUI backend to use: AUTO, RAYGUI, or GTK")
set_property(CACHE GUI_BACKEND PROPERTY STRINGS "AUTO" "RAYGUI" "GTK")

find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
    pkg_check_modules(GTK4 QUIET gtk4)
endif()

find_program(GLIB_COMPILE_RESOURCES glib-compile-resources)
find_program(BLUEPRINT_COMPILER blueprint-compiler HINTS ~/.local/bin)

# Resolve ACTIVE_GUI
if (GUI_BACKEND STREQUAL "GTK")
    if (NOT GTK4_FOUND OR NOT GLIB_COMPILE_RESOURCES)
        message(STATUS "")
        message(STATUS "###############################################################################")
        message(STATUS "# MISSING REQUIRED DEPENDENCY: GTK 4 Development Stack                        #")
        message(STATUS "###############################################################################")
        message(STATUS "GTK 4 backend was explicitly requested (--gui=gtk), but GTK 4 or")
        message(STATUS "glib-compile-resources were not found on this system.")
        message(STATUS "")
        message(STATUS "Please install them via your platform package manager:")
        message(STATUS "  - Debian / Ubuntu:  sudo apt install -y libgtk-4-dev libglib2.0-dev-bin pkg-config")
        message(STATUS "  - Fedora:           sudo dnf install -y gtk4-devel glib2-devel pkgconf-pkg-config")
        message(STATUS "  - Arch Linux:       sudo pacman -S --needed gtk4 glib2-devel pkgconf")
        message(STATUS "  - MSYS2 UCRT64:     pacman -S mingw-w64-ucrt-x86_64-gtk4")
        message(STATUS "")
        message(STATUS "Or build with the self-contained Raygui backend instead:")
        message(STATUS "  cmake -B build -DGUI_BACKEND=RAYGUI")
        message(STATUS "###############################################################################")
        message(FATAL_ERROR "GTK 4 and glib-compile-resources are required for GUI_BACKEND=GTK.")
    endif()
    set(ACTIVE_GUI "GTK")
elseif (GUI_BACKEND STREQUAL "RAYGUI")
    set(ACTIVE_GUI "RAYGUI")
elseif (GUI_BACKEND STREQUAL "AUTO")
    if (GTK4_FOUND AND GLIB_COMPILE_RESOURCES)
        set(ACTIVE_GUI "GTK")
    else()
        message(STATUS "GTK 4 not found or incomplete. Falling back to self-contained Raygui backend.")
        set(ACTIVE_GUI "RAYGUI")
    endif()
else()
    message(FATAL_ERROR "Invalid GUI_BACKEND '${GUI_BACKEND}'. Must be AUTO, RAYGUI, or GTK.")
endif()

message(STATUS "==================================================================")
message(STATUS " LeLauncher GUI Backend: ${ACTIVE_GUI}")
message(STATUS "==================================================================")

# ==============================================================================
# Include Directories
# ==============================================================================
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core
    ${CMAKE_CURRENT_SOURCE_DIR}/include/network
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui
    ${CMAKE_CURRENT_BINARY_DIR}
)

include(FetchContent)

# ==============================================================================
# Self-Contained Static libcurl (Cross-Platform & Windows static support)
# ==============================================================================
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
# Raylib & Raygui (Fetched when ACTIVE_GUI == RAYGUI)
# ==============================================================================
if (ACTIVE_GUI STREQUAL "RAYGUI")
    message(STATUS "Fetching self-contained static raylib (v5.5)...")
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    # Disable unneeded 3D models and audio modules (LeLauncher is a 2D GUI launcher)
    set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
    set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)
    set(SUPPORT_MODULE_RMODELS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(raylib)

    # Suppress third-party warnings in external raylib code on GCC
    if (TARGET raylib AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(raylib PRIVATE -Wno-stringop-overflow)
    endif()

    message(STATUS "Fetching self-contained raygui (v4.0)...")
    FetchContent_Declare(
        raygui
        GIT_REPOSITORY https://github.com/raysan5/raygui.git
        GIT_TAG 4.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(raygui)
endif()

# ==============================================================================
# Core Library (Data structures & Dat Container & Async Logger)
# ==============================================================================
add_library(lelauncher_core STATIC
    src/core/dat.c
    src/core/logger.c
)
target_include_directories(lelauncher_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core
)
target_compile_definitions(lelauncher_core PUBLIC HAVE_CURL=1)

if (ACTIVE_GUI STREQUAL "GTK")
    target_compile_definitions(lelauncher_core PUBLIC USE_GTK4=1)
    target_include_directories(lelauncher_core PUBLIC ${GTK4_INCLUDE_DIRS})
    target_link_libraries(lelauncher_core PUBLIC ${GTK4_LIBRARIES})
else()
    target_compile_definitions(lelauncher_core PUBLIC USE_RAYGUI=1)
    if (UNIX AND NOT APPLE)
        target_link_libraries(lelauncher_core PUBLIC pthread)
    endif()
endif()

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

target_link_libraries(lelauncher_network PUBLIC libcurl_static m)
target_compile_definitions(lelauncher_network PUBLIC CURL_STATICLIB=1)

if (ACTIVE_GUI STREQUAL "GTK")
    target_compile_definitions(lelauncher_network PUBLIC USE_GTK4=1)
    target_include_directories(lelauncher_network PUBLIC ${GTK4_INCLUDE_DIRS})
    target_link_libraries(lelauncher_network PUBLIC ${GTK4_LIBRARIES})
else()
    target_compile_definitions(lelauncher_network PUBLIC USE_RAYGUI=1)
    if (UNIX AND NOT APPLE)
        target_link_libraries(lelauncher_network PUBLIC pthread)
    endif()
endif()

if (WIN32)
    target_link_libraries(lelauncher_network PUBLIC
        ws2_32
        crypt32
        wldap32
        bcrypt
        normaliz
    )
endif()

# ==============================================================================
# GUI Sources Selection
# ==============================================================================
set(UI_SOURCES "")

if (ACTIVE_GUI STREQUAL "GTK")
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

    list(APPEND UI_SOURCES
        src/ui/app_window.c
        ${CMAKE_CURRENT_BINARY_DIR}/resources.c
    )
else()
    list(APPEND UI_SOURCES
        src/ui/raygui_window.c
    )
endif()

# ==============================================================================
# Executable Target
# ==============================================================================
set(APP_SOURCES
    src/main.c
    ${UI_SOURCES}
)

if (WIN32)
    enable_language(RC)
    list(APPEND APP_SOURCES data/LeLauncher.rc)
endif()

add_executable(${CMAKE_PROJECT_NAME} ${APP_SOURCES})

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    lelauncher_core
    lelauncher_network
)

if (ACTIVE_GUI STREQUAL "GTK")
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE USE_GTK4=1)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${GTK4_INCLUDE_DIRS})
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${GTK4_LIBRARIES})
else()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE USE_RAYGUI=1)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${raylib_SOURCE_DIR}/src
        ${raygui_SOURCE_DIR}/src
    )
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE raylib)
    if (UNIX AND NOT APPLE)
        target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE m pthread dl)
    endif()
endif()
