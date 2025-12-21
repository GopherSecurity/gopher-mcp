#!/bin/bash

# Master build script for libgopher_mcp_auth on macOS
# Builds x86_64, arm64, and universal binary versions

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
NC='\033[0m' # No Color

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Parse command line arguments
BUILD_X64=false
BUILD_ARM64=false
BUILD_UNIVERSAL=false
CLEAN_ONLY=false

# If no arguments, build all
if [ $# -eq 0 ]; then
    BUILD_X64=true
    BUILD_ARM64=true
    BUILD_UNIVERSAL=true
fi

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        x64|x86_64|intel)
            BUILD_X64=true
            shift
            ;;
        arm64|arm|silicon|m1|m2|m3)
            BUILD_ARM64=true
            shift
            ;;
        universal|uni|both)
            BUILD_UNIVERSAL=true
            shift
            ;;
        all)
            BUILD_X64=true
            BUILD_ARM64=true
            BUILD_UNIVERSAL=true
            shift
            ;;
        clean)
            CLEAN_ONLY=true
            shift
            ;;
        --help|-h|help)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  x64, x86_64, intel    Build for x86_64 architecture"
            echo "  arm64, arm, silicon   Build for arm64 architecture (Apple Silicon)"
            echo "  universal, uni        Build universal binary (x86_64 + arm64)"
            echo "  all                   Build all architectures (default if no args)"
            echo "  clean                 Clean all build outputs"
            echo "  --help, -h            Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                    # Build all architectures"
            echo "  $0 x64                # Build only x86_64"
            echo "  $0 arm64              # Build only arm64"
            echo "  $0 universal          # Build only universal binary"
            echo "  $0 x64 arm64          # Build x86_64 and arm64"
            echo "  $0 clean              # Clean all build outputs"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Clean function
clean_builds() {
    echo -e "${YELLOW}Cleaning all build outputs...${NC}"
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
    
    # Remove build directories
    rm -rf "${PROJECT_ROOT}/build-mac-x64"
    rm -rf "${PROJECT_ROOT}/build-mac-arm64"
    rm -rf "${PROJECT_ROOT}/build-mac-arm64-cross"
    rm -rf "${PROJECT_ROOT}/build-mac-x64-for-universal"
    rm -rf "${PROJECT_ROOT}/build-mac-arm64-for-universal"
    
    # Remove output directories
    rm -rf "${PROJECT_ROOT}/build-output/mac-x64"
    rm -rf "${PROJECT_ROOT}/build-output/mac-arm64"
    rm -rf "${PROJECT_ROOT}/build-output/mac-universal"
    
    echo -e "${GREEN}✓ Clean complete${NC}"
}

# If clean only, clean and exit
if [ "$CLEAN_ONLY" = true ]; then
    clean_builds
    exit 0
fi

# Show build plan
echo -e "${MAGENTA}========================================${NC}"
echo -e "${MAGENTA}macOS Build Plan for libgopher_mcp_auth${NC}"
echo -e "${MAGENTA}========================================${NC}"
echo ""
echo "Will build:"
[ "$BUILD_X64" = true ] && echo "  ✓ x86_64 (Intel)"
[ "$BUILD_ARM64" = true ] && echo "  ✓ arm64 (Apple Silicon)"
[ "$BUILD_UNIVERSAL" = true ] && echo "  ✓ Universal Binary"
echo ""

# Track build results
BUILDS_ATTEMPTED=0
BUILDS_SUCCEEDED=0
FAILED_BUILDS=()

# Build x86_64 if requested
if [ "$BUILD_X64" = true ]; then
    BUILDS_ATTEMPTED=$((BUILDS_ATTEMPTED + 1))
    echo -e "${BLUE}Building x86_64 version...${NC}"
    if "${SCRIPT_DIR}/build-mac-x64.sh"; then
        BUILDS_SUCCEEDED=$((BUILDS_SUCCEEDED + 1))
        echo -e "${GREEN}✓ x86_64 build completed successfully${NC}"
    else
        FAILED_BUILDS+=("x86_64")
        echo -e "${RED}✗ x86_64 build failed${NC}"
    fi
    echo ""
fi

# Build arm64 if requested
if [ "$BUILD_ARM64" = true ]; then
    BUILDS_ATTEMPTED=$((BUILDS_ATTEMPTED + 1))
    echo -e "${BLUE}Building arm64 version...${NC}"
    
    # Detect if we're on Intel Mac and need cross-compilation
    if [[ $(uname -m) == "x86_64" ]]; then
        echo -e "${YELLOW}  Detected Intel Mac - using cross-compilation for ARM64${NC}"
        if "${SCRIPT_DIR}/build-mac-arm64-cross.sh"; then
            BUILDS_SUCCEEDED=$((BUILDS_SUCCEEDED + 1))
            echo -e "${GREEN}✓ arm64 build completed successfully (cross-compiled)${NC}"
        else
            FAILED_BUILDS+=("arm64")
            echo -e "${RED}✗ arm64 build failed${NC}"
        fi
    else
        # On ARM Mac, use native build
        if "${SCRIPT_DIR}/build-mac-arm64.sh"; then
            BUILDS_SUCCEEDED=$((BUILDS_SUCCEEDED + 1))
            echo -e "${GREEN}✓ arm64 build completed successfully${NC}"
        else
            FAILED_BUILDS+=("arm64")
            echo -e "${RED}✗ arm64 build failed${NC}"
        fi
    fi
    echo ""
fi

# Build universal if requested
if [ "$BUILD_UNIVERSAL" = true ]; then
    BUILDS_ATTEMPTED=$((BUILDS_ATTEMPTED + 1))
    echo -e "${BLUE}Building universal binary...${NC}"
    if "${SCRIPT_DIR}/build-mac-universal.sh"; then
        BUILDS_SUCCEEDED=$((BUILDS_SUCCEEDED + 1))
        echo -e "${GREEN}✓ Universal binary build completed successfully${NC}"
    else
        FAILED_BUILDS+=("universal")
        echo -e "${RED}✗ Universal binary build failed${NC}"
    fi
    echo ""
fi

# Summary
echo -e "${MAGENTA}========================================${NC}"
echo -e "${MAGENTA}Build Summary${NC}"
echo -e "${MAGENTA}========================================${NC}"
echo ""
echo "Builds attempted: $BUILDS_ATTEMPTED"
echo "Builds succeeded: $BUILDS_SUCCEEDED"

if [ ${#FAILED_BUILDS[@]} -gt 0 ]; then
    echo -e "${RED}Builds failed: ${FAILED_BUILDS[*]}${NC}"
else
    echo -e "${GREEN}All builds completed successfully!${NC}"
fi

echo ""

# Show output locations if any builds succeeded
if [ $BUILDS_SUCCEEDED -gt 0 ]; then
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
    echo "Build outputs:"
    
    if [ "$BUILD_X64" = true ] && [ -d "${PROJECT_ROOT}/build-output/mac-x64" ]; then
        echo "  • x86_64:    build-output/mac-x64/"
    fi
    
    if [ "$BUILD_ARM64" = true ] && [ -d "${PROJECT_ROOT}/build-output/mac-arm64" ]; then
        echo "  • arm64:     build-output/mac-arm64/"
    fi
    
    if [ "$BUILD_UNIVERSAL" = true ] && [ -d "${PROJECT_ROOT}/build-output/mac-universal" ]; then
        echo "  • Universal: build-output/mac-universal/"
    fi
    
    echo ""
    echo "Each output directory contains:"
    echo "  - libgopher_mcp_auth.0.1.0.dylib (main library)"
    echo "  - libgopher_mcp_auth.dylib (symlink)"
    echo "  - verify_auth (verification tool)"
    echo "  - typescript/ (SDK and tests)"
fi

# Clean up temporary build directories if all builds succeeded
if [ ${#FAILED_BUILDS[@]} -eq 0 ] && [ $BUILDS_SUCCEEDED -gt 0 ]; then
    echo -e "${YELLOW}Cleaning up temporary build directories...${NC}"
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
    
    # Remove only temporary build directories, keep outputs
    rm -rf "${PROJECT_ROOT}/build-mac-x64"
    rm -rf "${PROJECT_ROOT}/build-mac-arm64"
    rm -rf "${PROJECT_ROOT}/build-mac-arm64-cross"
    rm -rf "${PROJECT_ROOT}/build-mac-x64-for-universal"
    rm -rf "${PROJECT_ROOT}/build-mac-arm64-for-universal"
    
    echo -e "${GREEN}✓ Cleanup complete${NC}"
fi

# Exit with error if any builds failed
if [ ${#FAILED_BUILDS[@]} -gt 0 ]; then
    exit 1
fi