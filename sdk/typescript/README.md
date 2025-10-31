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
├── mcp-filter-api.ts          # Core filter infrastructure
├── mcp-filter-chain.ts        # Advanced chain management
├── mcp-filter-buffer.ts       # Buffer operations and memory management
├── mcp-filter-manager.ts      # High-level message processing
├── mcp-ffi-bindings.ts        # FFI bindings to C++ shared library
├── mcp-c-structs.ts           # C struct conversion utilities
├── config-utils.ts            # Configuration loading and validation
├── filter-chain-ffi.ts        # FFI wrapper for filter chains
├── filter-dispatcher.ts       # Filter dispatcher implementation
├── filter-types.ts            # Filter type definitions and enums
├── gopher-filtered-transport.ts # Hybrid SDK transport wrapper
├── message-queue.ts           # Message queue for backpressure handling
├── types.ts                   # TypeScript type definitions
├── index.ts                   # Main entry point
└── __tests__/                 # Comprehensive test suite
    ├── async-filter-chain.test.ts
    ├── filter-ffi.test.ts
    ├── gopher-filtered-transport.test.ts
    ├── mcp-buffer-operations.test.ts
    ├── mcp-capifilter.test.ts
    ├── mcp-end-to-end.test.ts
    ├── mcp-ffi-dispatcher.integration.test.ts
    ├── mcp-ffi-dispatcher.unit.test.ts
    ├── mcp-filter-api.test.ts
    ├── mcp-filter-buffer.test.ts
    ├── mcp-filter-chain.test.ts
    ├── mcp-filter-manager.test.ts
    └── mcp-gopher-transport-integration.test.ts

examples/
├── assembler-example.ts          # Canonical configuration example
├── async-transport-example.ts    # Async transport with filters
├── basic-usage.ts                # Basic usage examples
├── ffi-basic-usage.ts            # FFI bridge usage examples
├── filter-manager-demo.ts        # FilterManager demonstration
├── filtered-transport-example.ts # Filtered transport wrapper example
├── integration-test.ts           # Integration test example
└── configs/                      # Example configuration files

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

### **Basic Usage with Canonical Configuration**

The SDK supports the canonical listener-based configuration format that matches the C++ implementation:

```typescript
import {
  createRealDispatcher,
  createFilterChainFromConfig,
  CanonicalConfig
} from "@mcp/filter-sdk";

// Create dispatcher
const dispatcher = createRealDispatcher();

// Define canonical configuration (matches C++ format)
const config: CanonicalConfig = {
  listeners: [
    {
      name: "mcp_server_listener",
      address: {
        socket_address: {
          address: "127.0.0.1",
          port_value: 9090
        }
      },
      filter_chains: [
        {
          filters: [
            { name: "http.codec", type: "http.codec" },
            { name: "sse.codec", type: "sse.codec" },
            { name: "json_rpc.dispatcher", type: "json_rpc.dispatcher" }
          ]
        }
      ]
    }
  ]
};

// Create filter chain from configuration
const chain = createFilterChainFromConfig(dispatcher, config);
```

### **Loading Configuration from JSON**

```typescript
import { loadConfigFromFile } from "./config-utils";

// Load configuration from JSON file
const config = loadConfigFromFile("./configs/mcp_server_filters.json");

// Create filter chain
const chain = createFilterChainFromConfig(dispatcher, config);
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
      accessLog: {
        enabled: true,
        format: "json",
        fields: ["timestamp", "method", "sessionId", "duration"],
      },
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

## 📋 **Configuration Format**

The SDK uses the canonical listener-based configuration format that matches the C++ implementation:

```json
{
  "listeners": [
    {
      "name": "mcp_server_listener",
      "address": {
        "socket_address": {
          "address": "127.0.0.1",
          "port_value": 9090
        }
      },
      "filter_chains": [
        {
          "filters": [
            { "name": "http.codec", "type": "http.codec" },
            { "name": "sse.codec", "type": "sse.codec" },
            { "name": "json_rpc.dispatcher", "type": "json_rpc.dispatcher" }
          ]
        }
      ]
    }
  ]
}
```

This format provides:
- Clear separation of network configuration (listeners) from processing logic (filter chains)
- Support for multiple listeners on different ports
- Consistent structure across C++ and TypeScript implementations
- Easy JSON-based configuration management

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

## 🌉 **FFI Bridge for Hybrid SDK Integration**

### **FilterChain FFI Class**

The `FilterChain` class provides a Koffi-based FFI bridge to C++ filter implementation for hybrid SDK integration, where the official MCP SDK handles protocol and Gopher-MCP provides enterprise filters.

#### **Quick Start**

```typescript
import { FilterChain } from './filter-chain-ffi';
import { createRealDispatcher } from './mcp-filter-api';

// Create dispatcher
const dispatcher = createRealDispatcher();

// Define filter configuration
const config = {
  listeners: [{
    name: 'filters',
    filter_chains: [{
      filters: [
        { type: 'rate_limiter', name: 'limiter', config: { rps: 100 } },
        { type: 'circuit_breaker', name: 'breaker', config: { threshold: 5 } },
        { type: 'metrics', name: 'metrics' }
      ]
    }]
  }]
};

// Create and use filter chain
const chain = new FilterChain(dispatcher, config);
await chain.initialize();

const result = await chain.processIncoming({ method: 'test' });
console.log('Filter decision:', result.decision); // 0=ALLOW, 1=DENY

await chain.shutdown();
chain.destroy();
```

#### **Features**

- **Async Message Processing**: Non-blocking filtering via `processIncoming/processOutgoing`
- **Dynamic Configuration**: Enable/disable filters at runtime
- **Metrics & Observability**: Real-time stats via `getChainStats()` and `getMetrics()`
- **Memory Safety**: Automatic C++ resource cleanup
- **Thread Safety**: All operations dispatched to C++ event loop

#### **Performance**

- FFI overhead: ~50-100μs per call (Koffi bridge)
- Filter processing: <5ms P99 for typical chains
- Memory per chain: ~10-20MB
- Throughput: >1000 req/s

#### **API Reference**

**Lifecycle:**
```typescript
constructor(dispatcher, config)  // Create chain
await initialize()               // Start processing
await shutdown()                 // Stop gracefully
destroy()                        // Release resources
```

**Message Processing:**
```typescript
await processIncoming(message)   // Filter incoming message
await processOutgoing(message)   // Filter outgoing message
```

**Metrics & Stats:**
```typescript
await getChainStats()            // Get chain statistics
await getMetrics(filterName?)    // Get filter metrics
```

**Dynamic Configuration:**
```typescript
await enableFilter(name)         // Enable a filter
await disableFilter(name)        // Disable a filter
await exportConfig()             // Export current config
```

**Validation:**
```typescript
FilterChain.validateConfig(config)  // Validate before creation
```

#### **Usage with Official MCP SDK**

The FilterChain can be wrapped in a custom transport to use with the official MCP SDK:

```typescript
import { FilterChain } from './filter-chain-ffi';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';

class FilteredTransport {
  constructor(baseTransport, filterChain) {
    this.baseTransport = baseTransport;
    this.filterChain = filterChain;
  }

  async send(message) {
    const result = await this.filterChain.processOutgoing(message);
    if (result.decision === 0) { // ALLOW
      return this.baseTransport.send(result.transformedMessage || message);
    }
    throw new Error(`Message blocked: ${result.reason}`);
  }

  onMessage(handler) {
    this.baseTransport.onMessage(async (message) => {
      const result = await this.filterChain.processIncoming(message);
      if (result.decision === 0) { // ALLOW
        handler(result.transformedMessage || message);
      }
    });
  }
}
```

See `examples/ffi-basic-usage.ts` for complete examples.

## 🔀 **Hybrid SDK + Gopher Filters**

### **GopherFilteredTransport**

The `GopherFilteredTransport` class provides a drop-in wrapper for any MCP SDK transport, adding C++ filter processing while the SDK handles protocol implementation. This enables enterprise-grade filtering (rate limiting, circuit breaker, metrics) without implementing custom transports from scratch.

#### **Architecture**

```
MCP SDK (@modelcontextprotocol/sdk)
    ↓
GopherFilteredTransport (wrapper)
    ↓
FilterChain (FFI bridge via Koffi)
    ↓
C++ Gopher-MCP Filters
```

#### **Quick Start**

```typescript
import { Server } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { GopherFilteredTransport, createRealDispatcher } from "./index";

// Create dispatcher for filter processing
const dispatcher = createRealDispatcher();

// Create base SDK transport
const stdioTransport = new StdioServerTransport();

// Wrap with Gopher filters (no address needed - SDK handles transport!)
const transport = new GopherFilteredTransport(stdioTransport, {
  dispatcherHandle: dispatcher,
  filterConfig: {
    listeners: [{
      name: "hybrid_filters",
      filter_chains: [{
        filters: [
          { type: "rate_limiter", name: "limiter", config: { rps: 100 } },
          { type: "circuit_breaker", name: "breaker", config: { threshold: 5 } },
          { type: "metrics", name: "metrics", config: { export_port: 9090 } }
        ]
      }]
    }]
  },
  debugLogging: false
});

// Use with MCP SDK server
const server = new Server({ name: "my-server", version: "1.0.0" });
await server.connect(transport);
```

#### **Key Features**

- **Transparent Wrapping**: Works with any SDK transport (stdio, HTTP, SSE, WebSocket)
- **Filter Decisions**: Handles ALLOW, DENY, DELAY, QUEUE, TRANSFORM
- **Fail-Open**: On filter errors, allows messages through for safety
- **Dynamic Config**: Enable/disable filters, reload config at runtime
- **Metrics & Observability**: Expose filter statistics and performance data
- **Message Queue**: Automatic backpressure handling with configurable queue size

#### **Filter Decisions**

```typescript
// ALLOW: Pass message through (optionally transformed)
{ decision: FilterDecision.ALLOW, transformedMessage?: string }

// DENY: Block message (throws FilterDeniedError on send)
{ decision: FilterDecision.DENY, reason: "Rate limit exceeded" }

// DELAY: Hold message for specified time then retry
{ decision: FilterDecision.DELAY, delayMs: 1000 }

// QUEUE: Add to queue for later processing (backpressure)
{ decision: FilterDecision.QUEUE }

// TRANSFORM: Pass with modified content
{ decision: FilterDecision.TRANSFORM, transformedMessage: "..." }
```

#### **Configuration Format**

Note: In hybrid SDK mode, the `address` field is **optional** since the SDK transport handles actual network I/O. Filters are purely for message processing:

```json
{
  "listeners": [{
    "name": "hybrid_filters",
    "filter_chains": [{
      "name": "default",
      "filters": [
        {
          "name": "rate_limiter",
          "type": "rate_limiter",
          "config": {
            "requests_per_second": 100,
            "burst_size": 20
          }
        },
        {
          "name": "circuit_breaker",
          "type": "circuit_breaker",
          "config": {
            "failure_threshold": 5,
            "timeout_ms": 60000
          }
        }
      ]
    }]
  }]
}
```

#### **Runtime Management**

```typescript
// Get metrics
const metrics = await transport.getMetrics();
console.log('Total processed:', metrics.chain.requests_total);

// Enable/disable filters
await transport.setFilterEnabled('limiter', false); // Disable rate limiter
await transport.setFilterEnabled('breaker', true);  // Enable circuit breaker

// Export current configuration
const config = await transport.exportFilterConfig();

// Get queue stats (for backpressure monitoring)
const queueStats = transport.getQueueStats();
console.log('Queued messages:', queueStats.size);
```

#### **Error Handling**

```typescript
try {
  await transport.send(message);
} catch (error) {
  if (error instanceof FilterDeniedError) {
    console.log('Message blocked by filter:', error.reason);
    // Handle rate limiting, circuit breaker open, etc.
  } else {
    console.error('Transport error:', error);
  }
}
```

#### **Complete Example with HTTP Transport**

```typescript
import { Server } from "@modelcontextprotocol/sdk/server/mcp.js";
import { SSEServerTransport } from "@modelcontextprotocol/sdk/server/sse.js";
import { GopherFilteredTransport, createRealDispatcher } from "./index";
import express from "express";

const app = express();
const dispatcher = createRealDispatcher();

app.post("/sse", async (req, res) => {
  const sseTransport = new SSEServerTransport("/message", res);

  const transport = new GopherFilteredTransport(sseTransport, {
    dispatcherHandle: dispatcher,
    filterConfig: {
      listeners: [{
        name: "http_filters",
        filter_chains: [{
          filters: [
            { type: "rate_limiter", name: "limiter", config: { rps: 50 } },
            { type: "circuit_breaker", name: "breaker", config: { threshold: 3 } },
            { type: "metrics", name: "metrics" }
          ]
        }]
      }]
    },
    onValidationWarning: (warnings) => console.warn('Filter warnings:', warnings)
  });

  const server = new Server({ name: "http-server", version: "1.0.0" });
  await server.connect(transport);
});

app.listen(3000);
```

#### **Migration from Pure SDK**

**Before (Pure SDK):**
```typescript
const transport = new StdioServerTransport();
await server.connect(transport);
```

**After (With Gopher Filters - One Line Change!):**
```typescript
const transport = new GopherFilteredTransport(
  new StdioServerTransport(),
  { dispatcherHandle: dispatcher, filterConfig: config }
);
await server.connect(transport);
```

#### **Performance Characteristics**

- **FFI Overhead**: ~50-100μs per call (Koffi bridge)
- **Filter Processing**: <5ms P99 for typical chains
- **Total Overhead**: <10% vs pure SDK
- **Memory per Connection**: ~10-20MB
- **Throughput**: >1000 req/s for typical workloads

#### **Comparison: Native vs Hybrid Approach**

| Aspect | Native C++ | Hybrid SDK |
|--------|---------------------|---------------------|
| Protocol | Custom C++ | Official SDK |
| Filters | C++ | C++ (via wrapper) |
| Overhead | ~0.5ms | ~0.66ms |
| SDK Updates | Manual | Automatic |
| Complexity | High | Low |
| Use Case | Full control | Quick adoption |

See `examples/configs/hybrid-wrapper-config.json` for complete configuration examples.

## Integration Status

- ✅ All 100 tests passing
- ✅ CApiFilter integration working
- ✅ Client-server examples functional
- ✅ FFI bridge operational with Koffi
- ✅ Hybrid SDK transport wrapper complete
