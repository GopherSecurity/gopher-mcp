#!/bin/bash

# GopherMcp C# SDK Build Script
# Builds the library, tests, and examples

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
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
dotnet restore --verbosity $VERBOSITY || handle_error "Package restore failed"

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

# Build tests
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

# Run tests if requested
if [ "$3" == "--test" ] || [ "$3" == "-t" ]; then
    print_section "Running tests"
    dotnet test tests/GopherMcp.Tests/GopherMcp.Tests.csproj \
        --configuration $BUILD_CONFIG \
        --no-build \
        --verbosity $VERBOSITY \
        --logger "console;verbosity=normal" || handle_error "Tests failed"
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