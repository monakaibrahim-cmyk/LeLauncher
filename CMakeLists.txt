cmake_minimum_required(VERSION 3.23)
project(LeLauncher LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

message(STATUS "==================================================================")
message(STATUS " Configuring LeLauncher (Wrath of the Lich King - GTK 4 Edition)  ")
message(STATUS "==================================================================")

# ==============================================================================
# Compiler & Toolchain Setup
# ==============================================================================
if (MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# ==============================================================================
# GTK 4 and GLib Tooling Discovery
# ==============================================================================
find_package(GTK4 CONFIG QUIET)
if (TARGET GTK4::GTK4)
    message(STATUS "Found GTK4 via CMake Config: GTK4::GTK4")
    set(GTK4_TARGET GTK4::GTK4)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
    set(GTK4_TARGET PkgConfig::GTK4)
endif()

find_program(GLIB_COMPILE_RESOURCES NAMES glib-compile-resources)
find_program(BLUEPRINT_COMPILER NAMES blueprint-compiler)

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
if (DEFINED GTK4_INCLUDE_DIRS)
    include_directories(${GTK4_INCLUDE_DIRS})
endif()

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
# Core Library (Data structures, Dat Container & Async Logger)
# ==============================================================================
add_library(lelauncher_core STATIC
    src/core/dat.c
    src/core/logger.c
)
target_include_directories(lelauncher_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core
)
target_compile_definitions(lelauncher_core PUBLIC HAVE_CURL=1 USE_GTK4=1)

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
target_compile_definitions(lelauncher_network PUBLIC CURL_STATICLIB=1 USE_GTK4=1)
target_include_directories(lelauncher_network PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/network
)

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
# GResource & Blueprint UI Generation
# ==============================================================================
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
        COMMENT "Using precompiled UI: window.ui"
    )
endif()

set(APP_SOURCES
    src/main.c
    src/ui/app_window.c
)

if (GLIB_COMPILE_RESOURCES)
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
        COMMENT "Compiling GResource: resources.c"
    )
    list(APPEND APP_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/resources.c)
endif()

if (WIN32)
    enable_language(RC)
    list(APPEND APP_SOURCES data/LeLauncher.rc)
endif()

add_executable(${CMAKE_PROJECT_NAME} ${APP_SOURCES})

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE USE_GTK4=1)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    lelauncher_core
    lelauncher_network
    ${GTK4_TARGET}
)

if (WIN32)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE shell32)
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE m pthread dl)
endif()
