#!/bin/bash

# Master build script for all Windows architectures
# Builds: x64, ARM64

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
NC='\033[0m'

echo -e "${MAGENTA}========================================${NC}"
echo -e "${MAGENTA}Windows Build for libgopher_mcp_auth${NC}"
echo -e "${MAGENTA}All Architectures${NC}"
echo -e "${MAGENTA}========================================${NC}"
echo ""

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Parse arguments
BUILD_TARGET="${1:-all}"

# Function to build Windows x64
build_windows_x64() {
    echo -e "${BLUE}Building Windows x64...${NC}"
    if [ -x "$SCRIPT_DIR/build-windows-x64.sh" ]; then
        "$SCRIPT_DIR/build-windows-x64.sh"
        return $?
    else
        echo -e "${RED}Windows x64 build script not found or not executable${NC}"
        return 1
    fi
}

# Function to build Windows ARM64
build_windows_arm64() {
    echo -e "${BLUE}Building Windows ARM64...${NC}"
    if [ -x "$SCRIPT_DIR/build-windows-arm64.sh" ]; then
        "$SCRIPT_DIR/build-windows-arm64.sh"
        return $?
    else
        echo -e "${RED}Windows ARM64 build script not found or not executable${NC}"
        return 1
    fi
}

# Track build status
BUILDS_ATTEMPTED=0
BUILDS_SUCCEEDED=0
BUILDS_FAILED=0

# Build based on target
case "$BUILD_TARGET" in
    x64|x86_64)
        echo "Building Windows x64 only..."
        BUILDS_ATTEMPTED=$((BUILDS_ATTEMPTED + 1))
        if build_windows_x64; then
            BUILDS_SUCCEEDED=$((BUILDS_SUCCEEDED + 1))
        else
            BUILDS_FAILED=$((BUILDS_FAILED + 1))
            echo -e "${YELLOW}Windows x64 build failed${NC}"
        fi
        ;;
    
    arm64|aarch64)
        echo "Building Windows ARM64 only..."
        BUILDS_ATTEMPTED=$((BUILDS_ATTEMPTED + 1))
        if build_windows_arm64; then
            BUILDS_SUCCEEDED=$((BUILDS_SUCCEEDED + 1))
        else
            BUILDS_FAILED=$((BUILDS_FAILED + 1))
            echo -e "${YELLOW}Windows ARM64 build failed${NC}"
        fi
        ;;
    
    all)
        echo "Building all Windows architectures..."
        echo ""
        
        # Build x64
        echo -e "${YELLOW}[1/2] Windows x64${NC}"
        BUILDS_ATTEMPTED=$((BUILDS_ATTEMPTED + 1))
        if build_windows_x64; then
            BUILDS_SUCCEEDED=$((BUILDS_SUCCEEDED + 1))
            echo -e "${GREEN}✓ Windows x64 complete${NC}"
        else
            BUILDS_FAILED=$((BUILDS_FAILED + 1))
            echo -e "${RED}✗ Windows x64 failed${NC}"
        fi
        echo ""
        
        # Build ARM64
        echo -e "${YELLOW}[2/2] Windows ARM64${NC}"
        BUILDS_ATTEMPTED=$((BUILDS_ATTEMPTED + 1))
        if build_windows_arm64; then
            BUILDS_SUCCEEDED=$((BUILDS_SUCCEEDED + 1))
            echo -e "${GREEN}✓ Windows ARM64 complete${NC}"
        else
            BUILDS_FAILED=$((BUILDS_FAILED + 1))
            echo -e "${RED}✗ Windows ARM64 failed${NC}"
        fi
        ;;
    
    clean)
        echo "Cleaning Windows build outputs..."
        rm -rf "$PROJECT_ROOT/build-output/windows-x64"
        rm -rf "$PROJECT_ROOT/build-output/windows-arm64"
        echo -e "${GREEN}✓ Clean complete${NC}"
        exit 0
        ;;
    
    *)
        echo "Usage: $0 [all|x64|arm64|clean]"
        echo ""
        echo "Options:"
        echo "  all    - Build all architectures (default)"
        echo "  x64    - Build Windows x64 only"
        echo "  arm64  - Build Windows ARM64 only"
        echo "  clean  - Remove all build outputs"
        exit 1
        ;;
esac

# Summary
echo ""
echo -e "${MAGENTA}========================================${NC}"
echo -e "${MAGENTA}Build Summary${NC}"
echo -e "${MAGENTA}========================================${NC}"
echo ""

if [ $BUILDS_SUCCEEDED -eq $BUILDS_ATTEMPTED ] && [ $BUILDS_ATTEMPTED -gt 0 ]; then
    echo -e "${GREEN}✅ All builds successful!${NC}"
    echo ""
    echo "Build outputs:"
    [ -d "$PROJECT_ROOT/build-output/windows-x64" ] && echo "  • windows-x64/   - Windows x86_64 binaries"
    [ -d "$PROJECT_ROOT/build-output/windows-arm64" ] && echo "  • windows-arm64/ - Windows ARM64 binaries"
    echo ""
    echo "All outputs are in: $PROJECT_ROOT/build-output/"
elif [ $BUILDS_SUCCEEDED -gt 0 ]; then
    echo -e "${YELLOW}⚠ Partial success${NC}"
    echo "  Succeeded: $BUILDS_SUCCEEDED"
    echo "  Failed: $BUILDS_FAILED"
    [ -d "$PROJECT_ROOT/build-output/windows-x64" ] && echo "  ✓ windows-x64/"
    [ -d "$PROJECT_ROOT/build-output/windows-arm64" ] && echo "  ✓ windows-arm64/"
else
    echo -e "${RED}❌ All builds failed${NC}"
    echo "  Attempted: $BUILDS_ATTEMPTED"
    echo "  Failed: $BUILDS_FAILED"
fi

echo ""
echo "To deploy Windows binaries:"
echo "  1. Copy the appropriate architecture folder to Windows"
echo "  2. Run the test_auth.exe or test_auth_arm64.exe"
echo ""

# Exit with error if any builds failed
[ $BUILDS_FAILED -gt 0 ] && exit 1
exit 0