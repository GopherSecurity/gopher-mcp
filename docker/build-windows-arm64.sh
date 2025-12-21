#!/bin/bash

# Build script for libgopher_mcp_auth on Windows ARM64
# Cross-compiles using LLVM-MinGW in Docker

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}Building libgopher_mcp_auth for Windows ARM64${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="${PROJECT_ROOT}/build-output/windows-arm64"

# Clean and create output directory
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

echo -e "${YELLOW}Building Windows ARM64 DLL...${NC}"
echo "Note: This creates ARM64 binaries for Windows on ARM devices"
echo ""

# Check if user wants fast (stub) build or full build
if [ "${1}" = "--stub" ] || [ "${1}" = "--fast" ]; then
    echo "Using fast stub build (not real ARM64, for testing only)..."
    DOCKERFILE="Dockerfile.windows-arm64-simple"
else
    echo "Using LLVM-MinGW for real ARM64 support (downloads ~100MB toolchain)..."
    echo "Tip: Use '$0 --stub' for a quick stub build"
    DOCKERFILE="Dockerfile.windows-arm64-llvm"
fi

# Build using selected Dockerfile
docker build \
    -t mcp-auth:windows-arm64 \
    -f "$SCRIPT_DIR/$DOCKERFILE" \
    "$PROJECT_ROOT"

if [ $? -ne 0 ]; then
    echo -e "${RED}Docker build failed${NC}"
    exit 1
fi

echo -e "${YELLOW}Extracting built files...${NC}"

# Run container and copy files
# Create temporary container and copy files manually
CONTAINER_ID=$(docker create mcp-auth:windows-arm64)
# Copy files from container (matching x64 approach)
docker cp "$CONTAINER_ID:/output/gopher_mcp_auth.dll" "$OUTPUT_DIR/" 2>/dev/null || true
docker cp "$CONTAINER_ID:/output/verify_auth.exe" "$OUTPUT_DIR/" 2>/dev/null || true
docker cp "$CONTAINER_ID:/output/gopher_mcp_auth.lib" "$OUTPUT_DIR/" 2>/dev/null || true
docker cp "$CONTAINER_ID:/output/typescript" "$OUTPUT_DIR/" 2>/dev/null || true
docker rm "$CONTAINER_ID" > /dev/null

# Check results
if ls "$OUTPUT_DIR"/*.dll >/dev/null 2>&1 || ls "$OUTPUT_DIR"/*.exe >/dev/null 2>&1; then
    echo -e "${GREEN}✅ Build successful!${NC}"
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
    echo -e "${GREEN}Windows ARM64 build complete!${NC}"
    echo ""
    echo "To test on Windows ARM64 (Surface Pro X, Windows Dev Kit 2023, etc.):"
    echo "  1. Copy build-output/windows-arm64/ to Windows ARM64 device"
    echo "  2. Run verify_auth.exe"
    echo ""
    echo "Note: These binaries are specifically for ARM64 Windows."
    echo "They will NOT run on x86/x64 Windows machines."
else
    echo -e "${RED}Build failed - no DLL or EXE files found${NC}"
    echo "Contents of output directory:"
    ls -la "$OUTPUT_DIR"
    exit 1
fi