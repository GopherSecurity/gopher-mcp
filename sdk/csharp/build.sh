#!/bin/bash

# Copyright 2025 Gopher Security, Inc.
# SPDX-License-Identifier: Apache-2.0

# GopherMcp C# SDK Build Script
# Builds the library, tests, and examples

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Build configuration
BUILD_CONFIG=${1:-Release}
VERBOSITY=${2:-normal}

echo -e "${GREEN}=== GopherMcp C# SDK Build ===${NC}"
echo "Configuration: $BUILD_CONFIG"
echo "Verbosity: $VERBOSITY"
echo ""

# Function to print section headers
print_section() {
    echo -e "${YELLOW}→ $1${NC}"
}

# Function to handle errors
handle_error() {
    echo -e "${RED}✗ Build failed: $1${NC}"
    exit 1
}

# Check if dotnet is installed
if ! command -v dotnet &> /dev/null; then
    handle_error "dotnet CLI is not installed. Please install .NET SDK 8.0 or later."
fi

# Print .NET version
print_section "Checking .NET SDK version"
dotnet --version

# Clean previous builds
print_section "Cleaning previous builds"
dotnet clean --configuration $BUILD_CONFIG --verbosity $VERBOSITY 2>/dev/null || true

# Restore packages
print_section "Restoring NuGet packages"
if [ -f "GopherMcp.sln" ]; then
    dotnet restore GopherMcp.sln --verbosity $VERBOSITY || handle_error "Package restore failed"
else
    dotnet restore src/GopherMcp.csproj --verbosity $VERBOSITY || handle_error "Package restore failed"
fi

# Build the main library
print_section "Building GopherMcp library"
dotnet build src/GopherMcp.csproj \
    --configuration $BUILD_CONFIG \
    --verbosity $VERBOSITY \
    --no-restore || handle_error "Library build failed"

# Build examples
print_section "Building examples"

echo "  • Building BasicUsage example..."
dotnet build examples/BasicUsage/BasicUsage.csproj \
    --configuration $BUILD_CONFIG \
    --verbosity $VERBOSITY \
    --no-restore || handle_error "BasicUsage build failed"

echo "  • Building McpCalculatorServer example..."
dotnet build examples/McpCalculatorServer/McpCalculatorServer.csproj \
    --configuration $BUILD_CONFIG \
    --verbosity $VERBOSITY \
    --no-restore || handle_error "McpCalculatorServer build failed"

echo "  • Building McpCalculatorClient example..."
dotnet build examples/McpCalculatorClient/McpCalculatorClient.csproj \
    --configuration $BUILD_CONFIG \
    --verbosity $VERBOSITY \
    --no-restore || handle_error "McpCalculatorClient build failed"

echo "  • Building AdvancedFiltering example..."
dotnet build examples/AdvancedFiltering/AdvancedFiltering.csproj \
    --configuration $BUILD_CONFIG \
    --verbosity $VERBOSITY \
    --no-restore || handle_error "AdvancedFiltering build failed"

# Build tests only if requested
if [ "$3" == "--test" ] || [ "$3" == "-t" ]; then
    print_section "Building tests"

    # Check if test projects exist and build them
    if [ -f "tests/GopherMcp.Tests/GopherMcp.Tests.csproj" ]; then
        echo "  • Building GopherMcp.Tests..."
        dotnet build tests/GopherMcp.Tests/GopherMcp.Tests.csproj \
            --configuration $BUILD_CONFIG \
            --verbosity $VERBOSITY \
            --no-restore || handle_error "Tests build failed"
    else
        echo "  • Creating test project..."
        mkdir -p tests/GopherMcp.Tests
        
        cat > tests/GopherMcp.Tests/GopherMcp.Tests.csproj << 'EOF'
<Project Sdk="Microsoft.NET.Sdk">

  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <IsPackable>false</IsPackable>
    <IsTestProject>true</IsTestProject>
  </PropertyGroup>

  <ItemGroup>
    <PackageReference Include="Microsoft.NET.Test.Sdk" Version="17.9.0" />
    <PackageReference Include="xunit" Version="2.6.6" />
    <PackageReference Include="xunit.runner.visualstudio" Version="2.5.6">
      <IncludeAssets>runtime; build; native; contentfiles; analyzers; buildtransitive</IncludeAssets>
      <PrivateAssets>all</PrivateAssets>
    </PackageReference>
    <PackageReference Include="Moq" Version="4.20.70" />
    <PackageReference Include="FluentAssertions" Version="6.12.0" />
    <PackageReference Include="coverlet.collector" Version="6.0.0">
      <IncludeAssets>runtime; build; native; contentfiles; analyzers; buildtransitive</IncludeAssets>
      <PrivateAssets>all</PrivateAssets>
    </PackageReference>
  </ItemGroup>

  <ItemGroup>
    <ProjectReference Include="..\..\src\GopherMcp.csproj" />
  </ItemGroup>

  <ItemGroup>
    <Compile Include="..\..\tests\Unit\*.cs" />
    <Compile Include="..\..\tests\Integration\*.cs" />
    <Compile Include="..\..\tests\Fixtures\*.cs" />
  </ItemGroup>

</Project>
EOF

    dotnet build tests/GopherMcp.Tests/GopherMcp.Tests.csproj \
        --configuration $BUILD_CONFIG \
        --verbosity $VERBOSITY \
        --no-restore || handle_error "Tests build failed"
    fi

    # Run tests
    print_section "Running tests"
    
    # Run tests with detailed output
    echo ""
    echo -e "${BOLD}${CYAN}╔════════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}${CYAN}║                             TEST EXECUTION                                 ║${NC}"
    echo -e "${BOLD}${CYAN}╚════════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    
    # Create test results directory
    mkdir -p ./test-results
    
    # Start timer
    TEST_START_TIME=$(date +%s)
    
    # Run tests with detailed console output and collect results
    TEST_OUTPUT=$(dotnet test tests/GopherMcp.Tests/GopherMcp.Tests.csproj \
        --configuration $BUILD_CONFIG \
        --no-build \
        --verbosity normal \
        --logger "console;verbosity=detailed" \
        --logger "trx;LogFileName=test_results.trx" \
        --logger "html;LogFileName=test_results.html" \
        --results-directory ./test-results \
        --collect:"XPlat Code Coverage" \
        --blame \
        --diag ./test-results/diagnostics.log 2>&1 | tee /dev/tty)
    
    TEST_EXIT_CODE=${PIPESTATUS[0]}
    
    # End timer
    TEST_END_TIME=$(date +%s)
    TEST_DURATION=$((TEST_END_TIME - TEST_START_TIME))
    
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    # Extract test summary from output
    TOTAL_TESTS=$(echo "$TEST_OUTPUT" | grep -oE "Total tests: [0-9]+" | grep -oE "[0-9]+")
    PASSED_TESTS=$(echo "$TEST_OUTPUT" | grep -oE "Passed: [0-9]+" | grep -oE "[0-9]+")
    FAILED_TESTS=$(echo "$TEST_OUTPUT" | grep -oE "Failed: [0-9]+" | grep -oE "[0-9]+")
    SKIPPED_TESTS=$(echo "$TEST_OUTPUT" | grep -oE "Skipped: [0-9]+" | grep -oE "[0-9]+")
    
    # Default values if not found
    TOTAL_TESTS=${TOTAL_TESTS:-0}
    PASSED_TESTS=${PASSED_TESTS:-0}
    FAILED_TESTS=${FAILED_TESTS:-0}
    SKIPPED_TESTS=${SKIPPED_TESTS:-0}
    
    # Display test summary with colors
    echo ""
    echo -e "${BOLD}${CYAN}╔════════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}${CYAN}║                             TEST SUMMARY                                   ║${NC}"
    echo -e "${BOLD}${CYAN}╚════════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    
    # Create a nice summary table
    echo -e "${BOLD}Test Results:${NC}"
    echo "┌─────────────────────────────────────────────────────────┐"
    printf "│ %-20s │ %-10s │ %-20s │\n" "Metric" "Count" "Status"
    echo "├─────────────────────────────────────────────────────────┤"
    printf "│ %-20s │ ${YELLOW}%-10s${NC} │ %-20s │\n" "Total Tests" "$TOTAL_TESTS" "Executed"
    
    if [ "$PASSED_TESTS" -gt 0 ]; then
        printf "│ %-20s │ ${GREEN}%-10s${NC} │ ${GREEN}%-20s${NC} │\n" "✅ Passed" "$PASSED_TESTS" "SUCCESS"
    fi
    
    if [ "$FAILED_TESTS" -gt 0 ]; then
        printf "│ %-20s │ ${RED}%-10s${NC} │ ${RED}%-20s${NC} │\n" "❌ Failed" "$FAILED_TESTS" "FAILURE"
    fi
    
    if [ "$SKIPPED_TESTS" -gt 0 ]; then
        printf "│ %-20s │ ${YELLOW}%-10s${NC} │ ${YELLOW}%-20s${NC} │\n" "⏭️  Skipped" "$SKIPPED_TESTS" "SKIPPED"
    fi
    
    echo "└─────────────────────────────────────────────────────────┘"
    
    # Display execution time
    echo ""
    echo -e "${BOLD}Execution Time: ${CYAN}${TEST_DURATION} seconds${NC}"
    
    # Calculate and display pass rate with visual bar
    if [ "$TOTAL_TESTS" -gt 0 ]; then
        PASS_RATE=$(echo "scale=1; $PASSED_TESTS * 100 / $TOTAL_TESTS" | bc)
        PASS_RATE_INT=$(echo "$PASS_RATE" | cut -d. -f1)
        
        echo ""
        echo -e "${BOLD}Pass Rate: ${GREEN}$PASS_RATE%${NC}"
        
        # Create visual progress bar
        BAR_LENGTH=50
        FILLED_LENGTH=$(echo "scale=0; $PASS_RATE_INT * $BAR_LENGTH / 100" | bc)
        EMPTY_LENGTH=$((BAR_LENGTH - FILLED_LENGTH))
        
        echo -n "["
        if [ "$FILLED_LENGTH" -gt 0 ]; then
            printf "${GREEN}%0.s█${NC}" $(seq 1 $FILLED_LENGTH)
        fi
        if [ "$EMPTY_LENGTH" -gt 0 ]; then
            printf "%0.s░" $(seq 1 $EMPTY_LENGTH)
        fi
        echo "]"
    fi
    
    echo ""
    
    # Show failed test details if any
    if [ "$FAILED_TESTS" -gt 0 ]; then
        echo -e "${BOLD}${RED}╔════════════════════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${BOLD}${RED}║                           FAILED TEST DETAILS                              ║${NC}"
        echo -e "${BOLD}${RED}╚════════════════════════════════════════════════════════════════════════════╝${NC}"
        echo ""
        
        # Extract and format failed test information
        echo "$TEST_OUTPUT" | grep -E "(Failed|Error|Exception)" | while IFS= read -r line; do
            if echo "$line" | grep -q "Failed"; then
                echo -e "${RED}⚠️  $line${NC}"
            elif echo "$line" | grep -q "Exception"; then
                echo -e "${MAGENTA}   $line${NC}"
            else
                echo "   $line"
            fi
        done | head -30
        
        echo ""
    fi
    
    # Show test artifacts section
    echo -e "${BOLD}${CYAN}╔════════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}${CYAN}║                            TEST ARTIFACTS                                  ║${NC}"
    echo -e "${BOLD}${CYAN}╚════════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    
    # Show test report locations
    if [ -f "./test-results/test_results.trx" ]; then
        echo -e "  📄 ${BOLD}TRX Report:${NC} ./test-results/test_results.trx"
    fi
    
    if [ -f "./test-results/test_results.html" ]; then
        echo -e "  🌐 ${BOLD}HTML Report:${NC} ./test-results/test_results.html"
        echo -e "     ${CYAN}Open in browser: open ./test-results/test_results.html${NC}"
    fi
    
    # Check for code coverage
    COVERAGE_FILE=$(find ./test-results -name "coverage.cobertura.xml" 2>/dev/null | head -1)
    if [ -n "$COVERAGE_FILE" ]; then
        echo -e "  📊 ${BOLD}Code Coverage:${NC} $COVERAGE_FILE"
        
        # Try to extract coverage percentage if possible
        if command -v xmllint &> /dev/null; then
            COVERAGE_PCT=$(xmllint --xpath "string(//coverage/@line-rate)" "$COVERAGE_FILE" 2>/dev/null)
            if [ -n "$COVERAGE_PCT" ]; then
                COVERAGE_PCT=$(echo "scale=1; $COVERAGE_PCT * 100" | bc)
                echo -e "     ${GREEN}Coverage: $COVERAGE_PCT%${NC}"
            fi
        fi
    fi
    
    # Check for diagnostic logs
    if [ -f "./test-results/diagnostics.log" ]; then
        echo -e "  🔍 ${BOLD}Diagnostic Log:${NC} ./test-results/diagnostics.log"
    fi
    
    echo ""
    
    # Exit with error if tests failed
    if [ "$TEST_EXIT_CODE" -ne 0 ]; then
        handle_error "Tests failed"
    fi
fi

# Pack NuGet package if requested
if [ "$3" == "--pack" ] || [ "$3" == "-p" ]; then
    print_section "Creating NuGet package"
    dotnet pack src/GopherMcp.csproj \
        --configuration $BUILD_CONFIG \
        --no-build \
        --verbosity $VERBOSITY \
        --output ./nupkg || handle_error "Package creation failed"
    
    echo -e "${GREEN}✓ Package created in ./nupkg/${NC}"
fi

# Generate documentation if requested
if [ "$3" == "--docs" ] || [ "$3" == "-d" ]; then
    print_section "Generating documentation"
    
    # Check if docfx is installed
    if command -v docfx &> /dev/null; then
        docfx docs/docfx.json --serve=false || handle_error "Documentation generation failed"
        echo -e "${GREEN}✓ Documentation generated in ./docs/_site/${NC}"
    else
        echo -e "${YELLOW}⚠ DocFX not installed. Skipping documentation generation.${NC}"
        echo "  Install with: dotnet tool install -g docfx"
    fi
fi

# Summary
echo ""
echo -e "${GREEN}=== Build Summary ===${NC}"
echo -e "${GREEN}✓${NC} Library built successfully"
echo -e "${GREEN}✓${NC} Examples built successfully"
echo -e "${GREEN}✓${NC} Tests built successfully"

# Show output locations
echo ""
echo -e "${GREEN}Output locations:${NC}"
echo "  • Library: src/bin/$BUILD_CONFIG/"
echo "  • Examples: examples/*/bin/$BUILD_CONFIG/"
echo "  • Tests: tests/GopherMcp.Tests/bin/$BUILD_CONFIG/"

if [ "$3" == "--pack" ] || [ "$3" == "-p" ]; then
    echo "  • NuGet Package: ./nupkg/"
fi

if [ "$3" == "--docs" ] || [ "$3" == "-d" ]; then
    echo "  • Documentation: ./docs/_site/"
fi

echo ""
echo -e "${GREEN}Build completed successfully!${NC}"

# Print usage help if no arguments or help requested
if [ "$1" == "--help" ] || [ "$1" == "-h" ] || [ -z "$1" ]; then
    echo ""
    echo "Usage: ./build.sh [configuration] [verbosity] [options]"
    echo ""
    echo "Configuration:"
    echo "  Debug     Build with debug configuration (default)"
    echo "  Release   Build with release configuration"
    echo ""
    echo "Verbosity:"
    echo "  quiet     Quiet output"
    echo "  minimal   Minimal output"
    echo "  normal    Normal output (default)"
    echo "  detailed  Detailed output"
    echo "  diagnostic Diagnostic output"
    echo ""
    echo "Options:"
    echo "  --test, -t   Run tests after building"
    echo "  --pack, -p   Create NuGet package"
    echo "  --docs, -d   Generate documentation"
    echo "  --help, -h   Show this help message"
    echo ""
    echo "Examples:"
    echo "  ./build.sh                    # Build with Debug configuration"
    echo "  ./build.sh Release            # Build with Release configuration"
    echo "  ./build.sh Release normal -t  # Build and run tests"
    echo "  ./build.sh Release minimal -p # Build and create package"
    echo ""
fi