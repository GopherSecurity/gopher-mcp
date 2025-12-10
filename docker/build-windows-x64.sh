#!/bin/bash

# Build script for Windows x64 (cross-compilation using MinGW)
# This creates a Windows DLL from macOS/Linux

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Building libgopher_mcp_auth for Windows x64${NC}"
echo -e "${GREEN}Target: Windows x64 (cross-compilation)${NC}"
echo -e "${GREEN}========================================${NC}"

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Build configuration
BUILD_DIR="${PROJECT_ROOT}/build-windows-x64"
OUTPUT_DIR="${PROJECT_ROOT}/build-output/windows-x64"

echo -e "${YELLOW}Note: This script requires MinGW-w64 for cross-compilation${NC}"
echo -e "${YELLOW}Install with: brew install mingw-w64 (macOS) or apt-get install mingw-w64 (Linux)${NC}"

# Check if MinGW is installed
if ! command -v x86_64-w64-mingw32-g++ &> /dev/null; then
    echo -e "${RED}Error: MinGW-w64 is not installed!${NC}"
    echo "Please install MinGW-w64 first:"
    echo "  macOS: brew install mingw-w64"
    echo "  Linux: apt-get install mingw-w64"
    exit 1
fi

# Clean previous builds
echo -e "${YELLOW}Cleaning previous builds...${NC}"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$OUTPUT_DIR"

# Navigate to build directory
cd "$BUILD_DIR"

# Configure CMake for Windows cross-compilation
echo -e "${YELLOW}Configuring CMake for Windows x64...${NC}"

# Create toolchain file for MinGW
cat > toolchain-mingw64.cmake << 'EOF'
# MinGW-w64 toolchain file for cross-compilation
SET(CMAKE_SYSTEM_NAME Windows)
SET(CMAKE_SYSTEM_PROCESSOR x86_64)

# Specify the cross-compiler
SET(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
SET(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
SET(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

# Target environment on the build host
SET(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)

# Search programs in the host environment
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Search libraries and headers in the target environment
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF

# Configure with toolchain
cmake \
    -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw64.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=11 \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_INSTALL_PREFIX="${OUTPUT_DIR}" \
    "${PROJECT_ROOT}/src/auth"

# Build the library
echo -e "${YELLOW}Building library...${NC}"
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)

# Install to output directory
echo -e "${YELLOW}Installing library to ${OUTPUT_DIR}...${NC}"
make install

# Move files to output root if needed
cd "$OUTPUT_DIR"
if [ -d "lib" ]; then
    mv lib/*.dll . 2>/dev/null || true
    mv lib/*.dll.a . 2>/dev/null || true
    rmdir lib 2>/dev/null || true
fi

if [ -d "include" ]; then
    mv include/*.h . 2>/dev/null || true
    rmdir include 2>/dev/null || true
fi

# Create verification batch file
cat > verify.bat << 'EOF'
@echo off
echo ====================================
echo libgopher_mcp_auth Windows Verification
echo ====================================
echo.
echo Library files:
dir *.dll
echo.
echo To test the library, build and run verify_auth.exe:
echo   build_verify.bat
echo   verify_auth.exe
EOF

# Verify the build
echo -e "${YELLOW}Verifying build...${NC}"
if [ -f "libgopher_mcp_auth.dll" ] || [ -f "gopher_mcp_auth.dll" ]; then
    echo -e "${GREEN}✅ Build successful!${NC}"
    echo ""
    echo "Library details:"
    echo "----------------"
    ls -lah *.dll 2>/dev/null || true
    
    echo ""
    echo -e "${GREEN}📦 Windows x64 build artifacts are in: ${OUTPUT_DIR}${NC}"
else
    echo -e "${RED}❌ Build failed - library not found${NC}"
    echo -e "${RED}Note: Windows cross-compilation requires proper MinGW setup${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}✨ Build complete!${NC}"
echo -e "${YELLOW}Note: Test on a Windows machine to verify functionality${NC}"