/**
 * SSE Codec State Machine
 * 
 * Following production patterns for Server-Sent Events protocol
 * Manages SSE stream lifecycle and error recovery
 */

#pragma once

#include "mcp/network/state_machine.h"
#include "mcp/event/event_loop.h"
#include <functional>

namespace mcp {
namespace filter {

/**
 * SSE codec states
 * Following SSE stream lifecycle
 */
enum class SseCodecState {
  // Initial state - not yet streaming
  Idle,
  
  // Stream states
  WaitingForHeaders,      // Waiting for HTTP headers
  StreamEstablished,       // Headers sent, ready for events
  SendingEvent,           // Sending an event
  WaitingForEvent,        // Waiting for next event
  
  // Keep-alive
  SendingKeepAlive,       // Sending keep-alive comment
  
  // Error and closing states
  StreamError,
  StreamClosing,
  StreamClosed
};

/**
 * SSE codec events
 * Triggers for state transitions
 */
enum class SseCodecEvent {
  // Stream lifecycle
  StartStream,
  HeadersSent,
  StreamReady,
  
  // Event handling
  SendEvent,
  EventSent,
  SendComment,
  CommentSent,
  SendRetry,
  RetrySent,
  
  // Keep-alive
  KeepAliveTimer,
  KeepAliveSent,
  
  // Error and close
  StreamError,
  ClientDisconnect,
  ServerClose,
  Reset
};

/**
 * SSE codec state machine configuration
 */
struct SseCodecStateMachineConfig {
  // Timeouts
  std::chrono::milliseconds keep_alive_interval{30000};  // 30s keep-alive
  std::chrono::milliseconds event_timeout{5000};         // 5s event send timeout
  std::chrono::milliseconds reconnect_time{3000};        // 3s reconnect hint
  
  // Limits
  size_t max_event_size{65536};     // 64KB max event size
  size_t max_queue_size{100};       // Max queued events
  
  // Features
  bool enable_keep_alive{true};
  bool enable_compression{false};   // Future: compression support
  bool strict_mode{false};          // Strict SSE spec compliance
};

/**
 * SSE codec state machine
 * 
 * Manages Server-Sent Events protocol state
 * Handles event streaming, keep-alive, and reconnection
 */
class SseCodecStateMachine : public network::StateMachine<SseCodecState, SseCodecEvent> {
public:
  using StateChangeCallback = std::function<void(SseCodecState, SseCodecState)>;
  using ErrorCallback = std::function<void(const std::string&)>;
  using KeepAliveCallback = std::function<void()>;
  
  SseCodecStateMachine(event::Dispatcher& dispatcher,
                       const SseCodecStateMachineConfig& config);
  
  ~SseCodecStateMachine() override = default;
  
  // State queries
  bool canStartStream() const {
    return state_ == SseCodecState::Idle;
  }
  
  bool canSendEvent() const {
    return state_ == SseCodecState::StreamEstablished ||
           state_ == SseCodecState::WaitingForEvent;
  }
  
  bool isStreaming() const {
    return state_ == SseCodecState::StreamEstablished ||
           state_ == SseCodecState::SendingEvent ||
           state_ == SseCodecState::WaitingForEvent ||
           state_ == SseCodecState::SendingKeepAlive;
  }
  
  bool hasError() const {
    return state_ == SseCodecState::StreamError;
  }
  
  bool isClosed() const {
    return state_ == SseCodecState::StreamClosed;
  }
  
  // Stream control
  void startStream();
  void closeStream();
  void resetStream();
  
  // Keep-alive management
  void startKeepAliveTimer();
  void stopKeepAliveTimer();
  void sendKeepAlive();
  
  // Callbacks
  void setStateChangeCallback(StateChangeCallback cb) {
    state_change_callback_ = std::move(cb);
  }
  
  void setErrorCallback(ErrorCallback cb) {
    error_callback_ = std::move(cb);
  }
  
  void setKeepAliveCallback(KeepAliveCallback cb) {
    keep_alive_callback_ = std::move(cb);
  }
  
  // Get reconnect time for retry field
  std::chrono::milliseconds getReconnectTime() const {
    return config_.reconnect_time;
  }
  
protected:
  // State machine implementation
  void defineTransitions() override;
  void onStateChange(SseCodecState old_state, SseCodecState new_state) override;
  void onInvalidTransition(SseCodecState state, SseCodecEvent event) override;
  
private:
  // Configuration
  SseCodecStateMachineConfig config_;
  
  // Callbacks
  StateChangeCallback state_change_callback_;
  ErrorCallback error_callback_;
  KeepAliveCallback keep_alive_callback_;
  
  // Timers
  event::TimerPtr keep_alive_timer_;
  event::TimerPtr event_timeout_timer_;
  
  // Timer handlers
  void onKeepAliveTimer();
  void onEventTimeout();
  
  // State tracking
  size_t queued_events_{0};
  size_t current_event_size_{0};
  bool client_connected_{true};
};

/**
 * SSE codec state machine builder
 * Fluent API for configuration
 */
class SseCodecStateMachineBuilder {
public:
  SseCodecStateMachineBuilder& withKeepAliveInterval(std::chrono::milliseconds interval) {
    config_.keep_alive_interval = interval;
    return *this;
  }
  
  SseCodecStateMachineBuilder& withEventTimeout(std::chrono::milliseconds timeout) {
    config_.event_timeout = timeout;
    return *this;
  }
  
  SseCodecStateMachineBuilder& withReconnectTime(std::chrono::milliseconds time) {
    config_.reconnect_time = time;
    return *this;
  }
  
  SseCodecStateMachineBuilder& withMaxEventSize(size_t size) {
    config_.max_event_size = size;
    return *this;
  }
  
  SseCodecStateMachineBuilder& withMaxQueueSize(size_t size) {
    config_.max_queue_size = size;
    return *this;
  }
  
  SseCodecStateMachineBuilder& withKeepAlive(bool enable) {
    config_.enable_keep_alive = enable;
    return *this;
  }
  
  SseCodecStateMachineBuilder& withStrictMode(bool strict) {
    config_.strict_mode = strict;
    return *this;
  }
  
  std::unique_ptr<SseCodecStateMachine> build(event::Dispatcher& dispatcher) {
    return std::make_unique<SseCodecStateMachine>(dispatcher, config_);
  }
  
private:
  SseCodecStateMachineConfig config_;
};

} // namespace filter
} // namespace mcp