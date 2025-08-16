/**
 * @file connection_state_machine.cc
 * @brief Implementation of Connection State Machine following production patterns
 */

#include "mcp/network/connection_state_machine.h"

#include <algorithm>
#include <sstream>

namespace mcp {
namespace network {

namespace {

// State transition validation matrix
// This defines valid state transitions following production connection model
const std::unordered_map<ConnectionState, std::unordered_set<ConnectionState>>&
getValidTransitions() {
  static const std::unordered_map<ConnectionState, std::unordered_set<ConnectionState>>
      transitions = {
          // Initial states
          {ConnectionState::Uninitialized,
           {ConnectionState::Initialized, ConnectionState::Error}},
          
          {ConnectionState::Initialized,
           {ConnectionState::Resolving, ConnectionState::Connecting,
            ConnectionState::Listening, ConnectionState::Closed, ConnectionState::Error}},
          
          // Client connecting states
          {ConnectionState::Resolving,
           {ConnectionState::Connecting, ConnectionState::Error,
            ConnectionState::WaitingToReconnect}},
          
          {ConnectionState::Connecting,
           {ConnectionState::TcpConnected, ConnectionState::Error,
            ConnectionState::WaitingToReconnect, ConnectionState::Closed}},
          
          {ConnectionState::TcpConnected,
           {ConnectionState::HandshakeInProgress, ConnectionState::Connected,
            ConnectionState::Error, ConnectionState::Closing}},
          
          {ConnectionState::HandshakeInProgress,
           {ConnectionState::Connected, ConnectionState::Error,
            ConnectionState::Closing, ConnectionState::WaitingToReconnect}},
          
          // Server accepting states
          {ConnectionState::Listening,
           {ConnectionState::Accepting, ConnectionState::Closed,
            ConnectionState::Error}},
          
          {ConnectionState::Accepting,
           {ConnectionState::Accepted, ConnectionState::Listening,
            ConnectionState::Error}},
          
          {ConnectionState::Accepted,
           {ConnectionState::HandshakeInProgress, ConnectionState::Connected,
            ConnectionState::Error, ConnectionState::Closing}},
          
          // Connected states
          {ConnectionState::Connected,
           {ConnectionState::Reading, ConnectionState::Writing,
            ConnectionState::Idle, ConnectionState::Processing,
            ConnectionState::ReadDisabled, ConnectionState::WriteDisabled,
            ConnectionState::HalfClosedLocal, ConnectionState::HalfClosedRemote,
            ConnectionState::Closing, ConnectionState::Error}},
          
          {ConnectionState::Reading,
           {ConnectionState::Connected, ConnectionState::Processing,
            ConnectionState::Writing, ConnectionState::Idle,
            ConnectionState::ReadDisabled, ConnectionState::HalfClosedRemote,
            ConnectionState::Closing, ConnectionState::Error}},
          
          {ConnectionState::Writing,
           {ConnectionState::Connected, ConnectionState::Reading,
            ConnectionState::Idle, ConnectionState::WriteDisabled,
            ConnectionState::Flushing, ConnectionState::HalfClosedLocal,
            ConnectionState::Closing, ConnectionState::Error}},
          
          {ConnectionState::Idle,
           {ConnectionState::Reading, ConnectionState::Writing,
            ConnectionState::Connected, ConnectionState::Closing,
            ConnectionState::Error}},
          
          {ConnectionState::Processing,
           {ConnectionState::Connected, ConnectionState::Reading,
            ConnectionState::Writing, ConnectionState::Idle,
            ConnectionState::Closing, ConnectionState::Error}},
          
          // Flow control states
          {ConnectionState::ReadDisabled,
           {ConnectionState::Connected, ConnectionState::Writing,
            ConnectionState::Paused, ConnectionState::Closing,
            ConnectionState::Error}},
          
          {ConnectionState::WriteDisabled,
           {ConnectionState::Connected, ConnectionState::Reading,
            ConnectionState::Paused, ConnectionState::Closing,
            ConnectionState::Error}},
          
          {ConnectionState::Paused,
           {ConnectionState::Connected, ConnectionState::ReadDisabled,
            ConnectionState::WriteDisabled, ConnectionState::Closing,
            ConnectionState::Error}},
          
          // Closing states
          {ConnectionState::HalfClosedLocal,
           {ConnectionState::Reading, ConnectionState::Closing,
            ConnectionState::Closed, ConnectionState::Error}},
          
          {ConnectionState::HalfClosedRemote,
           {ConnectionState::Writing, ConnectionState::Flushing,
            ConnectionState::Closing, ConnectionState::Closed,
            ConnectionState::Error}},
          
          {ConnectionState::Closing,
           {ConnectionState::Draining, ConnectionState::Flushing,
            ConnectionState::Closed, ConnectionState::Error}},
          
          {ConnectionState::Draining,
           {ConnectionState::Flushing, ConnectionState::Closed,
            ConnectionState::Error}},
          
          {ConnectionState::Flushing,
           {ConnectionState::Closed, ConnectionState::Error}},
          
          // Terminal states
          {ConnectionState::Closed,
           {ConnectionState::Initialized, ConnectionState::WaitingToReconnect}},
          
          {ConnectionState::Error,
           {ConnectionState::Closed, ConnectionState::WaitingToReconnect,
            ConnectionState::Recovering}},
          
          {ConnectionState::Aborted,
           {ConnectionState::Closed, ConnectionState::WaitingToReconnect}},
          
          // Recovery states
          {ConnectionState::Reconnecting,
           {ConnectionState::Resolving, ConnectionState::Connecting,
            ConnectionState::Error, ConnectionState::WaitingToReconnect}},
          
          {ConnectionState::WaitingToReconnect,
           {ConnectionState::Reconnecting, ConnectionState::Closed,
            ConnectionState::Error}},
          
          {ConnectionState::Recovering,
           {ConnectionState::Connected, ConnectionState::Error,
            ConnectionState::Closed}}
      };
  
  return transitions;
}

// Map connection events to appropriate state machine events
ConnectionStateMachineEvent mapConnectionEvent(ConnectionEvent event) {
  switch (event) {
    case ConnectionEvent::Connected:
      return ConnectionStateMachineEvent::SocketConnected;
    case ConnectionEvent::RemoteClose:
      return ConnectionStateMachineEvent::EndOfStream;
    case ConnectionEvent::LocalClose:
      return ConnectionStateMachineEvent::CloseRequested;
    default:
      return ConnectionStateMachineEvent::SocketError;
  }
}

}  // namespace

// ===== ConnectionStateMachine Implementation =====

ConnectionStateMachine::ConnectionStateMachine(
    event::Dispatcher& dispatcher,
    Connection& connection,
    const ConnectionStateMachineConfig& config)
    : dispatcher_(dispatcher),
      connection_(connection),
      config_(config),
      current_reconnect_delay_(config.initial_reconnect_delay) {
  
  // Register as connection callbacks
  connection_.addConnectionCallbacks(*this);
  
  // Initialize state entry time
  state_entry_time_ = std::chrono::steady_clock::now();
  
  // Set initial state based on mode
  if (config_.mode == ConnectionMode::Server) {
    current_state_ = ConnectionState::Initialized;
  } else {
    current_state_ = ConnectionState::Uninitialized;
  }
}

ConnectionStateMachine::~ConnectionStateMachine() {
  // Cancel all timers
  cancelAllTimers();
  
  // Remove callbacks
  connection_.removeConnectionCallbacks(*this);
}

// ===== Core State Machine Interface =====

bool ConnectionStateMachine::handleEvent(ConnectionStateMachineEvent event) {
  assertInDispatcherThread();
  
  // Queue event if we're in the middle of processing
  if (processing_events_) {
    event_queue_.push(event);
    return true;
  }
  
  // Process event based on current state
  processing_events_ = true;
  bool handled = false;
  
  switch (event) {
    case ConnectionStateMachineEvent::SocketCreated:
    case ConnectionStateMachineEvent::SocketBound:
    case ConnectionStateMachineEvent::SocketListening:
    case ConnectionStateMachineEvent::SocketConnected:
    case ConnectionStateMachineEvent::SocketAccepted:
    case ConnectionStateMachineEvent::SocketClosed:
    case ConnectionStateMachineEvent::SocketError:
      handleSocketEvent(event);
      handled = true;
      break;
      
    case ConnectionStateMachineEvent::ReadReady:
    case ConnectionStateMachineEvent::WriteReady:
    case ConnectionStateMachineEvent::ReadComplete:
    case ConnectionStateMachineEvent::WriteComplete:
    case ConnectionStateMachineEvent::EndOfStream:
      handleIoEvent(event);
      handled = true;
      break;
      
    case ConnectionStateMachineEvent::HandshakeStarted:
    case ConnectionStateMachineEvent::HandshakeComplete:
    case ConnectionStateMachineEvent::HandshakeFailed:
      handleTransportEvent(event);
      handled = true;
      break;
      
    case ConnectionStateMachineEvent::FilterChainInitialized:
    case ConnectionStateMachineEvent::FilterChainContinue:
    case ConnectionStateMachineEvent::FilterChainStop:
      handleFilterEvent(event);
      handled = true;
      break;
      
    case ConnectionStateMachineEvent::ConnectionRequested:
    case ConnectionStateMachineEvent::CloseRequested:
    case ConnectionStateMachineEvent::ResetRequested:
    case ConnectionStateMachineEvent::ReadDisableRequested:
    case ConnectionStateMachineEvent::WriteDisableRequested:
      handleApplicationEvent(event);
      handled = true;
      break;
      
    case ConnectionStateMachineEvent::ConnectTimeout:
    case ConnectionStateMachineEvent::IdleTimeout:
    case ConnectionStateMachineEvent::DrainTimeout:
      handleTimerEvent(event);
      handled = true;
      break;
      
    case ConnectionStateMachineEvent::ReconnectRequested:
    case ConnectionStateMachineEvent::RecoveryComplete:
    case ConnectionStateMachineEvent::RecoveryFailed:
      handleRecoveryEvent(event);
      handled = true;
      break;
  }
  
  // Process any queued events
  while (!event_queue_.empty()) {
    auto queued_event = event_queue_.front();
    event_queue_.pop();
    handleEvent(queued_event);
  }
  
  processing_events_ = false;
  return handled;
}

void ConnectionStateMachine::forceTransition(ConnectionState new_state,
                                            const std::string& reason) {
  assertInDispatcherThread();
  
  auto old_state = current_state_.load();
  
  // Create transition context
  StateTransitionContext context{
      .from_state = old_state,
      .to_state = new_state,
      .triggering_event = ConnectionStateMachineEvent::ResetRequested,
      .timestamp = std::chrono::steady_clock::now(),
      .reason = "FORCED: " + reason,
      .time_in_previous_state = getTimeInCurrentState()
  };
  
  // Update metrics
  context.bytes_read_in_state = state_bytes_read_;
  context.bytes_written_in_state = state_bytes_written_;
  state_bytes_read_ = 0;
  state_bytes_written_ = 0;
  
  // Exit old state
  onStateExit(old_state);
  
  // Update state
  current_state_ = new_state;
  state_entry_time_ = std::chrono::steady_clock::now();
  total_transitions_++;
  
  // Enter new state
  onStateEnter(new_state);
  
  // Record and notify
  recordStateTransition(context);
  notifyStateChange(context);
}

// ===== Connection Operations =====

void ConnectionStateMachine::connect(
    const Address::InstanceConstSharedPtr& address,
    CompletionCallback callback) {
  assertInDispatcherThread();
  
  if (config_.mode == ConnectionMode::Server) {
    if (callback) {
      callback(false);
    }
    return;
  }
  
  // Store address for potential reconnection
  reconnect_address_ = address;
  
  // Store callback
  if (callback) {
    pending_callbacks_.push_back(callback);
  }
  
  // Trigger connection sequence
  handleEvent(ConnectionStateMachineEvent::ConnectionRequested);
  
  // Start connect timer
  startConnectTimer();
}

void ConnectionStateMachine::listen(
    const Address::InstanceConstSharedPtr& address,
    CompletionCallback callback) {
  assertInDispatcherThread();
  
  if (config_.mode != ConnectionMode::Server &&
      config_.mode != ConnectionMode::Bidirectional) {
    if (callback) {
      callback(false);
    }
    return;
  }
  
  // Transition to listening state
  if (transitionTo(ConnectionState::Listening,
                  ConnectionStateMachineEvent::SocketListening,
                  "Starting to listen")) {
    if (callback) {
      callback(true);
    }
  } else {
    if (callback) {
      callback(false);
    }
  }
}

void ConnectionStateMachine::close(ConnectionCloseType type) {
  assertInDispatcherThread();
  
  ConnectionState target_state;
  
  switch (type) {
    case ConnectionCloseType::FlushWrite:
      target_state = ConnectionState::Flushing;
      break;
    case ConnectionCloseType::NoFlush:
      target_state = ConnectionState::Closing;
      break;
    case ConnectionCloseType::FlushWriteAndDelay:
      target_state = ConnectionState::Draining;
      startDrainTimer();
      break;
    default:
      target_state = ConnectionState::Closing;
  }
  
  transitionTo(target_state,
              ConnectionStateMachineEvent::CloseRequested,
              "Close requested");
}

void ConnectionStateMachine::reset() {
  assertInDispatcherThread();
  
  forceTransition(ConnectionState::Aborted, "Reset requested");
  connection_.close(ConnectionCloseType::NoFlush);
}

// ===== State Transition Logic =====

bool ConnectionStateMachine::transitionTo(
    ConnectionState new_state,
    ConnectionStateMachineEvent event,
    const std::string& reason) {
  assertInDispatcherThread();
  
  // Prevent reentrancy
  if (transition_in_progress_) {
    return false;
  }
  
  auto old_state = current_state_.load();
  
  // Check if transition is valid
  if (!isValidTransition(old_state, new_state, event)) {
    if (config_.error_callback) {
      config_.error_callback(
          "Invalid transition: " + getStateName(old_state) +
          " -> " + getStateName(new_state) +
          " (event: " + getEventName(event) + ")");
    }
    return false;
  }
  
  transition_in_progress_ = true;
  
  // Create transition context
  StateTransitionContext context{
      .from_state = old_state,
      .to_state = new_state,
      .triggering_event = event,
      .timestamp = std::chrono::steady_clock::now(),
      .reason = reason,
      .time_in_previous_state = getTimeInCurrentState()
  };
  
  // Update metrics
  context.bytes_read_in_state = state_bytes_read_;
  context.bytes_written_in_state = state_bytes_written_;
  state_bytes_read_ = 0;
  state_bytes_written_ = 0;
  
  // Exit old state
  onStateExit(old_state);
  
  // Update state
  current_state_ = new_state;
  state_entry_time_ = std::chrono::steady_clock::now();
  total_transitions_++;
  
  // Enter new state
  onStateEnter(new_state);
  
  // Record and notify
  recordStateTransition(context);
  notifyStateChange(context);
  
  transition_in_progress_ = false;
  return true;
}

bool ConnectionStateMachine::isValidTransition(
    ConnectionState from,
    ConnectionState to,
    ConnectionStateMachineEvent event) const {
  
  const auto& transitions = getValidTransitions();
  auto it = transitions.find(from);
  
  if (it == transitions.end()) {
    return false;
  }
  
  return it->second.find(to) != it->second.end();
}

std::unordered_set<ConnectionState>
ConnectionStateMachine::getValidNextStates() const {
  const auto& transitions = getValidTransitions();
  auto it = transitions.find(current_state_.load());
  
  if (it == transitions.end()) {
    return {};
  }
  
  return it->second;
}

void ConnectionStateMachine::onStateEnter(ConnectionState state) {
  // State-specific entry actions
  switch (state) {
    case ConnectionState::Connecting:
      startConnectTimer();
      break;
      
    case ConnectionState::HandshakeInProgress:
      startHandshakeTimer();
      break;
      
    case ConnectionState::Connected:
      // Notify success callbacks
      for (auto& callback : pending_callbacks_) {
        callback(true);
      }
      pending_callbacks_.clear();
      
      // Start idle timer if configured
      if (config_.idle_timeout.count() > 0) {
        startIdleTimer();
      }
      break;
      
    case ConnectionState::Draining:
      startDrainTimer();
      break;
      
    case ConnectionState::WaitingToReconnect:
      startReconnectTimer();
      break;
      
    case ConnectionState::Error:
      consecutive_errors_++;
      if (config_.enable_auto_reconnect &&
          reconnect_attempts_ < config_.max_reconnect_attempts) {
        initiateReconnection();
      }
      break;
      
    case ConnectionState::Closed:
      // Notify failure callbacks
      for (auto& callback : pending_callbacks_) {
        callback(false);
      }
      pending_callbacks_.clear();
      break;
      
    default:
      break;
  }
}

void ConnectionStateMachine::onStateExit(ConnectionState state) {
  // State-specific exit actions
  switch (state) {
    case ConnectionState::Connecting:
      if (connect_timer_) {
        connect_timer_->disableTimer();
      }
      break;
      
    case ConnectionState::HandshakeInProgress:
      if (handshake_timer_) {
        handshake_timer_->disableTimer();
      }
      break;
      
    case ConnectionState::Connected:
    case ConnectionState::Idle:
      if (idle_timer_) {
        idle_timer_->disableTimer();
      }
      break;
      
    case ConnectionState::Draining:
      if (drain_timer_) {
        drain_timer_->disableTimer();
      }
      break;
      
    default:
      break;
  }
}

// ===== Event Handlers =====

void ConnectionStateMachine::handleSocketEvent(
    ConnectionStateMachineEvent event) {
  auto current = current_state_.load();
  
  switch (event) {
    case ConnectionStateMachineEvent::SocketConnected:
      if (current == ConnectionState::Connecting) {
        transitionTo(ConnectionState::TcpConnected, event,
                    "TCP connection established");
        
        // Check if transport socket needs handshake
        if (transport_socket_ && transport_socket_->requiresHandshake()) {
          handleEvent(ConnectionStateMachineEvent::HandshakeStarted);
        } else {
          transitionTo(ConnectionState::Connected, event,
                      "Connection fully established");
        }
      }
      break;
      
    case ConnectionStateMachineEvent::SocketError:
      transitionTo(ConnectionState::Error, event, "Socket error");
      break;
      
    case ConnectionStateMachineEvent::SocketClosed:
      if (!ConnectionStatePatterns::isTerminal(current)) {
        transitionTo(ConnectionState::Closed, event, "Socket closed");
      }
      break;
      
    default:
      break;
  }
}

void ConnectionStateMachine::handleIoEvent(
    ConnectionStateMachineEvent event) {
  auto current = current_state_.load();
  
  switch (event) {
    case ConnectionStateMachineEvent::ReadReady:
      if (current == ConnectionState::Connected ||
          current == ConnectionState::Idle) {
        transitionTo(ConnectionState::Reading, event, "Read ready");
      }
      break;
      
    case ConnectionStateMachineEvent::WriteReady:
      if (current == ConnectionState::Connected ||
          current == ConnectionState::Idle) {
        transitionTo(ConnectionState::Writing, event, "Write ready");
      }
      break;
      
    case ConnectionStateMachineEvent::EndOfStream:
      if (config_.enable_half_close) {
        transitionTo(ConnectionState::HalfClosedRemote, event,
                    "Remote end of stream");
      } else {
        transitionTo(ConnectionState::Closing, event,
                    "End of stream - closing");
      }
      break;
      
    default:
      break;
  }
}

void ConnectionStateMachine::handleTransportEvent(
    ConnectionStateMachineEvent event) {
  switch (event) {
    case ConnectionStateMachineEvent::HandshakeStarted:
      transitionTo(ConnectionState::HandshakeInProgress, event,
                  "Transport handshake started");
      break;
      
    case ConnectionStateMachineEvent::HandshakeComplete:
      transport_connected_ = true;
      transitionTo(ConnectionState::Connected, event,
                  "Transport handshake complete");
      break;
      
    case ConnectionStateMachineEvent::HandshakeFailed:
      transitionTo(ConnectionState::Error, event,
                  "Transport handshake failed");
      break;
      
    default:
      break;
  }
}

void ConnectionStateMachine::handleApplicationEvent(
    ConnectionStateMachineEvent event) {
  auto current = current_state_.load();
  
  switch (event) {
    case ConnectionStateMachineEvent::ConnectionRequested:
      if (current == ConnectionState::Uninitialized ||
          current == ConnectionState::Initialized) {
        transitionTo(ConnectionState::Connecting, event,
                    "Connection requested");
      }
      break;
      
    case ConnectionStateMachineEvent::CloseRequested:
      if (!ConnectionStatePatterns::isTerminal(current)) {
        close();
      }
      break;
      
    case ConnectionStateMachineEvent::ResetRequested:
      reset();
      break;
      
    case ConnectionStateMachineEvent::ReadDisableRequested:
      if (ConnectionStatePatterns::canRead(current)) {
        read_disable_count_++;
        if (read_disable_count_ == 1) {
          transitionTo(ConnectionState::ReadDisabled, event,
                      "Read disabled");
        }
      }
      break;
      
    case ConnectionStateMachineEvent::WriteDisableRequested:
      if (ConnectionStatePatterns::canWrite(current)) {
        write_disable_count_++;
        if (write_disable_count_ == 1) {
          transitionTo(ConnectionState::WriteDisabled, event,
                      "Write disabled");
        }
      }
      break;
      
    default:
      break;
  }
}

void ConnectionStateMachine::handleTimerEvent(
    ConnectionStateMachineEvent event) {
  switch (event) {
    case ConnectionStateMachineEvent::ConnectTimeout:
      transitionTo(ConnectionState::Error, event, "Connect timeout");
      break;
      
    case ConnectionStateMachineEvent::IdleTimeout:
      transitionTo(ConnectionState::Closing, event, "Idle timeout");
      break;
      
    case ConnectionStateMachineEvent::DrainTimeout:
      transitionTo(ConnectionState::Closed, event, "Drain timeout");
      break;
      
    default:
      break;
  }
}

void ConnectionStateMachine::handleRecoveryEvent(
    ConnectionStateMachineEvent event) {
  switch (event) {
    case ConnectionStateMachineEvent::ReconnectRequested:
      initiateReconnection();
      break;
      
    case ConnectionStateMachineEvent::RecoveryComplete:
      handleRecoverySuccess();
      break;
      
    case ConnectionStateMachineEvent::RecoveryFailed:
      handleRecoveryFailure("Recovery failed");
      break;
      
    default:
      break;
  }
}

// ===== Timer Management =====

void ConnectionStateMachine::startConnectTimer() {
  if (config_.connect_timeout.count() == 0) {
    return;
  }
  
  connect_timer_ = dispatcher_.createTimer([this]() {
    handleEvent(ConnectionStateMachineEvent::ConnectTimeout);
  });
  
  connect_timer_->enableTimer(config_.connect_timeout);
}

void ConnectionStateMachine::startHandshakeTimer() {
  if (config_.handshake_timeout.count() == 0) {
    return;
  }
  
  handshake_timer_ = dispatcher_.createTimer([this]() {
    handleEvent(ConnectionStateMachineEvent::ConnectTimeout);
  });
  
  handshake_timer_->enableTimer(config_.handshake_timeout);
}

void ConnectionStateMachine::startIdleTimer() {
  if (config_.idle_timeout.count() == 0) {
    return;
  }
  
  idle_timer_ = dispatcher_.createTimer([this]() {
    handleEvent(ConnectionStateMachineEvent::IdleTimeout);
  });
  
  idle_timer_->enableTimer(config_.idle_timeout);
}

void ConnectionStateMachine::startDrainTimer() {
  if (config_.drain_timeout.count() == 0) {
    return;
  }
  
  drain_timer_ = dispatcher_.createTimer([this]() {
    handleEvent(ConnectionStateMachineEvent::DrainTimeout);
  });
  
  drain_timer_->enableTimer(config_.drain_timeout);
}

void ConnectionStateMachine::startReconnectTimer() {
  reconnect_timer_ = dispatcher_.createTimer([this]() {
    handleReconnectTimeout();
  });
  
  reconnect_timer_->enableTimer(current_reconnect_delay_);
}

void ConnectionStateMachine::cancelAllTimers() {
  if (connect_timer_) {
    connect_timer_->disableTimer();
    connect_timer_.reset();
  }
  
  if (handshake_timer_) {
    handshake_timer_->disableTimer();
    handshake_timer_.reset();
  }
  
  if (idle_timer_) {
    idle_timer_->disableTimer();
    idle_timer_.reset();
  }
  
  if (drain_timer_) {
    drain_timer_->disableTimer();
    drain_timer_.reset();
  }
  
  if (reconnect_timer_) {
    reconnect_timer_->disableTimer();
    reconnect_timer_.reset();
  }
}

// ===== Error Recovery =====

void ConnectionStateMachine::initiateReconnection() {
  if (!config_.enable_auto_reconnect ||
      reconnect_attempts_ >= config_.max_reconnect_attempts) {
    transitionTo(ConnectionState::Closed,
                ConnectionStateMachineEvent::RecoveryFailed,
                "Max reconnection attempts reached");
    return;
  }
  
  reconnect_attempts_++;
  
  // Calculate backoff delay
  current_reconnect_delay_ = std::min(
      current_reconnect_delay_ * config_.reconnect_backoff_multiplier,
      std::chrono::duration_cast<std::chrono::milliseconds>(
          config_.max_reconnect_delay));
  
  transitionTo(ConnectionState::WaitingToReconnect,
              ConnectionStateMachineEvent::ReconnectRequested,
              "Waiting to reconnect");
}

void ConnectionStateMachine::handleReconnectTimeout() {
  transitionTo(ConnectionState::Reconnecting,
              ConnectionStateMachineEvent::ReconnectRequested,
              "Attempting reconnection");
  
  // Re-initiate connection
  if (reconnect_address_) {
    connect(reconnect_address_);
  }
}

void ConnectionStateMachine::handleRecoverySuccess() {
  reconnect_attempts_ = 0;
  current_reconnect_delay_ = config_.initial_reconnect_delay;
  consecutive_errors_ = 0;
  
  if (config_.recovery_callback) {
    config_.recovery_callback(current_state_.load());
  }
}

void ConnectionStateMachine::handleRecoveryFailure(const std::string& reason) {
  if (config_.error_callback) {
    config_.error_callback("Recovery failed: " + reason);
  }
  
  transitionTo(ConnectionState::Error,
              ConnectionStateMachineEvent::RecoveryFailed,
              reason);
}

// ===== Callbacks Implementation =====

void ConnectionStateMachine::onEvent(ConnectionEvent event) {
  handleEvent(mapConnectionEvent(event));
}

void ConnectionStateMachine::onAboveWriteBufferHighWatermark() {
  auto current = current_state_.load();
  if (ConnectionStatePatterns::canWrite(current)) {
    write_disable_count_++;
    if (write_disable_count_ == 1) {
      transitionTo(ConnectionState::WriteDisabled,
                  ConnectionStateMachineEvent::WriteDisableRequested,
                  "Write buffer above high watermark");
    }
  }
}

void ConnectionStateMachine::onBelowWriteBufferLowWatermark() {
  auto current = current_state_.load();
  if (current == ConnectionState::WriteDisabled && write_disable_count_ > 0) {
    write_disable_count_--;
    if (write_disable_count_ == 0) {
      transitionTo(ConnectionState::Connected,
                  ConnectionStateMachineEvent::WriteReady,
                  "Write buffer below low watermark");
    }
  }
}

// ===== Helper Methods =====

std::chrono::milliseconds ConnectionStateMachine::getTimeInCurrentState() const {
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      now - state_entry_time_);
}

void ConnectionStateMachine::notifyStateChange(
    const StateTransitionContext& context) {
  // Call configured callback
  if (config_.state_change_callback) {
    config_.state_change_callback(context);
  }
  
  // Call registered listeners
  for (const auto& listener : state_change_listeners_) {
    listener(context);
  }
}

void ConnectionStateMachine::recordStateTransition(
    const StateTransitionContext& context) {
  // Add to history
  state_history_.push_back(context);
  
  // Trim history if needed
  while (state_history_.size() > kMaxHistorySize) {
    state_history_.pop_front();
  }
}

std::string ConnectionStateMachine::getStateName(ConnectionState state) {
  switch (state) {
    case ConnectionState::Uninitialized: return "Uninitialized";
    case ConnectionState::Initialized: return "Initialized";
    case ConnectionState::Resolving: return "Resolving";
    case ConnectionState::Connecting: return "Connecting";
    case ConnectionState::TcpConnected: return "TcpConnected";
    case ConnectionState::HandshakeInProgress: return "HandshakeInProgress";
    case ConnectionState::Listening: return "Listening";
    case ConnectionState::Accepting: return "Accepting";
    case ConnectionState::Accepted: return "Accepted";
    case ConnectionState::Connected: return "Connected";
    case ConnectionState::Reading: return "Reading";
    case ConnectionState::Writing: return "Writing";
    case ConnectionState::Idle: return "Idle";
    case ConnectionState::Processing: return "Processing";
    case ConnectionState::ReadDisabled: return "ReadDisabled";
    case ConnectionState::WriteDisabled: return "WriteDisabled";
    case ConnectionState::Paused: return "Paused";
    case ConnectionState::HalfClosedLocal: return "HalfClosedLocal";
    case ConnectionState::HalfClosedRemote: return "HalfClosedRemote";
    case ConnectionState::Closing: return "Closing";
    case ConnectionState::Draining: return "Draining";
    case ConnectionState::Flushing: return "Flushing";
    case ConnectionState::Closed: return "Closed";
    case ConnectionState::Error: return "Error";
    case ConnectionState::Aborted: return "Aborted";
    case ConnectionState::Reconnecting: return "Reconnecting";
    case ConnectionState::WaitingToReconnect: return "WaitingToReconnect";
    case ConnectionState::Recovering: return "Recovering";
    default: return "Unknown";
  }
}

std::string ConnectionStateMachine::getEventName(
    ConnectionStateMachineEvent event) {
  switch (event) {
    case ConnectionStateMachineEvent::SocketCreated: return "SocketCreated";
    case ConnectionStateMachineEvent::SocketConnected: return "SocketConnected";
    case ConnectionStateMachineEvent::SocketClosed: return "SocketClosed";
    case ConnectionStateMachineEvent::SocketError: return "SocketError";
    case ConnectionStateMachineEvent::ReadReady: return "ReadReady";
    case ConnectionStateMachineEvent::WriteReady: return "WriteReady";
    case ConnectionStateMachineEvent::EndOfStream: return "EndOfStream";
    case ConnectionStateMachineEvent::HandshakeComplete: return "HandshakeComplete";
    case ConnectionStateMachineEvent::HandshakeFailed: return "HandshakeFailed";
    case ConnectionStateMachineEvent::CloseRequested: return "CloseRequested";
    case ConnectionStateMachineEvent::ConnectTimeout: return "ConnectTimeout";
    case ConnectionStateMachineEvent::ReconnectRequested: return "ReconnectRequested";
    default: return "Unknown";
  }
}

// ===== ConnectionStatePatterns Implementation =====

bool ConnectionStatePatterns::canRead(ConnectionState state) {
  return state == ConnectionState::Connected ||
         state == ConnectionState::Reading ||
         state == ConnectionState::Idle ||
         state == ConnectionState::HalfClosedLocal;
}

bool ConnectionStatePatterns::canWrite(ConnectionState state) {
  return state == ConnectionState::Connected ||
         state == ConnectionState::Writing ||
         state == ConnectionState::Idle ||
         state == ConnectionState::HalfClosedRemote ||
         state == ConnectionState::Flushing;
}

bool ConnectionStatePatterns::isTerminal(ConnectionState state) {
  return state == ConnectionState::Closed ||
         state == ConnectionState::Error ||
         state == ConnectionState::Aborted;
}

bool ConnectionStatePatterns::isConnecting(ConnectionState state) {
  return state == ConnectionState::Resolving ||
         state == ConnectionState::Connecting ||
         state == ConnectionState::TcpConnected ||
         state == ConnectionState::HandshakeInProgress;
}

bool ConnectionStatePatterns::isConnected(ConnectionState state) {
  return state == ConnectionState::Connected ||
         state == ConnectionState::Reading ||
         state == ConnectionState::Writing ||
         state == ConnectionState::Idle ||
         state == ConnectionState::Processing;
}

bool ConnectionStatePatterns::isClosing(ConnectionState state) {
  return state == ConnectionState::Closing ||
         state == ConnectionState::Draining ||
         state == ConnectionState::Flushing ||
         state == ConnectionState::HalfClosedLocal ||
         state == ConnectionState::HalfClosedRemote;
}

bool ConnectionStatePatterns::canReconnect(ConnectionState state) {
  return (state == ConnectionState::Error ||
          state == ConnectionState::Closed) &&
         !isConnecting(state);
}

}  // namespace network
}  // namespace mcp