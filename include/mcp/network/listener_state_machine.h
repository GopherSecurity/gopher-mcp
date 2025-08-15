/**
 * @file listener_state_machine.h
 * @brief Production-grade listener state machine for network servers
 *
 * This implements a comprehensive state machine for listener lifecycle that:
 * - Manages socket binding and listening
 * - Handles connection acceptance with backpressure
 * - Supports listener filters (PROXY protocol, SNI, etc.)
 * - Provides graceful shutdown and draining
 * - Thread-safe through dispatcher thread confinement
 * - Observable state transitions for monitoring
 *
 * Design principles following industrial best practices:
 * - Lock-free: All operations in dispatcher thread (no mutexes)
 * - State validation: Enforce valid transitions only
 * - Async-first: All state changes are event-driven
 * - Observable: Comprehensive state tracking and callbacks
 * - Resilient: Graceful error handling and recovery
 * - Performant: Optimized for high connection rates
 */

#ifndef MCP_NETWORK_LISTENER_STATE_MACHINE_H
#define MCP_NETWORK_LISTENER_STATE_MACHINE_H

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "mcp/core/result.h"
#include "mcp/event/event_loop.h"
#include "mcp/network/address.h"
#include "mcp/network/connection.h"
#include "mcp/network/filter.h"
#include "mcp/network/socket.h"

namespace mcp {
namespace network {

/**
 * Listener States
 * 
 * Comprehensive state model covering:
 * - Socket lifecycle (bind, listen, accept)
 * - Connection acceptance and filtering
 * - Backpressure and flow control
 * - Graceful shutdown and draining
 *
 * States follow production server patterns:
 * - Past participle (-ed) for completed states
 * - Present participle (-ing) for ongoing actions
 * - Adjectives for conditions/modes
 */
enum class ListenerState {
  // ===== Initial States =====
  Uninitialized,  // Listener not yet initialized
  Initialized,    // Configuration loaded, ready to bind
  
  // ===== Socket Binding States =====
  Binding,        // Binding to address/port
  Bound,          // Successfully bound to address
  
  // ===== Listening States =====
  ListenPending,  // About to call listen()
  Listening,      // Actively listening for connections
  
  // ===== Connection Acceptance States =====
  AcceptReady,    // Ready to accept connections
  Accepting,      // Accept in progress
  AcceptFiltering,// Running listener filters on accepted connection
  AcceptComplete, // Connection accepted and filtered
  
  // ===== Flow Control States =====
  Paused,         // Temporarily paused (backpressure)
  Resuming,       // Resuming from paused state
  Throttled,      // Rate limiting active
  
  // ===== Shutdown States =====
  ShutdownInitiated,  // Graceful shutdown started
  Draining,           // Draining existing connections
  ShutdownComplete,   // All connections drained
  
  // ===== Error States =====
  Error,          // Unrecoverable error occurred
  BindError,      // Failed to bind to address
  ListenError,    // Failed to listen on socket
  AcceptError,    // Accept failure (may be recoverable)
  
  // ===== Special States =====
  Disabled,       // Administratively disabled
  Maintenance     // Maintenance mode (accepts but rejects)
};

/**
 * Listener events that trigger state transitions
 */
enum class ListenerEvent {
  // Configuration events
  Configure,
  Reconfigure,
  
  // Socket events
  BindRequested,
  BindSucceeded,
  BindFailed,
  ListenRequested,
  ListenSucceeded,
  ListenFailed,
  
  // Connection events
  ConnectionPending,
  ConnectionAccepted,
  ConnectionFiltered,
  ConnectionRejected,
  FilterTimeout,
  
  // Flow control events
  BackpressureApplied,
  BackpressureReleased,
  RateLimitExceeded,
  RateLimitRestored,
  
  // Administrative events
  EnableRequested,
  DisableRequested,
  MaintenanceModeEntered,
  MaintenanceModeExited,
  
  // Shutdown events
  ShutdownRequested,
  DrainComplete,
  ForceClose,
  
  // Error events
  SocketError,
  SystemResourceExhausted,
  ConfigurationError
};

/**
 * Listener state transition result
 */
struct ListenerStateTransition {
  bool valid;
  ListenerState from_state;
  ListenerState to_state;
  ListenerEvent triggering_event;
  std::string reason;
  std::chrono::steady_clock::time_point timestamp;
};

/**
 * Listener metrics for monitoring
 */
struct ListenerMetrics {
  // Connection metrics
  uint64_t total_accepted{0};
  uint64_t total_rejected{0};
  uint64_t active_connections{0};
  uint64_t max_connections_reached{0};
  
  // Filter metrics
  uint64_t filter_chain_success{0};
  uint64_t filter_chain_failure{0};
  uint64_t filter_timeout{0};
  
  // State duration tracking
  std::chrono::milliseconds time_in_listening{0};
  std::chrono::milliseconds time_in_paused{0};
  std::chrono::milliseconds time_in_draining{0};
  
  // Error metrics
  uint64_t accept_errors{0};
  uint64_t socket_errors{0};
  uint64_t resource_exhausted{0};
};

/**
 * Listener state callbacks for observers
 */
class ListenerStateCallbacks {
public:
  virtual ~ListenerStateCallbacks() = default;
  
  /**
   * Called before state transition
   * Return false to veto the transition
   */
  virtual bool onStateTransition(const ListenerStateTransition& transition) {
    return true;
  }
  
  /**
   * Called after successful state transition
   */
  virtual void onStateChanged(ListenerState new_state) {}
  
  /**
   * Called when connection is accepted (before filters)
   */
  virtual void onConnectionAccepted(const Address::InstanceConstSharedPtr& remote_address) {}
  
  /**
   * Called when connection passes all filters
   */
  virtual void onConnectionReady(ConnectionPtr connection) {}
  
  /**
   * Called when listener enters error state
   */
  virtual void onListenerError(const std::string& error_message) {}
  
  /**
   * Called periodically with metrics
   */
  virtual void onMetricsUpdate(const ListenerMetrics& metrics) {}
};

/**
 * Listener state machine configuration
 */
struct ListenerStateMachineConfig {
  // Backpressure thresholds
  uint32_t max_connections{10000};
  uint32_t pause_threshold{9000};  // Pause at 90% capacity
  uint32_t resume_threshold{7000}; // Resume at 70% capacity
  
  // Timeouts
  std::chrono::milliseconds accept_timeout{30000};
  std::chrono::milliseconds filter_timeout{5000};
  std::chrono::milliseconds drain_timeout{30000};
  
  // Rate limiting
  uint32_t max_accept_rate{1000};  // Per second
  std::chrono::milliseconds rate_limit_window{1000};
  
  // Retry configuration
  uint32_t max_accept_errors{10};
  std::chrono::milliseconds error_backoff{100};
  
  // Feature flags
  bool enable_backpressure{true};
  bool enable_rate_limiting{false};
  bool enable_graceful_shutdown{true};
  bool enable_filter_chain{true};
};

/**
 * Listener state machine implementation
 * 
 * Thread-safety: All methods must be called from dispatcher thread
 */
class ListenerStateMachine {
public:
  ListenerStateMachine(event::Dispatcher& dispatcher,
                       const ListenerStateMachineConfig& config);
  ~ListenerStateMachine();
  
  // State management
  ListenerState currentState() const { return state_; }
  bool isInState(ListenerState state) const { return state_ == state; }
  bool canAcceptConnections() const;
  bool isShuttingDown() const;
  
  // State transitions - all return transition result
  ListenerStateTransition initialize(const Address::InstanceConstSharedPtr& address,
                                    const SocketOptionsSharedPtr& options);
  ListenerStateTransition bind();
  ListenerStateTransition listen(int backlog = 128);
  ListenerStateTransition enable();
  ListenerStateTransition disable();
  ListenerStateTransition pause();
  ListenerStateTransition resume();
  ListenerStateTransition shutdown();
  ListenerStateTransition forceClose();
  
  // Connection handling
  ListenerStateTransition handleConnectionPending();
  ListenerStateTransition handleConnectionAccepted(ConnectionSocketPtr socket);
  ListenerStateTransition handleConnectionFiltered(ConnectionPtr connection);
  ListenerStateTransition handleConnectionRejected(const std::string& reason);
  
  // Error handling
  ListenerStateTransition handleError(ListenerEvent error_event,
                                     const std::string& error_message);
  
  // Metrics and monitoring
  const ListenerMetrics& metrics() const { return metrics_; }
  void resetMetrics();
  
  // State observation
  void addStateCallbacks(std::shared_ptr<ListenerStateCallbacks> callbacks);
  void removeStateCallbacks(std::shared_ptr<ListenerStateCallbacks> callbacks);
  
  // State validation
  bool isValidTransition(ListenerState from, ListenerState to) const;
  bool canHandleEvent(ListenerEvent event) const;
  
  // State history for debugging
  std::vector<ListenerStateTransition> getStateHistory(size_t max_entries = 100) const;
  
private:
  // State transition implementation
  ListenerStateTransition transitionTo(ListenerState new_state,
                                       ListenerEvent triggering_event,
                                       const std::string& reason = "");
  
  // State validation matrix
  bool isTransitionAllowed(ListenerState from, ListenerState to,
                           ListenerEvent event) const;
  
  // State entry/exit handlers
  void onEnterState(ListenerState state);
  void onExitState(ListenerState state);
  
  // Timer management
  void scheduleRetry();
  void cancelRetry();
  void updateStateTimer();
  
  // Backpressure management
  void checkBackpressure();
  void applyBackpressure();
  void releaseBackpressure();
  
  // Rate limiting
  void checkRateLimit();
  bool isRateLimited() const;
  void updateRateLimitWindow();
  
  // Metrics updates
  void updateMetrics(ListenerEvent event);
  void recordStateTime(ListenerState state, std::chrono::milliseconds duration);
  
  // Callback notifications
  void notifyStateChange(const ListenerStateTransition& transition);
  void notifyError(const std::string& error_message);
  
  // Core components
  event::Dispatcher& dispatcher_;
  ListenerStateMachineConfig config_;
  
  // Current state
  std::atomic<ListenerState> state_{ListenerState::Uninitialized};
  std::chrono::steady_clock::time_point state_enter_time_;
  
  // State history (circular buffer)
  std::vector<ListenerStateTransition> state_history_;
  size_t history_index_{0};
  static constexpr size_t kMaxHistorySize = 1000;
  
  // Connection tracking
  std::atomic<uint64_t> active_connections_{0};
  std::atomic<uint64_t> pending_connections_{0};
  
  // Rate limiting state
  std::queue<std::chrono::steady_clock::time_point> accept_timestamps_;
  
  // Error tracking
  uint32_t consecutive_errors_{0};
  std::chrono::steady_clock::time_point last_error_time_;
  
  // Timers
  event::TimerPtr retry_timer_;
  event::TimerPtr drain_timer_;
  event::TimerPtr metrics_timer_;
  
  // Callbacks
  std::vector<std::shared_ptr<ListenerStateCallbacks>> callbacks_;
  
  // Metrics
  ListenerMetrics metrics_;
  
  // Socket information
  Address::InstanceConstSharedPtr local_address_;
  SocketOptionsSharedPtr socket_options_;
};

/**
 * State machine factory for creating listener state machines
 */
class ListenerStateMachineFactory {
public:
  static std::unique_ptr<ListenerStateMachine> create(
      event::Dispatcher& dispatcher,
      const ListenerStateMachineConfig& config = ListenerStateMachineConfig{});
};

/**
 * Helper functions for state machine
 */
namespace ListenerStateHelper {
  // Convert state to string for logging
  std::string stateToString(ListenerState state);
  
  // Convert event to string for logging
  std::string eventToString(ListenerEvent event);
  
  // Check if state is terminal
  bool isTerminalState(ListenerState state);
  
  // Check if state allows new connections
  bool canAcceptInState(ListenerState state);
  
  // Get valid events for a state
  std::vector<ListenerEvent> getValidEvents(ListenerState state);
  
  // Get valid target states from current state
  std::vector<ListenerState> getValidTransitions(ListenerState from);
}

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_LISTENER_STATE_MACHINE_H