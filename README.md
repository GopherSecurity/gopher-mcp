# Gopher MCP

Please ⭐ if you like it.

C++ implementation of the Model Context Protocol (MCP), designed following enterprise-grade design patterns and architectures.

## Architecture Overview

Gopher MCP follows a layered architecture designed for high performance, scalability, and extensibility:

```
┌─────────────────────────────────────────────────────────────┐
│                     Application Layer                       │
│  ┌──────────────────────────────────────────────────────┐   │
│  │     MCP Server / Client / Custom Applications        │   │
│  └──────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│              Cross-Language Binding Layer                   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ Python │ TypeScript │ Go │ Rust │ Java │ C# │ Ruby   │   │
│  └──────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│                    C API (FFI Layer)                        │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ libgopher_mcp_c: Opaque Handles │ Memory Safety      │   │
│  │ RAII Guards │ Type Safety │ Error Handling           │   │
│  └──────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│                      Protocol Layer                         │
│  ┌──────────────────────────────────────────────────────┐   │
│  │        MCP JSON-RPC Protocol Implementation          │   │
│  │       Request/Response/Notification Handling         │   │
│  └──────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│                    Filter Chain Layer                       │
│  ┌──────────────────────────────────────────────────────┐   │
│  │   HTTP Codec │ SSE Codec │ Routing │ Rate Limiting   │   │
│  │   Circuit Breaker │ Metrics │ Backpressure │ Auth    │   │
│  └──────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│                    Transport Layer                          │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ Stdio │ HTTP(s)+SSE │ WebSocket │ TCP │ Redis │ P2P  │   │
│  └──────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│                     Network Layer                           │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ Connection Management │ Listener │ Socket Interface  │   │
│  └──────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│                  Event Loop & Dispatcher                    │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ Libevent Integration │ Timer Management │ I/O Events │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## Cross-Language Support

### Architecture Overview
The SDK is designed with cross-language support as a first-class feature:
- **Application Layer**: MCP server/client implementations using your preferred languages
- **Language Bindings**: Native bindings for each language that wrap the C API
- **C API Layer**: Stable FFI interface (`libgopher_mcp_c`) that bridges C++ and other languages

### C API Features
The C API provides a complete FFI-safe interface:
- **Opaque handles**: Hide C++ implementation details
- **Memory safety**: RAII guards and automatic resource management
- **Type safety**: Strong typing with comprehensive error handling
- **Thread safety**: All operations are thread-safe through the dispatcher model
- **Zero-copy buffers**: Efficient data sharing where possible

### Supported Languages
Native bindings are available for all mainstream programming languages:
- **Python**: ctypes/cffi integration with async support
- **TypeScript/Node.js**: N-API bindings for high performance
- **Go**: CGO integration with goroutine-safe wrappers
- **Rust**: Safe FFI wrappers with ownership guarantees
- **Java**: JNI bindings with automatic resource management
- **C#/.NET**: P/Invoke with async/await support
- **Ruby**: Native extension with GC integration
- **Swift**: Direct C interop for iOS/macOS
- **Kotlin**: JNI or Kotlin/Native for Android

Each binding maintains language-specific idioms while providing full access to MCP functionality.

## Core Design Principles

### 1. Thread-Safe Dispatcher Model
All I/O operations and state transitions occur within dispatcher threads, eliminating the need for complex synchronization:
- Each worker thread has its own dispatcher
- Callbacks are invoked in dispatcher thread context
- Thread-local storage for connection state

### 2. Filter Chain Architecture
Modular processing pipeline for extensible functionality:
- **Stateless filters** for HTTP/2 concurrent stream support
- In-place buffer modification for zero-copy performance
- Dynamic filter composition based on transport and requirements

### 3. Production Patterns
Following enterprise design patterns:
- Connection pooling with O(1) insertion/removal
- Circuit breaker for failure isolation
- Watermark-based flow control
- Graceful shutdown with timeout

## Key Components

### Application Base (`mcp_application_base.h`)
Base framework providing:
- Worker thread management
- Filter chain construction
- Connection pooling
- Metrics and observability
- Graceful lifecycle management

### MCP Server (`server/mcp_server.h`)
Production-ready server with:
- Multi-transport support (stdio, HTTP+SSE, WebSocket)
- Session management with timeout
- Resource subscriptions
- Tool registration and execution
- Prompt management

### MCP Client (`client/mcp_client.h`)
Enterprise client featuring:
- Transport negotiation
- Connection pooling
- Circuit breaker pattern
- Retry with exponential backoff
- Future-based async API

### Network Layer (`network/`)
Core networking infrastructure:
- **Connection**: Manages socket lifecycle and filter chain
- **Listener**: Accepts incoming connections
- **Filter**: Modular processing units
- **Socket Interface**: Platform abstraction

### Filter Chain (`filter/`)
Processing pipeline components:
- **HTTP Codec**: HTTP/1.1 and HTTP/2 protocol handling
- **SSE Codec**: Server-Sent Events processing
- **Routing**: Request routing based on method and path
- **Rate Limiting**: Token bucket rate control
- **Circuit Breaker**: Failure isolation
- **Metrics**: Performance monitoring
- **Backpressure**: Flow control

### Transport Layer (`transport/`)
Multiple transport implementations:
- **Stdio**: Standard I/O pipes
- **TCP**: Raw TCP sockets
- **SSL/TLS**: Secure sockets
- **HTTP+SSE**: HTTP with Server-Sent Events
- **HTTPS+SSE**: Secure HTTP+SSE

## Quick Start

### Prerequisites
- C++14 or later compiler
- CMake 3.10+
- libevent 2.1+
- OpenSSL 1.1+ (for SSL/TLS support)
- nghttp2 (optional, for HTTP/2 support)

### Build & Install
```bash
# Quick build and install (default: /usr/local, auto-prompts for sudo)
make
make install  # Will prompt for password if needed

# User-local installation (no sudo required)
make build CMAKE_INSTALL_PREFIX=~/.local
make install

# Custom installation
cmake -B build -DCMAKE_INSTALL_PREFIX=/opt/gopher-mcp
make -C build
make install  # Will use sudo if needed

# Uninstall (auto-detects if sudo is needed)
make uninstall
```

### Build Options
```bash
# Build only C++ libraries (no C API)
cmake -B build -DBUILD_C_API=OFF

# Build only static libraries
cmake -B build -DBUILD_SHARED_LIBS=OFF

# Build for production
make release
sudo make install
```

### Running Tests
```bash
make test           # Run tests with minimal output
make test-verbose   # Run tests with detailed output
make test-parallel  # Run tests in parallel
```

### For FFI Language Bindings
The C API library (`libgopher_mcp_c`) is built by default and provides a stable ABI for FFI bindings:

```bash
# The C API is included in default install
make
make install  # Auto-prompts for sudo if needed

# Headers: /usr/local/include/gopher-mcp/mcp/c_api/
# Library: /usr/local/lib/libgopher_mcp_c.{so,dylib}
```

## Usage Examples

### Creating an MCP Server
```cpp
#include "mcp/server/mcp_server.h"

int main() {
    mcp::server::McpServerConfig config;
    config.server_name = "my-mcp-server";
    config.worker_threads = 4;
    
    auto server = mcp::server::createMcpServer(config);
    
    // Register a tool
    mcp::Tool tool;
    tool.name = "calculator";
    tool.description = "Performs calculations";
    
    server->registerTool(tool, [](const std::string& name,
                                  const mcp::optional<mcp::Metadata>& args,
                                  mcp::server::SessionContext& session) {
        // Tool implementation
        mcp::CallToolResult result;
        // ... perform calculation ...
        return result;
    });
    
    // Start listening
    server->listen("tcp://0.0.0.0:8080");
    server->run();
    
    return 0;
}
```

### Creating an MCP Client
```cpp
#include "mcp/client/mcp_client.h"

int main() {
    mcp::client::McpClientConfig config;
    config.client_name = "my-mcp-client";
    config.max_retries = 3;
    
    auto client = mcp::client::createMcpClient(config);
    
    // Connect to server
    client->connect("tcp://localhost:8080");
    
    // Initialize protocol
    auto init_future = client->initializeProtocol();
    auto init_result = init_future.get();
    
    // Call a tool
    mcp::Metadata args;
    args["expression"] = "2 + 2";
    
    auto tool_future = client->callTool("calculator", args);
    auto tool_result = tool_future.get();
    
    return 0;
}
```

## Documentation

### Core Components
- [MCP Protocol](docs/mcp_protocol.md) - Protocol implementation details
- [Filter Chain](docs/filter_chain.md) - Processing pipeline architecture
- [Transport Layer](docs/transport_layer.md) - Transport implementations
- [Network Layer](docs/network_layer.md) - Connection management and socket abstraction

### Design Documents
- [Event Loop Design](docs/event_loop_design.md) - Event-driven architecture and dispatcher design
- [Filter Usage Guide](docs/filter_usage_guide.md) - Comprehensive guide for using and creating filters
- [CTAD Alternatives](docs/CTAD_alternatives.md) - Class Template Argument Deduction alternatives for C++14
- [MCP Serialization Coverage](docs/MCP_serialization_coverage.md) - JSON serialization implementation details

## Contributing

Please read our contributing guidelines before submitting pull requests.

## License

This project is licensed under the Apache License - see the LICENSE file for details.
