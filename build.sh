#!/bin/bash

# Build and test script for MCP C++ SDK

set -e  # Exit on error

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Default values
BUILD_TYPE="Debug"
BUILD_DIR="build"
CLEAN_BUILD=false
RUN_TESTS=true
VERBOSE=false
INSTALL_PREFIX=""

# Usage function
usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  -r, --release      Build in Release mode (default: Debug)"
    echo "  -c, --clean        Clean build (removes build directory first)"
    echo "  -t, --no-tests     Skip running tests after build"
    echo "  -v, --verbose      Verbose output"
    echo "  -p, --prefix PATH  Set installation prefix"
    echo "  -h, --help         Show this help message"
    exit 1
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -r|--release)
            BUILD_TYPE="Release"
            shift
            ;;
        -c|--clean)
            CLEAN_BUILD=true
            shift
            ;;
        -t|--no-tests)
            RUN_TESTS=false
            shift
            ;;
        -p|--prefix)
            INSTALL_PREFIX="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

# Function to print colored output
print_status() {
    echo -e "${BLUE}==>${NC} $1"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

# Change to script directory
cd "$SCRIPT_DIR"

# Clean build if requested
if [ "$CLEAN_BUILD" = true ]; then
    print_status "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    print_success "Build directory cleaned"
fi

# Create build directory
if [ ! -d "$BUILD_DIR" ]; then
    print_status "Creating build directory..."
    mkdir -p "$BUILD_DIR"
fi

# Configure with CMake
print_status "Configuring with CMake (${BUILD_TYPE} mode)..."
cd "$BUILD_DIR"

CMAKE_ARGS="-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
if [ "$VERBOSE" = true ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_VERBOSE_MAKEFILE=ON"
fi
if [ -n "$INSTALL_PREFIX" ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX"
    print_status "Using install prefix: $INSTALL_PREFIX"
fi

if cmake .. $CMAKE_ARGS; then
    print_success "CMake configuration successful"
else
    print_error "CMake configuration failed"
    exit 1
fi

# Build
print_status "Building project..."
if [ "$VERBOSE" = true ]; then
    cmake --build . --config $BUILD_TYPE --verbose
else
    cmake --build . --config $BUILD_TYPE -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
fi

if [ $? -eq 0 ]; then
    print_success "Build successful"
else
    print_error "Build failed"
    exit 1
fi

# Run tests if requested
if [ "$RUN_TESTS" = true ]; then
    print_status "Running tests..."

    # Run all tests using ctest
    if GTEST_COLOR=1 ctest --output-on-failure -C $BUILD_TYPE; then
        print_success "All tests passed!"
    else
        print_error "Some tests failed"
        exit 1
    fi
fi

print_success "Build completed successfully!"

# Print summary
echo ""
echo "Build Summary:"
echo "  Build Type: $BUILD_TYPE"
echo "  Build Dir:  $BUILD_DIR"
echo "  Tests Run:  $([ "$RUN_TESTS" = true ] && echo "Yes" || echo "No")"

# Provide next steps
echo ""
echo "Next steps:"
echo "  Run all tests:       cd $BUILD_DIR && ctest --output-on-failure"
echo "  Run specific test:   cd $BUILD_DIR && ./tests/test_variant"
echo "  Clean build:         $0 --clean"
echo "  Release build:       $0 --release"