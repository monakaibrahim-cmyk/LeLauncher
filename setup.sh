#!/usr/bin/env bash
# ==============================================================================
# LeLauncher Automated Bootstrap & Build Script (WotLK Edition)
#
# Detects operating system, ensures basic build tools are present,
# configures CMake, and builds the pure Raylib/Raygui launcher.
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
PACKAGE_BUNDLE=0
RUN_AFTER=0

__usage__() {
    echo -e "${BOLD}Usage:${NC} $0 [options]"
    echo ""
    echo "Options:"
    echo "  --release          Build in Release configuration (default, optimized)"
    echo "  --debug            Build in Debug configuration (with debug symbols & [file:line] logging)"
    echo "  --clean            Remove existing build directory before compiling"
    echo "  --deps-only        Ensure basic build tools are installed without building"
    echo "  --package          Package built launcher and runtime assets into dist/ tarball"
    echo "  --run              Execute the launcher immediately after a successful build"
    echo "  -h, --help         Show this help message"
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
        --package)
            PACKAGE_BUNDLE=1
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

echo -e "${CYAN}${BOLD}=== LeLauncher Automated Setup & Build (WotLK Edition) ===${NC}"
echo -e "Target Configuration: ${YELLOW}${BUILD_TYPE}${NC}"
echo -e "UI Engine:            ${CYAN}Pure Raylib + Raygui (100% Self-Contained)${NC}"

# Check for root/sudo elevation helper
SUDO=""
CAN_SUDO_NONINTERACTIVE=0
if [[ $EUID -eq 0 ]]; then
    CAN_SUDO_NONINTERACTIVE=1
elif command -v sudo >/dev/null 2>&1; then
    if sudo -n true >/dev/null 2>&1; then
        SUDO="sudo -n"
        CAN_SUDO_NONINTERACTIVE=1
    else
        SUDO="sudo"
        CAN_SUDO_NONINTERACTIVE=0
    fi
fi

# Detect missing critical tools (Zero GUI packages required)
__get_missing_dependencies__() {
    local missing=()
    if ! command -v cmake >/dev/null 2>&1; then missing+=("cmake"); fi
    if ! command -v git >/dev/null 2>&1; then missing+=("git"); fi
    if ! command -v cc >/dev/null 2>&1 && ! command -v gcc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then missing+=("build-essential (gcc/clang)"); fi
    echo "${missing[*]}"
}

__needs_dependencies__() {
    local missing
    missing=$(__get_missing_dependencies__)
    [[ -n "$missing" ]]
}

# Detect distribution and install essential build tools if missing
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

    if [[ $CAN_SUDO_NONINTERACTIVE -eq 0 ]] && ! [ -t 0 ]; then
        local missing_pkgs
        missing_pkgs=$(__get_missing_dependencies__)
        echo ""
        echo -e "${RED}${BOLD}===============================================================================${NC}"
        echo -e "${RED}${BOLD}  Error: Missing required basic tools:${NC} ${YELLOW}${missing_pkgs}${NC}"
        echo -e "${RED}${BOLD}  'sudo' requires an interactive password, which cannot be prompted in CI.${NC}"
        echo -e "${RED}${BOLD}===============================================================================${NC}"
        echo ""
        echo -e "${CYAN}To fix this, please run this one-time command on your VPS terminal:${NC}"
        echo ""
        if [[ "$DISTRO_ID" =~ (debian|ubuntu|linuxmint|pop) ]] || [[ "$DISTRO_LIKE" =~ (debian|ubuntu) ]]; then
            echo -e "  ${BOLD}sudo apt-get update && sudo apt-get install -y build-essential cmake git${NC}"
        elif [[ "$DISTRO_ID" =~ (fedora|rhel|centos|rocky|alma) ]] || [[ "$DISTRO_LIKE" =~ (fedora|rhel) ]]; then
            echo -e "  ${BOLD}sudo dnf install -y gcc gcc-c++ make cmake git${NC}"
        elif [[ "$DISTRO_ID" =~ (arch|manjaro|endeavouros) ]] || [[ "$DISTRO_LIKE" =~ arch ]]; then
            echo -e "  ${BOLD}sudo pacman -S --needed --noconfirm base-devel cmake git${NC}"
        else
            echo -e "  Install cmake, git, and a C compiler using your distribution package manager."
        fi
        RUNNER_USER=$(whoami 2>/dev/null || echo "$USER")
        echo ""
        echo -e "${CYAN}Alternatively, grant passwordless sudo to the runner user (${YELLOW}${RUNNER_USER}${CYAN}):${NC}"
        echo -e "  ${BOLD}echo \"${RUNNER_USER} ALL=(ALL) NOPASSWD: ALL\" | sudo tee /etc/sudoers.d/${RUNNER_USER}${NC}"
        echo ""
        exit 1
    fi

    if [[ "$DISTRO_ID" =~ (debian|ubuntu|linuxmint|pop) ]] || [[ "$DISTRO_LIKE" =~ (debian|ubuntu) ]]; then
        echo -e "${CYAN}Installing build essentials via apt...${NC}"
        $SUDO apt-get update -y
        $SUDO apt-get install -y build-essential cmake git
    elif [[ "$DISTRO_ID" =~ (fedora|rhel|centos|rocky|alma) ]] || [[ "$DISTRO_LIKE" =~ (fedora|rhel) ]]; then
        echo -e "${CYAN}Installing build essentials via dnf...${NC}"
        $SUDO dnf install -y gcc gcc-c++ make cmake git
    elif [[ "$DISTRO_ID" =~ (arch|manjaro|endeavouros) ]] || [[ "$DISTRO_LIKE" =~ arch ]]; then
        echo -e "${CYAN}Installing build essentials via pacman...${NC}"
        $SUDO pacman -S --needed --noconfirm base-devel cmake git
    elif [[ "$DISTRO_ID" =~ (opensuse|sles) ]] || [[ "$DISTRO_LIKE" =~ suse ]]; then
        echo -e "${CYAN}Installing build essentials via zypper...${NC}"
        $SUDO zypper install -y gcc gcc-c++ make cmake git
    elif [[ "$DISTRO_ID" == "alpine" ]]; then
        echo -e "${CYAN}Installing build essentials via apk...${NC}"
        $SUDO apk add build-base cmake git
    elif [[ "${OSTYPE:-}" == "msys" ]] || [[ "${MSYSTEM:-}" =~ UCRT64|MINGW64 ]]; then
        echo -e "${CYAN}Installing toolchain via MSYS2 pacman...${NC}"
        pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
    fi

    echo -e "${GREEN}✓ Basic toolchain verified and installed.${NC}"
}

# Check and install tools if needed
if __needs_dependencies__ || [[ $DEPS_ONLY -eq 1 ]]; then
    __install_dependencies__
else
    echo -e "${GREEN}✓ All required build tools are already present.${NC}"
fi

if [[ $DEPS_ONLY -eq 1 ]]; then
    echo -e "${GREEN}Dependencies checked successfully. Exiting as requested (--deps-only).${NC}"
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

# Package distribution bundle if requested
if [[ $PACKAGE_BUNDLE -eq 1 ]]; then
    echo -e "${CYAN}Packaging release distribution bundle...${NC}"
    ARCH=$(uname -m)
    DIST_DIR="${SCRIPT_DIR}/dist"
    BUNDLE_NAME="LeLauncher-Linux-${ARCH}"
    BUNDLE_DIR="${DIST_DIR}/${BUNDLE_NAME}"
    TARBALL_PATH="${DIST_DIR}/${BUNDLE_NAME}.tar.gz"

    rm -rf "$BUNDLE_DIR" "$TARBALL_PATH"
    mkdir -p "$BUNDLE_DIR"

    cp "${BUILD_DIR}/LeLauncher" "${BUNDLE_DIR}/"
    if [[ "$BUILD_TYPE" == "Release" ]] && command -v strip >/dev/null 2>&1; then
        echo -e "${CYAN}Stripping debug symbols from release binary...${NC}"
        strip "${BUNDLE_DIR}/LeLauncher" || true
    fi

    if [[ -f "${SCRIPT_DIR}/WoW.mfil" ]]; then
        cp "${SCRIPT_DIR}/WoW.mfil" "${BUNDLE_DIR}/"
    fi
    if [[ -d "${SCRIPT_DIR}/data" ]]; then
        cp -r "${SCRIPT_DIR}/data" "${BUNDLE_DIR}/"
    fi

    tar -czf "$TARBALL_PATH" -C "$DIST_DIR" "$BUNDLE_NAME"
    echo -e "${GREEN}${BOLD}✓ Distribution bundle created:${NC} ${YELLOW}${TARBALL_PATH}${NC}"
fi

# Run binary if requested
if [[ $RUN_AFTER -eq 1 ]]; then
    echo -e "${CYAN}Launching LeLauncher...${NC}"
    exec "${BUILD_DIR}/LeLauncher"
fi
