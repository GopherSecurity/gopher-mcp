#!/bin/bash

# Cross-compilation build script for libgopher_mcp_auth on macOS ARM64
# For building ARM64 binaries on Intel Macs
# Target: macOS 11.0+ (Big Sur and later for Apple Silicon)
# Architecture: arm64

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}Cross-Compiling libgopher_mcp_auth for macOS ARM64${NC}"
echo -e "${CYAN}Building on: $(uname -m)${NC}"
echo -e "${CYAN}Target: macOS 11.0+ (arm64/Apple Silicon)${NC}"
echo -e "${CYAN}========================================${NC}"

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Build configuration
BUILD_DIR="${PROJECT_ROOT}/build-mac-arm64-cross"
OUTPUT_DIR="${PROJECT_ROOT}/build-output/mac-arm64"
MIN_MACOS_VERSION="11.0"  # Minimum version for Apple Silicon

# Clean previous builds
echo -e "${YELLOW}Cleaning previous builds...${NC}"
rm -rf "$BUILD_DIR"
rm -rf "$OUTPUT_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$OUTPUT_DIR"

# Navigate to build directory
cd "$BUILD_DIR"

# Create a custom toolchain file for cross-compilation
echo -e "${YELLOW}Creating cross-compilation toolchain file...${NC}"
cat > toolchain-arm64.cmake << 'EOF'
# Toolchain file for cross-compiling to macOS ARM64
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)

# Force the compiler to generate ARM64 code
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "")

# Use system compiler but force ARM64
set(CMAKE_C_FLAGS "-arch arm64" CACHE STRING "")
set(CMAKE_CXX_FLAGS "-arch arm64" CACHE STRING "")

# Disable runtime checks that would fail during cross-compilation
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
EOF

# Use system libraries (they should be universal or we'll static link)
echo -e "${YELLOW}Configuring for cross-compilation to ARM64...${NC}"

# Try to find OpenSSL that we can use
OPENSSL_LIBS=""
OPENSSL_INCLUDES=""

# Check for MacPorts universal OpenSSL
if [ -d "/opt/local/lib" ] && [ -f "/opt/local/lib/libssl.dylib" ]; then
    echo "  Checking MacPorts OpenSSL..."
    if lipo -info /opt/local/lib/libssl.dylib 2>/dev/null | grep -q arm64; then
        echo "  ✓ Found universal OpenSSL in MacPorts"
        OPENSSL_LIBS="/opt/local/lib"
        OPENSSL_INCLUDES="/opt/local/include"
    fi
fi

# Check for Homebrew universal OpenSSL (rare but possible)
if [ -z "$OPENSSL_LIBS" ] && [ -f "/usr/local/opt/openssl/lib/libssl.dylib" ]; then
    echo "  Checking Homebrew OpenSSL..."
    if lipo -info /usr/local/opt/openssl/lib/libssl.dylib 2>/dev/null | grep -q arm64; then
        echo "  ✓ Found universal OpenSSL in Homebrew"
        OPENSSL_LIBS="/usr/local/opt/openssl/lib"
        OPENSSL_INCLUDES="/usr/local/opt/openssl/include"
    fi
fi

# If no universal OpenSSL found, we'll try static linking
if [ -z "$OPENSSL_LIBS" ]; then
    echo -e "${YELLOW}  No universal OpenSSL found, will attempt static linking${NC}"
    
    # Download and build OpenSSL for ARM64 if needed
    echo -e "${YELLOW}Building OpenSSL for ARM64...${NC}"
    
    OPENSSL_BUILD_DIR="${BUILD_DIR}/openssl-arm64"
    mkdir -p "$OPENSSL_BUILD_DIR"
    cd "$OPENSSL_BUILD_DIR"
    
    # Download OpenSSL if not present
    if [ ! -f "openssl-3.0.11.tar.gz" ]; then
        echo "  Downloading OpenSSL 3.0.11..."
        curl -LO https://www.openssl.org/source/openssl-3.0.11.tar.gz
        tar -xzf openssl-3.0.11.tar.gz
    fi
    
    cd openssl-3.0.11
    
    # Configure for ARM64 static libraries
    echo "  Configuring OpenSSL for ARM64..."
    ./Configure darwin64-arm64-cc \
        --prefix="${OPENSSL_BUILD_DIR}/install" \
        no-shared \
        no-tests \
        -mmacosx-version-min=11.0
    
    echo "  Building OpenSSL (this may take a few minutes)..."
    make -j$(sysctl -n hw.ncpu) >/dev/null 2>&1
    make install_sw >/dev/null 2>&1
    
    OPENSSL_LIBS="${OPENSSL_BUILD_DIR}/install/lib"
    OPENSSL_INCLUDES="${OPENSSL_BUILD_DIR}/install/include"
    
    echo "  ✓ OpenSSL built for ARM64"
fi

cd "$BUILD_DIR"

# Configure CMake with cross-compilation settings
echo -e "${YELLOW}Configuring CMake for ARM64 cross-compilation...${NC}"

CMAKE_ARGS=(
    -DCMAKE_TOOLCHAIN_FILE="${BUILD_DIR}/toolchain-arm64.cmake"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_CXX_STANDARD=11
    -DBUILD_SHARED_LIBS=ON
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/install"
    -DCMAKE_MACOSX_RPATH=ON
    -DCMAKE_INSTALL_RPATH="@loader_path"
)

# Add OpenSSL paths
if [ -n "$OPENSSL_LIBS" ]; then
    # Check if we have static or dynamic libraries
    if [ -f "${OPENSSL_LIBS}/libssl.a" ]; then
        echo "  Using static OpenSSL libraries"
        CMAKE_ARGS+=(
            -DOPENSSL_USE_STATIC_LIBS=TRUE
            -DOPENSSL_SSL_LIBRARY="${OPENSSL_LIBS}/libssl.a"
            -DOPENSSL_CRYPTO_LIBRARY="${OPENSSL_LIBS}/libcrypto.a"
            -DOPENSSL_INCLUDE_DIR="${OPENSSL_INCLUDES}"
        )
    else
        echo "  Using dynamic OpenSSL libraries"
        CMAKE_ARGS+=(
            -DOPENSSL_SSL_LIBRARY="${OPENSSL_LIBS}/libssl.dylib"
            -DOPENSSL_CRYPTO_LIBRARY="${OPENSSL_LIBS}/libcrypto.dylib"
            -DOPENSSL_INCLUDE_DIR="${OPENSSL_INCLUDES}"
        )
    fi
fi

# Handle curl - use SDK version for cross-compilation
MACOS_SDK_PATH=$(xcrun --sdk macosx --show-sdk-path)
CMAKE_ARGS+=(
    -DCURL_LIBRARY="${MACOS_SDK_PATH}/usr/lib/libcurl.tbd"
    -DCURL_INCLUDE_DIR="${MACOS_SDK_PATH}/usr/include"
)

cmake "${CMAKE_ARGS[@]}" "${PROJECT_ROOT}/src/auth"

# Build the library
echo -e "${YELLOW}Building library...${NC}"
make -j$(sysctl -n hw.ncpu) VERBOSE=1

# Install to temporary directory
make install

# Copy output files
echo -e "${YELLOW}Organizing output files...${NC}"
cp "${BUILD_DIR}/install/lib/libgopher_mcp_auth.0.1.0.dylib" "${OUTPUT_DIR}/"

# Create symlink for compatibility
ln -sf libgopher_mcp_auth.0.1.0.dylib "${OUTPUT_DIR}/libgopher_mcp_auth.dylib"

# Build verification app for macOS ARM64
echo -e "${YELLOW}Building verification app...${NC}"
cd "${OUTPUT_DIR}"

# Use the verification source
VERIFY_SIMPLE="${SCRIPT_DIR}/mac-x64/verify_auth.c"
if [ ! -f "$VERIFY_SIMPLE" ]; then
    # Create a minimal verification program
    cat > verify_auth.c << 'EOF'
#include <stdio.h>
#include <dlfcn.h>

int main() {
    void* handle = dlopen("./libgopher_mcp_auth.dylib", RTLD_LAZY);
    if (!handle) {
        printf("Failed to load library: %s\n", dlerror());
        return 1;
    }
    
    // Try to get version function
    typedef const char* (*version_func)();
    version_func get_version = (version_func)dlsym(handle, "mcp_auth_version");
    
    if (get_version) {
        printf("Library version: %s\n", get_version());
    } else {
        printf("Library loaded but version function not found\n");
    }
    
    dlclose(handle);
    printf("✓ Verification successful\n");
    return 0;
}
EOF
else
    cp "$VERIFY_SIMPLE" verify_auth.c
fi

# Cross-compile verification app
echo "  Cross-compiling verification tool for ARM64..."
cc -arch arm64 -mmacosx-version-min=11.0 -o verify_auth verify_auth.c
rm -f verify_auth.c

echo "  ✓ Created verify_auth (ARM64)"

# Clean up build directory
cd "$PROJECT_ROOT"
echo -e "${YELLOW}Cleaning up build directory...${NC}"
rm -rf "$BUILD_DIR"

# Verify the output
echo ""
echo -e "${YELLOW}Verifying output...${NC}"
cd "$OUTPUT_DIR"

if [ -f "libgopher_mcp_auth.0.1.0.dylib" ] && [ -f "verify_auth" ]; then
    echo -e "${GREEN}✅ Cross-compilation successful!${NC}"
    echo ""
    echo "Output files:"
    echo "------------------------------------"
    ls -lah
    echo ""
    
    # Show library info
    echo "Library information:"
    file libgopher_mcp_auth.0.1.0.dylib
    echo ""
    
    # Show architecture
    echo "Architecture:"
    lipo -info libgopher_mcp_auth.0.1.0.dylib
    echo ""
    
    # Check dependencies
    echo "Library dependencies:"
    otool -L libgopher_mcp_auth.0.1.0.dylib | head -10
    echo ""
    
    echo -e "${GREEN}📦 Output contains:${NC}"
    echo "  - libgopher_mcp_auth.0.1.0.dylib (ARM64 library)"
    echo "  - libgopher_mcp_auth.dylib (symlink)"
    echo "  - verify_auth (ARM64 verification tool)"
    echo ""
    
    echo -e "${CYAN}Note: These binaries are cross-compiled for ARM64${NC}"
    echo "They can only be tested on Apple Silicon Macs"
else
    echo -e "${RED}❌ Build failed - required files not found${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}✨ Cross-compilation complete!${NC}"
echo ""
echo "To use on Apple Silicon Macs:"
echo "  1. Copy build-output/mac-arm64/ to an M1/M2/M3 Mac"
echo "  2. Run: ./verify_auth"
echo ""
echo "For universal binary that works on both architectures:"
echo "  ./docker/build-mac-universal.sh"
echo ""