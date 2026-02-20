#!/bin/bash

# Cross-compile libgopher-mcp for Linux ARM64 using Docker
# Uses x64 container with aarch64-linux-gnu cross-compiler (fast, no QEMU emulation)

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
MAGENTA='\033[0;35m'
NC='\033[0m'

echo -e "${MAGENTA}========================================${NC}"
echo -e "${MAGENTA}Building libgopher-mcp for Linux ARM64${NC}"
echo -e "${MAGENTA}Using cross-compilation (no QEMU)${NC}"
echo -e "${MAGENTA}========================================${NC}"
echo ""

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="${PROJECT_ROOT}/build-output/linux-arm64"

# Check for Docker
if ! command -v docker &> /dev/null; then
    echo -e "${RED}Error: Docker is not installed${NC}"
    echo "Please install Docker Desktop from https://www.docker.com/products/docker-desktop/"
    exit 1
fi

# Clean and create output directory
echo -e "${YELLOW}Cleaning previous builds...${NC}"
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

echo -e "${YELLOW}Building ARM64 library using cross-compilation...${NC}"
echo "This runs at native x64 speed (no QEMU emulation)"
echo ""

# Build using Docker with cross-compilation (no --platform flag needed)
docker build \
    -t gopher-mcp:linux-arm64-cross \
    -f "$SCRIPT_DIR/Dockerfile.linux-arm64-cross" \
    "$PROJECT_ROOT"

if [ $? -ne 0 ]; then
    echo -e "${RED}Docker build failed${NC}"
    exit 1
fi

echo ""
echo -e "${YELLOW}Extracting built files...${NC}"

# Run container and copy files to host (no --platform flag needed)
docker run --rm \
    -v "$OUTPUT_DIR:/host-output" \
    gopher-mcp:linux-arm64-cross

# Check results
if [ -f "$OUTPUT_DIR/libgopher-mcp.so" ] || [ -f "$OUTPUT_DIR/libgopher-mcp.so.0.1.0" ]; then
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Build successful!${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo "Output files:"
    echo "------------------------------------"
    ls -lh "$OUTPUT_DIR"/*.so* 2>/dev/null || true
    ls -lh "$OUTPUT_DIR"/verify_mcp 2>/dev/null || true

    # Show architecture verification
    if command -v file >/dev/null 2>&1; then
        echo ""
        echo "Architecture verification:"
        MAIN_LIB=""
        if [ -f "$OUTPUT_DIR/libgopher-mcp.so.0.1.0" ]; then
            MAIN_LIB="$OUTPUT_DIR/libgopher-mcp.so.0.1.0"
        elif [ -f "$OUTPUT_DIR/libgopher-mcp.so" ]; then
            MAIN_LIB="$OUTPUT_DIR/libgopher-mcp.so"
        fi
        if [ -n "$MAIN_LIB" ]; then
            file "$MAIN_LIB"
        fi
    fi

    echo ""
    echo -e "${GREEN}Output structure:${NC}"
    echo "  build-output/linux-arm64/"
    echo "    ├── libgopher-mcp.so* (main MCP library)"
    echo "    ├── libgopher_mcp_c.so* (C API for FFI)"
    echo "    ├── libfmt.so* (formatting library)"
    echo "    ├── verify_mcp (verification tool)"
    echo "    └── include/ (header files)"
    echo ""
    echo "To test on Linux ARM64:"
    echo "  1. Copy build-output/linux-arm64/ to ARM64 Linux system"
    echo "  2. cd linux-arm64"
    echo "  3. LD_LIBRARY_PATH=. ./verify_mcp"
    echo ""
else
    echo -e "${RED}Build failed - library not found${NC}"
    echo "Contents of output directory:"
    ls -la "$OUTPUT_DIR"
    exit 1
fi
