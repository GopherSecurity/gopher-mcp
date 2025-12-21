#!/bin/bash

# Build script for libgopher_mcp_auth on Linux x86_64
# Can be run natively on Linux or via Docker

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Building libgopher_mcp_auth for Linux x86_64${NC}"
echo -e "${BLUE}========================================${NC}"

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Detect if we're running in Docker or native Ubuntu
IS_DOCKER=false
IS_NATIVE_UBUNTU=false

if [ -f /.dockerenv ]; then
    IS_DOCKER=true
    echo -e "${YELLOW}Running in Docker container${NC}"
elif [ -f /etc/os-release ]; then
    . /etc/os-release
    if [[ "$ID" == "ubuntu" ]] && [[ "$VERSION_ID" == "20.04" ]]; then
        IS_NATIVE_UBUNTU=true
        echo -e "${YELLOW}Running on native Ubuntu 20.04${NC}"
    fi
fi

# Build configuration
BUILD_DIR="${PROJECT_ROOT}/build-linux-x64"
OUTPUT_DIR="${PROJECT_ROOT}/build-output/linux-x64"

# Function to install dependencies on Ubuntu
install_dependencies() {
    echo -e "${YELLOW}Installing build dependencies...${NC}"
    
    # Check if running with sudo privileges
    if [ "$EUID" -ne 0 ]; then 
        echo "Need sudo privileges to install packages"
        SUDO="sudo"
    else
        SUDO=""
    fi
    
    $SUDO apt-get update
    $SUDO apt-get install -y \
        build-essential \
        cmake \
        git \
        curl \
        libcurl4-openssl-dev \
        libssl-dev \
        pkg-config
}

# Function to build using Docker
build_with_docker() {
    echo -e "${YELLOW}Building with Docker...${NC}"
    
    # Create output directory
    mkdir -p "$OUTPUT_DIR"
    
    # Build using Docker
    docker build \
        -t mcp-auth:linux-x64 \
        -f "$SCRIPT_DIR/Dockerfile.linux-x64" \
        "$PROJECT_ROOT"
    
    if [ $? -ne 0 ]; then
        echo -e "${RED}Docker build failed${NC}"
        exit 1
    fi
    
    # Create container and copy files
    echo -e "${YELLOW}Extracting built files...${NC}"
    CONTAINER_ID=$(docker create mcp-auth:linux-x64)
    docker cp "$CONTAINER_ID:/output/." "$OUTPUT_DIR/"
    docker rm "$CONTAINER_ID" > /dev/null
}

# Function to build natively
build_native() {
    echo -e "${YELLOW}Building natively on Ubuntu 20.04...${NC}"
    
    # Clean previous builds
    echo -e "${YELLOW}Cleaning previous builds...${NC}"
    rm -rf "$BUILD_DIR"
    rm -rf "$OUTPUT_DIR"
    mkdir -p "$BUILD_DIR"
    mkdir -p "$OUTPUT_DIR"
    
    # Navigate to build directory
    cd "$BUILD_DIR"
    
    # Configure CMake
    echo -e "${YELLOW}Configuring CMake for x86_64...${NC}"
    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_STANDARD=11 \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/install" \
        "${PROJECT_ROOT}/src/auth"
    
    # Build the library
    echo -e "${YELLOW}Building library...${NC}"
    make -j$(nproc)
    
    # Install to temporary directory
    make install
    
    # Copy output files
    echo -e "${YELLOW}Organizing output files...${NC}"
    cp "${BUILD_DIR}/install/lib/libgopher_mcp_auth.so"* "${OUTPUT_DIR}/"
    
    # Create proper symlinks
    cd "${OUTPUT_DIR}"
    if [ -f "libgopher_mcp_auth.so" ]; then
        # Rename the main library to versioned name
        mv libgopher_mcp_auth.so libgopher_mcp_auth.so.0.1.0
    fi
    ln -sf libgopher_mcp_auth.so.0.1.0 libgopher_mcp_auth.so.0
    ln -sf libgopher_mcp_auth.so.0 libgopher_mcp_auth.so
    
    # Build verification app
    echo -e "${YELLOW}Building verification app...${NC}"
    cd "${OUTPUT_DIR}"
    
    # Create or copy verification source
    if [ -f "${SCRIPT_DIR}/linux-x64/verify_auth.c" ]; then
        cp "${SCRIPT_DIR}/linux-x64/verify_auth.c" verify_auth.c
    else
        # Create a minimal verification program
        cat > verify_auth.c << 'EOF'
#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>

int main() {
    printf("====================================\n");
    printf("libgopher_mcp_auth Verification Tool\n");
    printf("====================================\n\n");
    
    // Try to load the library
    void* handle = dlopen("./libgopher_mcp_auth.so", RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Failed to load library: %s\n", dlerror());
        return 1;
    }
    
    printf("✓ Library loaded successfully\n");
    
    // Try to get version function
    typedef const char* (*version_func)();
    version_func get_version = (version_func)dlsym(handle, "mcp_auth_version");
    
    if (get_version) {
        const char* version = get_version();
        if (version) {
            printf("✓ Library version: %s\n", version);
        }
    }
    
    // Check for main functions
    const char* functions[] = {
        "mcp_auth_init",
        "mcp_auth_client_create",
        "mcp_auth_client_destroy",
        "mcp_auth_validate_token",
        NULL
    };
    
    printf("\nChecking exported functions:\n");
    for (int i = 0; functions[i]; i++) {
        void* func = dlsym(handle, functions[i]);
        if (func) {
            printf("  ✓ %s found\n", functions[i]);
        } else {
            printf("  ✗ %s not found\n", functions[i]);
        }
    }
    
    dlclose(handle);
    
    printf("\n====================================\n");
    printf("✓ Verification complete!\n");
    printf("====================================\n");
    
    return 0;
}
EOF
    fi
    
    # Compile verification app
    gcc -o verify_auth verify_auth.c \
        -std=gnu11 \
        -D_GNU_SOURCE \
        -ldl \
        -O2
    
    if [ -f "verify_auth.c" ]; then
        rm verify_auth.c
    fi
    
    echo "  ✓ Created verify_auth"
    
    # Clean up build directory
    cd "$PROJECT_ROOT"
    echo -e "${YELLOW}Cleaning up build directory...${NC}"
    rm -rf "$BUILD_DIR"
}

# Main build logic
if [ "$IS_NATIVE_UBUNTU" = true ]; then
    # Check if dependencies are installed
    if ! command -v cmake &> /dev/null; then
        echo -e "${YELLOW}CMake not found, installing dependencies...${NC}"
        install_dependencies
    fi
    
    build_native
elif command -v docker &> /dev/null; then
    # Use Docker if available
    build_with_docker
else
    echo -e "${RED}Error: Not running on Ubuntu 20.04 and Docker is not available${NC}"
    echo "Please either:"
    echo "  1. Run this script on Ubuntu 20.04"
    echo "  2. Install Docker"
    echo "  3. Use the Docker-based build script: ./build-linux-x64.sh"
    exit 1
fi

# Verify the output
echo ""
echo -e "${YELLOW}Verifying output...${NC}"
cd "$OUTPUT_DIR"

if [ -f "libgopher_mcp_auth.so.0.1.0" ] && [ -f "verify_auth" ]; then
    echo -e "${GREEN}✅ Build successful!${NC}"
    echo ""
    echo "Output files:"
    echo "------------------------------------"
    ls -lah
    echo ""
    
    # Show library info
    echo "Library information:"
    file libgopher_mcp_auth.so.0.1.0
    echo ""
    
    # Show library dependencies
    echo "Library dependencies:"
    ldd libgopher_mcp_auth.so.0.1.0 || true
    echo ""
    
    echo -e "${GREEN}📦 Output contains:${NC}"
    echo "  - libgopher_mcp_auth.so.0.1.0 (main library)"
    echo "  - libgopher_mcp_auth.so.0 (soname symlink)"
    echo "  - libgopher_mcp_auth.so (development symlink)"
    echo "  - verify_auth (verification tool)"
    echo ""
    
    # Test verification app if on compatible system
    if [[ "$(uname -m)" == "x86_64" ]] && [[ "$(uname -s)" == "Linux" ]]; then
        echo -e "${YELLOW}Testing verification app...${NC}"
        if LD_LIBRARY_PATH=. ./verify_auth; then
            echo -e "${GREEN}✓ Verification test passed${NC}"
        else
            echo -e "${YELLOW}⚠ Verification test failed${NC}"
        fi
    fi
else
    echo -e "${RED}❌ Build failed - required files not found${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}✨ Build complete!${NC}"
echo ""
echo "To use the library:"
echo "  1. Copy build-output/linux-x64/ to your Ubuntu 20.04 system"
echo "  2. Run: LD_LIBRARY_PATH=. ./verify_auth"
echo ""