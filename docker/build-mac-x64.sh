#!/bin/bash

# Build script for libgopher_mcp_auth on macOS x86_64
# Target: macOS 10.14+ (Mojave and later)
# Architecture: x86_64

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Building libgopher_mcp_auth for macOS x86_64${NC}"
echo -e "${GREEN}Target: macOS 10.14+ (x86_64)${NC}"
echo -e "${GREEN}========================================${NC}"

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Build configuration
BUILD_DIR="${PROJECT_ROOT}/build-mac-x64"
OUTPUT_DIR="${PROJECT_ROOT}/build-output/mac-x64"
MIN_MACOS_VERSION="10.14"

# Clean previous builds
echo -e "${YELLOW}Cleaning previous builds...${NC}"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$OUTPUT_DIR"

# Navigate to build directory
cd "$BUILD_DIR"

# Configure CMake with macOS-specific settings
echo -e "${YELLOW}Configuring CMake for macOS x86_64...${NC}"
cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=11 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=${MIN_MACOS_VERSION} \
    -DCMAKE_OSX_ARCHITECTURES=x86_64 \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_INSTALL_PREFIX="${OUTPUT_DIR}" \
    -DCMAKE_MACOSX_RPATH=ON \
    -DCMAKE_INSTALL_RPATH="@loader_path" \
    "${PROJECT_ROOT}/src/auth"

# Build the library
echo -e "${YELLOW}Building library...${NC}"
make -j$(sysctl -n hw.ncpu)

# Install to output directory
echo -e "${YELLOW}Installing library to ${OUTPUT_DIR}...${NC}"
make install

# Create universal binary info script
cat > "${OUTPUT_DIR}/info.sh" << 'EOF'
#!/bin/bash
echo "Library Information:"
echo "===================="
file libgopher_mcp_auth*.dylib
echo ""
echo "Architecture:"
lipo -info libgopher_mcp_auth*.dylib 2>/dev/null || true
echo ""
echo "Dependencies:"
otool -L libgopher_mcp_auth.0.1.0.dylib 2>/dev/null || otool -L libgopher_mcp_auth.dylib
echo ""
echo "Minimum macOS version:"
otool -l libgopher_mcp_auth.0.1.0.dylib 2>/dev/null | grep -A 3 LC_VERSION_MIN_MACOSX || \
otool -l libgopher_mcp_auth.dylib | grep -A 3 LC_VERSION_MIN_MACOSX || \
echo "Version info not found"
echo ""
echo "Symbols exported:"
nm -gU libgopher_mcp_auth.0.1.0.dylib 2>/dev/null | head -20 || \
nm -gU libgopher_mcp_auth.dylib | head -20
EOF
chmod +x "${OUTPUT_DIR}/info.sh"

# Verify the build
echo -e "${YELLOW}Verifying build...${NC}"
cd "$OUTPUT_DIR"

if [ -f "libgopher_mcp_auth.dylib" ] || [ -f "lib/libgopher_mcp_auth.dylib" ]; then
    # Move libraries to output root if they're in lib/
    if [ -d "lib" ]; then
        mv lib/*.dylib . 2>/dev/null || true
        rmdir lib 2>/dev/null || true
    fi
    
    # Move headers to output root if they're in include/
    if [ -d "include" ]; then
        mv include/*.h . 2>/dev/null || true
        rmdir include 2>/dev/null || true
    fi
    
    echo -e "${GREEN}✅ Build successful!${NC}"
    echo ""
    echo "Library details:"
    echo "----------------"
    ls -lah *.dylib
    echo ""
    
    # Show library info
    ./info.sh
    
    echo ""
    echo -e "${GREEN}📦 macOS x86_64 build artifacts are in: ${OUTPUT_DIR}${NC}"
else
    echo -e "${RED}❌ Build failed - library not found${NC}"
    exit 1
fi

# Optional: Create a distribution package
echo ""
echo -e "${YELLOW}Creating distribution package...${NC}"
cd "$PROJECT_ROOT"
tar -czf "build-output/libgopher_mcp_auth-mac-x64-${MIN_MACOS_VERSION}.tar.gz" \
    -C "$OUTPUT_DIR" \
    $(ls "$OUTPUT_DIR"/*.dylib "$OUTPUT_DIR"/*.h "$OUTPUT_DIR"/info.sh 2>/dev/null | xargs -n1 basename)

echo -e "${GREEN}📦 Distribution package created: build-output/libgopher_mcp_auth-mac-x64-${MIN_MACOS_VERSION}.tar.gz${NC}"
echo ""
echo -e "${GREEN}✨ Build complete!${NC}"