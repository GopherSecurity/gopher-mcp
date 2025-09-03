# Gopher MCP C# SDK

A comprehensive C# SDK for the Gopher MCP (Model Context Protocol) project, providing P/Invoke bindings and high-level wrappers for the MCP C++ API.

## Features

- **Multi-targeting**: Supports .NET 8.0 (latest LTS), .NET 7.0 (STS), and .NET 6.0 (previous LTS)
- **Type Safety**: Strong typing with comprehensive null safety annotations
- **Cross-Platform**: Works on Windows, Linux, and macOS
- **C# 12 Features**: Modern language features and syntax
- **Comprehensive Testing**: Full unit test coverage with xUnit (474+ tests)
- **P/Invoke Bindings**: Complete FFI-safe interop with MCP C API
- **High-Level Wrapper**: User-friendly `GopherMcpWrapper` for easy integration
- **Clean Architecture**: Well-structured SDK with separation of concerns

## Project Structure

```
sdk/csharp/
├── src/                     # Main library source code
│   ├── GopherMcp.csproj    # Library project file
│   ├── Interop/             # P/Invoke bindings
│   │   ├── McpTypes.cs     # Core MCP types and structures
│   │   ├── McpHandles.cs   # Safe handle wrappers
│   │   ├── McpApi.cs       # Main MCP API bindings
│   │   ├── McpMemoryApi.cs # Memory management API
│   │   ├── McpJsonApi.cs   # JSON handling API
│   │   ├── McpCollectionsApi.cs # Collections API
│   │   ├── McpFilterApi.cs # Filter architecture API
│   │   └── McpFilterBufferApi.cs # Zero-copy buffer API
│   └── GopherMcpWrapper.cs # High-level C# wrapper
├── tests/                   # Unit tests
│   ├── GopherMcp.Tests.csproj
│   ├── Interop/            # P/Invoke binding tests
│   └── GopherMcpWrapperTests.cs # Wrapper tests
├── examples/               # Example applications
│   ├── GopherMcpExample.csproj
│   ├── Program.cs          # Basic demo application
│   └── GopherMcpWrapperExample.cs # Comprehensive wrapper examples
├── GopherMcp.sln          # Solution file
├── Directory.Build.props   # Shared build configuration
├── global.json            # .NET SDK version configuration
├── .editorconfig          # Code style configuration
├── .gitignore             # Git ignore rules
└── README.md              # This file
```

## Prerequisites

- .NET SDK: 8.0, 7.0, or 6.0 (any version will work)
- Visual Studio 2022 (v17.4+ for .NET 7, v17.8+ for .NET 8), VS Code, or JetBrains Rider (optional)
- MCP C++ SDK native library (for runtime execution)

## Building the SDK

### Using .NET CLI

```bash
# Navigate to the C# SDK directory
cd sdk/csharp

# Restore dependencies
dotnet restore

# Build the solution (all frameworks)
dotnet build

# Build for a specific framework
dotnet build --framework net8.0
```

### Using Visual Studio

1. Open `sdk/csharp/GopherMcp.sln` in Visual Studio
2. Build → Build Solution (or press Ctrl+Shift+B)

### Using Build Script

```bash
cd sdk/csharp
./build.sh  # Unix/macOS/Linux
```

## Running Tests

```bash
# Run all tests
dotnet test

# Run tests for a specific framework
dotnet test --framework net8.0

# Run tests with detailed output
dotnet test --logger "console;verbosity=detailed"

# Run specific test categories
dotnet test --filter "FullyQualifiedName~GopherMcpWrapperTests"
```

## Running Examples

```bash
# Navigate to examples directory
cd sdk/csharp/examples

# Run the basic demo
dotnet run

# Run the wrapper example with different modes
dotnet run --project GopherMcpExample.csproj
```

## Usage Examples

### Using the High-Level Wrapper

```csharp
using GopherMcp;
using static GopherMcp.Interop.McpTypes;

// Create wrapper instance
using (var wrapper = new GopherMcpWrapper())
{
    // Initialize as client
    var config = new McpClientConfig
    {
        TransportType = mcp_transport_type_t.MCP_TRANSPORT_STDIO
    };
    
    if (await wrapper.InitializeClientAsync("MyClient", "1.0.0", config))
    {
        // Connect to server
        await wrapper.ConnectAsync("localhost", 8080);
        
        // Send request
        var response = await wrapper.SendRequestAsync("echo", new { message = "Hello" });
        Console.WriteLine($"Response: {response.Result}");
    }
}
```

### Using P/Invoke Bindings Directly

```csharp
using GopherMcp.Interop;
using static GopherMcp.Interop.McpTypes;
using static GopherMcp.Interop.McpApi;

// Create dispatcher
var dispatcher = mcp_dispatcher_create();

// Create client config
var config = new mcp_client_config_t
{
    transport = mcp_transport_type_t.MCP_TRANSPORT_STDIO
};

// Create client
var client = mcp_client_create(dispatcher, ref config);

// Connect
var result = mcp_client_connect(client);
if (result == mcp_result_t.MCP_OK)
{
    Console.WriteLine("Connected successfully!");
}

// Clean up
mcp_client_destroy(client);
mcp_dispatcher_destroy(dispatcher);
```

## API Components

### Core P/Invoke Bindings

- **McpTypes**: Core MCP types, enums, and structures
- **McpHandles**: Safe handle wrappers for native resources
- **McpApi**: Main MCP API functions (client/server operations)
- **McpMemoryApi**: Memory management and allocation
- **McpJsonApi**: JSON parsing and serialization
- **McpCollectionsApi**: Native collection operations
- **McpFilterApi**: Filter-based network processing
- **McpFilterBufferApi**: Zero-copy buffer management

### High-Level Wrapper

The `GopherMcpWrapper` class provides:
- Async/await pattern support
- Event-driven architecture
- Automatic resource management
- Type-safe request/response handling
- Built-in error handling

## Testing

The SDK includes comprehensive test coverage:
- **Unit Tests**: 474+ tests covering all components
- **Integration Tests**: Testing P/Invoke marshalling
- **Wrapper Tests**: High-level API validation

## Performance Considerations

- **Zero-Copy Operations**: Buffer APIs support zero-copy for efficiency
- **Native Interop**: Minimal overhead P/Invoke bindings
- **Async Support**: Non-blocking operations with async/await
- **Resource Management**: Proper disposal patterns with SafeHandles

## Contributing

Contributions are welcome! Please ensure:
1. All tests pass
2. Code follows the established patterns
3. New features include tests
4. Documentation is updated

## License

See the main project LICENSE file for details.

## Support

For issues and questions:
- File an issue on the project GitHub repository
- Check existing documentation and examples
- Review test cases for usage patterns