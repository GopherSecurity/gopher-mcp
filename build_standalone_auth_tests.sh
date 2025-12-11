#!/bin/bash

echo "========================================="
echo "Standalone Auth Tests Build (Minimal Deps)"
echo "========================================="
echo

# Color codes
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-auth-tests-standalone"

# Step 1: Build standalone auth library if needed
if [ ! -f "${SCRIPT_DIR}/build-auth-only/libgopher_mcp_auth.dylib" ]; then
    echo -e "${BLUE}Building standalone auth library first...${NC}"
    mkdir -p "${SCRIPT_DIR}/build-auth-only"
    cd "${SCRIPT_DIR}/build-auth-only"
    cmake "${SCRIPT_DIR}/src/auth" -DCMAKE_BUILD_TYPE=Release
    make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
    cd "${SCRIPT_DIR}"
    echo -e "${GREEN}✅ Auth library built${NC}"
fi

# Step 2: Create a minimal CMakeLists.txt for auth tests only
echo -e "${BLUE}Creating minimal test configuration...${NC}"
cat > "${SCRIPT_DIR}/tests/auth/CMakeLists_minimal.txt" << 'EOF'
cmake_minimum_required(VERSION 3.16)
project(auth_tests_minimal)

# Use C++17 for tests (even though library is C++11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# Try to use system GTest first
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    # Only download GTest if not found on system
    message(STATUS "GTest not found on system, downloading...")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${OPENSSL_INCLUDE_DIR}
    ${CURL_INCLUDE_DIRS}
)

# Link to the standalone auth library
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../build-auth-only)

# Helper function to add auth test
function(add_auth_test test_name)
    add_executable(${test_name} ${test_name}.cc)
    target_link_libraries(${test_name}
        gopher_mcp_auth
        ${OPENSSL_LIBRARIES}
        ${CURL_LIBRARIES}
        Threads::Threads
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

# Add all auth tests
enable_testing()
add_auth_test(test_auth_types)
add_auth_test(benchmark_jwt_validation)
add_auth_test(benchmark_crypto_optimization)
add_auth_test(benchmark_network_optimization)
add_auth_test(test_keycloak_integration)
add_auth_test(test_mcp_inspector_flow)
add_auth_test(test_complete_integration)
EOF

# Step 3: Build the tests
echo -e "${BLUE}Building auth tests with minimal dependencies...${NC}"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
cmake "${SCRIPT_DIR}/tests/auth" \
    -DCMAKE_BUILD_TYPE=Release \
    -C "${SCRIPT_DIR}/tests/auth/CMakeLists_minimal.txt" \
    2>&1 | tee cmake_output.log

# Use the minimal CMakeLists
cp "${SCRIPT_DIR}/tests/auth/CMakeLists_minimal.txt" "${SCRIPT_DIR}/tests/auth/CMakeLists.txt.bak"
cp "${SCRIPT_DIR}/tests/auth/CMakeLists_minimal.txt" "${SCRIPT_DIR}/tests/auth/CMakeLists.txt"

# Configure again with the right file
cmake "${SCRIPT_DIR}/tests/auth" -DCMAKE_BUILD_TYPE=Release

# Build
echo -e "${BLUE}Building test executables...${NC}"
make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# Restore original CMakeLists if it existed
if [ -f "${SCRIPT_DIR}/tests/auth/CMakeLists.txt.bak" ]; then
    mv "${SCRIPT_DIR}/tests/auth/CMakeLists.txt.bak" "${SCRIPT_DIR}/tests/auth/CMakeLists.txt"
fi

cd "$SCRIPT_DIR"

# Step 4: Show results
if [ $? -eq 0 ]; then
    echo
    echo -e "${GREEN}✅ Standalone auth tests built successfully!${NC}"
    echo
    echo "Test executables created in: $BUILD_DIR"
    echo
    echo "To run the tests:"
    echo "  export DYLD_LIBRARY_PATH=${SCRIPT_DIR}/build-auth-only"
    echo "  ${BUILD_DIR}/test_auth_types"
    echo
    echo "Or run all tests:"
    echo "  for test in ${BUILD_DIR}/*test* ${BUILD_DIR}/benchmark*; do"
    echo "    echo \"Running \$(basename \$test)...\""
    echo "    DYLD_LIBRARY_PATH=${SCRIPT_DIR}/build-auth-only \$test"
    echo "  done"
else
    echo -e "${RED}❌ Build failed${NC}"
    exit 1
fi