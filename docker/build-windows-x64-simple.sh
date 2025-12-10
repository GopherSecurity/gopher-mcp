#!/bin/bash

# Simple build script for Windows x64 using prebuilt MSYS2 libraries
set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo -e "${GREEN}=================================================="
echo "Building libgopher_mcp_auth for Windows x64"
echo "Using: MSYS2 prebuilt OpenSSL and CURL"
echo -e "==================================================${NC}"
echo ""

# Build Docker image
echo -e "${BLUE}🐳 Building Docker image with MSYS2 libraries...${NC}"
docker build -t mcp-auth-builder:windows-x64-simple \
    -f "$SCRIPT_DIR/Dockerfile.windows-x64-simple" \
    "$PROJECT_ROOT"

# Create output directory on host
OUTPUT_DIR="$PROJECT_ROOT/build-output/windows-x64"
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

# Run container to build the library
echo ""
echo -e "${BLUE}🔨 Building library with MSYS2 OpenSSL/CURL...${NC}"
docker run --rm \
    -v "$OUTPUT_DIR:/host-output" \
    mcp-auth-builder:windows-x64-simple \
    bash -c "cp -r /output/* /host-output/ 2>/dev/null || true"

echo ""
echo -e "${GREEN}✅ Build complete!${NC}"
echo ""
echo -e "${YELLOW}Output files:${NC}"
echo "------------------------------------"
ls -lah "$OUTPUT_DIR"

# Check main library
echo ""
echo -e "${YELLOW}Library information:${NC}"
if [ -f "$OUTPUT_DIR/gopher_mcp_auth.dll" ]; then
    echo -e "${GREEN}Main library:${NC}"
    file "$OUTPUT_DIR/gopher_mcp_auth.dll"
    echo "  Size: $(du -h "$OUTPUT_DIR/gopher_mcp_auth.dll" | cut -f1)"
else
    echo -e "${RED}Warning: gopher_mcp_auth.dll not found${NC}"
fi

# Check verification tool
if [ -f "$OUTPUT_DIR/verify_auth.exe" ]; then
    echo ""
    echo -e "${YELLOW}Verification tool:${NC}"
    file "$OUTPUT_DIR/verify_auth.exe"
    echo "  Size: $(du -h "$OUTPUT_DIR/verify_auth.exe" | cut -f1)"
fi

# Total size
echo ""
echo -e "${YELLOW}Total size:${NC} $(du -sh "$OUTPUT_DIR" | cut -f1)"

echo ""
echo -e "${GREEN}📦 Windows x64 build artifacts are in:${NC}"
echo "   $OUTPUT_DIR"
echo ""
echo -e "${BLUE}To test on Windows:${NC}"
echo "  1. Copy the entire build-output/windows-x64/ directory to a Windows machine"
echo "  2. Run: verify_auth.exe"
echo ""
echo -e "${YELLOW}Note:${NC} The DLL uses MSYS2 prebuilt OpenSSL and CURL libraries."
echo "Required DLLs are included in the output directory."
echo ""