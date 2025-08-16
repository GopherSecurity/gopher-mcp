/**
 * @file transport_socket_state_machine.cc
 * @brief Implementation of generic transport socket state machine
 */

#include "mcp/transport/transport_socket_state_machine.h"

#include <algorithm>
#include <sstream>

namespace mcp {
namespace transport {

// =================================================================
// TransportSocketStateMachine Implementation
// =================================================================

TransportSocketStateMachine::TransportSocketStateMachine(
    event::Dispatcher& dispatcher,
    StateMachineConfig config)
    : dispatcher_(dispatcher),
      config_(std::move(config)),
      state_entry_time_(std::chrono::steady_clock::now()) {
  
  // Register default state change callback if provided
  if (config_.state_change_callback) {
    addStateChangeListener(config_.state_change_callback);
  }
}

TransportSocketStateMachine::~TransportSocketStateMachine() {
  cancelStateTimer();
}

StateTransitionResult TransportSocketStateMachine::transitionTo(
    TransportSocketState new_state,
    const std::string& reason,
    CompletionCallback callback) {
  
  assertInDispatcherThread();
  
  // Check for reentrancy
  if (transition_in_progress_) {
    // Schedule the transition instead
    scheduleTransition(new_state, reason, callback);
    return {false, "Transition already in progress", current_state_};
  }
  
  // Validate transition
  if (!isTransitionValid(current_state_, new_state)) {
    std::stringstream ss;
    ss << "Invalid transition from " << static_cast<int>(current_state_.load())
       << " to " << static_cast<int>(new_state);
    
    if (config_.error_callback) {
      config_.error_callback(ss.str());
    }
    
    if (callback) {
      callback(false);
    }
    
    return {false, ss.str(), current_state_};
  }
  
  // Execute the transition
  executeTransition(new_state, reason, callback);
  
  return {true, "", new_state};
}

StateTransitionResult TransportSocketStateMachine::forceTransitionTo(
    TransportSocketState new_state,
    const std::string& reason) {
  
  assertInDispatcherThread();
  
  if (!config_.allow_force_transitions) {
    return {false, "Force transitions not allowed", current_state_};
  }
  
  // Skip validation and execute directly
  executeTransition(new_state, reason + " [FORCED]", nullptr);
  
  return {true, "", new_state};
}

void TransportSocketStateMachine::scheduleTransition(
    TransportSocketState new_state,
    const std::string& reason,
    CompletionCallback callback) {
  
  assertInDispatcherThread();
  
  // Store pending transition
  pending_transition_ = std::make_unique<PendingTransition>(
      PendingTransition{new_state, reason, callback});
  
  // Schedule on next event loop iteration
  dispatcher_.post([this]() {
    if (pending_transition_) {
      auto pending = std::move(pending_transition_);
      transitionTo(pending->state, pending->reason, pending->callback);
    }
  });
}

void TransportSocketStateMachine::handleIoReady(bool readable, bool writable) {
  assertInDispatcherThread();
  
  // Default implementation - subclasses should override
  // Map I/O events to state transitions based on current state
  
  switch (current_state_) {
    case TransportSocketState::Connecting:
      if (writable) {
        // Socket became writable, connection likely succeeded
        handleConnectionResult(true);
      }
      break;
      
    case TransportSocketState::Connected:
    case TransportSocketState::Idle:
      if (readable) {
        transitionTo(TransportSocketState::Reading, "Socket readable", nullptr);
      }
      break;
      
    case TransportSocketState::ReadBlocked:
      if (readable) {
        transitionTo(TransportSocketState::Reading, "Socket readable", nullptr);
      }
      break;
      
    case TransportSocketState::WriteBlocked:
      if (writable) {
        transitionTo(TransportSocketState::Writing, "Socket writable", nullptr);
      }
      break;
      
    default:
      // No default action
      break;
  }
}

void TransportSocketStateMachine::handleConnectionResult(
    bool success, 
    const std::string& error_message) {
  
  assertInDispatcherThread();
  
  if (success) {
    transitionTo(TransportSocketState::TcpConnected, 
                "TCP connection established", nullptr);
  } else {
    // Connection failed
    if (config_.error_callback) {
      config_.error_callback("Connection failed: " + error_message);
    }
    transitionTo(TransportSocketState::Error, 
                "Connection failed: " + error_message, nullptr);
  }
}

void TransportSocketStateMachine::handleHandshakeResult(
    bool success,
    const std::string& error_message) {
  
  assertInDispatcherThread();
  
  if (success) {
    // First transition to HandshakeComplete state as per valid state transitions
    transitionTo(TransportSocketState::HandshakeComplete,
                "Handshake completed successfully", nullptr);
    // Then transition to Connected state
    transitionTo(TransportSocketState::Connected,
                "Ready for data transfer", nullptr);
  } else {
    if (config_.error_callback) {
      config_.error_callback("Handshake failed: " + error_message);
    }
    transitionTo(TransportSocketState::Error,
                "Handshake failed: " + error_message, nullptr);
  }
}

void TransportSocketStateMachine::addStateChangeListener(
    StateChangeCallback callback) {
  assertInDispatcherThread();
  state_change_listeners_.push_back(callback);
}

void TransportSocketStateMachine::clearStateChangeListeners() {
  assertInDispatcherThread();
  state_change_listeners_.clear();
}

void TransportSocketStateMachine::addTransitionValidator(
    ValidationCallback validator) {
  assertInDispatcherThread();
  custom_validators_.push_back(validator);
}

bool TransportSocketStateMachine::isTransitionValid(
    TransportSocketState from, 
    TransportSocketState to) const {
  
  // Check built-in rules first
  if (!checkBuiltinTransitionRules(from, to)) {
    return false;
  }
  
  // Check protocol-specific rules
  auto valid_transitions = getValidTransitions(from);
  if (valid_transitions.find(to) == valid_transitions.end()) {
    return false;
  }
  
  // Check custom validators
  for (const auto& validator : custom_validators_) {
    if (!validator(from, to)) {
      return false;
    }
  }
  
  return true;
}

std::chrono::milliseconds 
TransportSocketStateMachine::getTimeInCurrentState() const {
  auto now = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - state_entry_time_);
  return duration;
}

void TransportSocketStateMachine::startStateTimer(
    std::chrono::milliseconds timeout,
    std::function<void()> timeout_callback) {
  
  assertInDispatcherThread();
  
  cancelStateTimer();
  
  if (timeout.count() > 0) {
    state_timer_ = dispatcher_.createTimer([this, timeout_callback]() {
      if (timeout_callback) {
        timeout_callback();
      }
      onStateTimeout(current_state_);
    });
    state_timer_->enableTimer(timeout);
  }
}

void TransportSocketStateMachine::cancelStateTimer() {
  assertInDispatcherThread();
  
  if (state_timer_) {
    state_timer_->disableTimer();
    state_timer_.reset();
  }
}

void TransportSocketStateMachine::onStateExit(
    TransportSocketState state,
    CompletionCallback callback) {
  // Default implementation - subclasses can override
  if (callback) {
    callback(true);
  }
}

void TransportSocketStateMachine::onStateEnter(
    TransportSocketState state,
    CompletionCallback callback) {
  // Default implementation - subclasses can override
  
  // Start state-specific timers
  switch (state) {
    case TransportSocketState::Connecting:
      if (config_.connect_timeout.count() > 0) {
        startStateTimer(config_.connect_timeout, [this]() {
          transitionTo(TransportSocketState::Error, 
                      "Connection timeout", nullptr);
        });
      }
      break;
      
    case TransportSocketState::HandshakeInProgress:
      if (config_.handshake_timeout.count() > 0) {
        startStateTimer(config_.handshake_timeout, [this]() {
          transitionTo(TransportSocketState::Error,
                      "Handshake timeout", nullptr);
        });
      }
      break;
      
    case TransportSocketState::Idle:
      if (config_.idle_timeout.count() > 0) {
        startStateTimer(config_.idle_timeout, [this]() {
          transitionTo(TransportSocketState::ShuttingDown,
                      "Idle timeout", nullptr);
        });
      }
      break;
      
    case TransportSocketState::ShuttingDown:
      if (config_.shutdown_timeout.count() > 0) {
        startStateTimer(config_.shutdown_timeout, [this]() {
          forceTransitionTo(TransportSocketState::Closed,
                           "Shutdown timeout");
        });
      }
      break;
      
    default:
      // No timer for other states
      break;
  }
  
  if (callback) {
    callback(true);
  }
}

std::unordered_set<TransportSocketState> 
TransportSocketStateMachine::getValidTransitions(
    TransportSocketState from) const {
  
  // Default transition rules - subclasses should override
  std::unordered_set<TransportSocketState> valid;
  
  switch (from) {
    case TransportSocketState::Uninitialized:
      valid = {TransportSocketState::Initialized, 
               TransportSocketState::Error};
      break;
      
    case TransportSocketState::Initialized:
      valid = {TransportSocketState::Connecting,
               TransportSocketState::Error,
               TransportSocketState::Closed};
      break;
      
    case TransportSocketState::Connecting:
      valid = {TransportSocketState::TcpConnected,
               TransportSocketState::Error,
               TransportSocketState::Closed};
      break;
      
    case TransportSocketState::TcpConnected:
      valid = {TransportSocketState::HandshakeInProgress,
               TransportSocketState::Connected,  // For protocols without handshake
               TransportSocketState::Error,
               TransportSocketState::Closed};
      break;
      
    case TransportSocketState::HandshakeInProgress:
      valid = {TransportSocketState::HandshakeComplete,
               TransportSocketState::Error,
               TransportSocketState::Closed};
      break;
      
    case TransportSocketState::HandshakeComplete:
      valid = {TransportSocketState::Connected,
               TransportSocketState::Error,
               TransportSocketState::Closed};
      break;
      
    case TransportSocketState::Connected:
      valid = {TransportSocketState::Reading,
               TransportSocketState::Writing,
               TransportSocketState::Idle,
               TransportSocketState::ShuttingDown,
               TransportSocketState::Error,
               TransportSocketState::Closed};
      break;
      
    case TransportSocketState::Reading:
      valid = {TransportSocketState::Connected,
               TransportSocketState::Idle,
               TransportSocketState::ReadBlocked,
               TransportSocketState::Writing,
               TransportSocketState::ShuttingDown,
               TransportSocketState::Error,
               TransportSocketState::Closed};
      break;
      
    case TransportSocketState::Writing:
      valid = {TransportSocketState::Connected,
               TransportSocketState::Idle,
               TransportSocketState::WriteBlocked,
               TransportSocketState::Reading,
               TransportSocketState::ShuttingDown,
               TransportSocketState::Error,
               TransportSocketState::Closed};
      break;
      
    case TransportSocketState::Idle:
      valid = {TransportSocketState::Reading,
               TransportSocketState::Writing,
               TransportSocketState::Connected,
               TransportSocketState::ShuttingDown,
               TransportSocketState::Error,
               TransportSocketState::Closed};
      break;
      
    case TransportSocketState::ReadBlocked:
      valid = {TransportSocketState::Reading,
               TransportSocketState::ShuttingDown,
               TransportSocketState::Error,
               TransportSocketState::Closed};
      break;
      
    case TransportSocketState::WriteBlocked:
      valid = {TransportSocketState::Writing,
               TransportSocketState::ShuttingDown,
               TransportSocketState::Error,
               TransportSocketState::Closed};
      break;
      
    case TransportSocketState::ShuttingDown:
      valid = {TransportSocketState::ShutdownComplete,
               TransportSocketState::Closed,
               TransportSocketState::Error};
      break;
      
    case TransportSocketState::ShutdownComplete:
      valid = {TransportSocketState::Closed};
      break;
      
    case TransportSocketState::Closed:
      // Terminal state - normally no transitions allowed
      // But allow transition to Initialized for connection reuse
      valid = {TransportSocketState::Initialized};
      break;
      
    case TransportSocketState::Error:
      // Error state - allow recovery or closure
      valid = {TransportSocketState::Initialized,  // For retry
               TransportSocketState::Closed};
      break;
  }
  
  return valid;
}

void TransportSocketStateMachine::onStateTimeout(TransportSocketState state) {
  // Default implementation - subclasses can override
  // Already handled in timer callbacks above
}

void TransportSocketStateMachine::executeTransition(
    TransportSocketState new_state,
    const std::string& reason,
    CompletionCallback callback) {
  
  assertInDispatcherThread();
  
  // Mark transition in progress
  transition_in_progress_ = true;
  
  TransportSocketState old_state = current_state_;
  
  // Create state change event
  StateChangeEvent event{
    old_state,
    new_state,
    std::chrono::steady_clock::now(),
    reason
  };
  
  // Execute exit action for current state
  onStateExit(old_state, [this, new_state, event, callback](bool exit_success) {
    if (!exit_success) {
      transition_in_progress_ = false;
      if (callback) {
        callback(false);
      }
      return;
    }
    
    // Update state atomically
    current_state_ = new_state;
    state_entry_time_ = std::chrono::steady_clock::now();
    total_transitions_++;
    
    // Track error recoveries
    if (event.from_state == TransportSocketState::Error &&
        event.to_state != TransportSocketState::Closed) {
      error_recoveries_++;
    }
    
    // Cancel any active timer from previous state
    cancelStateTimer();
    
    // Record and notify
    recordStateChange(event);
    notifyStateChange(event);
    
    // Execute entry action for new state
    onStateEnter(new_state, [this, callback](bool enter_success) {
      transition_in_progress_ = false;
      
      if (callback) {
        callback(enter_success);
      }
    });
  });
}

void TransportSocketStateMachine::notifyStateChange(
    const StateChangeEvent& event) {
  for (const auto& listener : state_change_listeners_) {
    listener(event);
  }
}

void TransportSocketStateMachine::recordStateChange(
    const StateChangeEvent& event) {
  state_history_.push_back(event);
  
  // Trim history if needed
  while (state_history_.size() > config_.max_state_history) {
    state_history_.pop_front();
  }
}

bool TransportSocketStateMachine::checkBuiltinTransitionRules(
    TransportSocketState from,
    TransportSocketState to) const {
  
  // Basic sanity checks
  
  // Can't transition to same state (use different mechanism for that)
  if (from == to) {
    return false;
  }
  
  // Can't transition to Uninitialized (except from Error/Closed for reset)
  if (to == TransportSocketState::Uninitialized &&
      from != TransportSocketState::Error &&
      from != TransportSocketState::Closed) {
    return false;
  }
  
  // Check error recovery limit
  if (from == TransportSocketState::Error &&
      to != TransportSocketState::Closed &&
      error_recoveries_ >= config_.max_error_recoveries) {
    return false;
  }
  
  return true;
}

// =================================================================
// StateMachineTestHelper Implementation
// =================================================================

bool StateMachineTestHelper::validateTransitionMatrix(
    const TransportSocketStateMachine& machine) {
  
  // Check that every non-terminal state has at least one valid exit
  for (int i = 0; i <= static_cast<int>(TransportSocketState::Error); ++i) {
    auto state = static_cast<TransportSocketState>(i);
    
    // Terminal states don't need exits
    if (state == TransportSocketState::Closed) {
      continue;
    }
    
    auto valid_transitions = machine.getValidTransitions(state);
    if (valid_transitions.empty()) {
      return false;  // Dead-end state found
    }
  }
  
  return true;
}

std::string StateMachineTestHelper::generateStateDiagram(
    const TransportSocketStateMachine& machine) {
  
  std::stringstream ss;
  ss << "digraph TransportSocketStateMachine {\n";
  ss << "  rankdir=TB;\n";
  ss << "  node [shape=box];\n\n";
  
  // Define states with colors
  ss << "  // State definitions\n";
  ss << "  Uninitialized [fillcolor=lightgray, style=filled];\n";
  ss << "  Connected [fillcolor=lightgreen, style=filled];\n";
  ss << "  Error [fillcolor=lightcoral, style=filled];\n";
  ss << "  Closed [fillcolor=gray, style=filled];\n\n";
  
  // Generate transitions
  ss << "  // Transitions\n";
  for (int i = 0; i <= static_cast<int>(TransportSocketState::Error); ++i) {
    auto from = static_cast<TransportSocketState>(i);
    auto valid_transitions = machine.getValidTransitions(from);
    
    for (auto to : valid_transitions) {
      ss << "  " << static_cast<int>(from) << " -> " 
         << static_cast<int>(to) << ";\n";
    }
  }
  
  ss << "}\n";
  return ss.str();
}

bool StateMachineTestHelper::runCommonScenarios(
    TransportSocketStateMachine& machine) {
  
  // Scenario 1: Normal connection flow
  auto result = machine.transitionTo(TransportSocketState::Initialized, 
                                     "Init", nullptr);
  if (!result.success) return false;
  
  result = machine.transitionTo(TransportSocketState::Connecting,
                                "Connect", nullptr);
  if (!result.success) return false;
  
  machine.handleConnectionResult(true);
  if (machine.currentState() != TransportSocketState::TcpConnected) {
    return false;
  }
  
  // Reset for next scenario
  machine.forceTransitionTo(TransportSocketState::Closed, "Reset");
  
  // Scenario 2: Connection failure
  machine.transitionTo(TransportSocketState::Initialized, "Init", nullptr);
  machine.transitionTo(TransportSocketState::Connecting, "Connect", nullptr);
  machine.handleConnectionResult(false, "Connection refused");
  
  if (machine.currentState() != TransportSocketState::Error) {
    return false;
  }
  
  return true;
}

}  // namespace transport
}  // namespace mcp