/**
 * @file http_sse_transport_socket_refactored.cc
 * @brief Industrial-strength HTTP+SSE transport socket with state machine
 * 
 * This implementation follows best practices from:
 * - Production proxy connection management patterns
 * - SSL state machine's async operation flow
 * - Clean separation of client/server logic
 * - Event-driven architecture with dispatcher
 * 
 * Key architectural principles:
 * - All operations run in dispatcher thread (lock-free)
 * - State machine drives all protocol transitions
 * - Clear separation of concerns (parsing, state, I/O)
 * - Comprehensive error handling and recovery
 * - Observable state changes for monitoring
 */

#include "mcp/transport/http_sse_transport_socket.h"

#include <chrono>
#include <sstream>
#include <algorithm>

#include "mcp/buffer.h"
#include "mcp/core/result.h"
#include "mcp/http/llhttp_parser.h"

namespace mcp {
namespace transport {

// =============================================================================
// Constants and Configuration
// =============================================================================

namespace {

// Buffer watermarks for flow control
constexpr size_t kLowWatermark = 16384;   // 16KB
constexpr size_t kHighWatermark = 65536;  // 64KB

// Timeout configurations
constexpr auto kDefaultConnectTimeout = std::chrono::seconds(30);
constexpr auto kDefaultRequestTimeout = std::chrono::seconds(60);
constexpr auto kDefaultKeepAliveInterval = std::chrono::seconds(30);
constexpr auto kDefaultReconnectDelay = std::chrono::seconds(3);

// Reconnection backoff parameters
constexpr uint32_t kMaxReconnectAttempts = 10;
constexpr auto kReconnectBackoffMultiplier = 2;
constexpr auto kMaxReconnectDelay = std::chrono::seconds(60);

// HTTP protocol constants
constexpr const char* kHttpVersion = "HTTP/1.1";
constexpr const char* kSseContentType = "text/event-stream";
constexpr const char* kJsonRpcContentType = "application/json";

}  // namespace

// =============================================================================
// HttpSseTransportSocket Implementation
// =============================================================================

HttpSseTransportSocket::HttpSseTransportSocket(
    const HttpSseTransportSocketConfig& config,
    event::Dispatcher& dispatcher,
    bool is_server_mode)
    : config_(config),
      dispatcher_(dispatcher),
      is_server_mode_(is_server_mode) {
  
  // Initialize state machine based on mode
  HttpSseMode mode = is_server_mode ? HttpSseMode::Server : HttpSseMode::Client;
  state_machine_ = std::make_unique<HttpSseStateMachine>(mode, dispatcher);
  transition_coordinator_ = std::make_unique<HttpSseTransitionCoordinator>(*state_machine_);
  
  // Register state change listener for internal handling
  state_machine_->addStateChangeListener(
      [this](HttpSseState old_state, HttpSseState new_state) {
        onStateChanged(old_state, new_state);
      });
  
  // Configure state machine with entry/exit actions
  configureStateMachine();
  
  // Initialize parser factory if not provided
  if (!config_.parser_factory) {
    config_.parser_factory = std::make_shared<http::LLHttpParserFactory>();
  }
  
  // Initialize buffers with watermark support
  initializeBuffers();
  
  // Initialize parsers
  initializeParsers();
  
  // Configure reconnection strategy
  configureReconnectionStrategy();
  
  // Transition to initialized state
  state_machine_->transition(HttpSseState::Initialized);
}

HttpSseTransportSocket::~HttpSseTransportSocket() {
  // Ensure clean shutdown
  if (!state_machine_->isTerminalState()) {
    closeSocket(network::ConnectionEvent::LocalClose);
  }
}

// =============================================================================
// TransportSocket Interface Implementation
// =============================================================================

void HttpSseTransportSocket::setTransportSocketCallbacks(
    network::TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

std::string HttpSseTransportSocket::protocol() const {
  return "http+sse";
}

std::string HttpSseTransportSocket::failureReason() const {
  return failure_reason_;
}

bool HttpSseTransportSocket::canFlushClose() {
  // Can flush close if no pending data and not in critical state
  return write_buffer_->length() == 0 && 
         pending_requests_.empty() &&
         !isInCriticalOperation();
}

VoidResult HttpSseTransportSocket::connect(network::Socket& socket) {
  // Validate current state
  if (state_machine_->getCurrentState() != HttpSseState::Initialized) {
    Error err;
    err.code = -1;
    err.message = "Invalid state for connection: " + 
                  HttpSseStateMachine::getStateName(state_machine_->getCurrentState());
    return makeVoidError(err);
  }
  
  // Record connection start time
  connect_time_ = std::chrono::steady_clock::now();
  
  // For client mode, initiate connection sequence
  if (!is_server_mode_) {
    // Transition to connecting state
    state_machine_->transition(HttpSseState::TcpConnecting,
        [this](bool success, const std::string& error) {
          if (!success) {
            handleConnectionError("Failed to start connection: " + error);
          }
        });
  } else {
    // For server mode, wait for incoming connection
    state_machine_->transition(HttpSseState::ServerListening,
        [this](bool success, const std::string& error) {
          if (!success) {
            handleConnectionError("Failed to start listening: " + error);
          }
        });
  }
  
  return makeVoidSuccess();
}

void HttpSseTransportSocket::closeSocket(network::ConnectionEvent event) {
  // Already closed, nothing to do
  if (state_machine_->isTerminalState()) {
    return;
  }
  
  // Record close reason
  connection_close_event_ = event;
  
  // Execute graceful shutdown sequence
  transition_coordinator_->executeShutdown(
      [this, event](bool success) {
        if (callbacks_) {
          callbacks_->raiseEvent(event);
        }
      });
}

TransportIoResult HttpSseTransportSocket::doRead(Buffer& buffer) {
  // Check if we can read in current state
  if (!HttpSseStatePatterns::canReceiveData(state_machine_->getCurrentState())) {
    return {TransportIoAction::Pause, 0, false};
  }
  
  // Move data from socket buffer to our read buffer
  size_t bytes_read = buffer.length();
  if (bytes_read > 0) {
    read_buffer_->move(buffer);
    bytes_received_ += bytes_read;
    
    // Check watermarks
    if (read_buffer_->length() > kHighWatermark) {
      // Notify high watermark reached
      if (callbacks_) {
        callbacks_->onAboveWriteBufferHighWatermark();
      }
    }
    
    // Process received data based on current state
    processReceivedData();
  }
  
  // Determine next action based on state
  TransportIoAction action = determineReadAction();
  
  return {action, bytes_read, false};
}

TransportIoResult HttpSseTransportSocket::doWrite(Buffer& buffer, bool end_stream) {
  // Check if we can write in current state
  if (!HttpSseStatePatterns::canSendData(state_machine_->getCurrentState())) {
    return {TransportIoAction::Pause, 0, false};
  }
  
  // Process pending writes first
  if (write_buffer_->length() > 0) {
    buffer.move(*write_buffer_);
  }
  
  // Process queued requests if connected
  if (state_machine_->isConnected()) {
    flushPendingRequests();
  }
  
  size_t bytes_written = buffer.length();
  if (bytes_written > 0) {
    bytes_sent_ += bytes_written;
    
    // Check watermarks
    if (buffer.length() < kLowWatermark && callbacks_) {
      callbacks_->onBelowWriteBufferLowWatermark();
    }
  }
  
  // Handle end_stream if requested
  if (end_stream && buffer.length() == 0) {
    handleEndStream();
  }
  
  // Determine next action based on state
  TransportIoAction action = determineWriteAction();
  
  return {action, bytes_written, false};
}

void HttpSseTransportSocket::onConnected() {
  // TCP connection established
  state_machine_->transition(HttpSseState::TcpConnected,
      [this](bool success, const std::string& error) {
        if (success) {
          // For client, start HTTP handshake
          if (!is_server_mode_) {
            initiateHttpHandshake();
          }
        } else {
          handleConnectionError("Failed to transition to connected: " + error);
        }
      });
}

// =============================================================================
// HTTP Parser Callbacks
// =============================================================================

http::ParserCallbackResult HttpSseTransportSocket::onMessageBegin() {
  // Create new message context based on parser type
  if (is_server_mode_) {
    current_request_ = std::make_unique<http::HttpMessage>();
    current_request_->type = http::MessageType::Request;
  } else {
    current_response_ = std::make_unique<http::HttpMessage>();
    current_response_->type = http::MessageType::Response;
  }
  
  processing_headers_ = true;
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onUrl(const char* data, size_t length) {
  if (current_request_) {
    accumulated_url_.append(data, length);
  }
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onStatus(const char* data, size_t length) {
  if (current_response_) {
    std::string status_str(data, length);
    current_response_->status_code = std::stoi(status_str);
  }
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onHeaderField(const char* data, size_t length) {
  // Save previous header if we have a complete pair
  if (!current_header_field_.empty() && !current_header_value_.empty()) {
    if (current_request_) {
      current_request_->headers[current_header_field_] = current_header_value_;
    } else if (current_response_) {
      current_response_->headers[current_header_field_] = current_header_value_;
    }
    current_header_value_.clear();
  }
  
  current_header_field_.assign(data, length);
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onHeaderValue(const char* data, size_t length) {
  current_header_value_.append(data, length);
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onHeadersComplete() {
  // Save last header
  if (!current_header_field_.empty() && !current_header_value_.empty()) {
    if (current_request_) {
      current_request_->headers[current_header_field_] = current_header_value_;
    } else if (current_response_) {
      current_response_->headers[current_header_field_] = current_header_value_;
    }
  }
  
  processing_headers_ = false;
  
  // Handle based on message type
  if (current_response_) {
    handleHttpResponse();
  } else if (current_request_) {
    handleHttpRequest();
  }
  
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onBody(const char* data, size_t length) {
  if (current_request_) {
    current_request_->body.append(data, length);
  } else if (current_response_) {
    current_response_->body.append(data, length);
  }
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onMessageComplete() {
  if (current_response_) {
    // Process complete HTTP response
    processHttpResponse();
    current_response_.reset();
  } else if (current_request_) {
    // Process complete HTTP request (server mode)
    processHttpRequest();
    current_request_.reset();
  }
  
  // Clear parsing state
  current_header_field_.clear();
  current_header_value_.clear();
  accumulated_url_.clear();
  
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onChunkHeader(size_t length) {
  // Handle chunked transfer encoding if needed
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpSseTransportSocket::onChunkComplete() {
  return http::ParserCallbackResult::Success;
}

void HttpSseTransportSocket::onError(const std::string& error) {
  handleParseError("HTTP parser error: " + error);
}

// =============================================================================
// SSE Parser Callbacks
// =============================================================================

void HttpSseTransportSocket::onSseEvent(const http::SseEvent& event) {
  // Update metrics
  sse_events_received_++;
  
  // Create stream if needed
  std::string stream_id = "sse-stream-" + std::to_string(sse_events_received_);
  auto* stream = state_machine_->getStream(stream_id);
  if (!stream) {
    stream = state_machine_->createStream(stream_id);
  }
  
  // Record event
  state_machine_->onSseEventReceived(stream_id, event.data);
  
  // Transition state if needed
  if (state_machine_->getCurrentState() == HttpSseState::SseEventBuffering) {
    state_machine_->transition(HttpSseState::SseEventReceived);
  }
  
  // Deliver event to application
  if (callbacks_) {
    // Convert SSE event to application data
    auto event_buffer = createBuffer();
    event_buffer->add(event.data.c_str(), event.data.length());
    
    // Notify application of received data
    callbacks_->connection()->dispatcher().post([this, event_buffer]() {
      if (callbacks_) {
        callbacks_->connection()->onData(*event_buffer, false);
      }
    });
  }
}

void HttpSseTransportSocket::onSseComment(const std::string& comment) {
  // SSE comments are used for keep-alive
  if (state_machine_->getCurrentState() == HttpSseState::SseStreamActive) {
    state_machine_->transition(HttpSseState::SseKeepAliveReceiving,
        [this](bool success, const std::string& error) {
          if (success) {
            // Transition back to active after processing keep-alive
            state_machine_->scheduleTransition(HttpSseState::SseStreamActive);
          }
        });
  }
}

void HttpSseTransportSocket::onSseError(const std::string& error) {
  handleParseError("SSE parser error: " + error);
}

// =============================================================================
// State Machine Configuration
// =============================================================================

void HttpSseTransportSocket::configureStateMachine() {
  // Configure entry actions for key states
  configureStateEntryActions();
  
  // Configure exit actions for cleanup
  configureStateExitActions();
  
  // Add custom validators for business logic
  configureStateValidators();
  
  // Set up state timeouts
  configureStateTimeouts();
}

void HttpSseTransportSocket::configureStateEntryActions() {
  // TCP Connecting entry - start connection timer
  state_machine_->setEntryAction(HttpSseState::TcpConnecting,
      [this](HttpSseState state, std::function<void()> done) {
        startConnectTimer();
        done();
      });
  
  // HTTP Request Preparing - prepare request data
  state_machine_->setEntryAction(HttpSseState::HttpRequestPreparing,
      [this](HttpSseState state, std::function<void()> done) {
        prepareHttpRequest();
        done();
      });
  
  // SSE Stream Active - start keep-alive timer
  state_machine_->setEntryAction(HttpSseState::SseStreamActive,
      [this](HttpSseState state, std::function<void()> done) {
        startKeepAliveTimer();
        done();
      });
  
  // Reconnect Waiting - schedule reconnection
  state_machine_->setEntryAction(HttpSseState::ReconnectWaiting,
      [this](HttpSseState state, std::function<void()> done) {
        scheduleReconnectTimer();
        done();
      });
}

void HttpSseTransportSocket::configureStateExitActions() {
  // TCP Connecting exit - cancel connection timer
  state_machine_->setExitAction(HttpSseState::TcpConnecting,
      [this](HttpSseState state, std::function<void()> done) {
        cancelConnectTimer();
        done();
      });
  
  // SSE Stream Active exit - cancel keep-alive timer
  state_machine_->setExitAction(HttpSseState::SseStreamActive,
      [this](HttpSseState state, std::function<void()> done) {
        cancelKeepAliveTimer();
        done();
      });
  
  // Cleanup on leaving connected states
  state_machine_->setExitAction(HttpSseState::SseStreamActive,
      [this](HttpSseState state, std::function<void()> done) {
        cleanupActiveStreams();
        done();
      });
}

void HttpSseTransportSocket::configureStateValidators() {
  // Add validator to prevent transitions during critical operations
  state_machine_->addTransitionValidator(
      [this](HttpSseState from, HttpSseState to) -> bool {
        // Don't allow transitions while processing parser callbacks
        if (processing_headers_) {
          return false;
        }
        
        // Don't allow shutdown during active request processing
        if (to == HttpSseState::ShutdownInitiated && !pending_requests_.empty()) {
          return false;
        }
        
        return true;
      });
}

void HttpSseTransportSocket::configureStateTimeouts() {
  // Configure default timeouts for states
  // These will be set when entering the respective states
  connect_timeout_ = config_.connect_timeout;
  request_timeout_ = config_.request_timeout;
  keepalive_interval_ = config_.keepalive_interval;
}

void HttpSseTransportSocket::configureReconnectionStrategy() {
  // Set exponential backoff strategy
  state_machine_->setReconnectStrategy(
      [this](uint32_t attempt) -> std::chrono::milliseconds {
        if (attempt >= kMaxReconnectAttempts) {
          return std::chrono::milliseconds::max();  // No more retries
        }
        
        auto delay = config_.reconnect_delay;
        for (uint32_t i = 1; i < attempt; ++i) {
          delay *= kReconnectBackoffMultiplier;
          if (delay > kMaxReconnectDelay) {
            delay = kMaxReconnectDelay;
            break;
          }
        }
        
        return delay;
      });
}

// =============================================================================
// State Change Handler
// =============================================================================

void HttpSseTransportSocket::onStateChanged(HttpSseState old_state, HttpSseState new_state) {
  // Log state transition for debugging
  logStateTransition(old_state, new_state);
  
  // Handle state-specific logic
  switch (new_state) {
    case HttpSseState::TcpConnected:
      handleTcpConnected();
      break;
      
    case HttpSseState::HttpRequestSent:
      handleHttpRequestSent();
      break;
      
    case HttpSseState::SseStreamActive:
      handleSseStreamActive();
      break;
      
    case HttpSseState::Error:
      handleErrorState();
      break;
      
    case HttpSseState::Closed:
      handleClosedState();
      break;
      
    default:
      break;
  }
  
  // Notify application callbacks if needed
  notifyStateChange(old_state, new_state);
}

// =============================================================================
// Buffer Management
// =============================================================================

void HttpSseTransportSocket::initializeBuffers() {
  // Create buffers with watermark support
  read_buffer_ = createBuffer();
  write_buffer_ = createBuffer();
  sse_buffer_ = createBuffer();
  
  // Configure watermarks for flow control
  // Note: In production, these would be WatermarkBuffer instances
  // For now, we'll track watermarks manually
  read_buffer_low_watermark_ = kLowWatermark;
  read_buffer_high_watermark_ = kHighWatermark;
  write_buffer_low_watermark_ = kLowWatermark;
  write_buffer_high_watermark_ = kHighWatermark;
}

// =============================================================================
// Parser Management
// =============================================================================

void HttpSseTransportSocket::initializeParsers() {
  // Create HTTP parsers based on mode
  if (is_server_mode_) {
    // Server parses requests
    request_parser_ = config_.parser_factory->createParser(
        http::ParserType::Request, *this);
  } else {
    // Client parses responses
    response_parser_ = config_.parser_factory->createParser(
        http::ParserType::Response, *this);
  }
  
  // Create SSE parser
  sse_parser_ = std::make_unique<http::SseParser>(*this);
}

// =============================================================================
// Data Processing
// =============================================================================

void HttpSseTransportSocket::processReceivedData() {
  HttpSseState current_state = state_machine_->getCurrentState();
  
  // Route data to appropriate processor based on state
  if (HttpSseStatePatterns::isHttpResponseState(current_state)) {
    processHttpData();
  } else if (HttpSseStatePatterns::isSseStreamState(current_state)) {
    processSseData();
  } else if (is_server_mode_ && HttpSseStatePatterns::isHttpRequestState(current_state)) {
    processHttpData();
  }
}

void HttpSseTransportSocket::processHttpData() {
  if (!read_buffer_ || read_buffer_->length() == 0) {
    return;
  }
  
  // Feed data to appropriate parser
  http::HttpParser* parser = nullptr;
  if (is_server_mode_) {
    parser = request_parser_.get();
  } else {
    parser = response_parser_.get();
  }
  
  if (parser) {
    // Parse available data
    size_t consumed = parser->execute(
        static_cast<const char*>(read_buffer_->linearize(read_buffer_->length())),
        read_buffer_->length());
    
    // Remove consumed data
    if (consumed > 0) {
      read_buffer_->drain(consumed);
    }
  }
}

void HttpSseTransportSocket::processSseData() {
  if (!sse_buffer_ || sse_buffer_->length() == 0) {
    return;
  }
  
  if (sse_parser_) {
    // Parse SSE events
    size_t consumed = sse_parser_->parse(
        *sse_buffer_,
        sse_buffer_->length());
    
    // Remove consumed data
    if (consumed > 0) {
      sse_buffer_->drain(consumed);
    }
  }
}

// =============================================================================
// HTTP Protocol Handling
// =============================================================================

void HttpSseTransportSocket::initiateHttpHandshake() {
  // Prepare initial HTTP request
  state_machine_->transition(HttpSseState::HttpRequestPreparing,
      [this](bool success, const std::string& error) {
        if (success) {
          sendInitialHttpRequest();
        } else {
          handleConnectionError("Failed to prepare HTTP request: " + error);
        }
      });
}

void HttpSseTransportSocket::sendInitialHttpRequest() {
  // Build HTTP request
  std::string request = buildHttpRequest(
      config_.request_method,
      config_.request_endpoint_path,
      "{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"id\":1}");
  
  // Add to write buffer
  write_buffer_->add(request.c_str(), request.length());
  
  // Transition to sending state
  state_machine_->transition(HttpSseState::HttpRequestSending,
      [this](bool success, const std::string& error) {
        if (!success) {
          handleConnectionError("Failed to send HTTP request: " + error);
        }
      });
}

void HttpSseTransportSocket::handleHttpResponse() {
  if (!current_response_) {
    return;
  }
  
  // Check response status
  if (current_response_->status_code >= 200 && 
      current_response_->status_code < 300) {
    // Success response
    handleSuccessfulHttpResponse();
  } else {
    // Error response
    handleErrorHttpResponse();
  }
}

void HttpSseTransportSocket::handleSuccessfulHttpResponse() {
  // Check if this is an SSE upgrade response
  auto content_type = current_response_->headers.find("content-type");
  if (content_type != current_response_->headers.end() &&
      content_type->second.find(kSseContentType) != std::string::npos) {
    // Transition to SSE mode
    state_machine_->transition(HttpSseState::SseNegotiating,
        [this](bool success, const std::string& error) {
          if (success) {
            establishSseStream();
          } else {
            handleConnectionError("Failed to negotiate SSE: " + error);
          }
        });
  } else {
    // Regular HTTP response
    state_machine_->transition(HttpSseState::HttpResponseBodyReceiving);
  }
}

void HttpSseTransportSocket::handleErrorHttpResponse() {
  std::stringstream error;
  error << "HTTP error response: " << current_response_->status_code;
  handleConnectionError(error.str());
}

void HttpSseTransportSocket::handleHttpRequest() {
  if (!current_request_) {
    return;
  }
  
  // Server mode: process incoming request
  state_machine_->transition(HttpSseState::ServerRequestReceiving,
      [this](bool success, const std::string& error) {
        if (success) {
          processIncomingRequest();
        }
      });
}

void HttpSseTransportSocket::processHttpResponse() {
  // Complete HTTP response received
  responses_received_++;
  
  // Handle based on current context
  if (state_machine_->getCurrentState() == HttpSseState::HttpResponseBodyReceiving) {
    // Deliver response to application
    deliverHttpResponse();
  }
}

void HttpSseTransportSocket::processHttpRequest() {
  // Complete HTTP request received (server mode)
  requests_received_++;
  
  // Process request and prepare response
  prepareHttpResponse();
}

void HttpSseTransportSocket::processIncomingRequest() {
  // Server: handle incoming request
  // This would typically involve routing to application handlers
  state_machine_->transition(HttpSseState::ServerResponseSending);
}

void HttpSseTransportSocket::prepareHttpResponse() {
  // Server: prepare HTTP response
  // Check if client wants SSE stream
  auto accept = current_request_->headers.find("accept");
  if (accept != current_request_->headers.end() &&
      accept->second.find(kSseContentType) != std::string::npos) {
    // Prepare SSE response
    prepareSseResponse();
  } else {
    // Prepare regular HTTP response
    prepareRegularHttpResponse();
  }
}

void HttpSseTransportSocket::prepareSseResponse() {
  // Build SSE response headers
  std::ostringstream response;
  response << kHttpVersion << " 200 OK\r\n";
  response << "Content-Type: " << kSseContentType << "\r\n";
  response << "Cache-Control: no-cache\r\n";
  response << "Connection: keep-alive\r\n";
  response << "\r\n";
  
  // Send response
  write_buffer_->add(response.str().c_str(), response.str().length());
  
  // Transition to SSE pushing state
  state_machine_->transition(HttpSseState::ServerSsePushing);
}

void HttpSseTransportSocket::prepareRegularHttpResponse() {
  // Build regular HTTP response
  std::ostringstream response;
  response << kHttpVersion << " 200 OK\r\n";
  response << "Content-Type: " << kJsonRpcContentType << "\r\n";
  response << "Content-Length: 0\r\n";  // Would be actual length
  response << "\r\n";
  
  // Send response
  write_buffer_->add(response.str().c_str(), response.str().length());
}

// =============================================================================
// SSE Stream Handling
// =============================================================================

void HttpSseTransportSocket::establishSseStream() {
  // SSE stream negotiated successfully
  state_machine_->transition(HttpSseState::SseStreamActive,
      [this](bool success, const std::string& error) {
        if (success) {
          sse_stream_active_ = true;
          notifySseStreamEstablished();
        } else {
          handleConnectionError("Failed to establish SSE stream: " + error);
        }
      });
}

void HttpSseTransportSocket::handleSseStreamActive() {
  // SSE stream is now active
  // Start processing SSE events
  if (read_buffer_->length() > 0) {
    // Move any remaining data to SSE buffer
    sse_buffer_->move(*read_buffer_);
    processSseData();
  }
}

void HttpSseTransportSocket::notifySseStreamEstablished() {
  // Notify application that SSE stream is ready
  if (callbacks_) {
    callbacks_->connection()->dispatcher().post([this]() {
      // Application can now send requests
      flushPendingRequests();
    });
  }
}

// =============================================================================
// Request Management
// =============================================================================

void HttpSseTransportSocket::sendHttpRequest(const std::string& body, 
                                             const std::string& path) {
  // Create pending request
  PendingRequest request;
  request.id = std::to_string(next_request_id_++);
  request.body = body;
  request.sent_time = std::chrono::steady_clock::now();
  
  // Queue request if not connected
  if (!state_machine_->isConnected()) {
    pending_requests_.push(request);
    return;
  }
  
  // Send immediately if connected
  std::string http_request = buildHttpRequest(config_.request_method, path, body);
  write_buffer_->add(http_request.c_str(), http_request.length());
  
  // Track active request
  active_requests_[request.id] = request;
  requests_sent_++;
  
  // Start request timeout timer
  startRequestTimer(request.id);
}

void HttpSseTransportSocket::flushPendingRequests() {
  while (!pending_requests_.empty() && state_machine_->isConnected()) {
    auto request = pending_requests_.front();
    pending_requests_.pop();
    
    // Send request
    std::string http_request = buildHttpRequest(
        config_.request_method,
        config_.request_endpoint_path,
        request.body);
    
    write_buffer_->add(http_request.c_str(), http_request.length());
    
    // Track active request
    active_requests_[request.id] = request;
    requests_sent_++;
    
    // Start request timeout timer
    startRequestTimer(request.id);
  }
}

std::string HttpSseTransportSocket::buildHttpRequest(const std::string& method,
                                                     const std::string& path,
                                                     const std::string& body) {
  std::ostringstream request;
  
  // Request line
  request << method << " " << path << " " << kHttpVersion << "\r\n";
  
  // Headers
  request << "Host: " << extractHostFromUrl(config_.endpoint_url) << "\r\n";
  request << "Content-Type: " << kJsonRpcContentType << "\r\n";
  request << "Content-Length: " << body.length() << "\r\n";
  
  // Custom headers
  for (const auto& header : config_.headers) {
    request << header.first << ": " << header.second << "\r\n";
  }
  
  // End headers
  request << "\r\n";
  
  // Body
  request << body;
  
  return request.str();
}

// =============================================================================
// Timer Management
// =============================================================================

void HttpSseTransportSocket::startConnectTimer() {
  connect_timer_ = dispatcher_.createTimer([this]() {
    handleConnectTimeout();
  });
  connect_timer_->enableTimer(connect_timeout_);
}

void HttpSseTransportSocket::cancelConnectTimer() {
  if (connect_timer_) {
    connect_timer_->disableTimer();
    connect_timer_.reset();
  }
}

void HttpSseTransportSocket::startKeepAliveTimer() {
  if (!config_.enable_keepalive) {
    return;
  }
  
  keepalive_timer_ = dispatcher_.createTimer([this]() {
    sendKeepAlive();
  });
  keepalive_timer_->enableTimer(keepalive_interval_);
}

void HttpSseTransportSocket::cancelKeepAliveTimer() {
  if (keepalive_timer_) {
    keepalive_timer_->disableTimer();
    keepalive_timer_.reset();
  }
}

void HttpSseTransportSocket::startRequestTimer(const std::string& request_id) {
  auto timer = dispatcher_.createTimer([this, request_id]() {
    handleRequestTimeout(request_id);
  });
  timer->enableTimer(request_timeout_);
  
  // Store timer with request
  auto it = active_requests_.find(request_id);
  if (it != active_requests_.end()) {
    it->second.timeout_timer = timer;
  }
}

void HttpSseTransportSocket::scheduleReconnectTimer() {
  uint32_t attempt = state_machine_->getReconnectAttempt();
  auto delay = config_.reconnect_delay * (1 << std::min(attempt, 5u));
  
  reconnect_timer_ = dispatcher_.createTimer([this]() {
    attemptReconnect();
  });
  reconnect_timer_->enableTimer(delay);
}

// =============================================================================
// Error Handling
// =============================================================================

void HttpSseTransportSocket::handleConnectionError(const std::string& error) {
  failure_reason_ = error;
  
  // Transition to error state
  state_machine_->transition(HttpSseState::Error,
      [this](bool success, const std::string& transition_error) {
        if (config_.auto_reconnect && !is_server_mode_) {
          scheduleReconnect();
        }
      });
}

void HttpSseTransportSocket::handleParseError(const std::string& error) {
  failure_reason_ = error;
  
  // Parsing errors are usually fatal
  state_machine_->forceTransition(HttpSseState::Error);
}

void HttpSseTransportSocket::handleConnectTimeout() {
  handleConnectionError("Connection timeout");
}

void HttpSseTransportSocket::handleRequestTimeout(const std::string& request_id) {
  // Remove timed out request
  active_requests_.erase(request_id);
  
  // Log timeout
  logRequestTimeout(request_id);
}

void HttpSseTransportSocket::handleErrorState() {
  // Clean up resources
  cleanupActiveStreams();
  
  // Clear pending requests
  while (!pending_requests_.empty()) {
    pending_requests_.pop();
  }
  
  // Notify application
  if (callbacks_) {
    callbacks_->raiseEvent(network::ConnectionEvent::RemoteClose);
  }
}

void HttpSseTransportSocket::handleClosedState() {
  // Final cleanup
  cleanupActiveStreams();
  
  // Clear all buffers
  read_buffer_->drain(read_buffer_->length());
  write_buffer_->drain(write_buffer_->length());
  sse_buffer_->drain(sse_buffer_->length());
}

// =============================================================================
// Reconnection Logic
// =============================================================================

void HttpSseTransportSocket::scheduleReconnect() {
  if (!config_.auto_reconnect || is_server_mode_) {
    return;
  }
  
  state_machine_->scheduleReconnect();
}

void HttpSseTransportSocket::attemptReconnect() {
  // Reset state for reconnection
  failure_reason_.clear();
  
  // Transition to reconnecting
  state_machine_->transition(HttpSseState::ReconnectAttempting,
      [this](bool success, const std::string& error) {
        if (success) {
          // Start connection sequence again
          state_machine_->transition(HttpSseState::TcpConnecting);
        }
      });
}

// =============================================================================
// Helper Methods
// =============================================================================

bool HttpSseTransportSocket::isInCriticalOperation() const {
  HttpSseState state = state_machine_->getCurrentState();
  return state == HttpSseState::HttpRequestSending ||
         state == HttpSseState::HttpResponseHeadersReceiving ||
         state == HttpSseState::SseNegotiating;
}

TransportIoAction HttpSseTransportSocket::determineReadAction() const {
  HttpSseState state = state_machine_->getCurrentState();
  
  if (state_machine_->isTerminalState()) {
    return TransportIoAction::Close;
  }
  
  if (!HttpSseStatePatterns::canReceiveData(state)) {
    return TransportIoAction::Pause;
  }
  
  return TransportIoAction::Continue;
}

TransportIoAction HttpSseTransportSocket::determineWriteAction() const {
  HttpSseState state = state_machine_->getCurrentState();
  
  if (state_machine_->isTerminalState()) {
    return TransportIoAction::Close;
  }
  
  if (!HttpSseStatePatterns::canSendData(state)) {
    return TransportIoAction::Pause;
  }
  
  if (write_buffer_->length() > 0 || !pending_requests_.empty()) {
    return TransportIoAction::Continue;
  }
  
  return TransportIoAction::Pause;
}

void HttpSseTransportSocket::handleEndStream() {
  // End stream requested
  if (state_machine_->isConnected()) {
    // Initiate graceful shutdown
    closeSocket(network::ConnectionEvent::LocalClose);
  }
}

void HttpSseTransportSocket::handleTcpConnected() {
  // TCP connection established
  // For client, continue with HTTP handshake
  // For server, wait for incoming request
}

void HttpSseTransportSocket::handleHttpRequestSent() {
  // HTTP request sent successfully
  // Transition to waiting for response
  state_machine_->transition(HttpSseState::HttpResponseWaiting);
}

void HttpSseTransportSocket::sendKeepAlive() {
  if (sse_stream_active_) {
    // Send SSE comment for keep-alive
    std::string keepalive = ": keep-alive\n\n";
    write_buffer_->add(keepalive.c_str(), keepalive.length());
  }
  
  // Reschedule timer
  if (keepalive_timer_) {
    keepalive_timer_->enableTimer(keepalive_interval_);
  }
}

void HttpSseTransportSocket::deliverHttpResponse() {
  if (!current_response_ || !callbacks_) {
    return;
  }
  
  // Convert response to application format
  auto response_buffer = createBuffer();
  response_buffer->add(current_response_->body.c_str(), 
                       current_response_->body.length());
  
  // Deliver to application
  callbacks_->connection()->dispatcher().post([this, response_buffer]() {
    if (callbacks_) {
      callbacks_->connection()->onData(*response_buffer, false);
    }
  });
}

void HttpSseTransportSocket::cleanupActiveStreams() {
  // Mark all active streams as zombie
  auto stats = state_machine_->getStreamStats();
  
  // Cleanup zombie streams
  state_machine_->cleanupZombieStreams();
}

void HttpSseTransportSocket::notifyStateChange(HttpSseState old_state, 
                                               HttpSseState new_state) {
  // Notify application of significant state changes
  if (HttpSseStatePatterns::isConnectedState(new_state) && 
      !HttpSseStatePatterns::isConnectedState(old_state)) {
    // Became connected
    if (callbacks_) {
      callbacks_->raiseEvent(network::ConnectionEvent::Connected);
    }
  } else if (!HttpSseStatePatterns::isConnectedState(new_state) && 
             HttpSseStatePatterns::isConnectedState(old_state)) {
    // Lost connection
    if (callbacks_) {
      callbacks_->raiseEvent(network::ConnectionEvent::RemoteClose);
    }
  }
}

std::string HttpSseTransportSocket::extractHostFromUrl(const std::string& url) {
  // Simple host extraction (in production, use proper URL parser)
  size_t start = url.find("://");
  if (start == std::string::npos) {
    return "";
  }
  start += 3;
  
  size_t end = url.find('/', start);
  if (end == std::string::npos) {
    end = url.length();
  }
  
  return url.substr(start, end - start);
}

void HttpSseTransportSocket::logStateTransition(HttpSseState old_state, 
                                                HttpSseState new_state) {
  // Log for debugging (in production, use proper logging)
  std::stringstream log;
  log << "[HTTP+SSE] State transition: "
      << HttpSseStateMachine::getStateName(old_state)
      << " -> "
      << HttpSseStateMachine::getStateName(new_state);
  
  // Would write to log system
}

void HttpSseTransportSocket::logRequestTimeout(const std::string& request_id) {
  // Log timeout (in production, use proper logging)
  std::stringstream log;
  log << "[HTTP+SSE] Request timeout: " << request_id;
  
  // Would write to log system
}

// =============================================================================
// Factory Implementation
// =============================================================================

HttpSseTransportSocketFactory::HttpSseTransportSocketFactory(
    const HttpSseTransportSocketConfig& config,
    event::Dispatcher& dispatcher)
    : config_(config), dispatcher_(dispatcher) {}

bool HttpSseTransportSocketFactory::implementsSecureTransport() const {
  return config_.use_ssl;
}

network::TransportSocketPtr HttpSseTransportSocketFactory::createTransportSocket(
    network::TransportSocketOptionsSharedPtr options) const {
  // Client mode
  return std::make_unique<HttpSseTransportSocket>(config_, dispatcher_, false);
}

network::TransportSocketPtr HttpSseTransportSocketFactory::createTransportSocket() const {
  // Server mode
  return std::make_unique<HttpSseTransportSocket>(config_, dispatcher_, true);
}

bool HttpSseTransportSocketFactory::supportsAlpn() const {
  return config_.alpn_protocols.has_value();
}

std::string HttpSseTransportSocketFactory::defaultServerNameIndication() const {
  if (config_.sni_hostname.has_value()) {
    return config_.sni_hostname.value();
  }
  return extractHostFromUrl(config_.endpoint_url);
}

void HttpSseTransportSocketFactory::hashKey(
    std::vector<uint8_t>& key,
    network::TransportSocketOptionsSharedPtr options) const {
  // Hash configuration for connection pooling
  std::string hash_input = config_.endpoint_url + 
                           std::to_string(config_.use_ssl) +
                           std::to_string(config_.verify_ssl);
  
  key.insert(key.end(), hash_input.begin(), hash_input.end());
}

}  // namespace transport
}  // namespace mcp