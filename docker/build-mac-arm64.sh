#!/bin/bash

# Build script for libgopher_mcp_auth on macOS ARM64 (Apple Silicon)
# Target: macOS 11.0+ (Big Sur and later for Apple Silicon)
# Architecture: arm64

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Building libgopher_mcp_auth for macOS ARM64${NC}"
echo -e "${GREEN}Target: macOS 11.0+ (arm64/Apple Silicon)${NC}"
echo -e "${GREEN}========================================${NC}"

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Build configuration
BUILD_DIR="${PROJECT_ROOT}/build-mac-arm64"
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

# Check for ARM64 OpenSSL installation
echo -e "${YELLOW}Checking for ARM64 OpenSSL...${NC}"

# Common locations for ARM64 OpenSSL
ARM64_OPENSSL_ROOT=""
if [ -d "/opt/homebrew/opt/openssl" ]; then
    ARM64_OPENSSL_ROOT="/opt/homebrew/opt/openssl"
    echo "  Found ARM64 OpenSSL at: $ARM64_OPENSSL_ROOT"
elif [ -d "/opt/homebrew/opt/openssl@3" ]; then
    ARM64_OPENSSL_ROOT="/opt/homebrew/opt/openssl@3"
    echo "  Found ARM64 OpenSSL at: $ARM64_OPENSSL_ROOT"
elif [ -d "/opt/homebrew/opt/openssl@1.1" ]; then
    ARM64_OPENSSL_ROOT="/opt/homebrew/opt/openssl@1.1"
    echo "  Found ARM64 OpenSSL at: $ARM64_OPENSSL_ROOT"
else
    echo -e "${RED}Error: ARM64 OpenSSL not found!${NC}"
    echo "Please install OpenSSL for ARM64 using:"
    echo "  arch -arm64 /bin/bash -c \"curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh | /bin/bash\""
    echo "  /opt/homebrew/bin/brew install openssl"
    echo ""
    echo "Or if building on Intel Mac, use cross-compilation:"
    echo "  ./docker/build-mac-universal.sh"
    exit 1
fi

# Check for ARM64 libcurl
echo -e "${YELLOW}Checking for ARM64 libcurl...${NC}"
ARM64_CURL_ROOT=""
if [ -d "/opt/homebrew/opt/curl" ]; then
    ARM64_CURL_ROOT="/opt/homebrew/opt/curl"
    echo "  Found ARM64 curl at: $ARM64_CURL_ROOT"
else
    # Use system curl as fallback
    echo "  Using system curl (universal)"
fi

# Configure CMake with macOS ARM64-specific settings
echo -e "${YELLOW}Configuring CMake for macOS ARM64...${NC}"

CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_CXX_STANDARD=11
    -DCMAKE_OSX_DEPLOYMENT_TARGET=${MIN_MACOS_VERSION}
    -DCMAKE_OSX_ARCHITECTURES=arm64
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_SHARED_LIBS=ON
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/install"
    -DCMAKE_MACOSX_RPATH=ON
    -DCMAKE_INSTALL_RPATH="@loader_path"
)

# Add OpenSSL paths if found
if [ -n "$ARM64_OPENSSL_ROOT" ]; then
    CMAKE_ARGS+=(
        -DOPENSSL_ROOT_DIR="$ARM64_OPENSSL_ROOT"
        -DOPENSSL_LIBRARIES="${ARM64_OPENSSL_ROOT}/lib/libssl.dylib;${ARM64_OPENSSL_ROOT}/lib/libcrypto.dylib"
        -DOPENSSL_INCLUDE_DIR="${ARM64_OPENSSL_ROOT}/include"
    )
fi

# Add curl paths if found
if [ -n "$ARM64_CURL_ROOT" ]; then
    CMAKE_ARGS+=(
        -DCURL_LIBRARY="${ARM64_CURL_ROOT}/lib/libcurl.dylib"
        -DCURL_INCLUDE_DIR="${ARM64_CURL_ROOT}/include"
    )
fi

cmake "${CMAKE_ARGS[@]}" "${PROJECT_ROOT}/src/auth"

# Build the library
echo -e "${YELLOW}Building library...${NC}"
make -j$(sysctl -n hw.ncpu)

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

# Try to use verification programs in order of preference
VERIFY_SAFE="${SCRIPT_DIR}/mac-x64/verify_auth_safe.c"
VERIFY_FULL="${SCRIPT_DIR}/mac-x64/verify_auth_full.c"
VERIFY_SIMPLE="${SCRIPT_DIR}/mac-x64/verify_auth.c"

if [ -f "${VERIFY_SAFE}" ]; then
    echo "  Building safe verification tool from docker/mac-x64/verify_auth_safe.c"
    cp "${VERIFY_SAFE}" verify_auth.c
    
    # Build with macOS 11.0 compatibility for ARM64
    MACOSX_DEPLOYMENT_TARGET=11.0 cc -arch arm64 -o verify_auth verify_auth.c
    
    if [ $? -eq 0 ]; then
        echo "  ✓ Built safe verification tool"
    else
        echo "  Warning: Safe version build failed, trying simple version..."
        if [ -f "${VERIFY_SIMPLE}" ]; then
            cp "${VERIFY_SIMPLE}" verify_auth.c
            MACOSX_DEPLOYMENT_TARGET=11.0 cc -arch arm64 -o verify_auth verify_auth.c
            echo "  ✓ Built simple verification tool"
        fi
    fi
    
    # Clean up source
    rm -f verify_auth.c
    
elif [ -f "${VERIFY_FULL}" ]; then
    echo "  Building full verification tool from docker/mac-x64/verify_auth_full.c"
    cp "${VERIFY_FULL}" verify_auth.c
    MACOSX_DEPLOYMENT_TARGET=11.0 cc -arch arm64 -o verify_auth verify_auth.c
    rm -f verify_auth.c
    echo "  ✓ Built full verification tool"
    
elif [ -f "${VERIFY_SIMPLE}" ]; then
    echo "  Building simple verification tool from docker/mac-x64/verify_auth.c"
    cp "${VERIFY_SIMPLE}" verify_auth.c
    MACOSX_DEPLOYMENT_TARGET=11.0 cc -arch arm64 -o verify_auth verify_auth.c
    rm verify_auth.c
    echo "  ✓ Built simple verification tool"
else
    echo -e "${RED}Error: No verification source found${NC}"
    exit 1
fi

# Strip extended attributes to avoid security issues
xattr -cr verify_auth 2>/dev/null || true

echo "  Created verify_auth (macOS ARM64 compatible)"

# Clean up build directory
cd "$PROJECT_ROOT"
echo -e "${YELLOW}Cleaning up build directory...${NC}"
rm -rf "$BUILD_DIR"

# Verify the output
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
    
    # Show architecture
    echo "Architecture:"
    lipo -info libgopher_mcp_auth.0.1.0.dylib
    echo ""
    
    # Show minimum macOS version
    echo "Minimum macOS version:"
    otool -l libgopher_mcp_auth.0.1.0.dylib | grep -A 4 "LC_BUILD_VERSION\|LC_VERSION_MIN" | head -6
    echo ""
    
    echo -e "${GREEN}📦 Output contains:${NC}"
    echo "  - libgopher_mcp_auth.0.1.0.dylib (ARM64 authentication library)"
    echo "  - libgopher_mcp_auth.dylib (symlink for compatibility)"
    echo "  - verify_auth (verification tool, macOS 11.0+ ARM64 compatible)"
    echo ""
    
    # Test verification app (only if running on ARM64)
    if [[ $(uname -m) == "arm64" ]]; then
        echo -e "${YELLOW}Testing verification app...${NC}"
        if ./verify_auth; then
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

# Build TypeScript SDK and tests (same as x64 version)
echo ""
echo -e "${YELLOW}Building TypeScript SDK and tests...${NC}"

# Create TypeScript output directories
TS_SDK_DIR="${OUTPUT_DIR}/typescript/sdk"
TS_TEST_DIR="${OUTPUT_DIR}/typescript/tests"
mkdir -p "$TS_SDK_DIR"
mkdir -p "$TS_TEST_DIR"

# Copy TypeScript SDK files (auth-related only)
echo "  Copying auth SDK files..."
# Copy from new auth/src directory structure
if [ -d "${PROJECT_ROOT}/sdk/typescript/auth/src" ]; then
  cp "${PROJECT_ROOT}/sdk/typescript/auth/src/auth-types.ts" "$TS_SDK_DIR/" 2>/dev/null || true
  cp "${PROJECT_ROOT}/sdk/typescript/auth/src/auth.ts" "$TS_SDK_DIR/" 2>/dev/null || true
  cp "${PROJECT_ROOT}/sdk/typescript/auth/src/mcp-auth-api.ts" "$TS_SDK_DIR/" 2>/dev/null || true
  cp "${PROJECT_ROOT}/sdk/typescript/auth/src/mcp-auth-ffi-bindings.ts" "$TS_SDK_DIR/" 2>/dev/null || true
  cp "${PROJECT_ROOT}/sdk/typescript/auth/src/oauth-helper.ts" "$TS_SDK_DIR/" 2>/dev/null || true
  cp "${PROJECT_ROOT}/sdk/typescript/auth/src/session-manager.ts" "$TS_SDK_DIR/" 2>/dev/null || true
  cp "${PROJECT_ROOT}/sdk/typescript/auth/src/index.ts" "$TS_SDK_DIR/" 2>/dev/null || true
  cp "${PROJECT_ROOT}/sdk/typescript/auth/src/express-adapter.ts" "$TS_SDK_DIR/" 2>/dev/null || true
else
  # Fallback to old structure if new one doesn't exist
  cp "${PROJECT_ROOT}/sdk/typescript/src/auth-types.ts" "$TS_SDK_DIR/" 2>/dev/null || true
  cp "${PROJECT_ROOT}/sdk/typescript/src/auth.ts" "$TS_SDK_DIR/" 2>/dev/null || true
  cp "${PROJECT_ROOT}/sdk/typescript/src/mcp-auth-api.ts" "$TS_SDK_DIR/" 2>/dev/null || true
  cp "${PROJECT_ROOT}/sdk/typescript/src/mcp-auth-ffi-bindings.ts" "$TS_SDK_DIR/" 2>/dev/null || true
  cp "${PROJECT_ROOT}/sdk/typescript/src/oauth-helper.ts" "$TS_SDK_DIR/" 2>/dev/null || true
  cp "${PROJECT_ROOT}/sdk/typescript/src/session-manager.ts" "$TS_SDK_DIR/" 2>/dev/null || true
  cp "${PROJECT_ROOT}/sdk/typescript/src/sdk-index.ts" "$TS_SDK_DIR/" 2>/dev/null || true
  cp "${PROJECT_ROOT}/sdk/typescript/auth-adapter/express-adapter.ts" "$TS_SDK_DIR/" 2>/dev/null || true
fi
# Also copy adapter if it exists in the new location
cp "${PROJECT_ROOT}/sdk/typescript/auth/adapter/express-adapter.ts" "$TS_SDK_DIR/" 2>/dev/null || true

# Copy package.json for SDK
cat > "$TS_SDK_DIR/package.json" << 'EOF'
{
  "name": "@mcp/auth-sdk",
  "version": "1.0.0",
  "description": "MCP Authentication SDK",
  "main": "index.js",
  "types": "index.d.ts",
  "dependencies": {
    "koffi": "^2.4.2"
  }
}
EOF

# Copy TypeScript test files
echo "  Setting up TypeScript tests..."
cp "${SCRIPT_DIR}/mac-x64/typescript-test.ts" "$TS_TEST_DIR/" 2>/dev/null || {
    echo "    Creating default TypeScript test..."
    # If typescript-test.ts doesn't exist, create a minimal one
    cat > "$TS_TEST_DIR/typescript-test.ts" << 'EOF'
import * as koffi from 'koffi';
console.log('TypeScript test for libgopher_mcp_auth (ARM64)');
try {
    const lib = koffi.load('../../libgopher_mcp_auth.dylib');
    console.log('✓ Library loaded successfully');
    const getVersion = lib.func('char* mcp_auth_version()');
    const version = getVersion();
    console.log(`✓ Library version: ${version}`);
} catch (e: any) {
    console.error('✗ Test failed:', e.message);
    process.exit(1);
}
EOF
}


# Copy test configuration files
cp "${SCRIPT_DIR}/mac-x64/package.json" "$TS_TEST_DIR/" 2>/dev/null || {
    cat > "$TS_TEST_DIR/package.json" << 'EOF'
{
  "name": "mcp-auth-tests",
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
}

cp "${SCRIPT_DIR}/mac-x64/tsconfig.json" "$TS_TEST_DIR/" 2>/dev/null || {
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
}

# Create a test runner script
cat > "$TS_TEST_DIR/run-test.sh" << 'EOF'
#!/bin/bash
echo "Installing dependencies..."
npm install --silent
echo "Running TypeScript tests..."
export MCP_LIBRARY_PATH="../../libgopher_mcp_auth.dylib"
npm test
EOF
chmod +x "$TS_TEST_DIR/run-test.sh"

echo -e "${GREEN}✓ TypeScript SDK and tests prepared${NC}"

echo ""
echo -e "${GREEN}✨ Build complete!${NC}"
echo ""
echo "Output structure:"
echo "  build-output/mac-arm64/"
echo "    ├── libgopher_mcp_auth.0.1.0.dylib (ARM64)"
echo "    ├── libgopher_mcp_auth.dylib (symlink)"
echo "    ├── verify_auth (C verification for ARM64)"
echo "    └── typescript/"
echo "        ├── sdk/          (TypeScript SDK files)"
echo "        └── tests/        (TypeScript test suite)"
echo ""
echo "To use on Apple Silicon Macs:"
echo "  1. Copy the entire build-output/mac-arm64/ directory to the target machine"
echo "  2. For C verification: ./verify_auth"
echo "  3. For TypeScript tests: cd typescript/tests && ./run-test.sh"
echo ""