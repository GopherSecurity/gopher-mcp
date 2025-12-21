#!/bin/bash

# Build script for libgopher_mcp_auth on Linux ARM64/aarch64
# Uses Docker with pre-built GCC image for fast builds

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
MAGENTA='\033[0;35m'
NC='\033[0m'

echo -e "${MAGENTA}========================================${NC}"
echo -e "${MAGENTA}Building libgopher_mcp_auth for Linux ARM64${NC}"
echo -e "${MAGENTA}========================================${NC}"
echo ""

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="${PROJECT_ROOT}/build-output/linux-arm64"

# Clean and create output directory
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

echo -e "${YELLOW}Building ARM64 library using Docker...${NC}"

# Build using the GCC-based Dockerfile
docker build \
    --platform linux/arm64 \
    -t mcp-auth:arm64 \
    -f "$SCRIPT_DIR/Dockerfile.linux-arm64" \
    "$PROJECT_ROOT"

if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed${NC}"
    exit 1
fi

echo -e "${YELLOW}Extracting built files...${NC}"

# Run container and copy files
docker run --rm \
    --platform linux/arm64 \
    -v "$OUTPUT_DIR:/host-output" \
    mcp-auth:arm64

# Check results
if [ -f "$OUTPUT_DIR/libgopher_mcp_auth.so.0.1.0" ]; then
    echo -e "${GREEN}✅ Build successful!${NC}"
    echo ""
    echo "Files created:"
    ls -lh "$OUTPUT_DIR"
    
    if command -v file >/dev/null 2>&1; then
        echo ""
        echo "Architecture verification:"
        file "$OUTPUT_DIR/libgopher_mcp_auth.so.0.1.0"
    fi
    
    echo ""
    echo -e "${GREEN}ARM64 library built successfully!${NC}"
    echo ""
    echo "To test on ARM64 Linux:"
    echo "  1. Copy build-output/linux-arm64/ to ARM64 system"
    echo "  2. cd linux-arm64"
    echo "  3. LD_LIBRARY_PATH=. ./verify_auth"
else
    echo -e "${RED}Build failed - library not found${NC}"
    exit 1
fi