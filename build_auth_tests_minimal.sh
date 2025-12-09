#!/bin/bash

echo "========================================="
echo "Minimal Auth Test Build (No Dependencies)"
echo "========================================="
echo

# Color codes
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Step 1: Build standalone auth library if needed
if [ ! -f "${SCRIPT_DIR}/build-auth-only/libgopher_mcp_auth.dylib" ]; then
    echo -e "${BLUE}Building standalone auth library...${NC}"
    mkdir -p "${SCRIPT_DIR}/build-auth-only"
    cd "${SCRIPT_DIR}/build-auth-only"
    cmake "${SCRIPT_DIR}/src/auth" -DCMAKE_BUILD_TYPE=Release
    make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
    cd "${SCRIPT_DIR}"
fi

# Step 2: Check if _deps already exists with required content
if [ -d "${SCRIPT_DIR}/build/_deps" ]; then
    echo -e "${GREEN}✅ Dependencies already available${NC}"
else
    echo -e "${YELLOW}⚠️  Dependencies directory missing${NC}"
    echo -e "${BLUE}Creating minimal _deps structure...${NC}"
    
    # Option 1: Try to copy from existing build if available
    if [ -d "${HOME}/.cache/mcp_cpp_deps" ]; then
        echo "Using cached dependencies..."
        cp -r "${HOME}/.cache/mcp_cpp_deps" "${SCRIPT_DIR}/build/_deps"
    else
        echo -e "${YELLOW}Note: Full build required once to fetch dependencies${NC}"
        echo "Run: cd build && cmake .. && make -j4"
        echo "This will download dependencies to build/_deps/"
        exit 1
    fi
fi

# Step 3: Build only the auth tests
echo -e "${BLUE}Building auth tests...${NC}"
cd "${SCRIPT_DIR}/build"

# Build individual test targets
for test in test_auth_types benchmark_jwt_validation benchmark_crypto_optimization \
           benchmark_network_optimization test_keycloak_integration \
           test_mcp_inspector_flow test_complete_integration; do
    echo "Building $test..."
    make $test
done

cd "${SCRIPT_DIR}"

echo -e "${GREEN}✅ Auth tests built successfully${NC}"
echo
echo "To run tests with standalone library:"
echo "  export DYLD_LIBRARY_PATH=${SCRIPT_DIR}/build-auth-only"
echo "  ./build/tests/test_auth_types"