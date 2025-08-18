/**
 * @file http_codec_state_machine.h
 * @brief HTTP/1.1 codec state machine following MCP transport patterns
 *
 * Design principles:
 * - Thread-confined to dispatcher thread (lock-free)
 * - Async-first with completion callbacks
 * - Observable state transitions with listeners
 * - Pluggable validation and error handling
 * - Uniform interface matching TransportSocketStateMachine pattern
 *
 * Based on patterns from:
 * - MCP's TransportSocketStateMachine
 * - MCP's SslStateMachine
 * - MCP's ConnectionStateMachine
 * - Industrial HTTP proxy best practices
 */

#ifndef MCP_FILTER_HTTP_CODEC_STATE_MACHINE_H
#define MCP_FILTER_HTTP_CODEC_STATE_MACHINE_H

#include <atomic>
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

namespace mcp {
namespace filter {

/**
 * HTTP codec states - supports both client and server modes
 */
enum class HttpCodecState {
  // Common states
  Idle,  // Initial state, no active request/response

  // Server mode states
  WaitingForRequest,        // Server: waiting for request
  ReceivingRequestHeaders,  // Server: parsing request headers
  ReceivingRequestBody,     // Server: receiving request body
  SendingResponse,          // Server: sending response (headers + body)

  // Client mode states
  SendingRequest,            // Client: sending request (headers + body)
  WaitingForResponse,        // Client: waiting for response
  ReceivingResponseHeaders,  // Client: parsing response headers
  ReceivingResponseBody,     // Client: receiving response body

  // Terminal states
  Closed,  // Connection closed
  Error    // Protocol or I/O error
};

/**
 * HTTP codec events - supports both client and server modes
 */
enum class HttpCodecEvent {
  // Request events (server receives, client sends)
  RequestBegin,  // Server: incoming request start, Client: outgoing request
                 // start
  RequestHeadersComplete,  // Server: request headers complete, Client: request
                           // headers sent
  RequestBodyData,  // Server: request body data, Client: request body data sent
  RequestComplete,  // Server: request complete, Client: request fully sent

  // Response events (server sends, client receives)
  ResponseBegin,  // Server: start sending response, Client: response received
  ResponseHeadersComplete,  // Server: response headers sent, Client: response
                            // headers received
  ResponseBodyData,  // Server: response body data sent, Client: response body
                     // data received
  ResponseComplete,  // Server: response fully sent, Client: response fully
                     // received

  // Error events
  ParseError,
  Timeout,

  // Control events
  Reset,  // Reset for keep-alive
  Close   // Close connection
};

/**
 * State transition result for HTTP codec
 */
struct HttpCodecStateTransitionResult {
  bool success{false};
  std::string error_message;
  HttpCodecState resulting_state;

  static HttpCodecStateTransitionResult Success(HttpCodecState state) {
    return {true, "", state};
  }

  static HttpCodecStateTransitionResult Failure(const std::string& error) {
    return {false, error, HttpCodecState::Error};
  }
};

/**
 * State transition context with detailed information
 */
struct HttpCodecStateTransitionContext {
  HttpCodecState from_state;
  HttpCodecState to_state;
  HttpCodecEvent triggering_event;
  std::chrono::steady_clock::time_point timestamp;
  std::string reason;

  // Metrics
  std::chrono::milliseconds time_in_previous_state;
  uint64_t bytes_processed_in_state{0};
  uint32_t requests_in_state{0};
};

/**
 * HTTP codec state machine configuration - supports client and server modes
 */
struct HttpCodecStateMachineConfig {
  // Mode
  bool is_server{true};  // true for server mode, false for client mode

  // Timeouts
  std::chrono::milliseconds header_timeout{30000};  // 30s for headers
  std::chrono::milliseconds body_timeout{60000};    // 60s for body
  std::chrono::milliseconds idle_timeout{120000};  // 2min idle between requests

  // Limits
  uint32_t max_header_size{8192};    // 8KB max header size
  uint64_t max_body_size{10485760};  // 10MB max body size

  // Features
  bool enable_keep_alive{true};
  bool enable_chunked_encoding{true};

  // Callbacks
  std::function<void(const HttpCodecStateTransitionContext&)>
      state_change_callback;
  std::function<void(const std::string&)> error_callback;
};

/**
 * HTTP Codec State Machine
 *
 * Manages HTTP/1.1 protocol state transitions following MCP patterns.
 * Thread-confined to dispatcher thread for lock-free operation.
 */
class HttpCodecStateMachine {
 public:
  using StateChangeCallback =
      std::function<void(const HttpCodecStateTransitionContext&)>;
  using CompletionCallback = std::function<void(bool success)>;
  using ValidationCallback =
      std::function<bool(HttpCodecState from, HttpCodecState to)>;
  using StateAction =
      std::function<void(HttpCodecState state, std::function<void()> done)>;

  /**
   * Constructor
   *
   * @param dispatcher Event dispatcher for async operations
   * @param config State machine configuration
   */
  HttpCodecStateMachine(event::Dispatcher& dispatcher,
                        const HttpCodecStateMachineConfig& config);

  virtual ~HttpCodecStateMachine();

  // ===== Core State Machine Interface =====

  /**
   * Get current state
   */
  HttpCodecState currentState() const {
    return current_state_.load(std::memory_order_acquire);
  }

  /**
   * Handle an event
   *
   * @param event The event to process
   * @param callback Completion callback (optional)
   * @return Immediate result (async completion via callback)
   */
  HttpCodecStateTransitionResult handleEvent(
      HttpCodecEvent event, CompletionCallback callback = nullptr);

  /**
   * Transition to a new state
   *
   * @param new_state Target state
   * @param event Triggering event
   * @param reason Reason for transition
   * @param callback Completion callback
   */
  HttpCodecStateTransitionResult transitionTo(
      HttpCodecState new_state,
      HttpCodecEvent event,
      const std::string& reason,
      CompletionCallback callback = nullptr);

  /**
   * Force transition (bypasses validation)
   */
  void forceTransition(HttpCodecState new_state, const std::string& reason);

  /**
   * Schedule transition on next event loop iteration
   */
  void scheduleTransition(HttpCodecState new_state,
                          HttpCodecEvent event,
                          const std::string& reason,
                          CompletionCallback callback = nullptr);

  // ===== Protocol Operations =====

  /**
   * Reset for next request (keep-alive)
   */
  void resetForNextRequest(CompletionCallback callback = nullptr);

  /**
   * Set whether to expect a request body (for testing)
   */
  void setExpectRequestBody(bool expect_body) {
    expect_request_body_ = expect_body;
  }

  /**
   * Set whether to expect a response body (for client mode)
   */
  void setExpectResponseBody(bool expect_body) {
    expect_response_body_ = expect_body;
  }

  // ===== State Queries =====

  bool canReceiveRequest() const {
    return current_state_ == HttpCodecState::WaitingForRequest;
  }

  bool isReceivingRequestBody() const {
    return current_state_ == HttpCodecState::ReceivingRequestBody;
  }

  bool hasError() const { return current_state_ == HttpCodecState::Error; }

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
  const std::deque<HttpCodecStateTransitionContext>& getStateHistory() const {
    return state_history_;
  }

  // ===== State Actions =====

  /**
   * Register async entry action for a state
   */
  void setEntryAction(HttpCodecState state, StateAction action);

  /**
   * Register async exit action for a state
   */
  void setExitAction(HttpCodecState state, StateAction action);

  // ===== Validation =====

  /**
   * Add custom transition validator
   */
  void addTransitionValidator(ValidationCallback validator);

  /**
   * Check if transition is valid
   */
  bool isTransitionValid(HttpCodecState from,
                         HttpCodecState to,
                         HttpCodecEvent event) const;

  // ===== Metrics =====

  /**
   * Get time in current state
   */
  std::chrono::milliseconds getTimeInCurrentState() const;

  /**
   * Get total transitions
   */
  uint64_t getTotalTransitions() const {
    return total_transitions_.load(std::memory_order_relaxed);
  }

  /**
   * Get requests processed
   */
  uint64_t getRequestsProcessed() const {
    return requests_processed_.load(std::memory_order_relaxed);
  }

  // ===== Timer Management =====

  /**
   * Set timeout for current state
   */
  void setStateTimeout(std::chrono::milliseconds timeout,
                       HttpCodecState timeout_state);

  /**
   * Cancel current state timeout
   */
  void cancelStateTimeout();

  // ===== Utility =====

  /**
   * Get human-readable state name
   */
  static std::string getStateName(HttpCodecState state);

  /**
   * Get human-readable event name
   */
  static std::string getEventName(HttpCodecEvent event);

 protected:
  // ===== Protected Methods for Extension =====

  /**
   * Called before exiting a state
   */
  virtual void onStateExit(HttpCodecState state, CompletionCallback callback);

  /**
   * Called after entering a state
   */
  virtual void onStateEnter(HttpCodecState state, CompletionCallback callback);

  /**
   * Get valid transitions for a state
   */
  virtual std::unordered_set<HttpCodecState> getValidTransitions(
      HttpCodecState from) const;

  /**
   * Handle state-specific timeout
   */
  virtual void onStateTimeout(HttpCodecState state);

  /**
   * Assert we're in dispatcher thread
   */
  void assertInDispatcherThread() const {
    // All methods must be called from dispatcher thread
    // TODO: Implement actual check when dispatcher supports it
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
  void executeTransition(HttpCodecState new_state,
                         HttpCodecEvent event,
                         const std::string& reason,
                         CompletionCallback callback);

  /**
   * Execute async entry/exit actions
   */
  void executeEntryAction(HttpCodecState state, std::function<void()> done);
  void executeExitAction(HttpCodecState state, std::function<void()> done);

  /**
   * Notify state change listeners
   */
  void notifyStateChange(const HttpCodecStateTransitionContext& context);

  /**
   * Record state transition
   */
  void recordStateTransition(const HttpCodecStateTransitionContext& context);

  /**
   * Timer callbacks
   */
  void onHeaderTimeout();
  void onBodyTimeout();
  void onIdleTimeout();

  // ===== Members =====

  // Core
  event::Dispatcher& dispatcher_;
  HttpCodecStateMachineConfig config_;

  // Current state
  std::atomic<HttpCodecState> current_state_{HttpCodecState::WaitingForRequest};
  std::chrono::steady_clock::time_point state_entry_time_;

  // State history
  std::deque<HttpCodecStateTransitionContext> state_history_;
  static constexpr size_t kMaxHistorySize = 100;

  // Valid transitions
  std::unordered_map<HttpCodecState, std::unordered_set<HttpCodecState>>
      valid_transitions_;

  // State actions
  std::unordered_map<HttpCodecState, StateAction> entry_actions_;
  std::unordered_map<HttpCodecState, StateAction> exit_actions_;

  // Validators
  std::vector<ValidationCallback> custom_validators_;

  // Listeners
  std::vector<StateChangeCallback> state_change_listeners_;

  // Timers
  event::TimerPtr header_timer_;
  event::TimerPtr body_timer_;
  event::TimerPtr idle_timer_;

  // Metrics
  std::atomic<uint64_t> total_transitions_{0};
  std::atomic<uint64_t> requests_processed_{0};

  // State tracking
  bool transition_in_progress_{false};
  bool keep_alive_enabled_{true};
  bool expect_request_body_{false};   // Whether current request has a body
  bool expect_response_body_{false};  // Whether current response has a body
  size_t current_header_size_{0};
  size_t current_body_size_{0};
};

}  // namespace filter
}  // namespace mcp

#endif  // MCP_FILTER_HTTP_CODEC_STATE_MACHINE_H