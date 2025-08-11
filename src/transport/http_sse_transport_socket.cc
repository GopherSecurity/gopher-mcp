#include "mcp/transport/http_sse_transport_socket.h"

#include <sstream>
#include <iostream>

#include "mcp/buffer.h"
#include "mcp/core/result.h"  // For TransportIoResult and makeVoidSuccess
#include "mcp/http/llhttp_parser.h"

namespace mcp {
namespace transport {

HttpSseTransportSocket::HttpSseTransportSocket(
    const HttpSseTransportSocketConfig& config,
    event::Dispatcher& dispatcher,
    bool is_server_mode)
    : config_(config), dispatcher_(dispatcher), is_server_mode_(is_server_mode) {
  // Initialize parser factory if not provided
  // This ensures we have a valid factory for creating parsers
  if (!config_.parser_factory) {
    // Use llhttp as default HTTP parser
    config_.parser_factory = std::make_shared<http::LLHttpParserFactory>();
  }
  
  // Initialize buffers for data handling
  read_buffer_ = createBuffer();
  write_buffer_ = createBuffer();
  sse_buffer_ = createBuffer();
  
  // Initialize parsers immediately in constructor
  // This prevents null pointer access if doRead() is called before connect()
  // The parsers need to be ready for any incoming data
  initializeParsers();
}

HttpSseTransportSocket::~HttpSseTransportSocket() {
  if (state_ != State::Closed) {
    closeSocket(network::ConnectionEvent::LocalClose);
  }
}

void HttpSseTransportSocket::setTransportSocketCallbacks(
    network::TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

bool HttpSseTransportSocket::canFlushClose() {
  return write_buffer_->length() == 0 && pending_requests_.empty();
}

VoidResult HttpSseTransportSocket::connect(network::Socket& socket) {
  if (state_ != State::Disconnected) {
    Error err;
    err.code = -1;
    err.message = "Already connected or connecting";
    return makeVoidError(err);
  }
  
  updateState(State::Connecting);
  
  // Protocol negotiation flow:
  // 1. Reset parsers for clean state (already initialized in constructor)
  // 2. Detect HTTP version from initial handshake
  // 3. Switch parser implementation based on protocol
  
  // Reset parsers for a clean connection state
  // Parsers were already created in constructor to avoid null pointer access
  if (request_parser_) {
    request_parser_->reset();
  }
  if (response_parser_) {
    response_parser_->reset();
  }
  if (sse_parser_) {
    sse_parser_->reset();
  }
  
  // Start connection sequence
  connect_time_ = std::chrono::steady_clock::now();
  
  // Send initial HTTP handshake request
  sendHttpRequest("{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"id\":1}",
                  config_.request_endpoint_path);
  
  updateState(State::HandshakeRequest);
  
  return makeVoidSuccess();
}

void HttpSseTransportSocket::closeSocket(network::ConnectionEvent event) {
  if (state_ == State::Closed) {
    return;
  }
  
  updateState(State::Closing);
  
  // Cancel all timers
  if (keepalive_timer_) {
    keepalive_timer_->disableTimer();
    keepalive_timer_.reset();
  }
  if (reconnect_timer_) {
    reconnect_timer_->disableTimer();
    reconnect_timer_.reset();
  }
  
  // Clear pending requests
  while (!pending_requests_.empty()) {
    pending_requests_.pop();
  }
  active_requests_.clear();
  
  // Reset parsers
  if (request_parser_) {
    request_parser_->reset();
  }
  if (response_parser_) {
    response_parser_->reset();
  }
  if (sse_parser_) {
    sse_parser_->reset();
  }
  
  updateState(State::Closed);
  
  // Notify callbacks
  if (callbacks_) {
    callbacks_->raiseEvent(event);
  }
}

TransportIoResult HttpSseTransportSocket::doRead(Buffer& buffer) {
  // Read data from underlying socket through MCP networking abstraction
  // Flow: Socket readable -> ConnectionImpl::doRead -> Transport::doRead -> Parse HTTP/SSE
  // Server mode: Parse HTTP request -> Send SSE response
  // Client mode: Parse HTTP response -> Process SSE events
  
  if (state_ == State::Closed || state_ == State::Closing) {
    return TransportIoResult::close();
  }
  
  std::cerr << "[DEBUG] HttpSseTransportSocket::doRead called, is_server=" 
            << is_server_mode_ << ", state=" << static_cast<int>(state_.load()) 
            << ", buffer_len=" << buffer.length() << std::endl;
  
  // Process incoming data based on state and mode
  size_t bytes_processed = 0;
  
  if (is_server_mode_ && !sse_stream_active_) {
    // Server waiting for HTTP request
    std::cerr << "[DEBUG] Server processing HTTP request" << std::endl;
    processHttpRequest(buffer);
    bytes_processed = buffer.length();
  } else if (sse_stream_active_) {
    // Both client and server process SSE data after handshake
    processSseData(buffer);
    bytes_processed = buffer.length();
  } else {
    // Client processing HTTP response
    processHttpResponse(buffer);
    bytes_processed = buffer.length();
  }
  
  bytes_received_ += bytes_processed;
  
  return TransportIoResult::stop();
}

TransportIoResult HttpSseTransportSocket::doWrite(Buffer& buffer, bool end_stream) {
  if (state_ == State::Closed || state_ == State::Closing) {
    return TransportIoResult::close();
  }
  
  // Queue outgoing data
  write_buffer_->move(buffer);
  
  // Process pending requests if connected
  if (state_ == State::Connected) {
    flushPendingRequests();
  }
  
  size_t bytes_written = buffer.length();
  bytes_sent_ += bytes_written;
  
  return TransportIoResult::success(bytes_written);
}

void HttpSseTransportSocket::onConnected() {
  // Handle connection based on whether this is client or server mode
  // Flow: TCP connection established -> Check mode -> Client sends request / Server waits
  // Server mode: Wait for incoming HTTP request, then send SSE response
  // Client mode: Send HTTP upgrade request, wait for SSE response
  
  if (is_server_mode_) {
    // Server mode: Connection accepted, wait for HTTP request from client
    // The server transport socket is created when accepting connections
    std::cerr << "[DEBUG] HTTP+SSE Server: Connection accepted, waiting for HTTP request" << std::endl;
    updateState(State::Connected);
    
    // Server is ready to receive HTTP requests
    // The doRead() will handle incoming HTTP requests and send SSE response
    
  } else if (state_ == State::Disconnected) {
    // Client mode: TCP connected, now send HTTP upgrade request for SSE
    // This initiates the HTTP handshake to establish SSE stream
    std::cerr << "[DEBUG] HTTP+SSE Client: TCP connected, sending HTTP upgrade request" << std::endl;
    updateState(State::HandshakeRequest);
    
    // Send HTTP GET request with SSE headers
    // The server should respond with text/event-stream content type
    sendHttpUpgradeRequest();
    
  } else if (state_ == State::HandshakeResponse || state_ == State::SseConnected) {
    // Server mode or handshake complete
    updateState(State::Connected);
    
    // Start keep-alive timer
    if (config_.enable_keepalive && !keepalive_timer_) {
      keepalive_timer_ = dispatcher_.createTimer([this]() {
        sendHttpRequest("{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"id\":\"ping\"}",
                       config_.request_endpoint_path);
      });
      keepalive_timer_->enableTimer(config_.keepalive_interval);
    }
    
    // Notify connection established
    if (callbacks_) {
      callbacks_->raiseEvent(network::ConnectionEvent::Connected);
    }
  }
}

// HttpParserCallbacks implementation

http::ParserCallbackResult HttpSseTransportSocket::onMessageBegin() {
  // Reset current message
  if (processing_headers_) {
    current_response_ = http::createHttpResponse(http::HttpStatusCode::OK);
  } else {
    current_request_ = http::createHttpRequest(http::HttpMethod::GET, "/");
    accumulated_url_.clear();  // Reset URL accumulator
  }
  current_header_field_.clear();
  current_header_value_.clear();
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onUrl(const char* data, size_t length) {
  if (current_request_) {
    // Accumulate URL data
    accumulated_url_.append(data, length);
  }
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onStatus(const char* data, size_t length) {
  // Status text - parser already has status code
  if (current_response_ && response_parser_) {
    // Recreate response with correct status code
    current_response_ = http::createHttpResponse(response_parser_->statusCode());
  }
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onHeaderField(const char* data, size_t length) {
  // If we have a value, store previous header
  if (!current_header_value_.empty() && !current_header_field_.empty()) {
    if (current_response_) {
      current_response_->headers().add(current_header_field_, current_header_value_);
    } else if (current_request_) {
      current_request_->headers().add(current_header_field_, current_header_value_);
    }
    current_header_field_.clear();
    current_header_value_.clear();
  }
  current_header_field_.append(data, length);
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onHeaderValue(const char* data, size_t length) {
  current_header_value_.append(data, length);
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onHeadersComplete() {
  // Store last header
  if (!current_header_value_.empty() && !current_header_field_.empty()) {
    if (current_response_) {
      current_response_->headers().add(current_header_field_, current_header_value_);
    } else if (current_request_) {
      current_request_->headers().add(current_header_field_, current_header_value_);
    }
    current_header_field_.clear();
    current_header_value_.clear();
  }
  
  // If we have a request with accumulated URL, recreate it with the correct URL
  if (current_request_ && !accumulated_url_.empty() && request_parser_) {
    // Get the method from the parser
    auto method = request_parser_->httpMethod();
    // Store headers from old request
    auto headers_map = current_request_->headers().getMap();
    // Create new request with correct URL
    current_request_ = http::createHttpRequest(method, accumulated_url_);
    // Restore headers
    for (const auto& header : headers_map) {
      current_request_->headers().add(header.first, header.second);
    }
    accumulated_url_.clear();
  }
  
  processing_headers_ = false;
  
  // Check for SSE content type
  if (current_response_) {
    auto content_type = current_response_->headers().get("content-type");
    if (content_type.has_value() && 
        content_type.value().find("text/event-stream") != std::string::npos) {
      sse_stream_active_ = true;
      updateState(State::SseConnected);
    }
  }
  
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onBody(const char* data, size_t length) {
  if (current_response_) {
    current_response_->body().add(data, length);
  } else if (current_request_) {
    current_request_->body().add(data, length);
  }
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onMessageComplete() {
  if (current_response_) {
    // Client received response
    if (state_ == State::HandshakeResponse) {
      // Handshake complete, establish SSE connection
      sendSseConnectRequest();
      updateState(State::SseConnecting);
    }
    responses_received_++;
  } else if (current_request_ && is_server_mode_) {
    // Server received complete HTTP request
    std::cerr << "[DEBUG] HTTP+SSE Server: Received HTTP request, sending SSE response" << std::endl;
    sendSseResponse();
  }
  
  // Reset for next message
  current_response_.reset();
  current_request_.reset();
  processing_headers_ = true;
  
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onChunkHeader(size_t length) {
  // Handle chunked encoding
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onChunkComplete() {
  return http::ParserCallbackResult::Success;
}

void HttpSseTransportSocket::onError(const std::string& error) {
  failure_reason_ = "HTTP parser error: " + error;
  closeSocket(network::ConnectionEvent::RemoteClose);
}

// SseParserCallbacks implementation

void HttpSseTransportSocket::onSseEvent(const http::SseEvent& event) {
  sse_events_received_++;
  handleSseEvent(event);
}

void HttpSseTransportSocket::onSseComment(const std::string& comment) {
  // Keep-alive comments, ignore
}

void HttpSseTransportSocket::onSseError(const std::string& error) {
  failure_reason_ = "SSE parser error: " + error;
  if (config_.auto_reconnect) {
    scheduleReconnect();
  } else {
    closeSocket(network::ConnectionEvent::RemoteClose);
  }
}

// Private helper methods

void HttpSseTransportSocket::initializeParsers() {
  // Parser initialization with protocol detection support
  // Initially create HTTP/1.1 parsers, will switch to HTTP/2 if detected
  
  // Create HTTP parsers based on preferred version
  // HTTP/1.1 uses llhttp, HTTP/2 uses nghttp2
  // Parser factory abstracts the implementation details
  request_parser_ = config_.parser_factory->createParser(
      http::HttpParserType::REQUEST, this);
  response_parser_ = config_.parser_factory->createParser(
      http::HttpParserType::RESPONSE, this);
  
  // Create SSE parser (works with both HTTP/1.1 and HTTP/2)
  sse_parser_ = http::createSseParser(this);
  
  // TODO: Add ALPN negotiation for HTTP/2 detection
  // TODO: Switch parser implementation based on negotiated protocol
  // For now, using HTTP/1.1 by default
}

void HttpSseTransportSocket::sendHttpRequest(const std::string& body,
                                            const std::string& path) {
  std::string request = buildHttpRequest(
      body.empty() ? "GET" : "POST", path, body);
  
  // Add to write buffer
  write_buffer_->add(request.data(), request.length());
  
  // Track request
  PendingRequest req;
  req.id = std::to_string(next_request_id_++);
  req.body = body;
  req.sent_time = std::chrono::steady_clock::now();
  
  if (config_.request_timeout.count() > 0) {
    req.timeout_timer = dispatcher_.createTimer([this, id = req.id]() {
      handleRequestTimeout(id);
    });
    req.timeout_timer->enableTimer(config_.request_timeout);
  }
  
  active_requests_[req.id] = std::move(req);
  requests_sent_++;
  
  // Trigger write
  if (callbacks_) {
    callbacks_->flushWriteBuffer();
  }
}

void HttpSseTransportSocket::sendSseConnectRequest() {
  sendHttpRequest("", config_.sse_endpoint_path);
}

void HttpSseTransportSocket::sendSseResponse() {
  // Server sends SSE response headers to establish event stream
  // This is called when server receives an HTTP request with Accept: text/event-stream
  
  std::ostringstream response;
  response << "HTTP/1.1 200 OK\r\n";
  response << "Content-Type: text/event-stream\r\n";
  response << "Cache-Control: no-cache\r\n";
  response << "Connection: keep-alive\r\n";
  response << "Access-Control-Allow-Origin: *\r\n";
  response << "\r\n";
  
  // Send initial SSE comment to establish connection
  response << ": SSE connection established\n\n";
  
  std::string response_str = response.str();
  std::cerr << "[DEBUG] Sending SSE response:\n" << response_str << std::endl;
  
  // Write response through MCP networking abstraction layer
  // Use the transport socket's write buffer mechanism
  if (write_buffer_) {
    // Add response to write buffer
    write_buffer_->add(response_str);
    
    // Flush write buffer through callbacks
    // This ensures proper flow control and event handling
    if (callbacks_) {
      callbacks_->flushWriteBuffer();
    }
    
    // Mark SSE stream as active
    sse_stream_active_ = true;
    updateState(State::SseConnected);
    
    // Notify that connection is ready
    if (callbacks_) {
      callbacks_->raiseEvent(network::ConnectionEvent::Connected);
    }
  }
}

void HttpSseTransportSocket::sendHttpUpgradeRequest() {
  // Send initial HTTP request to establish SSE connection
  // This is called when client first connects via TCP
  
  // Parse endpoint URL to extract path
  std::string path = config_.request_endpoint_path;
  if (path.empty()) {
    path = "/mcp/v1/sse";  // Default MCP SSE endpoint
  }
  
  // Extract host from endpoint URL
  std::string host;
  if (!config_.endpoint_url.empty()) {
    // Parse from URL if not set
    size_t protocol_end = config_.endpoint_url.find("://");
    if (protocol_end != std::string::npos) {
      protocol_end += 3;
      size_t path_start = config_.endpoint_url.find('/', protocol_end);
      size_t port_start = config_.endpoint_url.find(':', protocol_end);
      
      if (port_start != std::string::npos && 
          (path_start == std::string::npos || port_start < path_start)) {
        host = config_.endpoint_url.substr(protocol_end, port_start - protocol_end);
      } else if (path_start != std::string::npos) {
        host = config_.endpoint_url.substr(protocol_end, path_start - protocol_end);
      } else {
        host = config_.endpoint_url.substr(protocol_end);
      }
    }
  }
  
  if (host.empty()) {
    host = "localhost";
  }
  
  // Build HTTP request with SSE accept header
  std::ostringstream request;
  request << "GET " << path << " HTTP/1.1\r\n";
  request << "Host: " << host << "\r\n";
  request << "Accept: text/event-stream\r\n";
  request << "Cache-Control: no-cache\r\n";
  request << "Connection: keep-alive\r\n";
  
  // Add any custom headers if needed in the future
  // TODO: Add support for custom headers in config
  
  request << "\r\n";
  
  std::string request_str = request.str();
  std::cerr << "[DEBUG] Sending HTTP upgrade request:\n" << request_str << std::endl;
  
  // Send through MCP networking abstraction layer using write buffer
  if (write_buffer_) {
    write_buffer_->add(request_str);
    
    // Flush write buffer through callbacks
    if (callbacks_) {
      callbacks_->flushWriteBuffer();
    }
  }
}

void HttpSseTransportSocket::processIncomingData(Buffer& buffer) {
  if (sse_stream_active_) {
    processSseData(buffer);
  } else {
    processHttpResponse(buffer);
  }
}

void HttpSseTransportSocket::processSseData(Buffer& buffer) {
  // Parse SSE events using zero-copy approach
  // Flow: Buffer slices -> SSE parser -> Event callbacks -> Message processing
  // Zero-copy: Parser processes buffer slices directly without copying
  
  if (!sse_parser_) {
    return;
  }
  
  // Let SSE parser process the buffer directly
  // The parser should handle buffer slices internally for zero-copy
  sse_parser_->parse(buffer);
}

void HttpSseTransportSocket::processHttpRequest(Buffer& buffer) {
  // Server-side: Parse incoming HTTP request using zero-copy approach
  // Flow: Read buffer -> Parse directly from buffer slices -> onMessageComplete -> Send SSE response
  // Zero-copy: Parse from buffer's raw slices without intermediate allocation
  
  if (!request_parser_) {
    std::cerr << "[DEBUG] Server: Request parser not initialized!" << std::endl;
    return;
  }
  
  // Zero-copy parsing: iterate through buffer slices
  // Each slice represents a contiguous memory region in the buffer
  // MCP Buffer API provides direct access to memory slices without copying
  
  // Get raw slices from buffer (up to 16 slices typical for most buffers)
  constexpr size_t kMaxSlices = 16;
  RawSlice slices[kMaxSlices];
  size_t num_slices = buffer.getRawSlices(slices, kMaxSlices);
  
  size_t total_consumed = 0;
  for (size_t i = 0; i < num_slices; ++i) {
    const auto& slice = slices[i];
    if (slice.len_ == 0) continue;
    
    std::cerr << "[DEBUG] Server parsing HTTP request slice: " 
              << std::string(static_cast<const char*>(slice.mem_), 
                           std::min(size_t(100), slice.len_)) << std::endl;
    
    // Parse directly from buffer memory without copying
    // The parser processes data in-place from the buffer's memory
    size_t consumed = request_parser_->execute(
        static_cast<const char*>(slice.mem_), slice.len_);
    total_consumed += consumed;
    
    // Stop if parser consumed less than the slice (incomplete message)
    if (consumed < slice.len_) {
      break;
    }
  }
  
  // Drain only what was consumed by the parser
  buffer.drain(total_consumed);
}

void HttpSseTransportSocket::processHttpResponse(Buffer& buffer) {
  // Client-side: Parse HTTP response using zero-copy approach
  // Flow: Read buffer -> Parse directly from buffer slices -> Check for SSE content-type
  // Zero-copy: Parse from buffer's raw slices without intermediate allocation
  
  if (!response_parser_) {
    return;
  }
  
  // Zero-copy parsing: iterate through buffer slices
  // MCP Buffer API provides direct access to memory slices without copying
  
  // Get raw slices from buffer
  constexpr size_t kMaxSlices = 16;
  RawSlice slices[kMaxSlices];
  size_t num_slices = buffer.getRawSlices(slices, kMaxSlices);
  
  size_t total_consumed = 0;
  for (size_t i = 0; i < num_slices; ++i) {
    const auto& slice = slices[i];
    if (slice.len_ == 0) continue;
    
    // Parse directly from buffer memory without copying
    // The parser processes data in-place from the buffer's memory
    size_t consumed = response_parser_->execute(
        static_cast<const char*>(slice.mem_), slice.len_);
    total_consumed += consumed;
    
    // Stop if parser consumed less than the slice
    if (consumed < slice.len_) {
      break;
    }
  }
  
  // Drain only what was consumed
  buffer.drain(total_consumed);
}

void HttpSseTransportSocket::handleSseEvent(const http::SseEvent& event) {
  // Parse JSON-RPC message from SSE event data
  read_buffer_->add(event.data.data(), event.data.length());
  
  // Pass to connection for processing
  if (callbacks_) {
    callbacks_->setTransportSocketIsReadable();
  }
}

void HttpSseTransportSocket::handleRequestTimeout(const std::string& request_id) {
  auto it = active_requests_.find(request_id);
  if (it != active_requests_.end()) {
    active_requests_.erase(it);
    
    // Add timeout error to read buffer
    std::string error_response = R"({"jsonrpc":"2.0","error":{"code":-32603,"message":"Request timeout"},"id":")" + 
                                request_id + "\"}";
    read_buffer_->add(error_response.data(), error_response.length());
    
    if (callbacks_) {
      callbacks_->setTransportSocketIsReadable();
    }
  }
}

void HttpSseTransportSocket::scheduleReconnect() {
  if (!reconnect_timer_) {
    reconnect_timer_ = dispatcher_.createTimer([this]() {
      attemptReconnect();
    });
  }
  reconnect_timer_->enableTimer(config_.reconnect_delay);
}

void HttpSseTransportSocket::attemptReconnect() {
  // Reset state and reconnect
  sse_stream_active_ = false;
  updateState(State::Connecting);
  
  // Re-establish connection
  sendHttpRequest("{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"id\":1}",
                  config_.request_endpoint_path);
  updateState(State::HandshakeRequest);
}

void HttpSseTransportSocket::updateState(State new_state) {
  state_ = new_state;
}

std::string HttpSseTransportSocket::buildHttpRequest(const std::string& method,
                                                     const std::string& path,
                                                     const std::string& body) {
  std::ostringstream request;
  
  // Request line
  request << method << " " << path << " " 
          << http::httpVersionToString(config_.preferred_version) << "\r\n";
  
  // Headers
  request << "Host: " << config_.endpoint_url << "\r\n";
  
  if (!body.empty()) {
    request << "Content-Type: application/json\r\n";
    request << "Content-Length: " << body.length() << "\r\n";
  }
  
  if (method == "GET" && path == config_.sse_endpoint_path) {
    request << "Accept: text/event-stream\r\n";
    request << "Cache-Control: no-cache\r\n";
  }
  
  // Custom headers
  for (const auto& header : config_.headers) {
    request << header.first << ": " << header.second << "\r\n";
  }
  
  // End headers
  request << "\r\n";
  
  // Body
  if (!body.empty()) {
    request << body;
  }
  
  return request.str();
}

void HttpSseTransportSocket::flushPendingRequests() {
  while (!pending_requests_.empty() && state_ == State::Connected) {
    auto req = std::move(pending_requests_.front());
    pending_requests_.pop();
    sendHttpRequest(req.body, config_.request_endpoint_path);
  }
}

// HttpSseTransportSocketFactory implementation

HttpSseTransportSocketFactory::HttpSseTransportSocketFactory(
    const HttpSseTransportSocketConfig& config,
    event::Dispatcher& dispatcher)
    : config_(config), dispatcher_(dispatcher) {}

bool HttpSseTransportSocketFactory::implementsSecureTransport() const {
  return config_.verify_ssl;
}

network::TransportSocketPtr HttpSseTransportSocketFactory::createTransportSocket(
    network::TransportSocketOptionsSharedPtr options) const {
  // Client mode transport socket
  return std::make_unique<HttpSseTransportSocket>(config_, dispatcher_, false);
}

network::TransportSocketPtr HttpSseTransportSocketFactory::createTransportSocket() const {
  // Server mode transport socket
  return std::make_unique<HttpSseTransportSocket>(config_, dispatcher_, true);
}

std::string HttpSseTransportSocketFactory::defaultServerNameIndication() const {
  // Extract hostname from endpoint URL
  size_t start = config_.endpoint_url.find("://");
  if (start != std::string::npos) {
    start += 3;
    size_t end = config_.endpoint_url.find('/', start);
    if (end != std::string::npos) {
      return config_.endpoint_url.substr(start, end - start);
    }
    return config_.endpoint_url.substr(start);
  }
  return config_.endpoint_url;
}

void HttpSseTransportSocketFactory::hashKey(
    std::vector<uint8_t>& key,
    network::TransportSocketOptionsSharedPtr options) const {
  // Hash endpoint URL
  for (char c : config_.endpoint_url) {
    key.push_back(static_cast<uint8_t>(c));
  }
}

}  // namespace transport
}  // namespace mcp