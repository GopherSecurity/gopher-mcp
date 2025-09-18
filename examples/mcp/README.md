# MCP Example Server - Configuration Guide

## Overview

The MCP Example Server (`mcp_example_server`) is a production-ready Model Context Protocol server that supports configurable filter chains for processing requests through various protocol layers and quality-of-service filters.

## Quick Start

### Basic Usage

```bash
# Run with default settings (no filter configuration)
./mcp_example_server

# Run with stdio transport
./mcp_example_server --transport stdio

# Run with TCP transport on custom port
./mcp_example_server --transport tcp --port 8080

# Run with filter configuration
./mcp_example_server --transport stdio --config minimal_config.json
```

### Command Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--transport` | Transport type: `stdio`, `tcp`, `http_sse` | `stdio` |
| `--port` | TCP port number | `3000` |
| `--config` | Path to JSON configuration file | None |
| `--verbose` | Enable verbose logging | `false` |
| `--http-health-path` | HTTP health check endpoint | `/health` |
| `--help` | Show help message | - |

## Filter Configuration

### Configuration File Structure

Filter configurations are defined in JSON files with the following structure:

```json
{
  "filter_chains": [
    {
      "name": "server",
      "filters": [
        {
          "type": "filter_type",
          "name": "filter_instance_name",
          "config": {
            // Filter-specific configuration
          }
        }
      ]
    }
  ]
}
```

### Available Filter Types

#### 1. JSON-RPC Protocol Filter (`json_rpc`)

Handles JSON-RPC 2.0 protocol processing.

**Configuration:**
```json
{
  "type": "json_rpc",
  "name": "json_rpc_protocol",
  "config": {
    "mode": "server",           // "server" or "client"
    "use_framing": false,        // Enable length-prefixed framing
    "max_message_size": 1048576, // Max message size in bytes
    "batch_enabled": true,       // Enable batch requests
    "batch_limit": 100,          // Max requests in a batch
    "strict_mode": true,         // Enforce strict JSON-RPC 2.0
    "timeout_ms": 30000,         // Request timeout in milliseconds
    "validate_params": true      // Validate method parameters
  }
}
```

#### 2. HTTP Codec Filter (`http_codec`)

Processes HTTP/1.1 protocol layer.

**Configuration:**
```json
{
  "type": "http_codec",
  "name": "http_parser",
  "config": {
    "mode": "server",            // "server" or "client"
    "max_header_size": 8192,     // Max HTTP header size in bytes
    "max_body_size": 1048576,    // Max HTTP body size in bytes
    "timeout_ms": 30000,         // Request timeout
    "keepalive": true            // Enable HTTP keep-alive
  }
}
```

#### 3. Server-Sent Events Filter (`sse_codec`)

Handles SSE protocol for event streaming.

**Configuration:**
```json
{
  "type": "sse_codec",
  "name": "sse_parser",
  "config": {
    "max_event_size": 65536,     // Max event size in bytes
    "reconnect_time_ms": 3000,   // Client reconnection time
    "enable_compression": false,  // Enable compression
    "keepalive_interval_ms": 15000 // Keepalive ping interval
  }
}
```

#### 4. Metrics Filter (`metrics`)

Collects performance metrics and statistics.

**Configuration:**
```json
{
  "type": "metrics",
  "name": "metrics_collector",
  "config": {
    "track_methods": true,       // Track per-method metrics
    "track_errors": true,        // Track error rates
    "report_interval_ms": 5000,  // Reporting interval
    "histogram_buckets": [0.001, 0.01, 0.1, 1, 10], // Latency buckets
    "export_format": "prometheus" // Metrics format
  }
}
```

#### 5. Rate Limiting Filter (`rate_limit`)

Enforces request rate limits.

**Configuration:**
```json
{
  "type": "rate_limit",
  "name": "rate_limiter",
  "config": {
    "max_requests_per_second": 100,  // Global rate limit
    "burst_size": 200,                // Token bucket burst size
    "strategy": "token_bucket",       // Rate limiting strategy
    "per_method_limits": {            // Per-method limits (optional)
      "initialize": 10,
      "shutdown": 10,
      "process": 1000
    }
  }
}
```

#### 6. Circuit Breaker Filter (`circuit_breaker`)

Provides fault tolerance through circuit breaking.

**Configuration:**
```json
{
  "type": "circuit_breaker",
  "name": "circuit_breaker",
  "config": {
    "failure_threshold": 5,          // Failures to open circuit
    "success_threshold": 2,          // Successes to close circuit
    "timeout_ms": 1000,              // Call timeout
    "reset_timeout_ms": 5000,        // Time before retry
    "half_open_max_requests": 3,     // Requests in half-open state
    "error_percentage_threshold": 50, // Error % to open circuit
    "min_request_count": 10          // Min requests for % calculation
  }
}
```

## Configuration Examples

### Minimal Configuration (JSON-RPC Only)

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

**Usage:**
```bash
./mcp_example_server --transport stdio --config minimal_config.json
```

This configuration is ideal for:
- Direct stdio communication
- Simple JSON-RPC processing
- No HTTP/SSE overhead

### Full Protocol Stack Configuration

`full_config.json`:
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
          "type": "http_codec",
          "name": "http_parser",
          "config": {
            "mode": "server",
            "max_header_size": 8192
          }
        },
        {
          "type": "sse_codec",
          "name": "sse_parser",
          "config": {
            "max_event_size": 65536
          }
        },
        {
          "type": "json_rpc",
          "name": "json_rpc_protocol",
          "config": {
            "mode": "server",
            "use_framing": true
          }
        }
      ]
    }
  ]
}
```

**Usage:**
```bash
./mcp_example_server --transport http_sse --config full_config.json --port 8080
```

This configuration provides:
- Full HTTP/SSE/JSON-RPC protocol stack
- Metrics collection
- Rate limiting
- Suitable for production deployments

### QoS-Focused Configuration

`qos_config.json`:
```json
{
  "filter_chains": [
    {
      "name": "server",
      "filters": [
        {
          "type": "circuit_breaker",
          "name": "circuit_breaker",
          "config": {
            "failure_threshold": 5,
            "reset_timeout_ms": 5000
          }
        },
        {
          "type": "rate_limit",
          "name": "rate_limiter",
          "config": {
            "max_requests_per_second": 50,
            "burst_size": 100
          }
        },
        {
          "type": "metrics",
          "name": "metrics_collector",
          "config": {
            "track_methods": true,
            "track_errors": true
          }
        },
        {
          "type": "json_rpc",
          "name": "json_rpc_protocol",
          "config": {
            "mode": "server",
            "use_framing": false
          }
        }
      ]
    }
  ]
}
```

This configuration emphasizes:
- Fault tolerance with circuit breaking
- Rate limiting for resource protection
- Comprehensive metrics collection

## Filter Chain Processing Order

Filters are processed in the order they appear in the configuration:

1. **Inbound Request Flow**: First filter → Last filter
2. **Outbound Response Flow**: Last filter → First filter

Example with full stack:
```
Inbound:  Request → Metrics → RateLimit → HTTP → SSE → JSON-RPC → Application
Outbound: Application → JSON-RPC → SSE → HTTP → RateLimit → Metrics → Response
```

## Transport-Specific Recommendations

### Stdio Transport
- Use minimal configuration (JSON-RPC only)
- Set `use_framing: false` for newline-delimited messages
- Ideal for command-line tools and scripts

### TCP Transport
- Can use any filter configuration
- Consider adding rate limiting for protection
- Add metrics for monitoring

### HTTP/SSE Transport
- Requires full protocol stack (HTTP + SSE + JSON-RPC)
- Always include http_codec and sse_codec filters
- Set `use_framing: false` as SSE handles message boundaries

## Monitoring and Debugging

### Verbose Mode

Enable detailed logging:
```bash
./mcp_example_server --config full_config.json --verbose
```

### Configuration Validation

The server validates configuration on startup and reports:
- Number of filters loaded
- Filter types and names
- Configuration errors

Example output:
```
[CONFIG] Found server filter chain configuration
[CONFIG] Filter chain will include 5 filters:
[CONFIG]   - metrics
[CONFIG]   - rate_limit
[CONFIG]   - http_codec
[CONFIG]   - sse_codec
[CONFIG]   - json_rpc (framing: enabled)
```

### Runtime Behavior

Filters log their activity when verbose mode is enabled:
```
[JSON-RPC-FILTER] Created JsonRpcProtocolFilter - mode: server, framing: disabled
[JSON-RPC-FILTER] Configuration applied - framing: disabled
[JSON-RPC-FILTER] onData called - buffer size: 45, end_stream: false
```

## Best Practices

1. **Start Simple**: Begin with minimal configuration and add filters as needed
2. **Order Matters**: Place QoS filters (metrics, rate_limit) before protocol filters
3. **Transport Matching**: Ensure filter chain matches transport requirements
4. **Resource Limits**: Always configure appropriate size and rate limits
5. **Monitoring**: Include metrics filter in production deployments
6. **Testing**: Test configuration changes in development before production

## Troubleshooting

### Common Issues

1. **"Filter type not found"**: Ensure filter type is correctly spelled
2. **"Invalid configuration"**: Check JSON syntax and required fields
3. **"Connection refused"**: Verify port is not in use and transport matches client
4. **"Parse error"**: Validate JSON structure and data types

### Configuration Tips

- Use JSON validators to check syntax
- Start with provided example configurations
- Enable verbose mode for debugging
- Check server logs for detailed error messages

## Advanced Usage

### Custom Filter Development

To add custom filters:
1. Implement the filter following the Filter interface
2. Register with FilterRegistry
3. Add configuration schema
4. Include in filter chain configuration

### Dynamic Reconfiguration

Note: Hot reload is not currently supported. To change configuration:
1. Stop the server (Ctrl+C)
2. Modify configuration file
3. Restart server with new configuration

## Examples Directory

This directory contains:
- `minimal_config.json` - Basic JSON-RPC only configuration
- `full_config.json` - Complete protocol stack with QoS filters
- `mcp_example_server.cc` - Server implementation source
- `mcp_example_client.cc` - Example client implementation

## Related Documentation

- [MCP Protocol Specification](https://modelcontextprotocol.io)