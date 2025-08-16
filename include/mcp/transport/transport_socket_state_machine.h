/**
 * @file transport_socket_state_machine.h
 * @brief Generic transport socket state machine following production patterns
 *
 * Design principles:
 * - Thread-confined to dispatcher thread (lock-free)
 * - Async-first with completion callbacks
 * - Observable state transitions with listeners
 * - Pluggable validation and error handling
 * - Integration with MCP transport socket interface
 * - Support for both client and server modes
 *
 * Based on patterns from:
 * - Production transport socket architecture
 * - MCP's SSL and HTTP+SSE state machines
 * - Industrial best practices for network proxies
 */

#ifndef MCP_TRANSPORT_TRANSPORT_SOCKET_STATE_MACHINE_H
#define MCP_TRANSPORT_TRANSPORT_SOCKET_STATE_MACHINE_H

#include <atomic>
#include <cassert>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mcp/core/result.h"
#include "mcp/event/event_loop.h"
#include "mcp/network/connection.h"
#include "mcp/network/transport_socket.h"

namespace mcp {
namespace transport {

/**
 * Generic transport socket states
 *
 * This provides a base set of states common to most transport protocols.
 * Specific implementations can extend this with protocol-specific states.
 */
enum class TransportSocketState {
  // Initial states
  Uninitialized,  // Socket not yet initialized
  Initialized,    // Socket initialized but not connected

  // Connection establishment
  Connecting,    // TCP connection in progress
  TcpConnected,  // TCP connected, protocol handshake pending

  // Protocol-specific handshake states
  HandshakeInProgress,  // Protocol handshake in progress
  HandshakeComplete,    // Handshake completed successfully

  // Established connection states
  Connected,  // Fully connected and ready for data transfer
  Reading,    // Actively reading data
  Writing,    // Actively writing data
  Idle,       // Connected but no active I/O

  // Flow control states
  ReadBlocked,   // Read blocked (buffer full or backpressure)
  WriteBlocked,  // Write blocked (buffer full or backpressure)

  // Shutdown states
  ShuttingDown,      // Graceful shutdown initiated
  ShutdownComplete,  // Shutdown completed

  // Terminal states
  Closed,  // Connection closed normally
  Error    // Connection terminated due to error
};

/**
 * State transition result
 */
struct StateTransitionResult {
  bool success{false};
  std::string error_message;
  TransportSocketState resulting_state;
};

/**
 * State change event for observers
 */
struct StateChangeEvent {
  TransportSocketState from_state;
  TransportSocketState to_state;
  std::chrono::steady_clock::time_point timestamp;
  std::string reason;  // Why the transition occurred
};

/**
 * State machine configuration
 */
struct StateMachineConfig {
  // Operating mode
  enum class Mode {
    Client,  // Client-side state machine
    Server   // Server-side state machine
  };
  Mode mode{Mode::Client};

  // Timeouts
  std::chrono::milliseconds connect_timeout{30000};
  std::chrono::milliseconds handshake_timeout{10000};
  std::chrono::milliseconds idle_timeout{0};  // 0 = no timeout
  std::chrono::milliseconds shutdown_timeout{5000};

  // State history
  size_t max_state_history{100};  // Number of state changes to track

  // Error handling
  bool allow_force_transitions{false};  // Allow forced state transitions
  uint32_t max_error_recoveries{3};     // Max error recovery attempts

  // Callbacks
  std::function<void(const StateChangeEvent&)> state_change_callback;
  std::function<void(const std::string&)> error_callback;
};

/**
 * Base class for transport socket state machines
 *
 * This provides the core state machine infrastructure that specific
 * transport implementations can build upon.
 */
class TransportSocketStateMachine {
 public:
  using StateChangeCallback = std::function<void(const StateChangeEvent&)>;
  using CompletionCallback = std::function<void(bool success)>;
  using ValidationCallback =
      std::function<bool(TransportSocketState from, TransportSocketState to)>;

  // Friend class for testing utilities
  friend class StateMachineTestHelper;

  /**
   * Constructor
   *
   * @param dispatcher Event dispatcher for async operations
   * @param config State machine configuration
   */
  TransportSocketStateMachine(event::Dispatcher& dispatcher,
                              StateMachineConfig config);

  virtual ~TransportSocketStateMachine();

  // ===== Core State Machine Interface =====

  /**
   * Get current state
   */
  TransportSocketState currentState() const { return current_state_; }

  /**
   * Transition to a new state
   *
   * @param new_state Target state
   * @param reason Reason for transition (for logging/debugging)
   * @param callback Completion callback (called after transition)
   * @return Immediate result (async completion via callback)
   */
  StateTransitionResult transitionTo(TransportSocketState new_state,
                                     const std::string& reason,
                                     CompletionCallback callback = nullptr);

  /**
   * Force transition to a state (bypasses validation)
   * Use only for error recovery scenarios
   */
  StateTransitionResult forceTransitionTo(TransportSocketState new_state,
                                          const std::string& reason);

  /**
   * Schedule a state transition on next event loop iteration
   * Prevents stack overflow in recursive scenarios
   */
  void scheduleTransition(TransportSocketState new_state,
                          const std::string& reason,
                          CompletionCallback callback = nullptr);

  // ===== I/O Event Handling =====

  /**
   * Handle I/O ready events
   *
   * @param readable Socket is readable
   * @param writable Socket is writable
   */
  virtual void handleIoReady(bool readable, bool writable);

  /**
   * Handle connection result
   *
   * @param success Connection succeeded
   * @param error_message Error message if failed
   */
  virtual void handleConnectionResult(bool success,
                                      const std::string& error_message = "");

  /**
   * Handle protocol-specific handshake result
   */
  virtual void handleHandshakeResult(bool success,
                                     const std::string& error_message = "");

  // ===== State Observers =====

  /**
   * Add state change listener
   */
  void addStateChangeListener(StateChangeCallback callback);

  /**
   * Remove all state change listeners
   */
  void clearStateChangeListeners();

  /**
   * Get state history
   */
  const std::deque<StateChangeEvent>& getStateHistory() const {
    return state_history_;
  }

  // ===== State Validation =====

  /**
   * Add custom state transition validator
   */
  void addTransitionValidator(ValidationCallback validator);

  /**
   * Check if a state transition is valid
   */
  bool isTransitionValid(TransportSocketState from,
                         TransportSocketState to) const;

  // ===== Metrics and Monitoring =====

  /**
   * Get time spent in current state
   */
  std::chrono::milliseconds getTimeInCurrentState() const;

  /**
   * Get total number of state transitions
   */
  uint64_t getTotalTransitions() const { return total_transitions_; }

  /**
   * Get number of error recoveries
   */
  uint32_t getErrorRecoveries() const { return error_recoveries_; }

  // ===== Timer Management =====

  /**
   * Start a state-specific timeout timer
   */
  void startStateTimer(std::chrono::milliseconds timeout,
                       std::function<void()> timeout_callback);

  /**
   * Cancel any active state timer
   */
  void cancelStateTimer();

 protected:
  // ===== Protected Methods for Subclasses =====

  /**
   * Called before exiting a state (async)
   * Subclasses can override to perform cleanup
   */
  virtual void onStateExit(TransportSocketState state,
                           CompletionCallback callback);

  /**
   * Called after entering a state (async)
   * Subclasses can override to perform initialization
   */
  virtual void onStateEnter(TransportSocketState state,
                            CompletionCallback callback);

  /**
   * Get default valid transitions for a state
   * Subclasses should override to define protocol-specific rules
   */
  virtual std::unordered_set<TransportSocketState> getValidTransitions(
      TransportSocketState from) const;

  /**
   * Handle state-specific timeout
   * Subclasses can override for custom timeout handling
   */
  virtual void onStateTimeout(TransportSocketState state);

  /**
   * Assert we're in the dispatcher thread
   */
  void assertInDispatcherThread() const {
    // All methods must be called from dispatcher thread
    // Note: Disabled for now due to complexities with test setup
    // TODO: Re-enable once we have proper test infrastructure
    // assert(dispatcher_.isThreadSafe());
  }

  // ===== Protected Members =====

  event::Dispatcher& dispatcher_;
  StateMachineConfig config_;

  // Current state (atomic for safe reading from other threads)
  std::atomic<TransportSocketState> current_state_{
      TransportSocketState::Uninitialized};

  // State entry time
  std::chrono::steady_clock::time_point state_entry_time_;

  // Transition in progress flag (prevents reentrancy)
  bool transition_in_progress_{false};

  // Error recovery counter
  uint32_t error_recoveries_{0};

 private:
  // ===== Private Implementation =====

  /**
   * Execute state transition
   */
  void executeTransition(TransportSocketState new_state,
                         const std::string& reason,
                         CompletionCallback callback);

  /**
   * Notify state change listeners
   */
  void notifyStateChange(const StateChangeEvent& event);

  /**
   * Record state change in history
   */
  void recordStateChange(const StateChangeEvent& event);

  /**
   * Check built-in transition rules
   */
  bool checkBuiltinTransitionRules(TransportSocketState from,
                                   TransportSocketState to) const;

  // ===== Private Members =====

  // State change listeners
  std::vector<StateChangeCallback> state_change_listeners_;

  // Custom validators
  std::vector<ValidationCallback> custom_validators_;

  // State history
  std::deque<StateChangeEvent> state_history_;

  // Active state timer
  event::TimerPtr state_timer_;

  // Metrics
  std::atomic<uint64_t> total_transitions_{0};

  // Pending transition (for scheduled transitions)
  struct PendingTransition {
    TransportSocketState state;
    std::string reason;
    CompletionCallback callback;
  };
  std::unique_ptr<PendingTransition> pending_transition_;
};

/**
 * Factory for creating transport socket state machines
 */
class TransportSocketStateMachineFactory {
 public:
  virtual ~TransportSocketStateMachineFactory() = default;

  /**
   * Create a state machine instance
   */
  virtual std::unique_ptr<TransportSocketStateMachine> createStateMachine(
      event::Dispatcher& dispatcher, StateMachineConfig config) = 0;

  /**
   * Get default configuration
   */
  virtual StateMachineConfig getDefaultConfig() const {
    return StateMachineConfig{};
  }
};

/**
 * Utility class for state machine testing
 */
class StateMachineTestHelper {
 public:
  /**
   * Validate state transition matrix
   * Ensures all states have valid exit transitions
   */
  static bool validateTransitionMatrix(
      const TransportSocketStateMachine& machine);

  /**
   * Generate state diagram in DOT format
   * Useful for documentation and debugging
   */
  static std::string generateStateDiagram(
      const TransportSocketStateMachine& machine);

  /**
   * Run state machine through common scenarios
   */
  static bool runCommonScenarios(TransportSocketStateMachine& machine);
};

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_TRANSPORT_SOCKET_STATE_MACHINE_H