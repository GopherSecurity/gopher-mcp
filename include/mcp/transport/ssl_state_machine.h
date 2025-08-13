/**
 * @file ssl_state_machine.h
 * @brief Async-only SSL/TLS state machine for event loop architecture
 * 
 * This implements a lock-free, event-driven state machine that:
 * - Runs entirely in the dispatcher thread (no mutexes)
 * - Uses event loop for all state transitions
 * - Follows reactor pattern for I/O events
 * - Provides comprehensive state tracking and validation
 * 
 * Design principles:
 * - Thread confinement: All operations in dispatcher thread
 * - Event-driven: State changes triggered by events
 * - Non-blocking: No synchronization primitives
 * - Callback-based: Async notifications
 * 
 * Following industrial best practices from Envoy, Nginx, Node.js
 */

#ifndef MCP_TRANSPORT_SSL_STATE_MACHINE_H
#define MCP_TRANSPORT_SSL_STATE_MACHINE_H

#include <chrono>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mcp/core/result.h"
#include "mcp/event/event_loop.h"

namespace mcp {
namespace transport {

/**
 * SSL/TLS Socket States
 * 
 * Comprehensive state model covering:
 * - Connection lifecycle
 * - Handshake phases
 * - Data transfer
 * - Shutdown sequence
 * - Error conditions
 */
enum class SslSocketState {
  // Initial states
  Uninitialized,     // Socket not yet initialized
  Initialized,       // SSL context created, ready to connect
  
  // Connection states
  Connecting,        // TCP connection in progress
  TcpConnected,      // TCP connected, ready for SSL handshake
  
  // Handshake states (client)
  ClientHandshakeInit,      // Client initiating handshake
  ClientHelloSent,          // Client hello sent
  ServerHelloReceived,      // Server hello received
  ClientCertRequested,      // Server requested client certificate
  ClientCertSent,           // Client certificate sent
  ClientKeyExchange,        // Client key exchange in progress
  ClientChangeCipherSpec,   // Client changing cipher spec
  ClientFinished,           // Client finished message sent
  
  // Handshake states (server)
  ServerHandshakeInit,      // Server waiting for client hello
  ClientHelloReceived,      // Client hello received
  ServerHelloSent,          // Server hello sent
  ServerCertSent,           // Server certificate sent
  ServerKeyExchange,        // Server key exchange in progress
  ServerCertRequest,        // Server requesting client cert
  ServerHelloDone,          // Server hello done sent
  ClientCertReceived,       // Client certificate received
  ServerChangeCipherSpec,   // Server changing cipher spec
  ServerFinished,           // Server finished message sent
  
  // Async states
  HandshakeWantRead,        // Handshake blocked on read
  HandshakeWantWrite,       // Handshake blocked on write
  HandshakeWantAsync,       // Handshake blocked on async operation
  CertificateValidating,    // Async certificate validation
  
  // Established state
  Connected,           // SSL connection established, ready for data
  
  // Renegotiation states (TLS 1.2 only)
  RenegotiationRequested,   // Renegotiation requested
  RenegotiationInProgress,  // Renegotiation handshake in progress
  
  // Shutdown states
  ShutdownInitiated,   // SSL shutdown initiated
  ShutdownSent,        // Close notify alert sent
  ShutdownReceived,    // Close notify alert received
  ShutdownComplete,    // Bidirectional shutdown complete
  
  // Terminal states
  Closed,              // Connection closed cleanly
  Error                // Connection terminated due to error
};

/**
 * SSL Socket Mode - determines valid state transitions
 */
enum class SslSocketMode {
  Client,
  Server
};

/**
 * State transition result
 */
struct StateTransitionResult {
  bool success;
  std::string error_message;
  SslSocketState resulting_state;
  
  static StateTransitionResult Success(SslSocketState state) {
    return {true, "", state};
  }
  
  static StateTransitionResult Failure(const std::string& error) {
    return {false, error, SslSocketState::Error};
  }
};

/**
 * SSL State Machine (Async-Only)
 * 
 * Lock-free state machine that runs entirely in dispatcher thread.
 * No mutexes needed - uses event loop serialization.
 * 
 * Thread model:
 * - ALL methods must be called from dispatcher thread
 * - No synchronization primitives used
 * - State transitions are atomic within event loop
 * - Callbacks executed asynchronously
 */
class SslStateMachine {
public:
  /**
   * Callback types for async operations
   */
  using StateChangeCallback = std::function<void(SslSocketState old_state, 
                                                 SslSocketState new_state)>;
  using TransitionCompleteCallback = std::function<void(bool success, 
                                                        const std::string& error)>;
  using StateAction = std::function<void(SslSocketState state, 
                                         std::function<void()> done)>;
  using TransitionValidator = std::function<bool(SslSocketState from, 
                                                 SslSocketState to)>;
  
  /**
   * Create state machine for given mode
   * @param mode Client or Server mode
   * @param dispatcher Event dispatcher (must be called from its thread)
   */
  SslStateMachine(SslSocketMode mode, event::Dispatcher& dispatcher);
  
  ~SslStateMachine();
  
  /**
   * Get current state (immediate, no lock needed)
   * @note Must be called from dispatcher thread
   */
  SslSocketState getCurrentState() const {
    assertInDispatcherThread();
    return current_state_;
  }
  
  /**
   * Check if transition is valid
   * @param from Source state
   * @param to Target state
   * @return true if transition is valid for current mode
   * @note Must be called from dispatcher thread
   */
  bool canTransition(SslSocketState from, SslSocketState to) const;
  
  /**
   * Async state transition
   * @param new_state Target state
   * @param callback Called when transition completes (may be null)
   * 
   * Flow (all in dispatcher thread):
   * 1. Validate transition
   * 2. Execute async exit action for current state
   * 3. Update state atomically
   * 4. Execute async entry action for new state
   * 5. Notify state change listeners
   * 6. Invoke completion callback
   * 
   * @note Must be called from dispatcher thread
   */
  void transition(SslSocketState new_state, 
                 TransitionCompleteCallback callback = nullptr);
  
  /**
   * Schedule state transition in next event loop iteration
   * Useful for avoiding stack overflow in recursive transitions
   * 
   * @param new_state Target state
   * @param callback Completion callback (may be null)
   * @note Must be called from dispatcher thread
   */
  void scheduleTransition(SslSocketState new_state,
                         TransitionCompleteCallback callback = nullptr) {
    assertInDispatcherThread();
    dispatcher_.post([this, new_state, callback]() {
      transition(new_state, callback);
    });
  }
  
  /**
   * Force transition (bypass validation) - use with extreme caution
   * @param new_state Target state
   * @note Must be called from dispatcher thread
   */
  void forceTransition(SslSocketState new_state);
  
  /**
   * Register state change listener
   * @param callback Called on every state change
   * @return Listener ID for removal
   */
  uint32_t addStateChangeListener(StateChangeCallback callback);
  
  /**
   * Unregister state change listener
   * @param listener_id ID returned from addStateChangeListener
   */
  void removeStateChangeListener(uint32_t listener_id);
  
  /**
   * Register async entry action for a state
   * @param state State to register action for
   * @param action Async action with completion callback
   */
  void setEntryAction(SslSocketState state, StateAction action) {
    assertInDispatcherThread();
    entry_actions_[state] = action;
  }
  
  /**
   * Register async exit action for a state
   * @param state State to register action for
   * @param action Async action with completion callback
   */
  void setExitAction(SslSocketState state, StateAction action) {
    assertInDispatcherThread();
    exit_actions_[state] = action;
  }
  
  /**
   * Register custom transition validator
   * @param validator Additional validation logic
   */
  void addTransitionValidator(TransitionValidator validator) {
    assertInDispatcherThread();
    custom_validators_.push_back(validator);
  }
  
  /**
   * Handle I/O ready event
   * Triggers appropriate state transitions based on I/O availability
   * 
   * @param readable Socket is readable
   * @param writable Socket is writable
   * @note Must be called from dispatcher thread
   */
  void handleIoReady(bool readable, bool writable);
  
  /**
   * Handle SSL operation result
   * Maps SSL results to state transitions
   * 
   * @param ssl_result Result from SSL_do_handshake/read/write
   * @param ssl_error SSL error code if result <= 0
   * @note Must be called from dispatcher thread
   */
  void handleSslResult(int ssl_result, int ssl_error);
  
  /**
   * Handle async operation completion
   * @param operation_id Operation identifier
   * @param success Operation succeeded
   * @param error Error message if failed
   */
  void handleAsyncComplete(uint32_t operation_id, bool success, 
                          const std::string& error = "");
  
  /**
   * Start async operation with state transitions
   * @param success_state State to transition to on success
   * @param failure_state State to transition to on failure
   * @return Operation ID for completion notification
   */
  uint32_t startAsyncOperation(SslSocketState success_state,
                               SslSocketState failure_state);
  
  /**
   * Set timeout for current state
   * @param timeout Timeout duration
   * @param timeout_state State to transition to on timeout
   */
  void setStateTimeout(std::chrono::milliseconds timeout,
                      SslSocketState timeout_state);
  
  /**
   * Cancel current state timeout
   */
  void cancelStateTimeout();
  
  /**
   * Get human-readable state name
   */
  static std::string getStateName(SslSocketState state);
  
  /**
   * Helper methods
   */
  bool isTerminalState() const {
    return current_state_ == SslSocketState::Closed || 
           current_state_ == SslSocketState::Error;
  }
  
  bool isHandshaking() const;
  bool isConnected() const { return current_state_ == SslSocketState::Connected; }
  bool isWaitingForIo() const;
  
  SslSocketMode getMode() const { return mode_; }
  
  /**
   * Get time in current state
   */
  std::chrono::milliseconds getTimeInCurrentState() const;
  
  /**
   * Get state history for debugging
   */
  std::vector<std::pair<SslSocketState, std::chrono::steady_clock::time_point>> 
  getStateHistory(size_t max_entries = 10) const;

private:
  /**
   * Initialize valid transitions for client/server
   */
  void initializeClientTransitions();
  void initializeServerTransitions();
  
  /**
   * Execute async entry/exit actions
   */
  void executeEntryAction(SslSocketState state, std::function<void()> done);
  void executeExitAction(SslSocketState state, std::function<void()> done);
  
  /**
   * Notify state change listeners
   */
  void notifyStateChange(SslSocketState old_state, SslSocketState new_state);
  
  /**
   * Validate state transition
   */
  bool isValidTransition(SslSocketState from, SslSocketState to) const;
  
  /**
   * Record state transition in history
   */
  void recordStateTransition(SslSocketState state);
  
  /**
   * Assert we're in dispatcher thread
   * Note: Since we don't have a way to check thread context in the current
   * dispatcher implementation, this is a no-op for now.
   * In production, all calls are expected to be from dispatcher thread.
   */
  void assertInDispatcherThread() const {
    // No-op for now - all methods must be called from dispatcher thread
  }
  
  /**
   * Handle I/O event based on current state
   */
  void processIoEvent(bool readable, bool writable);
  
  /**
   * Map SSL error to next state
   */
  SslSocketState mapSslErrorToState(int ssl_error) const;

private:
  // Configuration
  const SslSocketMode mode_;
  event::Dispatcher& dispatcher_;
  
  // Current state (no mutex needed - dispatcher thread only)
  SslSocketState current_state_{SslSocketState::Uninitialized};
  std::chrono::steady_clock::time_point state_entry_time_;
  
  // State history for debugging
  static constexpr size_t kMaxHistorySize = 50;
  std::vector<std::pair<SslSocketState, std::chrono::steady_clock::time_point>> state_history_;
  
  // Valid transitions map
  std::unordered_map<SslSocketState, std::unordered_set<SslSocketState>> valid_transitions_;
  
  // State actions (async)
  std::unordered_map<SslSocketState, StateAction> entry_actions_;
  std::unordered_map<SslSocketState, StateAction> exit_actions_;
  
  // Custom validators
  std::vector<TransitionValidator> custom_validators_;
  
  // State change listeners
  uint32_t next_listener_id_{1};
  std::unordered_map<uint32_t, StateChangeCallback> state_listeners_;
  
  // Async operations
  struct AsyncOperation {
    SslSocketState success_state;
    SslSocketState failure_state;
  };
  uint32_t next_operation_id_{1};
  std::unordered_map<uint32_t, AsyncOperation> pending_operations_;
  
  // State timeout
  event::TimerPtr state_timeout_timer_;
  SslSocketState timeout_state_;
  
  // Transition in progress flag (prevents reentrancy)
  bool transition_in_progress_{false};
};

/**
 * Factory for creating configured state machines
 */
class SslStateMachineFactory {
public:
  /**
   * Create client state machine with standard configuration
   */
  static std::unique_ptr<SslStateMachine> createClientStateMachine(
      event::Dispatcher& dispatcher);
  
  /**
   * Create server state machine with standard configuration
   */
  static std::unique_ptr<SslStateMachine> createServerStateMachine(
      event::Dispatcher& dispatcher);
};

/**
 * Helper patterns for common state operations
 */
class SslStatePatterns {
public:
  /**
   * Check if state represents an active handshake
   */
  static bool isHandshakeState(SslSocketState state);
  
  /**
   * Check if state is waiting for I/O
   */
  static bool isIoBlockedState(SslSocketState state);
  
  /**
   * Check if state allows data transfer
   */
  static bool canTransferData(SslSocketState state);
  
  /**
   * Check if state allows shutdown initiation
   */
  static bool canInitiateShutdown(SslSocketState state);
  
  /**
   * Get next expected handshake state
   */
  static optional<SslSocketState> getNextHandshakeState(
      SslSocketState current, SslSocketMode mode);
  
  /**
   * Check if state represents an error
   */
  static bool isErrorState(SslSocketState state);
  
  /**
   * Get next expected client handshake state
   */
  static optional<SslSocketState> getNextClientHandshakeState(
      SslSocketState current);
  
  /**
   * Get next expected server handshake state
   */
  static optional<SslSocketState> getNextServerHandshakeState(
      SslSocketState current);
};

/**
 * Transition coordinator for complex sequences
 */
class SslTransitionCoordinator {
public:
  SslTransitionCoordinator(SslStateMachine& machine)
      : machine_(machine) {}
  
  /**
   * Execute handshake sequence
   */
  void executeHandshake(std::function<void(bool)> callback);
  
  /**
   * Execute shutdown sequence
   */
  void executeShutdown(std::function<void(bool)> callback);
  
  /**
   * Execute renegotiation (TLS 1.2)
   */
  void executeRenegotiation(std::function<void(bool)> callback);

private:
  SslStateMachine& machine_;
  
  void executeSequence(const std::vector<SslSocketState>& states,
                       size_t index,
                       std::function<void(bool)> callback);
};

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_SSL_STATE_MACHINE_H