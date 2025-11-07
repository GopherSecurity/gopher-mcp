# Calculator Server - Hybrid (SDK + Gopher Filters)

## Overview

This is the **Hybrid implementation** - combining:
- **Official MCP SDK** (`@modelcontextprotocol/sdk`) for protocol handling
- **HTTP transport** (`StreamableHTTPServerTransport`, default stateless JSON mode with optional `--stateful` flag) for web-accessible server
- **Gopher-MCP C++ filters** for enterprise features (request logging and extensible filters)

This demonstrates how to add production-grade filtering to existing MCP SDK applications with minimal code changes, now accessible over HTTP.

By default the server runs in **stateless JSON mode** for easy multi-client usage. Pass `--stateful` on the command line to re-enable session-managed SSE streaming when you need the original behavior.

## Architecture

### High-Level Overview

This example demonstrates a **production-ready hybrid architecture** that combines the official MCP SDK with Gopher-MCP enterprise filters. This approach enables you to leverage the SDK's protocol compliance while adding advanced features like request logging, rate limiting, circuit breakers, and metrics collection.

```
┌─────────────────────────────────────────────────────────────────┐
│                    MCP Client (Web/HTTP)                        │
│  • StreamableHTTPClientTransport                                │
│  • Standard MCP SDK (no modifications needed)                   │
└────────────────────────┬────────────────────────────────────────┘
                         │ HTTP
                         │ POST /mcp (JSON-RPC 2.0)
┌────────────────────────▼────────────────────────────────────────┐
│                   Node.js HTTP Server                           │
│  • http.createServer()                                          │
│  • Routes: /mcp (main), /health (monitoring)                    │
│  • Request validation & routing                                 │
└────────────────────────┬────────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────────┐
│              Official MCP SDK - Server                          │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ Server Instance                                          │  │
│  │  • name: "calculator-server-hybrid"                      │  │
│  │  • version: "1.0.0"                                      │  │
│  │  • capabilities: { tools: {} }                           │  │
│  └──────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ Request Handlers                                         │  │
│  │  • ListToolsRequestSchema → returns tool definitions     │  │
│  │  • CallToolRequestSchema → executes tool logic           │  │
│  └──────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ StreamableHTTPServerTransport                            │  │
│  │  • Handles HTTP protocol (JSON responses, no SSE stream) │  │
│  │  • Stateless mode (no shared session ID)                 │  │
│  │  • Bi-directional JSON-RPC 2.0 communication             │  │
│  └──────────────────────────────────────────────────────────┘  │
└────────────────────────┬────────────────────────────────────────┘
                         │ MCP Messages (JSON-RPC)
┌────────────────────────▼────────────────────────────────────────┐
│          GopherFilteredTransport (Interception Layer)           │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ Message Interception Points                              │  │
│  │  • onConnect() → initialize filter chain                 │  │
│  │  • onMessage(msg) → process incoming messages            │  │
│  │  • send(msg) → process outgoing messages                 │  │
│  │  • onClose() → cleanup filter resources                  │  │
│  └──────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ Transport Wrapping                                       │  │
│  │  • Wraps StreamableHTTPServerTransport                   │  │
│  │  • Delegates to SDK transport for protocol handling      │  │
│  │  • Intercepts messages for filter processing            │  │
│  └──────────────────────────────────────────────────────────┘  │
└────────────────────────┬────────────────────────────────────────┘
                         │ FFI Calls (via Koffi)
┌────────────────────────▼────────────────────────────────────────┐
│            Gopher-MCP C++ Filter Chain                          │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ Dispatcher (Event Loop)                                  │  │
│  │  • createHybridDispatcher() → creates libevent loop      │  │
│  │  • Manages filter lifecycle                              │  │
│  │  • Handles async operations                              │  │
│  └──────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ Filter Chain Assembly (from config-hybrid.json)          │  │
│  │  ┌────────────────────────────────────────────────────┐  │  │
│  │  │ 1. Rate Limiter Filter                            │  │  │
│  │  │    • Token bucket strategy with burst support     │  │  │
│  │  │    • Configurable capacity & refill rate          │  │  │
│  │  │    • Per-connection rate limiting                 │  │  │
│  │  │    • HTTP-level interception (blocks before SDK)  │  │  │
│  │  │    • Returns HTTP 429 on rate limit exceeded      │  │  │
│  │  └────────────────────────────────────────────────────┘  │  │
│  │  ┌────────────────────────────────────────────────────┐  │  │
│  │  │ 2. Metrics Filter                                 │  │  │
│  │  │    • Real-time metrics collection & reporting     │  │  │
│  │  │    • Request/response/error tracking              │  │  │
│  │  │    • Latency statistics (min/max/avg)             │  │  │
│  │  │    • Throughput rate calculation                  │  │  │
│  │  │    • Callback-based metrics updates               │  │  │
│  │  └────────────────────────────────────────────────────┘  │  │
│  │  ┌────────────────────────────────────────────────────┐  │  │
│  │  │ 3. Request Logger Filter                          │  │  │
│  │  │    • Logs all JSON-RPC requests/responses         │  │  │
│  │  │    • Configurable log levels & formats            │  │  │
│  │  │    • Payload inspection with size limits          │  │  │
│  │  └────────────────────────────────────────────────────┘  │  │
│  │  ┌────────────────────────────────────────────────────┐  │  │
│  │  │ 4. Circuit Breaker Filter                         │  │  │
│  │  │    • Failure detection & auto-recovery            │  │  │
│  │  │    • Prevents cascading failures                  │  │  │
│  │  │    • Configurable thresholds & timeouts           │  │  │
│  │  │    • Half-open state for gradual recovery         │  │  │
│  │  └────────────────────────────────────────────────────┘  │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### Component Breakdown

#### 1. **Application Layer** (TypeScript)
**Location**: `calculator-server-hybrid.ts`

**Responsibilities**:
- Define MCP tools and their business logic
- Manage application state (CalculatorState class)
- Handle tool invocations (calculate, memory, history)
- Coordinate server lifecycle and graceful shutdown

**Key Classes**:
```typescript
class CalculatorState {
  // Application-specific state management
  - memory: number
  - history: Array<{id, operation, result, timestamp}>
  - storeMemory(), recallMemory(), clearMemory()
  - addToHistory(), getHistory(), getStatistics()
}
```

**Tool Registration Pattern**:
```typescript
server.setRequestHandler(ListToolsRequestSchema, async () => ({
  tools: [
    { name: 'calculate', description: '...', inputSchema: {...} },
    { name: 'memory', description: '...', inputSchema: {...} },
    { name: 'history', description: '...', inputSchema: {...} }
  ]
}));

server.setRequestHandler(CallToolRequestSchema, async (request) => {
  // Execute tool logic based on request.params.name
  // Return { content: [...], isError?: boolean }
});
```

#### 2. **MCP SDK Layer** (Official SDK)
**Package**: `@modelcontextprotocol/sdk`

**Components**:
- **Server**: MCP server instance with capabilities
- **StreamableHTTPServerTransport**: HTTP protocol implementation (stateless JSON responses)
- **Request Handlers**: Process MCP protocol messages

**Integration Points**:
```typescript
const server = new Server({ name, version }, { capabilities });
const sdkTransport = new StreamableHTTPServerTransport({
  sessionIdGenerator: undefined,
  enableJsonResponse: true,
});
await server.connect(filteredTransport); // Connect via wrapped transport
```

#### 3. **Filter Integration Layer** (TypeScript Wrapper)
**Location**: `sdk/typescript/src/gopher-filtered-transport.ts`

**Responsibilities**:
- Wrap SDK transport to intercept messages
- Interface with C++ filters via FFI (Koffi)
- Convert between TypeScript and C representations
- Manage filter lifecycle

**Key Methods**:
```typescript
class GopherFilteredTransport {
  constructor(baseTransport, options: {
    dispatcherHandle: number,
    filterConfig: CanonicalConfig,
    debugLogging?: boolean
  })

  // Transport interface (delegates to SDK)
  async start(): Promise<void>
  async send(message): Promise<void>
  async close(): Promise<void>

  // Filter-specific extensions
  setFilterEnabled(name: string, enabled: boolean): void
  getMetrics(): Promise<Metrics>
}
```

#### 4. **C++ Filter Chain Layer**
**Location**: `src/c_api/`, `src/filter/`

**Components**:
- **Dispatcher**: libevent-based event loop for async operations
- **Filter Registry**: Available filter implementations
- **Filter Chain Assembler**: Builds chain from configuration
- **Individual Filters**: Request logger, rate limiter, circuit breaker, metrics

**C API Functions**:
```c
// Dispatcher management
gopher_dispatcher_handle_t* createHybridDispatcher();
void destroyHybridDispatcher(gopher_dispatcher_handle_t*);

// Filter chain management
gopher_filter_chain_t* createFilterChain(config);
void processMessage(chain, message);
void destroyFilterChain(chain);
```

### Data Flow

#### Incoming Request Flow
```
1. HTTP Client → POST /mcp
   ↓
2. Node HTTP Server → handleRequest()
   ↓
3. GopherFilteredTransport.handleRequest()
   ↓ Reads & parses HTTP body
   ↓ Pre-filters through C++ filter chain (FFI call)
4. C++ Rate Limiter → Checks token bucket
   ↓ (If DENIED: HTTP 429 sent immediately, flow stops)
   ↓ (If ALLOWED: continue to step 5)
5. C++ Metrics Filter → Tracks request
   ↓
6. C++ Request Logger Filter → Logs request
   ↓
7. C++ Circuit Breaker → Checks circuit state
   ↓ Returns to TypeScript
8. GopherFilteredTransport → Recreates request stream
   ↓ Passes to SDK transport
9. StreamableHTTPServerTransport (SDK)
   ↓ Handles HTTP protocol
10. SDK Server → Request Handler
    ↓
11. CalculatorState → Execute tool logic
    ↓ Return result
12. SDK → Prepare response
    ↓
13. StreamableHTTPServerTransport → HTTP response
    ↓
14. HTTP Client ← JSON-RPC result
```

**Note**: For HTTP POST requests, filtering happens in `handleRequest()` BEFORE the SDK processes the message. This prevents rate-limited requests from consuming server resources. The `onmessage` callback is bypassed for HTTP to avoid double-filtering.

#### Message Interception Points
```typescript
// Incoming message (from client)
HTTP Request
  → Node Server
    → filteredTransport.handleRequest()
      → [C++ Filters: Request Logger]
        → SDK Transport (parse HTTP JSON)
          → SDK Server (handle JSON-RPC)
            → Tool Handler (execute logic)

// Outgoing message (to client)
Tool Result
  → SDK Server (format JSON-RPC)
    → filteredTransport.send()
      → [C++ Filters: Request Logger]
        → SDK Transport (format HTTP response)
          → HTTP Response
```

### Configuration System

#### Filter Configuration (`config-hybrid.json`)
```json
{
  "listeners": [
    {
      "name": "http_mcp_server_listener",
      "filter_chains": [
        {
          "name": "http_server_filters",
          "filters": [
            {
              "name": "rate_limiter",
              "type": "rate_limit",
              "config": {
                "strategy": "token_bucket",
                "bucket_capacity": 100,
                "refill_rate": 50,
                "allow_burst": true,
                "burst_size": 25,
                "debug_token_count": false
              }
            },
            {
              "name": "metrics",
              "type": "metrics",
              "config": {
                "provider": "internal",
                "rate_update_interval_seconds": 1,
                "report_interval_seconds": 10,
                "max_latency_threshold_ms": 5000,
                "error_rate_threshold": 10,
                "track_methods": true,
                "enable_histograms": false
              }
            },
            {
              "name": "request_logger",
              "type": "request_logger",
              "config": {
                "log_level": "debug",
                "log_format": "pretty",
                "include_timestamps": true,
                "include_payload": true,
                "max_payload_length": 1000,
                "output": "stdout"
              }
            },
            {
              "name": "circuit_breaker",
              "type": "circuit_breaker",
              "config": {
                "failure_threshold": 5,
                "error_rate_threshold": 0.5,
                "min_requests": 10,
                "timeout_ms": 10000,
                "window_size_ms": 60000,
                "half_open_max_requests": 3,
                "half_open_success_threshold": 2,
                "track_timeouts": true,
                "track_errors": true,
                "track_4xx_as_errors": false
              }
            }
          ]
        }
      ]
    }
  ]
}
```

**Configuration Flow**:
1. Load JSON from `config-hybrid.json`
2. Parse into `CanonicalConfig` TypeScript type
3. Pass to `GopherFilteredTransport` constructor
4. Convert to C structures via FFI
5. C++ Filter Chain Assembler validates and builds chain
6. Filters initialized with their specific configs

### Key Design Patterns

#### 1. **Transport Wrapper Pattern**
- GopherFilteredTransport wraps StreamableHTTPServerTransport
- Implements same interface, adds filter processing
- Transparent to SDK Server (no SDK modifications)
- Delegates protocol handling to SDK

#### 2. **FFI Bridge Pattern**
- TypeScript ↔ C++ communication via Koffi
- Opaque handles for C++ objects (dispatcher, filter chain)
- Explicit resource management (create/destroy functions)
- Type-safe conversions between JS and C types

#### 3. **Configuration-Driven Architecture**
- Filter chain defined in JSON configuration
- Runtime assembly from configuration
- No code changes to add/remove/reorder filters
- Supports multiple environments (dev, staging, prod configs)

#### 4. **Graceful Lifecycle Management**
```typescript
// Initialization order
1. createHybridDispatcher()      // C++ event loop
2. Load filter configuration     // JSON → TypeScript
3. Create SDK transport          // MCP protocol
4. Create GopherFilteredTransport // Wrap with filters
5. Connect server to transport   // Activate
6. Start HTTP server             // Listen

// Shutdown order (reverse)
1. Close HTTP server
2. Close filtered transport       // Cleanup filters
3. Destroy dispatcher             // Stop event loop
4. Exit process
```

### Building Your Own Hybrid Application

#### Template Structure
```typescript
// 1. Import required modules
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import { GopherFilteredTransport } from "../../../sdk/typescript/src/gopher-filtered-transport.js";
import { createHybridDispatcher, destroyHybridDispatcher } from "../../../sdk/typescript/src/filter-dispatcher.js";
import type { CanonicalConfig } from "../../../sdk/typescript/src/filter-types.js";

// 2. Define your application state
class YourApplicationState {
  // Your business logic state
}

// 3. Create and configure server
async function createYourServer() {
  // Load filter configuration
  const filterConfig: CanonicalConfig = JSON.parse(
    fs.readFileSync("config.json", "utf-8")
  );

  // Create MCP server
  const server = new Server({
    name: 'your-server-name',
    version: '1.0.0'
  }, {
    capabilities: { tools: {} }
  });

  // Create application state
  const state = new YourApplicationState();

  // Register your tools
  server.setRequestHandler(ListToolsRequestSchema, async () => ({
    tools: [
      { name: 'your_tool', description: '...', inputSchema: {...} }
    ]
  }));

  server.setRequestHandler(CallToolRequestSchema, async (request) => {
    // Handle tool calls
    const { name, arguments: args } = request.params;
    // Execute your business logic
    return { content: [...] };
  });

  // Create dispatcher and transport stack
  const dispatcher = createHybridDispatcher();
  const sdkTransport = new StreamableHTTPServerTransport({
    sessionIdGenerator: undefined,
    enableJsonResponse: true,
  });
  const filteredTransport = new GopherFilteredTransport(sdkTransport, {
    dispatcherHandle: dispatcher,
    filterConfig: filterConfig,
    debugLogging: process.env.DEBUG === '1'
  });

  // Connect and start
  await server.connect(filteredTransport);

  const httpServer = http.createServer(async (req, res) => {
    await filteredTransport.handleRequest(req, res);
  });

  httpServer.listen(PORT, HOST);

  // Setup graceful shutdown
  process.on('SIGINT', async () => {
    await httpServer.close();
    await filteredTransport.close();
    destroyHybridDispatcher(dispatcher);
    process.exit(0);
  });
}
```

#### Customization Points

1. **Application State**: Replace `CalculatorState` with your domain model
2. **Tool Definitions**: Define tools specific to your use case
3. **Tool Handlers**: Implement business logic for each tool
4. **Filter Configuration**: Enable/configure filters as needed:
   - Request logging for observability
   - Rate limiting for API protection
   - Circuit breaker for resilience (already configured)
   - Metrics for monitoring with real-time callbacks

## Key Features

### Hybrid Benefits
- ✅ **Uses official SDK** - Compatible with SDK ecosystem and updates
- ✅ **HTTP transport** - Stateless JSON responses over HTTP on port 8080 (opt-in stateful SSE via `--stateful`)
- ✅ **Enterprise filters** - Production-grade features from C++ implementation
- ✅ **Easy migration** - Drop-in enhancement for existing SDK apps
- ✅ **Best of both worlds** - SDK simplicity + Gopher power

### Available Tools
1. **calculate** - Arithmetic operations (add, subtract, multiply, divide, power, sqrt, factorial)
2. **memory** - Memory management (store, recall, clear)
3. **history** - Calculation history (list, clear, stats)

### Active Filters
1. **Rate Limiter** - Token bucket strategy with burst support (100 capacity, 50/sec refill, 25 burst)
2. **Metrics** - Real-time metrics collection with callbacks and configurable reporting intervals
3. **Request Logger** - Logs all JSON-RPC traffic with timestamps and payload inspection
4. **Circuit Breaker** - Failure detection with 5 failure threshold, 50% error rate threshold, and 10s timeout

## Quick Start

### TL;DR - Run Server and Client

All commands assume you've already run `npm install` inside `sdk/typescript`.

**Terminal 1 - Start Server**:
```bash
cd examples/typescript/calculator-hybrid
npx tsx calculator-server-hybrid.ts
```

Want the original SSE/session behaviour? Start with:
```bash
npx tsx calculator-server-hybrid.ts --stateful
```

**Terminal 2 - Start Client** (in a new terminal):
```bash
cd examples/typescript/calculator-hybrid
npx tsx calculator-client-hybrid.ts http://127.0.0.1:8080/mcp
```

Then use the interactive client to perform calculations!

> The server and client now resolve the MCP SDK via explicit relative imports, so setting `NODE_PATH` is no longer required.

**Example Session**:
```
calc> calc add 5 3
📊 Result: 5 + 3 = 8
⏱️  Response time: 25ms

calc> calc multiply 4 7
📊 Result: 4 × 7 = 28
⏱️  Response time: 18ms

calc> memory store 42
💾 Stored 42 in memory

calc> stats
📊 Calculator Statistics:
• Total calculations: 2
• Memory value: 42
• Operations: +: 1, ×: 1
```

### Connection Model & Concurrency

- The server now runs `StreamableHTTPServerTransport` in **stateless JSON mode** (`sessionIdGenerator: undefined`, `enableJsonResponse: true`), so each client initializes cleanly without sharing session state.
- In stateless mode, standalone SSE streams (`GET /mcp`) are intentionally disabled and return HTTP 405. All interactions flow through `POST /mcp`, which the SDK automatically handles via JSON responses.
- This change eliminates the `Invalid Request: Server already initialized` error when reconnecting and allows multiple calculator clients to stay connected simultaneously.
- Need SSE streaming or explicit session IDs? Start the server with `--stateful` to enable session-managed mode. In that configuration the server accepts GET `/mcp` SSE streams but behaves like the original single-session transport (only one initialization flow per active session).

### Prerequisites

1. **Build C++ library**:
   ```bash
   cd ../../..
   make build
   ```

2. **Install dependencies**:
   ```bash
   cd sdk/typescript
   npm install
   ```

3. **Build TypeScript SDK**:
   ```bash
   npm run build
   ```

### Run the Server

From the calculator-hybrid directory:
```bash
cd examples/typescript/calculator-hybrid
npx tsx calculator-server-hybrid.ts
```

Enable stateful SSE mode:
```bash
cd examples/typescript/calculator-hybrid
npx tsx calculator-server-hybrid.ts --stateful
```

Or with environment variables:
```bash
cd examples/typescript/calculator-hybrid
PORT=9090 HOST=0.0.0.0 npx tsx calculator-server-hybrid.ts
```

**Alternative** - Run from project root:
```bash
cd examples/typescript/calculator-hybrid
npx tsx calculator-server-hybrid.ts
```
The server file loads the configuration from `config-hybrid.json` in the same directory.

Expected output:
```
🚀 Starting Calculator Server (Scenario 2: Hybrid SDK + Filters)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
🔧 Server mode: Stateless (JSON responses, SSE disabled)
📡 Creating dispatcher for filter chain...
✅ Dispatcher created

📋 Loaded canonical filter configuration:
   Chain: http_server_filters
     - request_logger (request_logger)

🔌 Connecting MCP server to filtered transport...
✅ Server connected

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
✅ MCP Calculator Server is running

🏗️  Architecture:
  • Protocol: Official MCP SDK
  • Transport: StreamableHTTPServerTransport (HTTP (stateless JSON responses))
  • Filters: Gopher-MCP C++ via wrapper

📚 Available Tools:
  • calculate - Arithmetic operations (add, subtract, multiply, divide, power, sqrt, factorial)
  • memory - Memory management (store, recall, clear)
  • history - Calculation history (list, clear, stats)

🛡️  Active Filters:
  • Rate Limiter - Token bucket (100 cap, 50/sec refill, 25 burst)
  • Metrics - Real-time metrics collection & callbacks
  • Request Logger - Prints JSON-RPC traffic
  • Circuit Breaker - Failure detection (5 failures, 50% error rate, 10s timeout)

🌐 Server Address: http://127.0.0.1:8080/mcp

📝 Test with curl:
   curl -X POST http://127.0.0.1:8080/mcp \
     -H "Content-Type: application/json" \
     -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

🎯 Server ready and waiting for connections...
```

> When launched with `--stateful`, the startup banner reports `Server mode: Stateful…` and the transport line reads `StreamableHTTPServerTransport (HTTP/SSE (stateful sessions))`.

### Run the Client

The calculator client is a **simple MCP client** using only the standard SDK (no filters). It connects to the server using the Streamable HTTP transport; in the default stateless configuration the server responds with JSON (standalone SSE streams are disabled).

**Architecture**: Pure Standard SDK
- Protocol: `@modelcontextprotocol/sdk`
- Transport: `StreamableHTTPClientTransport` (HTTP transport)
- Filters: **None** (simple, clean implementation)
- Methods: Uses `client.callTool()` for MCP tool calls

**Note**: This client demonstrates how to use the standard MCP SDK without any custom filters or wrappers. It connects seamlessly to the hybrid server which uses filters on the server side.

#### Starting the Client

From the calculator-hybrid directory (with server already running):
```bash
cd examples/typescript/calculator-hybrid
npx tsx calculator-client-hybrid.ts http://127.0.0.1:8080/mcp
```

**Note**: The client uses relative imports to resolve the MCP SDK from `sdk/typescript/node_modules`, so no additional setup is needed.

Expected output (transport banner still mentions HTTP+SSE because it is printed by the SDK):
```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
🧮 MCP Calculator Client (Simple - No Filters)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Architecture: Pure Standard SDK
  • Protocol: @modelcontextprotocol/sdk
  • Transport: StreamableHTTPClientTransport (HTTP+SSE)
  • Filters: None
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

🔌 Connecting to calculator server...
📍 Server URL: http://127.0.0.1:8080/mcp
📡 Using HTTP+SSE transport (standard SDK)
✅ Connection established
🔄 Initializing MCP session...
✅ Session initialized

📚 Discovering available tools...

✅ Connected successfully!
📦 Available tools: 3
   • calculate - Perform arithmetic calculations
   • memory - Manage calculator memory
   • history - View calculation history

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
🧮 Simple Calculator Client - Interactive Mode
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Commands:
  calc <operation> <a> [b]  - Perform calculation
  memory <action> [value]   - Memory operations (store, recall, clear)
  history [limit]           - Show calculation history
  stats                     - Show statistics
  help                      - Show this help
  quit                      - Exit

Operations: add, subtract, multiply, divide, power, sqrt, factorial
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

calc>
```

#### Client Commands

Once connected, you can use these interactive commands:

**Calculator Operations**:
```bash
calc> calc add 5 3
📊 Result: 5 + 3 = 8
⏱️  Response time: 25ms

calc> calc multiply 4 7
📊 Result: 4 × 7 = 28
⏱️  Response time: 18ms

calc> calc sqrt 16
📊 Result: √16 = 4
⏱️  Response time: 20ms

calc> calc power 2 10
📊 Result: 2^10 = 1024
⏱️  Response time: 22ms
```

**Memory Operations**:
```bash
calc> memory store 42
💾 Stored 42 in memory

calc> memory recall
💾 Memory value: 42

calc> memory clear
💾 Memory cleared
```

**History & Statistics**:
```bash
calc> history 5
📜 Calculation History:
• 5 + 3 = 8 (10:30:15)
• 4 × 7 = 28 (10:30:20)
• √16 = 4 (10:30:25)
• 2^10 = 1024 (10:30:30)

calc> stats
📊 Calculator Statistics:
• Total calculations: 4
• Memory value: 42
• Operations: +: 1, ×: 1, sqrt: 1, ^: 1
```

**Help & Exit**:
```bash
calc> help
📖 Calculator Client Commands:
[shows full help text]

calc> quit
👋 Shutting down calculator client...
✅ Disconnected from server
✅ Calculator client closed
```

## Testing

You can test the server using:
1. **Interactive Client** (Recommended) - Use `calculator-client-hybrid.ts` for a better experience
2. **curl Commands** - Direct HTTP calls for testing individual endpoints

### Option 1: Interactive Client

See [Run the Client](#run-the-client) section above for the full interactive experience. The interactive client demonstrates all calculator features including operations, memory management, and history tracking.

### Option 2: curl Commands

### Health Check

```bash
curl http://127.0.0.1:8080/health
```

Expected response:
```json
{"status":"ok"}
```

### List Available Tools

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
```

### Perform Calculation

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc":"2.0",
    "id":2,
    "method":"tools/call",
    "params":{
      "name":"calculate",
      "arguments":{"operation":"add","a":5,"b":3}
    }
  }'
```

### Test Memory Operations

```bash
# Store value
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc":"2.0",
    "id":3,
    "method":"tools/call",
    "params":{
      "name":"memory",
      "arguments":{"action":"store","value":42}
    }
  }'

# Recall value
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc":"2.0",
    "id":4,
    "method":"tools/call",
    "params":{
      "name":"memory",
      "arguments":{"action":"recall"}
    }
  }'
```

### View Calculation History

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -d '{
    "jsonrpc":"2.0",
    "id":5,
    "method":"tools/call",
    "params":{
      "name":"history",
      "arguments":{"action":"list","limit":10}
    }
  }'
```

### Test Rate Limiting

The example includes a test script to verify rate limiting behavior:

```bash
cd examples/typescript/calculator-hybrid
./test-rate-limit-simple.sh
```

**What it does:**
- Sends 6 rapid requests to test the rate limiter
- First 3 requests should succeed (within bucket capacity)
- Requests 4-6 should be blocked with HTTP 429 errors

**Expected output:**
```bash
📤 Request 1: ✅ Success (tool list returned)
📤 Request 2: ✅ Success
📤 Request 3: ✅ Success
📤 Request 4: ❌ Rate limited (HTTP 429)
📤 Request 5: ❌ Rate limited (HTTP 429)
📤 Request 6: ❌ Rate limited (HTTP 429)
```

**Server logs will show:**
```
⚠️ Rate limit exceeded
   Client: undefined
   Remaining tokens: 0
```

**Note:** The test script uses the restrictive rate limit config (3 capacity, 1/sec refill) from `config-hybrid.json`. Adjust these values to test different rate limiting scenarios.

## Configuration

### Environment Variables

- `PORT` - HTTP server port (default: 8080)
- `HOST` - HTTP server host (default: 127.0.0.1)
- `DEBUG` - Enable debug logging (set to "1")

Example:
```bash
PORT=9090 HOST=0.0.0.0 DEBUG=1 node calculator-server-hybrid.js
```

### Filter Configuration

The server uses `config-hybrid.json` for filter configuration:

```json
{
  "listeners": [
    {
      "name": "http_mcp_server_listener",
      "filter_chains": [
        {
          "name": "http_server_filters",
          "filters": [
            {
              "name": "request_logger",
              "type": "request_logger",
              "config": {
                "log_level": "debug",
                "log_format": "pretty",
                "include_timestamps": true,
                "include_payload": true,
                "max_payload_length": 1000,
                "output": "stdout"
              }
            }
          ]
        }
      ]
    }
  ]
}
```

You can extend this with additional filters like rate limiting or circuit breakers.

### Debug Logging

Enable detailed filter logging:
```bash
DEBUG=1 npm run server:hybrid
```

## Comparison: Pure SDK vs Hybrid

### Server Comparison

| Feature | Pure SDK | Hybrid Server (HTTP stateless) | Native |
|---------|----------|--------------------------|---------|
| Protocol | Official SDK | Official SDK | C++ Native |
| Transport | stdio/HTTP | HTTP (stateless JSON) | HTTP+SSE |
| Filters | ❌ None | ✅ Gopher C++ | ✅ Gopher C++ |
| Request Logging | Basic | ✅ Advanced | ✅ Advanced |
| Rate Limiting | ❌ | ✅ Token bucket + burst | ✅ |
| Metrics Collection | ❌ | ✅ Real-time callbacks | ✅ |
| Circuit Breaker | ❌ | ✅ Fully configured | ✅ |
| Web Accessible | Requires setup | ✅ Built-in | ✅ Built-in |
| Performance | Baseline | +5-10% overhead | +0-5% overhead |
| Complexity | Low | Medium | High |
| Migration Effort | N/A | Minimal | Moderate |

> Need SSE streaming or session tracking? Launch the hybrid server with `--stateful` to opt into the original session-managed transport.

### Client Comparison

This example includes a **simple client** (`calculator-client-hybrid.ts`) that uses:
- ✅ Pure standard MCP SDK (no filters)
- ✅ `StreamableHTTPClientTransport` for HTTP transport
- ✅ Interactive CLI interface
- ✅ No FFI overhead (client-side)
- ✅ Clean, minimal implementation
- ✅ Uses `client.callTool()` for proper MCP protocol handling

The client demonstrates that you can use the standard SDK client with a hybrid server seamlessly!

**Key Implementation Details**:
```typescript
// Client connects to server
const transport = new StreamableHTTPClientTransport(new URL(serverUrl));
await client.connect(transport);

// Call tools using the standard SDK method
const result = await client.callTool({
    name: 'calculate',
    arguments: { operation: 'add', a: 5, b: 3 }
});

// Access the result
if (result.content && result.content[0].type === 'text') {
    console.log(result.content[0].text);  // "5 + 3 = 8"
}
```

## Migration from Pure SDK

**Before** (Pure SDK with HTTP):
```typescript
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";

const server = new Server({
  name: "my-server",
  version: "1.0.0"
});

const transport = new StreamableHTTPServerTransport({
  sessionIdGenerator: undefined,
  enableJsonResponse: true,
});

await server.connect(transport);
```

**After** (Hybrid with Filters):
```typescript
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import { GopherFilteredTransport } from "./gopher-filtered-transport.js";
import { createHybridDispatcher, destroyHybridDispatcher } from "./filter-dispatcher.js";
import * as http from "node:http";

const server = new Server({
  name: "my-server",
  version: "1.0.0"
});

// Create dispatcher for filter chain
const dispatcher = createHybridDispatcher();

// Create base transport
const baseTransport = new StreamableHTTPServerTransport({
  // Mirror the default stateless mode used by the example CLI (use --stateful at runtime if you need session IDs).
  sessionIdGenerator: undefined,
  enableJsonResponse: true,
});

// Wrap with filters
const filteredTransport = new GopherFilteredTransport(baseTransport, {
  dispatcherHandle: dispatcher,
  filterConfig: loadFilterConfig()
});

// Connect server
await server.connect(filteredTransport);

// Create HTTP server and handle requests
const httpServer = http.createServer(async (req, res) => {
  await filteredTransport.handleRequest(req, res);
});

httpServer.listen(8080, "127.0.0.1");
```

## Observability

### Request Logging

All JSON-RPC requests are logged by the request logger filter with pretty formatting:
```
🟢 [RequestLogger::onData] ENTRY
   Buffer length: 58
   End stream: true

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
→ INCOMING REQUEST:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

🟢 [RequestLogger::onWrite] ENTRY
   Buffer length: 318
   End stream: false

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
← OUTGOING RESPONSE:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
{"id":1,"jsonrpc":"2.0","result":{"tools":[...]}}
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

### Metrics Collection & Callbacks

The metrics filter provides real-time metrics via callbacks:

```typescript
import { registerMetricsCallbacks, type MetricsCallbacks } from "@gopher-mcp/sdk";

const callbacks: MetricsCallbacks = {
  onMetricsUpdate: (snapshot) => {
    console.log(`Requests: ${snapshot.requestsReceived}`);
    console.log(`Latency: ${snapshot.avgLatencyMs}ms`);
    console.log(`Rate: ${snapshot.currentReceiveRateBps}bps`);
  },
  onThresholdExceeded: (event) => {
    console.log(`Alert: ${event.metric} exceeded threshold`);
  },
  onError: (error) => {
    console.error('Metrics error:', error);
  }
};

const handle = registerMetricsCallbacks(filterChain.getHandle(), callbacks);
// ... later ...
unregisterMetricsCallbacks(filterChain.getHandle(), handle);
```

**Available Metrics**:
- Request/response/message counts (RX/TX)
- Byte counts (RX/TX)
- Error counts (RX/TX/protocol)
- Latency statistics (min/max/avg)
- Throughput rates (RX/TX in bps)
- Per-method tracking (when enabled)

### Unified Filter Event Callbacks

All filters emit events through a unified chain-level event system. Register a single callback to receive events from all filters:

```typescript
import { FilterEventType, type FilterEvent } from "@gopher-mcp/sdk";

// Get access to the filter chain
const filterChain = filteredTransport.filterChain;

// Register unified event callback for ALL filter events
filterChain.setEventCallback((event: FilterEvent) => {
  // Handle circuit breaker events
  if (event.filterName === 'circuit_breaker') {
    if (event.eventType === FilterEventType.CIRCUIT_STATE_CHANGE) {
      const { oldState, newState, reason } = event.eventData;
      console.log(`Circuit breaker: ${oldState} → ${newState} (${reason})`);
    } else if (event.eventType === FilterEventType.CIRCUIT_REQUEST_BLOCKED) {
      const { method } = event.eventData;
      console.log(`Request blocked by circuit breaker: ${method}`);
    } else if (event.eventType === FilterEventType.CIRCUIT_HEALTH_UPDATE) {
      const { successRate, averageLatencyMs } = event.eventData;
      console.log(`Health: ${(successRate * 100).toFixed(2)}% success, ${averageLatencyMs.toFixed(2)}ms avg latency`);
    }
  }

  // Handle rate limiter events
  // NOTE: C++ registers the filter as 'rate_limit', not 'rate_limiter'
  if (event.filterName === 'rate_limit') {
    if (event.eventType === FilterEventType.RATE_LIMIT_EXCEEDED) {
      const { clientId, remainingTokens } = event.eventData;
      console.warn(`⚠️ Rate limit exceeded`);
      console.warn(`   Client: ${clientId}`);
      console.warn(`   Remaining tokens: ${remainingTokens}`);
    }
  }

  // Log all events in debug mode
  if (process.env.DEBUG === '1') {
    console.log(`[FilterEvent] ${event.filterName}: ${FilterEventType[event.eventType]}`);
    console.log('  Data:', event.eventData);
  }
});
```

**Available Filter Events**:
- **Circuit Breaker**:
  - `CIRCUIT_STATE_CHANGE`: State transitions (CLOSED → OPEN → HALF_OPEN)
  - `CIRCUIT_REQUEST_BLOCKED`: Requests blocked when circuit is open
  - `CIRCUIT_HEALTH_UPDATE`: Periodic health statistics
- **Rate Limiter**:
  - `RATE_LIMIT_EXCEEDED`: Quota exhausted
  - `RATE_LIMIT_SAMPLE`: Token consumption metrics
  - `RATE_LIMIT_WINDOW_RESET`: Window boundary events
- **Metrics**:
  - `METRIC_UPDATE`: Real-time metrics snapshots
- **Request Logger**:
  - `REQUEST_LOGGED`: Request logging events
  - `RESPONSE_LOGGED`: Response logging events

**Circuit Breaker States**:
- **CLOSED**: Normal operation, all requests pass through
- **OPEN**: Circuit tripped, requests are blocked
- **HALF_OPEN**: Testing if service recovered, limited requests allowed

### Runtime Configuration

Filters can be enabled/disabled at runtime:
```typescript
await transport.setFilterEnabled('request_logger', false); // Disable
await transport.setFilterEnabled('request_logger', true);  // Enable
await transport.setFilterEnabled('rate_limiter', false);   // Disable rate limiting
await transport.setFilterEnabled('circuit_breaker', false); // Disable circuit breaker
```

## Troubleshooting

### Native Library Not Found
**Error**: `Cannot find module` or library loading fails

**Solution**:
```bash
# Rebuild C++ library
cd ../../..
make clean
make build
```

### HTTP Connection Refused
**Error**: `ECONNREFUSED` when testing with curl

**Solution**:
1. Ensure the server is running
2. Check the port matches (default: 8080)
3. Verify HOST binding (default: 127.0.0.1)

### 404 Not Found
**Error**: HTTP 404 when accessing the server

**Solution**: Make sure you're accessing the correct endpoint:
- MCP endpoint: `http://127.0.0.1:8080/mcp`
- Health check: `http://127.0.0.1:8080/health`

### Filter Configuration Not Found
**Error**: `Unable to locate http-server-filters.json configuration file`

**Solution**: Ensure the configuration file exists in one of these locations:
- `examples/typescript/configs/http-server-filters.json`
- `sdk/typescript/examples/configs/http-server-filters.json`

### Client Connection Issues
**Error**: `Connection failed` or `Session not found`

**Solution**:
1. Ensure the server is running before starting the client
2. Verify the server URL matches (default: `http://127.0.0.1:8080/mcp`)
3. Check that no firewall is blocking the connection
4. Try restarting the server to clear any stale sessions

### Client Module Not Found
**Error**: `Cannot find module '@modelcontextprotocol/sdk/client/index.js'`

**Solution**:
```bash
# Run from the SDK directory so dependencies resolve
cd sdk/typescript
npx tsx ../../examples/typescript/calculator-hybrid/calculator-client-hybrid.ts http://127.0.0.1:8080/mcp
```

### resultSchema.parse is not a function
**Error**: `resultSchema.parse is not a function` when calling tools

**Solution**: This error occurs when using the wrong SDK method. Make sure you're using `client.callTool()` instead of `client.request()`:

**❌ Wrong** - Using low-level request:
```typescript
const response = await client.request({
    method: 'tools/call',
    params: { name: 'calculate', arguments: {...} }
});
```

> If you want this code to run in stateful/SSE mode at startup, replace `sessionIdGenerator: undefined` with `sessionIdGenerator: () => randomUUID()` and remove `enableJsonResponse`. The CLI example achieves the same effect on demand via the `--stateful` flag.

**✅ Correct** - Using high-level callTool:
```typescript
const result = await client.callTool({
    name: 'calculate',
    arguments: {...}
});
```

The `callTool()` method handles all JSON-RPC protocol details and schema validation automatically.

## Development

### Project Structure
```
calculator-hybrid/
├── calculator-server-hybrid.ts          # Main server (hybrid with filters)
├── calculator-client-hybrid.ts          # Simple client (standard SDK, no filters)
├── config-hybrid.json                   # Filter configuration
├── configs/
│   └── hybrid-filters.json              # Alternative filter configuration
└── README.md                             # This file
```

**Files**:
- `calculator-server-hybrid.ts`: Hybrid server using SDK + Gopher filters
- `calculator-client-hybrid.ts`: Simple client using pure standard SDK (no filters)
- `config-hybrid.json`: Filter chain configuration (rate limiter, metrics, request logger, circuit breaker)
- `configs/hybrid-filters.json`: Alternative filter configuration format

### Adding Custom Filters

To add new filters, update `config-hybrid.json`:
```json
{
  "listeners": [
    {
      "name": "http_mcp_server_listener",
      "filter_chains": [
        {
          "name": "http_server_filters",
          "filters": [
            {
              "name": "my_custom_filter",
              "type": "custom_filter_type",
              "config": {
                "setting1": "value1"
              }
            }
          ]
        }
      ]
    }
  ]
}
```

**Available Filter Types**:
- `rate_limit` - Token bucket rate limiting with burst support
- `metrics` - Metrics collection with real-time callbacks and threshold alerts
- `request_logger` - Request/response logging with configurable formats
- `circuit_breaker` - Circuit breaker pattern with failure detection and auto-recovery

See the [Filter Configuration](#filter-configuration-config-hybridjson) section for detailed configuration options.

## Performance Considerations

- **FFI Overhead**: ~50-100μs per message (Koffi bridge)
- **Filter Processing**: <5ms P99 for typical chains
- **Memory**: ~10-20MB per filter chain
- **Throughput**: >1000 req/s for typical workloads

## License

MIT
