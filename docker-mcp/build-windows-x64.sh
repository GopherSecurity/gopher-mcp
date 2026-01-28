#!/bin/bash

# Build script for libgopher-mcp on Windows x64
# Cross-compiles using MinGW-w64 in Docker

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=======================================${NC}"
echo -e "${BLUE}Building libgopher-mcp for Windows x64${NC}"
echo -e "${BLUE}=======================================${NC}"
echo ""

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="${PROJECT_ROOT}/build-output/windows-x64"

# Clean and create output directory
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

echo -e "${YELLOW}Building Windows x64 DLL using MinGW-w64...${NC}"

# Build using the Dockerfile
docker build \
    -t gopher-mcp:windows-x64 \
    -f "$SCRIPT_DIR/Dockerfile.windows-x64" \
    "$PROJECT_ROOT"

if [ $? -ne 0 ]; then
    echo -e "${RED}Docker build failed${NC}"
    exit 1
fi

echo -e "${YELLOW}Extracting built files...${NC}"

# Create temporary container and copy files
CONTAINER_ID=$(docker create gopher-mcp:windows-x64)
docker cp "$CONTAINER_ID:/output/gopher-mcp.dll" "$OUTPUT_DIR/" 2>/dev/null || true
docker cp "$CONTAINER_ID:/output/gopher-mcp.lib" "$OUTPUT_DIR/" 2>/dev/null || true
docker cp "$CONTAINER_ID:/output/gopher_mcp_c.dll" "$OUTPUT_DIR/" 2>/dev/null || true
docker cp "$CONTAINER_ID:/output/gopher_mcp_c.lib" "$OUTPUT_DIR/" 2>/dev/null || true
docker cp "$CONTAINER_ID:/output/verify_mcp.exe" "$OUTPUT_DIR/" 2>/dev/null || true
docker cp "$CONTAINER_ID:/output/include" "$OUTPUT_DIR/" 2>/dev/null || true
docker rm "$CONTAINER_ID" > /dev/null

# Check results
if ls "$OUTPUT_DIR"/*.dll >/dev/null 2>&1 || ls "$OUTPUT_DIR"/*.exe >/dev/null 2>&1; then
    echo -e "${GREEN}Build successful!${NC}"
    echo ""
    echo "Files created:"
    ls -lh "$OUTPUT_DIR"

    if command -v file >/dev/null 2>&1; then
        echo ""
        echo "File information:"
        for f in "$OUTPUT_DIR"/*.dll "$OUTPUT_DIR"/*.exe; do
            [ -f "$f" ] && file "$f"
        done
    fi

    echo ""
    echo -e "${GREEN}Windows x64 build complete!${NC}"
    echo ""
    echo "To test on Windows:"
    echo "  1. Copy build-output/windows-x64/ to Windows system"
    echo "  2. Run verify_mcp.exe"
else
    echo -e "${RED}Build failed - no DLL or EXE files found${NC}"
    echo "Contents of output directory:"
    ls -la "$OUTPUT_DIR"
    exit 1
fi
