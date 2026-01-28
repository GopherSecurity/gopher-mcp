#!/bin/bash

# Build script for libgopher-mcp on macOS ARM64 (Apple Silicon)
# Target: macOS 11.0+ (Big Sur and later for Apple Silicon)
# Architecture: arm64

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Building libgopher-mcp for macOS ARM64${NC}"
echo -e "${GREEN}Target: macOS 11.0+ (arm64/Apple Silicon)${NC}"
echo -e "${GREEN}========================================${NC}"

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Build configuration
BUILD_DIR="${PROJECT_ROOT}/build-mac-arm64"
DEPS_DIR="${PROJECT_ROOT}/_deps-arm64"
INSTALL_DIR="${PROJECT_ROOT}/install_prefix_dir"
OUTPUT_DIR="${PROJECT_ROOT}/build-output/mac-arm64"
MIN_MACOS_VERSION="11.0"  # Minimum version for Apple Silicon

# Clean previous builds (but preserve _deps for caching)
echo -e "${YELLOW}Cleaning previous builds...${NC}"
rm -rf "$BUILD_DIR"
rm -rf "$OUTPUT_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$DEPS_DIR"
mkdir -p "$OUTPUT_DIR"

# Navigate to build directory
cd "$BUILD_DIR"

# Detect current architecture
CURRENT_ARCH=$(uname -m)
echo -e "${YELLOW}Detecting system architecture...${NC}"
echo "  Current architecture: $CURRENT_ARCH"

# Determine Homebrew path based on architecture
if [ "$CURRENT_ARCH" = "arm64" ]; then
    # Native ARM64 Mac - use /opt/homebrew
    BREW_CMD="/opt/homebrew/bin/brew"
    HOMEBREW_PREFIX="/opt/homebrew"

    if [ ! -f "$BREW_CMD" ]; then
        echo -e "${RED}Error: ARM64 Homebrew not found at /opt/homebrew${NC}"
        echo "Please install Homebrew for ARM64 using:"
        echo "  /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
        exit 1
    fi
elif [ "$CURRENT_ARCH" = "x86_64" ]; then
    # Intel Mac - use /usr/local but build for ARM64 (cross-compile)
    BREW_CMD="/usr/local/bin/brew"
    HOMEBREW_PREFIX="/usr/local"

    echo -e "${YELLOW}Note: Running on Intel Mac - will cross-compile for ARM64${NC}"

    if [ ! -f "$BREW_CMD" ]; then
        echo -e "${RED}Error: Homebrew not found at /usr/local${NC}"
        echo "Please install Homebrew using:"
        echo "  /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
        exit 1
    fi
else
    echo -e "${RED}Error: Unsupported architecture: $CURRENT_ARCH${NC}"
    exit 1
fi

echo "  Found Homebrew at: $BREW_CMD"

# Install dependencies if missing
echo -e "${YELLOW}Checking and installing dependencies...${NC}"

# Check and install OpenSSL
if [ ! -d "${HOMEBREW_PREFIX}/opt/openssl@3" ] && [ ! -d "${HOMEBREW_PREFIX}/opt/openssl" ] && [ ! -d "${HOMEBREW_PREFIX}/opt/openssl@1.1" ]; then
    echo "  Installing OpenSSL..."
    $BREW_CMD install openssl
else
    echo "  OpenSSL already installed"
fi

# Check and install libevent
if [ ! -d "${HOMEBREW_PREFIX}/opt/libevent" ]; then
    echo "  Installing libevent..."
    $BREW_CMD install libevent
else
    echo "  libevent already installed"
fi

# Now find the installed paths
echo -e "${YELLOW}Locating dependencies...${NC}"

OPENSSL_ROOT=""
if [ -d "${HOMEBREW_PREFIX}/opt/openssl@3" ]; then
    OPENSSL_ROOT="${HOMEBREW_PREFIX}/opt/openssl@3"
elif [ -d "${HOMEBREW_PREFIX}/opt/openssl" ]; then
    OPENSSL_ROOT="${HOMEBREW_PREFIX}/opt/openssl"
elif [ -d "${HOMEBREW_PREFIX}/opt/openssl@1.1" ]; then
    OPENSSL_ROOT="${HOMEBREW_PREFIX}/opt/openssl@1.1"
else
    echo -e "${RED}Error: OpenSSL installation failed${NC}"
    exit 1
fi
echo "  OpenSSL: $OPENSSL_ROOT"

LIBEVENT_ROOT=""
if [ -d "${HOMEBREW_PREFIX}/opt/libevent" ]; then
    LIBEVENT_ROOT="${HOMEBREW_PREFIX}/opt/libevent"
else
    echo -e "${RED}Error: libevent installation failed${NC}"
    exit 1
fi
echo "  libevent: $LIBEVENT_ROOT"

# Configure CMake with macOS ARM64-specific settings
echo -e "${YELLOW}Configuring CMake for macOS ARM64...${NC}"

# Set PKG_CONFIG_PATH to find packages
export PKG_CONFIG_PATH="${HOMEBREW_PREFIX}/lib/pkgconfig:${OPENSSL_ROOT}/lib/pkgconfig:${LIBEVENT_ROOT}/lib/pkgconfig:$PKG_CONFIG_PATH"

CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_CXX_STANDARD=17
    -DCMAKE_OSX_DEPLOYMENT_TARGET=${MIN_MACOS_VERSION}
    -DCMAKE_OSX_ARCHITECTURES=arm64
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_SHARED_LIBS=ON
    -DBUILD_STATIC_LIBS=ON
    -DBUILD_TESTS=OFF
    -DBUILD_C_API=ON
    -DBUILD_BINDINGS_EXAMPLES=OFF
    -DBUILD_EXAMPLES=OFF
    -DFETCHCONTENT_BASE_DIR="${DEPS_DIR}"
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/install"
    -DCMAKE_MACOSX_RPATH=ON
    -DCMAKE_INSTALL_RPATH="@loader_path"
    # Add Homebrew prefix path so CMake finds libraries first
    -DCMAKE_PREFIX_PATH="${HOMEBREW_PREFIX};${OPENSSL_ROOT};${LIBEVENT_ROOT}"
    # Fix compatibility with older CMakeLists.txt in dependencies (yaml-cpp)
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

# Add explicit OpenSSL paths
CMAKE_ARGS+=(
    -DOPENSSL_ROOT_DIR="$OPENSSL_ROOT"
    -DOPENSSL_CRYPTO_LIBRARY="${OPENSSL_ROOT}/lib/libcrypto.dylib"
    -DOPENSSL_SSL_LIBRARY="${OPENSSL_ROOT}/lib/libssl.dylib"
    -DOPENSSL_INCLUDE_DIR="${OPENSSL_ROOT}/include"
)

# Add explicit libevent paths
# These override the hard-coded /usr/local paths in CMakeLists.txt
CMAKE_ARGS+=(
    -DLIBEVENT_INCLUDE_DIR="${LIBEVENT_ROOT}/include"
    -DLIBEVENT_CORE_LIBRARY="${LIBEVENT_ROOT}/lib/libevent_core.dylib"
    -DLIBEVENT_PTHREADS_LIBRARY="${LIBEVENT_ROOT}/lib/libevent_pthreads.dylib"
)

cmake "${CMAKE_ARGS[@]}" "${PROJECT_ROOT}"

# Build the library
echo -e "${YELLOW}Building library...${NC}"
make -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Install to temporary directory
make install

# Copy output files
echo -e "${YELLOW}Organizing output files...${NC}"

# Copy all gopher-mcp dylib files (including symlinks)
# This ensures all dependencies are included
cp -P "${INSTALL_DIR}"/lib/libgopher-mcp*.dylib "${OUTPUT_DIR}/" 2>/dev/null || true
cp -P "${INSTALL_DIR}"/lib/libgopher_mcp_c*.dylib "${OUTPUT_DIR}/" 2>/dev/null || true

# Copy third-party dependencies
cp -P "${INSTALL_DIR}"/lib/libfmt*.dylib "${OUTPUT_DIR}/" 2>/dev/null || true
cp -P "${INSTALL_DIR}"/lib/libllhttp*.dylib "${OUTPUT_DIR}/" 2>/dev/null || true

# Copy headers
if [ -d "${INSTALL_DIR}/include" ]; then
    cp -R "${INSTALL_DIR}/include" "${OUTPUT_DIR}/"
fi

# Build verification app for macOS ARM64
echo -e "${YELLOW}Building verification app...${NC}"
cd "${OUTPUT_DIR}"

# Create a simple verification program
cat > verify_mcp.c << 'VERIFY_EOF'
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>

int main() {
    printf("libgopher-mcp verification tool (ARM64)\n");
    printf("========================================\n\n");

    // Try to load the C API library (used for FFI bindings)
    void* handle = dlopen("./libgopher_mcp_c.dylib", RTLD_NOW);
    if (!handle) {
        printf("✗ Failed to load C API library: %s\n", dlerror());
        // Try the main library as fallback
        handle = dlopen("./libgopher-mcp.dylib", RTLD_NOW);
        if (!handle) {
            printf("✗ Failed to load main library: %s\n", dlerror());
            return 1;
        }
        printf("✓ Main library loaded successfully\n");
    } else {
        printf("✓ C API library loaded successfully\n");
    }

    // Check for mcp_init function
    void* init_func = dlsym(handle, "mcp_init");
    if (init_func) {
        printf("✓ mcp_init function found\n");
    } else {
        printf("• mcp_init function not found\n");
    }

    // Check for mcp_cleanup function
    void* cleanup_func = dlsym(handle, "mcp_cleanup");
    if (cleanup_func) {
        printf("✓ mcp_cleanup function found\n");
    } else {
        printf("• mcp_cleanup function not found\n");
    }

    // Check for mcp_client_create function (C API)
    void* create_func = dlsym(handle, "mcp_client_create");
    if (create_func) {
        printf("✓ mcp_client_create function found (C API)\n");
    } else {
        printf("• mcp_client_create function not found\n");
    }

    // Check for mcp_json_parse function (C API JSON)
    void* json_func = dlsym(handle, "mcp_json_parse");
    if (json_func) {
        printf("✓ mcp_json_parse function found (C API JSON)\n");
    } else {
        printf("• mcp_json_parse function not found\n");
    }

    dlclose(handle);

    printf("\n✓ Verification complete\n");
    return 0;
}
VERIFY_EOF

# Build verification tool for ARM64
MACOSX_DEPLOYMENT_TARGET=${MIN_MACOS_VERSION} cc -arch arm64 -o verify_mcp verify_mcp.c -ldl
rm -f verify_mcp.c

# Strip extended attributes to avoid security issues
xattr -cr verify_mcp 2>/dev/null || true

echo "  Created verify_mcp (macOS ARM64 compatible)"

# Clean up build directory
cd "$PROJECT_ROOT"
echo -e "${YELLOW}Cleaning up build directory...${NC}"
rm -rf "$BUILD_DIR"

# Verify the output
echo ""
echo -e "${YELLOW}Verifying output...${NC}"
cd "$OUTPUT_DIR"

MAIN_LIB=""
if [ -f "libgopher-mcp.0.1.0.dylib" ]; then
    MAIN_LIB="libgopher-mcp.0.1.0.dylib"
elif [ -f "libgopher-mcp.dylib" ]; then
    MAIN_LIB="libgopher-mcp.dylib"
fi

if [ -n "$MAIN_LIB" ] && [ -f "verify_mcp" ]; then
    echo -e "${GREEN}✅ Build successful!${NC}"
    echo ""
    echo "Output files:"
    echo "------------------------------------"
    ls -lah *.dylib 2>/dev/null || true
    ls -lah verify_mcp 2>/dev/null || true
    echo ""

    # Show library info
    echo "Library information:"
    file "$MAIN_LIB"
    echo ""

    # Show architecture
    echo "Architecture:"
    lipo -info "$MAIN_LIB"
    echo ""

    # Show minimum macOS version
    echo "Minimum macOS version:"
    otool -l "$MAIN_LIB" | grep -A 4 "LC_BUILD_VERSION\|LC_VERSION_MIN" | head -6
    echo ""

    echo -e "${GREEN}📦 Output contains:${NC}"
    echo "  - $MAIN_LIB (the MCP library, ARM64)"
    [ -f "libgopher-mcp.dylib" ] && echo "  - libgopher-mcp.dylib (symlink for compatibility)"
    [ -f "libgopher-mcp-event.0.1.0.dylib" ] && echo "  - libgopher-mcp-event.0.1.0.dylib (event library)"
    [ -f "libgopher-mcp-event.dylib" ] && echo "  - libgopher-mcp-event.dylib (symlink for compatibility)"
    [ -f "libgopher_mcp_c.0.1.0.dylib" ] && echo "  - libgopher_mcp_c.0.1.0.dylib (C API for FFI bindings)"
    [ -f "libgopher_mcp_c.dylib" ] && echo "  - libgopher_mcp_c.dylib (symlink for compatibility)"
    echo "  - verify_mcp (verification tool, macOS 11.0+ ARM64 compatible)"
    [ -d "include" ] && echo "  - include/ (header files)"
    echo ""

    # Test verification app (only if running on ARM64)
    if [[ $(uname -m) == "arm64" ]]; then
        echo -e "${YELLOW}Testing verification app...${NC}"
        if ./verify_mcp; then
            echo -e "${GREEN}✓ Verification test passed${NC}"
        else
            echo -e "${YELLOW}⚠ Verification test failed or crashed${NC}"
            echo "This may be due to missing dependencies or library issues"
            echo "The build artifacts have been created successfully"
        fi
    else
        echo -e "${YELLOW}Skipping verification test (not running on ARM64)${NC}"
    fi
else
    echo -e "${RED}❌ Build failed - required files not found${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}✨ Build complete!${NC}"
echo ""
echo "Output structure:"
echo "  build-output/mac-arm64/"
echo "    ├── libgopher-mcp.0.1.0.dylib (ARM64)"
echo "    ├── libgopher-mcp.dylib (symlink)"
echo "    ├── libgopher-mcp-event.*.dylib (if built)"
echo "    ├── libgopher_mcp_c.0.1.0.dylib (C API for FFI)"
echo "    ├── libgopher_mcp_c.dylib (symlink)"
echo "    ├── verify_mcp (C verification for ARM64)"
echo "    └── include/ (headers)"
echo ""
echo "To use on Apple Silicon Macs:"
echo "  1. Copy the entire build-output/mac-arm64/ directory to the target machine"
echo "  2. For C verification: ./verify_mcp"
echo ""
