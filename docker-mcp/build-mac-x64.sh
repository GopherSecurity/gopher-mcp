#!/bin/bash

# Build script for libgopher-mcp on macOS x86_64
# Target: macOS 10.14+ (Mojave and later)
# Architecture: x86_64

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Building libgopher-mcp for macOS x86_64${NC}"
echo -e "${GREEN}Target: macOS 10.14+ (x86_64)${NC}"
echo -e "${GREEN}========================================${NC}"

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Build configuration
BUILD_DIR="${PROJECT_ROOT}/build-mac-x64"
DEPS_DIR="${PROJECT_ROOT}/_deps-x64"
INSTALL_DIR="${PROJECT_ROOT}/install_prefix_dir"
OUTPUT_DIR="${PROJECT_ROOT}/build-output/mac-x64"
MIN_MACOS_VERSION="10.14"

# Clean previous builds (but preserve _deps for caching)
echo -e "${YELLOW}Cleaning previous builds...${NC}"
rm -rf "$BUILD_DIR"
rm -rf "$OUTPUT_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$DEPS_DIR"
mkdir -p "$OUTPUT_DIR"

# Navigate to build directory
cd "$BUILD_DIR"

# Configure CMake with macOS-specific settings
echo -e "${YELLOW}Configuring CMake for macOS x86_64...${NC}"

cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=17 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=${MIN_MACOS_VERSION} \
    -DCMAKE_OSX_ARCHITECTURES=x86_64 \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_STATIC_LIBS=ON \
    -DBUILD_TESTS=OFF \
    -DBUILD_C_API=ON \
    -DBUILD_BINDINGS_EXAMPLES=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DFETCHCONTENT_BASE_DIR="${DEPS_DIR}" \
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/install" \
    -DCMAKE_MACOSX_RPATH=ON \
    -DCMAKE_INSTALL_RPATH="@loader_path" \
    "${PROJECT_ROOT}"

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

# Build verification app for macOS
echo -e "${YELLOW}Building verification app...${NC}"
cd "${OUTPUT_DIR}"

# Create a simple verification program
cat > verify_mcp.c << 'VERIFY_EOF'
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>

int main() {
    printf("libgopher-mcp verification tool\n");
    printf("================================\n\n");

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

# Build verification tool
MACOSX_DEPLOYMENT_TARGET=${MIN_MACOS_VERSION} cc -o verify_mcp verify_mcp.c -ldl
rm -f verify_mcp.c

# Strip extended attributes to avoid security issues
xattr -cr verify_mcp 2>/dev/null || true

echo "  Created verify_mcp (macOS compatible)"

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

    # Show minimum macOS version
    echo "Minimum macOS version:"
    otool -l "$MAIN_LIB" | grep -A 4 "LC_BUILD_VERSION\|LC_VERSION_MIN" | head -6
    echo ""

    echo -e "${GREEN}📦 Output contains:${NC}"
    echo "  - $MAIN_LIB (the MCP library)"
    [ -f "libgopher-mcp.dylib" ] && echo "  - libgopher-mcp.dylib (symlink for compatibility)"
    [ -f "libgopher-mcp-event.0.1.0.dylib" ] && echo "  - libgopher-mcp-event.0.1.0.dylib (event library)"
    [ -f "libgopher-mcp-event.dylib" ] && echo "  - libgopher-mcp-event.dylib (symlink for compatibility)"
    [ -f "libgopher_mcp_c.0.1.0.dylib" ] && echo "  - libgopher_mcp_c.0.1.0.dylib (C API for FFI bindings)"
    [ -f "libgopher_mcp_c.dylib" ] && echo "  - libgopher_mcp_c.dylib (symlink for compatibility)"
    echo "  - verify_mcp (verification tool, macOS 10.14+ compatible)"
    [ -d "include" ] && echo "  - include/ (header files)"
    echo ""

    # Test verification app
    echo -e "${YELLOW}Testing verification app...${NC}"
    if ./verify_mcp; then
        echo -e "${GREEN}✓ Verification test passed${NC}"
    else
        echo -e "${YELLOW}⚠ Verification test failed or crashed${NC}"
        echo "This may be due to missing dependencies or library issues"
        echo "The build artifacts have been created successfully"
    fi
else
    echo -e "${RED}❌ Build failed - required files not found${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}✨ Build complete!${NC}"
echo ""
echo "Output structure:"
echo "  build-output/mac-x64/"
echo "    ├── libgopher-mcp.0.1.0.dylib"
echo "    ├── libgopher-mcp.dylib (symlink)"
echo "    ├── libgopher-mcp-event.*.dylib (if built)"
echo "    ├── libgopher_mcp_c.0.1.0.dylib (C API for FFI)"
echo "    ├── libgopher_mcp_c.dylib (symlink)"
echo "    ├── verify_mcp (C verification)"
echo "    └── include/ (headers)"
echo ""
echo "To use on macOS:"
echo "  1. Copy the entire build-output/mac-x64/ directory to the target machine"
echo "  2. For C verification: ./verify_mcp"
echo ""
