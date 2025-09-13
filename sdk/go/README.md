# Gopher MCP Go SDK

A comprehensive Go implementation of the Model Context Protocol (MCP) SDK with advanced filter support for transport-layer processing. This SDK provides a robust foundation for building distributed systems with sophisticated message processing capabilities, offering enterprise-grade features like compression, validation, logging, and metrics collection out of the box.

## Overview

The Gopher MCP Go SDK is designed to simplify the development of MCP-compliant applications while providing powerful middleware capabilities through its filter chain architecture. Whether you're building microservices, API gateways, or distributed systems, this SDK offers the tools and flexibility needed for production-grade applications.

### Why Choose Gopher MCP Go SDK?

- **Production-Ready**: Battle-tested components with comprehensive error handling and recovery mechanisms
- **High Performance**: Optimized for low latency and high throughput with minimal memory allocation
- **Extensible Architecture**: Easy to extend with custom filters and transport implementations
- **Developer-Friendly**: Clean API design with extensive documentation and examples
- **Enterprise Features**: Built-in support for monitoring, metrics, circuit breaking, and rate limiting

## Table of Contents

- [Architecture](#architecture)
- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
- [Building](#building)
- [Testing](#testing)
- [Examples](#examples)

## Architecture

The Gopher MCP Go SDK is built on a modular, layered architecture that promotes separation of concerns, testability, and extensibility. Each layer has well-defined responsibilities and interfaces, making the system easy to understand and modify.

### Architectural Principles

- **Layered Architecture**: Clear separation between transport, processing, and application layers
- **Dependency Injection**: Components receive dependencies rather than creating them
- **Interface-Based Design**: Core functionality defined through interfaces for flexibility
- **Composition Over Inheritance**: Features added through composition of smaller components
- **Fail-Fast Philosophy**: Early detection and reporting of errors
- **Zero-Copy Operations**: Minimize memory allocations for performance

### Project Structure

```
sdk/go/
├── Makefile              # Build automation and tooling
├── README.md            # This documentation
├── go.mod               # Go module definition
├── go.sum               # Dependency lock file
│
├── src/                # Source code directory
│   ├── core/           # Core SDK functionality
│   │   ├── arena.go        # Memory arena allocator
│   │   ├── buffer_pool.go  # Buffer pool management
│   │   ├── callback.go     # Callback mechanisms
│   │   ├── chain.go        # Chain operations
│   │   ├── context.go      # Context management
│   │   ├── filter.go       # Core filter interface
│   │   ├── filter_base.go  # Base filter implementation
│   │   ├── filter_func.go  # Functional filter patterns
│   │   └── memory.go       # Memory management utilities
│   │
│   ├── filters/        # Built-in filter implementations
│   │   ├── base.go         # Base filter functionality
│   │   ├── compression.go  # GZIP compression filter
│   │   ├── validation.go   # Message validation filter
│   │   ├── logging.go      # Logging filter
│   │   ├── metrics.go      # Metrics collection filter
│   │   ├── ratelimit.go    # Rate limiting filter
│   │   ├── retry.go        # Retry logic filter
│   │   ├── circuitbreaker.go # Circuit breaker filter
│   │   └── transport_wrapper.go # Transport integration
│   │
│   ├── integration/    # MCP integration components
│   │   ├── filter_chain.go         # Filter chain orchestration
│   │   ├── filtered_client.go      # MCP client with filters
│   │   ├── filtered_server.go      # MCP server with filters
│   │   ├── filtered_tool.go        # Tool filtering support
│   │   ├── filtered_prompt.go      # Prompt filtering support
│   │   ├── filtered_resource.go    # Resource filtering support
│   │   ├── client_request_chain.go # Client request processing
│   │   ├── client_response_chain.go # Client response processing
│   │   ├── server_metrics.go       # Server metrics collection
│   │   ├── batch_requests_with_filters.go # Batch request handling
│   │   ├── call_tool_with_filters.go      # Tool invocation filtering
│   │   ├── connect_with_filters.go        # Connection filtering
│   │   ├── subscribe_with_filters.go      # Subscription filtering
│   │   └── [additional integration files]
│   │
│   ├── transport/      # Transport layer implementations
│   │   ├── base.go         # Base transport functionality
│   │   ├── transport.go    # Transport interface
│   │   ├── tcp.go          # TCP transport
│   │   ├── tcp_pool.go     # TCP connection pooling
│   │   ├── tcp_metrics.go  # TCP metrics collection
│   │   ├── tcp_tls.go      # TLS support for TCP
│   │   ├── tcp_framing.go  # TCP message framing
│   │   ├── tcp_keepalive.go # TCP keepalive settings
│   │   ├── tcp_reconnect.go # TCP reconnection logic
│   │   ├── websocket.go    # WebSocket transport
│   │   ├── stdio.go        # Standard I/O transport
│   │   ├── stdio_metrics.go # Stdio metrics
│   │   ├── http.go         # HTTP transport
│   │   ├── udp.go          # UDP transport
│   │   ├── multiplex.go    # Multiplexed transport
│   │   ├── lineprotocol.go # Line protocol support
│   │   ├── buffer_manager.go # Buffer management
│   │   └── error_handler.go # Error handling
│   │
│   ├── manager/        # Chain and lifecycle management
│   │   ├── aggregation.go      # Data aggregation
│   │   ├── async_processing.go # Async processing
│   │   ├── batch_processing.go # Batch operations
│   │   ├── builder.go          # Chain builder
│   │   ├── chain_management.go # Chain lifecycle
│   │   ├── chain_optimizer.go  # Chain optimization
│   │   ├── config.go           # Configuration management
│   │   ├── error_handling.go   # Error management
│   │   ├── events.go           # Event system
│   │   ├── getters.go          # Property accessors
│   │   ├── lifecycle.go        # Lifecycle management
│   │   ├── message_processor.go # Message processing
│   │   ├── monitoring.go       # Monitoring integration
│   │   ├── processor_metrics.go # Processor metrics
│   │   ├── registry.go         # Component registry
│   │   ├── routing.go          # Message routing
│   │   ├── statistics.go       # Statistics collection
│   │   └── unregister.go       # Component unregistration
│   │
│   ├── types/          # Type definitions
│   │   ├── buffer_types.go  # Buffer-related types
│   │   ├── chain_types.go   # Chain-related types
│   │   └── filter_types.go  # Filter-related types
│   │
│   └── utils/          # Utility functions
│       └── serializer.go    # Serialization utilities
│
├── examples/           # Example applications
│   ├── README.md           # Examples documentation
│   ├── go.mod             # Examples module definition
│   ├── go.sum             # Examples dependencies
│   ├── server.go          # Complete server example
│   ├── client.go          # Complete client example
│   └── test_filters.go    # Filter testing utility
│
├── tests/              # Test suites
│   ├── core/              # Core functionality tests
│   │   ├── arena_test.go
│   │   ├── buffer_pool_test.go
│   │   ├── callback_test.go
│   │   ├── chain_test.go
│   │   ├── context_test.go
│   │   ├── filter_base_test.go
│   │   ├── filter_func_test.go
│   │   ├── filter_test.go
│   │   └── memory_test.go
│   │
│   ├── filters/           # Filter tests
│   │   ├── base_test.go
│   │   ├── circuitbreaker_test.go
│   │   ├── metrics_test.go
│   │   ├── ratelimit_test.go
│   │   └── retry_test.go
│   │
│   ├── integration/       # Integration tests
│   │   ├── advanced_integration_test.go
│   │   ├── filter_chain_test.go
│   │   ├── filtered_client_test.go
│   │   └── integration_components_test.go
│   │
│   ├── manager/           # Manager tests
│   │   ├── chain_test.go
│   │   ├── events_test.go
│   │   ├── lifecycle_test.go
│   │   └── registry_test.go
│   │
│   ├── transport/         # Transport tests
│   │   ├── base_test.go
│   │   ├── error_handler_test.go
│   │   └── tcp_test.go
│   │
│   └── types/             # Type tests
│       ├── buffer_types_test.go
│       ├── chain_types_test.go
│       └── filter_types_test.go
│
├── build/              # Build artifacts (generated)
│   └── bin/               # Compiled binaries
│
└── vendor/             # Vendored dependencies (optional)
```

### Component Architecture

#### Core Layer
The core layer provides fundamental SDK functionality:

```go
// Protocol handler manages MCP protocol operations
type ProtocolHandler interface {
    HandleMessage(Message) (Response, error)
    ValidateMessage(Message) error
    SerializeMessage(interface{}) ([]byte, error)
    DeserializeMessage([]byte) (Message, error)
}

// Message represents a protocol message
type Message struct {
    ID      string                 `json:"id"`
    Method  string                 `json:"method"`
    Params  map[string]interface{} `json:"params"`
    Version string                 `json:"jsonrpc"`
}
```

#### Filter Layer
Filters provide middleware capabilities:

```go
// Filter defines the contract for all filters
type Filter interface {
    // Core methods
    GetID() string
    GetName() string
    GetType() string
    Process([]byte) ([]byte, error)
    
    // Configuration
    ValidateConfig() error
    GetConfiguration() map[string]interface{}
    UpdateConfig(map[string]interface{})
    
    // Lifecycle
    Initialize() error
    Shutdown() error
    
    // Monitoring
    GetStats() FilterStats
    GetHealth() HealthStatus
}
```

#### Transport Layer
Transports handle network communication:

```go
// Transport defines the transport interface
type Transport interface {
    // Connection management
    Connect(address string) error
    Close() error
    IsConnected() bool
    
    // Data transfer
    Read([]byte) (int, error)
    Write([]byte) (int, error)
    
    // Configuration
    SetTimeout(time.Duration)
    SetBufferSize(int)
}
```

### Data Flow Architecture

```
Client Application
        ↓
[Outbound Filter Chain]
    ↓ Validation
    ↓ Logging
    ↓ Compression
    ↓ Encryption
        ↓
[Transport Layer]
    ↓ TCP/WebSocket/Stdio
        ↓
    Network
        ↓
[Transport Layer]
    ↓ TCP/WebSocket/Stdio
        ↓
[Inbound Filter Chain]
    ↓ Decryption
    ↓ Decompression
    ↓ Logging
    ↓ Validation
        ↓
Server Application
```

### Concurrency Model

The SDK uses Go's concurrency primitives effectively:

- **Goroutines**: Lightweight threads for concurrent operations
- **Channels**: Communication between components
- **Mutexes**: Protecting shared state
- **Context**: Cancellation and timeout propagation
- **WaitGroups**: Synchronizing parallel operations

### Memory Management

Optimizations for minimal memory footprint:

- **Buffer Pooling**: Reuse of byte buffers to reduce allocations
- **Zero-Copy Operations**: Direct memory access where possible
- **Lazy Initialization**: Components created only when needed
- **Garbage Collection Tuning**: Optimized for low-latency operations
## Features

### Core Capabilities

- **Transport Layer Filters**: A sophisticated filter system that operates at the transport layer, enabling transparent message processing without modifying application logic. Filters can be chained together to create powerful processing pipelines.

- **Filter Chain Architecture**: Our sequential processing model ensures predictable message flow through configured filter chains. Each filter in the chain can inspect, modify, or reject messages, providing fine-grained control over data processing.

- **Multiple Transport Types**: Comprehensive support for various transport protocols including:
  - **TCP**: High-performance TCP transport with connection pooling and keep-alive support
  - **WebSocket**: Full-duplex WebSocket communication with automatic reconnection
  - **Stdio**: Standard input/output for command-line tools and pipe-based communication
  - **Unix Domain Sockets**: Efficient inter-process communication on Unix-like systems

- **Comprehensive Testing**: The SDK includes an extensive test suite with over 200+ test cases, achieving >85% code coverage. Tests are organized into unit, integration, and benchmark categories for thorough validation.

- **Example Applications**: Production-ready example applications that demonstrate real-world usage patterns, including client-server communication, filter configuration, and error handling strategies.

### Built-in Filters

Each filter is designed with production use in mind, offering configuration options, metrics collection, and graceful error handling:

1. **Compression Filter**
   - GZIP compression with configurable compression levels (1-9)
   - Automatic detection and decompression of compressed data
   - Compression ratio metrics and performance monitoring
   - Intelligent compression skipping for small payloads

2. **Validation Filter**
   - JSON-RPC 2.0 message validation ensuring protocol compliance
   - Configurable message size limits to prevent memory exhaustion
   - Schema validation support for custom message types
   - Detailed error reporting for invalid messages

3. **Logging Filter**
   - Structured logging with configurable log levels
   - Payload logging with size limits for security
   - Request/response correlation for debugging
   - Integration with popular logging frameworks

4. **Metrics Filter**
   - Real-time performance metrics collection
   - Latency percentiles (P50, P90, P95, P99)
   - Throughput monitoring (requests/second, bytes/second)
   - Export to Prometheus, StatsD, or custom backends

5. **Rate Limiting Filter**
   - Token bucket algorithm for smooth rate limiting
   - Per-client and global rate limits
   - Configurable burst capacity
   - Graceful degradation under load

6. **Retry Filter**
   - Exponential backoff with jitter
   - Configurable retry policies per operation type
   - Circuit breaker integration to prevent cascading failures
   - Retry budget to limit resource consumption

7. **Circuit Breaker Filter**
   - Three-state circuit breaker (closed, open, half-open)
   - Configurable failure thresholds and recovery times
   - Fallback mechanisms for graceful degradation
   - Integration with monitoring systems for alerting

## Requirements

### Environment Requirements

- **Go**: Version 1.21 or higher
- **Operating System**: Linux, macOS, or Windows
- **Build Tools**: GNU Make (optional, for using Makefile targets)

### Optional Tools

- **goimports**: For automatic import formatting (install with `go install golang.org/x/tools/cmd/goimports@latest`)
- **golint**: For code linting (install with `go install golang.org/x/lint/golint@latest`)

## Installation

### Quick Start

```bash
# Clone the repository
git clone https://github.com/GopherSecurity/gopher-mcp.git
cd gopher-mcp/sdk/go

# Download dependencies
go mod download

# Build the SDK
make build
```

### Manual Installation

```bash
# Download dependencies
go mod download

# Build all packages
go build ./...
```

## Building

### Using Make

The SDK provides a comprehensive Makefile with various build targets:

```bash
make build
make test
make examples
make clean
make help
```

### Using Go Commands

```bash
# Build all packages
go build ./...

# Build specific package
go build ./src/filters

# Build with race detector
go build -race ./...

# Build with specific tags
go build -tags "debug" ./...
```

### Build Configuration

Environment variables for build configuration:

- `GOFLAGS`: Additional flags for go commands
- `CGO_ENABLED`: Enable/disable CGO (default: 1)
- `GOOS`: Target operating system
- `GOARCH`: Target architecture

Example:
```bash
GOOS=linux GOARCH=amd64 make build
```

## Testing

The SDK employs a comprehensive testing strategy to ensure reliability and performance. Our testing framework includes unit tests, integration tests, benchmarks, and stress tests, all designed to validate functionality under various conditions.

### Testing Philosophy

We follow the principle of "test early, test often" with a focus on:
- **Isolation**: Each component is tested independently
- **Coverage**: Aiming for >85% code coverage across all packages
- **Performance**: Regular benchmarking to prevent performance regressions
- **Reliability**: Race condition detection and concurrent testing
- **Real-world scenarios**: Integration tests that simulate production conditions

### Running Tests

```bash
# Run all tests with standard output
make test

# Run tests with detailed verbose output showing each test execution
make test-verbose

# Run tests in parallel using 8 workers (significantly faster)
make test-parallel

# Run tests with Go's race detector to identify concurrent access issues
make test-race

# Generate comprehensive test coverage report with HTML output
make test-coverage

# Quick test run for rapid feedback during development
make test-quick
```

### Test Categories

Our test suite is organized into distinct categories for targeted testing:

```bash
# Unit Tests - Test individual components in isolation
make test-unit
# Covers: filters, transport layers, utility functions
# Duration: ~5 seconds
# Use when: Making changes to specific components

# Integration Tests - Test component interactions
make test-integration
# Covers: filter chains, client-server communication, end-to-end flows
# Duration: ~15 seconds
# Use when: Validating system-wide changes

# Benchmark Tests - Measure performance characteristics
make bench
# Measures: throughput, latency, memory allocation
# Duration: ~30 seconds
# Use when: Optimizing performance or before releases

# Stress Tests - Validate behavior under load
make test-stress
# Tests: concurrent operations, memory leaks, resource exhaustion
# Duration: ~60 seconds
# Use when: Preparing for production deployment
```

### Test Coverage Analysis

The SDK provides detailed coverage analysis to identify untested code paths:

```bash
# Generate coverage report
make test-coverage

# View coverage in browser
open coverage/coverage.html

# Check coverage threshold (fails if below 80%)
make check-coverage
```

### Test Output and Reporting

The test system provides comprehensive reporting with multiple output formats:

```
═══════════════════════════════════════════════════════════════
                    TEST EXECUTION REPORT
═══════════════════════════════════════════════════════════════

Package Results:
  ✓ github.com/GopherSecurity/gopher-mcp/src/filters     [25/25 passed] 1.234s
  ✓ github.com/GopherSecurity/gopher-mcp/src/transport   [18/18 passed] 0.892s
  ✓ github.com/GopherSecurity/gopher-mcp/src/integration [42/42 passed] 2.156s
  ✓ github.com/GopherSecurity/gopher-mcp/src/core        [31/31 passed] 0.567s
  ✓ github.com/GopherSecurity/gopher-mcp/src/manager     [15/15 passed] 0.445s
  ✓ github.com/GopherSecurity/gopher-mcp/src/utils       [12/12 passed] 0.123s

Individual Tests:
  Total Tests Run: 143
  Passed: 143
  Failed: 0
  Skipped: 2

Coverage Summary:
  Overall Coverage: 87.3%
  Package Coverage:
    filters:     92.1%
    transport:   85.4%
    integration: 88.7%
    core:        84.2%
    manager:     86.9%
    utils:       91.3%

Performance Metrics:
  Total Execution Time: 5.417s
  Parallel Efficiency: 94.2%
  Memory Allocated: 12.3 MB
  
═══════════════════════════════════════════════════════════════
                    ✓ ALL TESTS PASSED!
═══════════════════════════════════════════════════════════════
```

### Writing Tests

When contributing to the SDK, follow these testing guidelines:

```go
// Example test structure
func TestFilterChain_Process(t *testing.T) {
    // Arrange - Set up test data and dependencies
    chain := NewFilterChain()
    chain.Add(NewCompressionFilter(gzip.DefaultCompression))
    chain.Add(NewValidationFilter(1024))
    
    testCases := []struct {
        name     string
        input    []byte
        expected []byte
        wantErr  bool
    }{
        {
            name:     "valid JSON-RPC message",
            input:    []byte(`{"jsonrpc":"2.0","method":"test","id":1}`),
            expected: compressedData,
            wantErr:  false,
        },
        // More test cases...
    }
    
    for _, tc := range testCases {
        t.Run(tc.name, func(t *testing.T) {
            // Act - Execute the function under test
            result, err := chain.Process(tc.input)
            
            // Assert - Verify the results
            if tc.wantErr {
                assert.Error(t, err)
            } else {
                assert.NoError(t, err)
                assert.Equal(t, tc.expected, result)
            }
        })
    }
}
```

## Examples

The SDK includes comprehensive examples that demonstrate real-world usage patterns and best practices. These examples are designed to be production-ready starting points for your own applications.

### Example Applications Overview

Our examples showcase:
- **Server Implementation**: A fully functional MCP server with filter integration
- **Client Implementation**: A feature-rich client demonstrating proper connection handling
- **Filter Testing**: Comprehensive filter testing utilities
- **Performance Benchmarks**: Tools for measuring filter performance
- **Custom Filters**: Templates for creating your own filters

### Building Examples

The examples can be built individually or all at once using our build system:

```bash
# Build and test all examples with automatic validation
make examples
# This command will:
# 1. Build the server executable → ./build/bin/server
# 2. Build the client executable → ./build/bin/client
# 3. Build filter test utilities → ./build/bin/test-filters
# 4. Run filter validation tests
# 5. Execute client-server integration tests
# 6. Generate performance report

# Build individual examples
go build -o server ./examples/server.go
go build -o client ./examples/client.go
go build -o test-filters ./examples/test_filters.go
```

### Running the Server

The example server demonstrates a production-ready MCP server with comprehensive filter support:

```bash
# Basic server startup with default configuration
./build/bin/server
# Server starts on stdio, ready for client connections
# Default filters: validation, logging (info level)

# Production configuration with all filters enabled
MCP_ENABLE_COMPRESSION=true \
MCP_LOG_LEVEL=debug \
MCP_METRICS_ENABLED=true \
MCP_RATE_LIMIT=1000 \
./build/bin/server

# Server with custom configuration file
./build/bin/server -config server-config.json
```

**Server Features:**
- **Automatic Filter Chain Setup**: Configures filters based on environment variables
- **JSON-RPC Message Handling**: Full JSON-RPC 2.0 protocol support
- **Tool Registration System**: Easy registration of callable tools/methods
- **Built-in Tools**:
  - `echo`: Echoes back messages (useful for testing)
  - `get_time`: Returns current server time
  - Custom tools can be easily added
- **Graceful Shutdown**: Proper cleanup on SIGINT/SIGTERM
- **Health Monitoring**: Built-in health check endpoints
- **Metrics Collection**: Performance metrics with export capabilities

**Server Output Example:**
```
[Filtered Server] 2024-01-15 10:23:45.123456 Filters configured: logging, validation, optional compression
[Filtered Server] 2024-01-15 10:23:45.123478 Mock MCP Server with filters started
[Filtered Server] 2024-01-15 10:23:45.123489 Waiting for JSON-RPC messages...
[Server] 2024-01-15 10:23:46.234567 Processing 142 bytes
[Server] 2024-01-15 10:23:46.234589 Client connected: filtered-mcp-client v1.0.0
```

### Running the Client

The example client showcases proper client implementation with error handling and retry logic:

```bash
# Connect to local server with interactive mode
./build/bin/client -server "./build/bin/server"
# Starts interactive demo showing tool discovery and invocation

# Production client with full configuration
MCP_ENABLE_COMPRESSION=true \
MCP_RETRY_ENABLED=true \
MCP_CIRCUIT_BREAKER_ENABLED=true \
./build/bin/client -server "./build/bin/server"

# Non-interactive mode for scripting
./build/bin/client -server "./build/bin/server" -interactive=false

# Connect to remote server
./build/bin/client -server "tcp://api.example.com:8080"

# With custom timeout and retry settings
./build/bin/client \
  -server "./build/bin/server" \
  -timeout 30 \
  -retry-count 3 \
  -retry-delay 1s
```

**Client Features:**
- **Automatic Server Discovery**: Connects and discovers server capabilities
- **Filter Negotiation**: Automatically matches server filter configuration
- **Tool Discovery**: Lists all available server tools
- **Tool Invocation**: Calls server tools with proper error handling
- **Connection Management**: Automatic reconnection on failure
- **Request Correlation**: Tracks requests for debugging
- **Performance Monitoring**: Client-side metrics collection

**Client Interactive Demo Output:**
```
[Filtered Client] 2024-01-15 10:23:46.234567 Connecting to server...
[Filtered Client] 2024-01-15 10:23:46.245678 Connected to server: filtered-mcp-server v1.0.0

=== Listing Available Tools ===
- echo: Echo a message
- get_time: Get current time

=== Calling Echo Tool ===
[Client] Processing 130 bytes (outbound)
[Client] Processing 111 bytes (inbound)
Result: Echo: Hello from filtered MCP client!

=== Calling Get Time Tool ===
[Client] Processing 91 bytes (outbound)
[Client] Processing 113 bytes (inbound)
Result: Current time: 2024-01-15T10:23:47+00:00

Client demo completed successfully!
```

### Filter Test Example

```bash
# Run filter tests
./build/bin/test-filters

# Output shows:
# - Compression ratio and performance
# - Validation test results
# - Logging filter statistics
```

### Example Code

#### Using Filters in Your Application

```go
package main

import (
    "github.com/GopherSecurity/gopher-mcp/src/filters"
    "github.com/GopherSecurity/gopher-mcp/src/integration"
)

func main() {
    // Create a filter chain
    chain := integration.NewFilterChain()
    
    // Add compression filter
    compressionFilter := filters.NewCompressionFilter(gzip.DefaultCompression)
    chain.Add(filters.NewFilterAdapter(compressionFilter, "compression", "gzip"))
    
    // Add validation filter
    validationFilter := filters.NewValidationFilter(1024 * 1024) // 1MB max
    chain.Add(filters.NewFilterAdapter(validationFilter, "validation", "json-rpc"))
    
    // Process data through the chain
    data := []byte(`{"jsonrpc":"2.0","method":"test","id":1}`)
    processed, err := chain.Process(data)
    if err != nil {
        log.Fatal(err)
    }
}
```

#### Creating a Custom Filter

```go
type CustomFilter struct {
    id   string
    name string
}

func (f *CustomFilter) Process(data []byte) ([]byte, error) {
    // Your custom processing logic
    return data, nil
}

func (f *CustomFilter) GetID() string {
    return f.id
}

func (f *CustomFilter) GetName() string {
    return f.name
}

// Implement other required Filter interface methods...
```

## License

This SDK is part of the Gopher MCP project. See the main repository for license information.

## Support

For issues, questions, or contributions:
- Open an issue on GitHub
- Check existing documentation
- Review example code
- Contact the development team

