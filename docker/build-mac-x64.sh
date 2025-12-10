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
SUPPORT_DIR="${PROJECT_ROOT}/docker/mac-x64"
MIN_MACOS_VERSION="10.14"

# Clean previous builds
echo -e "${YELLOW}Cleaning previous builds...${NC}"
rm -rf "$BUILD_DIR"
rm -rf "$OUTPUT_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$OUTPUT_DIR"
mkdir -p "$SUPPORT_DIR"

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
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/install" \
    -DCMAKE_MACOSX_RPATH=ON \
    -DCMAKE_INSTALL_RPATH="@loader_path" \
    "${PROJECT_ROOT}/src/auth"

# Build the library
echo -e "${YELLOW}Building library...${NC}"
make -j$(sysctl -n hw.ncpu)

# Install to temporary directory
make install

# Copy only the main library file to output
echo -e "${YELLOW}Organizing output files...${NC}"
cp "${BUILD_DIR}/install/lib/libgopher_mcp_auth.0.1.0.dylib" "${OUTPUT_DIR}/"
# Create symlink for compatibility
ln -sf libgopher_mcp_auth.0.1.0.dylib "${OUTPUT_DIR}/libgopher_mcp_auth.dylib"

# Build verification app with macOS 10.14 compatibility
echo -e "${YELLOW}Building verification app...${NC}"
cd "$OUTPUT_DIR"

# Build minimal C version for maximum macOS 10.14.6 compatibility
# Use very conservative flags to avoid any modern features
if [ -f "${PROJECT_ROOT}/verify_auth_minimal.c" ]; then
    # Compile with absolute minimum requirements
    clang \
        -o verify_auth \
        -std=c89 \
        -mmacosx-version-min=10.14 \
        -fno-stack-protector \
        -fno-strict-aliasing \
        -Wl,-no_adhoc_codesign \
        "${PROJECT_ROOT}/verify_auth_minimal.c"
    echo "  Built minimal C version for macOS 10.14.6 compatibility"
    
    # Strip the binary to remove any extra metadata
    strip verify_auth
    
    # Remove any extended attributes that might cause issues
    xattr -c verify_auth 2>/dev/null || true
elif [ -f "${PROJECT_ROOT}/verify_auth_simple.c" ]; then
    # Fallback to simple version
    clang -std=c99 \
        -mmacosx-version-min=10.14 \
        -o verify_auth \
        "${PROJECT_ROOT}/verify_auth_simple.c"
    echo "  Built C version for macOS 10.14.6+ compatibility"
else
    echo -e "${RED}Warning: verify_auth source not found${NC}"
fi

# Move support files to docker/mac-x64
echo -e "${YELLOW}Moving support files to ${SUPPORT_DIR}...${NC}"

# Create info script in support directory
cat > "${SUPPORT_DIR}/info.sh" << 'EOF'
#!/bin/bash
echo "Library Information:"
echo "===================="
file "$1"
echo ""
echo "Architecture:"
lipo -info "$1" 2>/dev/null || echo "Single architecture"
echo ""
echo "Dependencies:"
otool -L "$1"
echo ""
echo "Minimum macOS version:"
otool -l "$1" | grep -A 4 "LC_BUILD_VERSION" | grep "minos" | awk '{print "macOS " $2}'
echo ""
echo "Library size:"
ls -lh "$1" | awk '{print $5}'
EOF
chmod +x "${SUPPORT_DIR}/info.sh"

# Create build verification script in support directory
cat > "${SUPPORT_DIR}/build_verify.sh" << 'EOF'
#!/bin/bash
# Build verification app for macOS
set -e
OUTPUT_DIR="../../build-output/mac-x64"
cd "$OUTPUT_DIR"
if [ ! -f verify_auth ]; then
    echo "Building verification app..."
    clang++ -std=c++11 -mmacosx-version-min=10.14 -stdlib=libc++ \
        -o verify_auth ../../verify_auth.cc
fi
echo "Running verification..."
./verify_auth
EOF
chmod +x "${SUPPORT_DIR}/build_verify.sh"

# Copy headers to support directory (for reference)
if [ -d "${BUILD_DIR}/install/include" ]; then
    cp -r "${BUILD_DIR}/install/include" "${SUPPORT_DIR}/"
fi

# Create README in support directory
cat > "${SUPPORT_DIR}/README.md" << 'EOF'
# macOS x64 Support Files

This directory contains support files for the macOS x64 build.

## Files

- `info.sh` - Display library information
- `build_verify.sh` - Build and run verification
- `verify_auth_c` - C version for macOS 10.14.6 compatibility
- `include/` - Header files (for reference)

## Usage

To get library info:
```bash
./info.sh ../../build-output/mac-x64/libgopher_mcp_auth.0.1.0.dylib
```

To verify the library:
```bash
./build_verify.sh
```

For macOS 10.14.6 compatibility issues, use the C version:
```bash
cd ../../build-output/mac-x64
../../docker/mac-x64/verify_auth_c
```

## Output Directory

The main output files are in `../../build-output/mac-x64/`:
- `libgopher_mcp_auth.0.1.0.dylib` - The authentication library
- `libgopher_mcp_auth.dylib` - Symlink for compatibility
- `verify_auth` - Verification tool (C++ version)

## macOS 10.14.6 Compatibility

If `verify_auth` fails with "dyld: cannot load" error on macOS 10.14.6,
use the C version (`verify_auth_c`) from this support directory instead.
EOF

# Clean up build directory
cd "$PROJECT_ROOT"
rm -rf "$BUILD_DIR"

# Verify the output
echo ""
echo -e "${YELLOW}Verifying output...${NC}"
cd "$OUTPUT_DIR"

if [ -f "libgopher_mcp_auth.0.1.0.dylib" ] && [ -f "verify_auth" ]; then
    echo -e "${GREEN}✅ Build successful!${NC}"
    echo ""
    echo "Output files:"
    echo "-------------"
    ls -lah libgopher_mcp_auth.0.1.0.dylib verify_auth
    echo ""
    
    # Show library info
    "${SUPPORT_DIR}/info.sh" libgopher_mcp_auth.0.1.0.dylib
    
    echo ""
    echo -e "${GREEN}📦 Output files:${NC}"
    echo "  - ${OUTPUT_DIR}/libgopher_mcp_auth.0.1.0.dylib"
    echo "  - ${OUTPUT_DIR}/verify_auth"
    echo ""
    echo -e "${GREEN}📁 Support files:${NC}"
    echo "  - ${SUPPORT_DIR}/"
else
    echo -e "${RED}❌ Build failed - required files not found${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}✨ Build complete!${NC}"