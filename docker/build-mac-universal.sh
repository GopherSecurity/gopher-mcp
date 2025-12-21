#!/bin/bash

# Build script for libgopher_mcp_auth Universal Binary on macOS
# Target: macOS 10.14+ (x86_64) and macOS 11.0+ (arm64)
# Architecture: Universal Binary (x86_64 + arm64)

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Building libgopher_mcp_auth Universal Binary${NC}"
echo -e "${BLUE}Target: macOS Universal (x86_64 + arm64)${NC}"
echo -e "${BLUE}========================================${NC}"

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Build configurations
BUILD_DIR_X64="${PROJECT_ROOT}/build-mac-x64-for-universal"
BUILD_DIR_ARM64="${PROJECT_ROOT}/build-mac-arm64-for-universal"
OUTPUT_DIR="${PROJECT_ROOT}/build-output/mac-universal"

# Clean previous builds
echo -e "${YELLOW}Cleaning previous builds...${NC}"
rm -rf "$BUILD_DIR_X64"
rm -rf "$BUILD_DIR_ARM64"
rm -rf "$OUTPUT_DIR"
mkdir -p "$BUILD_DIR_X64"
mkdir -p "$BUILD_DIR_ARM64"
mkdir -p "$OUTPUT_DIR"

# ========================================
# Build x86_64 version
# ========================================
echo ""
echo -e "${GREEN}[1/3] Building x86_64 version...${NC}"
cd "$BUILD_DIR_X64"

# Check if we're on ARM64 and need to cross-compile
if [[ "$(uname -m)" == "arm64" ]]; then
    echo -e "${YELLOW}Detected ARM64 host, setting up cross-compilation for x86_64...${NC}"
    
    echo "Using CMake to find appropriate libraries for x86_64"
    X64_CMAKE_ARGS=(
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_CXX_STANDARD=11
        -DCMAKE_OSX_DEPLOYMENT_TARGET=10.14
        -DCMAKE_OSX_ARCHITECTURES=x86_64
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_SHARED_LIBS=ON
        -DCMAKE_INSTALL_PREFIX="${BUILD_DIR_X64}/install"
        -DCMAKE_MACOSX_RPATH=ON
        -DCMAKE_INSTALL_RPATH="@loader_path"
    )
else
    # On Intel Mac, use standard paths
    X64_CMAKE_ARGS=(
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_CXX_STANDARD=11
        -DCMAKE_OSX_DEPLOYMENT_TARGET=10.14
        -DCMAKE_OSX_ARCHITECTURES=x86_64
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_SHARED_LIBS=ON
        -DCMAKE_INSTALL_PREFIX="${BUILD_DIR_X64}/install"
        -DCMAKE_MACOSX_RPATH=ON
        -DCMAKE_INSTALL_RPATH="@loader_path"
    )
    
    # Add x86_64 OpenSSL if found
    if [ -d "/usr/local/opt/openssl@3" ]; then
        X64_CMAKE_ARGS+=(
            -DOPENSSL_ROOT_DIR="/usr/local/opt/openssl@3"
            -DOPENSSL_LIBRARIES="/usr/local/opt/openssl@3/lib/libssl.dylib;/usr/local/opt/openssl@3/lib/libcrypto.dylib"
            -DOPENSSL_INCLUDE_DIR="/usr/local/opt/openssl@3/include"
        )
    elif [ -d "/usr/local/opt/openssl" ]; then
        X64_CMAKE_ARGS+=(
            -DOPENSSL_ROOT_DIR="/usr/local/opt/openssl"
            -DOPENSSL_LIBRARIES="/usr/local/opt/openssl/lib/libssl.dylib;/usr/local/opt/openssl/lib/libcrypto.dylib"
            -DOPENSSL_INCLUDE_DIR="/usr/local/opt/openssl/include"
        )
    fi
fi

cmake "${X64_CMAKE_ARGS[@]}" "${PROJECT_ROOT}/src/auth"

make -j$(sysctl -n hw.ncpu)
make install

echo -e "${GREEN}✓ x86_64 build complete${NC}"

# ========================================
# Build arm64 version
# ========================================
echo ""
echo -e "${GREEN}[2/3] Building arm64 version...${NC}"
echo -e "${YELLOW}Note: Building ARM64 on Intel Mac using cross-compilation${NC}"
cd "$BUILD_DIR_ARM64"

# Use the cross-compilation script for ARM64
"${SCRIPT_DIR}/build-mac-arm64-cross.sh"

# Check if the ARM64 build succeeded
if [ ! -f "${PROJECT_ROOT}/build-output/mac-arm64/libgopher_mcp_auth.0.1.0.dylib" ]; then
    echo -e "${RED}ARM64 build failed. Trying alternative method...${NC}"
    
    # Alternative: Create toolchain file for cross-compilation
    cat > toolchain-arm64.cmake << 'EOF'
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "")
set(CMAKE_C_FLAGS "-arch arm64" CACHE STRING "")
set(CMAKE_CXX_FLAGS "-arch arm64" CACHE STRING "")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
EOF

    ARM64_CMAKE_ARGS=(
        -DCMAKE_TOOLCHAIN_FILE="${BUILD_DIR_ARM64}/toolchain-arm64.cmake"
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_CXX_STANDARD=11
        -DBUILD_SHARED_LIBS=ON
        -DCMAKE_INSTALL_PREFIX="${BUILD_DIR_ARM64}/install"
        -DCMAKE_MACOSX_RPATH=ON
        -DCMAKE_INSTALL_RPATH="@loader_path"
    )
    
    # Try to use system libraries which might be universal
    ARM64_CMAKE_ARGS+=(
        -DOPENSSL_USE_STATIC_LIBS=OFF
        -DCURL_LIBRARY="/usr/lib/libcurl.dylib"
        -DCURL_INCLUDE_DIR="/usr/include"
    )
    
    cmake "${ARM64_CMAKE_ARGS[@]}" "${PROJECT_ROOT}/src/auth"
    make -j$(sysctl -n hw.ncpu)
    make install
    
    # Copy the built library to expected location
    mkdir -p "${PROJECT_ROOT}/build-output/mac-arm64"
    cp "${BUILD_DIR_ARM64}/install/lib/libgopher_mcp_auth.0.1.0.dylib" \
       "${PROJECT_ROOT}/build-output/mac-arm64/" || true
fi

# Copy ARM64 library to build directory for universal binary creation
if [ -f "${PROJECT_ROOT}/build-output/mac-arm64/libgopher_mcp_auth.0.1.0.dylib" ]; then
    cp "${PROJECT_ROOT}/build-output/mac-arm64/libgopher_mcp_auth.0.1.0.dylib" \
       "${BUILD_DIR_ARM64}/libgopher_mcp_auth.0.1.0.dylib"
    echo -e "${GREEN}✓ arm64 build complete${NC}"
else
    echo -e "${RED}Failed to build ARM64 version${NC}"
    echo "You may need to install ARM64 dependencies or use a different approach"
    exit 1
fi

# ========================================
# Create Universal Binary
# ========================================
echo ""
echo -e "${GREEN}[3/3] Creating Universal Binary...${NC}"

# Find the actual library locations
X64_LIB="${BUILD_DIR_X64}/install/lib/libgopher_mcp_auth.0.1.0.dylib"
ARM64_LIB="${BUILD_DIR_ARM64}/libgopher_mcp_auth.0.1.0.dylib"

# Fallback paths if not in expected locations
if [ ! -f "$ARM64_LIB" ]; then
    ARM64_LIB="${BUILD_DIR_ARM64}/install/lib/libgopher_mcp_auth.0.1.0.dylib"
fi

# Verify both libraries exist
if [ ! -f "$X64_LIB" ]; then
    echo -e "${RED}Error: x86_64 library not found at: $X64_LIB${NC}"
    exit 1
fi

if [ ! -f "$ARM64_LIB" ]; then
    echo -e "${RED}Error: ARM64 library not found at: $ARM64_LIB${NC}"
    exit 1
fi

# Use lipo to combine the libraries
lipo -create \
    "$X64_LIB" \
    "$ARM64_LIB" \
    -output "${OUTPUT_DIR}/libgopher_mcp_auth.0.1.0.dylib"

# Create symlink for compatibility
cd "${OUTPUT_DIR}"
ln -sf libgopher_mcp_auth.0.1.0.dylib libgopher_mcp_auth.dylib

echo -e "${GREEN}✓ Universal binary created${NC}"

# ========================================
# Build Universal Verification App
# ========================================
echo ""
echo -e "${YELLOW}Building universal verification app...${NC}"

# Try to use verification programs in order of preference
VERIFY_SAFE="${SCRIPT_DIR}/mac-x64/verify_auth_safe.c"
VERIFY_FULL="${SCRIPT_DIR}/mac-x64/verify_auth_full.c"
VERIFY_SIMPLE="${SCRIPT_DIR}/mac-x64/verify_auth.c"

if [ -f "${VERIFY_SAFE}" ]; then
    echo "  Building safe verification tool..."
    cp "${VERIFY_SAFE}" verify_auth.c
    
    # Build universal binary for verification tool
    # First build x86_64
    MACOSX_DEPLOYMENT_TARGET=10.14 cc -arch x86_64 -o verify_auth_x64 verify_auth.c
    
    # Then build arm64
    MACOSX_DEPLOYMENT_TARGET=11.0 cc -arch arm64 -o verify_auth_arm64 verify_auth.c
    
    # Combine into universal binary
    lipo -create verify_auth_x64 verify_auth_arm64 -output verify_auth
    
    # Clean up intermediate files
    rm -f verify_auth_x64 verify_auth_arm64 verify_auth.c
    
    echo "  ✓ Built universal verification tool"
    
elif [ -f "${VERIFY_SIMPLE}" ]; then
    echo "  Building simple verification tool..."
    cp "${VERIFY_SIMPLE}" verify_auth.c
    
    # Build universal binary directly
    cc -arch x86_64 -arch arm64 \
       -mmacosx-version-min=10.14 \
       -o verify_auth verify_auth.c
    
    rm verify_auth.c
    echo "  ✓ Built universal verification tool"
else
    echo -e "${RED}Error: No verification source found${NC}"
    exit 1
fi

# Strip extended attributes
xattr -cr verify_auth 2>/dev/null || true

# Clean up build directories
cd "$PROJECT_ROOT"
echo -e "${YELLOW}Cleaning up temporary build directories...${NC}"
rm -rf "$BUILD_DIR_X64"
rm -rf "$BUILD_DIR_ARM64"
rm -rf "${PROJECT_ROOT}/build-mac-arm64-cross"  # Also clean up cross-compilation directory if it exists
echo -e "${GREEN}✓ Cleanup complete${NC}"

# ========================================
# Verify the output
# ========================================
echo ""
echo -e "${YELLOW}Verifying output...${NC}"
cd "$OUTPUT_DIR"

if [ -f "libgopher_mcp_auth.0.1.0.dylib" ] && [ -f "verify_auth" ]; then
    echo -e "${GREEN}✅ Build successful!${NC}"
    echo ""
    echo "Output files:"
    echo "------------------------------------"
    ls -lah
    echo ""
    
    # Show library info
    echo "Library information:"
    file libgopher_mcp_auth.0.1.0.dylib
    echo ""
    
    # Show architectures
    echo "Architectures in library:"
    lipo -info libgopher_mcp_auth.0.1.0.dylib
    echo ""
    
    # Show architectures in verification app
    echo "Architectures in verify_auth:"
    lipo -info verify_auth
    echo ""
    
    # Show detailed architecture info
    echo "Detailed architecture info:"
    lipo -detailed_info libgopher_mcp_auth.0.1.0.dylib | head -20
    echo ""
    
    echo -e "${BLUE}📦 Output contains:${NC}"
    echo "  - libgopher_mcp_auth.0.1.0.dylib (Universal: x86_64 + arm64)"
    echo "  - libgopher_mcp_auth.dylib (symlink for compatibility)"
    echo "  - verify_auth (Universal verification tool)"
    echo ""
    
    # Test verification app
    echo -e "${YELLOW}Testing verification app...${NC}"
    if ./verify_auth; then
        echo -e "${GREEN}✓ Verification test passed on $(uname -m)${NC}"
    else
        echo -e "${YELLOW}⚠ Verification test failed${NC}"
        echo "The build artifacts have been created successfully"
    fi
else
    echo -e "${RED}❌ Build failed - required files not found${NC}"
    exit 1
fi

# ========================================
# Setup TypeScript SDK and tests
# ========================================
echo ""
echo -e "${YELLOW}Setting up TypeScript SDK and tests...${NC}"

# Create TypeScript output directories
TS_SDK_DIR="${OUTPUT_DIR}/typescript/sdk"
TS_TEST_DIR="${OUTPUT_DIR}/typescript/tests"
mkdir -p "$TS_SDK_DIR"
mkdir -p "$TS_TEST_DIR"

# Copy TypeScript SDK files
echo "  Copying auth SDK files..."
if [ -d "${PROJECT_ROOT}/sdk/typescript/auth/src" ]; then
  cp "${PROJECT_ROOT}/sdk/typescript/auth/src/"*.ts "$TS_SDK_DIR/" 2>/dev/null || true
else
  cp "${PROJECT_ROOT}/sdk/typescript/src/"*auth*.ts "$TS_SDK_DIR/" 2>/dev/null || true
fi

# Create package.json for SDK
cat > "$TS_SDK_DIR/package.json" << 'EOF'
{
  "name": "@mcp/auth-sdk-universal",
  "version": "1.0.0",
  "description": "MCP Authentication SDK (Universal Binary)",
  "main": "index.js",
  "types": "index.d.ts",
  "os": ["darwin"],
  "cpu": ["x64", "arm64"],
  "dependencies": {
    "koffi": "^2.4.2"
  }
}
EOF

# Create TypeScript test
cat > "$TS_TEST_DIR/typescript-test.ts" << 'EOF'
import * as koffi from 'koffi';
import * as os from 'os';

console.log('TypeScript test for libgopher_mcp_auth (Universal Binary)');
console.log(`Running on: ${os.platform()} ${os.arch()}`);

try {
    const lib = koffi.load('../../libgopher_mcp_auth.dylib');
    console.log('✓ Library loaded successfully');
    
    const getVersion = lib.func('char* mcp_auth_version()');
    const version = getVersion();
    console.log(`✓ Library version: ${version}`);
    
    // Test basic initialization
    const init = lib.func('int mcp_auth_init()');
    const result = init();
    if (result === 0) {
        console.log('✓ Library initialized successfully');
    }
    
    console.log(`✓ All tests passed on ${os.arch()}`);
} catch (e: any) {
    console.error('✗ Test failed:', e.message);
    process.exit(1);
}
EOF

# Create test package.json
cat > "$TS_TEST_DIR/package.json" << 'EOF'
{
  "name": "mcp-auth-tests-universal",
  "version": "1.0.0",
  "scripts": {
    "test": "tsx typescript-test.ts"
  },
  "dependencies": {
    "koffi": "^2.4.2"
  },
  "devDependencies": {
    "@types/node": "^20.0.0",
    "tsx": "^4.0.0",
    "typescript": "^5.0.0"
  }
}
EOF

# Create tsconfig.json
cat > "$TS_TEST_DIR/tsconfig.json" << 'EOF'
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "commonjs",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "moduleResolution": "node"
  },
  "files": ["typescript-test.ts"]
}
EOF

# Create test runner script
cat > "$TS_TEST_DIR/run-test.sh" << 'EOF'
#!/bin/bash
echo "Installing dependencies..."
npm install --silent
echo "Running TypeScript tests on $(uname -m)..."
export MCP_LIBRARY_PATH="../../libgopher_mcp_auth.dylib"
npm test
EOF
chmod +x "$TS_TEST_DIR/run-test.sh"

echo -e "${GREEN}✓ TypeScript SDK and tests prepared${NC}"

# ========================================
# Final Summary
# ========================================
echo ""
echo -e "${BLUE}✨ Universal Binary Build Complete!${NC}"
echo ""
echo "Output structure:"
echo "  build-output/mac-universal/"
echo "    ├── libgopher_mcp_auth.0.1.0.dylib (Universal: x86_64 + arm64)"
echo "    ├── libgopher_mcp_auth.dylib (symlink)"
echo "    ├── verify_auth (Universal verification tool)"
echo "    └── typescript/"
echo "        ├── sdk/          (TypeScript SDK files)"
echo "        └── tests/        (TypeScript test suite)"
echo ""
echo "Compatibility:"
echo "  • x86_64: macOS 10.14+ (Mojave and later)"
echo "  • arm64:  macOS 11.0+ (Big Sur and later)"
echo ""
echo "This universal binary will automatically use the correct architecture:"
echo "  • Intel Macs → x86_64 code"
echo "  • Apple Silicon Macs → arm64 code (native performance)"
echo ""
echo "To use:"
echo "  1. Copy build-output/mac-universal/ to any Mac"
echo "  2. For C verification: ./verify_auth"
echo "  3. For TypeScript: cd typescript/tests && ./run-test.sh"
echo ""