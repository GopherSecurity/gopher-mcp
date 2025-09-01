# MCP Filter SDK

A TypeScript SDK for the MCP (Model Context Protocol) Filter C API, providing advanced filter infrastructure, buffer management, and filter chain composition capabilities.

## 🎯 **Architecture Overview**

This SDK is designed as a **filter library only** - it provides the infrastructure for creating, managing, and composing filters that can be integrated into custom MCP transport layers. The SDK does **NOT** provide transport layer functionality; instead, it focuses on:

- **Filter Lifecycle Management**: Create, configure, and manage filters
- **Filter Chain Composition**: Build complex processing pipelines
- **Advanced Buffer Operations**: Zero-copy operations and memory management
- **Integration Ready**: Easy to plug into existing MCP implementations

## 🏗️ **Core Components**

### **1. Filter API (`filter-api.ts`)**

Wrapper for `mcp_c_filter_api.h` providing:

- Filter creation and lifecycle management
- Built-in filter types (HTTP, TCP, Security, Observability)
- Filter chain building and management
- Basic buffer operations
- **Uses existing C++ RAII system** - no duplicate resource management

### **2. Filter Chain (`filter-chain.ts`)**

Wrapper for `mcp_c_filter_chain.h` providing:

- Advanced chain composition (sequential, parallel, conditional)
- Dynamic routing and load balancing
- Chain optimization and performance monitoring
- Event-driven chain management

### **3. Filter Buffer (`filter-buffer.ts`)**

Wrapper for `mcp_c_filter_buffer.h` providing:

- Zero-copy buffer operations
- Scatter-gather I/O support
- Advanced memory pooling
- Type-safe integer I/O operations

## 📁 **File Structure**

```
src/
├── filter-api.ts          # Core filter infrastructure
├── filter-chain.ts        # Advanced chain management
├── filter-buffer.ts       # Buffer operations and memory management
├── ffi-bindings.ts        # FFI bindings to C++ shared library
├── types/                 # TypeScript type definitions
└── __tests__/            # Comprehensive test suite
    ├── filter-api.test.ts
    ├── filter-chain.test.ts
    └── filter-buffer.test.ts

examples/
└── basic-usage.ts        # Usage examples and patterns
```

## 🚀 **Quick Start**

### **Installation**

```bash
npm install @mcp/filter-sdk
```

### **Basic Usage**

```typescript
import {
  createBuiltinFilter,
  createSimpleChain,
  BuiltinFilterType,
  createBufferFromString,
} from "@mcp/filter-sdk";

// Create a simple HTTP processing pipeline
const authFilter = createBuiltinFilter(0, BuiltinFilterType.AUTHENTICATION, {});
const rateLimitFilter = createBuiltinFilter(
  0,
  BuiltinFilterType.RATE_LIMIT,
  {}
);
const accessLogFilter = createBuiltinFilter(
  0,
  BuiltinFilterType.ACCESS_LOG,
  {}
);

// Build the filter chain
const chain = createSimpleChain(
  0,
  [authFilter, rateLimitFilter, accessLogFilter],
  "http-pipeline"
);

// Create and manage buffers
const buffer = createBufferFromString("Hello, MCP!", BufferOwnership.SHARED);
```

### **Advanced Chain Composition**

```typescript
import { createParallelChain, ChainExecutionMode } from "@mcp/filter-sdk";

// Create parallel processing pipeline
const filters = [
  createBuiltinFilter(0, BuiltinFilterType.METRICS, {}),
  createBuiltinFilter(0, BuiltinFilterType.TRACING, {}),
  createBuiltinFilter(0, BuiltinFilterType.ACCESS_LOG, {}),
];

const parallelChain = createParallelChain(0, filters, 2, "parallel-pipeline");
```

## 🔧 **Integration with MCP Transport Layers**

### **For MCP Server Developers**

```typescript
// In your MCP server implementation
import { createFilterManager, addFilterToManager } from "@mcp/filter-sdk";

class McpServer {
  private filterManager: number;

  constructor() {
    // Create filter manager for this connection
    this.filterManager = createFilterManager(connection, dispatcher);

    // Add security filters
    const authFilter = createBuiltinFilter(
      dispatcher,
      BuiltinFilterType.AUTHENTICATION,
      {}
    );
    addFilterToManager(this.filterManager, authFilter);

    // Initialize the filter pipeline
    initializeFilterManager(this.filterManager);
  }

  async handleRequest(request: any) {
    // Your request handling logic
    // Filters will automatically process the request
  }
}
```

### **For MCP Client Developers**

```typescript
// In your MCP client implementation
import { createFilterChainBuilder, addFilterToChain } from "@mcp/filter-sdk";

class McpClient {
  private requestFilters: number;

  constructor() {
    // Create request filter chain
    const builder = createFilterChainBuilder(dispatcher);

    // Add request processing filters
    addFilterToChain(
      builder,
      createBuiltinFilter(dispatcher, BuiltinFilterType.RATE_LIMIT, {}),
      FilterPosition.FIRST
    );
    addFilterToChain(
      builder,
      createBuiltinFilter(dispatcher, BuiltinFilterType.METRICS, {}),
      FilterPosition.LAST
    );

    this.requestFilters = buildFilterChain(builder);
  }

  async sendRequest(data: any) {
    // Send through filter chain before transmission
    // Filters will process and potentially modify the request
  }
}
```

## 🧪 **Testing**

The SDK includes comprehensive test coverage:

```bash
# Run all tests
npm test

# Run specific test suites
npm test -- --testPathPattern=filter-api.test.ts
npm test -- --testPathPattern=filter-chain.test.ts
npm test -- --testPathPattern=filter-buffer.test.ts
```

## 📚 **API Reference**

### **Filter Types**

- `BuiltinFilterType.TCP_PROXY` - TCP proxy functionality
- `BuiltinFilterType.HTTP_CODEC` - HTTP encoding/decoding
- `BuiltinFilterType.TLS_TERMINATION` - TLS termination
- `BuiltinFilterType.AUTHENTICATION` - Authentication
- `BuiltinFilterType.RATE_LIMIT` - Rate limiting
- `BuiltinFilterType.ACCESS_LOG` - Access logging
- `BuiltinFilterType.METRICS` - Metrics collection
- `BuiltinFilterType.TRACING` - Distributed tracing

### **Chain Execution Modes**

- `ChainExecutionMode.SEQUENTIAL` - Execute filters in order
- `ChainExecutionMode.PARALLEL` - Execute filters concurrently
- `ChainExecutionMode.CONDITIONAL` - Execute based on conditions
- `ChainExecutionMode.PIPELINE` - Pipeline with buffering

### **Buffer Operations**

- Zero-copy data access
- Scatter-gather I/O
- Memory pooling
- Type-safe integer operations
- String and JSON utilities

## 🔒 **Security Features**

- **Authentication filters** for request validation
- **Authorization filters** for access control
- **Rate limiting** to prevent abuse
- **TLS termination** for secure communication
- **Input validation** and sanitization

## 📊 **Performance Features**

- **Zero-copy operations** for minimal memory overhead
- **Parallel processing** for high-throughput scenarios
- **Memory pooling** for efficient resource management
- **Chain optimization** for optimal filter ordering
- **Load balancing** across filter instances

## 🌟 **Key Benefits**

1. **Protocol Agnostic**: Works with any MCP transport layer
2. **Performance Focused**: Zero-copy operations and efficient memory management
3. **Easy Integration**: Simple API that fits into existing MCP implementations
4. **Production Ready**: Comprehensive error handling and resource management
5. **Extensible**: Easy to add custom filters and chain logic

## 🤝 **Contributing**

This SDK is designed to be a clean, focused filter library. Contributions should:

- Maintain the filter-only scope
- Follow the existing C++ header structure
- Include comprehensive tests
- Document new features clearly

## 📄 **License**

[License information]

## 🔗 **Related Projects**

- **MCP Specification**: [Model Context Protocol](https://modelcontextprotocol.io/)
- **C++ Implementation**: Core filter infrastructure
- **Transport Examples**: Custom MCP transport layer implementations
