# Gopher MCP C# SDK - Hello World Library

A simple C# starter library for the Gopher MCP SDK project. This initial version provides a basic HelloWorld class to demonstrate the SDK structure, build system, and testing framework.

## Features

- **Multi-targeting**: Supports .NET 8.0 (latest LTS), .NET 7.0 (STS), and .NET 6.0 (previous LTS)
- **Type Safety**: Strong typing with comprehensive null safety annotations
- **Cross-Platform**: Works on Windows, Linux, and macOS
- **C# 12 Features**: Modern language features and syntax
- **Comprehensive Testing**: Full unit test coverage with xUnit
- **Source Link**: Full debugging support with source stepping
- **Clean Architecture**: Well-structured starter template for future expansion

## Project Structure

```
sdk/csharp/
├── src/                     # Main library source code
│   ├── GopherMcp.csproj    # Library project file
│   └── HelloWorld.cs        # Simple demonstration class
├── tests/                   # Unit tests
│   ├── GopherMcp.Tests.csproj
│   └── HelloWorldTests.cs   # HelloWorld class tests
├── examples/                # Example applications
│   ├── GopherMcpExample.csproj
│   └── Program.cs           # Example console application
├── GopherMcp.sln           # Solution file
├── Directory.Build.props    # Shared build configuration
├── global.json             # .NET SDK version configuration
├── .editorconfig           # Code style configuration
├── .gitignore              # Git ignore rules
└── README.md               # This file
```

## Prerequisites

- .NET SDK: 8.0, 7.0, or 6.0 (any version will work)
- Visual Studio 2022 (v17.4+ for .NET 7, v17.8+ for .NET 8), VS Code, or JetBrains Rider (optional)

## Building the SDK

### Using .NET CLI

```bash
# Navigate to the C# SDK directory
cd sdk/csharp

# Restore dependencies
dotnet restore

# Build the solution (all target frameworks)
dotnet build

# Build for specific framework
dotnet build -f net8.0  # For .NET 8.0
dotnet build -f net7.0  # For .NET 7.0
dotnet build -f net6.0  # For .NET 6.0

# Run tests (all frameworks)
dotnet test

# Test specific framework
dotnet test -f net7.0

# Build in Release mode
dotnet build -c Release
```

### Using Visual Studio

1. Open `GopherMcp.sln` in Visual Studio
2. Build → Build Solution (Ctrl+Shift+B)
3. Test → Run All Tests (Ctrl+R, A)

## Running the Example

```bash
# Navigate to the examples directory
cd sdk/csharp/examples

# Run the example
dotnet run

# Or run with the solution
cd sdk/csharp
dotnet run --project examples/GopherMcpExample.csproj
```

## Usage

### Basic Hello World

```csharp
using GopherMcp;

// Create a hello world instance
var helloWorld = new HelloWorld();
Console.WriteLine(helloWorld.GetGreeting());

// Get a personalized greeting
var greeting = helloWorld.GetPersonalizedGreeting("Alice");
Console.WriteLine(greeting); // "Hello, World! Nice to meet you, Alice!"
```

### Creating Custom Greetings

```csharp
using GopherMcp;

// Create multiple greeting instances
var morning = new HelloWorld("Good morning!");
var evening = new HelloWorld("Good evening!");
var formal = new HelloWorld("Greetings and salutations!");

// Use them with different names
Console.WriteLine(morning.GetPersonalizedGreeting("Alice"));
// Output: Good morning! Nice to meet you, Alice!

Console.WriteLine(evening.GetPersonalizedGreeting("Bob"));
// Output: Good evening! Nice to meet you, Bob!

Console.WriteLine(formal.GetPersonalizedGreeting("Dr. Smith"));
// Output: Greetings and salutations! Nice to meet you, Dr. Smith!
```

## API Documentation

### HelloWorld Class

Simple demonstration class showing basic SDK structure.

**Constructor**
- `HelloWorld(string greeting = "Hello, World!")` - Creates a new instance with optional custom greeting

**Methods**
- `GetGreeting()` - Returns the greeting message
- `GetPersonalizedGreeting(string name)` - Returns a personalized greeting
- `static GetVersion()` - Returns the SDK version information


## Testing

The SDK includes comprehensive unit tests using xUnit, FluentAssertions, and Moq.

```bash
# Run all tests
dotnet test

# Run with coverage
dotnet test --collect:"XPlat Code Coverage"

# Run specific test
dotnet test --filter "FullyQualifiedName~HelloWorldTests"

# Run tests in verbose mode
dotnet test -v detailed
```

## Building for Distribution

```bash
# Create NuGet package for all target frameworks
dotnet pack -c Release

# Create package for specific framework
dotnet pack -c Release -f net8.0
dotnet pack -c Release -f net7.0
dotnet pack -c Release -f net6.0

# Package will be created in src/bin/Release/
# The package supports net8.0, net7.0, and net6.0 targets
```

## Framework-Specific Features

### .NET 8.0 (Latest LTS)
When targeting .NET 8.0, the SDK takes advantage of:
- **Performance improvements**: ~30% faster JSON serialization
- **Native AOT support**: Experimental support for ahead-of-time compilation
- **Reduced memory usage**: Improved garbage collection
- **Enhanced diagnostics**: Better debugging and profiling tools
- **C# 12 language features**: Primary constructors, collection expressions, etc.

### .NET 7.0 (Standard Term Support)
When targeting .NET 7.0, you get:
- **Performance enhancements**: ~20% performance improvement over .NET 6
- **Improved trimming**: Better support for trimmed deployments
- **RegEx improvements**: Source-generated regex for better performance
- **Observability improvements**: Enhanced distributed tracing
- **C# 11 features**: Generic math, raw string literals, list patterns

### .NET 6.0 (Previous LTS)
When targeting .NET 6.0, you get:
- **Long-term support**: Supported until November 2024
- **Stable platform**: Battle-tested in production
- **Hot reload**: Edit code while debugging
- **Minimal APIs**: Simplified API development
- **C# 10 features**: Global usings, file-scoped namespaces

## Troubleshooting

### Build Issues
If you encounter build errors:
1. Ensure you have at least one supported SDK installed: `dotnet --list-sdks`
2. Check SDK version: `dotnet --version` (should be 6.0.100 or higher)
3. Clear NuGet cache if needed: `dotnet nuget locals all --clear`
4. Restore packages: `dotnet restore`

### Test Failures
If tests are failing:
1. Run tests in verbose mode: `dotnet test -v detailed`
2. Check for any missing dependencies
3. Ensure you're using the correct target framework

## Contributing

Contributions are welcome! Please ensure:
1. All tests pass
2. New features include tests
3. Code follows C# coding conventions
4. XML documentation is provided for public APIs

## License

This SDK is licensed under the Apache License 2.0. See the LICENSE file in the repository root for details.

## Support

For issues and questions:
- GitHub Issues: [Create an issue](https://github.com/yourusername/mcp-cpp-sdk/issues)
- Documentation: See the main SDK documentation