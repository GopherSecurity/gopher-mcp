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
const std::unordered_map<ConnectionMachineState, std::unordered_set<ConnectionMachineState>>&
getValidTransitions() {
  static const std::unordered_map<ConnectionMachineState, std::unordered_set<ConnectionMachineState>>
      transitions = {
          // Initial states
          {ConnectionMachineState::Uninitialized,
           {ConnectionMachineState::Initialized, ConnectionMachineState::Error}},
          
          {ConnectionMachineState::Initialized,
           {ConnectionMachineState::Resolving, ConnectionMachineState::Connecting,
            ConnectionMachineState::Listening, ConnectionMachineState::Closed, ConnectionMachineState::Error}},
          
          // Client connecting states
          {ConnectionMachineState::Resolving,
           {ConnectionMachineState::Connecting, ConnectionMachineState::Error,
            ConnectionMachineState::WaitingToReconnect}},
          
          {ConnectionMachineState::Connecting,
           {ConnectionMachineState::TcpConnected, ConnectionMachineState::Error,
            ConnectionMachineState::WaitingToReconnect, ConnectionMachineState::Closed}},
          
          {ConnectionMachineState::TcpConnected,
           {ConnectionMachineState::HandshakeInProgress, ConnectionMachineState::Connected,
            ConnectionMachineState::Error, ConnectionMachineState::Closing}},
          
          {ConnectionMachineState::HandshakeInProgress,
           {ConnectionMachineState::Connected, ConnectionMachineState::Error,
            ConnectionMachineState::Closing, ConnectionMachineState::WaitingToReconnect}},
          
          // Server accepting states
          {ConnectionMachineState::Listening,
           {ConnectionMachineState::Accepting, ConnectionMachineState::Closed,
            ConnectionMachineState::Error}},
          
          {ConnectionMachineState::Accepting,
           {ConnectionMachineState::Accepted, ConnectionMachineState::Listening,
            ConnectionMachineState::Error}},
          
          {ConnectionMachineState::Accepted,
           {ConnectionMachineState::HandshakeInProgress, ConnectionMachineState::Connected,
            ConnectionMachineState::Error, ConnectionMachineState::Closing}},
          
          // Connected states
          {ConnectionMachineState::Connected,
           {ConnectionMachineState::Reading, ConnectionMachineState::Writing,
            ConnectionMachineState::Idle, ConnectionMachineState::Processing,
            ConnectionMachineState::ReadDisabled, ConnectionMachineState::WriteDisabled,
            ConnectionMachineState::HalfClosedLocal, ConnectionMachineState::HalfClosedRemote,
            ConnectionMachineState::Closing, ConnectionMachineState::Error}},
          
          {ConnectionMachineState::Reading,
           {ConnectionMachineState::Connected, ConnectionMachineState::Processing,
            ConnectionMachineState::Writing, ConnectionMachineState::Idle,
            ConnectionMachineState::ReadDisabled, ConnectionMachineState::HalfClosedRemote,
            ConnectionMachineState::Closing, ConnectionMachineState::Error}},
          
          {ConnectionMachineState::Writing,
           {ConnectionMachineState::Connected, ConnectionMachineState::Reading,
            ConnectionMachineState::Idle, ConnectionMachineState::WriteDisabled,
            ConnectionMachineState::Flushing, ConnectionMachineState::HalfClosedLocal,
            ConnectionMachineState::Closing, ConnectionMachineState::Error}},
          
          {ConnectionMachineState::Idle,
           {ConnectionMachineState::Reading, ConnectionMachineState::Writing,
            ConnectionMachineState::Connected, ConnectionMachineState::Closing,
            ConnectionMachineState::Error}},
          
          {ConnectionMachineState::Processing,
           {ConnectionMachineState::Connected, ConnectionMachineState::Reading,
            ConnectionMachineState::Writing, ConnectionMachineState::Idle,
            ConnectionMachineState::Closing, ConnectionMachineState::Error}},
          
          // Flow control states
          {ConnectionMachineState::ReadDisabled,
           {ConnectionMachineState::Connected, ConnectionMachineState::Writing,
            ConnectionMachineState::Paused, ConnectionMachineState::Closing,
            ConnectionMachineState::Error}},
          
          {ConnectionMachineState::WriteDisabled,
           {ConnectionMachineState::Connected, ConnectionMachineState::Reading,
            ConnectionMachineState::Paused, ConnectionMachineState::Closing,
            ConnectionMachineState::Error}},
          
          {ConnectionMachineState::Paused,
           {ConnectionMachineState::Connected, ConnectionMachineState::ReadDisabled,
            ConnectionMachineState::WriteDisabled, ConnectionMachineState::Closing,
            ConnectionMachineState::Error}},
          
          // Closing states
          {ConnectionMachineState::HalfClosedLocal,
           {ConnectionMachineState::Reading, ConnectionMachineState::Closing,
            ConnectionMachineState::Closed, ConnectionMachineState::Error}},
          
          {ConnectionMachineState::HalfClosedRemote,
           {ConnectionMachineState::Writing, ConnectionMachineState::Flushing,
            ConnectionMachineState::Closing, ConnectionMachineState::Closed,
            ConnectionMachineState::Error}},
          
          {ConnectionMachineState::Closing,
           {ConnectionMachineState::Draining, ConnectionMachineState::Flushing,
            ConnectionMachineState::Closed, ConnectionMachineState::Error}},
          
          {ConnectionMachineState::Draining,
           {ConnectionMachineState::Flushing, ConnectionMachineState::Closed,
            ConnectionMachineState::Error}},
          
          {ConnectionMachineState::Flushing,
           {ConnectionMachineState::Closed, ConnectionMachineState::Error}},
          
          // Terminal states
          {ConnectionMachineState::Closed,
           {ConnectionMachineState::Initialized, ConnectionMachineState::WaitingToReconnect}},
          
          {ConnectionMachineState::Error,
           {ConnectionMachineState::Closed, ConnectionMachineState::WaitingToReconnect,
            ConnectionMachineState::Recovering}},
          
          {ConnectionMachineState::Aborted,
           {ConnectionMachineState::Closed, ConnectionMachineState::WaitingToReconnect}},
          
          // Recovery states
          {ConnectionMachineState::Reconnecting,
           {ConnectionMachineState::Resolving, ConnectionMachineState::Connecting,
            ConnectionMachineState::Error, ConnectionMachineState::WaitingToReconnect}},
          
          {ConnectionMachineState::WaitingToReconnect,
           {ConnectionMachineState::Reconnecting, ConnectionMachineState::Closed,
            ConnectionMachineState::Error}},
          
          {ConnectionMachineState::Recovering,
           {ConnectionMachineState::Connected, ConnectionMachineState::Error,
            ConnectionMachineState::Closed}}
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
    current_state_ = ConnectionMachineState::Initialized;
  } else {
    current_state_ = ConnectionMachineState::Uninitialized;
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

void ConnectionStateMachine::forceTransition(ConnectionMachineState new_state,
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
  if (transitionTo(ConnectionMachineState::Listening,
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
  
  ConnectionMachineState target_state;
  
  switch (type) {
    case ConnectionCloseType::FlushWrite:
      target_state = ConnectionMachineState::Flushing;
      break;
    case ConnectionCloseType::NoFlush:
      target_state = ConnectionMachineState::Closing;
      break;
    case ConnectionCloseType::FlushWriteAndDelay:
      target_state = ConnectionMachineState::Draining;
      startDrainTimer();
      break;
    default:
      target_state = ConnectionMachineState::Closing;
  }
  
  transitionTo(target_state,
              ConnectionStateMachineEvent::CloseRequested,
              "Close requested");
}

void ConnectionStateMachine::reset() {
  assertInDispatcherThread();
  
  forceTransition(ConnectionMachineState::Aborted, "Reset requested");
  connection_.close(ConnectionCloseType::NoFlush);
}

// ===== State Transition Logic =====

bool ConnectionStateMachine::transitionTo(
    ConnectionMachineState new_state,
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
    ConnectionMachineState from,
    ConnectionMachineState to,
    ConnectionStateMachineEvent event) const {
  
  const auto& transitions = getValidTransitions();
  auto it = transitions.find(from);
  
  if (it == transitions.end()) {
    return false;
  }
  
  return it->second.find(to) != it->second.end();
}

std::unordered_set<ConnectionMachineState>
ConnectionStateMachine::getValidNextStates() const {
  const auto& transitions = getValidTransitions();
  auto it = transitions.find(current_state_.load());
  
  if (it == transitions.end()) {
    return {};
  }
  
  return it->second;
}

void ConnectionStateMachine::onStateEnter(ConnectionMachineState state) {
  // State-specific entry actions
  switch (state) {
    case ConnectionMachineState::Connecting:
      startConnectTimer();
      break;
      
    case ConnectionMachineState::HandshakeInProgress:
      startHandshakeTimer();
      break;
      
    case ConnectionMachineState::Connected:
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
      
    case ConnectionMachineState::Draining:
      startDrainTimer();
      break;
      
    case ConnectionMachineState::WaitingToReconnect:
      startReconnectTimer();
      break;
      
    case ConnectionMachineState::Error:
      consecutive_errors_++;
      if (config_.enable_auto_reconnect &&
          reconnect_attempts_ < config_.max_reconnect_attempts) {
        initiateReconnection();
      }
      break;
      
    case ConnectionMachineState::Closed:
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

void ConnectionStateMachine::onStateExit(ConnectionMachineState state) {
  // State-specific exit actions
  switch (state) {
    case ConnectionMachineState::Connecting:
      if (connect_timer_) {
        connect_timer_->disableTimer();
      }
      break;
      
    case ConnectionMachineState::HandshakeInProgress:
      if (handshake_timer_) {
        handshake_timer_->disableTimer();
      }
      break;
      
    case ConnectionMachineState::Connected:
    case ConnectionMachineState::Idle:
      if (idle_timer_) {
        idle_timer_->disableTimer();
      }
      break;
      
    case ConnectionMachineState::Draining:
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
      if (current == ConnectionMachineState::Connecting) {
        transitionTo(ConnectionMachineState::TcpConnected, event,
                    "TCP connection established");
        
        // For now, assume no handshake needed
        // TODO: Add transport socket handshake detection when available
        transitionTo(ConnectionMachineState::Connected, event,
                    "Connection fully established");
      }
      break;
      
    case ConnectionStateMachineEvent::SocketError:
      transitionTo(ConnectionMachineState::Error, event, "Socket error");
      break;
      
    case ConnectionStateMachineEvent::SocketClosed:
      if (!ConnectionStatePatterns::isTerminal(current)) {
        transitionTo(ConnectionMachineState::Closed, event, "Socket closed");
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
      if (current == ConnectionMachineState::Connected ||
          current == ConnectionMachineState::Idle) {
        transitionTo(ConnectionMachineState::Reading, event, "Read ready");
      }
      break;
      
    case ConnectionStateMachineEvent::WriteReady:
      if (current == ConnectionMachineState::Connected ||
          current == ConnectionMachineState::Idle) {
        transitionTo(ConnectionMachineState::Writing, event, "Write ready");
      }
      break;
      
    case ConnectionStateMachineEvent::EndOfStream:
      if (config_.enable_half_close) {
        transitionTo(ConnectionMachineState::HalfClosedRemote, event,
                    "Remote end of stream");
      } else {
        transitionTo(ConnectionMachineState::Closing, event,
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
      transitionTo(ConnectionMachineState::HandshakeInProgress, event,
                  "Transport handshake started");
      break;
      
    case ConnectionStateMachineEvent::HandshakeComplete:
      transitionTo(ConnectionMachineState::Connected, event,
                  "Transport handshake complete");
      break;
      
    case ConnectionStateMachineEvent::HandshakeFailed:
      transitionTo(ConnectionMachineState::Error, event,
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
      if (current == ConnectionMachineState::Uninitialized ||
          current == ConnectionMachineState::Initialized) {
        transitionTo(ConnectionMachineState::Connecting, event,
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
          transitionTo(ConnectionMachineState::ReadDisabled, event,
                      "Read disabled");
        }
      }
      break;
      
    case ConnectionStateMachineEvent::WriteDisableRequested:
      if (ConnectionStatePatterns::canWrite(current)) {
        write_disable_count_++;
        if (write_disable_count_ == 1) {
          transitionTo(ConnectionMachineState::WriteDisabled, event,
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
      transitionTo(ConnectionMachineState::Error, event, "Connect timeout");
      break;
      
    case ConnectionStateMachineEvent::IdleTimeout:
      transitionTo(ConnectionMachineState::Closing, event, "Idle timeout");
      break;
      
    case ConnectionStateMachineEvent::DrainTimeout:
      transitionTo(ConnectionMachineState::Closed, event, "Drain timeout");
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
    transitionTo(ConnectionMachineState::Closed,
                ConnectionStateMachineEvent::RecoveryFailed,
                "Max reconnection attempts reached");
    return;
  }
  
  reconnect_attempts_++;
  
  // Calculate backoff delay
  auto next_delay = std::chrono::duration_cast<std::chrono::milliseconds>(
      current_reconnect_delay_ * config_.reconnect_backoff_multiplier);
  current_reconnect_delay_ = std::min(next_delay, config_.max_reconnect_delay);
  
  transitionTo(ConnectionMachineState::WaitingToReconnect,
              ConnectionStateMachineEvent::ReconnectRequested,
              "Waiting to reconnect");
}

void ConnectionStateMachine::handleReconnectTimeout() {
  transitionTo(ConnectionMachineState::Reconnecting,
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
  
  transitionTo(ConnectionMachineState::Error,
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
      transitionTo(ConnectionMachineState::WriteDisabled,
                  ConnectionStateMachineEvent::WriteDisableRequested,
                  "Write buffer above high watermark");
    }
  }
}

void ConnectionStateMachine::onBelowWriteBufferLowWatermark() {
  auto current = current_state_.load();
  if (current == ConnectionMachineState::WriteDisabled && write_disable_count_ > 0) {
    write_disable_count_--;
    if (write_disable_count_ == 0) {
      transitionTo(ConnectionMachineState::Connected,
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

std::string ConnectionStateMachine::getStateName(ConnectionMachineState state) {
  switch (state) {
    case ConnectionMachineState::Uninitialized: return "Uninitialized";
    case ConnectionMachineState::Initialized: return "Initialized";
    case ConnectionMachineState::Resolving: return "Resolving";
    case ConnectionMachineState::Connecting: return "Connecting";
    case ConnectionMachineState::TcpConnected: return "TcpConnected";
    case ConnectionMachineState::HandshakeInProgress: return "HandshakeInProgress";
    case ConnectionMachineState::Listening: return "Listening";
    case ConnectionMachineState::Accepting: return "Accepting";
    case ConnectionMachineState::Accepted: return "Accepted";
    case ConnectionMachineState::Connected: return "Connected";
    case ConnectionMachineState::Reading: return "Reading";
    case ConnectionMachineState::Writing: return "Writing";
    case ConnectionMachineState::Idle: return "Idle";
    case ConnectionMachineState::Processing: return "Processing";
    case ConnectionMachineState::ReadDisabled: return "ReadDisabled";
    case ConnectionMachineState::WriteDisabled: return "WriteDisabled";
    case ConnectionMachineState::Paused: return "Paused";
    case ConnectionMachineState::HalfClosedLocal: return "HalfClosedLocal";
    case ConnectionMachineState::HalfClosedRemote: return "HalfClosedRemote";
    case ConnectionMachineState::Closing: return "Closing";
    case ConnectionMachineState::Draining: return "Draining";
    case ConnectionMachineState::Flushing: return "Flushing";
    case ConnectionMachineState::Closed: return "Closed";
    case ConnectionMachineState::Error: return "Error";
    case ConnectionMachineState::Aborted: return "Aborted";
    case ConnectionMachineState::Reconnecting: return "Reconnecting";
    case ConnectionMachineState::WaitingToReconnect: return "WaitingToReconnect";
    case ConnectionMachineState::Recovering: return "Recovering";
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

bool ConnectionStatePatterns::canRead(ConnectionMachineState state) {
  return state == ConnectionMachineState::Connected ||
         state == ConnectionMachineState::Reading ||
         state == ConnectionMachineState::Idle ||
         state == ConnectionMachineState::HalfClosedLocal;
}

bool ConnectionStatePatterns::canWrite(ConnectionMachineState state) {
  return state == ConnectionMachineState::Connected ||
         state == ConnectionMachineState::Writing ||
         state == ConnectionMachineState::Idle ||
         state == ConnectionMachineState::HalfClosedRemote ||
         state == ConnectionMachineState::Flushing;
}

bool ConnectionStatePatterns::isTerminal(ConnectionMachineState state) {
  return state == ConnectionMachineState::Closed ||
         state == ConnectionMachineState::Error ||
         state == ConnectionMachineState::Aborted;
}

bool ConnectionStatePatterns::isConnecting(ConnectionMachineState state) {
  return state == ConnectionMachineState::Resolving ||
         state == ConnectionMachineState::Connecting ||
         state == ConnectionMachineState::TcpConnected ||
         state == ConnectionMachineState::HandshakeInProgress;
}

bool ConnectionStatePatterns::isConnected(ConnectionMachineState state) {
  return state == ConnectionMachineState::Connected ||
         state == ConnectionMachineState::Reading ||
         state == ConnectionMachineState::Writing ||
         state == ConnectionMachineState::Idle ||
         state == ConnectionMachineState::Processing;
}

bool ConnectionStatePatterns::isClosing(ConnectionMachineState state) {
  return state == ConnectionMachineState::Closing ||
         state == ConnectionMachineState::Draining ||
         state == ConnectionMachineState::Flushing ||
         state == ConnectionMachineState::HalfClosedLocal ||
         state == ConnectionMachineState::HalfClosedRemote;
}

bool ConnectionStatePatterns::canReconnect(ConnectionMachineState state) {
  return (state == ConnectionMachineState::Error ||
          state == ConnectionMachineState::Closed) &&
         !isConnecting(state);
}

}  // namespace network
}  // namespace mcp