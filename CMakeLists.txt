cmake_minimum_required(VERSION 3.23)
project(LeLauncher LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

message(STATUS "==================================================================")
message(STATUS " Configuring LeLauncher (WotLK Edition - Native Raylib/Raygui)    ")
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
# Self-Contained Static libcurl (Cross-Platform & Windows Native Schannel)
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
# Self-Contained Raylib (5.5) & Raygui (4.0)
# ==============================================================================
message(STATUS "Fetching self-contained static raylib (v5.5)...")
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
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

# Suppress false-positive third-party warnings in external raylib code on GCC
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
if (UNIX AND NOT APPLE)
    target_link_libraries(lelauncher_core PUBLIC pthread)
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

if (UNIX AND NOT APPLE)
    target_link_libraries(lelauncher_network PUBLIC pthread)
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
# Executable Target
# ==============================================================================
set(APP_SOURCES
    src/main.c
    src/ui/raygui_window.c
)

if (WIN32)
    enable_language(RC)
    list(APPEND APP_SOURCES data/LeLauncher.rc)
endif()

add_executable(${CMAKE_PROJECT_NAME} ${APP_SOURCES})

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${raylib_SOURCE_DIR}/src
    ${raygui_SOURCE_DIR}/src
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    lelauncher_core
    lelauncher_network
    raylib
)

if (UNIX AND NOT APPLE)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE m pthread dl)
endif()
