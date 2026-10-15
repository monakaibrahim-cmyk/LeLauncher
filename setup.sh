#!/usr/bin/env bash
# ==============================================================================
# LeLauncher Automated Bootstrap & Build Script
#
# Detects operating system, installs required GTK 4 development libraries,
# configures CMake, and builds the launcher.
# ==============================================================================

set -euo pipefail

# ANSI color codes for readable output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
BUILD_TYPE="Release"
CLEAN_BUILD=0
DEPS_ONLY=0
RUN_AFTER=0

__usage__() {
    echo -e "${BOLD}Usage:${NC} $0 [options]"
    echo ""
    echo "Options:"
    echo "  --release      Build in Release configuration (default, optimized, file:line omitted)"
    echo "  --debug        Build in Debug configuration (with debug symbols & [file:line] logging)"
    echo "  --clean        Remove existing build directory before compiling"
    echo "  --deps-only    Install required system dependencies without building"
    echo "  --run          Execute the launcher immediately after a successful build"
    echo "  -h, --help     Show this help message"
    echo ""
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --clean)
            CLEAN_BUILD=1
            shift
            ;;
        --deps-only)
            DEPS_ONLY=1
            shift
            ;;
        --run)
            RUN_AFTER=1
            shift
            ;;
        -h|--help)
            __usage__
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            __usage__
            exit 1
            ;;
    esac
done

echo -e "${CYAN}${BOLD}=== LeLauncher Automated Setup & Build ===${NC}"
echo -e "Target Configuration: ${YELLOW}${BUILD_TYPE}${NC}"

# Check for root/sudo elevation helper
SUDO=""
if [[ $EUID -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    else
        echo -e "${YELLOW}Warning: 'sudo' not detected. If packages need to be installed, please run as root.${NC}"
    fi
fi

# Detect missing critical tools or headers
__needs_dependencies__() {
    if ! command -v cmake >/dev/null 2>&1; then return 0; fi
    if ! command -v git >/dev/null 2>&1; then return 0; fi
    if ! command -v pkg-config >/dev/null 2>&1 && ! command -v pkgconf >/dev/null 2>&1; then return 0; fi
    if ! command -v glib-compile-resources >/dev/null 2>&1; then return 0; fi
    if ! pkg-config --exists gtk4 2>/dev/null && ! pkgconf --exists gtk4 2>/dev/null; then return 0; fi
    return 1
}

# Detect distribution and install dependencies
__install_dependencies__() {
    echo -e "${CYAN}Detecting system package manager...${NC}"

    if [[ -f /etc/os-release ]]; then
        . /etc/os-release
        DISTRO_ID="${ID:-}"
        DISTRO_LIKE="${ID_LIKE:-}"
    else
        DISTRO_ID="unknown"
        DISTRO_LIKE=""
    fi

    echo -e "Detected platform: ${BOLD}${DISTRO_ID}${NC}"

    if [[ "$DISTRO_ID" =~ (debian|ubuntu|linuxmint|pop) ]] || [[ "$DISTRO_LIKE" =~ (debian|ubuntu) ]]; then
        echo -e "${CYAN}Installing GTK 4, GLib tools, and build essentials via apt...${NC}"
        $SUDO apt-get update -y
        $SUDO apt-get install -y \
            build-essential \
            cmake \
            git \
            pkg-config \
            libgtk-4-dev \
            libglib2.0-dev-bin
    elif [[ "$DISTRO_ID" =~ (fedora|rhel|centos|rocky|alma) ]] || [[ "$DISTRO_LIKE" =~ (fedora|rhel) ]]; then
        echo -e "${CYAN}Installing GTK 4 and build essentials via dnf...${NC}"
        $SUDO dnf install -y \
            gcc \
            gcc-c++ \
            make \
            cmake \
            git \
            pkgconf-pkg-config \
            gtk4-devel \
            glib2-devel
    elif [[ "$DISTRO_ID" =~ (arch|manjaro|endeavouros) ]] || [[ "$DISTRO_LIKE" =~ arch ]]; then
        echo -e "${CYAN}Installing GTK 4 and build essentials via pacman...${NC}"
        $SUDO pacman -S --needed --noconfirm \
            base-devel \
            cmake \
            git \
            pkgconf \
            gtk4 \
            glib2-devel
    elif [[ "$DISTRO_ID" =~ (opensuse|sles) ]] || [[ "$DISTRO_LIKE" =~ suse ]]; then
        echo -e "${CYAN}Installing GTK 4 and build essentials via zypper...${NC}"
        $SUDO zypper install -y \
            gcc \
            gcc-c++ \
            make \
            cmake \
            git \
            pkg-config \
            gtk4-devel \
            glib2-devel
    elif [[ "$DISTRO_ID" == "alpine" ]]; then
        echo -e "${CYAN}Installing GTK 4 and build essentials via apk...${NC}"
        $SUDO apk add \
            build-base \
            cmake \
            git \
            pkgconf \
            gtk4.0-dev \
            glib-dev
    elif [[ "${OSTYPE:-}" == "msys" ]] || [[ "${MSYSTEM:-}" =~ UCRT64|MINGW64 ]]; then
        echo -e "${CYAN}Installing GTK 4 and toolchain via MSYS2 pacman...${NC}"
        pacman -S --needed --noconfirm \
            mingw-w64-ucrt-x86_64-gcc \
            mingw-w64-ucrt-x86_64-cmake \
            mingw-w64-ucrt-x86_64-ninja \
            mingw-w64-ucrt-x86_64-pkgconf \
            mingw-w64-ucrt-x86_64-gtk4
    else
        echo -e "${RED}Unsupported or unrecognized Linux distribution: ${DISTRO_ID}${NC}"
        echo -e "Please install GTK 4 development headers (libgtk-4-dev) and glib-compile-resources manually."
        exit 1
    fi

    echo -e "${GREEN}✓ Dependencies verified and installed.${NC}"
}

# Check and install dependencies if needed
if __needs_dependencies__ || [[ $DEPS_ONLY -eq 1 ]]; then
    __install_dependencies__
else
    echo -e "${GREEN}✓ All required build tools and GTK 4 libraries are already present.${NC}"
fi

if [[ $DEPS_ONLY -eq 1 ]]; then
    echo -e "${GREEN}Dependencies installed successfully. Exiting as requested (--deps-only).${NC}"
    exit 0
fi

# Clean previous build if requested
if [[ $CLEAN_BUILD -eq 1 ]] && [[ -d "$BUILD_DIR" ]]; then
    echo -e "${CYAN}Cleaning existing build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

# Determine optimal parallel jobs
CPU_CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo -e "${CYAN}Configuring build with CMake (${BUILD_TYPE})...${NC}"
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -S "$SCRIPT_DIR"

echo -e "${CYAN}Compiling LeLauncher using ${CPU_CORES} parallel cores...${NC}"
cmake --build "$BUILD_DIR" --parallel "$CPU_CORES"

echo -e "${GREEN}${BOLD}✓ Build complete!${NC} Binary location: ${YELLOW}${BUILD_DIR}/LeLauncher${NC}"

# Run binary if requested
if [[ $RUN_AFTER -eq 1 ]]; then
    echo -e "${CYAN}Launching LeLauncher...${NC}"
    exec "${BUILD_DIR}/LeLauncher"
fi
