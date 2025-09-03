# C# SDK Design Document

## Overview

The Gopher MCP C# SDK provides a comprehensive .NET binding for the MCP (Model Context Protocol) C++ API. This document outlines the design principles, architecture, and implementation details of the SDK.

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Design Principles](#design-principles)
3. [Project Structure](#project-structure)
4. [Core Components](#core-components)
5. [Interop Layer](#interop-layer)
6. [High-Level Wrapper](#high-level-wrapper)
7. [Memory Management](#memory-management)
8. [Error Handling](#error-handling)
9. [Threading Model](#threading-model)
10. [Testing Strategy](#testing-strategy)
11. [Build Configuration](#build-configuration)
12. [Future Considerations](#future-considerations)

## Architecture Overview

The SDK follows a layered architecture approach:

```
┌─────────────────────────────────────┐
│     Application Layer               │
│     (User Applications)             │
└─────────────────────────────────────┘
                 ↕
┌─────────────────────────────────────┐
│     High-Level Wrapper Layer        │
│     (GopherMcpWrapper)              │
└─────────────────────────────────────┘
                 ↕
┌─────────────────────────────────────┐
│     P/Invoke Bindings Layer         │
│     (McpApi, McpTypes, etc.)        │
└─────────────────────────────────────┘
                 ↕
┌─────────────────────────────────────┐
│     Native MCP C++ Library          │
│     (mcp_c_api)                     │
└─────────────────────────────────────┘
```

## Design Principles

### 1. Type Safety
- Strong typing throughout the SDK
- FFI-safe type definitions matching C API exactly
- Compile-time type checking where possible

### 2. Zero-Copy Operations
- Minimize data copying between managed and unmanaged memory
- Use `ref` structs and pinned memory where appropriate
- Support for zero-copy buffer operations via `McpFilterBufferApi`

### 3. Resource Management
- RAII pattern using `SafeHandle` for native resources
- Automatic cleanup via `IDisposable` pattern
- Deterministic resource disposal

### 4. Cross-Platform Compatibility
- Target multiple .NET versions (6.0, 7.0, 8.0)
- Platform-agnostic P/Invoke declarations
- Support for Windows, Linux, and macOS

### 5. Developer Experience
- Intuitive high-level API
- Comprehensive XML documentation
- Async/await support for non-blocking operations

## Project Structure

```
sdk/csharp/
├── src/                          # Main library source
│   ├── GopherMcp.csproj         # Library project file
│   ├── Interop/                  # P/Invoke bindings
│   │   ├── McpTypes.cs          # Core type definitions
│   │   ├── McpHandles.cs        # SafeHandle implementations
│   │   ├── McpApi.cs            # Main API functions
│   │   ├── McpMemoryApi.cs      # Memory management
│   │   ├── McpJsonApi.cs        # JSON operations
│   │   ├── McpCollectionsApi.cs # Collections support
│   │   ├── McpFilterApi.cs      # Filter architecture
│   │   └── McpFilterBufferApi.cs # Buffer operations
│   └── GopherMcpWrapper.cs      # High-level wrapper
├── tests/                        # Unit tests
│   ├── GopherMcp.Tests.csproj
│   ├── Interop/                  # P/Invoke tests
│   └── GopherMcpWrapperTests.cs # Wrapper tests
├── examples/                     # Example applications
│   ├── GopherMcpExample.csproj
│   ├── Program.cs               # Basic demo
│   └── GopherMcpWrapperExample.cs # Comprehensive examples
└── design.md                    # This file
```

## Core Components

### McpTypes.cs
Defines FFI-safe type mappings between C# and C:

```csharp
// FFI-safe boolean (1 byte)
public struct mcp_bool_t
{
    private byte value;
    // Implicit conversions to/from bool
}

// Result codes enum
public enum mcp_result_t : int
{
    MCP_OK = 0,
    MCP_ERROR_INVALID_ARGUMENT = -1,
    // ...
}

// String reference for zero-copy
public struct mcp_string_ref
{
    public IntPtr data;
    public UIntPtr length;
}
```

### McpHandles.cs
Implements safe handle wrappers for native resources:

```csharp
public class McpDispatcherHandle : SafeHandleZeroOrMinusOneIsInvalid
public class McpClientHandle : SafeHandleZeroOrMinusOneIsInvalid
public class McpServerHandle : SafeHandleZeroOrMinusOneIsInvalid
// ...
```

### McpApi.cs
Main P/Invoke declarations for core MCP functionality:

```csharp
[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
public static extern McpDispatcherHandle mcp_dispatcher_create();

[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
public static extern McpClientHandle mcp_client_create(
    McpDispatcherHandle dispatcher,
    ref mcp_client_config_t config);
```

## Interop Layer

### P/Invoke Strategy
- All P/Invoke methods use `CallingConvention.Cdecl`
- String marshalling via UTF-8 encoding
- Struct layout is sequential with explicit field ordering
- Callbacks use delegate marshalling with proper lifetime management

### Type Marshalling

| C Type | C# Type | Notes |
|--------|---------|-------|
| `bool` | `mcp_bool_t` | 1-byte struct for ABI stability |
| `char*` | `IntPtr` | Manual UTF-8 marshalling |
| `size_t` | `UIntPtr` | Platform-specific size |
| `void*` | `IntPtr` | Opaque pointers |
| Function pointers | Delegates | Marshalled as unmanaged function pointers |

### Callback Management
```csharp
public delegate void mcp_client_connection_handler_t(
    IntPtr client,
    mcp_connection_state_t state,
    IntPtr user_data);

// Callbacks are pinned to prevent GC collection
private readonly List<GCHandle> _pinnedCallbacks = new();
```

## High-Level Wrapper

### GopherMcpWrapper
Provides a user-friendly C# interface:

```csharp
public class GopherMcpWrapper : IDisposable
{
    // Async initialization
    public async Task<bool> InitializeClientAsync(
        string name, string version, McpClientConfig? config = null)
    
    // Event-driven architecture
    public event EventHandler<McpErrorEventArgs>? ErrorOccurred;
    public event EventHandler<ConnectionStateEventArgs>? ConnectionStateChanged;
    
    // Request/Response pattern
    public async Task<McpResponse> SendRequestAsync(
        string method, object? parameters = null, TimeSpan? timeout = null)
}
```

### Key Features
- **Async/Await Support**: All long-running operations are async
- **Event System**: .NET events for callbacks from native code
- **Type Safety**: Strong typing with generics where appropriate
- **Resource Management**: Automatic cleanup with IDisposable

## Memory Management

### Strategies Employed

1. **SafeHandle Usage**
   - Automatic reference counting
   - Critical finalization for resource cleanup
   - Protection against handle recycling attacks

2. **Pinned Memory**
   - Used for callbacks to prevent GC movement
   - Explicit pinning for buffer operations
   - Unpinned after native call completion

3. **String Handling**
   ```csharp
   // Zero-copy string passing
   public static mcp_string_ref FromString(string str)
   {
       var bytes = Encoding.UTF8.GetBytes(str);
       var ptr = Marshal.AllocHGlobal(bytes.Length);
       Marshal.Copy(bytes, 0, ptr, bytes.Length);
       return new mcp_string_ref { data = ptr, length = new UIntPtr((uint)bytes.Length) };
   }
   ```

4. **Buffer Management**
   - Zero-copy buffer operations via `McpFilterBufferApi`
   - Memory pools for efficient allocation
   - Explicit lifetime management

## Error Handling

### Error Propagation
```csharp
// Native errors mapped to exceptions
public class McpException : Exception
{
    public int ErrorCode { get; }
    
    public McpException(string message, int errorCode = -1) 
        : base(message)
    {
        ErrorCode = errorCode;
    }
}
```

### Result Code Handling
```csharp
// Check native result and throw on error
private void CheckResult(mcp_result_t result, string operation)
{
    if (result != mcp_result_t.MCP_OK)
    {
        throw new McpException($"{operation} failed", (int)result);
    }
}
```

## Threading Model

### Thread Safety
- P/Invoke layer is thread-safe (native library responsibility)
- Wrapper maintains thread-safe collections for handlers
- Events raised on ThreadPool threads to avoid blocking

### Synchronization
```csharp
// Thread-safe handler registration
private readonly ConcurrentDictionary<string, Func<McpRequest, Task<McpResponse>>> 
    _requestHandlers = new();

// Lock-free event invocation
public event EventHandler<McpErrorEventArgs>? ErrorOccurred;
```

### Async Operations
- Non-blocking I/O via Task-based async pattern
- Cancellation token support for long-running operations
- ConfigureAwait(false) to avoid capturing synchronization context

## Testing Strategy

### Test Categories

1. **Unit Tests** (456+ tests)
   - Type marshalling verification
   - Handle lifecycle validation
   - API signature testing
   - Helper method testing

2. **Integration Tests**
   - End-to-end client/server scenarios
   - Cross-framework compatibility
   - Memory leak detection

3. **Test Frameworks**
   - xUnit for test execution
   - FluentAssertions for readable assertions
   - Moq for mocking dependencies

### Test Coverage Areas
```
Interop/
├── McpTypesTests.cs         # Type definitions
├── McpHandleTests.cs        # SafeHandle implementations  
├── McpApiTests.cs           # Core API functions
├── McpMemoryApiTests.cs     # Memory management
├── McpJsonApiTests.cs       # JSON operations
├── McpCollectionsApiTests.cs # Collections
├── McpFilterApiTests.cs     # Filter architecture
└── McpFilterBufferApiTests.cs # Buffer operations

GopherMcpWrapperTests.cs     # High-level wrapper
```

## Build Configuration

### Multi-Targeting
```xml
<TargetFrameworks>net8.0;net7.0;net6.0</TargetFrameworks>
```

### Build Properties
- **LangVersion**: latest (C# 12 features)
- **Nullable**: enable (nullable reference types)
- **ImplicitUsings**: enable (global usings)
- **TreatWarningsAsErrors**: true (production builds)

### NuGet Dependencies
- Production: None (zero dependencies)
- Testing: xUnit, FluentAssertions, Moq
- Development: SourceLink for debugging support

### Platform-Specific Compilation
```csharp
#if NET8_0_OR_GREATER
    // .NET 8+ specific features
#elif NET7_0
    // .NET 7 specific features
#elif NET6_0
    // .NET 6 compatibility
#endif
```

## Future Considerations

### Planned Enhancements

1. **Performance Optimizations**
   - Span<T> and Memory<T> for buffer operations
   - Stackalloc for small temporary buffers
   - ValueTask for high-frequency async operations

2. **Extended Platform Support**
   - .NET Standard 2.1 for broader compatibility
   - AOT compilation support for .NET 8+
   - Mobile platform support (iOS, Android)

3. **Advanced Features**
   - Source generators for automatic P/Invoke generation
   - Roslyn analyzers for compile-time validation
   - Performance counters and diagnostics

4. **Tooling Integration**
   - NuGet package publishing
   - Visual Studio extension for MCP development
   - dotnet CLI templates

### API Evolution
- Semantic versioning for API stability
- Obsolete attribute for deprecation
- Extension methods for backward compatibility

### Security Considerations
- Input validation at API boundaries
- Safe string handling to prevent buffer overflows
- Secure defaults for all configuration options
- Regular security audits of P/Invoke declarations

## Conclusion

The Gopher MCP C# SDK provides a robust, type-safe, and performant bridge between .NET applications and the native MCP C++ library. Its layered architecture allows for both low-level control via P/Invoke bindings and high-level convenience through the wrapper API, meeting the needs of various use cases while maintaining excellent performance and resource efficiency.