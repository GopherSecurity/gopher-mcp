# Transport Layer Documentation

The Transport Layer provides multiple transport implementations for the MCP protocol, supporting various communication patterns and security requirements.

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                  Transport Layer                         │
├──────────────────────────────────────────────────────────┤
│  ┌────────────┐  ┌──────────┐  ┌────────────────────┐    │
│  │   Stdio    │  │   TCP    │  │    HTTP(s)+SSE     │    │
│  │ Transport  │  │Transport │  │    Transport       │    │
│  └────────────┘  └──────────┘  └────────────────────┘    │
├──────────────────────────────────────────────────────────┤
│  ┌────────────┐  ┌──────────┐  ┌────────────────────┐    │
│  │ WebSocket  │  │Redis Msg │  │   Peer-to-Peer     │    │
│  │ Transport  │  │Transport │  │      Tunnel        │    │
│  └────────────┘  └──────────┘  └────────────────────┘    │
└──────────────────────────────────────────────────────────┘
```

## Transport Types

### Stdio Transport (`transport/stdio_transport_socket.h`)

Communicates via standard input/output pipes, ideal for subprocess communication.

**Architecture:**
```
┌─────────────┐     stdin       ┌─────────────┐
│   Client    │ ──────────────> │   Server    │
│   Process   │                 │   Process   │
│             │ <────────────── │             │
└─────────────┘     stdout      └─────────────┘
```

**Key Features:**
- Zero network overhead
- Process isolation
- Automatic cleanup on process termination
- Bidirectional communication

**Implementation:**
```cpp
class StdioTransportSocket : public TransportSocket {
public:
    IoResult doRead(Buffer& buffer) override {
        // Read from stdin pipe
        uint8_t data[16384];
        ssize_t bytes_read = ::read(stdin_fd_, data, sizeof(data));
        
        if (bytes_read > 0) {
            buffer.add(data, bytes_read);
            return IoResult{IoResult::Action::Continue, bytes_read};
        }
        
        return IoResult{IoResult::Action::Error, 0, errno};
    }
    
    IoResult doWrite(Buffer& buffer, bool end_stream) override {
        // Write to stdout pipe
        size_t length = buffer.length();
        const void* data = buffer.linearize(length);
        
        ssize_t bytes_written = ::write(stdout_fd_, data, length);
        if (bytes_written > 0) {
            buffer.drain(bytes_written);
            return IoResult{IoResult::Action::Continue, bytes_written};
        }
        
        return IoResult{IoResult::Action::Error, 0, errno};
    }
};
```

**Usage:**
```cpp
// Create stdio transport
auto transport = std::make_unique<StdioTransportSocket>(
    stdin_handle,
    stdout_handle,
    dispatcher
);

// Use with connection
auto connection = std::make_unique<ConnectionImpl>(
    std::move(transport),
    dispatcher,
    callbacks
);
```

### TCP Transport (`transport/tcp_transport_socket.h`)

Raw TCP socket transport for network communication.

**State Machine:**
```
┌──────────┐  connect()  ┌────────────┐  connected ┌───────────┐
│   Init   │ ──────────> │ Connecting │ ─────────> │ Connected │
└──────────┘             └────────────┘            └───────────┘
                               │                          │
                               │ error                    │ close
                               ↓                          ↓
                         ┌──────────┐              ┌──────────┐
                         │  Error   │              │  Closed  │
                         └──────────┘              └──────────┘
```

**Key Features:**
- Non-blocking I/O
- Nagle's algorithm control
- Keep-alive support
- Socket option configuration

**Implementation:**
```cpp
class TcpTransportSocket : public TransportSocket {
    enum class State {
        Init,
        Connecting,
        Connected,
        Closed
    };
    
    IoResult doConnect(const Address& address) {
        // Create socket
        fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        
        // Set socket options
        setReuseAddr(true);
        setNoDelay(true);  // Disable Nagle's algorithm
        
        // Initiate connection
        int result = ::connect(fd_, address.sockAddr(), address.len());
        
        if (result == 0) {
            state_ = State::Connected;
            return IoResult::success();
        }
        
        if (errno == EINPROGRESS) {
            state_ = State::Connecting;
            return IoResult::Again();
        }
        
        return IoResult::error(errno);
    }
};
```

### SSL/TLS Transport (`transport/ssl_transport_socket.h`)

Secure socket transport using OpenSSL.

**SSL State Machine:**
```
┌──────────┐  handshake  ┌────────────┐  complete  ┌───────────┐
│   Init   │ ──────────> │ Handshaking│ ─────────> │   Ready   │
└──────────┘             └────────────┘            └───────────┘
                               │                          │
                          want_read/                  shutdown
                          want_write                      │
                               │                          ↓
                               └─────────┐        ┌──────────┐
                                         │        │ Shutting │
                                         └──────> │   Down   │
                                                  └──────────┘
```

**Key Features:**
- TLS 1.2/1.3 support
- Certificate verification
- SNI (Server Name Indication)
- Session resumption
- ALPN negotiation

**Implementation:**
```cpp
class SslTransportSocket : public TransportSocket {
    IoResult doHandshake() {
        ERR_clear_error();
        int result = SSL_do_handshake(ssl_.get());
        
        if (result == 1) {
            // Handshake complete
            state_ = State::Ready;
            verifyPeerCertificate();
            return IoResult::success();
        }
        
        int ssl_error = SSL_get_error(ssl_.get(), result);
        switch (ssl_error) {
            case SSL_ERROR_WANT_READ:
                return IoResult::Again();
            case SSL_ERROR_WANT_WRITE:
                return IoResult::Again();
            default:
                return handleSslError(ssl_error);
        }
    }
    
    IoResult doRead(Buffer& buffer) override {
        uint8_t data[16384];
        int bytes_read = SSL_read(ssl_.get(), data, sizeof(data));
        
        if (bytes_read > 0) {
            buffer.add(data, bytes_read);
            return IoResult::success(bytes_read);
        }
        
        return handleSslError(SSL_get_error(ssl_.get(), bytes_read));
    }
};
```

### HTTP+SSE Transport (`transport/http_sse_transport_socket.h`)

HTTP with Server-Sent Events for bidirectional communication over HTTP/1.1.

**Communication Pattern:**
```
Client                          Server
  │                               │
  ├──── POST /rpc ───────────────>│  JSON-RPC Request
  │<──── 200 OK ──────────────────┤  JSON-RPC Response
  │                               │
  ├──── GET /events ─────────────>│  SSE Connection
  │<──── 200 OK ──────────────────┤
  │<──── event: notification ─────┤  Server Push
  │<──── data: {...} ─────────────┤
  │                               │
```

**Key Features:**
- Firewall-friendly (uses standard HTTP)
- Automatic reconnection
- Event ID tracking
- Heartbeat support

**Implementation:**
```cpp
class HttpSseTransportSocket : public TransportSocket {
    // Handle RPC endpoint
    void handleRpcRequest(const HttpRequest& request) {
        // Extract JSON-RPC from request body
        auto jsonrpc = parseJsonRpc(request.body);
        
        // Process and send response
        auto response = processJsonRpc(jsonrpc);
        sendHttpResponse(200, "application/json", response);
    }
    
    // Handle SSE endpoint
    void handleSseConnection(const HttpRequest& request) {
        // Send SSE headers
        sendHttpResponse(200, "text/event-stream", "", false);
        
        // Keep connection alive
        sse_connection_active_ = true;
        
        // Send events as they occur
        scheduleHeartbeat();
    }
    
    // Send SSE event
    void sendSseEvent(const std::string& event, 
                      const std::string& data) {
        std::ostringstream sse;
        sse << "event: " << event << "\n";
        sse << "data: " << data << "\n\n";
        
        Buffer buffer;
        buffer.add(sse.str());
        doWrite(buffer, false);
    }
};
```

### HTTPS+SSE Transport

Combines SSL/TLS with HTTP+SSE for secure communication.

**Layer Composition:**
```
┌──────────────────────────────┐
│     HTTP+SSE Protocol        │
├──────────────────────────────┤
│      SSL/TLS Encryption      │
├──────────────────────────────┤
│       TCP Transport          │
└──────────────────────────────┘
```

## Transport Selection

### Automatic Negotiation

```cpp
TransportType negotiateTransport(const std::string& uri) {
    if (uri.starts_with("stdio://")) {
        return TransportType::Stdio;
    }
    if (uri.starts_with("tcp://")) {
        return TransportType::Tcp;
    }
    if (uri.starts_with("https://")) {
        return TransportType::HttpsSse;
    }
    if (uri.starts_with("http://")) {
        return TransportType::HttpSse;
    }
    if (uri.starts_with("wss://")) {
        return TransportType::WebSocketSecure;
    }
    if (uri.starts_with("ws://")) {
        return TransportType::WebSocket;
    }
    
    // Default to stdio for local communication
    return TransportType::Stdio;
}
```

### Transport Capabilities

```cpp
struct TransportCapabilities {
    bool supports_streaming = false;
    bool supports_multiplexing = false;
    bool supports_compression = false;
    bool supports_encryption = false;
    size_t max_message_size = 0;
};

TransportCapabilities getCapabilities(TransportType type) {
    switch (type) {
        case TransportType::Stdio:
            return {
                .supports_streaming = true,
                .supports_multiplexing = false,
                .supports_compression = false,
                .supports_encryption = false,
                .max_message_size = SIZE_MAX
            };
            
        case TransportType::HttpSse:
            return {
                .supports_streaming = true,
                .supports_multiplexing = false,
                .supports_compression = true,
                .supports_encryption = false,
                .max_message_size = 10 * 1024 * 1024  // 10MB
            };
            
        case TransportType::HttpsSse:
            return {
                .supports_streaming = true,
                .supports_multiplexing = false,
                .supports_compression = true,
                .supports_encryption = true,
                .max_message_size = 10 * 1024 * 1024  // 10MB
            };
    }
}
```

## Transport Socket Interface

### Base Interface (`network/transport_socket.h`)

```cpp
class TransportSocket {
public:
    // I/O operations
    virtual IoResult doRead(Buffer& buffer) = 0;
    virtual IoResult doWrite(Buffer& buffer, bool end_stream) = 0;
    virtual IoResult doClose() = 0;
    
    // Connection management
    virtual void onConnected() {}
    virtual void onError(const Error& error) {}
    
    // TLS operations (optional)
    virtual IoResult doHandshake() {
        return IoResult::success();
    }
    
    // Get transport info
    virtual std::string protocol() const = 0;
    virtual bool isSecure() const { return false; }
};
```

### IoResult (`io_result.h`)

```cpp
struct IoResult {
    enum class Action {
        Continue,  // Operation completed, continue processing
        Again,     // Would block, try again later
        Error,     // Error occurred
        Close      // Connection should be closed
    };
    
    Action action;
    size_t bytes_processed = 0;
    int error_code = 0;
    
    static IoResult success(size_t bytes = 0) {
        return {Action::Continue, bytes, 0};
    }
    
    static IoResult wouldBlock() {
        return {Action::Again, 0, EAGAIN};
    }
    
    static IoResult error(int err) {
        return {Action::Error, 0, err};
    }
};
```

## Flow Control

### Backpressure Handling

```cpp
class TransportSocketWithBackpressure : public TransportSocket {
    IoResult doWrite(Buffer& buffer, bool end_stream) override {
        // Check if write would block
        if (write_buffer_.length() > high_watermark_) {
            // Apply backpressure
            callbacks_->onAboveWriteBufferHighWatermark();
            return IoResult::wouldBlock();
        }
        
        // Attempt write
        auto result = performWrite(buffer);
        
        // Check if below low watermark
        if (write_buffer_.length() < low_watermark_) {
            callbacks_->onBelowWriteBufferLowWatermark();
        }
        
        return result;
    }
    
private:
    size_t high_watermark_ = 1024 * 1024;  // 1MB
    size_t low_watermark_ = 256 * 1024;    // 256KB
};
```

## Error Handling

### Transport Errors

```cpp
enum class TransportError {
    ConnectionRefused,
    ConnectionReset,
    Timeout,
    SslHandshakeFailed,
    CertificateVerificationFailed,
    ProtocolError,
    BufferOverflow
};

class TransportException : public std::runtime_error {
public:
    TransportException(TransportError error, const std::string& message)
        : std::runtime_error(message), error_(error) {}
    
    TransportError error() const { return error_; }
    
private:
    TransportError error_;
};
```

### Error Recovery

```cpp
class ResilientTransport : public TransportSocket {
    IoResult handleError(IoResult result) {
        if (result.action == IoResult::Action::Error) {
            error_count_++;
            
            if (shouldReconnect()) {
                scheduleReconnect();
                return IoResult::wouldBlock();
            }
            
            if (shouldFailover()) {
                switchToBackupTransport();
                return IoResult::wouldBlock();
            }
        }
        
        return result;
    }
    
    bool shouldReconnect() const {
        return error_count_ < max_retries_ && 
               reconnect_enabled_;
    }
    
    void scheduleReconnect() {
        dispatcher_.createTimer(
            retry_delay_,
            [this]() { attemptReconnect(); }
        );
        
        // Exponential backoff
        retry_delay_ *= 2;
    }
};
```

## Performance Optimization

### Zero-Copy I/O

```cpp
class ZeroCopyTransport : public TransportSocket {
    IoResult doWrite(Buffer& buffer, bool end_stream) override {
        // Use sendmsg with MSG_ZEROCOPY if available
        struct msghdr msg = {};
        struct iovec iov[buffer.getRawSlices().size()];
        
        int i = 0;
        for (const auto& slice : buffer.getRawSlices()) {
            iov[i].iov_base = slice.mem_;
            iov[i].iov_len = slice.len_;
            i++;
        }
        
        msg.msg_iov = iov;
        msg.msg_iovlen = i;
        
        ssize_t sent = sendmsg(fd_, &msg, MSG_ZEROCOPY);
        if (sent > 0) {
            buffer.drain(sent);
            return IoResult::success(sent);
        }
        
        return IoResult::error(errno);
    }
};
```

### Buffer Pooling

```cpp
class PooledTransport : public TransportSocket {
    IoResult doRead(Buffer& buffer) override {
        // Get buffer from pool
        auto pooled_buffer = buffer_pool_.acquire();
        
        ssize_t bytes = ::read(fd_, 
                              pooled_buffer->writableData(),
                              pooled_buffer->remainingCapacity());
        
        if (bytes > 0) {
            pooled_buffer->commit(bytes);
            buffer.move(*pooled_buffer);
            buffer_pool_.release(std::move(pooled_buffer));
            return IoResult::success(bytes);
        }
        
        buffer_pool_.release(std::move(pooled_buffer));
        return IoResult::error(errno);
    }
    
private:
    BufferPool buffer_pool_;
};
```

## Testing Transports

### Mock Transport

```cpp
class MockTransport : public TransportSocket {
public:
    void injectData(const std::string& data) {
        read_buffer_.add(data);
        callbacks_->onReadReady();
    }
    
    void expectWrite(const std::string& expected) {
        expected_writes_.push_back(expected);
    }
    
    IoResult doRead(Buffer& buffer) override {
        buffer.move(read_buffer_);
        return IoResult::success(buffer.length());
    }
    
    IoResult doWrite(Buffer& buffer, bool end_stream) override {
        std::string actual(
            static_cast<char*>(buffer.linearize(buffer.length())),
            buffer.length()
        );
        
        EXPECT_FALSE(expected_writes_.empty());
        EXPECT_EQ(expected_writes_.front(), actual);
        expected_writes_.pop_front();
        
        buffer.drain(buffer.length());
        return IoResult::success();
    }
    
private:
    Buffer read_buffer_;
    std::deque<std::string> expected_writes_;
};
```

## Best Practices

### 1. Handle Partial I/O
```cpp
IoResult doWrite(Buffer& buffer, bool end_stream) {
    while (buffer.length() > 0) {
        ssize_t sent = ::write(fd_, 
                               buffer.linearize(buffer.length()),
                               buffer.length());
        
        if (sent > 0) {
            buffer.drain(sent);
            continue;
        }
        
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return IoResult::wouldBlock();
        }
        
        return IoResult::error(errno);
    }
    
    return IoResult::success();
}
```

### 2. Proper Resource Cleanup
```cpp
~TransportSocket() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
    
    if (ssl_) {
        SSL_shutdown(ssl_.get());
    }
}
```

### 3. Connection State Management
```cpp
class StatefulTransport : public TransportSocket {
    bool canRead() const {
        return state_ == State::Connected && !read_disabled_;
    }
    
    bool canWrite() const {
        return state_ == State::Connected && 
               write_buffer_.length() < high_watermark_;
    }
};
```