#!/bin/bash

# Script to build and test the C# SDK for MCP
# Usage: ./build.sh [options]
# Options:
#   --release     Build in Release configuration (default: Debug)
#   --no-test     Skip running tests
#   --no-restore  Skip package restore
#   --clean       Clean before building
#   --help        Show this help message

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get script directory
DIR_CURR=$(cd "$(dirname "$0")"; pwd)
cd "$DIR_CURR"

# Default options
CONFIGURATION="Debug"
RUN_TESTS=true
RUN_RESTORE=true
RUN_CLEAN=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --release)
            CONFIGURATION="Release"
            shift
            ;;
        --no-test)
            RUN_TESTS=false
            shift
            ;;
        --no-restore)
            RUN_RESTORE=false
            shift
            ;;
        --clean)
            RUN_CLEAN=true
            shift
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  --release     Build in Release configuration (default: Debug)"
            echo "  --no-test     Skip running tests"
            echo "  --no-restore  Skip package restore"
            echo "  --clean       Clean before building"
            echo "  --help        Show this help message"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to compare version strings
version_ge() {
    # Returns 0 if $1 >= $2
    [ "$(printf '%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]
}

echo -e "${GREEN}=== MCP C# SDK Build Script ===${NC}"
echo

# Check for .NET SDK installation
echo -e "${YELLOW}Checking .NET SDK installation...${NC}"
if ! command_exists dotnet; then
    echo -e "${RED}Error: .NET SDK is not installed!${NC}"
    echo "Please install .NET SDK from: https://dotnet.microsoft.com/download"
    echo
    echo "For macOS:"
    echo "  brew install --cask dotnet-sdk"
    echo
    echo "For Ubuntu/Debian:"
    echo "  wget https://dot.net/v1/dotnet-install.sh"
    echo "  chmod +x dotnet-install.sh"
    echo "  ./dotnet-install.sh"
    echo
    echo "For Windows:"
    echo "  Download installer from https://dotnet.microsoft.com/download"
    exit 1
fi

# Get .NET SDK version
DOTNET_VERSION=$(dotnet --version)
echo -e "Found .NET SDK version: ${GREEN}$DOTNET_VERSION${NC}"

# Check minimum version (6.0.0)
MIN_VERSION="6.0.0"
if ! version_ge "$DOTNET_VERSION" "$MIN_VERSION"; then
    echo -e "${RED}Error: .NET SDK version $MIN_VERSION or higher is required!${NC}"
    echo "Current version: $DOTNET_VERSION"
    echo "Please update your .NET SDK installation."
    exit 1
fi

# List installed .NET runtimes
echo
echo -e "${YELLOW}Installed .NET runtimes:${NC}"
dotnet --list-runtimes | grep -E "Microsoft.NETCore.App|Microsoft.AspNetCore.App" | while read -r line; do
    echo "  - $line"
done

# List installed .NET SDKs
echo
echo -e "${YELLOW}Installed .NET SDKs:${NC}"
dotnet --list-sdks | while read -r line; do
    echo "  - $line"
done

echo
echo -e "${YELLOW}Build configuration: ${GREEN}$CONFIGURATION${NC}"
echo

# Clean if requested
if [ "$RUN_CLEAN" = true ]; then
    echo -e "${YELLOW}Cleaning previous build artifacts...${NC}"
    dotnet clean --configuration "$CONFIGURATION" --verbosity minimal
    echo -e "${GREEN}✓ Clean completed${NC}"
    echo
fi

# Restore packages
if [ "$RUN_RESTORE" = true ]; then
    echo -e "${YELLOW}Restoring NuGet packages...${NC}"
    if dotnet restore --verbosity minimal; then
        echo -e "${GREEN}✓ Package restore completed${NC}"
    else
        echo -e "${RED}✗ Package restore failed${NC}"
        exit 1
    fi
    echo
fi

# Build the solution
echo -e "${YELLOW}Building solution...${NC}"
if dotnet build --configuration "$CONFIGURATION" --no-restore --verbosity minimal; then
    echo -e "${GREEN}✓ Build completed successfully${NC}"
else
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi
echo

# Run tests if not disabled
if [ "$RUN_TESTS" = true ]; then
    echo -e "${YELLOW}Running tests...${NC}"
    
    # Run tests with better output formatting
    if dotnet test --configuration "$CONFIGURATION" --no-build --verbosity minimal --logger "console;verbosity=detailed" | tee test-output.tmp; then
        # Extract test summary
        TOTAL=$(grep -E "Total tests:|测试总数:" test-output.tmp | tail -1 | grep -oE "[0-9]+" | tail -1)
        PASSED=$(grep -E "Passed:|通过数:" test-output.tmp | tail -1 | grep -oE "[0-9]+" | tail -1)
        FAILED=$(grep -E "Failed:|失败:" test-output.tmp | tail -1 | grep -oE "[0-9]+" | tail -1)
        SKIPPED=$(grep -E "Skipped:|已跳过:" test-output.tmp | tail -1 | grep -oE "[0-9]+" | tail -1)
        
        rm -f test-output.tmp
        
        echo
        echo -e "${GREEN}✓ Test run completed${NC}"
        echo -e "  Total:   $TOTAL"
        echo -e "  Passed:  ${GREEN}$PASSED${NC}"
        if [ "$FAILED" != "0" ] && [ -n "$FAILED" ]; then
            echo -e "  Failed:  ${RED}$FAILED${NC}"
        fi
        if [ "$SKIPPED" != "0" ] && [ -n "$SKIPPED" ]; then
            echo -e "  Skipped: ${YELLOW}$SKIPPED${NC}"
        fi
        
        if [ "$FAILED" != "0" ] && [ -n "$FAILED" ]; then
            exit 1
        fi
    else
        rm -f test-output.tmp
        echo -e "${RED}✗ Test run failed${NC}"
        exit 1
    fi
    echo
fi

# Show output locations
echo -e "${GREEN}=== Build Outputs ===${NC}"
echo -e "Library: ${YELLOW}src/bin/$CONFIGURATION/${NC}"
echo -e "Example: ${YELLOW}examples/bin/$CONFIGURATION/${NC}"
echo -e "Tests:   ${YELLOW}tests/bin/$CONFIGURATION/${NC}"
echo

# Success message
echo -e "${GREEN}✅ Build completed successfully!${NC}"

# Show next steps
echo
echo -e "${YELLOW}Next steps:${NC}"
echo "  - Run example: dotnet run --project examples/GopherMcpExample.csproj"
echo "  - Create NuGet package: dotnet pack src/GopherMcp.csproj"
echo "  - View test results: dotnet test --logger html"