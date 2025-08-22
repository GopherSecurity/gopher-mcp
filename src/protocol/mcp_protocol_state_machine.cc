/**
 * MCP Application Protocol State Machine Implementation
 */

#include "mcp/protocol/mcp_protocol_state_machine.h"
#include <iostream>
#include <sstream>

namespace mcp {
namespace protocol {

McpProtocolStateMachine::McpProtocolStateMachine(
    event::Dispatcher& dispatcher,
    const McpProtocolStateMachineConfig& config)
    : config_(config),
      dispatcher_(dispatcher),
      state_entry_time_(std::chrono::steady_clock::now()) {}

McpProtocolStateMachine::~McpProtocolStateMachine() {
  cancelStateTimer();
}

bool McpProtocolStateMachine::handleEvent(McpProtocolEvent event,
                                          const optional<std::string>& reason) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  
  McpProtocolState current = current_state_.load();
  McpProtocolState new_state = current;
  
  // Determine new state based on event and current state
  switch (current) {
    case McpProtocolState::DISCONNECTED:
      if (event == McpProtocolEvent::CONNECT_REQUESTED) {
        new_state = McpProtocolState::CONNECTING;
      } else if (event == McpProtocolEvent::NETWORK_CONNECTED) {
        // Direct connection without explicit request
        new_state = McpProtocolState::CONNECTED;
      }
      break;
      
    case McpProtocolState::CONNECTING:
      if (event == McpProtocolEvent::NETWORK_CONNECTED) {
        new_state = McpProtocolState::CONNECTED;
      } else if (event == McpProtocolEvent::NETWORK_DISCONNECTED ||
                 event == McpProtocolEvent::TIMEOUT) {
        new_state = McpProtocolState::DISCONNECTED;
      } else if (event == McpProtocolEvent::PROTOCOL_ERROR) {
        new_state = McpProtocolState::ERROR;
      }
      break;
      
    case McpProtocolState::CONNECTED:
      if (event == McpProtocolEvent::INITIALIZE_REQUESTED ||
          event == McpProtocolEvent::INITIALIZE_SENT) {
        new_state = McpProtocolState::INITIALIZING;
      } else if (event == McpProtocolEvent::NETWORK_DISCONNECTED) {
        new_state = McpProtocolState::DISCONNECTED;
      } else if (event == McpProtocolEvent::PROTOCOL_ERROR) {
        new_state = McpProtocolState::ERROR;
      }
      break;
      
    case McpProtocolState::INITIALIZING:
      if (event == McpProtocolEvent::INITIALIZED ||
          event == McpProtocolEvent::INITIALIZE_RECEIVED) {
        new_state = McpProtocolState::READY;
      } else if (event == McpProtocolEvent::TIMEOUT) {
        new_state = McpProtocolState::ERROR;
      } else if (event == McpProtocolEvent::NETWORK_DISCONNECTED) {
        new_state = McpProtocolState::DISCONNECTED;
      } else if (event == McpProtocolEvent::PROTOCOL_ERROR) {
        new_state = McpProtocolState::ERROR;
      }
      break;
      
    case McpProtocolState::READY:
      if (event == McpProtocolEvent::SHUTDOWN_REQUESTED) {
        new_state = McpProtocolState::DRAINING;
      } else if (event == McpProtocolEvent::NETWORK_DISCONNECTED) {
        if (config_.auto_reconnect && reconnect_attempts_ < config_.max_reconnect_attempts) {
          new_state = McpProtocolState::CONNECTING;
          reconnect_attempts_++;
        } else {
          new_state = McpProtocolState::DISCONNECTED;
        }
      } else if (event == McpProtocolEvent::PROTOCOL_ERROR) {
        new_state = McpProtocolState::ERROR;
      }
      break;
      
    case McpProtocolState::DRAINING:
      if (event == McpProtocolEvent::DRAIN_COMPLETE ||
          event == McpProtocolEvent::TIMEOUT) {
        new_state = McpProtocolState::CLOSED;
      } else if (event == McpProtocolEvent::NETWORK_DISCONNECTED) {
        new_state = McpProtocolState::CLOSED;
      }
      break;
      
    case McpProtocolState::ERROR:
      if (event == McpProtocolEvent::RECONNECT_REQUESTED) {
        if (reconnect_attempts_ < config_.max_reconnect_attempts) {
          new_state = McpProtocolState::CONNECTING;
          reconnect_attempts_++;
        }
      } else if (event == McpProtocolEvent::SHUTDOWN_REQUESTED) {
        new_state = McpProtocolState::CLOSED;
      }
      break;
      
    case McpProtocolState::CLOSED:
      if (event == McpProtocolEvent::CONNECT_REQUESTED) {
        // Allow reconnection from closed state
        reconnect_attempts_ = 0;
        new_state = McpProtocolState::CONNECTING;
      }
      break;
  }
  
  // Check if transition is valid and perform it
  if (new_state != current) {
    if (isTransitionValid(current, new_state, event)) {
      transitionTo(new_state, event, reason);
      return true;
    }
  }
  
  return false;
}

void McpProtocolStateMachine::handleError(const Error& error) {
  std::lock_guard<std::mutex> lock(error_mutex_);
  last_error_ = error;
  
  // Trigger error event
  handleEvent(McpProtocolEvent::PROTOCOL_ERROR, error.message);
}

void McpProtocolStateMachine::reset() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  
  cancelStateTimer();
  current_state_ = McpProtocolState::DISCONNECTED;
  reconnect_attempts_ = 0;
  last_error_ = nullopt;
  state_entry_time_ = std::chrono::steady_clock::now();
}

std::chrono::milliseconds McpProtocolStateMachine::getTimeInCurrentState() const {
  auto now = std::chrono::steady_clock::now();
  auto duration = now - state_entry_time_;
  return std::chrono::duration_cast<std::chrono::milliseconds>(duration);
}

optional<Error> McpProtocolStateMachine::getLastError() const {
  std::lock_guard<std::mutex> lock(error_mutex_);
  return last_error_;
}

bool McpProtocolStateMachine::isTransitionValid(McpProtocolState from,
                                                McpProtocolState to,
                                                McpProtocolEvent event) const {
  // All transitions handled in handleEvent are valid
  // This method can be extended with additional validation logic
  return true;
}

void McpProtocolStateMachine::transitionTo(McpProtocolState new_state,
                                          McpProtocolEvent event,
                                          const optional<std::string>& reason) {
  McpProtocolState old_state = current_state_.load();
  
  // Exit current state
  onExitState(old_state);
  
  // Update state
  current_state_ = new_state;
  state_entry_time_ = std::chrono::steady_clock::now();
  
  // Enter new state
  onEnterState(new_state);
  
  // Notify callback
  if (config_.state_change_callback) {
    ProtocolStateTransitionContext context;
    context.from_state = old_state;
    context.to_state = new_state;
    context.trigger_event = event;
    context.timestamp = state_entry_time_;
    context.reason = reason;
    context.error = last_error_;
    
    config_.state_change_callback(context);
  }
  
  std::cerr << "[PROTOCOL] State transition: " 
            << stateToString(old_state) << " -> " 
            << stateToString(new_state) 
            << " (event: " << eventToString(event) << ")"
            << (reason.has_value() ? " reason: " + reason.value() : "")
            << std::endl;
}

void McpProtocolStateMachine::startStateTimer(std::chrono::milliseconds timeout) {
  cancelStateTimer();
  
  // Create timer directly - following pattern from other state machines
  // This should be called from dispatcher thread context (from handleEvent)
  state_timer_ = dispatcher_.createTimer([this]() {
    handleStateTimeout();
  });
  state_timer_->enableTimer(timeout);
}

void McpProtocolStateMachine::cancelStateTimer() {
  if (state_timer_) {
    state_timer_->disableTimer();
    state_timer_.reset();
  }
}

void McpProtocolStateMachine::handleStateTimeout() {
  std::cerr << "[PROTOCOL] State timeout in state: " 
            << stateToString(current_state_) << std::endl;
  
  // Handle timeout based on current state
  handleEvent(McpProtocolEvent::TIMEOUT, "State timeout");
  
  // Notify error callback
  if (config_.error_callback) {
    Error error;
    error.code = -1;
    error.message = "Protocol state timeout in " + stateToString(current_state_);
    config_.error_callback(error);
  }
}

void McpProtocolStateMachine::onEnterState(McpProtocolState state) {
  // Start timers based on state
  switch (state) {
    case McpProtocolState::CONNECTING:
      startStateTimer(config_.connection_timeout);
      break;
      
    case McpProtocolState::INITIALIZING:
      startStateTimer(config_.initialization_timeout);
      break;
      
    case McpProtocolState::DRAINING:
      startStateTimer(config_.drain_timeout);
      break;
      
    default:
      // No timeout for other states
      break;
  }
}

void McpProtocolStateMachine::onExitState(McpProtocolState state) {
  // Cancel any active timer
  cancelStateTimer();
  
  // Clear error when leaving error state
  if (state == McpProtocolState::ERROR) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = nullopt;
  }
}

std::string McpProtocolStateMachine::stateToString(McpProtocolState state) {
  switch (state) {
    case McpProtocolState::DISCONNECTED: return "DISCONNECTED";
    case McpProtocolState::CONNECTING: return "CONNECTING";
    case McpProtocolState::CONNECTED: return "CONNECTED";
    case McpProtocolState::INITIALIZING: return "INITIALIZING";
    case McpProtocolState::READY: return "READY";
    case McpProtocolState::DRAINING: return "DRAINING";
    case McpProtocolState::ERROR: return "ERROR";
    case McpProtocolState::CLOSED: return "CLOSED";
    default: return "UNKNOWN";
  }
}

std::string McpProtocolStateMachine::eventToString(McpProtocolEvent event) {
  switch (event) {
    case McpProtocolEvent::CONNECT_REQUESTED: return "CONNECT_REQUESTED";
    case McpProtocolEvent::NETWORK_CONNECTED: return "NETWORK_CONNECTED";
    case McpProtocolEvent::NETWORK_DISCONNECTED: return "NETWORK_DISCONNECTED";
    case McpProtocolEvent::INITIALIZE_REQUESTED: return "INITIALIZE_REQUESTED";
    case McpProtocolEvent::INITIALIZE_SENT: return "INITIALIZE_SENT";
    case McpProtocolEvent::INITIALIZE_RECEIVED: return "INITIALIZE_RECEIVED";
    case McpProtocolEvent::INITIALIZED: return "INITIALIZED";
    case McpProtocolEvent::SHUTDOWN_REQUESTED: return "SHUTDOWN_REQUESTED";
    case McpProtocolEvent::DRAIN_COMPLETE: return "DRAIN_COMPLETE";
    case McpProtocolEvent::PROTOCOL_ERROR: return "PROTOCOL_ERROR";
    case McpProtocolEvent::TIMEOUT: return "TIMEOUT";
    case McpProtocolEvent::RECONNECT_REQUESTED: return "RECONNECT_REQUESTED";
    default: return "UNKNOWN";
  }
}

} // namespace protocol
} // namespace mcp