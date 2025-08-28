#!/bin/bash

# Build script for creating a dynamic wrapper library from a static library
# This script creates a dynamic library that links to your static MCP Filter library

set -e

# Configuration
STATIC_LIB="libgopher-mcp.a"
WRAPPER_SRC="wrapper.c"
OUTPUT_DIR="../build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== MCP Filter Wrapper Library Builder ===${NC}"

# Check if static library exists
if [ ! -f "$STATIC_LIB" ]; then
    echo -e "${RED}Error: Static library '$STATIC_LIB' not found!${NC}"
    echo "Please place your static library in the current directory."
    exit 1
fi

# Check if wrapper source exists
if [ ! -f "$WRAPPER_SRC" ]; then
    echo -e "${RED}Error: Wrapper source '$WRAPPER_SRC' not found!${NC}"
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Detect platform
PLATFORM=$(uname -s)
ARCH=$(uname -m)

echo -e "${YELLOW}Platform: $PLATFORM${NC}"
echo -e "${YELLOW}Architecture: $ARCH${NC}"

# Build based on platform
case "$PLATFORM" in
    "Darwin")
        # macOS
        if [ "$ARCH" = "x86_64" ] || [ "$ARCH" = "arm64" ]; then
            OUTPUT_LIB="$OUTPUT_DIR/libgopher-mcp.dylib"
            echo -e "${YELLOW}Building macOS dynamic library...${NC}"
            
            gcc -shared -fPIC \
                -o "$OUTPUT_LIB" \
                "$WRAPPER_SRC" \
                -Wl,-undefined,dynamic_lookup
            
            if [ $? -eq 0 ]; then
                echo -e "${GREEN}✅ Successfully built: $OUTPUT_LIB${NC}"
            else
                echo -e "${RED}❌ Build failed!${NC}"
                exit 1
            fi
        else
            echo -e "${RED}Unsupported architecture: $ARCH${NC}"
            exit 1
        fi
        ;;
        
    "Linux")
        # Linux
        if [ "$ARCH" = "x86_64" ] || [ "$ARCH" = "aarch64" ]; then
            OUTPUT_LIB="$OUTPUT_DIR/libgopher-mcp.so"
            echo -e "${YELLOW}Building Linux dynamic library...${NC}"
            
            gcc -shared -fPIC \
                -o "$OUTPUT_LIB" \
                "$WRAPPER_SRC"
            
            if [ $? -eq 0 ]; then
                echo -e "${GREEN}✅ Successfully built: $OUTPUT_LIB${NC}"
            else
                echo -e "${RED}❌ Build failed!${NC}"
                exit 1
            fi
        else
            echo -e "${RED}Unsupported architecture: $ARCH${NC}"
            exit 1
        fi
        ;;
        
    "MINGW"*|"MSYS"*|"CYGWIN"*)
        # Windows (Git Bash, MSYS2, Cygwin)
        OUTPUT_LIB="$OUTPUT_DIR/gopher-mcp.dll"
        echo -e "${YELLOW}Building Windows dynamic library...${NC}"
        echo -e "${YELLOW}Note: This requires a Windows C compiler (cl.exe)${NC}"
        
        # Try to use cl.exe if available
        if command -v cl.exe >/dev/null 2>&1; then
            cl.exe /LD "$WRAPPER_SRC" "$STATIC_LIB" /Fe:"$OUTPUT_LIB"
            
            if [ $? -eq 0 ]; then
                echo -e "${GREEN}✅ Successfully built: $OUTPUT_LIB${NC}"
            else
                echo -e "${RED}❌ Build failed!${NC}"
                exit 1
            fi
        else
            echo -e "${RED}Error: cl.exe not found. Please install Visual Studio or Build Tools.${NC}"
            echo "Alternatively, build on Windows or use WSL."
            exit 1
        fi
        ;;
        
    *)
        echo -e "${RED}Unsupported platform: $PLATFORM${NC}"
        exit 1
        ;;
esac

echo -e "${GREEN}🎉 Wrapper library build completed!${NC}"
echo -e "${YELLOW}The SDK will now automatically detect and use this dynamic library.${NC}"
echo -e "${YELLOW}Output: $OUTPUT_LIB${NC}"

# Show library info
if [ -f "$OUTPUT_LIB" ]; then
    echo -e "\n${GREEN}Library Information:${NC}"
    ls -la "$OUTPUT_LIB"
    
    if command -v file >/dev/null 2>&1; then
        echo -e "\n${GREEN}File Type:${NC}"
        file "$OUTPUT_LIB"
    fi
fi
