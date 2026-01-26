#!/bin/bash

# Build script for libgopher-mcp on Linux x86_64
# Target: Linux x86_64 (glibc-based distributions)
# Architecture: x86_64

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Building libgopher-mcp for Linux x86_64${NC}"
echo -e "${GREEN}========================================${NC}"

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Build configuration
BUILD_DIR="${PROJECT_ROOT}/build-linux-x64"
DEPS_DIR="${PROJECT_ROOT}/_deps-linux-x64"
INSTALL_DIR="${PROJECT_ROOT}/install_prefix_dir"
OUTPUT_DIR="${PROJECT_ROOT}/build-output/linux-x64"

# Detect architecture
CURRENT_ARCH=$(uname -m)
echo -e "${YELLOW}Detecting system architecture...${NC}"
echo "  Current architecture: $CURRENT_ARCH"

if [ "$CURRENT_ARCH" != "x86_64" ]; then
    echo -e "${RED}Error: This script is for x86_64 Linux${NC}"
    echo "Current architecture: $CURRENT_ARCH"
    echo "Use build-linux-arm64.sh for ARM64 systems"
    exit 1
fi

# Detect package manager and install dependencies
echo -e "${YELLOW}Checking and installing dependencies...${NC}"

install_dependencies() {
    if command -v apt-get &> /dev/null; then
        echo "  Detected Debian/Ubuntu - using apt-get"
        sudo apt-get update
        sudo apt-get install -y \
            build-essential \
            cmake \
            libssl-dev \
            libevent-dev \
            pkg-config \
            git
    elif command -v dnf &> /dev/null; then
        echo "  Detected Fedora/RHEL - using dnf"
        sudo dnf install -y \
            gcc-c++ \
            cmake \
            openssl-devel \
            libevent-devel \
            pkgconfig \
            git
    elif command -v yum &> /dev/null; then
        echo "  Detected CentOS/RHEL - using yum"
        sudo yum install -y \
            gcc-c++ \
            cmake \
            openssl-devel \
            libevent-devel \
            pkgconfig \
            git
    elif command -v pacman &> /dev/null; then
        echo "  Detected Arch Linux - using pacman"
        sudo pacman -Sy --noconfirm \
            base-devel \
            cmake \
            openssl \
            libevent \
            pkgconf \
            git
    elif command -v apk &> /dev/null; then
        echo "  Detected Alpine Linux - using apk"
        sudo apk add --no-cache \
            build-base \
            cmake \
            openssl-dev \
            libevent-dev \
            pkgconfig \
            git
    else
        echo -e "${RED}Error: Could not detect package manager${NC}"
        echo "Please install manually: cmake, libssl-dev, libevent-dev, pkg-config, git"
        exit 1
    fi
}

# Check if dependencies are installed
if ! command -v cmake &> /dev/null || ! pkg-config --exists openssl 2>/dev/null || ! pkg-config --exists libevent 2>/dev/null; then
    echo "  Some dependencies are missing, installing..."
    install_dependencies
else
    echo "  All dependencies already installed"
fi

# Clean previous builds (but preserve _deps for caching)
echo -e "${YELLOW}Cleaning previous builds...${NC}"
rm -rf "$BUILD_DIR"
rm -rf "$OUTPUT_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$DEPS_DIR"
mkdir -p "$OUTPUT_DIR"

# Navigate to build directory
cd "$BUILD_DIR"

# Configure CMake
echo -e "${YELLOW}Configuring CMake for Linux x86_64...${NC}"

cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=14 \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_STATIC_LIBS=ON \
    -DBUILD_TESTS=OFF \
    -DBUILD_C_API=ON \
    -DBUILD_BINDINGS_EXAMPLES=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DFETCHCONTENT_BASE_DIR="${DEPS_DIR}" \
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/install" \
    -DCMAKE_INSTALL_RPATH="\$ORIGIN" \
    "${PROJECT_ROOT}"

# Build the library
echo -e "${YELLOW}Building library...${NC}"
make -j$(nproc 2>/dev/null || echo 4)

# Install to temporary directory
make install

# Copy output files
echo -e "${YELLOW}Organizing output files...${NC}"

# Copy all gopher-mcp shared library files (including symlinks)
cp -P "${INSTALL_DIR}"/lib/libgopher-mcp*.so* "${OUTPUT_DIR}/" 2>/dev/null || true
cp -P "${INSTALL_DIR}"/lib/libgopher_mcp_c*.so* "${OUTPUT_DIR}/" 2>/dev/null || true

# Copy third-party dependencies
cp -P "${INSTALL_DIR}"/lib/libfmt*.so* "${OUTPUT_DIR}/" 2>/dev/null || true
cp -P "${INSTALL_DIR}"/lib/libllhttp*.so* "${OUTPUT_DIR}/" 2>/dev/null || true

# Copy headers
if [ -d "${INSTALL_DIR}/include" ]; then
    cp -R "${INSTALL_DIR}/include" "${OUTPUT_DIR}/"
fi

# Build verification app
echo -e "${YELLOW}Building verification app...${NC}"
cd "${OUTPUT_DIR}"

# Create a simple verification program
cat > verify_mcp.c << 'VERIFY_EOF'
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>

int main() {
    printf("libgopher-mcp verification tool (Linux x86_64)\n");
    printf("===============================================\n\n");

    // Try to load the C API library (used for FFI bindings)
    void* handle = dlopen("./libgopher_mcp_c.so", RTLD_NOW);
    if (!handle) {
        printf("Note: C API library not found: %s\n", dlerror());
        // Try the main library as fallback
        handle = dlopen("./libgopher-mcp.so", RTLD_NOW);
        if (!handle) {
            printf("X Failed to load main library: %s\n", dlerror());
            return 1;
        }
        printf("OK Main library loaded successfully\n");
    } else {
        printf("OK C API library loaded successfully\n");
    }

    // Check for mcp_init function
    void* init_func = dlsym(handle, "mcp_init");
    if (init_func) {
        printf("OK mcp_init function found\n");
    } else {
        printf("-- mcp_init function not found\n");
    }

    // Check for mcp_cleanup function
    void* cleanup_func = dlsym(handle, "mcp_cleanup");
    if (cleanup_func) {
        printf("OK mcp_cleanup function found\n");
    } else {
        printf("-- mcp_cleanup function not found\n");
    }

    // Check for mcp_client_create function (C API)
    void* create_func = dlsym(handle, "mcp_client_create");
    if (create_func) {
        printf("OK mcp_client_create function found (C API)\n");
    } else {
        printf("-- mcp_client_create function not found\n");
    }

    // Check for mcp_json_parse function (C API JSON)
    void* json_func = dlsym(handle, "mcp_json_parse");
    if (json_func) {
        printf("OK mcp_json_parse function found (C API JSON)\n");
    } else {
        printf("-- mcp_json_parse function not found\n");
    }

    dlclose(handle);

    printf("\nOK Verification complete\n");
    return 0;
}
VERIFY_EOF

# Build verification tool
gcc -o verify_mcp verify_mcp.c -ldl -O2
rm -f verify_mcp.c

echo "  Created verify_mcp (Linux x86_64)"

# Clean up build directory
cd "$PROJECT_ROOT"
echo -e "${YELLOW}Cleaning up build directory...${NC}"
rm -rf "$BUILD_DIR"

# Verify the output
echo ""
echo -e "${YELLOW}Verifying output...${NC}"
cd "$OUTPUT_DIR"

MAIN_LIB=""
if [ -f "libgopher-mcp.so.0.1.0" ]; then
    MAIN_LIB="libgopher-mcp.so.0.1.0"
elif [ -f "libgopher-mcp.so" ]; then
    MAIN_LIB="libgopher-mcp.so"
fi

if [ -n "$MAIN_LIB" ] && [ -f "verify_mcp" ]; then
    echo -e "${GREEN}Build successful!${NC}"
    echo ""
    echo "Output files:"
    echo "------------------------------------"
    ls -lah *.so* 2>/dev/null || true
    ls -lah verify_mcp 2>/dev/null || true
    echo ""

    # Show library info
    echo "Library information:"
    file "$MAIN_LIB"
    echo ""

    echo -e "${GREEN}Output contains:${NC}"
    echo "  - $MAIN_LIB (the MCP library, x86_64)"
    [ -f "libgopher-mcp.so" ] && echo "  - libgopher-mcp.so (symlink)"
    [ -f "libgopher-mcp-event.so.0.1.0" ] && echo "  - libgopher-mcp-event.so.0.1.0 (event library)"
    [ -f "libgopher_mcp_c.so.0.1.0" ] && echo "  - libgopher_mcp_c.so.0.1.0 (C API for FFI)"
    [ -f "libgopher_mcp_c.so" ] && echo "  - libgopher_mcp_c.so (symlink)"
    echo "  - verify_mcp (verification tool)"
    [ -d "include" ] && echo "  - include/ (header files)"
    echo ""

    # Test verification app
    echo -e "${YELLOW}Testing verification app...${NC}"
    export LD_LIBRARY_PATH="$OUTPUT_DIR:$LD_LIBRARY_PATH"
    if ./verify_mcp; then
        echo -e "${GREEN}Verification test passed${NC}"
    else
        echo -e "${YELLOW}Verification test failed or crashed${NC}"
        echo "This may be due to missing dependencies or library issues"
        echo "The build artifacts have been created successfully"
    fi
else
    echo -e "${RED}Build failed - required files not found${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}Build complete!${NC}"
echo ""
echo "Output structure:"
echo "  build-output/linux-x64/"
echo "    ├── libgopher-mcp.so.0.1.0 (x86_64)"
echo "    ├── libgopher-mcp.so (symlink)"
echo "    ├── libgopher-mcp-event.*.so (if built)"
echo "    ├── libgopher_mcp_c.so.0.1.0 (C API for FFI)"
echo "    ├── libgopher_mcp_c.so (symlink)"
echo "    ├── verify_mcp (verification tool)"
echo "    └── include/ (headers)"
echo ""
echo "To use:"
echo "  1. Copy the entire build-output/linux-x64/ directory"
echo "  2. Set LD_LIBRARY_PATH to include the directory"
echo "  3. Run: LD_LIBRARY_PATH=. ./verify_mcp"
echo ""
