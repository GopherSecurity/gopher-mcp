# MCP C++ Examples - Server and Client

Production-ready examples demonstrating the MCP C++ SDK capabilities.

## Quick Start

### Build the Examples

```bash
# From the project root
make
# or
cmake -B build && cmake --build build
```

### Run the Server

```bash
# Start HTTP/SSE server on port 3000 (default)
./build/examples/mcp/mcp_example_server

# Start with verbose logging
./build/examples/mcp/mcp_example_server --verbose

# Start with custom port and metrics
./build/examples/mcp/mcp_example_server --port 8080 --metrics
```

### Run the Client

```bash
# Connect and run feature demo
./build/examples/mcp/mcp_example_client --demo --verbose

# Connect to custom host/port
./build/examples/mcp/mcp_example_client --host localhost --port 8080 --demo

# Run quietly (only show errors)
./build/examples/mcp/mcp_example_client --demo --quiet
```

## Example Server Features

The `mcp_example_server` demonstrates:

| Feature | Description |
|---------|-------------|
| **Multi-transport** | HTTP/SSE (default), stdio, WebSocket |
| **Resources** | Static resources and templates with subscriptions |
| **Tools** | Calculator, database query, system info tools |
| **Prompts** | Code review, data analysis, greeting prompts |
| **Sessions** | Session management with configurable timeouts |
| **Metrics** | Request counts, latency, error tracking |

### Server Command Line Options

```
Usage: mcp_example_server [options]

Options:
  --port <port>        Listen port (default: 3000)
  --host <address>     Bind address (default: 0.0.0.0)
  --transport <type>   Transport: http, stdio, websocket, all (default: http)
  --workers <n>        Number of worker threads (default: 4)
  --max-sessions <n>   Maximum concurrent sessions (default: 100)
  --config <path>      Configuration file (JSON format)
  --metrics            Enable metrics endpoint
  --verbose            Enable verbose logging
  --rpc-path <path>    HTTP JSON-RPC endpoint path (default: /rpc)
  --sse-path <path>    HTTP SSE events endpoint path (default: /events)
  --health-path <path> HTTP health check endpoint path (default: /health)
  --help               Show help message
```

### Server API Endpoints (HTTP Transport)

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/rpc` | POST | JSON-RPC 2.0 endpoint |
| `/events` | GET | Server-Sent Events stream |
| `/health` | GET | Health check endpoint |

## Example Client Features

The `mcp_example_client` demonstrates:

| Feature | Description |
|---------|-------------|
| **Protocol initialization** | MCP handshake and capability negotiation |
| **Resource listing** | Discover and read server resources |
| **Tool execution** | Call tools with parameters |
| **Prompt retrieval** | Get prompt templates with arguments |
| **Notifications** | Send log, progress, heartbeat notifications |
| **Batch requests** | Send multiple requests concurrently |
| **Connection pooling** | Efficient connection reuse |

### Client Command Line Options

```
Usage: mcp_example_client [options]

Options:
  --host <hostname>    Server hostname (default: localhost)
  --port <port>        Server port (default: 3000)
  --transport <type>   Transport: http, stdio, websocket (default: http)
  --demo               Run feature demonstrations
  --metrics            Show detailed metrics
  --verbose            Enable verbose logging
  --pool-size <n>      Connection pool size (default: 5)
  --max-retries <n>    Maximum retry attempts (default: 3)
  --workers <n>        Number of worker threads (default: 2)
  --quiet              Reduce output (only show errors)
  --help               Show help message
```

## Example Session

### Terminal 1: Start Server
```bash
$ ./build/examples/mcp/mcp_example_server --verbose

[INFO] MCP Server - Enterprise Edition
[INFO] Primary transport: http
[INFO] Listen address: http://0.0.0.0:3000
[INFO] Server started successfully!
[INFO] HTTP/SSE Endpoints:
[INFO]   JSON-RPC: POST http://0.0.0.0:3000/rpc
[INFO]   SSE Events: GET http://0.0.0.0:3000/events
[INFO]   Health: GET http://0.0.0.0:3000/health
[INFO] Press Ctrl+C to shutdown
```

### Terminal 2: Run Client
```bash
$ ./build/examples/mcp/mcp_example_client --demo --verbose

[INFO] MCP Client - Enterprise Edition
[INFO] Transport: http
[INFO] Server URI: http://localhost:3000
[INFO] Connected successfully!
[INFO] Protocol initialized: 2025-06-18
[INFO] Server: mcp-enterprise-server v2.0.0

[TEST 1] Protocol Initialization
  [PASS] Protocol initialization

[TEST 2] Custom Request Handlers
  [PASS] ping handler - returns pong=true
  [PASS] echo handler
  [PASS] server/status handler
  [PASS] health handler

[TEST 3] Resources
  Found 3 resources
    - Server Configuration (config://server/settings)
    - Server Event Log (log://server/events)
    - Server Metrics (metrics://server/stats)

[TEST 4] Tools
  Found 3 tools
    - calculator: Simple calculator for basic arithmetic
    - database_query: Execute database queries
    - system_info: Get system information
  add(10, 5) = 15.000000
  [PASS] calculator: add operation

[TEST 5] Prompts
  Found 3 prompts
    - code_review
    - data_analysis
    - greeting

TEST SUMMARY: 28 passed, 0 failed
```

## Filter Configuration

For advanced users, the server supports configurable filter chains via JSON configuration files.

### Basic Configuration (stdio transport)

`minimal_config.json`:
```json
{
  "filter_chains": [
    {
      "name": "server",
      "filters": [
        {
          "type": "json_rpc",
          "name": "json_rpc_protocol",
          "config": {
            "mode": "server",
            "use_framing": false,
            "strict_mode": true
          }
        }
      ]
    }
  ]
}
```

Usage:
```bash
./mcp_example_server --transport stdio --config minimal_config.json
```

### Production Configuration (HTTP/SSE with QoS)

`production_config.json`:
```json
{
  "filter_chains": [
    {
      "name": "server",
      "filters": [
        {
          "type": "metrics",
          "name": "metrics_collector",
          "config": {
            "track_methods": true,
            "report_interval_ms": 5000
          }
        },
        {
          "type": "rate_limit",
          "name": "rate_limiter",
          "config": {
            "max_requests_per_second": 100,
            "burst_size": 200
          }
        },
        {
          "type": "circuit_breaker",
          "name": "circuit_breaker",
          "config": {
            "failure_threshold": 5,
            "reset_timeout_ms": 5000
          }
        },
        {
          "type": "json_rpc",
          "name": "json_rpc_protocol",
          "config": {
            "mode": "server"
          }
        }
      ]
    }
  ]
}
```

### Available Filter Types

| Filter | Description |
|--------|-------------|
| `json_rpc` | JSON-RPC 2.0 protocol processing |
| `http_codec` | HTTP/1.1 protocol handling |
| `sse_codec` | Server-Sent Events processing |
| `metrics` | Performance metrics collection |
| `rate_limit` | Token bucket rate limiting |
| `circuit_breaker` | Fault tolerance circuit breaker |

## Other Examples

| Example | Description |
|---------|-------------|
| `mcp_example_server_enhanced.cc` | Extended server with additional features |
| `mcp_https_example.cc` | HTTPS/TLS server example |
| `mcp_config_example_server.cc` | Configuration-driven server |

## Troubleshooting

### Connection Refused
- Ensure server is running before starting client
- Check port is not in use: `lsof -i :3000`
- Verify firewall allows connections

### Parse Errors
- Enable verbose mode (`--verbose`) to see raw messages
- Check JSON syntax in configuration files

### Signal Handling
- Use Ctrl+C for graceful shutdown
- Server prints statistics before exit

## Related Documentation

- [Main README](../../README.md) - Project overview and installation
- [MCP Protocol](https://modelcontextprotocol.io) - Protocol specification
