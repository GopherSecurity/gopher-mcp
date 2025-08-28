# MCP Filter SDK

A TypeScript SDK for the Model Context Protocol (MCP) Filter C API, providing high-performance network filtering capabilities with a developer-friendly TypeScript interface.

## Features

- **Real C API Bindings**: Complete bindings to the MCP Filter C API using koffi FFI library
- **Type Safety**: Full TypeScript support with comprehensive type definitions
- **Resource Management**: Automatic cleanup and RAII-style resource management
- **Event-Driven**: Built-in event system for monitoring and debugging
- **High Performance**: Zero-copy buffer operations and efficient memory management
- **Cross-Platform**: Support for Windows, macOS, and Linux
- **Production Ready**: Uses real FFI bindings with intelligent fallback for development

## Installation

```bash
npm install @mcp/filter-sdk
```

The SDK automatically installs the required FFI dependency (`koffi`) for native library bindings.

## Prerequisites

- Node.js 16.0.0 or higher
- The MCP Filter C library must be built and available
- Platform-specific dependencies for FFI operations

### Library Types Supported

The SDK supports both static and dynamic libraries:

- **Dynamic Libraries** (recommended for FFI):

  - macOS: `libgopher-mcp.dylib`
  - Linux: `libgopher-mcp.so`
  - Windows: `gopher-mcp.dll`

- **Static Libraries** (requires wrapper):
  - macOS/Linux: `libgopher-mcp.a`
  - Windows: `gopher-mcp.lib`

**Note**: Static libraries (`.a` files) cannot be directly loaded by FFI libraries. To use a static library, you need to create a dynamic wrapper library that links to it.

## Quick Start

### Basic Usage

```typescript
import {
  McpFilterSdk,
  McpProtocolLayer,
  McpBuiltinFilterType,
} from "@mcp/filter-sdk";

async function main() {
  // Create SDK instance
  const sdk = new McpFilterSdk({
    enableLogging: true,
    logLevel: "info",
  });

  try {
    // Initialize the SDK
    const initResult = await sdk.initialize();
    if (!initResult.success) {
      throw new Error(`Failed to initialize: ${initResult.error}`);
    }

    // Create a custom filter
    const filterResult = await sdk.createFilter({
      name: "my_custom_filter",
      type: McpBuiltinFilterType.CUSTOM,
      layer: McpProtocolLayer.LAYER_7_APPLICATION,
    });

    if (!filterResult.success) {
      throw new Error(`Failed to create filter: ${filterResult.error}`);
    }

    const filter = filterResult.data;
    console.log(`Filter created: ${filter}`);

    // Create a buffer
    const bufferResult = await sdk.createBuffer(
      Buffer.from("Hello, World!"),
      McpBufferFlag.OWNED
    );

    if (!bufferResult.success) {
      throw new Error(`Failed to create buffer: ${bufferResult.error}`);
    }

    const buffer = bufferResult.data;
    console.log(`Buffer created: ${buffer}`);

    // Clean up
    await sdk.destroyBuffer(buffer);
    await sdk.destroyFilter(filter);
  } finally {
    // Shutdown the SDK
    await sdk.shutdown();
  }
}

main().catch(console.error);
```

### Creating Filter Chains

```typescript
import {
  McpFilterSdk,
  McpProtocolLayer,
  McpBuiltinFilterType,
  McpFilterPosition,
} from "@mcp/filter-sdk";

async function createFilterChain() {
  const sdk = new McpFilterSdk();
  await sdk.initialize();

  try {
    // Create filters
    const authFilter = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.AUTHENTICATION
    );
    const rateLimitFilter = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.RATE_LIMIT
    );
    const routerFilter = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.HTTP_ROUTER
    );

    // Build filter chain
    const chainBuilder = sdk.createChainBuilder();

    chainBuilder
      .addFilter(authFilter, McpFilterPosition.FIRST)
      .addFilter(rateLimitFilter, McpFilterPosition.AFTER, authFilter)
      .addFilter(routerFilter, McpFilterPosition.LAST);

    const chain = chainBuilder.build();
    console.log(`Filter chain created: ${chain}`);

    return chain;
  } finally {
    await sdk.shutdown();
  }
}
```

### Working with Buffers

```typescript
import { McpFilterSdk, McpBufferFlag } from "@mcp/filter-sdk";

async function bufferOperations() {
  const sdk = new McpFilterSdk();
  await sdk.initialize();

  try {
    // Create buffer with data
    const data = Buffer.from("Network packet data");
    const buffer = await sdk.createBuffer(data, McpBufferFlag.OWNED);

    // Get buffer slices for zero-copy access
    const slices = await sdk.getBufferSlices(buffer);

    for (const slice of slices) {
      console.log(`Slice: ${slice.length} bytes, flags: ${slice.flags}`);
      // Process slice.data without copying
    }

    // Reserve space for writing
    const reservation = await sdk.reserveBuffer(buffer, 1024);

    // Write data to reserved space
    const writeData = Buffer.from("Additional data");
    writeData.copy(reservation.data);

    // Commit the written data
    await sdk.commitBuffer(buffer, writeData.length);

    await sdk.destroyBuffer(buffer);
  } finally {
    await sdk.shutdown();
  }
}
```

### Event Handling

```typescript
import { McpFilterSdk } from "@mcp/filter-sdk";

async function eventHandling() {
  const sdk = new McpFilterSdk();

  // Set up event listeners
  sdk.on("filter:created", (filter, config) => {
    console.log(`Filter created: ${config.name} (${filter})`);
  });

  sdk.on("filter:error", (filter, error) => {
    console.error(`Filter ${filter} error: ${error}`);
  });

  sdk.on("buffer:created", (buffer, size) => {
    console.log(`Buffer created: ${buffer} (${size} bytes)`);
  });

  sdk.on("memory:allocated", (size) => {
    console.log(`Memory allocated: ${size} bytes`);
  });

  await sdk.initialize();

  // ... use SDK ...

  await sdk.shutdown();
}
```

## API Reference

### Core Classes

#### `McpFilterSdk`

The main SDK class that provides access to all MCP Filter functionality.

**Constructor Options:**

- `autoCleanup`: Automatically clean up resources (default: true)
- `memoryPoolSize`: Default memory pool size in bytes (default: 1MB)
- `enableLogging`: Enable SDK logging (default: true)
- `logLevel`: Log level ('debug' | 'info' | 'warn' | 'error', default: 'info')
- `maxFilters`: Maximum number of filters (default: 1000)
- `maxChains`: Maximum number of chains (default: 100)
- `maxBuffers`: Maximum number of buffers (default: 10000)

**Methods:**

- `initialize()`: Initialize the SDK
- `shutdown()`: Shutdown the SDK
- `createFilter(config)`: Create a new filter
- `createBuiltinFilter(type, settings)`: Create a built-in filter
- `destroyFilter(filter)`: Destroy a filter
- `createBuffer(data, flags)`: Create a buffer
- `destroyBuffer(buffer)`: Destroy a buffer
- `createMemoryPool(size)`: Create a memory pool
- `destroyMemoryPool(pool)`: Destroy a memory pool
- `getStats()`: Get SDK statistics
- `cleanupAll()`: Clean up all resources

### Types

#### Filter Types

- `McpFilter`: Filter handle
- `McpFilterConfig`: Filter configuration
- `McpFilterCallbacks`: Filter callback functions
- `McpFilterStatus`: Filter processing status
- `McpFilterPosition`: Filter position in chain

#### Protocol Types

- `McpProtocolLayer`: OSI protocol layer
- `McpTransportProtocol`: Transport protocol (TCP, UDP, QUIC, SCTP)
- `McpAppProtocol`: Application protocol (HTTP, gRPC, WebSocket, etc.)
- `McpProtocolMetadata`: Protocol-specific metadata

#### Buffer Types

- `McpBufferHandle`: Buffer handle
- `McpBufferSlice`: Buffer slice for zero-copy access
- `McpBufferFlag`: Buffer flags (readonly, owned, external, zero-copy)
- `McpBufferOwnership`: Buffer ownership model

#### Chain Types

- `McpFilterChain`: Filter chain handle
- `McpChainConfig`: Chain configuration
- `McpChainExecutionMode`: Chain execution mode (sequential, parallel, conditional)
- `McpRoutingStrategy`: Chain routing strategy

### Enums

#### Filter Status

- `McpFilterStatus.CONTINUE`: Continue filter chain processing
- `McpFilterStatus.STOP_ITERATION`: Stop filter chain processing

#### Protocol Layers

- `McpProtocolLayer.LAYER_3_NETWORK`: Network layer (IP)
- `McpProtocolLayer.LAYER_4_TRANSPORT`: Transport layer (TCP/UDP)
- `McpProtocolLayer.LAYER_5_SESSION`: Session layer (TLS)
- `McpProtocolLayer.LAYER_6_PRESENTATION`: Presentation layer (encoding)
- `McpProtocolLayer.LAYER_7_APPLICATION`: Application layer (HTTP/gRPC)

#### Built-in Filter Types

- `McpBuiltinFilterType.TCP_PROXY`: TCP proxy filter
- `McpBuiltinFilterType.HTTP_CODEC`: HTTP codec filter
- `McpBuiltinFilterType.AUTHENTICATION`: Authentication filter
- `McpBuiltinFilterType.RATE_LIMIT`: Rate limiting filter
- `McpBuiltinFilterType.LOAD_BALANCER`: Load balancer filter

## Error Handling

The SDK uses a result wrapper pattern for error handling:

```typescript
const result = await sdk.createFilter(config);
if (result.success) {
  const filter = result.data;
  // Use filter
} else {
  console.error(`Error: ${result.error}`);
  // Handle error
}
```

## Resource Management

The SDK automatically manages resources and provides cleanup methods:

```typescript
// Automatic cleanup on shutdown
const sdk = new McpFilterSdk({ autoCleanup: true });
await sdk.initialize();

// Manual cleanup
await sdk.cleanupAll();

// Individual resource cleanup
await sdk.destroyFilter(filter);
await sdk.destroyBuffer(buffer);
```

## Performance Considerations

- **Zero-Copy Operations**: Use buffer slices for zero-copy data access
- **Memory Pools**: Create memory pools for batch operations
- **Resource Reuse**: Reuse filters and chains when possible
- **Event Handling**: Use event-driven patterns for asynchronous operations

## Platform Support

- **Windows**: x64, x86 (IA32)
- **macOS**: x64, ARM64
- **Linux**: x64, ARM64

## Building from Source

```bash
# Clone the repository
git clone https://github.com/modelcontextprotocol/gopher-mcp.git
cd gopher-mcp

# Build the C library
./build.sh

# Install SDK dependencies
cd sdk/typescript
npm install

# Build the SDK
npm run build

# Run tests
npm test
```

### Using Static Libraries

If you have a static library (`.a` file) and want to use it with FFI, you need to create a dynamic wrapper library:

#### Option 1: Create a Dynamic Wrapper (Recommended)

Create a simple C wrapper that links to your static library:

```c
// wrapper.c
#include "mcp_filter_api.h"

// Export the functions you need
__attribute__((visibility("default")))
int mcp_init(void* allocator) {
    return mcp_init_impl(allocator);
}

__attribute__((visibility("default")))
void mcp_shutdown(void) {
    mcp_shutdown_impl();
}

// ... export other functions
```

Build the wrapper:

```bash
# macOS
gcc -shared -fPIC -o libgopher-mcp.dylib wrapper.c -L. -lgopher-mcp

# Linux
gcc -shared -fPIC -o libgopher-mcp.so wrapper.c -L. -lgopher-mcp

# Windows
cl /LD wrapper.c gopher-mcp.lib /Fe:gopher-mcp.dll
```

#### Option 2: Use Mock Implementation

For development and testing, the SDK automatically falls back to a mock implementation when the native library is unavailable.

#### Option 3: Build Dynamic Library Directly

Modify your build system to create dynamic libraries instead of static ones.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests for new functionality
5. Ensure all tests pass
6. Submit a pull request

## License

MIT License - see LICENSE file for details.

## Support

- **Issues**: GitHub Issues
- **Documentation**: This README and inline code documentation
- **Examples**: See the `examples/` directory for more usage examples

## Changelog

### v1.0.0

- Initial release
- Complete C API bindings
- TypeScript type definitions
- Resource management
- Event system
- Buffer operations
- Filter chain support
