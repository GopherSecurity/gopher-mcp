#!/bin/bash

# Build script for cross-compiling libgopher_mcp_auth for Linux x86_64 (Ubuntu 20.04)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=================================================="
echo "Building libgopher_mcp_auth for Linux x86_64"
echo "Target: Ubuntu 20.04"
echo "=================================================="
echo ""

# Build Docker image
echo "🐳 Building Docker image..."
docker build -t mcp-auth-builder:ubuntu20-x64 \
    -f "$SCRIPT_DIR/Dockerfile.ubuntu20-x64" \
    "$PROJECT_ROOT"

# Create output directory on host
OUTPUT_DIR="$PROJECT_ROOT/build-output/linux-x64"
mkdir -p "$OUTPUT_DIR"

# Run container to build the library
echo ""
echo "🔨 Building library in container..."
docker run --rm \
    -v "$OUTPUT_DIR:/host-output" \
    mcp-auth-builder:ubuntu20-x64 \
    bash -c "cp -r /output/* /host-output/"

echo ""
echo "✅ Build complete!"
echo ""
echo "Output files:"
ls -la "$OUTPUT_DIR"

# Check library info
echo ""
echo "Library information:"
if [ -f "$OUTPUT_DIR/libgopher_mcp_auth.so.0.1.0" ]; then
    file "$OUTPUT_DIR/libgopher_mcp_auth.so.0.1.0"
    echo ""
    echo "Library size:"
    du -h "$OUTPUT_DIR/libgopher_mcp_auth.so"*
fi

# Check TypeScript SDK
if [ -d "$OUTPUT_DIR/typescript" ]; then
    echo ""
    echo "TypeScript SDK files:"
    ls -la "$OUTPUT_DIR/typescript/"
fi

echo ""
echo "📦 Linux x86_64 build artifacts are in: $OUTPUT_DIR"
echo ""