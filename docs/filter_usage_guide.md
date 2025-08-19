# MCP Filter Usage Guide: Client vs Server

This document explains which filters are appropriate for MCP clients vs servers and why.

## Filter Layer Architecture

```
Network Layer (TCP/Stdio/HTTP)
    ↓
Transport Security (TLS/SSL) [BOTH]
    ↓
Circuit Breaker [CLIENT primarily, SERVER optional]
    ↓
Rate Limiting [SERVER primarily, CLIENT optional]
    ↓
Backpressure [BOTH]
    ↓
Metrics Collection [BOTH]
    ↓
Request Validation [SERVER primarily, CLIENT optional]
    ↓
Protocol Processing (JSON-RPC) [BOTH]
    ↓
Application Layer
```

## Filter Usage by Component

### 1. Circuit Breaker Filter (`circuit_breaker_filter.h`)

**PRIMARY USE: CLIENT**
- **Client**: Essential for preventing cascading failures when server is unhealthy
  - Protects against repeatedly calling a failing server
  - Implements retry logic with exponential backoff
  - Transitions between CLOSED → OPEN → HALF_OPEN states
  - Example: If MCP server returns 5 consecutive errors, circuit opens

**SECONDARY USE: SERVER** 
- **Server**: Optional for protecting downstream dependencies
  - Only if server makes outbound calls to other services
  - Protects server's own dependencies (databases, APIs)
  - Example: If tool execution repeatedly fails, circuit opens for that tool

```cpp
// CLIENT USAGE - Primary
class McpClient {
  void setupFilterChain() {
    // Circuit breaker is outermost to fail fast
    builder.addFilter([]() {
      CircuitBreakerConfig config;
      config.failure_threshold = 5;  // Open after 5 failures
      config.timeout = std::chrono::seconds(30);  // Try again after 30s
      return std::make_shared<CircuitBreakerFilter>(callbacks, config);
    });
  }
};

// SERVER USAGE - Optional, only for downstream protection
class McpServer {
  void setupFilterChain() {
    if (has_downstream_dependencies) {
      builder.addFilter([]() {
        // Only for protecting server's own external calls
        return std::make_shared<CircuitBreakerFilter>(callbacks, config);
      });
    }
  }
};
```

### 2. Rate Limit Filter (`rate_limit_filter.h`)

**PRIMARY USE: SERVER**
- **Server**: Critical for protecting against abuse and DOS
  - Limits requests per client/connection
  - Implements various strategies (token bucket, sliding window)
  - Enforces fair usage across clients
  - Example: Limit each client to 100 requests/minute

**SECONDARY USE: CLIENT**
- **Client**: Optional for self-throttling
  - Prevents overwhelming server (good citizen behavior)
  - Useful in batch processing scenarios
  - Example: Process maximum 10 requests/second to be polite

```cpp
// SERVER USAGE - Primary
class McpServer {
  void setupFilterChain() {
    // Rate limiting is essential for servers
    builder.addFilter([]() {
      RateLimitConfig config;
      config.strategy = RateLimitStrategy::TokenBucket;
      config.bucket_capacity = 100;  // 100 requests
      config.refill_rate = 10;  // 10 per second
      config.per_client_limiting = true;  // Per-client limits
      return std::make_shared<RateLimitFilter>(callbacks, config);
    });
  }
};

// CLIENT USAGE - Optional self-throttling
class McpClient {
  void setupFilterChain() {
    if (batch_mode) {
      builder.addFilter([]() {
        RateLimitConfig config;
        config.strategy = RateLimitStrategy::LeakyBucket;
        config.leak_rate = 5;  // Max 5 requests/second
        return std::make_shared<RateLimitFilter>(callbacks, config);
      });
    }
  }
};
```

### 3. Backpressure Filter (`backpressure_filter.h`)

**EQUAL USE: BOTH CLIENT AND SERVER**
- **Server**: Prevents memory exhaustion from slow clients
  - Pauses reading when buffers exceed high watermark
  - Resumes when buffers drop below low watermark
  - Essential for streaming responses (SSE)

- **Client**: Prevents memory exhaustion from large responses
  - Controls flow when processing is slower than receiving
  - Important for resource-constrained clients
  - Critical for streaming data consumption

```cpp
// SERVER USAGE - Essential for memory management
class McpServer {
  void setupFilterChain() {
    builder.addFilter([]() {
      BackpressureConfig config;
      config.high_watermark = 1024 * 1024;  // 1MB
      config.low_watermark = 256 * 1024;   // 256KB
      // Pause reading from client when buffer > 1MB
      return std::make_shared<BackpressureFilter>(callbacks, config);
    });
  }
};

// CLIENT USAGE - Essential for flow control
class McpClient {
  void setupFilterChain() {
    builder.addFilter([]() {
      BackpressureConfig config;
      config.high_watermark = 512 * 1024;  // 512KB (smaller for client)
      config.low_watermark = 128 * 1024;   // 128KB
      // Pause reading from server when processing is slow
      return std::make_shared<BackpressureFilter>(callbacks, config);
    });
  }
};
```

### 4. Metrics Filter (`metrics_filter.h`)

**EQUAL USE: BOTH CLIENT AND SERVER**
- **Server**: Monitors service health and performance
  - Tracks request rates, error rates, latencies
  - Per-method statistics for capacity planning
  - Connection-level metrics for debugging

- **Client**: Monitors integration health
  - Tracks API call performance
  - Identifies slow endpoints
  - Monitors retry rates and failures

```cpp
// SERVER USAGE - Comprehensive monitoring
class McpServer {
  void setupFilterChain() {
    builder.addFilter([]() {
      MetricsFilter::Config config;
      config.report_interval = std::chrono::seconds(10);
      config.track_methods = true;  // Per-method metrics
      config.enable_histograms = true;  // Latency distribution
      return std::make_shared<MetricsFilter>(callbacks, config);
    });
  }
};

// CLIENT USAGE - Integration monitoring
class McpClient {
  void setupFilterChain() {
    builder.addFilter([]() {
      MetricsFilter::Config config;
      config.report_interval = std::chrono::seconds(60);  // Less frequent
      config.track_methods = true;  // Track which calls are slow
      config.enable_histograms = false;  // Simpler metrics
      return std::make_shared<MetricsFilter>(callbacks, config);
    });
  }
};
```

### 5. Request Validation Filter (`request_validation_filter.h`)

**PRIMARY USE: SERVER**
- **Server**: Security and protocol compliance
  - Validates method names against whitelist/blacklist
  - Enforces parameter size limits
  - Checks protocol version compatibility
  - Prevents injection attacks
  - Example: Block unauthorized methods, validate JSON depth

**SECONDARY USE: CLIENT**
- **Client**: Optional pre-validation before sending
  - Catch errors early before network round-trip
  - Useful during development/testing
  - Example: Validate request format before sending

```cpp
// SERVER USAGE - Primary, security critical
class McpServer {
  void setupFilterChain() {
    builder.addFilter([]() {
      RequestValidationConfig config;
      config.validate_methods = true;
      config.allowed_methods = {
        "initialize", "ping", "tools/list", "tools/call",
        "resources/list", "resources/read"
      };
      config.blocked_methods = {"admin/*", "debug/*"};
      config.max_param_size = 1024 * 1024;  // 1MB limit
      config.validate_json_depth = true;
      config.max_json_depth = 100;
      return std::make_shared<RequestValidationFilter>(callbacks, config);
    });
  }
};

// CLIENT USAGE - Optional, for early validation
class McpClient {
  void setupFilterChain() {
    if (development_mode) {
      builder.addFilter([]() {
        RequestValidationConfig config;
        config.validate_protocol_version = true;
        config.required_protocol_version = "2.0";
        // Catch protocol errors before sending
        return std::make_shared<RequestValidationFilter>(callbacks, config);
      });
    }
  }
};
```

## Filter Ordering Guidelines

### Server Filter Order (outermost to innermost):
1. **Rate Limiting** - Reject excess requests immediately
2. **Circuit Breaker** - (Optional) Protect downstream
3. **Backpressure** - Control memory usage
4. **Metrics** - Measure everything
5. **Request Validation** - Security checks
6. **JSON-RPC** - Protocol processing

### Client Filter Order (outermost to innermost):
1. **Circuit Breaker** - Fail fast on server issues
2. **Rate Limiting** - (Optional) Self-throttle
3. **Backpressure** - Control memory usage
4. **Metrics** - Monitor integration
5. **Request Validation** - (Optional) Early validation
6. **JSON-RPC** - Protocol processing

## Use Case Examples

### High-Volume Production Server
```cpp
void setupProductionServer() {
  // MUST HAVE filters for production server
  builder.addFilter(createRateLimitFilter());      // Prevent DOS
  builder.addFilter(createBackpressureFilter());   // Memory protection
  builder.addFilter(createMetricsFilter());        // Observability
  builder.addFilter(createValidationFilter());     // Security
  builder.addFilter(createJsonRpcFilter());        // Protocol
}
```

### Resilient Production Client
```cpp
void setupProductionClient() {
  // MUST HAVE filters for production client
  builder.addFilter(createCircuitBreakerFilter()); // Failure handling
  builder.addFilter(createBackpressureFilter());   // Memory protection
  builder.addFilter(createMetricsFilter());        // Monitoring
  builder.addFilter(createJsonRpcFilter());        // Protocol
}
```

### Development/Testing Setup
```cpp
void setupDevelopment() {
  // Minimal filters for development
  builder.addFilter(createMetricsFilter());        // See what's happening
  builder.addFilter(createJsonRpcFilter());        // Protocol processing
}
```

## Summary Table

| Filter | Server Priority | Client Priority | Primary Purpose |
|--------|----------------|-----------------|-----------------|
| Circuit Breaker | Optional | **Essential** | Prevent cascading failures |
| Rate Limiting | **Essential** | Optional | Prevent abuse/DOS |
| Backpressure | **Essential** | **Essential** | Memory/flow control |
| Metrics | **Essential** | **Essential** | Observability |
| Request Validation | **Essential** | Optional | Security/compliance |
| JSON-RPC | **Essential** | **Essential** | Protocol processing |

## Key Principles

1. **Servers protect themselves**: Rate limiting, validation, backpressure
2. **Clients protect against server failures**: Circuit breaker, retries
3. **Both need observability**: Metrics for debugging and monitoring
4. **Both need flow control**: Backpressure prevents memory issues
5. **Order matters**: Fail-fast filters go first, protocol processing last