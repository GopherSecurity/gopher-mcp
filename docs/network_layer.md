# Network Layer Documentation

The Network Layer provides the foundation for all network I/O operations in the MCP C++ SDK. It abstracts platform-specific socket operations and provides a unified interface for connection management.

## Architecture

```
┌──────────────────────────────────────────────────────┐
│                   Connection                         │
│  ┌────────────────────────────────────────────────┐ │
│  │ • Socket lifecycle management                  │ │
│  │ • Filter chain processing                      │ │
│  │ • Read/Write buffer management                 │ │
│  │ • Flow control with watermarks                 │ │
│  └────────────────────────────────────────────────┘ │
├──────────────────────────────────────────────────────┤
│                    Listener                          │
│  ┌────────────────────────────────────────────────┐ │
│  │ • Accept incoming connections                  │ │
│  │ • Connection balancing across workers          │ │
│  │ • Backlog management                           │ │
│  └────────────────────────────────────────────────┘ │
├──────────────────────────────────────────────────────┤
│                Socket Interface                      │
│  ┌────────────────────────────────────────────────┐ │
│  │ • Platform abstraction (Linux/macOS/Windows)   │ │
│  │ • Socket options configuration                 │ │
│  │ • Non-blocking I/O setup                       │ │
│  └────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────┘
```

## Core Components

### Connection (`network/connection.h`)

The Connection class manages the lifecycle of a network connection and its associated filter chain.

**Key Features:**
- **State Machine**: Manages connection states (Connecting, Connected, Closing, Closed)
- **Filter Chain Integration**: Processes all data through configured filters
- **Buffer Management**: Maintains read/write buffers with watermark-based flow control
- **Thread Safety**: All operations occur in dispatcher thread context

**Connection Lifecycle:**
1. **Creation**: Connection created with socket and filter chain
2. **Initialization**: Filters initialized via `onNewConnection()`
3. **Data Processing**: Read data → Read filters → Application
4. **Write Processing**: Application → Write filters → Socket
5. **Closure**: Graceful or immediate shutdown with filter cleanup

```cpp
// Connection interface
class Connection {
public:
    // Write data through filter chain
    virtual void write(Buffer& data, bool end_stream) = 0;
    
    // Close connection
    virtual void close(ConnectionCloseType type) = 0;
    
    // Flow control
    virtual void readDisable(bool disable) = 0;
    
    // Buffer access for filters
    virtual Buffer* currentWriteBuffer() = 0;
    virtual bool currentWriteEndStream() const = 0;
};
```

### ConnectionImpl (`network/connection_impl.h`)

Production implementation of the Connection interface.

**Thread-Safe Write Processing:**
```cpp
void ConnectionImpl::write(Buffer& data, bool end_stream) {
    assert(dispatcher_.isThreadSafe());
    
    // Set current write context for filter chain
    current_write_buffer_ = &data;
    current_write_end_stream_ = end_stream;
    
    // Process through write filters - they modify data in-place
    FilterStatus status = filter_manager_.onWrite();
    
    // Clear context
    current_write_buffer_ = nullptr;
    current_write_end_stream_ = false;
    
    if (status == FilterStatus::StopIteration) {
        return;
    }
    
    // Move processed data to write buffer
    data.move(write_buffer_);
}
```

### Listener (`network/listener.h`)

The Listener accepts incoming connections and distributes them across worker threads.

**Key Features:**
- **Robust Accept Loop**: Handles accept errors gracefully
- **Connection Balancing**: Distributes connections across workers
- **Backlog Management**: Configurable connection backlog
- **Filter Chain Factory**: Creates filter chain for each connection

### TcpActiveListener (`network/server_listener_impl.h`)

Production TCP listener implementation following enterprise patterns.

**Accept Flow:**
1. Socket becomes readable → dispatcher callback
2. Accept new socket
3. Create connection with filter chain
4. Notify callbacks with new connection
5. Connection managed by owning thread

### SocketInterface (`network/socket_interface.h`)

Platform abstraction layer for socket operations.

**Key Abstractions:**
- **Socket Creation**: TCP, UDP, Unix domain sockets
- **Socket Options**: Reuse address, non-blocking, keepalive
- **Address Resolution**: IPv4/IPv6 address handling
- **Error Handling**: Platform-specific error code mapping

## Filter System

### Filter (`network/filter.h`)

Base interface for all network filters.

```cpp
class Filter {
public:
    // Read path - processes incoming data
    virtual FilterStatus onData(Buffer& data, bool end_stream) = 0;
    
    // Write path - processes outgoing data
    virtual FilterStatus onWrite(Buffer& data, bool end_stream) = 0;
    
    // Connection lifecycle
    virtual FilterStatus onNewConnection() = 0;
    virtual void onConnectionClose(ConnectionCloseType close_type) = 0;
};
```

### FilterManager

Manages the filter chain for a connection.

**Processing Flow:**
1. **Read Path**: Socket → Read Filters → Application
2. **Write Path**: Application → Write Filters → Socket
3. **Stateless Design**: Filters don't store request state
4. **In-Place Modification**: Filters modify buffers directly

## Flow Control

### Watermark-Based Backpressure

Prevents buffer overflow through high/low watermarks:

```cpp
class Connection {
    // High watermark reached - stop reading
    void onAboveHighWatermark() {
        readDisable(true);
        callbacks_->onAboveWriteBufferHighWatermark();
    }
    
    // Below low watermark - resume reading
    void onBelowLowWatermark() {
        readDisable(false);
        callbacks_->onBelowWriteBufferLowWatermark();
    }
};
```

**Configuration:**
- **High Watermark**: Default 1MB - stops reading from socket
- **Low Watermark**: Default 256KB - resumes reading
- **Buffer Limits**: Prevents unbounded memory growth

## Connection Pooling

### ConnectionPool (`mcp_application_base.h`)

Efficient connection reuse with O(1) operations.

**Pool Management:**
```cpp
class ConnectionPool {
    // Connection states in pool
    std::list<PooledConnectionPtr> ready_connections_;
    std::list<PooledConnectionPtr> busy_connections_;
    std::list<PooledConnectionPtr> connecting_connections_;
    
    // Acquire connection for stream
    ConnectionPtr acquireConnection() {
        // Try ready connections first
        // Create new if under limit
        // Return nullptr if at capacity
    }
    
    // Release stream on connection
    void releaseStream(ConnectionPtr conn) {
        // Update connection state
        // Move between lists if needed
    }
};
```

## Best Practices

### 1. Dispatcher Thread Safety
All connection operations must occur in dispatcher thread:
```cpp
void processData(Buffer& data) {
    assert(dispatcher_.isThreadSafe());
    // Safe to modify connection state
}
```

### 2. Filter Chain Composition
Build filter chains based on requirements:
```cpp
FilterChainBuilder builder(dispatcher, stats);
builder.withMetrics(config)
       .withRateLimiting(config)
       .withHttpCodec(config);
auto filters = builder.build();
```

### 3. Graceful Shutdown
Always perform graceful shutdown:
```cpp
// Flush pending writes
connection->close(ConnectionCloseType::FlushWrite);

// Immediate close only for errors
connection->close(ConnectionCloseType::NoFlush);
```

### 4. Buffer Management
Use move semantics for zero-copy:
```cpp
// Move data between buffers
source_buffer.move(dest_buffer);

// Drain specific amount
buffer.drain(bytes_to_drain);
```

## Error Handling

### Connection Errors
- **Socket Errors**: Handled via IoResult with platform-specific codes
- **Filter Errors**: Propagated through FilterStatus::StopIteration
- **Timeout Errors**: Managed by dispatcher timers

### Recovery Strategies
- **Reconnection**: Automatic retry with exponential backoff
- **Circuit Breaking**: Prevent cascading failures
- **Graceful Degradation**: Continue with reduced functionality

## Performance Considerations

### Zero-Copy Operations
- Use Buffer::move() instead of copy
- In-place filter modifications
- Direct socket writes when possible

### Memory Management
- Pre-allocated buffer pools
- Watermark-based flow control
- Lazy allocation for idle connections

### CPU Optimization
- Lock-free operations in dispatcher thread
- Batch processing for multiple connections
- Efficient epoll/kqueue usage via libevent

## Thread Model

### Single Dispatcher per Worker
```
Worker Thread 1: Dispatcher → Connections 1-100
Worker Thread 2: Dispatcher → Connections 101-200
Worker Thread 3: Dispatcher → Connections 201-300
```

### Connection Affinity
- Connection bound to single dispatcher
- No cross-thread communication needed
- Thread-local storage for performance

## Debugging

### Connection State Tracking
```cpp
// Log connection state transitions
void onStateChange(State old_state, State new_state) {
    LOG(INFO) << "Connection " << id_ 
              << " transition: " << old_state 
              << " -> " << new_state;
}
```

### Buffer Inspection
```cpp
// Examine buffer contents
std::string contents(static_cast<char*>(buffer.linearize()), 
                    buffer.length());
LOG(DEBUG) << "Buffer: " << contents;
```

### Filter Chain Tracing
Enable debug logging in filters to trace data flow through the chain.