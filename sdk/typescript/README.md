# MCP Filter SDK

A comprehensive TypeScript SDK for the MCP (Model Context Protocol) Filter C API, providing advanced filter infrastructure, buffer management, filter chain composition, and a complete transport layer implementation.

## 🎯 **Architecture Overview**

This SDK provides both **filter infrastructure** and **transport layer implementation**:

### **Filter Infrastructure:**
- **Filter Lifecycle Management**: Create, configure, and manage filters
- **Filter Chain Composition**: Build complex processing pipelines
- **Advanced Buffer Operations**: Zero-copy operations and memory management
- **FilterManager**: High-level message processing with comprehensive filter support

### **Transport Layer:**
- **GopherTransport**: Complete MCP transport implementation
- **Protocol Support**: TCP, UDP, and stdio protocols
- **Enterprise Features**: Security, observability, traffic management
- **MCP Integration**: Seamless integration with MCP client/server

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

### **4. Filter Manager (`filter-manager.ts`)**

High-level message processing system providing:

- **Comprehensive Filter Support**: All 15 available C++ filter types
- **JSON-RPC Processing**: Complete request/response processing pipeline
- **Configuration Management**: Flexible filter configuration system
- **Error Handling**: Robust error handling with fallback behaviors
- **Resource Management**: Automatic cleanup and memory safety

### **5. Gopher Transport (`mcp-example/src/gopher-transport.ts`)**

Complete MCP transport implementation providing:

- **Protocol Support**: TCP, UDP, and stdio protocols
- **FilterManager Integration**: All messages processed through filter pipeline
- **Session Management**: Unique session IDs and lifecycle management
- **Enterprise Features**: Security, observability, traffic management
- **MCP Compatibility**: Full compatibility with MCP client/server

## 📁 **File Structure**

```
src/
├── filter-api.ts          # Core filter infrastructure
├── filter-chain.ts        # Advanced chain management
├── filter-buffer.ts       # Buffer operations and memory management
├── filter-manager.ts      # High-level message processing
├── ffi-bindings.ts        # FFI bindings to C++ shared library
├── types/                 # TypeScript type definitions
└── __tests__/            # Comprehensive test suite
    ├── filter-api.test.ts
    ├── filter-chain.test.ts
    ├── filter-buffer.test.ts
    └── filter-manager-simple.test.ts

examples/
├── basic-usage.ts        # Basic usage examples
└── filter-manager-demo.ts # FilterManager demonstration

mcp-example/              # Complete MCP integration example
├── src/
│   ├── gopher-transport.ts    # Complete transport implementation
│   ├── filter-types.ts        # Local type definitions
│   ├── mcp-client.ts          # MCP client with GopherTransport
│   └── mcp-server.ts          # MCP server with GopherTransport
└── package.json
```

## 🚀 **Quick Start**

### **Installation**

```bash
npm install @mcp/filter-sdk
```

### **Basic Usage with FilterManager**

```typescript
import { FilterManager, FilterManagerConfig } from "@mcp/filter-sdk";

// Configure comprehensive filter pipeline
const config: FilterManagerConfig = {
  security: {
    authentication: {
      method: "jwt",
      secret: "your-secret-key",
    },
    authorization: {
      enabled: true,
      policy: "allow",
      rules: [{ resource: "*", action: "read" }],
    },
  },
  observability: {
    accessLog: { enabled: true, format: "json" },
    metrics: { enabled: true },
    tracing: { enabled: true, serviceName: "my-service" },
  },
  trafficManagement: {
    rateLimit: { enabled: true, requestsPerMinute: 1000 },
    circuitBreaker: { enabled: true, failureThreshold: 5 },
  },
};

// Create FilterManager
const filterManager = new FilterManager(config);

// Process JSON-RPC messages
const message = {
  jsonrpc: "2.0",
  id: 1,
  method: "tools/list",
  params: {},
};

const processedMessage = await filterManager.process(message);
console.log("Processed message:", processedMessage);
```

### **Complete MCP Integration with GopherTransport**

```typescript
import { GopherTransport, GopherTransportConfig } from "./gopher-transport";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";

// Configure transport with comprehensive filters
const transportConfig: GopherTransportConfig = {
  name: "my-mcp-client",
  protocol: "tcp",
  host: "localhost",
  port: 8080,
  filters: {
    security: {
      authentication: { method: "jwt", secret: "client-secret" },
      authorization: { enabled: true, policy: "allow" },
    },
    observability: {
      accessLog: { enabled: true },
      metrics: { enabled: true },
      tracing: { enabled: true, serviceName: "mcp-client" },
    },
    trafficManagement: {
      rateLimit: { enabled: true, requestsPerMinute: 500 },
      circuitBreaker: { enabled: true, failureThreshold: 3 },
    },
  },
};

// Create and use transport
const transport = new GopherTransport(transportConfig);
await transport.start();

const client = new Client({ name: "my-client", version: "1.0.0" });
await client.connect(transport);

// All messages automatically processed through filter pipeline
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

## 🔧 **Complete MCP Integration**

### **MCP Client with GopherTransport**

```typescript
import { GopherTransport, GopherTransportConfig } from "./gopher-transport";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";

// Client-specific configuration
const clientConfig: GopherTransportConfig = {
  name: "mcp-client-transport",
  protocol: "tcp",
  host: "localhost",
  port: 8080,
  filters: {
    security: {
      authentication: {
        method: "jwt",
        secret: "client-secret-key",
        issuer: "mcp-client",
        audience: "mcp-server",
      },
    },
    observability: {
      accessLog: { enabled: true, format: "json" },
      metrics: { enabled: true, labels: { component: "mcp-client" } },
      tracing: { enabled: true, serviceName: "mcp-client", samplingRate: 0.2 },
    },
    trafficManagement: {
      rateLimit: { enabled: true, requestsPerMinute: 500, burstSize: 25 },
      circuitBreaker: { enabled: true, failureThreshold: 3, timeout: 15000 },
      retry: { enabled: true, maxAttempts: 2, backoffStrategy: "exponential" },
    },
  },
};

const transport = new GopherTransport(clientConfig);
await transport.start();

const client = new Client({ name: "calculator-client", version: "1.0.0" });
await client.connect(transport);

// All messages automatically processed through comprehensive filter pipeline
```

### **MCP Server with GopherTransport**

```typescript
import { GopherTransport, GopherTransportConfig } from "./gopher-transport";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";

// Server-specific configuration
const serverConfig: GopherTransportConfig = {
  name: "mcp-server-transport",
  protocol: "tcp",
  host: "0.0.0.0",
  port: 8080,
  filters: {
    security: {
      authentication: {
        method: "jwt",
        secret: "server-secret-key",
        issuer: "mcp-server",
        audience: "mcp-client",
      },
      authorization: {
        enabled: true,
        policy: "allow",
        rules: [{ resource: "tools/*", action: "call", conditions: { authenticated: true } }],
      },
    },
    observability: {
      accessLog: { enabled: true, format: "json", fields: ["timestamp", "method", "sessionId", "duration"] },
      metrics: { enabled: true, labels: { component: "mcp-server", service: "calculator" } },
      tracing: { enabled: true, serviceName: "mcp-server", samplingRate: 0.5 },
    },
    trafficManagement: {
      rateLimit: { enabled: true, requestsPerMinute: 2000, burstSize: 100 },
      circuitBreaker: { enabled: true, failureThreshold: 10, timeout: 60000 },
      loadBalancer: {
        enabled: true,
        strategy: "round-robin",
        upstreams: [
          { host: "worker-1", port: 8080, weight: 1, healthCheck: true },
          { host: "worker-2", port: 8080, weight: 1, healthCheck: true },
        ],
      },
    },
    http: {
      compression: { enabled: true, algorithms: ["gzip", "deflate"], minSize: 512 },
    },
  },
};

const mcpServer = new McpServer({ name: "calculator-server", version: "1.0.0" });
const transport = new GopherTransport(serverConfig);

await transport.start();
await mcpServer.connect(transport);

// All requests automatically processed through comprehensive filter pipeline
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

### **Available Filter Types (All 15 C++ Filters)**

**Network Filters:**
- `TCP_PROXY` - TCP proxy functionality
- `UDP_PROXY` - UDP proxy functionality

**HTTP Filters:**
- `HTTP_CODEC` - HTTP encoding/decoding
- `HTTP_ROUTER` - HTTP request routing
- `HTTP_COMPRESSION` - HTTP compression (gzip, deflate, brotli)

**Security Filters:**
- `TLS_TERMINATION` - TLS/SSL termination
- `AUTHENTICATION` - Authentication (JWT, API key, OAuth2)
- `AUTHORIZATION` - Authorization and access control

**Observability Filters:**
- `ACCESS_LOG` - Access logging
- `METRICS` - Metrics collection
- `TRACING` - Distributed tracing

**Traffic Management Filters:**
- `RATE_LIMIT` - Rate limiting
- `CIRCUIT_BREAKER` - Circuit breaker pattern
- `RETRY` - Retry logic with backoff
- `LOAD_BALANCER` - Load balancing

**Custom Filters:**
- `CUSTOM` - User-defined filters

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

1. **Complete Solution**: Both filter infrastructure and transport layer implementation
2. **Enterprise Ready**: Production-grade security, observability, and traffic management
3. **Protocol Support**: TCP, UDP, and stdio protocols with easy configuration
4. **Performance Focused**: Zero-copy operations and efficient memory management
5. **Easy Integration**: Drop-in replacement for standard MCP transports
6. **Comprehensive Filtering**: All 15 C++ filter types with flexible configuration
7. **Resource Safe**: Automatic cleanup and memory management
8. **Type Safe**: Full TypeScript support with proper type definitions

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
