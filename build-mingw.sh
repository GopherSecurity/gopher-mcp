#!/bin/bash
# Build script for GopherMCP with MinGW on Cygwin
# Usage: ./build-mingw.sh [clean|release|debug]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-mingw"
BUILD_TYPE="${1:-Release}"

# Handle clean option
if [ "$1" == "clean" ]; then
    echo "Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
    echo "Done."
    exit 0
fi

# Set build type
if [ "$1" == "debug" ]; then
    BUILD_TYPE="Debug"
elif [ "$1" == "release" ]; then
    BUILD_TYPE="Release"
fi

echo "========================================"
echo "GopherMCP MinGW Build Script"
echo "========================================"
echo "Build type: ${BUILD_TYPE}"
echo "Build directory: ${BUILD_DIR}"
echo ""

# Create build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Configure with CMake
echo "Configuring with CMake..."
cmake -DCMAKE_TOOLCHAIN_FILE="${SCRIPT_DIR}/cmake/mingw-w64-toolchain.cmake" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DBUILD_EXAMPLES=ON \
      -DBUILD_TESTS=OFF \
      -DBUILD_SHARED_LIBS=OFF \
      -DBUILD_STATIC_LIBS=ON \
      "${SCRIPT_DIR}"

# Build
echo ""
echo "Building mcp_example_server..."
cmake --build . --target mcp_example_server -j$(nproc 2>/dev/null || echo 4)

echo ""
echo "========================================"
echo "Build complete!"
echo "========================================"
echo "Executable: ${BUILD_DIR}/examples/mcp/mcp_example_server.exe"
echo ""
echo "To run: ${BUILD_DIR}/examples/mcp/mcp_example_server.exe --help"
