#include "mcp/transport/http_sse_transport_socket.h"

#include <sstream>

#include "mcp/buffer.h"
#include "mcp/http/llhttp_parser.h"

namespace mcp {
namespace transport {

HttpSseTransportSocket::HttpSseTransportSocket(
    const HttpSseTransportSocketConfig& config,
    event::Dispatcher& dispatcher)
    : config_(config), dispatcher_(dispatcher) {
  // Initialize parser factory if not provided
  if (!config_.parser_factory) {
    // Use llhttp as default if available
    config_.parser_factory = std::make_shared<http::LLHttpParserFactory>();
  }
  
  // Initialize buffers
  read_buffer_ = createBuffer();
  write_buffer_ = createBuffer();
  sse_buffer_ = createBuffer();
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
  
  // Initialize parsers
  initializeParsers();
  
  // Start connection sequence
  connect_time_ = std::chrono::steady_clock::now();
  
  // Send initial HTTP handshake request
  sendHttpRequest("{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"id\":1}",
                  config_.request_endpoint_path);
  
  updateState(State::HandshakeRequest);
  
  return makeVoidResult();
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
  if (state_ == State::Closed || state_ == State::Closing) {
    return TransportIoResult{TransportIoAction::Close, 0, false};
  }
  
  // Process incoming data based on state
  size_t bytes_processed = 0;
  
  if (sse_stream_active_) {
    processSseData(buffer);
    bytes_processed = buffer.length();
  } else {
    processHttpResponse(buffer);
    bytes_processed = buffer.length();
  }
  
  bytes_received_ += bytes_processed;
  
  return TransportIoResult{TransportIoAction::StopIteration, bytes_processed, false};
}

TransportIoResult HttpSseTransportSocket::doWrite(Buffer& buffer, bool end_stream) {
  if (state_ == State::Closed || state_ == State::Closing) {
    return TransportIoResult{TransportIoAction::Close, 0, true};
  }
  
  // Queue outgoing data
  write_buffer_->move(buffer);
  
  // Process pending requests if connected
  if (state_ == State::Connected) {
    flushPendingRequests();
  }
  
  size_t bytes_written = buffer.length();
  bytes_sent_ += bytes_written;
  
  return TransportIoResult{TransportIoAction::StopIteration, bytes_written, false};
}

void HttpSseTransportSocket::onConnected() {
  if (state_ == State::HandshakeResponse || state_ == State::SseConnected) {
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
  }
  current_header_field_.clear();
  current_header_value_.clear();
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onUrl(const char* data, size_t length) {
  if (current_request_) {
    std::string url(data, length);
    current_request_->setUri(current_request_->uri() + url);
  }
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onStatus(const char* data, size_t length) {
  // Status text - parser already has status code
  if (current_response_ && response_parser_) {
    current_response_->setStatusCode(response_parser_->statusCode());
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
    // Process complete response
    if (state_ == State::HandshakeResponse) {
      // Handshake complete, establish SSE connection
      sendSseConnectRequest();
      updateState(State::SseConnecting);
    }
    responses_received_++;
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
  // Create HTTP parsers
  request_parser_ = config_.parser_factory->createParser(
      http::HttpParserType::REQUEST, this);
  response_parser_ = config_.parser_factory->createParser(
      http::HttpParserType::RESPONSE, this);
  
  // Create SSE parser
  sse_parser_ = http::createSseParser(this);
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

void HttpSseTransportSocket::processIncomingData(Buffer& buffer) {
  if (sse_stream_active_) {
    processSseData(buffer);
  } else {
    processHttpResponse(buffer);
  }
}

void HttpSseTransportSocket::processSseData(Buffer& buffer) {
  // Parse SSE events
  sse_parser_->parse(buffer);
}

void HttpSseTransportSocket::processHttpResponse(Buffer& buffer) {
  // Parse HTTP response
  std::vector<char> data(buffer.length());
  buffer.copyOut(0, buffer.length(), data.data());
  
  size_t consumed = response_parser_->execute(data.data(), data.size());
  buffer.drain(consumed);
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
  for (const auto& [name, value] : config_.headers) {
    request << name << ": " << value << "\r\n";
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
  return std::make_unique<HttpSseTransportSocket>(config_, dispatcher_);
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