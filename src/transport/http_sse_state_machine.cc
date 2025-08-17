/**
 * @file http_sse_state_machine.cc
 * @brief Implementation of HTTP+SSE state machine
 * 
 * Provides comprehensive state management for HTTP+SSE transport with:
 * - Lock-free operation in dispatcher thread
 * - Automatic reconnection with exponential backoff
 * - Stream lifecycle management with zombie pattern
 * - Observable state transitions
 * - Graceful error handling and recovery
 */

#include "mcp/transport/http_sse_state_machine.h"

#include <algorithm>
#include <sstream>

namespace mcp {
namespace transport {

// =============================================================================
// HttpSseStateMachine Implementation
// =============================================================================

HttpSseStateMachine::HttpSseStateMachine(HttpSseMode mode, event::Dispatcher& dispatcher)
    : mode_(mode), 
      dispatcher_(dispatcher),
      current_state_(HttpSseState::Uninitialized),
      state_entry_time_(std::chrono::steady_clock::now()) {
  
  // Initialize valid transitions based on mode
  if (mode_ == HttpSseMode::Client) {
    initializeClientTransitions();
  } else {
    initializeServerTransitions();
  }
  
  // Set default reconnection strategy (exponential backoff)
  reconnect_strategy_ = [](uint32_t attempt) -> std::chrono::milliseconds {
    // Exponential backoff: 1s, 2s, 4s, 8s, 16s, 32s, then cap at 60s
    uint32_t delay_seconds = std::min(1u << std::min(attempt, 5u), 60u);
    return std::chrono::milliseconds(delay_seconds * 1000);
  };
  
  // Record initial state
  recordStateTransition(current_state_);
}

HttpSseStateMachine::~HttpSseStateMachine() {
  // Cancel any pending timers
  if (state_timeout_timer_) {
    state_timeout_timer_->disableTimer();
  }
  if (reconnect_timer_) {
    reconnect_timer_->disableTimer();
  }
  
  // Clean up all streams
  active_streams_.clear();
}

bool HttpSseStateMachine::canTransition(HttpSseState from, HttpSseState to) const {
  assertInDispatcherThread();
  
  // Check basic transition map
  if (!isValidTransition(from, to)) {
    return false;
  }
  
  // Apply custom validators
  for (const auto& validator : custom_validators_) {
    if (!validator(from, to)) {
      return false;
    }
  }
  
  return true;
}

void HttpSseStateMachine::transition(HttpSseState new_state,
                                    TransitionCompleteCallback callback) {
  assertInDispatcherThread();
  
  // Prevent reentrancy during async operations
  if (transition_in_progress_) {
    if (callback) {
      callback(false, "Transition already in progress");
    }
    return;
  }
  
  HttpSseState old_state = current_state_;
  
  // Validate transition
  if (!canTransition(old_state, new_state)) {
    std::stringstream ss;
    ss << "Invalid transition from " << getStateName(old_state) 
       << " to " << getStateName(new_state) 
       << " in " << (mode_ == HttpSseMode::Client ? "client" : "server") << " mode";
    
    failed_transitions_++;
    
    if (callback) {
      callback(false, ss.str());
    }
    return;
  }
  
  // Set transition in progress flag
  transition_in_progress_ = true;
  total_transitions_++;
  
  // Execute async exit action for current state
  executeExitAction(old_state, [this, old_state, new_state, callback]() {
    // After exit action completes, update state
    current_state_ = new_state;
    state_entry_time_ = std::chrono::steady_clock::now();
    recordStateTransition(new_state);
    
    // Cancel any existing state timeout
    cancelStateTimeout();
    
    // Execute async entry action for new state
    executeEntryAction(new_state, [this, old_state, new_state, callback]() {
      // After entry action completes, notify observers
      notifyStateChange(old_state, new_state);
      
      // Clear transition in progress flag
      transition_in_progress_ = false;
      
      // Invoke completion callback
      if (callback) {
        callback(true, "");
      }
    });
  });
}

void HttpSseStateMachine::forceTransition(HttpSseState new_state) {
  assertInDispatcherThread();
  
  // Force transition bypasses validation but still uses async flow
  transition_in_progress_ = true;
  HttpSseState old_state = current_state_;
  
  // Execute async exit action
  executeExitAction(old_state, [this, old_state, new_state]() {
    // Force state change
    current_state_ = new_state;
    state_entry_time_ = std::chrono::steady_clock::now();
    recordStateTransition(new_state);
    
    // Cancel any existing state timeout
    cancelStateTimeout();
    
    // Execute async entry action
    executeEntryAction(new_state, [this, old_state, new_state]() {
      // Notify observers
      notifyStateChange(old_state, new_state);
      transition_in_progress_ = false;
    });
  });
}

uint32_t HttpSseStateMachine::addStateChangeListener(StateChangeCallback callback) {
  assertInDispatcherThread();
  uint32_t id = next_listener_id_++;
  state_listeners_[id] = callback;
  return id;
}

void HttpSseStateMachine::removeStateChangeListener(uint32_t listener_id) {
  assertInDispatcherThread();
  state_listeners_.erase(listener_id);
}

// =============================================================================
// Stream Management
// =============================================================================

HttpSseStateMachine::StreamState* HttpSseStateMachine::createStream(
    const std::string& stream_id) {
  assertInDispatcherThread();
  
  // Check if stream already exists
  if (active_streams_.find(stream_id) != active_streams_.end()) {
    return nullptr;  // Stream already exists
  }
  
  // Create new stream
  auto stream = std::make_unique<StreamState>(stream_id);
  StreamState* stream_ptr = stream.get();
  active_streams_[stream_id] = std::move(stream);
  
  return stream_ptr;
}

HttpSseStateMachine::StreamState* HttpSseStateMachine::getStream(
    const std::string& stream_id) {
  assertInDispatcherThread();
  
  auto it = active_streams_.find(stream_id);
  if (it != active_streams_.end()) {
    return it->second.get();
  }
  return nullptr;
}

void HttpSseStateMachine::resetStream(const std::string& stream_id, 
                                     StreamResetReason reason) {
  assertInDispatcherThread();
  
  auto it = active_streams_.find(stream_id);
  if (it != active_streams_.end()) {
    auto& stream = it->second;
    stream->reset_called = true;
    
    // Mark as zombie for cleanup
    markStreamAsZombie(stream_id);
    
    // Log reset reason for debugging
    error_context_ = "Stream " + stream_id + " reset: " + std::to_string(static_cast<int>(reason));
  }
}

void HttpSseStateMachine::markStreamAsZombie(const std::string& stream_id) {
  assertInDispatcherThread();
  
  auto it = active_streams_.find(stream_id);
  if (it != active_streams_.end()) {
    it->second->zombie_stream = true;
    zombie_stream_ids_.push_back(stream_id);
    
    // Schedule cleanup if too many zombies
    if (zombie_stream_ids_.size() > 10) {
      dispatcher_.post([this]() { cleanupZombieStreams(); });
    }
  }
}

void HttpSseStateMachine::cleanupZombieStreams() {
  assertInDispatcherThread();
  
  for (const auto& stream_id : zombie_stream_ids_) {
    cleanupStream(stream_id);
  }
  zombie_stream_ids_.clear();
}

void HttpSseStateMachine::cleanupStream(const std::string& stream_id) {
  active_streams_.erase(stream_id);
}

// =============================================================================
// HTTP Protocol Events
// =============================================================================

void HttpSseStateMachine::onHttpRequestSent(const std::string& stream_id) {
  assertInDispatcherThread();
  
  auto* stream = getStream(stream_id);
  if (stream) {
    stream->end_stream_sent = true;
    stream->request_state = HttpSseState::HttpRequestSent;
    
    // Transition to waiting for response
    scheduleTransition(HttpSseState::HttpResponseWaiting);
  }
}

void HttpSseStateMachine::onHttpResponseReceived(const std::string& stream_id, 
                                                int status_code) {
  assertInDispatcherThread();
  
  auto* stream = getStream(stream_id);
  if (stream) {
    stream->headers_complete = true;
    stream->response_state = HttpSseState::HttpResponseHeadersReceiving;
    
    if (status_code == 200) {
      // Success - prepare for SSE stream
      scheduleTransition(HttpSseState::SseNegotiating);
    } else if (status_code >= 500) {
      // Server error - schedule reconnect
      last_error_ = Error{status_code, "HTTP " + std::to_string(status_code)};
      scheduleReconnect();
    } else {
      // Client error or redirect - don't reconnect
      last_error_ = Error{status_code, "HTTP " + std::to_string(status_code)};
      scheduleTransition(HttpSseState::Error);
    }
  }
}

void HttpSseStateMachine::onSseEventReceived(const std::string& stream_id,
                                            const std::string& event_data) {
  assertInDispatcherThread();
  
  auto* stream = getStream(stream_id);
  if (stream) {
    stream->events_received++;
    stream->bytes_received += event_data.size();
    
    // Update state if needed
    if (current_state_ == HttpSseState::SseNegotiating) {
      transition(HttpSseState::SseStreamActive);
    }
  }
}

// =============================================================================
// Reconnection Management
// =============================================================================

void HttpSseStateMachine::scheduleReconnect() {
  assertInDispatcherThread();
  
  // Check if we can reconnect
  if (!HttpSseStatePatterns::canReconnect(current_state_)) {
    return;
  }
  
  // Calculate backoff delay
  reconnect_delay_ = reconnect_strategy_(reconnect_attempt_);
  reconnect_attempt_++;
  
  // Transition to reconnect state
  transition(HttpSseState::ReconnectScheduled, [this](bool success, const std::string& error) {
    if (success) {
      // Create timer for reconnection
      if (!reconnect_timer_) {
        reconnect_timer_ = dispatcher_.createTimer([this]() {
          onReconnectTimeout();
        });
      }
      
      // Start backoff timer
      reconnect_timer_->enableTimer(reconnect_delay_);
      
      // Transition to backoff state
      scheduleTransition(HttpSseState::ReconnectWaiting);
    }
  });
}

void HttpSseStateMachine::cancelReconnect() {
  assertInDispatcherThread();
  
  if (reconnect_timer_) {
    reconnect_timer_->disableTimer();
  }
  reconnect_attempt_ = 0;
}

void HttpSseStateMachine::onReconnectTimeout() {
  assertInDispatcherThread();
  
  // Transition to reconnect attempting
  transition(HttpSseState::ReconnectAttempting, [this](bool success, const std::string& error) {
    if (success) {
      // Reset to initial state for connection
      scheduleTransition(HttpSseState::Initialized);
    } else {
      // Reconnect failed, try again
      failed_reconnects_++;
      scheduleReconnect();
    }
  });
}

// =============================================================================
// State Timeout Management
// =============================================================================

void HttpSseStateMachine::setStateTimeout(std::chrono::milliseconds timeout,
                                         HttpSseState timeout_state) {
  assertInDispatcherThread();
  
  // Cancel existing timeout
  cancelStateTimeout();
  
  // Create new timeout timer
  timeout_state_ = timeout_state;
  state_timeout_timer_ = dispatcher_.createTimer([this, timeout_state]() {
    // Timeout expired, transition to timeout state
    scheduleTransition(timeout_state);
  });
  state_timeout_timer_->enableTimer(timeout);
}

void HttpSseStateMachine::cancelStateTimeout() {
  assertInDispatcherThread();
  
  if (state_timeout_timer_) {
    state_timeout_timer_->disableTimer();
    state_timeout_timer_.reset();
  }
}

// =============================================================================
// Helper Methods
// =============================================================================

std::string HttpSseStateMachine::getStateName(HttpSseState state) {
  switch (state) {
    case HttpSseState::Uninitialized: return "Uninitialized";
    case HttpSseState::Initialized: return "Initialized";
    case HttpSseState::TcpConnecting: return "TcpConnecting";
    case HttpSseState::TcpConnected: return "TcpConnected";
    case HttpSseState::HttpRequestPreparing: return "HttpRequestPreparing";
    case HttpSseState::HttpRequestSending: return "HttpRequestSending";
    case HttpSseState::HttpRequestBodySending: return "HttpRequestBodySending";
    case HttpSseState::HttpRequestSent: return "HttpRequestSent";
    case HttpSseState::HttpResponseWaiting: return "HttpResponseWaiting";
    case HttpSseState::HttpResponseHeadersReceiving: return "HttpResponseHeadersReceiving";
    case HttpSseState::HttpResponseUpgrading: return "HttpResponseUpgrading";
    case HttpSseState::HttpResponseBodyReceiving: return "HttpResponseBodyReceiving";
    case HttpSseState::SseNegotiating: return "SseNegotiating";
    case HttpSseState::SseStreamActive: return "SseStreamActive";
    case HttpSseState::SseEventBuffering: return "SseEventBuffering";
    case HttpSseState::SseEventReceived: return "SseEventReceived";
    case HttpSseState::SseKeepAliveReceiving: return "SseKeepAliveReceiving";
    case HttpSseState::ServerListening: return "ServerListening";
    case HttpSseState::ServerConnectionAccepted: return "ServerConnectionAccepted";
    case HttpSseState::ServerRequestReceiving: return "ServerRequestReceiving";
    case HttpSseState::ServerResponseSending: return "ServerResponseSending";
    case HttpSseState::ServerSsePushing: return "ServerSsePushing";
    case HttpSseState::ReconnectScheduled: return "ReconnectScheduled";
    case HttpSseState::ReconnectWaiting: return "ReconnectWaiting";
    case HttpSseState::ReconnectAttempting: return "ReconnectAttempting";
    case HttpSseState::ShutdownInitiated: return "ShutdownInitiated";
    case HttpSseState::ShutdownDraining: return "ShutdownDraining";
    case HttpSseState::ShutdownCompleted: return "ShutdownCompleted";
    case HttpSseState::Closed: return "Closed";
    case HttpSseState::Error: return "Error";
    case HttpSseState::HttpOnlyMode: return "HttpOnlyMode";
    case HttpSseState::PartialDataReceived: return "PartialDataReceived";
    default: return "Unknown";
  }
}

std::chrono::milliseconds HttpSseStateMachine::getTimeInCurrentState() const {
  assertInDispatcherThread();
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now - state_entry_time_);
}

std::vector<std::pair<HttpSseState, std::chrono::steady_clock::time_point>> 
HttpSseStateMachine::getStateHistory(size_t max_entries) const {
  assertInDispatcherThread();
  
  size_t start = 0;
  if (state_history_.size() > max_entries) {
    start = state_history_.size() - max_entries;
  }
  
  return std::vector<std::pair<HttpSseState, std::chrono::steady_clock::time_point>>(
      state_history_.begin() + start, state_history_.end());
}

HttpSseStateMachine::StreamStats HttpSseStateMachine::getStreamStats() const {
  assertInDispatcherThread();
  
  StreamStats stats{};
  stats.active_streams = active_streams_.size();
  stats.zombie_streams = zombie_stream_ids_.size();
  
  std::chrono::milliseconds total_lifetime{0};
  
  for (const auto& [id, stream] : active_streams_) {
    stats.total_bytes_sent += stream->bytes_sent;
    stats.total_bytes_received += stream->bytes_received;
    stats.total_events_received += stream->events_received;
    
    if (stream->complete_time.has_value()) {
      auto lifetime = std::chrono::duration_cast<std::chrono::milliseconds>(
          stream->complete_time.value() - stream->created_time);
      total_lifetime += lifetime;
    }
  }
  
  if (stats.active_streams > 0) {
    stats.avg_stream_lifetime = total_lifetime / stats.active_streams;
  }
  
  return stats;
}

// =============================================================================
// Private Methods - State Transition Management
// =============================================================================

void HttpSseStateMachine::initializeClientTransitions() {
  // Initial transitions
  valid_transitions_[HttpSseState::Uninitialized] = {
    HttpSseState::Initialized,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::Initialized] = {
    HttpSseState::TcpConnecting,
    HttpSseState::Closed,
    HttpSseState::Error
  };
  
  // TCP connection transitions
  valid_transitions_[HttpSseState::TcpConnecting] = {
    HttpSseState::TcpConnected,
    HttpSseState::ReconnectScheduled,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::TcpConnected] = {
    HttpSseState::HttpRequestPreparing,
    HttpSseState::ShutdownInitiated,
    HttpSseState::Error
  };
  
  // HTTP request transitions
  valid_transitions_[HttpSseState::HttpRequestPreparing] = {
    HttpSseState::HttpRequestSending,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::HttpRequestSending] = {
    HttpSseState::HttpRequestBodySending,
    HttpSseState::HttpRequestSent,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::HttpRequestBodySending] = {
    HttpSseState::HttpRequestSent,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::HttpRequestSent] = {
    HttpSseState::HttpResponseWaiting,
    HttpSseState::Error
  };
  
  // HTTP response transitions
  valid_transitions_[HttpSseState::HttpResponseWaiting] = {
    HttpSseState::HttpResponseHeadersReceiving,
    HttpSseState::ReconnectScheduled,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::HttpResponseHeadersReceiving] = {
    HttpSseState::HttpResponseUpgrading,
    HttpSseState::HttpResponseBodyReceiving,
    HttpSseState::SseNegotiating,
    HttpSseState::HttpOnlyMode,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::HttpResponseUpgrading] = {
    HttpSseState::SseNegotiating,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::HttpResponseBodyReceiving] = {
    HttpSseState::HttpOnlyMode,
    HttpSseState::Closed,
    HttpSseState::Error
  };
  
  // SSE stream transitions
  valid_transitions_[HttpSseState::SseNegotiating] = {
    HttpSseState::SseStreamActive,
    HttpSseState::HttpOnlyMode,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::SseStreamActive] = {
    HttpSseState::SseEventBuffering,
    HttpSseState::SseKeepAliveReceiving,
    HttpSseState::ShutdownInitiated,
    HttpSseState::ReconnectScheduled,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::SseEventBuffering] = {
    HttpSseState::SseEventReceived,
    HttpSseState::SseStreamActive,
    HttpSseState::PartialDataReceived,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::SseEventReceived] = {
    HttpSseState::SseStreamActive,
    HttpSseState::SseEventBuffering,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::SseKeepAliveReceiving] = {
    HttpSseState::SseStreamActive,
    HttpSseState::Error
  };
  
  // Reconnection transitions
  valid_transitions_[HttpSseState::ReconnectScheduled] = {
    HttpSseState::ReconnectWaiting,
    HttpSseState::Closed,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::ReconnectWaiting] = {
    HttpSseState::ReconnectAttempting,
    HttpSseState::Closed,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::ReconnectAttempting] = {
    HttpSseState::Initialized,
    HttpSseState::TcpConnecting,
    HttpSseState::ReconnectScheduled,
    HttpSseState::Error
  };
  
  // Shutdown transitions
  valid_transitions_[HttpSseState::ShutdownInitiated] = {
    HttpSseState::ShutdownDraining,
    HttpSseState::ShutdownCompleted,
    HttpSseState::Closed
  };
  
  valid_transitions_[HttpSseState::ShutdownDraining] = {
    HttpSseState::ShutdownCompleted,
    HttpSseState::Closed
  };
  
  valid_transitions_[HttpSseState::ShutdownCompleted] = {
    HttpSseState::Closed
  };
  
  // Degraded state transitions
  valid_transitions_[HttpSseState::HttpOnlyMode] = {
    HttpSseState::SseNegotiating,  // Retry SSE
    HttpSseState::ShutdownInitiated,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::PartialDataReceived] = {
    HttpSseState::SseEventBuffering,
    HttpSseState::ReconnectScheduled,
    HttpSseState::Error
  };
  
  // Terminal states have no valid transitions
  valid_transitions_[HttpSseState::Closed] = {};
  valid_transitions_[HttpSseState::Error] = {
    HttpSseState::ReconnectScheduled  // Allow recovery from error
  };
}

void HttpSseStateMachine::initializeServerTransitions() {
  // Initial transitions
  valid_transitions_[HttpSseState::Uninitialized] = {
    HttpSseState::Initialized,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::Initialized] = {
    HttpSseState::ServerListening,
    HttpSseState::ServerConnectionAccepted,  // Direct accept for pre-connected sockets
    HttpSseState::Closed,
    HttpSseState::Error
  };
  
  // Server listening transitions
  valid_transitions_[HttpSseState::ServerListening] = {
    HttpSseState::ServerConnectionAccepted,
    HttpSseState::ShutdownInitiated,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::ServerConnectionAccepted] = {
    HttpSseState::ServerRequestReceiving,
    HttpSseState::Error
  };
  
  // Server request handling
  valid_transitions_[HttpSseState::ServerRequestReceiving] = {
    HttpSseState::ServerResponseSending,
    HttpSseState::Error
  };
  
  valid_transitions_[HttpSseState::ServerResponseSending] = {
    HttpSseState::ServerSsePushing,
    HttpSseState::Closed,
    HttpSseState::Error
  };
  
  // Server SSE pushing
  valid_transitions_[HttpSseState::ServerSsePushing] = {
    HttpSseState::SseEventBuffering,
    HttpSseState::SseKeepAliveReceiving,
    HttpSseState::ShutdownInitiated,
    HttpSseState::Error
  };
  
  // Shutdown transitions (same as client)
  valid_transitions_[HttpSseState::ShutdownInitiated] = {
    HttpSseState::ShutdownDraining,
    HttpSseState::ShutdownCompleted,
    HttpSseState::Closed
  };
  
  valid_transitions_[HttpSseState::ShutdownDraining] = {
    HttpSseState::ShutdownCompleted,
    HttpSseState::Closed
  };
  
  valid_transitions_[HttpSseState::ShutdownCompleted] = {
    HttpSseState::Closed
  };
  
  // Terminal states
  valid_transitions_[HttpSseState::Closed] = {};
  valid_transitions_[HttpSseState::Error] = {};
}

void HttpSseStateMachine::executeEntryAction(HttpSseState state, std::function<void()> done) {
  auto it = entry_actions_.find(state);
  if (it != entry_actions_.end() && it->second) {
    // Execute async action with completion callback
    it->second(state, done);
  } else {
    // No action, immediately call done
    done();
  }
}

void HttpSseStateMachine::executeExitAction(HttpSseState state, std::function<void()> done) {
  auto it = exit_actions_.find(state);
  if (it != exit_actions_.end() && it->second) {
    // Execute async action with completion callback
    it->second(state, done);
  } else {
    // No action, immediately call done
    done();
  }
}

void HttpSseStateMachine::notifyStateChange(HttpSseState old_state, HttpSseState new_state) {
  // Already in dispatcher thread, no locking needed
  for (const auto& [id, callback] : state_listeners_) {
    callback(old_state, new_state);
  }
}

void HttpSseStateMachine::recordStateTransition(HttpSseState state) {
  state_history_.push_back({state, std::chrono::steady_clock::now()});
  
  // Limit history size
  if (state_history_.size() > kMaxHistorySize) {
    state_history_.erase(state_history_.begin());
  }
}

bool HttpSseStateMachine::isValidTransition(HttpSseState from, HttpSseState to) const {
  auto it = valid_transitions_.find(from);
  if (it == valid_transitions_.end()) {
    return false;
  }
  
  return it->second.find(to) != it->second.end();
}

// =============================================================================
// HttpSseStateMachineFactory Implementation
// =============================================================================

std::unique_ptr<HttpSseStateMachine> HttpSseStateMachineFactory::createClientStateMachine(
    event::Dispatcher& dispatcher) {
  auto machine = std::make_unique<HttpSseStateMachine>(HttpSseMode::Client, dispatcher);
  
  // Set up standard client entry/exit actions
  
  // TCP connection establishment
  machine->setEntryAction(HttpSseState::TcpConnecting, 
                         [](HttpSseState, std::function<void()> done) {
                           // Initiate TCP connection
                           done();
                         });
  
  // HTTP request preparation
  machine->setEntryAction(HttpSseState::HttpRequestPreparing,
                         [](HttpSseState, std::function<void()> done) {
                           // Prepare HTTP headers
                           done();
                         });
  
  // SSE stream negotiation
  machine->setEntryAction(HttpSseState::SseNegotiating,
                         [](HttpSseState, std::function<void()> done) {
                           // Validate SSE headers
                           done();
                         });
  
  // Stream active
  machine->setEntryAction(HttpSseState::SseStreamActive,
                         [](HttpSseState, std::function<void()> done) {
                           // Stream established
                           done();
                         });
  
  // Cleanup on exit
  machine->setExitAction(HttpSseState::SseStreamActive,
                        [](HttpSseState, std::function<void()> done) {
                          // Cleanup stream resources
                          done();
                        });
  
  return machine;
}

std::unique_ptr<HttpSseStateMachine> HttpSseStateMachineFactory::createServerStateMachine(
    event::Dispatcher& dispatcher) {
  auto machine = std::make_unique<HttpSseStateMachine>(HttpSseMode::Server, dispatcher);
  
  // Set up standard server entry/exit actions
  
  // Server listening
  machine->setEntryAction(HttpSseState::ServerListening,
                         [](HttpSseState, std::function<void()> done) {
                           // Start listening for connections
                           done();
                         });
  
  // Connection accepted
  machine->setEntryAction(HttpSseState::ServerConnectionAccepted,
                         [](HttpSseState, std::function<void()> done) {
                           // Initialize connection resources
                           done();
                         });
  
  // SSE pushing
  machine->setEntryAction(HttpSseState::ServerSsePushing,
                         [](HttpSseState, std::function<void()> done) {
                           // Initialize SSE stream
                           done();
                         });
  
  return machine;
}

std::unique_ptr<HttpSseStateMachine> HttpSseStateMachineFactory::createStateMachine(
    const Config& config,
    event::Dispatcher& dispatcher) {
  auto machine = std::make_unique<HttpSseStateMachine>(config.mode, dispatcher);
  
  // Configure reconnection strategy
  if (config.max_reconnect_attempts > 0) {
    machine->setReconnectStrategy([config](uint32_t attempt) -> std::chrono::milliseconds {
      if (attempt >= config.max_reconnect_attempts) {
        return std::chrono::milliseconds::max();  // No more retries
      }
      
      // Exponential backoff with jitter
      auto base_delay = config.reconnect_initial_delay;
      auto max_delay = config.reconnect_max_delay;
      
      auto delay = base_delay * (1 << std::min(attempt, 10u));
      delay = std::min(delay, max_delay);
      
      // Add jitter (±10%)
      auto jitter = delay / 10;
      delay = delay - jitter + std::chrono::milliseconds(rand() % (2 * jitter.count()));
      
      return delay;
    });
  }
  
  // Set default timeout
  machine->setStateTimeout(config.default_timeout, HttpSseState::Error);
  
  // Enable zombie cleanup if configured
  if (config.enable_zombie_cleanup) {
    // Schedule periodic cleanup
    // Note: In real implementation, would use a periodic timer
  }
  
  return machine;
}

// =============================================================================
// HttpSseStatePatterns Implementation
// =============================================================================

bool HttpSseStatePatterns::isConnectedState(HttpSseState state) {
  switch (state) {
    case HttpSseState::TcpConnected:
    case HttpSseState::HttpResponseBodyReceiving:
    case HttpSseState::SseStreamActive:
    case HttpSseState::SseEventBuffering:
    case HttpSseState::SseEventReceived:
    case HttpSseState::SseKeepAliveReceiving:
    case HttpSseState::HttpOnlyMode:
    case HttpSseState::ServerSsePushing:
      return true;
    default:
      return false;
  }
}

bool HttpSseStatePatterns::isHttpRequestState(HttpSseState state) {
  switch (state) {
    case HttpSseState::HttpRequestPreparing:
    case HttpSseState::HttpRequestSending:
    case HttpSseState::HttpRequestBodySending:
    case HttpSseState::HttpRequestSent:
    case HttpSseState::ServerRequestReceiving:  // Server-side request processing
      return true;
    default:
      return false;
  }
}

bool HttpSseStatePatterns::isHttpResponseState(HttpSseState state) {
  switch (state) {
    case HttpSseState::HttpResponseWaiting:
    case HttpSseState::HttpResponseHeadersReceiving:
    case HttpSseState::HttpResponseUpgrading:
    case HttpSseState::HttpResponseBodyReceiving:
      return true;
    default:
      return false;
  }
}

bool HttpSseStatePatterns::isSseStreamState(HttpSseState state) {
  switch (state) {
    case HttpSseState::SseNegotiating:
    case HttpSseState::SseStreamActive:
    case HttpSseState::SseEventBuffering:
    case HttpSseState::SseEventReceived:
    case HttpSseState::SseKeepAliveReceiving:
    case HttpSseState::ServerSsePushing:
      return true;
    default:
      return false;
  }
}

bool HttpSseStatePatterns::canSendData(HttpSseState state) {
  switch (state) {
    case HttpSseState::HttpRequestSending:
    case HttpSseState::HttpRequestBodySending:
    case HttpSseState::ServerResponseSending:
    case HttpSseState::ServerSsePushing:
      return true;
    default:
      return false;
  }
}

bool HttpSseStatePatterns::canReceiveData(HttpSseState state) {
  switch (state) {
    case HttpSseState::HttpResponseWaiting:
    case HttpSseState::HttpResponseHeadersReceiving:
    case HttpSseState::HttpResponseBodyReceiving:
    case HttpSseState::SseStreamActive:
    case HttpSseState::SseEventBuffering:
    case HttpSseState::ServerRequestReceiving:
      return true;
    default:
      return false;
  }
}

optional<HttpSseState> HttpSseStatePatterns::getNextHttpRequestState(HttpSseState current) {
  switch (current) {
    case HttpSseState::HttpRequestPreparing:
      return HttpSseState::HttpRequestSending;
    case HttpSseState::HttpRequestSending:
      return HttpSseState::HttpRequestSent;
    case HttpSseState::HttpRequestBodySending:
      return HttpSseState::HttpRequestSent;
    case HttpSseState::HttpRequestSent:
      return HttpSseState::HttpResponseWaiting;
    default:
      return nullopt;
  }
}

optional<HttpSseState> HttpSseStatePatterns::getNextHttpResponseState(HttpSseState current) {
  switch (current) {
    case HttpSseState::HttpResponseWaiting:
      return HttpSseState::HttpResponseHeadersReceiving;
    case HttpSseState::HttpResponseHeadersReceiving:
      return HttpSseState::HttpResponseBodyReceiving;
    case HttpSseState::HttpResponseUpgrading:
      return HttpSseState::SseNegotiating;
    case HttpSseState::HttpResponseBodyReceiving:
      return HttpSseState::HttpOnlyMode;
    default:
      return nullopt;
  }
}

bool HttpSseStatePatterns::isErrorState(HttpSseState state) {
  return state == HttpSseState::Error || state == HttpSseState::PartialDataReceived;
}

bool HttpSseStatePatterns::canReconnect(HttpSseState state) {
  // Can only reconnect from terminal or error states, not from active states
  switch (state) {
    case HttpSseState::Error:
    case HttpSseState::Closed:
    case HttpSseState::PartialDataReceived:
    case HttpSseState::ShutdownCompleted:
      return true;
    // Cannot reconnect from active connection states
    case HttpSseState::TcpConnecting:  // Already connecting
    case HttpSseState::HttpResponseWaiting:  // Active connection
    case HttpSseState::SseStreamActive:  // Active SSE stream
      return false;
    default:
      return false;
  }
}

// =============================================================================
// HttpSseTransitionCoordinator Implementation
// =============================================================================

void HttpSseTransitionCoordinator::executeConnection(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers,
    std::function<void(bool)> callback) {
  
  // Connection sequence for HTTP+SSE
  std::vector<HttpSseState> sequence = {
    HttpSseState::Initialized,
    HttpSseState::TcpConnecting,
    HttpSseState::TcpConnected,
    HttpSseState::HttpRequestPreparing,
    HttpSseState::HttpRequestSending,
    HttpSseState::HttpRequestSent,
    HttpSseState::HttpResponseWaiting,
    HttpSseState::HttpResponseHeadersReceiving,
    HttpSseState::SseNegotiating,
    HttpSseState::SseStreamActive
  };
  
  executeSequence(sequence, 0, callback);
}

void HttpSseTransitionCoordinator::executeHttpRequest(
    const std::string& stream_id,
    std::function<void(bool)> callback) {
  
  // HTTP request sequence
  std::vector<HttpSseState> sequence = {
    HttpSseState::HttpRequestPreparing,
    HttpSseState::HttpRequestSending,
    HttpSseState::HttpRequestSent,
    HttpSseState::HttpResponseWaiting
  };
  
  executeSequence(sequence, 0, callback);
}

void HttpSseTransitionCoordinator::executeSseNegotiation(
    const std::string& stream_id,
    std::function<void(bool)> callback) {
  
  // SSE negotiation sequence
  std::vector<HttpSseState> sequence = {
    HttpSseState::SseNegotiating,
    HttpSseState::SseStreamActive
  };
  
  executeSequence(sequence, 0, callback);
}

void HttpSseTransitionCoordinator::executeShutdown(std::function<void(bool)> callback) {
  // Graceful shutdown sequence
  std::vector<HttpSseState> sequence = {
    HttpSseState::ShutdownInitiated,
    HttpSseState::ShutdownDraining,
    HttpSseState::ShutdownCompleted,
    HttpSseState::Closed
  };
  
  executeSequence(sequence, 0, callback);
}

void HttpSseTransitionCoordinator::executeReconnection(std::function<void(bool)> callback) {
  // Reconnection sequence
  std::vector<HttpSseState> sequence = {
    HttpSseState::ReconnectScheduled,
    HttpSseState::ReconnectWaiting,
    HttpSseState::ReconnectAttempting,
    HttpSseState::Initialized
  };
  
  executeSequence(sequence, 0, [this, callback](bool success) {
    if (success) {
      // Continue with connection sequence
      executeConnection("", {}, callback);
    } else {
      callback(false);
    }
  });
}

void HttpSseTransitionCoordinator::executeSequence(
    const std::vector<HttpSseState>& states,
    size_t index,
    std::function<void(bool)> callback) {
  
  // Check if sequence is complete
  if (index >= states.size()) {
    callback(true);
    return;
  }
  
  // Check if already in target state (can skip)
  if (machine_.getCurrentState() == states[index]) {
    // Move to next state in sequence
    executeSequence(states, index + 1, callback);
    return;
  }
  
  // Transition to next state in sequence
  machine_.transition(states[index],
                     [this, states, index, callback](bool success, const std::string& error) {
    if (!success) {
      // Sequence failed
      callback(false);
      return;
    }
    
    // Continue with next state
    executeSequence(states, index + 1, callback);
  });
}

}  // namespace transport
}  // namespace mcp