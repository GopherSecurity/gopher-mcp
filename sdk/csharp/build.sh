#!/usr/bin/env bash

# Build script for GopherMcp C# SDK
# Builds the SDK, tests, and runs all unit tests

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Default values
BUILD_CONFIG="Release"
RUN_TESTS=true
CLEAN_BUILD=false
VERBOSE=false
PACK_NUGET=false
BUILD_EXAMPLES=true

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_CONFIG="Debug"
            shift
            ;;
        --no-tests)
            RUN_TESTS=false
            shift
            ;;
        --no-examples)
            BUILD_EXAMPLES=false
            shift
            ;;
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        --pack)
            PACK_NUGET=true
            shift
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  --debug        Build in Debug configuration (default: Release)"
            echo "  --no-tests     Skip running unit tests (tests run by default)"
            echo "  --no-examples  Skip building example projects"
            echo "  --clean        Clean before building"
            echo "  --verbose      Enable verbose output"
            echo "  --pack         Create NuGet package"
            echo "  --help         Show this help message"
            echo ""
            echo "Default behavior:"
            echo "  - Builds SDK in Release mode"
            echo "  - Runs all unit tests"
            echo "  - Builds example projects"
            echo "  - Shows test results summary"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Building GopherMcp C# SDK${NC}"
echo -e "${GREEN}========================================${NC}"
echo "Configuration: $BUILD_CONFIG"
echo "Run Tests: $RUN_TESTS"
echo "Clean Build: $CLEAN_BUILD"
echo ""

# Check if dotnet is installed
if ! command -v dotnet &> /dev/null; then
    echo -e "${RED}Error: .NET SDK is not installed${NC}"
    echo "Please install .NET SDK from https://dotnet.microsoft.com/download"
    exit 1
fi

# Display .NET version
echo -e "${YELLOW}Using .NET SDK:${NC}"
dotnet --version
echo ""

# Clean if requested
if [ "$CLEAN_BUILD" = true ]; then
    echo -e "${YELLOW}Cleaning previous builds...${NC}"
    dotnet clean -c $BUILD_CONFIG --nologo
    rm -rf bin obj src/bin src/obj tests/bin tests/obj examples/bin examples/obj
    echo -e "${GREEN}Clean completed${NC}"
    echo ""
fi

# Restore dependencies
echo -e "${YELLOW}Restoring dependencies...${NC}"
if [ "$VERBOSE" = true ]; then
    dotnet restore --verbosity normal
else
    dotnet restore --nologo
fi
echo -e "${GREEN}Restore completed${NC}"
echo ""

# Build the main SDK project
echo -e "${YELLOW}Building GopherMcp SDK...${NC}"
if [ "$VERBOSE" = true ]; then
    dotnet build src/GopherMcp.csproj -c $BUILD_CONFIG --verbosity normal
else
    dotnet build src/GopherMcp.csproj -c $BUILD_CONFIG --nologo
fi

if [ $? -eq 0 ]; then
    echo -e "${GREEN}SDK build successful${NC}"
else
    echo -e "${RED}SDK build failed${NC}"
    exit 1
fi
echo ""

# Build the test project
if [ "$RUN_TESTS" = true ]; then
    echo -e "${YELLOW}Building test project...${NC}"
    if [ "$VERBOSE" = true ]; then
        dotnet build tests/GopherMcp.Tests.csproj -c $BUILD_CONFIG --verbosity normal
    else
        dotnet build tests/GopherMcp.Tests.csproj -c $BUILD_CONFIG --nologo
    fi
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}Test project build successful${NC}"
    else
        echo -e "${RED}Test project build failed${NC}"
        exit 1
    fi
    echo ""
fi

# Build example projects if requested
if [ "$BUILD_EXAMPLES" = true ]; then
    echo -e "${YELLOW}Building example projects...${NC}"
    EXAMPLE_BUILD_SUCCESS=true
    for project in examples/*.csproj; do
        if [ -f "$project" ]; then
            # Check if the example has source files
            PROJECT_DIR=$(dirname "$project")
            if ls "$PROJECT_DIR"/*.cs 2>/dev/null | head -1 > /dev/null; then
                echo "Building $(basename $project)..."
                if [ "$VERBOSE" = true ]; then
                    dotnet build "$project" -c $BUILD_CONFIG --verbosity normal 2>&1 | tee /tmp/build_output.txt
                else
                    dotnet build "$project" -c $BUILD_CONFIG --nologo 2>&1 | tee /tmp/build_output.txt
                fi
                
                if [ ${PIPESTATUS[0]} -eq 0 ]; then
                    echo -e "${GREEN}✓ $(basename $project) build successful${NC}"
                else
                    # Check if it failed due to missing Main method
                    if grep -q "CS5001" /tmp/build_output.txt; then
                        echo -e "${YELLOW}⚠ $(basename $project): No Main method (example not implemented yet)${NC}"
                    else
                        echo -e "${YELLOW}⚠ $(basename $project) build failed${NC}"
                        EXAMPLE_BUILD_SUCCESS=false
                    fi
                fi
            else
                echo -e "${YELLOW}⚠ $(basename $project): No source files yet${NC}"
            fi
        fi
    done
    
    if [ "$EXAMPLE_BUILD_SUCCESS" = false ]; then
        echo -e "${YELLOW}Some examples failed to build (this is okay if they're not implemented yet)${NC}"
    fi
    echo ""
fi

# Run tests if requested
if [ "$RUN_TESTS" = true ]; then
    echo -e "${YELLOW}========================================${NC}"
    echo -e "${YELLOW}Running Unit Tests${NC}"
    echo -e "${YELLOW}========================================${NC}"
    
    # Check and prepare native library
    NATIVE_LIB_WARNING=false
    NATIVE_LIB_COPIED=false
    
    # Determine platform-specific paths
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # Detect architecture
        if [[ $(uname -m) == "arm64" ]]; then
            PLATFORM_DIR="osx-arm64"
        else
            PLATFORM_DIR="osx-x64"
        fi
        NATIVE_LIB_NAME="libgopher_mcp_c.0.1.0.dylib"
        NATIVE_LIB_LINK="libgopher_mcp_c.dylib"
        # Also support unversioned name if versioned doesn't exist
        NATIVE_LIB_ALT="libgopher_mcp_c.dylib"
    elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
        if [[ $(uname -m) == "aarch64" ]]; then
            PLATFORM_DIR="linux-arm64"
        else
            PLATFORM_DIR="linux-x64"
        fi
        NATIVE_LIB_NAME="libgopher_mcp_c.so.0.1.0"
        NATIVE_LIB_LINK="libgopher_mcp_c.so"
    elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]]; then
        PLATFORM_DIR="win-x64"
        NATIVE_LIB_NAME="gopher_mcp_c.0.1.0.dll"
        NATIVE_LIB_LINK="gopher_mcp_c.dll"
    else
        PLATFORM_DIR="unknown"
        NATIVE_LIB_NAME=""
    fi
    
    # Check if native library exists in runtime directory
    if [ ! -z "$NATIVE_LIB_NAME" ] && [ ! -f "src/runtimes/$PLATFORM_DIR/native/$NATIVE_LIB_NAME" ]; then
        # Try to find it in the build directory (check both locations)
        if [ -f "../../build/src/c_api/$NATIVE_LIB_NAME" ]; then
            echo -e "${YELLOW}Native library found in build/src/c_api/. Copying to runtime directory...${NC}"
            mkdir -p "src/runtimes/$PLATFORM_DIR/native"
            cp "../../build/src/c_api/$NATIVE_LIB_NAME" "src/runtimes/$PLATFORM_DIR/native/"
            NATIVE_LIB_COPIED=true
        elif [ -f "../../build/$NATIVE_LIB_NAME" ]; then
            echo -e "${YELLOW}Native library found in build directory. Copying to runtime directory...${NC}"
            mkdir -p "src/runtimes/$PLATFORM_DIR/native"
            cp "../../build/$NATIVE_LIB_NAME" "src/runtimes/$PLATFORM_DIR/native/"
            NATIVE_LIB_COPIED=true
        else
            NATIVE_LIB_WARNING=true
        fi
    fi
    
    # Copy native library to test output directories for all target frameworks
    if [ ! -z "$NATIVE_LIB_NAME" ]; then
        for framework in net6.0 net7.0 net8.0; do
            TEST_OUTPUT_DIR="tests/bin/$BUILD_CONFIG/$framework"
            if [ -d "$TEST_OUTPUT_DIR" ]; then
                if [ -f "src/runtimes/$PLATFORM_DIR/native/$NATIVE_LIB_NAME" ]; then
                    cp "src/runtimes/$PLATFORM_DIR/native/$NATIVE_LIB_NAME" "$TEST_OUTPUT_DIR/" 2>/dev/null
                    # Create symbolic link with simplified name
                    if [ ! -z "$NATIVE_LIB_LINK" ]; then
                        (cd "$TEST_OUTPUT_DIR" && ln -sf "$NATIVE_LIB_NAME" "$NATIVE_LIB_LINK" 2>/dev/null)
                    fi
                elif [ -f "../../build/src/c_api/$NATIVE_LIB_NAME" ]; then
                    cp "../../build/src/c_api/$NATIVE_LIB_NAME" "$TEST_OUTPUT_DIR/" 2>/dev/null
                    if [ ! -z "$NATIVE_LIB_LINK" ]; then
                        (cd "$TEST_OUTPUT_DIR" && ln -sf "$NATIVE_LIB_NAME" "$NATIVE_LIB_LINK" 2>/dev/null)
                    fi
                elif [ -f "../../build/$NATIVE_LIB_NAME" ]; then
                    cp "../../build/$NATIVE_LIB_NAME" "$TEST_OUTPUT_DIR/" 2>/dev/null
                    if [ ! -z "$NATIVE_LIB_LINK" ]; then
                        (cd "$TEST_OUTPUT_DIR" && ln -sf "$NATIVE_LIB_NAME" "$NATIVE_LIB_LINK" 2>/dev/null)
                    fi
                fi
            fi
        done
    fi
    
    if [ "$NATIVE_LIB_WARNING" = true ]; then
        echo -e "${YELLOW}⚠️  Native Library Warning${NC}"
        echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        echo "Native library not found. Tests will fail when trying to call native functions."
        echo ""
        echo "To fix this:"
        echo "1. Build the C++ SDK: (cd ../.. && make)"
        echo "2. The library will be automatically copied when you run this script again"
        echo ""
        echo "Expected library: $NATIVE_LIB_NAME"
        echo "Platform: $PLATFORM_DIR"
        echo ""
    elif [ "$NATIVE_LIB_COPIED" = true ]; then
        echo -e "${GREEN}✓ Native library copied successfully${NC}"
        echo ""
    fi
    
    # Run tests with detailed output
    echo ""
    echo "Running tests with detailed results..."
    
    # Create results directory
    mkdir -p TestResults
    
    # Note: We run tests for only one framework (net8.0) to avoid the 
    # "event_enable_debug_mode was called twice" error from libevent.
    # This happens because the static initializer in the C++ library runs
    # multiple times when .NET loads the native library for each framework.
    if [ "$VERBOSE" = true ]; then
        dotnet test tests/GopherMcp.Tests.csproj \
            -c $BUILD_CONFIG \
            --framework net8.0 \
            --no-build \
            --logger:"console;verbosity=detailed" \
            --logger:"trx;LogFileName=TestResults.trx" \
            --logger:"html;LogFileName=TestResults.html" \
            --collect:"XPlat Code Coverage" \
            --results-directory:"TestResults" \
            -- RunConfiguration.TestSessionTimeout=60000
    else
        dotnet test tests/GopherMcp.Tests.csproj \
            -c $BUILD_CONFIG \
            --framework net8.0 \
            --no-build \
            --logger:"console;verbosity=normal" \
            --logger:"trx;LogFileName=TestResults.trx" \
            --logger:"html;LogFileName=TestResults.html" \
            --collect:"XPlat Code Coverage" \
            --results-directory:"TestResults" \
            -- RunConfiguration.TestSessionTimeout=60000
    fi
    
    TEST_RESULT=$?
    
    echo ""
    echo -e "${YELLOW}========================================${NC}"
    echo -e "${YELLOW}Test Results Summary${NC}"
    echo -e "${YELLOW}========================================${NC}"
    
    # Parse test results if available
    if [ -f "TestResults/TestResults.trx" ]; then
        # Try to extract summary from TRX file (simple parsing)
        if command -v grep &> /dev/null && command -v sed &> /dev/null; then
            TOTAL=$(grep -o 'total="[^"]*"' TestResults/TestResults.trx | head -1 | sed 's/total="//' | sed 's/"//')
            PASSED=$(grep -o 'passed="[^"]*"' TestResults/TestResults.trx | head -1 | sed 's/passed="//' | sed 's/"//')
            FAILED=$(grep -o 'failed="[^"]*"' TestResults/TestResults.trx | head -1 | sed 's/failed="//' | sed 's/"//')
            SKIPPED=$(grep -o 'skipped="[^"]*"' TestResults/TestResults.trx | head -1 | sed 's/skipped="//' | sed 's/"//')
            
            if [ ! -z "$TOTAL" ]; then
                echo "Total Tests: $TOTAL"
                [ ! -z "$PASSED" ] && echo -e "${GREEN}Passed: $PASSED${NC}"
                [ ! -z "$FAILED" ] && [ "$FAILED" != "0" ] && echo -e "${RED}Failed: $FAILED${NC}"
                [ ! -z "$SKIPPED" ] && [ "$SKIPPED" != "0" ] && echo -e "${YELLOW}Skipped: $SKIPPED${NC}"
            fi
        fi
        
        echo ""
        echo "Detailed results saved to:"
        echo "  - TestResults/TestResults.trx (XML format)"
        echo "  - TestResults/TestResults.html (HTML report)"
        
        # Check for coverage results
        if ls TestResults/*/coverage.cobertura.xml 1> /dev/null 2>&1; then
            echo "  - TestResults/*/coverage.cobertura.xml (Code coverage)"
        fi
    fi
    
    if [ $TEST_RESULT -eq 0 ]; then
        echo ""
        echo -e "${GREEN}✓ All tests passed successfully!${NC}"
    else
        echo ""
        echo -e "${RED}✗ Some tests failed. Check the detailed output above.${NC}"
        
        # Show failed test names if we can extract them
        if [ -f "TestResults/TestResults.trx" ] && command -v grep &> /dev/null; then
            echo ""
            echo -e "${RED}Failed Tests:${NC}"
            grep -o 'testName="[^"]*".*outcome="Failed"' TestResults/TestResults.trx | \
                sed 's/testName="/  - /' | sed 's/".*$//' || true
        fi
        
        exit $TEST_RESULT
    fi
    echo ""
fi

# Pack NuGet package if requested
if [ "$PACK_NUGET" = true ]; then
    echo -e "${YELLOW}Creating NuGet package...${NC}"
    if [ "$VERBOSE" = true ]; then
        dotnet pack src/GopherMcp.csproj -c $BUILD_CONFIG --no-build --verbosity normal
    else
        dotnet pack src/GopherMcp.csproj -c $BUILD_CONFIG --no-build --nologo
    fi
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}NuGet package created successfully${NC}"
        echo "Package location: src/bin/$BUILD_CONFIG/"
        ls -la src/bin/$BUILD_CONFIG/*.nupkg 2>/dev/null || true
    else
        echo -e "${RED}NuGet package creation failed${NC}"
        exit 1
    fi
    echo ""
fi

# Summary
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Build Summary${NC}"
echo -e "${GREEN}========================================${NC}"
echo "Configuration: $BUILD_CONFIG"
echo "SDK Build: ✓"
if [ "$RUN_TESTS" = true ]; then
    echo "Tests: ✓"
fi
if [ "$PACK_NUGET" = true ]; then
    echo "NuGet Package: ✓"
fi
echo ""

# Output locations
echo -e "${YELLOW}Output Locations:${NC}"
echo "SDK Library: src/bin/$BUILD_CONFIG/"
if [ "$RUN_TESTS" = true ]; then
    echo "Test Results: tests/bin/$BUILD_CONFIG/"
fi
echo "Examples: examples/bin/$BUILD_CONFIG/"

echo ""
echo -e "${GREEN}Build completed successfully!${NC}"