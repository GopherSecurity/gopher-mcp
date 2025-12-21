#!/bin/sh

# Script to check Docker availability and provide installation instructions

echo "========================================="
echo "Docker Environment Check"
echo "========================================="
echo ""

# Check if Docker is installed
if command -v docker >/dev/null 2>&1; then
    echo "✅ Docker is installed"
    echo ""
    docker --version
    
    # Check if Docker daemon is running
    if docker info >/dev/null 2>&1; then
        echo "✅ Docker daemon is running"
    else
        echo "❌ Docker daemon is not running"
        echo ""
        echo "Please start Docker Desktop:"
        echo "  • Open Docker Desktop application"
        echo "  • Wait for Docker to start (icon in menu bar)"
        echo "  • Try the build script again"
    fi
else
    echo "❌ Docker is not installed or not in PATH"
    echo ""
    echo "To build Ubuntu binaries on macOS, you need Docker."
    echo ""
    echo "Installation options:"
    echo ""
    echo "1. Docker Desktop (Recommended):"
    echo "   Download from: https://www.docker.com/products/docker-desktop"
    echo ""
    echo "2. Using Homebrew:"
    echo "   brew install --cask docker"
    echo ""
    echo "3. Colima (Alternative, lighter weight):"
    echo "   brew install colima docker"
    echo "   colima start"
    echo ""
    echo "After installation:"
    echo "  1. Start Docker Desktop or Colima"
    echo "  2. Run: docker --version (to verify)"
    echo "  3. Run the build scripts again"
fi

echo ""
echo "========================================="
echo "Available Build Scripts"
echo "========================================="
echo ""
echo "For macOS (no Docker required):"
echo "  • ./docker/build-mac-x64.sh        - Intel Macs"
echo "  • ./docker/build-mac-arm64-cross.sh - Apple Silicon (cross-compile)"
echo "  • ./docker/build-mac-universal.sh   - Universal binary"
echo ""
echo "For Ubuntu 20.04 (Docker required):"
echo "  • ./docker/build-ubuntu20-docker.sh       - x86_64"
echo "  • ./docker/build-ubuntu20-arm64-docker.sh - ARM64"
echo "  • ./docker/build-linux-x64.sh             - x86_64 (original)"
echo ""