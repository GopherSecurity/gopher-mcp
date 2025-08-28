#!/bin/bash

# Build script for MCP filter wrapper
# This script builds a dynamic library with enhanced functionality

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Building MCP Filter Wrapper...${NC}"

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_DIR="$SCRIPT_DIR/.."

echo "Script directory: $SCRIPT_DIR"
echo "SDK directory: $SDK_DIR"

# Check if we're on macOS
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo -e "${YELLOW}Detected macOS${NC}"
    
    # Build the wrapper
    echo "Building wrapper.cpp..."
    
    g++ -shared -fPIC \
        -o "$SDK_DIR/build/libgopher-mcp.dylib" \
        wrapper.cpp \
        -std=c++17 \
        -Wl,-undefined,dynamic_lookup
    
    if [[ $? -eq 0 ]]; then
        echo -e "${GREEN}Successfully built libgopher-mcp.dylib${NC}"
        echo "Output: $SDK_DIR/build/libgopher-mcp.dylib"
    else
        echo -e "${RED}Build failed${NC}"
        exit 1
    fi

elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo -e "${YELLOW}Detected Linux${NC}"
    
    # Build the wrapper for Linux
    echo "Building wrapper.cpp for Linux..."
    
    g++ -shared -fPIC \
        -o "$SDK_DIR/build/libgopher-mcp.so" \
        wrapper.cpp \
        -std=c++17
    
    if [[ $? -eq 0 ]]; then
        echo -e "${GREEN}Successfully built libgopher-mcp.so${NC}"
        echo "Output: $SDK_DIR/build/libgopher-mcp.so"
    else
        echo -e "${RED}Build failed${NC}"
        exit 1
    fi

elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]]; then
    echo -e "${YELLOW}Detected Windows${NC}"
    
    # Build the wrapper for Windows
    echo "Building wrapper.cpp for Windows..."
    
    cl.exe /LD \
        wrapper.cpp \
        /Fe"$SDK_DIR/build/gopher-mcp.dll"
    
    if [[ $? -eq 0 ]]; then
        echo -e "${GREEN}Successfully built gopher-mcp.dll${NC}"
        echo "Output: $SDK_DIR/build/gopher-mcp.dll"
    else
        echo -e "${RED}Build failed${NC}"
        exit 1
    fi

else
    echo -e "${RED}Unsupported operating system: $OSTYPE${NC}"
    exit 1
fi

echo -e "${GREEN}Build completed successfully!${NC}"
echo ""
echo "Next steps:"
echo "1. Test the wrapper functionality"
echo "2. Verify enhanced data processing capabilities"
echo "3. Test all examples with the enhanced functionality"
