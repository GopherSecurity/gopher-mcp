# MCP C++ SDK - Model Context Protocol Implementation for C++

[![C++14](https://img.shields.io/badge/C%2B%2B-14%2F17%2F20-blue.svg)](https://isocpp.org/)
[![MCP](https://img.shields.io/badge/MCP-2025--06--18-green.svg)](https://modelcontextprotocol.io/)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)]()
[![Multi-Language](https://img.shields.io/badge/Multi--Language-Python%20%7C%20Go%20%7C%20Rust%20%7C%20Java%20%7C%20C%23%20and%20more-orange.svg)]()

**The most comprehensive C++ implementation of the Model Context Protocol (MCP)** for building AI-powered applications. Production-ready SDK with enterprise features including multi-transport support, connection pooling, and **multi-language bindings via C API (Python, TypeScript, Go, Rust, Java, C#, Ruby and more)**.

⭐ **Please give a star if you find this useful!**

## Table of Contents

- [Architecture](#architecture)
- [Cross-Language Support](#cross-language-support)
- [What is MCP?](#what-is-mcp)
- [Features](#features)
- [Quick Start](#quick-start)
- [Examples](#examples)
- [Installation](#installation)
- [Documentation](#documentation)
- [FAQ](#faq)
- [Contributing](#contributing)
- [License](#license)

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Application Layer                       │
│         MCP Server / Client / Custom Applications           │
├─────────────────────────────────────────────────────────────┤
│              Cross-Language Binding Layer                   │
│      Python │ TypeScript │ Go │ Rust │ Java │ C# │ Ruby     │
├─────────────────────────────────────────────────────────────┤
│                    C API (FFI Layer)                        │
│       libgopher_mcp_c: Opaque Handles │ Memory Safety       │
│        RAII Guards │ Type Safety │ Error Handling           │
├─────────────────────────────────────────────────────────────┤
│                      Protocol Layer                         │
│           MCP JSON-RPC Protocol Implementation              │
│          Request/Response/Notification Handling             │
├─────────────────────────────────────────────────────────────┤
│                    Filter Chain Layer                       │
│      HTTP Codec │ SSE Codec │ Routing │ Rate Limiting       │
│      Circuit Breaker │ Metrics │ Backpressure │ Auth        │
├─────────────────────────────────────────────────────────────┤
│                    Transport Layer                          │
│      Stdio │ HTTP(s)+SSE │ WebSocket │ TCP │ Redis │ P2P    │
├─────────────────────────────────────────────────────────────┤
│                     Network Layer                           │
│      Connection Management │ Listener │ Socket Interface    │
├─────────────────────────────────────────────────────────────┤
│                  Event Loop & Dispatcher                    │
│      Libevent Integration │ Timer Management │ I/O Events   │
└─────────────────────────────────────────────────────────────┘
```

### Design Principles

1. **Thread-Safe Dispatcher Model** - All I/O in dispatcher threads, no complex synchronization
2. **Filter Chain Architecture** - Modular, composable request processing
3. **Production Patterns** - Connection pooling, circuit breaker, graceful shutdown

## Cross-Language Support

**Use MCP C++ SDK from any programming language** via the stable C API (`libgopher_mcp_c`):

| Language | Binding Type | Features |
|----------|--------------|----------|
| **Python** | ctypes/cffi | Async support, type hints |
| **TypeScript/Node.js** | N-API | High performance, native async |
| **Go** | CGO | Goroutine-safe wrappers |
| **Rust** | FFI | Safe wrappers, ownership guarantees |
| **Java** | JNI | Automatic resource management |
| **C#/.NET** | P/Invoke | Async/await support |
| **Ruby** | Native extension | GC integration |

```bash
# Installation paths
Headers: /usr/local/include/gopher-mcp/mcp/c_api/
Library: /usr/local/lib/libgopher_mcp_c.{so,dylib,dll}
```

## What is MCP?

**Model Context Protocol (MCP)** is an open protocol that enables AI models (like Claude, GPT, etc.) to securely interact with external tools, data sources, and services. MCP provides a standardized way for:

- **AI assistants** to access files, databases, and APIs
- **Developers** to expose tools and resources to AI models
- **Applications** to integrate AI capabilities with enterprise systems

This MCP C++ SDK implements the [official MCP specification](https://modelcontextprotocol.io/) in high-performance C++, suitable for:

- Embedded systems and IoT devices
- High-frequency trading and real-time applications
- Game engines and graphics applications
- Native desktop and mobile applications
- Backend services requiring low latency

## Features

| Category | Features |
|----------|----------|
| **Protocol** | Full MCP 2025-06-18 specification, JSON-RPC 2.0, resources, tools, prompts |
| **Transports** | stdio, HTTP+SSE, HTTPS+SSE, WebSocket, TCP |
| **Performance** | Zero-copy buffers, lock-free operations, connection pooling |
| **Reliability** | Circuit breaker, rate limiting, retry with backoff, graceful shutdown |
| **Security** | TLS/SSL, authentication middleware, request validation |
| **Observability** | Structured logging, metrics, health endpoints |
| **Cross-Language** | C API for Python, Node.js, Go, Rust, Java, C#, Ruby bindings |

## Quick Start

### Build and Install

```bash
# Show all available commands
make help

# Build
make

# Install (auto-prompts for sudo if needed)
make install

# Run tests
make test
```

### Create an MCP Server

```cpp
#include "mcp/server/mcp_server.h"

int main() {
    mcp::server::McpServerConfig config;
    config.server_name = "my-mcp-server";

    auto server = mcp::server::createMcpServer(config);

    // Register a tool
    mcp::Tool calculator;
    calculator.name = "add";
    calculator.description = "Add two numbers";

    server->registerTool(calculator, [](const std::string& name,
                                         const mcp::optional<mcp::Metadata>& arguments,
                                         mcp::server::SessionContext& ctx) {
        mcp::CallToolResult result;
        auto args = arguments.value();
        auto a_it = args.find("a");
        auto b_it = args.find("b");

        double a = mcp::holds_alternative<double>(a_it->second)
                       ? mcp::get<double>(a_it->second) : 0.0;
        double b = mcp::holds_alternative<double>(b_it->second)
                       ? mcp::get<double>(b_it->second) : 0.0;

        result.content.push_back(mcp::TextContent{"Result: " + std::to_string(a + b)});
        return result;
    });

    server->listen("http://0.0.0.0:3000");
    server->run();
}
```

### Create an MCP Client

```cpp
#include "mcp/client/mcp_client.h"

int main() {
    mcp::client::McpClientConfig config;
    config.client_name = "my-mcp-client";

    auto client = mcp::client::createMcpClient(config);
    client->connect("http://localhost:3000");

    // Initialize protocol
    auto init = client->initializeProtocol().get();

    // Call a tool
    auto args = mcp::make<mcp::Metadata>()
                    .add("a", 10.0)
                    .add("b", 5.0)
                    .build();

    auto result = client->callTool("add", mcp::make_optional(args)).get();
}
```

## Examples

See [examples/mcp/README.md](examples/mcp/README.md) for complete working examples:

```bash
# Terminal 1: Start server
./build/examples/mcp/mcp_example_server --verbose

# Terminal 2: Run client with demo
./build/examples/mcp/mcp_example_client --demo --verbose
```

The example server includes:
- Resource registration and subscriptions
- Tool execution (calculator, database query, system info)
- Prompt templates
- Session management
- HTTP/SSE endpoints

## Installation

### Prerequisites

- C++14 compiler (GCC 8+, Clang 10+, MSVC 2019+)
- CMake 3.10+
- libevent 2.1+
- OpenSSL 1.1+ (optional, for TLS)

### Build Options

```bash
# Debug build
make debug

# Release build
make release

# Static libraries only
cmake -B build -DBUILD_SHARED_LIBS=OFF

# Without C API
cmake -B build -DBUILD_C_API=OFF

# Custom install prefix
cmake -B build -DCMAKE_INSTALL_PREFIX=~/.local
```

### Windows (Cygwin + MinGW)

Building on Windows requires Cygwin with MinGW-w64 toolchain:

#### Prerequisites
Install [Cygwin](https://www.cygwin.com/) with these packages:
- `make`, `cmake` - Build tools
- `mingw64-x86_64-gcc-g++` - MinGW C++ compiler
- `mingw64-x86_64-libevent` - Event library
- `mingw64-x86_64-openssl` - SSL/TLS library

#### Build Commands
```bash
# From Cygwin bash shell
./build-mingw.sh           # Release build (default)
./build-mingw.sh debug     # Debug build
./build-mingw.sh release   # Release build
./build-mingw.sh clean     # Clean build directory
```

#### Output
- Build directory: `build-mingw/`
- Executable: `build-mingw/examples/mcp/mcp_example_server.exe`

## Documentation

### MCP C++ Documentation

- [Examples Guide](examples/mcp/README.md) - Working server and client examples

### MCP CPP Core Components

- [MCP Protocol in C++](docs/mcp_protocol.md) - Model Context Protocol implementation details
- [Filter Chain](docs/filter_chain.md) - Processing pipeline architecture
- [Transport Layer](docs/transport_layer.md) - Transport implementations
- [Network Layer](docs/network_layer.md) - Connection management and socket abstraction

### Design Documents

- [Event Loop Design](docs/event_loop_design.md) - Event-driven architecture and dispatcher design
- [Filter Usage Guide](docs/filter_usage_guide.md) - Comprehensive guide for using and creating filters
- [CTAD Alternatives](docs/CTAD_alternatives.md) - Class Template Argument Deduction alternatives for C++14
- [MCP Serialization Coverage](docs/MCP_serialization_coverage.md) - JSON serialization implementation details

## FAQ

### What is the Model Context Protocol?

MCP is an open protocol by Anthropic that standardizes how AI models interact with external tools and data. It defines JSON-RPC methods for resources (data), tools (actions), and prompts (templates).

### Why use C++ for MCP?

C++ provides maximum performance and portability. This SDK is ideal for embedded systems, real-time applications, game engines, and any environment where Python/Node.js overhead is unacceptable.

### Does this work with Claude, GPT, and other AI models?

Yes. MCP is model-agnostic. Any AI system that supports MCP can use servers built with this SDK.

### How do I integrate with my Python/Node.js application?

Use the C API bindings. The SDK provides a stable C interface that can be called from any language with FFI support.

### Is this production-ready?

Yes. The SDK includes connection pooling, circuit breakers, rate limiting, TLS support, and comprehensive error handling suitable for enterprise deployments.

### What transports are supported?

- **stdio** - Standard input/output (CLI tools)
- **HTTP+SSE** - HTTP with Server-Sent Events (web applications)
- **HTTPS+SSE** - Secure HTTP+SSE
- **WebSocket** - Bidirectional real-time
- **TCP** - Raw TCP sockets

### How do I contribute?

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines. Issues and pull requests welcome!

## Keywords & Search Terms

`MCP C++`, `MCP CPP`, `Model Context Protocol C++`, `MCP SDK`, `C++ MCP`, `CPP MCP`, `Model Context Protocol CPP`, `MCP implementation`, `AI model integration C++`, `LLM integration C++`, `MCP server C++`, `MCP client C++`, `Model Context Protocol SDK`, `C++ AI SDK`, `Enterprise MCP`, `Production MCP C++`

## Contributing

Contributions are welcome! Please read our contributing guidelines before submitting pull requests.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.

## Related Projects

- [Model Context Protocol Specification](https://modelcontextprotocol.io/)
- [MCP TypeScript SDK](https://github.com/modelcontextprotocol/typescript-sdk)
- [MCP Python SDK](https://github.com/modelcontextprotocol/python-sdk)
