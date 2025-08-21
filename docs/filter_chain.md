# Filter Chain Documentation

The Filter Chain provides a modular, extensible processing pipeline for all network data in the MCP C++ SDK. Filters enable protocol handling, routing, metrics, rate limiting, and other cross-cutting concerns.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Read Path                            │
│  Socket → Buffer → Read Filters → Application           │
│                                                         │
│  ┌──────┐  ┌──────────┐  ┌─────────┐  ┌────────────┐    │
│  │Socket│→ │HTTP Codec│→ │Routing  │→ │JSON-RPC    │    │
│  │ Data │  │  Filter  │  │ Filter  │  │   Filter   │    │
│  └──────┘  └──────────┘  └─────────┘  └────────────┘    │
├─────────────────────────────────────────────────────────┤
│                   Write Path                            │
│  Application → Write Filters → Buffer → Socket          │
│                                                         │
│  ┌────────────┐  ┌─────────┐  ┌──────────┐  ┌──────┐    │
│  │JSON-RPC    │→ │Metrics  │→ │HTTP Codec│→ │Socket│    │
│  │  Response  │  │ Filter  │  │  Filter  │  │Write │    │
│  └────────────┘  └─────────┘  └──────────┘  └──────┘    │
└─────────────────────────────────────────────────────────┘
```

## Core Design Principles

### 1. Stateless Filters
Filters must be stateless to support HTTP/2 concurrent streams:
```cpp
// BAD - stores state
class StatefulFilter : public Filter {
    std::string current_request_;  // ❌ Will fail with concurrent requests
    
    FilterStatus onData(Buffer& data, bool end_stream) {
        current_request_ += data.toString();
        // Process when complete...
    }
};

// GOOD - stateless processing
class StatelessFilter : public Filter {
    FilterStatus onData(Buffer& data, bool end_stream) {
        // Process data immediately without storing state
        processDataInPlace(data);
        return FilterStatus::Continue;
    }
};
```

### 2. In-Place Buffer Modification
Filters modify buffers directly for zero-copy performance:
```cpp
FilterStatus HttpCodecFilter::onWrite(Buffer& data, bool end_stream) {
    // Save original body
    size_t body_length = data.length();
    std::string body(static_cast<const char*>(data.linearize(body_length)), 
                     body_length);
    
    // Clear and rebuild with HTTP headers
    data.drain(body_length);
    
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Length: " << body_length << "\r\n";
    response << "\r\n";
    response << body;
    
    // Add formatted response back
    std::string response_str = response.str();
    data.add(response_str.c_str(), response_str.length());
    
    return FilterStatus::Continue;
}
```

### 3. Filter Chain Processing
Filters are processed sequentially with early termination support:
```cpp
enum class FilterStatus {
    Continue,        // Continue to next filter
    StopIteration    // Stop processing chain
};
```

## Filter Types

### Protocol Filters

#### HttpCodecFilter (`filter/http_codec_filter.h`)
Handles HTTP/1.1 and HTTP/2 protocol encoding/decoding.

**Responsibilities:**
- Parse HTTP requests/responses
- Add HTTP headers to responses
- Handle keep-alive connections
- Support chunked transfer encoding

```cpp
class HttpCodecFilter : public Filter {
    // Parse incoming HTTP request
    FilterStatus onData(Buffer& data, bool end_stream) {
        // Parse HTTP headers
        auto headers = parseHeaders(data);
        
        // Extract body based on Content-Length
        auto body = extractBody(data, headers);
        
        // Pass to next filter
        return FilterStatus::Continue;
    }
    
    // Format outgoing HTTP response
    FilterStatus onWrite(Buffer& data, bool end_stream) {
        // Wrap response body with HTTP headers
        formatHttpResponse(data);
        return FilterStatus::Continue;
    }
};
```

#### SseCodecFilter (`filter/sse_codec_filter.h`)
Handles Server-Sent Events protocol.

**Features:**
- Event stream parsing
- Automatic reconnection support
- Event ID tracking
- Heartbeat handling

### Routing Filters

#### HttpRoutingFilter (`filter/http_routing_filter.h`)
Routes HTTP requests to appropriate handlers.

**Stateless Routing:**
```cpp
class HttpRoutingFilter : public Filter {
    // Route handlers registered at startup
    std::map<std::string, Handler> handlers_;
    
    FilterStatus onData(Buffer& data, bool end_stream) {
        // Parse request to get method and path
        auto [method, path] = parseRequest(data);
        
        // Find and invoke handler immediately
        auto handler = handlers_.find(method + ":" + path);
        if (handler != handlers_.end()) {
            auto response = handler->second(data);
            sendResponse(response);
            return FilterStatus::StopIteration;
        }
        
        return FilterStatus::Continue;
    }
};
```

### Quality of Service Filters

#### RateLimitFilter (`filter/rate_limit_filter.h`)
Token bucket rate limiting.

**Configuration:**
```cpp
struct RateLimitConfig {
    size_t tokens_per_second = 100;
    size_t burst_size = 1000;
    bool per_connection = false;  // Global vs per-connection
};
```

**Implementation:**
```cpp
class RateLimitFilter : public Filter {
    FilterStatus onData(Buffer& data, bool end_stream) {
        if (!token_bucket_.tryConsume(1)) {
            // Rate limit exceeded
            sendRateLimitError();
            return FilterStatus::StopIteration;
        }
        return FilterStatus::Continue;
    }
};
```

#### CircuitBreakerFilter (`filter/circuit_breaker_filter.h`)
Prevents cascading failures.

**States:**
- **CLOSED**: Normal operation
- **OPEN**: Blocking requests due to failures
- **HALF_OPEN**: Testing recovery

```cpp
class CircuitBreakerFilter : public Filter {
    FilterStatus onData(Buffer& data, bool end_stream) {
        if (circuit_state_ == State::OPEN) {
            sendServiceUnavailable();
            return FilterStatus::StopIteration;
        }
        
        // Track request for failure detection
        trackRequest();
        return FilterStatus::Continue;
    }
};
```

#### BackpressureFilter (`filter/backpressure_filter.h`)
Flow control to prevent buffer overflow.

**Watermark-Based Control:**
```cpp
class BackpressureFilter : public Filter {
    FilterStatus onData(Buffer& data, bool end_stream) {
        if (buffer_size_ > high_watermark_) {
            // Apply backpressure
            connection_->readDisable(true);
            callbacks_->onBackpressureApplied();
        }
        return FilterStatus::Continue;
    }
    
    void onBufferDrained() {
        if (buffer_size_ < low_watermark_) {
            // Release backpressure
            connection_->readDisable(false);
            callbacks_->onBackpressureReleased();
        }
    }
};
```

### Observability Filters

#### MetricsFilter (`filter/metrics_filter.h`)
Collects connection and request metrics.

**Metrics Collected:**
- Bytes sent/received
- Request count
- Latency percentiles
- Error rates

```cpp
class MetricsFilter : public Filter {
    FilterStatus onData(Buffer& data, bool end_stream) {
        metrics_.bytes_received += data.length();
        metrics_.requests_total++;
        
        // Start request timer
        request_start_time_ = std::chrono::steady_clock::now();
        
        return FilterStatus::Continue;
    }
    
    FilterStatus onWrite(Buffer& data, bool end_stream) {
        metrics_.bytes_sent += data.length();
        
        if (end_stream) {
            // Calculate request latency
            auto duration = std::chrono::steady_clock::now() - 
                          request_start_time_;
            updateLatencyHistogram(duration);
        }
        
        return FilterStatus::Continue;
    }
};
```

### Protocol-Specific Filters

#### McpJsonRpcFilter (`filter/mcp_jsonrpc_filter.h`)
Handles MCP JSON-RPC protocol.

**Features:**
- Request/Response correlation
- Notification handling
- Error response generation
- Progress tracking

```cpp
class McpJsonRpcFilter : public Filter {
    FilterStatus onData(Buffer& data, bool end_stream) {
        // Parse JSON-RPC message
        auto message = parseJsonRpc(data);
        
        if (message.isRequest()) {
            callbacks_->onRequest(message.asRequest());
        } else if (message.isNotification()) {
            callbacks_->onNotification(message.asNotification());
        }
        
        return FilterStatus::Continue;
    }
};
```

## Filter Chain Builder

### Dynamic Filter Composition

```cpp
class FilterChainBuilder {
public:
    FilterChainBuilder& withRateLimiting(const RateLimitConfig& config) {
        filters_.push_back(std::make_shared<RateLimitFilter>(config));
        return *this;
    }
    
    FilterChainBuilder& withMetrics(const MetricsConfig& config) {
        filters_.push_back(std::make_shared<MetricsFilter>(config));
        return *this;
    }
    
    FilterChainBuilder& withHttpCodec() {
        filters_.push_back(std::make_shared<HttpCodecFilter>());
        return *this;
    }
    
    std::vector<FilterPtr> build() {
        return filters_;
    }
    
private:
    std::vector<FilterPtr> filters_;
};
```

### Usage Example

```cpp
// Build filter chain for HTTP+SSE transport
auto filters = FilterChainBuilder(dispatcher, stats)
    .withCircuitBreaker(circuit_config)      // First: protect backend
    .withRateLimiting(rate_config)          // Second: limit requests
    .withMetrics(metrics_config)            // Third: collect metrics
    .withHttpCodec()                        // Fourth: HTTP protocol
    .withSseCodec()                         // Fifth: SSE protocol
    .withMcpJsonRpc(callbacks)              // Sixth: MCP protocol
    .build();
```

## Filter Manager

### FilterManagerImpl (`network/filter_impl.cc`)

Manages filter chain execution.

**Read Path Processing:**
```cpp
FilterStatus FilterManagerImpl::onRead() {
    Buffer& data = connection_.getReadBuffer();
    
    for (auto& filter : read_filters_) {
        FilterStatus status = filter->onData(data, false);
        if (status == FilterStatus::StopIteration) {
            return FilterStatus::StopIteration;
        }
    }
    
    return FilterStatus::Continue;
}
```

**Write Path Processing:**
```cpp
FilterStatus FilterManagerImpl::onWrite() {
    Buffer* buffer = connection_.currentWriteBuffer();
    bool end_stream = connection_.currentWriteEndStream();
    
    for (auto& filter : write_filters_) {
        FilterStatus status = filter->onWrite(*buffer, end_stream);
        if (status == FilterStatus::StopIteration) {
            return FilterStatus::StopIteration;
        }
    }
    
    return FilterStatus::Continue;
}
```

## Custom Filter Development

### Filter Template

```cpp
class CustomFilter : public network::NetworkFilterBase {
public:
    explicit CustomFilter(event::Dispatcher& dispatcher)
        : dispatcher_(dispatcher) {}
    
    // Process incoming data
    network::FilterStatus onData(Buffer& data, bool end_stream) override {
        // Stateless processing
        processIncomingData(data);
        
        // Continue to next filter
        return network::FilterStatus::Continue;
    }
    
    // Process outgoing data
    network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
        // Modify data in-place
        addCustomHeaders(data);
        
        return network::FilterStatus::Continue;
    }
    
    // Connection lifecycle
    network::FilterStatus onNewConnection() override {
        // Initialize filter state
        return network::FilterStatus::Continue;
    }
    
    void onConnectionClose(network::ConnectionCloseType type) override {
        // Cleanup
    }
    
private:
    event::Dispatcher& dispatcher_;
};
```

### Registration

```cpp
// Register custom filter factory
void setupFilterChain(FilterChainBuilder& builder) override {
    builder.addFilter([]() {
        return std::make_shared<CustomFilter>(dispatcher);
    });
}
```

## Best Practices

### 1. Maintain Statelessness
Never store request-specific state in filters:
```cpp
// Use connection context or external storage
class StatelessAuthFilter : public Filter {
    FilterStatus onData(Buffer& data, bool end_stream) {
        auto token = extractToken(data);
        
        // Validate token without storing state
        if (!validateToken(token)) {
            sendAuthError();
            return FilterStatus::StopIteration;
        }
        
        return FilterStatus::Continue;
    }
};
```

### 2. Efficient Buffer Operations
Minimize copies and allocations:
```cpp
// Good: In-place modification
void modifyBuffer(Buffer& buffer) {
    // Find and replace in-place
    auto data = buffer.linearize(buffer.length());
    replaceInPlace(data, pattern, replacement);
}

// Bad: Unnecessary copy
void modifyBuffer(Buffer& buffer) {
    std::string copy = buffer.toString();  // Unnecessary copy
    copy = replace(copy, pattern, replacement);
    buffer.drain(buffer.length());
    buffer.add(copy);
}
```

### 3. Early Termination
Stop processing when appropriate:
```cpp
FilterStatus onData(Buffer& data, bool end_stream) {
    if (isBlocked(data)) {
        sendBlockedResponse();
        return FilterStatus::StopIteration;  // Don't process further
    }
    
    return FilterStatus::Continue;
}
```

### 4. Thread Safety
All filter operations occur in dispatcher thread:
```cpp
class ThreadSafeFilter : public Filter {
    FilterStatus onData(Buffer& data, bool end_stream) {
        // No locking needed - single threaded execution
        counter_++;  // Safe without mutex
        
        return FilterStatus::Continue;
    }
    
private:
    size_t counter_ = 0;  // Thread-local to dispatcher
};
```

## Performance Optimization

### Zero-Copy Processing
```cpp
// Process data without copying
FilterStatus onData(Buffer& data, bool end_stream) {
    // Get direct pointer to buffer data
    size_t length = data.length();
    const char* raw_data = static_cast<const char*>(data.linearize(length));
    
    // Process in-place
    processDataInPlace(const_cast<char*>(raw_data), length);
    
    return FilterStatus::Continue;
}
```

### Lazy Evaluation
```cpp
// Only compute expensive operations when needed
FilterStatus onData(Buffer& data, bool end_stream) {
    if (shouldComputeChecksum()) {
        auto checksum = computeChecksum(data);  // Expensive
        validateChecksum(checksum);
    }
    
    return FilterStatus::Continue;
}
```

## Debugging Filters

### Filter Tracing
```cpp
class TracingFilter : public Filter {
    FilterStatus onData(Buffer& data, bool end_stream) {
        LOG(DEBUG) << "Filter: " << name_ 
                   << " Data: " << data.length() << " bytes"
                   << " EndStream: " << end_stream;
        
        auto start = std::chrono::steady_clock::now();
        auto status = processData(data, end_stream);
        auto duration = std::chrono::steady_clock::now() - start;
        
        LOG(DEBUG) << "Filter: " << name_ 
                   << " Duration: " << duration.count() << "us"
                   << " Status: " << status;
        
        return status;
    }
};
```

### Buffer Inspection
```cpp
void debugBuffer(const Buffer& buffer) {
    std::string hex_dump = buffer.toHexString();
    LOG(DEBUG) << "Buffer hex: " << hex_dump;
    
    std::string text = buffer.toString();
    LOG(DEBUG) << "Buffer text: " << text;
}
```