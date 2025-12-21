#!/bin/bash

# Master build script for libgopher_mcp_auth on Linux
# Builds x86_64 and ARM64 versions

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Parse command line arguments
BUILD_X64=false
BUILD_ARM64=false
CLEAN_ONLY=false
USE_DOCKER=false

# If no arguments, build all
if [ $# -eq 0 ]; then
    BUILD_X64=true
    BUILD_ARM64=true
fi

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        x64|x86_64|amd64)
            BUILD_X64=true
            shift
            ;;
        arm64|aarch64|arm)
            BUILD_ARM64=true
            shift
            ;;
        all)
            BUILD_X64=true
            BUILD_ARM64=true
            shift
            ;;
        docker)
            USE_DOCKER=true
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
            echo "  x64, x86_64, amd64    Build for x86_64 architecture"
            echo "  arm64, aarch64        Build for ARM64 architecture"
            echo "  all                   Build all architectures (default if no args)"
            echo "  docker                Force Docker builds even on native Ubuntu"
            echo "  clean                 Clean all build outputs"
            echo "  --help, -h            Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                    # Build all architectures"
            echo "  $0 x64                # Build only x86_64"
            echo "  $0 arm64              # Build only ARM64"
            echo "  $0 all docker         # Build all using Docker"
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
    echo -e "${YELLOW}Cleaning all Linux build outputs...${NC}"
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
    
    # Remove build directories
    rm -rf "${PROJECT_ROOT}/build-linux-x64"
    rm -rf "${PROJECT_ROOT}/build-linux-arm64"
    rm -rf "${PROJECT_ROOT}/build-linux-x64"
    rm -rf "${PROJECT_ROOT}/build-linux-arm64"
    
    # Remove output directories
    rm -rf "${PROJECT_ROOT}/build-output/linux-x64"
    rm -rf "${PROJECT_ROOT}/build-output/linux-arm64"
    
    # Remove temporary Dockerfiles
    rm -f "${PROJECT_ROOT}/docker/Dockerfile.ubuntu20-arm64"
    
    echo -e "${GREEN}✓ Clean complete${NC}"
}

# If clean only, clean and exit
if [ "$CLEAN_ONLY" = true ]; then
    clean_builds
    exit 0
fi

# Detect if we're on Ubuntu 20.04
IS_UBUNTU_20=false
if [ -f /etc/os-release ]; then
    . /etc/os-release
    if [[ "$ID" == "ubuntu" ]] && [[ "$VERSION_ID" == "20.04" ]]; then
        IS_UBUNTU_20=true
    fi
fi

# Check for Docker
HAS_DOCKER=false
if command -v docker &> /dev/null; then
    HAS_DOCKER=true
fi

# Show build plan
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}Ubuntu 20.04 Build Plan for libgopher_mcp_auth${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""

echo "System info:"
echo "  OS: $(uname -s)"
echo "  Architecture: $(uname -m)"
if [ "$IS_UBUNTU_20" = true ]; then
    echo "  Ubuntu 20.04: ✓"
else
    echo "  Ubuntu 20.04: ✗"
fi
if [ "$HAS_DOCKER" = true ]; then
    echo "  Docker: ✓"
else
    echo "  Docker: ✗"
fi
echo ""

echo "Build targets:"
[ "$BUILD_X64" = true ] && echo "  ✓ x86_64 (AMD64)"
[ "$BUILD_ARM64" = true ] && echo "  ✓ ARM64 (aarch64)"
echo ""

if [ "$USE_DOCKER" = true ]; then
    echo "Build method: Docker (forced)"
elif [ "$IS_UBUNTU_20" = true ] && [ "$USE_DOCKER" = false ]; then
    echo "Build method: Native"
elif [ "$HAS_DOCKER" = true ]; then
    echo "Build method: Docker"
else
    echo -e "${RED}Error: No suitable build method available${NC}"
    echo "Please install Docker or run on Ubuntu 20.04"
    exit 1
fi
echo ""

# Track build results
BUILDS_ATTEMPTED=0
BUILDS_SUCCEEDED=0
FAILED_BUILDS=()

# Build x86_64 if requested
if [ "$BUILD_X64" = true ]; then
    BUILDS_ATTEMPTED=$((BUILDS_ATTEMPTED + 1))
    echo -e "${BLUE}Building x86_64 version...${NC}"
    
    if [ "$USE_DOCKER" = true ] || [ "$IS_UBUNTU_20" = false ]; then
        # Use Docker build
        if [ "$HAS_DOCKER" = true ]; then
            if "${SCRIPT_DIR}/build-linux-x64.sh"; then
                BUILDS_SUCCEEDED=$((BUILDS_SUCCEEDED + 1))
                echo -e "${GREEN}✓ x86_64 build completed successfully${NC}"
            else
                FAILED_BUILDS+=("x86_64")
                echo -e "${RED}✗ x86_64 build failed${NC}"
            fi
        else
            FAILED_BUILDS+=("x86_64")
            echo -e "${RED}✗ x86_64 build failed - Docker not available${NC}"
        fi
    else
        # Use native build script
        if "${SCRIPT_DIR}/build-linux-x64.sh"; then
            BUILDS_SUCCEEDED=$((BUILDS_SUCCEEDED + 1))
            echo -e "${GREEN}✓ x86_64 build completed successfully${NC}"
        else
            FAILED_BUILDS+=("x86_64")
            echo -e "${RED}✗ x86_64 build failed${NC}"
        fi
    fi
    echo ""
fi

# Build ARM64 if requested
if [ "$BUILD_ARM64" = true ]; then
    BUILDS_ATTEMPTED=$((BUILDS_ATTEMPTED + 1))
    echo -e "${MAGENTA}Building ARM64 version...${NC}"
    
    # ARM64 builds typically require Docker for cross-compilation
    if "${SCRIPT_DIR}/build-linux-arm64.sh"; then
        BUILDS_SUCCEEDED=$((BUILDS_SUCCEEDED + 1))
        echo -e "${GREEN}✓ ARM64 build completed successfully${NC}"
    else
        FAILED_BUILDS+=("ARM64")
        echo -e "${RED}✗ ARM64 build failed${NC}"
    fi
    echo ""
fi

# Summary
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}Build Summary${NC}"
echo -e "${CYAN}========================================${NC}"
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
    
    if [ -d "${PROJECT_ROOT}/build-output/linux-x64" ]; then
        echo "  • x86_64:  build-output/linux-x64/"
        if [ -f "${PROJECT_ROOT}/build-output/linux-x64/libgopher_mcp_auth.so.0.1.0" ]; then
            file "${PROJECT_ROOT}/build-output/linux-x64/libgopher_mcp_auth.so.0.1.0" | sed 's/^/      /'
        fi
    fi
    
    if [ -d "${PROJECT_ROOT}/build-output/linux-arm64" ]; then
        echo "  • ARM64:   build-output/linux-arm64/"
        if [ -f "${PROJECT_ROOT}/build-output/linux-arm64/libgopher_mcp_auth.so.0.1.0" ]; then
            file "${PROJECT_ROOT}/build-output/linux-arm64/libgopher_mcp_auth.so.0.1.0" | sed 's/^/      /'
        fi
    fi
    
    echo ""
    echo "Each output directory contains:"
    echo "  - libgopher_mcp_auth.so.0.1.0 (main library)"
    echo "  - libgopher_mcp_auth.so.0 (soname symlink)"
    echo "  - libgopher_mcp_auth.so (development symlink)"
    echo "  - verify_auth (verification tool)"
fi

# Clean up temporary build directories if all builds succeeded
if [ ${#FAILED_BUILDS[@]} -eq 0 ] && [ $BUILDS_SUCCEEDED -gt 0 ]; then
    echo ""
    echo -e "${YELLOW}Cleaning up temporary build directories...${NC}"
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
    
    # Remove only temporary build directories, keep outputs
    rm -rf "${PROJECT_ROOT}/build-linux-x64"
    rm -rf "${PROJECT_ROOT}/build-linux-arm64"
    rm -rf "${PROJECT_ROOT}/build-linux-x64"
    rm -rf "${PROJECT_ROOT}/build-linux-arm64"
    
    # Remove temporary Dockerfiles
    rm -f "${PROJECT_ROOT}/docker/Dockerfile.ubuntu20-arm64"
    
    echo -e "${GREEN}✓ Cleanup complete${NC}"
fi

# Exit with error if any builds failed
if [ ${#FAILED_BUILDS[@]} -gt 0 ]; then
    exit 1
fi

echo ""
echo -e "${GREEN}✨ Done!${NC}"
echo ""
echo "To deploy on Ubuntu 20.04:"
echo "  1. Copy the appropriate build-output/linux-{arch}/ directory"
echo "  2. Install runtime dependencies:"
echo "     sudo apt-get install libcurl4 libssl1.1"
echo "  3. Test the library:"
echo "     cd linux-{arch}"
echo "     LD_LIBRARY_PATH=. ./verify_auth"
echo ""