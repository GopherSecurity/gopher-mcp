/**
 * HTTP Codec State Machine
 * 
 * Following production patterns for protocol state management
 * Ensures proper HTTP/1.1 protocol flow and error handling
 */

#pragma once

#include "mcp/network/state_machine.h"
#include "mcp/event/event_loop.h"
#include <functional>

namespace mcp {
namespace filter {

/**
 * HTTP codec states
 * Following HTTP/1.1 connection lifecycle
 */
enum class HttpCodecState {
  // Initial state - waiting for request
  WaitingForRequest,
  
  // Request states
  ReceivingHeaders,
  ReceivingBody,
  RequestComplete,
  
  // Response states  
  SendingResponseHeaders,
  SendingResponseBody,
  ResponseComplete,
  
  // Error states
  ProtocolError,
  Closed
};

/**
 * HTTP codec events
 * Triggers for state transitions
 */
enum class HttpCodecEvent {
  // Request events
  StartHeaders,
  HeadersComplete,
  BodyData,
  MessageComplete,
  
  // Response events
  SendResponse,
  ResponseHeadersSent,
  ResponseBodySent,
  ResponseComplete,
  
  // Error events
  ParseError,
  ConnectionError,
  Reset,
  Close
};

/**
 * HTTP codec state machine configuration
 */
struct HttpCodecStateMachineConfig {
  // Timeouts
  std::chrono::milliseconds header_timeout{30000};  // 30s for headers
  std::chrono::milliseconds body_timeout{60000};    // 60s for body
  std::chrono::milliseconds idle_timeout{120000};   // 2min idle
  
  // Limits
  size_t max_header_size{8192};      // 8KB max header size
  size_t max_body_size{10485760};    // 10MB max body size
  
  // Features
  bool support_chunked{true};
  bool support_keep_alive{true};
  bool strict_mode{false};  // Strict HTTP/1.1 compliance
};

/**
 * HTTP codec state machine
 * 
 * Manages HTTP/1.1 protocol state transitions
 * Ensures proper request/response sequencing
 */
class HttpCodecStateMachine : public network::StateMachine<HttpCodecState, HttpCodecEvent> {
public:
  using StateChangeCallback = std::function<void(HttpCodecState, HttpCodecState)>;
  using ErrorCallback = std::function<void(const std::string&)>;
  
  HttpCodecStateMachine(event::Dispatcher& dispatcher,
                        const HttpCodecStateMachineConfig& config);
  
  ~HttpCodecStateMachine() override = default;
  
  // State queries
  bool canReceiveRequest() const {
    return state_ == HttpCodecState::WaitingForRequest ||
           state_ == HttpCodecState::ResponseComplete;
  }
  
  bool canSendResponse() const {
    return state_ == HttpCodecState::RequestComplete;
  }
  
  bool isReceivingBody() const {
    return state_ == HttpCodecState::ReceivingBody;
  }
  
  bool isSendingResponse() const {
    return state_ == HttpCodecState::SendingResponseHeaders ||
           state_ == HttpCodecState::SendingResponseBody;
  }
  
  bool isIdle() const {
    return state_ == HttpCodecState::WaitingForRequest;
  }
  
  bool hasError() const {
    return state_ == HttpCodecState::ProtocolError;
  }
  
  // Callbacks
  void setStateChangeCallback(StateChangeCallback cb) {
    state_change_callback_ = std::move(cb);
  }
  
  void setErrorCallback(ErrorCallback cb) {
    error_callback_ = std::move(cb);
  }
  
  // Reset for connection reuse (keep-alive)
  void resetForNextRequest();
  
protected:
  // State machine implementation
  void defineTransitions() override;
  void onStateChange(HttpCodecState old_state, HttpCodecState new_state) override;
  void onInvalidTransition(HttpCodecState state, HttpCodecEvent event) override;
  
private:
  // Configuration
  HttpCodecStateMachineConfig config_;
  
  // Callbacks
  StateChangeCallback state_change_callback_;
  ErrorCallback error_callback_;
  
  // Timers
  event::TimerPtr header_timer_;
  event::TimerPtr body_timer_;
  event::TimerPtr idle_timer_;
  
  // Timer handlers
  void onHeaderTimeout();
  void onBodyTimeout();
  void onIdleTimeout();
  
  // State tracking
  bool keep_alive_{true};
  size_t current_header_size_{0};
  size_t current_body_size_{0};
};

/**
 * HTTP codec state machine builder
 * Fluent API for configuration
 */
class HttpCodecStateMachineBuilder {
public:
  HttpCodecStateMachineBuilder& withHeaderTimeout(std::chrono::milliseconds timeout) {
    config_.header_timeout = timeout;
    return *this;
  }
  
  HttpCodecStateMachineBuilder& withBodyTimeout(std::chrono::milliseconds timeout) {
    config_.body_timeout = timeout;
    return *this;
  }
  
  HttpCodecStateMachineBuilder& withMaxHeaderSize(size_t size) {
    config_.max_header_size = size;
    return *this;
  }
  
  HttpCodecStateMachineBuilder& withMaxBodySize(size_t size) {
    config_.max_body_size = size;
    return *this;
  }
  
  HttpCodecStateMachineBuilder& withChunkedSupport(bool support) {
    config_.support_chunked = support;
    return *this;
  }
  
  HttpCodecStateMachineBuilder& withKeepAlive(bool support) {
    config_.support_keep_alive = support;
    return *this;
  }
  
  HttpCodecStateMachineBuilder& withStrictMode(bool strict) {
    config_.strict_mode = strict;
    return *this;
  }
  
  std::unique_ptr<HttpCodecStateMachine> build(event::Dispatcher& dispatcher) {
    return std::make_unique<HttpCodecStateMachine>(dispatcher, config_);
  }
  
private:
  HttpCodecStateMachineConfig config_;
};

} // namespace filter
} // namespace mcp