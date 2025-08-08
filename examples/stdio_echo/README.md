# Advanced MCP Echo Examples

State-of-the-art MCP echo client and server implementations following the best architecture patterns.

## Features

### 🏗️ Architecture Improvements

1. **Worker Thread Model**
   - Dedicated dispatcher threads for event processing
   - Main thread for control plane, worker threads for data plane
   - Load balancing across workers
   - Optional CPU pinning for performance

2. **Filter Chain Architecture**
   - Modular, composable message processing pipeline
   - Separation of concerns (metrics, rate limiting, protocol handling)
   - Easy to extend with custom filters
   - Follows MCP filter pattern

3. **Connection Pooling** (Client)
   - Efficient connection reuse
   - Automatic connection lifecycle management
   - Configurable pool size and idle limits
   - Thread-safe connection acquisition/release

4. **Enhanced Error Handling**
   - Detailed failure tracking with context
   - Stack trace capture for debugging
   - Failure categorization (connection, protocol, timeout, etc.)
   - Historical failure tracking

5. **Flow Control**
   - Watermark-based backpressure
   - Prevents memory exhaustion
   - Automatic read enable/disable based on buffer levels
   - Configurable high/low watermarks

6. **Observability**
   - Real-time metrics collection
   - Request latency tracking (min/avg/max)
   - Connection and request counters
   - Error rate monitoring
   - Periodic metrics reporting

### 🎯 Client-Specific Features

- **Circuit Breaker Pattern**: Prevents cascading failures
- **Request Timeout Management**: Automatic timeout detection
- **Batch Processing**: Send multiple requests efficiently
- **Retry Logic**: Configurable retry on failure
- **Future-based API**: Async request/response handling

### 🎯 Server-Specific Features

- **Graceful Shutdown**: Clean connection closure
- **Multi-worker Support**: Scale across CPU cores
- **Echo Protocol**: Full JSON-RPC echo implementation
- **Special Message Handling**: Shutdown notifications

## Building

```bash
cd build
cmake ..
make stdio_echo_server_advanced stdio_echo_client_advanced
```

## Usage

### Server

```bash
./stdio_echo_server_advanced [options]

Options:
  --workers N           Number of worker threads (default: 2)
  --metrics-interval S  Metrics reporting interval in seconds (default: 10)
  --no-metrics         Disable metrics collection
  --help              Show help message
```

Example:
```bash
./stdio_echo_server_advanced --workers 4 --metrics-interval 5
```

### Client

```bash
./stdio_echo_client_advanced [options]

Options:
  --requests N    Number of requests to send (default: 10)
  --delay MS     Delay between requests in ms (default: 100)
  --batch        Send requests in batch mode
  --help         Show help message
```

Example:
```bash
./stdio_echo_client_advanced --requests 100 --delay 50 --batch
```

## Testing

Run the comprehensive test suite:

```bash
./test_advanced_echo.sh
```

This runs multiple test scenarios:
- Sequential requests with delays
- Batch processing
- High load testing
- Stress testing with zero delays

## Architecture Details

### Application Base Framework

The `mcp_application_base.h` provides:

```cpp
class ApplicationBase {
  // Worker thread management
  void start();  // Starts main + worker threads
  void stop();   // Graceful shutdown
  
  // Metrics and observability
  ApplicationStats& getStats();
  void trackFailure(const FailureReason& reason);
  
  // Filter chain setup
  virtual void setupFilterChain(FilterChainBuilder& builder);
  
  // Worker initialization
  virtual void initializeWorker(WorkerContext& worker) = 0;
};
```

### Filter Chain

Filters are applied in order:

1. **RateLimitFilter** - Request rate limiting
2. **MetricsFilter** - Metrics collection
3. **FlowControlFilter** - Watermark-based backpressure
4. **McpProtocolFilter** - JSON-RPC message processing

### Connection Pool

```cpp
class ConnectionPool {
  ConnectionPtr acquireConnection();  // Get or create connection
  void releaseConnection(ConnectionPtr);  // Return to pool
  
  // Configurable limits
  size_t max_connections_;
  size_t max_idle_;
};
```

### Circuit Breaker

```cpp
class CircuitBreaker {
  bool allowRequest();     // Check if requests allowed
  void recordSuccess();    // Update on success
  void recordFailure();    // Update on failure
  
  // States: Closed (normal), Open (rejecting), HalfOpen (testing)
};
```

## Performance Considerations

1. **Thread Affinity**: Use `--pin-threads` to bind workers to CPUs
2. **Buffer Sizes**: Adjust watermarks based on message sizes
3. **Worker Count**: Set based on CPU cores and load
4. **Metrics Overhead**: Disable with `--no-metrics` for maximum performance

## Comparison with Basic Implementation

| Feature | Basic | Advanced |
|---------|-------|----------|
| Threading | Single thread | Multi-worker threads |
| Error Handling | Basic | Detailed with context |
| Connection Management | Direct | Pooled with lifecycle |
| Flow Control | None | Watermark-based |
| Metrics | None | Comprehensive |
| Message Processing | Monolithic | Filter chain |
| Failure Handling | Basic | Circuit breaker |
| Observability | Minimal | Full metrics + tracing ready |

## Future Enhancements

- [ ] TLS/SSL support
- [ ] HTTP/2 transport
- [ ] Distributed tracing integration
- [ ] Dynamic configuration updates
- [ ] Health check endpoints
- [ ] Rate limiting with token bucket
- [ ] Request prioritization
- [ ] Connection multiplexing

## Troubleshooting

### Server won't start
- Check if another process is using stdio
- Verify libevent is installed
- Check file descriptor limits

### Client timeouts
- Increase timeout with request manager
- Check circuit breaker state
- Verify server is running

### High memory usage
- Adjust buffer watermarks
- Reduce connection pool size
- Enable flow control

### Poor performance
- Increase worker threads
- Disable metrics collection
- Use CPU pinning
- Adjust buffer sizes