/**
 * MCP Application Protocol State Machine
 * 
 * Manages the application-level protocol state for MCP clients and servers.
 * This is separate from the network connection state machine and tracks
 * the MCP protocol lifecycle (initialization, capability negotiation, etc).
 * 
 * Following production patterns:
 * - Clear state transitions with validation
 * - Event-driven architecture
 * - Thread-safe operations
 * - Comprehensive error handling
 */

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "mcp/types.h"
#include "mcp/core/compat.h"
#include "mcp/event/event_loop.h"

namespace mcp {
namespace protocol {

/**
 * MCP Protocol States
 * 
 * Represents the application protocol lifecycle, independent of network state.
 * These states track MCP-specific protocol phases like initialization and
 * capability negotiation.
 */
enum class McpProtocolState {
  // Initial state - no connection
  DISCONNECTED,
  
  // Network connection being established
  CONNECTING,
  
  // Network connected, waiting to start protocol initialization
  CONNECTED,
  
  // Protocol initialization in progress (sending/receiving initialize)
  INITIALIZING,
  
  // Protocol initialized, capabilities negotiated, ready for operations
  READY,
  
  // Graceful shutdown in progress, draining pending operations
  DRAINING,
  
  // Protocol error occurred, connection unusable
  ERROR,
  
  // Final state - connection closed
  CLOSED
};

/**
 * MCP Protocol Events
 * 
 * Events that trigger state transitions in the protocol state machine.
 */
enum class McpProtocolEvent {
  // Connection lifecycle events
  CONNECT_REQUESTED,      // Application requests connection
  NETWORK_CONNECTED,      // Underlying network connection established
  NETWORK_DISCONNECTED,   // Network connection lost
  
  // Protocol lifecycle events
  INITIALIZE_REQUESTED,   // Initialize protocol requested
  INITIALIZE_SENT,        // Initialize request sent
  INITIALIZE_RECEIVED,    // Initialize response received
  INITIALIZED,            // Protocol successfully initialized
  
  // Shutdown events
  SHUTDOWN_REQUESTED,     // Graceful shutdown requested
  DRAIN_COMPLETE,         // All pending operations completed
  
  // Error events
  PROTOCOL_ERROR,         // Protocol-level error occurred
  TIMEOUT,                // Operation timed out
  
  // Recovery events
  RECONNECT_REQUESTED     // Reconnection requested after disconnect
};

/**
 * Protocol state transition context
 * Provides information about state transitions for logging and debugging
 */
struct ProtocolStateTransitionContext {
  McpProtocolState from_state;
  McpProtocolState to_state;
  McpProtocolEvent trigger_event;
  std::chrono::steady_clock::time_point timestamp;
  optional<std::string> reason;
  optional<Error> error;
};

/**
 * Configuration for the protocol state machine
 */
struct McpProtocolStateMachineConfig {
  // Timeout for protocol initialization
  std::chrono::milliseconds initialization_timeout{30000};
  
  // Timeout for graceful shutdown/draining
  std::chrono::milliseconds drain_timeout{10000};
  
  // Timeout for connection establishment
  std::chrono::milliseconds connection_timeout{10000};
  
  // Enable automatic reconnection on disconnect
  bool auto_reconnect{false};
  
  // Delay before reconnection attempts
  std::chrono::milliseconds reconnect_delay{1000};
  
  // Maximum reconnection attempts (0 = unlimited)
  size_t max_reconnect_attempts{3};
  
  // Callbacks for state transitions
  std::function<void(const ProtocolStateTransitionContext&)> state_change_callback;
  
  // Error callback
  std::function<void(const Error&)> error_callback;
};

/**
 * MCP Protocol State Machine
 * 
 * Manages application protocol state transitions with proper validation
 * and timeout handling.
 */
class McpProtocolStateMachine {
public:
  McpProtocolStateMachine(event::Dispatcher& dispatcher,
                          const McpProtocolStateMachineConfig& config);
  ~McpProtocolStateMachine();
  
  /**
   * Get current protocol state
   */
  McpProtocolState currentState() const {
    return current_state_.load();
  }
  
  /**
   * Check if in a specific state
   */
  bool isInState(McpProtocolState state) const {
    return current_state_ == state;
  }
  
  /**
   * Check if protocol is ready for operations
   */
  bool isReady() const {
    return current_state_ == McpProtocolState::READY;
  }
  
  /**
   * Check if in error state
   */
  bool isError() const {
    return current_state_ == McpProtocolState::ERROR;
  }
  
  /**
   * Handle protocol event
   * Returns true if the event was handled, false if invalid for current state
   */
  bool handleEvent(McpProtocolEvent event, 
                   const optional<std::string>& reason = nullopt);
  
  /**
   * Handle protocol error
   */
  void handleError(const Error& error);
  
  /**
   * Reset state machine to initial state
   */
  void reset();
  
  /**
   * Get time in current state
   */
  std::chrono::milliseconds getTimeInCurrentState() const;
  
  /**
   * Get last error if in error state
   */
  optional<Error> getLastError() const;
  
  /**
   * Get state name as string (for logging)
   */
  static std::string stateToString(McpProtocolState state);
  
  /**
   * Get event name as string (for logging)
   */
  static std::string eventToString(McpProtocolEvent event);

private:
  /**
   * Validate if transition is allowed
   */
  bool isTransitionValid(McpProtocolState from, 
                         McpProtocolState to,
                         McpProtocolEvent event) const;
  
  /**
   * Perform state transition
   */
  void transitionTo(McpProtocolState new_state, 
                   McpProtocolEvent event,
                   const optional<std::string>& reason = nullopt);
  
  /**
   * Start timeout timer for current state
   */
  void startStateTimer(std::chrono::milliseconds timeout);
  
  /**
   * Cancel current state timer
   */
  void cancelStateTimer();
  
  /**
   * Handle state timeout
   */
  void handleStateTimeout();
  
  /**
   * Enter state actions
   */
  void onEnterState(McpProtocolState state);
  
  /**
   * Exit state actions
   */
  void onExitState(McpProtocolState state);

private:
  // Configuration
  McpProtocolStateMachineConfig config_;
  
  // Current state
  std::atomic<McpProtocolState> current_state_{McpProtocolState::DISCONNECTED};
  
  // State timing
  std::chrono::steady_clock::time_point state_entry_time_;
  
  // Last error
  mutable std::mutex error_mutex_;
  optional<Error> last_error_;
  
  // Reconnection tracking
  size_t reconnect_attempts_{0};
  
  // Event dispatcher for timer management
  event::Dispatcher& dispatcher_;
  
  // State timeout timer
  event::TimerPtr state_timer_;
  
  // Thread safety
  mutable std::mutex state_mutex_;
};

} // namespace protocol
} // namespace mcp