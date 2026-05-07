/**
 * @file client_sse_state_machine.h
 * @brief Client-side SSE connection lifecycle state machine
 *
 * Manages the SSE endpoint negotiation lifecycle for MCP clients.
 * Replaces ad-hoc boolean flags (waiting_for_sse_endpoint_, is_sse_mode_,
 * use_sse_, hasSentSseGetRequest()) with a formal state machine that
 * provides transition validation, state history, and timeout handling.
 *
 * Design principles:
 * - Thread-confined to dispatcher thread (lock-free, no mutex)
 * - Async-first with completion callbacks
 * - Observable state transitions with listeners
 * - Pluggable validation and error handling
 * - Uniform interface matching SseCodecStateMachine / HttpCodecStateMachine
 *
 * Lifecycle:
 *   Idle -> WaitingForGetSent -> WaitingForEndpoint -> EndpointReceived
 *        -> Active -> Closed
 *
 * Or for Streamable HTTP (no SSE):
 *   StreamableHttp -> Closed
 */

#ifndef MCP_FILTER_CLIENT_SSE_STATE_MACHINE_H
#define MCP_FILTER_CLIENT_SSE_STATE_MACHINE_H

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mcp/event/event_loop.h"

namespace mcp {
namespace filter {

/**
 * Client SSE connection states
 *
 * Tracks the client-side SSE endpoint negotiation lifecycle.
 * Each state maps to a distinct phase in the MCP SSE transport
 * handshake (or the absence of one in Streamable HTTP mode).
 */
enum class ClientSseState {
  // Config-determined initial state: the factory was constructed with
  // use_sse=false, so this connection uses direct POST without any
  // SSE endpoint negotiation.
  StreamableHttp,

  // SSE negotiation lifecycle (use_sse=true):

  // Connection created, but the HTTP GET /sse request has not been
  // sent yet. The connection may still be completing a TCP/TLS
  // handshake at this point.
  Idle,

  // The first onWrite() call triggered the HTTP GET /sse request.
  // Waiting for the GET bytes to be flushed to the socket.
  WaitingForGetSent,

  // GET /sse has been sent. Waiting for the server to respond with
  // an SSE "endpoint" event that tells us where to POST messages.
  WaitingForEndpoint,

  // The "endpoint" SSE event arrived. The POST URL is known and
  // queued messages can be flushed. Transitional state before the
  // SSE Content-Type is confirmed.
  EndpointReceived,

  // The SSE stream is fully active: Content-Type: text/event-stream
  // has been seen, SSE events are being received, and outbound
  // JSON-RPC messages are sent via separate POST connections.
  Active,

  // Terminal: an unrecoverable error occurred during negotiation
  // or while the stream was active.
  Error,

  // Terminal: the connection has been closed cleanly.
  Closed
};

/**
 * Client SSE connection events
 *
 * Events that trigger state transitions during the SSE negotiation
 * lifecycle. Each event corresponds to an observable action in the
 * MCP client transport layer.
 */
enum class ClientSseEvent {
  // The filter's onNewConnection() has completed and the connection
  // is ready for the first write.
  ConnectionReady,

  // The HTTP GET /sse request has been written to the socket.
  GetSent,

  // The server responded with an SSE "endpoint" event containing
  // the POST callback URL.
  EndpointReceived,

  // The HTTP response carried Content-Type: text/event-stream,
  // confirming the server is streaming SSE events.
  StreamStarted,

  // The negotiation timeout timer fired before the server sent
  // the "endpoint" event.
  NegotiationTimeout,

  // An I/O or protocol error occurred.
  StreamError,

  // Graceful close of the connection.
  Close
};

/**
 * State transition result
 */
struct ClientSseTransitionResult {
  bool success{false};
  std::string error_message;
  ClientSseState resulting_state;

  static ClientSseTransitionResult Success(ClientSseState state) {
    return {true, "", state};
  }

  static ClientSseTransitionResult Failure(const std::string& error) {
    return {false, error, ClientSseState::Error};
  }
};

/**
 * State transition context with detailed information
 */
struct ClientSseTransitionContext {
  ClientSseState from_state;
  ClientSseState to_state;
  ClientSseEvent triggering_event;
  std::chrono::steady_clock::time_point timestamp;
  std::string reason;

  // How long the machine spent in the previous state
  std::chrono::milliseconds time_in_previous_state;
};

/**
 * Configuration for the client SSE state machine
 */
struct ClientSseStateMachineConfig {
  // Maximum time to wait for the server's "endpoint" SSE event after
  // sending GET /sse. 0 = no timeout (wait indefinitely).
  std::chrono::milliseconds negotiation_timeout{30000};

  // Callbacks
  std::function<void(const ClientSseTransitionContext&)> state_change_callback;
  std::function<void(const std::string&)> error_callback;
};

/**
 * Client SSE Connection State Machine
 *
 * Manages the SSE endpoint negotiation lifecycle for MCP clients.
 * Thread-confined to the dispatcher thread for lock-free operation.
 *
 * Each connection's filter instance owns its own state machine via
 * unique_ptr. State machines are never shared across connections.
 */
class ClientSseStateMachine {
 public:
  using StateChangeCallback =
      std::function<void(const ClientSseTransitionContext&)>;
  using CompletionCallback = std::function<void(bool success)>;
  using ValidationCallback =
      std::function<bool(ClientSseState from, ClientSseState to)>;

  /**
   * Constructor
   *
   * @param dispatcher Event dispatcher for timer management
   * @param config State machine configuration
   * @param use_sse True for SSE mode, false for Streamable HTTP
   */
  ClientSseStateMachine(event::Dispatcher& dispatcher,
                        const ClientSseStateMachineConfig& config,
                        bool use_sse);

  virtual ~ClientSseStateMachine();

  // ===== Core State Machine Interface =====

  /**
   * Get current state
   */
  ClientSseState currentState() const { return current_state_; }

  /**
   * Handle an event, triggering the appropriate state transition.
   *
   * @param event The event to process
   * @param callback Optional completion callback
   * @return Immediate result indicating success or failure
   */
  ClientSseTransitionResult handleEvent(
      ClientSseEvent event, CompletionCallback callback = nullptr);

  /**
   * Transition to a new state with explicit reason.
   * Validates the transition against the transition matrix.
   */
  ClientSseTransitionResult transitionTo(ClientSseState new_state,
                                         ClientSseEvent event,
                                         const std::string& reason,
                                         CompletionCallback callback = nullptr);

  /**
   * Force transition to a state (bypasses validation).
   * Use only for error recovery or testing.
   */
  void forceTransition(ClientSseState new_state, const std::string& reason);

  /**
   * Schedule a transition on the next event loop iteration.
   * Prevents stack overflow in deeply recursive callback chains.
   */
  void scheduleTransition(ClientSseState new_state,
                          ClientSseEvent event,
                          const std::string& reason,
                          CompletionCallback callback = nullptr);

  // ===== State Queries =====

  /**
   * True when negotiation is in progress: the machine is between
   * Idle and Active, actively trying to establish the SSE endpoint.
   */
  bool isNegotiating() const {
    return current_state_ == ClientSseState::WaitingForGetSent ||
           current_state_ == ClientSseState::WaitingForEndpoint ||
           current_state_ == ClientSseState::EndpointReceived;
  }

  /**
   * True when the SSE stream is fully established and the client
   * is receiving events.
   */
  bool isReady() const { return current_state_ == ClientSseState::Active; }

  /**
   * True when the client can send outbound JSON-RPC messages via POST.
   * In Streamable HTTP mode, this is always true. In SSE mode, this is
   * true only after the endpoint has been received and the stream is active.
   */
  bool canSendPost() const {
    return current_state_ == ClientSseState::Active ||
           current_state_ == ClientSseState::StreamableHttp;
  }

  /**
   * True if the machine is in a terminal error state.
   */
  bool hasError() const { return current_state_ == ClientSseState::Error; }

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
  const std::deque<ClientSseTransitionContext>& getStateHistory() const {
    return state_history_;
  }

  // ===== Validation =====

  /**
   * Add custom transition validator
   */
  void addTransitionValidator(ValidationCallback validator);

  /**
   * Check if transition is valid
   */
  bool isTransitionValid(ClientSseState from,
                         ClientSseState to,
                         ClientSseEvent event) const;

  // ===== Metrics =====

  /**
   * Get time in current state
   */
  std::chrono::milliseconds getTimeInCurrentState() const;

  /**
   * Get total transitions
   */
  uint64_t getTotalTransitions() const { return total_transitions_; }

  // ===== Utility =====

  /**
   * Get human-readable state name
   */
  static std::string getStateName(ClientSseState state);

  /**
   * Get human-readable event name
   */
  static std::string getEventName(ClientSseEvent event);

 protected:
  /**
   * Called before exiting a state
   */
  virtual void onStateExit(ClientSseState state, CompletionCallback callback);

  /**
   * Called after entering a state
   */
  virtual void onStateEnter(ClientSseState state, CompletionCallback callback);

  /**
   * Assert we're in dispatcher thread
   */
  void assertInDispatcherThread() const {
    // All methods must be called from dispatcher thread.
    // TODO: Implement actual check when dispatcher supports it.
  }

 private:
  // ===== Private Implementation =====

  /**
   * Initialize valid state transitions
   */
  void initializeTransitions();

  /**
   * Execute state transition
   */
  void executeTransition(ClientSseState new_state,
                         ClientSseEvent event,
                         const std::string& reason,
                         CompletionCallback callback);

  /**
   * Notify state change listeners
   */
  void notifyStateChange(const ClientSseTransitionContext& context);

  /**
   * Record state transition in history
   */
  void recordStateTransition(const ClientSseTransitionContext& context);

  /**
   * Called when the negotiation timeout timer fires
   */
  void onNegotiationTimeout();

  // ===== Members =====

  // Core
  event::Dispatcher& dispatcher_;
  ClientSseStateMachineConfig config_;

  // Current state — plain enum, not atomic. All access is confined
  // to the dispatcher thread, so no synchronization is needed.
  ClientSseState current_state_;
  std::chrono::steady_clock::time_point state_entry_time_;

  // State history (capped to avoid unbounded growth)
  std::deque<ClientSseTransitionContext> state_history_;
  static constexpr size_t kMaxHistorySize = 50;

  // Valid transitions map
  std::unordered_map<ClientSseState, std::unordered_set<ClientSseState>>
      valid_transitions_;

  // Custom validators
  std::vector<ValidationCallback> custom_validators_;

  // Listeners
  std::vector<StateChangeCallback> state_change_listeners_;

  // Negotiation timeout timer — armed when entering WaitingForEndpoint,
  // cancelled when leaving it.
  event::TimerPtr negotiation_timer_;

  // Metrics
  uint64_t total_transitions_{0};

  // Reentrancy guard — true while a transition is being executed,
  // so a second handleEvent() call from within a state-change callback
  // defers via scheduleTransition() instead of recursing.
  bool transition_in_progress_{false};
};

}  // namespace filter
}  // namespace mcp

#endif  // MCP_FILTER_CLIENT_SSE_STATE_MACHINE_H
