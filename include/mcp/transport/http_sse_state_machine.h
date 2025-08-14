/**
 * @file http_sse_state_machine.h
 * @brief Production-grade HTTP+SSE state machine for event-driven transport
 *
 * This implements a comprehensive state machine for HTTP+SSE transport that:
 * - Manages HTTP connection lifecycle
 * - Handles SSE stream establishment and maintenance
 * - Provides automatic reconnection with exponential backoff
 * - Supports both client and server modes
 * - Thread-safe through dispatcher thread confinement
 * - Observable state transitions for monitoring
 *
 * Design principles following industrial best practices:
 * - Lock-free: All operations in dispatcher thread (no mutexes)
 * - State validation: Enforce valid transitions only
 * - Async-first: All state changes are event-driven
 * - Observable: Comprehensive state tracking and callbacks
 * - Resilient: Graceful error handling and recovery
 * - Performant: Optimized for high-frequency SSE events
 *
 * Architecture inspired by:
 * - Production proxy connection management patterns
 * - SSL state machine design for consistency
 * - HTTP/1.1 and HTTP/2 codec state management
 * - Industrial event-driven architectures (Node.js, Nginx, HAProxy)
 */

#ifndef MCP_TRANSPORT_HTTP_SSE_STATE_MACHINE_H
#define MCP_TRANSPORT_HTTP_SSE_STATE_MACHINE_H

#include <atomic>
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
 * HTTP+SSE Connection States
 * 
 * Comprehensive state model covering:
 * - TCP connection lifecycle
 * - HTTP protocol negotiation
 * - SSE stream establishment
 * - Reconnection and recovery
 * - Graceful shutdown
 *
 * States are designed to be:
 * - Mutually exclusive (only one state at a time)
 * - Fully observable (state changes trigger callbacks)
 * - Recoverable (error states can transition to recovery)
 * 
 * Naming conventions:
 * - Past participle (-ed) for completed states
 * - Present participle (-ing) for ongoing actions
 * - Adjectives for conditions/modes
 */
enum class HttpSseState {
  // ===== Initial States =====
  Uninitialized,      // Socket not yet initialized
  Initialized,        // Ready to connect
  
  // ===== TCP Connection States =====
  TcpConnecting,      // TCP connection in progress
  TcpConnected,       // TCP established, ready for HTTP
  
  // ===== HTTP Request States (Client) =====
  HttpRequestPreparing,    // Preparing HTTP request
  HttpRequestSending,      // Sending HTTP request headers
  HttpRequestBodySending,  // Sending HTTP request body (if any)
  HttpRequestSent,         // Full request sent, awaiting response
  
  // ===== HTTP Response States =====
  HttpResponseWaiting,         // Waiting for HTTP response
  HttpResponseHeadersReceiving, // Receiving/parsing HTTP headers
  HttpResponseUpgrading,       // Processing HTTP upgrade (if needed)
  HttpResponseBodyReceiving,   // Receiving HTTP response body
  
  // ===== SSE Stream States =====
  SseNegotiating,          // Negotiating SSE stream parameters
  SseStreamActive,         // SSE stream established and active
  SseEventBuffering,       // Buffering partial SSE event
  SseEventReceived,        // Complete SSE event received
  SseKeepAliveReceiving,   // Processing SSE keep-alive/comment
  
  // ===== Server States =====
  ServerListening,         // Server waiting for connections
  ServerConnectionAccepted,// Server accepted connection
  ServerRequestReceiving,  // Server receiving HTTP request
  ServerResponseSending,   // Server sending HTTP response
  ServerSsePushing,        // Server pushing SSE events
  
  // ===== Reconnection States =====
  ReconnectScheduled,      // Reconnection scheduled after failure
  ReconnectWaiting,        // In exponential backoff period
  ReconnectAttempting,     // Attempting to reconnect
  
  // ===== Shutdown States =====
  ShutdownInitiated,       // Graceful shutdown started
  ShutdownDraining,        // Draining pending data
  ShutdownCompleted,       // Shutdown handshake complete
  
  // ===== Terminal States =====
  Closed,                  // Connection closed cleanly
  Error,                   // Connection terminated due to error
  
  // ===== Degraded States =====
  HttpOnlyMode,            // HTTP works but SSE unavailable
  PartialDataReceived      // Incomplete data received
};

/**
 * HTTP+SSE Mode - determines valid transitions and behavior
 */
enum class HttpSseMode { 
  Client,   // Client initiates connection
  Server    // Server accepts connections
};

/**
 * Stream reset reasons for connection failures
 */
enum class StreamResetReason {
  None,
  ConnectionFailure,
  ConnectionTermination,
  LocalReset,
  RemoteReset,
  Timeout,
  ProtocolError,
  OverflowError
};

/**
 * HTTP SSE state transition result with detailed error information
 */
struct HttpSseStateTransitionResult {
  bool success;
  std::string error_message;
  HttpSseState resulting_state;
  optional<std::chrono::milliseconds> retry_after;  // For reconnection
  
  static HttpSseStateTransitionResult Success(HttpSseState state) {
    return {true, "", state, nullopt};
  }
  
  static HttpSseStateTransitionResult Failure(const std::string& error) {
    return {false, error, HttpSseState::Error, nullopt};
  }
  
  static HttpSseStateTransitionResult Retry(const std::string& error, 
                                            std::chrono::milliseconds retry_after) {
    return {false, error, HttpSseState::ReconnectScheduled, retry_after};
  }
};

/**
 * HTTP SSE State Machine
 *
 * Lock-free state machine running entirely in dispatcher thread.
 * Provides comprehensive state management for HTTP+SSE transport.
 *
 * Thread model:
 * - ALL methods must be called from dispatcher thread
 * - No synchronization primitives used
 * - State transitions are atomic within event loop
 * - Callbacks executed asynchronously
 *
 * Usage pattern:
 * @code
 * auto machine = HttpSseStateMachineFactory::createClientStateMachine(dispatcher);
 * machine->addStateChangeListener([](auto old, auto new) {
 *   LOG(INFO) << "State changed: " << old << " -> " << new;
 * });
 * machine->transition(HttpSseState::TcpConnecting);
 * @endcode
 */
class HttpSseStateMachine {
 public:
  /**
   * Callback types for async operations
   */
  using StateChangeCallback = 
      std::function<void(HttpSseState old_state, HttpSseState new_state)>;
  using TransitionCompleteCallback = 
      std::function<void(bool success, const std::string& error)>;
  using StateAction = 
      std::function<void(HttpSseState state, std::function<void()> done)>;
  using TransitionValidator = 
      std::function<bool(HttpSseState from, HttpSseState to)>;
  using ReconnectStrategy = 
      std::function<std::chrono::milliseconds(uint32_t attempt)>;
  
  /**
   * Stream state tracking for HTTP request/response pairs
   * Tracks individual HTTP request/response pairs or SSE event streams
   */
  struct StreamState {
    std::string stream_id;                              // Unique stream identifier
    HttpSseState request_state{HttpSseState::Uninitialized};
    HttpSseState response_state{HttpSseState::Uninitialized};
    
    // State flags (using bitfields for efficiency)
    bool headers_complete : 1;
    bool body_complete : 1;
    bool trailers_complete : 1;
    bool end_stream_sent : 1;
    bool end_stream_received : 1;
    bool reset_called : 1;
    bool zombie_stream : 1;  // Logically closed but awaiting cleanup
    bool above_watermark : 1;  // Flow control
    
    // Timing information
    std::chrono::steady_clock::time_point created_time;
    optional<std::chrono::steady_clock::time_point> first_byte_time;
    optional<std::chrono::steady_clock::time_point> complete_time;
    optional<std::chrono::milliseconds> timeout_duration;
    
    // Metrics
    uint64_t bytes_sent{0};
    uint64_t bytes_received{0};
    uint64_t events_received{0};  // SSE events
    
    StreamState(const std::string& id) 
        : stream_id(id),
          headers_complete(false),
          body_complete(false),
          trailers_complete(false),
          end_stream_sent(false),
          end_stream_received(false),
          reset_called(false),
          zombie_stream(false),
          above_watermark(false),
          created_time(std::chrono::steady_clock::now()) {}
  };
  
  /**
   * Create state machine for given mode
   * @param mode Client or Server mode
   * @param dispatcher Event dispatcher (must be called from its thread)
   */
  HttpSseStateMachine(HttpSseMode mode, event::Dispatcher& dispatcher);
  
  ~HttpSseStateMachine();
  
  /**
   * Get current state (immediate, thread-safe from dispatcher thread)
   * @note Must be called from dispatcher thread
   */
  HttpSseState getCurrentState() const {
    assertInDispatcherThread();
    return current_state_;
  }
  
  /**
   * Check if transition is valid
   * 
   * Validates against:
   * - Built-in transition rules
   * - Custom validators
   * - Current mode (client/server)
   * 
   * @param from Source state
   * @param to Target state
   * @return true if transition is valid
   * @note Must be called from dispatcher thread
   */
  bool canTransition(HttpSseState from, HttpSseState to) const;
  
  /**
   * Perform async state transition
   * 
   * Flow (all in dispatcher thread):
   * 1. Validate transition
   * 2. Execute async exit action for current state
   * 3. Update state atomically
   * 4. Execute async entry action for new state
   * 5. Notify state change listeners
   * 6. Invoke completion callback
   * 
   * @param new_state Target state
   * @param callback Called when transition completes (may be null)
   * @note Must be called from dispatcher thread
   */
  void transition(HttpSseState new_state,
                  TransitionCompleteCallback callback = nullptr);
  
  /**
   * Schedule state transition in next event loop iteration
   * Prevents stack overflow in recursive transitions
   * 
   * @param new_state Target state
   * @param callback Completion callback (may be null)
   * @note Must be called from dispatcher thread
   */
  void scheduleTransition(HttpSseState new_state,
                          TransitionCompleteCallback callback = nullptr) {
    assertInDispatcherThread();
    dispatcher_.post(
        [this, new_state, callback]() { transition(new_state, callback); });
  }
  
  /**
   * Force transition (bypass validation) - use with extreme caution
   * Only for recovery from invalid states
   * 
   * @param new_state Target state
   * @note Must be called from dispatcher thread
   */
  void forceTransition(HttpSseState new_state);
  
  // ===== State Change Observers =====
  
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
  
  // ===== State Actions =====
  
  /**
   * Register async entry action for a state
   * Called when entering the state
   * 
   * @param state State to register action for
   * @param action Async action with completion callback
   */
  void setEntryAction(HttpSseState state, StateAction action) {
    assertInDispatcherThread();
    entry_actions_[state] = action;
  }
  
  /**
   * Register async exit action for a state
   * Called when leaving the state
   * 
   * @param state State to register action for
   * @param action Async action with completion callback
   */
  void setExitAction(HttpSseState state, StateAction action) {
    assertInDispatcherThread();
    exit_actions_[state] = action;
  }
  
  /**
   * Register custom transition validator
   * Adds additional validation logic beyond built-in rules
   * 
   * @param validator Additional validation logic
   */
  void addTransitionValidator(TransitionValidator validator) {
    assertInDispatcherThread();
    custom_validators_.push_back(validator);
  }
  
  // ===== Stream Management =====
  
  /**
   * Create new stream for tracking request/response or SSE events
   * @param stream_id Unique identifier for the stream
   * @return Pointer to created stream state
   */
  StreamState* createStream(const std::string& stream_id);
  
  /**
   * Get stream by ID
   * @param stream_id Stream identifier
   * @return Pointer to stream state or nullptr if not found
   */
  StreamState* getStream(const std::string& stream_id);
  
  /**
   * Reset stream and mark for cleanup
   * @param stream_id Stream to reset
   * @param reason Reset reason
   */
  void resetStream(const std::string& stream_id, StreamResetReason reason);
  
  /**
   * Mark stream as zombie (logically closed but awaiting cleanup)
   * @param stream_id Stream to mark as zombie
   */
  void markStreamAsZombie(const std::string& stream_id);
  
  /**
   * Clean up zombie streams
   * Should be called periodically or when resources are needed
   */
  void cleanupZombieStreams();
  
  // ===== HTTP Protocol Events =====
  
  /**
   * Handle HTTP request sent (client)
   * @param stream_id Associated stream
   */
  void onHttpRequestSent(const std::string& stream_id);
  
  /**
   * Handle HTTP response received (client)
   * @param stream_id Associated stream
   * @param status_code HTTP status code
   */
  void onHttpResponseReceived(const std::string& stream_id, int status_code);
  
  /**
   * Handle SSE event received
   * @param stream_id Associated stream
   * @param event_data Event data
   */
  void onSseEventReceived(const std::string& stream_id, 
                         const std::string& event_data);
  
  // ===== Reconnection Management =====
  
  /**
   * Set reconnection strategy
   * @param strategy Function to calculate backoff delay
   */
  void setReconnectStrategy(ReconnectStrategy strategy) {
    assertInDispatcherThread();
    reconnect_strategy_ = strategy;
  }
  
  /**
   * Schedule reconnection attempt
   * Uses configured reconnection strategy for backoff
   */
  void scheduleReconnect();
  
  /**
   * Cancel pending reconnection
   */
  void cancelReconnect();
  
  /**
   * Get reconnection attempt count
   */
  uint32_t getReconnectAttempt() const { return reconnect_attempt_; }
  
  // ===== State Timeout Management =====
  
  /**
   * Set timeout for current state
   * @param timeout Timeout duration
   * @param timeout_state State to transition to on timeout
   */
  void setStateTimeout(std::chrono::milliseconds timeout,
                       HttpSseState timeout_state);
  
  /**
   * Cancel current state timeout
   */
  void cancelStateTimeout();
  
  // ===== Helper Methods =====
  
  /**
   * Get human-readable state name
   */
  static std::string getStateName(HttpSseState state);
  
  /**
   * Check if in terminal state
   */
  bool isTerminalState() const {
    return current_state_ == HttpSseState::Closed ||
           current_state_ == HttpSseState::Error;
  }
  
  /**
   * Check if currently connected (HTTP or SSE)
   */
  bool isConnected() const {
    return current_state_ == HttpSseState::SseStreamActive ||
           current_state_ == HttpSseState::HttpOnlyMode;
  }
  
  /**
   * Check if SSE stream is active
   */
  bool isSseActive() const {
    return current_state_ == HttpSseState::SseStreamActive ||
           current_state_ == HttpSseState::SseEventBuffering ||
           current_state_ == HttpSseState::SseEventReceived;
  }
  
  /**
   * Check if in reconnection state
   */
  bool isReconnecting() const {
    return current_state_ == HttpSseState::ReconnectScheduled ||
           current_state_ == HttpSseState::ReconnectWaiting ||
           current_state_ == HttpSseState::ReconnectAttempting;
  }
  
  /**
   * Get mode (client/server)
   */
  HttpSseMode getMode() const { return mode_; }
  
  /**
   * Get time in current state
   */
  std::chrono::milliseconds getTimeInCurrentState() const;
  
  /**
   * Get state history for debugging
   * @param max_entries Maximum number of entries to return
   */
  std::vector<std::pair<HttpSseState, std::chrono::steady_clock::time_point>>
  getStateHistory(size_t max_entries = 10) const;
  
  /**
   * Get stream statistics
   */
  struct StreamStats {
    size_t active_streams;
    size_t zombie_streams;
    uint64_t total_bytes_sent;
    uint64_t total_bytes_received;
    uint64_t total_events_received;
    std::chrono::milliseconds avg_stream_lifetime;
  };
  StreamStats getStreamStats() const;
  
 private:
  /**
   * Initialize valid transitions for client/server
   */
  void initializeClientTransitions();
  void initializeServerTransitions();
  
  /**
   * Execute async entry/exit actions
   */
  void executeEntryAction(HttpSseState state, std::function<void()> done);
  void executeExitAction(HttpSseState state, std::function<void()> done);
  
  /**
   * Notify state change listeners
   */
  void notifyStateChange(HttpSseState old_state, HttpSseState new_state);
  
  /**
   * Validate state transition
   */
  bool isValidTransition(HttpSseState from, HttpSseState to) const;
  
  /**
   * Record state transition in history
   */
  void recordStateTransition(HttpSseState state);
  
  /**
   * Assert we're in dispatcher thread
   * In production, validates thread context
   */
  void assertInDispatcherThread() const {
    // Implementation would check thread ID
    // For now, assume all calls are from dispatcher thread
  }
  
  /**
   * Handle reconnection timeout
   */
  void onReconnectTimeout();
  
  /**
   * Clean up stream resources
   */
  void cleanupStream(const std::string& stream_id);
  
 private:
  // Configuration
  const HttpSseMode mode_;
  event::Dispatcher& dispatcher_;
  
  // Current state (no mutex needed - dispatcher thread only)
  HttpSseState current_state_{HttpSseState::Uninitialized};
  std::chrono::steady_clock::time_point state_entry_time_;
  
  // State history for debugging
  static constexpr size_t kMaxHistorySize = 50;
  std::vector<std::pair<HttpSseState, std::chrono::steady_clock::time_point>>
      state_history_;
  
  // Valid transitions map
  std::unordered_map<HttpSseState, std::unordered_set<HttpSseState>>
      valid_transitions_;
  
  // State actions (async)
  std::unordered_map<HttpSseState, StateAction> entry_actions_;
  std::unordered_map<HttpSseState, StateAction> exit_actions_;
  
  // Custom validators
  std::vector<TransitionValidator> custom_validators_;
  
  // State change listeners
  uint32_t next_listener_id_{1};
  std::unordered_map<uint32_t, StateChangeCallback> state_listeners_;
  
  // Stream management
  std::unordered_map<std::string, std::unique_ptr<StreamState>> active_streams_;
  std::vector<std::string> zombie_stream_ids_;  // Streams awaiting cleanup
  
  // Reconnection management
  ReconnectStrategy reconnect_strategy_;
  uint32_t reconnect_attempt_{0};
  event::TimerPtr reconnect_timer_;
  std::chrono::milliseconds reconnect_delay_{1000};  // Initial delay
  
  // State timeout
  event::TimerPtr state_timeout_timer_;
  HttpSseState timeout_state_;
  
  // Transition in progress flag (prevents reentrancy)
  bool transition_in_progress_{false};
  
  // Error tracking
  optional<Error> last_error_;
  std::string error_context_;
  
  // Metrics
  uint64_t total_transitions_{0};
  uint64_t failed_transitions_{0};
  uint64_t successful_reconnects_{0};
  uint64_t failed_reconnects_{0};
};

/**
 * Factory for creating configured state machines
 */
class HttpSseStateMachineFactory {
 public:
  /**
   * Create client state machine with standard configuration
   */
  static std::unique_ptr<HttpSseStateMachine> createClientStateMachine(
      event::Dispatcher& dispatcher);
  
  /**
   * Create server state machine with standard configuration
   */
  static std::unique_ptr<HttpSseStateMachine> createServerStateMachine(
      event::Dispatcher& dispatcher);
  
  /**
   * Create state machine with custom configuration
   */
  struct Config {
    HttpSseMode mode{HttpSseMode::Client};
    std::chrono::milliseconds default_timeout{30000};
    std::chrono::milliseconds reconnect_initial_delay{1000};
    std::chrono::milliseconds reconnect_max_delay{30000};
    uint32_t max_reconnect_attempts{10};
    bool enable_zombie_cleanup{true};
    std::chrono::milliseconds zombie_cleanup_interval{5000};
  };
  
  static std::unique_ptr<HttpSseStateMachine> createStateMachine(
      const Config& config,
      event::Dispatcher& dispatcher);
};

/**
 * Helper patterns for common HTTP+SSE operations
 */
class HttpSseStatePatterns {
 public:
  /**
   * Check if state represents active connection
   */
  static bool isConnectedState(HttpSseState state);
  
  /**
   * Check if state represents HTTP request phase
   */
  static bool isHttpRequestState(HttpSseState state);
  
  /**
   * Check if state represents HTTP response phase
   */
  static bool isHttpResponseState(HttpSseState state);
  
  /**
   * Check if state represents SSE streaming
   */
  static bool isSseStreamState(HttpSseState state);
  
  /**
   * Check if state allows sending data
   */
  static bool canSendData(HttpSseState state);
  
  /**
   * Check if state allows receiving data
   */
  static bool canReceiveData(HttpSseState state);
  
  /**
   * Get next expected state in HTTP request flow
   */
  static optional<HttpSseState> getNextHttpRequestState(HttpSseState current);
  
  /**
   * Get next expected state in HTTP response flow
   */
  static optional<HttpSseState> getNextHttpResponseState(HttpSseState current);
  
  /**
   * Check if state represents an error
   */
  static bool isErrorState(HttpSseState state);
  
  /**
   * Check if state allows reconnection
   */
  static bool canReconnect(HttpSseState state);
};

/**
 * Transition coordinator for complex HTTP+SSE sequences
 */
class HttpSseTransitionCoordinator {
 public:
  HttpSseTransitionCoordinator(HttpSseStateMachine& machine) 
      : machine_(machine) {}
  
  /**
   * Execute full HTTP+SSE connection sequence
   * @param url Target URL
   * @param headers HTTP headers
   * @param callback Completion callback
   */
  void executeConnection(const std::string& url,
                        const std::unordered_map<std::string, std::string>& headers,
                        std::function<void(bool)> callback);
  
  /**
   * Execute HTTP request sequence
   * @param stream_id Stream identifier
   * @param callback Completion callback
   */
  void executeHttpRequest(const std::string& stream_id,
                         std::function<void(bool)> callback);
  
  /**
   * Execute SSE stream establishment
   * @param stream_id Stream identifier
   * @param callback Completion callback
   */
  void executeSseNegotiation(const std::string& stream_id,
                            std::function<void(bool)> callback);
  
  /**
   * Execute graceful shutdown sequence
   * @param callback Completion callback
   */
  void executeShutdown(std::function<void(bool)> callback);
  
  /**
   * Execute reconnection sequence with backoff
   * @param callback Completion callback
   */
  void executeReconnection(std::function<void(bool)> callback);
  
 private:
  HttpSseStateMachine& machine_;
  
  /**
   * Execute state sequence
   * @param states Sequence of states to transition through
   * @param index Current index in sequence
   * @param callback Completion callback
   */
  void executeSequence(const std::vector<HttpSseState>& states,
                      size_t index,
                      std::function<void(bool)> callback);
};

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_HTTP_SSE_STATE_MACHINE_H