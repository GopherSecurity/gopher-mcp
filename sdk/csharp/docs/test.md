# MCP Filter C# SDK - Build and Test Guide

## Prerequisites

### Required Software
- **.NET SDK**: 6.0, 7.0, or 8.0
- **CMake**: 3.15 or higher
- **C++ Compiler**:
  - Windows: Visual Studio 2019+ with C++ workload or MinGW
  - Linux: GCC 9+ or Clang 10+
  - macOS: Xcode 12+ Command Line Tools
- **Platform Dependencies**:
  - Linux: `build-essential`, `libevent-dev`
  - macOS: `libevent` (via Homebrew)
  - Windows: Windows SDK 10.0+

## Quick Start

### 1. Clone Repository
```bash
git clone https://github.com/gopher-mcp/mcp-cpp-sdk.git
cd mcp-cpp-sdk/sdk/csharp
```

### 2. Build Everything (Native + .NET)
```bash
# Linux/macOS
./build.sh

# Windows
.\build.ps1
```

## Build Commands

### Basic Build
```bash
# Linux/macOS
./build.sh                    # Build everything in Release mode
./build.sh --debug           # Build in Debug mode
./build.sh --clean           # Clean build (removes all artifacts first)

# Windows
.\build.ps1                  # Build everything in Release mode
.\build.ps1 -Configuration Debug  # Build in Debug mode
.\build.ps1 -Clean          # Clean build
```

### Build Options
```bash
# Linux/macOS
./build.sh --release         # Release build (default)
./build.sh --debug          # Debug build
./build.sh --no-native      # Skip native library build
./build.sh --no-tests       # Skip test execution
./build.sh --pack           # Create NuGet package
./build.sh --clean          # Clean before building
./build.sh --help           # Show all options

# Windows
.\build.ps1 -Configuration Release  # Release build (default)
.\build.ps1 -Configuration Debug    # Debug build
.\build.ps1 -NoNative              # Skip native library build
.\build.ps1 -NoTests               # Skip test execution
.\build.ps1 -Pack                  # Create NuGet package
.\build.ps1 -Clean                 # Clean before building
.\build.ps1 -Help                  # Show all options
```

### Build Native Library Only
```bash
# From repository root
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_C_API=ON
cmake --build . --parallel

# Copy to SDK runtime directory
cp src/c_api/libgopher_mcp_c.* ../sdk/csharp/src/runtimes/[platform]/native/
```

### Build .NET SDK Only
```bash
cd sdk/csharp

# Restore dependencies
dotnet restore

# Build SDK
dotnet build src/GopherMcp.csproj -c Release

# Build all projects including examples
dotnet build -c Release
```

## Testing

### Run All Tests
```bash
# Using build script
./build.sh                    # Runs tests by default
./build.sh --no-tests        # Skip tests

# Using dotnet directly
dotnet test -c Release
```

### Run Specific Test Categories
```bash
# Run only unit tests
dotnet test --filter Category=Unit

# Run only integration tests
dotnet test --filter Category=Integration

# Run tests for specific component
dotnet test --filter FullyQualifiedName~Filter
dotnet test --filter FullyQualifiedName~Transport
dotnet test --filter FullyQualifiedName~Manager
```

### Test with Coverage
```bash
# Generate code coverage report
dotnet test --collect:"XPlat Code Coverage" --results-directory:TestResults

# With detailed output
dotnet test -c Release \
  --logger:"console;verbosity=detailed" \
  --logger:"trx;LogFileName=TestResults.trx" \
  --logger:"html;LogFileName=TestResults.html" \
  --collect:"XPlat Code Coverage"
```

### Debug Tests
```bash
# Run tests with verbose output
dotnet test --logger:"console;verbosity=detailed"

# Run specific test method
dotnet test --filter DisplayName~MethodName

# Run tests with diagnostic output
dotnet test --diag:TestResults/diag.txt
```

## Platform-Specific Instructions

### Linux
```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get update
sudo apt-get install -y cmake build-essential libevent-dev

# Build and test
./build.sh

# Run examples
dotnet run --project examples/BasicUsage/BasicUsage.csproj
```

### macOS
```bash
# Install dependencies
brew install cmake libevent

# For Apple Silicon (M1/M2)
export ARCH=arm64

# Build and test
./build.sh

# Run with specific runtime
dotnet run --runtime osx-arm64 --project examples/BasicUsage/BasicUsage.csproj
```

### Windows
```powershell
# Install Visual Studio 2019+ with C++ workload
# Install CMake from https://cmake.org/download/

# Open Developer Command Prompt or PowerShell
.\build.ps1

# Run examples
dotnet run --project examples\BasicUsage\BasicUsage.csproj
```

## Docker Build
```bash
# Build Docker image with SDK
docker build -t gopher-mcp-sdk -f Dockerfile .

# Run tests in container
docker run --rm gopher-mcp-sdk test

# Extract built packages
docker run --rm -v $(pwd)/output:/output gopher-mcp-sdk \
  sh -c "cp /packages/*.nupkg /output/"
```

## CI/CD Build

### GitHub Actions
```bash
# Triggered automatically on push/PR
# Manual trigger
gh workflow run build.yml

# View workflow runs
gh run list --workflow=build.yml
```

### Local CI Simulation
```bash
# Simulate CI build locally using act
act -j build

# Run specific job
act -j test
```

## Package Creation

### Create NuGet Package
```bash
# Using build script
./build.sh --pack

# Using dotnet directly
dotnet pack src/GopherMcp.csproj -c Release --output ./nupkg

# Include symbols
dotnet pack -c Release -p:IncludeSymbols=true -p:SymbolPackageFormat=snupkg
```

### Test Package Locally
```bash
# Create local NuGet source
dotnet nuget add source ./nupkg --name local-packages

# Install from local source
dotnet add package GopherMcp --source local-packages --version 1.0.0
```

### Publish Package
```bash
# Publish to NuGet.org
dotnet nuget push ./nupkg/*.nupkg \
  --source https://api.nuget.org/v3/index.json \
  --api-key YOUR_API_KEY

# Publish to GitHub Packages
dotnet nuget push ./nupkg/*.nupkg \
  --source https://nuget.pkg.github.com/OWNER/index.json \
  --api-key YOUR_GITHUB_TOKEN
```

## Development Workflow

### Incremental Development
```bash
# 1. Make changes to code
# 2. Build incrementally (no clean)
dotnet build src/GopherMcp.csproj

# 3. Run affected tests
dotnet test --filter FullyQualifiedName~ChangedComponent

# 4. Full build before commit
./build.sh --clean
```

### Watch Mode Development
```bash
# Auto-rebuild on file changes
dotnet watch build --project src/GopherMcp.csproj

# Auto-run tests on changes
dotnet watch test --project tests/GopherMcp.Tests.csproj
```

### Debugging
```bash
# Build with debug symbols
dotnet build -c Debug

# Run with debugging
dotnet run --configuration Debug --project examples/BasicUsage/BasicUsage.csproj

# Attach debugger (VS Code)
# Use launch.json configuration
```

## Troubleshooting

### Common Issues

1. **Native library not found**
   ```bash
   # Rebuild native library
   ./build.sh --clean --no-tests
   
   # Check library location
   ls -la src/runtimes/*/native/
   ```

2. **Test failures**
   ```bash
   # Run with detailed output
   dotnet test --logger:"console;verbosity=detailed"
   
   # Check for native library issues
   export DYLD_LIBRARY_PATH=./src/runtimes/osx-x64/native:$DYLD_LIBRARY_PATH  # macOS
   export LD_LIBRARY_PATH=./src/runtimes/linux-x64/native:$LD_LIBRARY_PATH    # Linux
   ```

3. **Build errors**
   ```bash
   # Clear all caches
   dotnet nuget locals all --clear
   dotnet clean
   rm -rf bin obj **/bin **/obj
   
   # Rebuild
   ./build.sh --clean
   ```

## Performance Testing

### Benchmark Tests
```bash
# Run benchmarks
dotnet run -c Release --project tests/GopherMcp.Benchmarks.csproj

# With memory diagnostics
dotnet run -c Release --project tests/GopherMcp.Benchmarks.csproj -- --memory

# Export results
dotnet run -c Release --project tests/GopherMcp.Benchmarks.csproj -- --exporters json
```

### Load Testing
```bash
# Run load tests
dotnet test --filter Category=LoadTest -c Release

# With custom parameters
dotnet test --filter Category=LoadTest -c Release \
  -- MSTest.LoadTestDuration=60 MSTest.LoadTestConcurrency=100
```

## Examples

### Run Example Applications
```bash
# Basic usage example
dotnet run --project examples/BasicUsage/BasicUsage.csproj

# MCP Calculator Server
dotnet run --project examples/McpCalculatorServer/McpCalculatorServer.csproj

# MCP Calculator Client
dotnet run --project examples/McpCalculatorClient/McpCalculatorClient.csproj

# Advanced filtering example
dotnet run --project examples/AdvancedFiltering/AdvancedFiltering.csproj
```

### Run with Custom Configuration
```bash
# Set environment variables
export GOPHER_MCP_CONFIG=./config/production.json
export GOPHER_MCP_LOG_LEVEL=Debug

# Run with configuration
dotnet run --project examples/McpCalculatorServer/McpCalculatorServer.csproj \
  --configuration Release \
  -- --port 8080 --host localhost
```

This guide provides all the commands needed to build, test, and deploy the GopherMcp C# SDK across different platforms and scenarios.