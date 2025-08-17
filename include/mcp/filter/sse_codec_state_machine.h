/**
 * @file sse_codec_state_machine.h
 * @brief Server-Sent Events (SSE) codec state machine following MCP transport patterns
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
 * - SSE specification (W3C)
 * - Industrial streaming proxy best practices
 */

#ifndef MCP_FILTER_SSE_CODEC_STATE_MACHINE_H
#define MCP_FILTER_SSE_CODEC_STATE_MACHINE_H

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
 * SSE codec states - simplified to essential states only
 */
enum class SseCodecState {
  // Stream lifecycle
  Idle,               // Not streaming
  StreamActive,       // Stream established, can send events
  SendingEvent,       // Currently sending an event
  
  // Terminal states
  Closed,             // Stream closed
  Error               // Stream error
};

/**
 * SSE codec events - simplified
 */
enum class SseCodecEvent {
  // Stream events
  StartStream,
  StreamReady,
  
  // Event transmission
  SendEvent,
  EventSent,
  
  // Keep-alive
  KeepAliveTimer,
  
  // Error events
  StreamError,
  
  // Control events
  CloseStream,
  Reset
};

/**
 * State transition result for SSE codec
 */
struct SseCodecStateTransitionResult {
  bool success{false};
  std::string error_message;
  SseCodecState resulting_state;
  
  static SseCodecStateTransitionResult Success(SseCodecState state) {
    return {true, "", state};
  }
  
  static SseCodecStateTransitionResult Failure(const std::string& error) {
    return {false, error, SseCodecState::Error};
  }
};

/**
 * State transition context for SSE codec
 */
struct SseCodecStateTransitionContext {
  SseCodecState from_state;
  SseCodecState to_state;
  SseCodecEvent triggering_event;
  std::chrono::steady_clock::time_point timestamp;
  std::string reason;
  
  // Metrics
  std::chrono::milliseconds time_in_previous_state;
  uint64_t events_sent_in_state{0};
  uint64_t bytes_sent_in_state{0};
};

/**
 * SSE event data structure - simplified
 */
struct SseEventData {
  optional<std::string> id;        // Event ID
  optional<std::string> event;     // Event type
  std::string data;                 // Event data (required)
  optional<uint32_t> retry;        // Retry interval in ms
};

/**
 * SSE codec state machine configuration - simplified
 */
struct SseCodecStateMachineConfig {
  // Timeouts
  std::chrono::milliseconds keep_alive_interval{30000};  // 30s keep-alive
  std::chrono::milliseconds event_timeout{10000};        // 10s event send timeout
  
  // Limits
  uint32_t max_event_size{65536};          // 64KB max event
  
  // Features
  bool enable_keep_alive{true};            // Send keep-alive comments
  
  // Callbacks
  std::function<void(const SseCodecStateTransitionContext&)> state_change_callback;
  std::function<void(const std::string&)> error_callback;
  std::function<void()> keep_alive_callback;
};

/**
 * SSE Codec State Machine
 *
 * Manages Server-Sent Events protocol state transitions following MCP patterns.
 * Thread-confined to dispatcher thread for lock-free operation.
 */
class SseCodecStateMachine {
public:
  using StateChangeCallback = std::function<void(const SseCodecStateTransitionContext&)>;
  using CompletionCallback = std::function<void(bool success)>;
  using ValidationCallback = std::function<bool(SseCodecState from, SseCodecState to)>;
  using StateAction = std::function<void(SseCodecState state, std::function<void()> done)>;
  using EventCallback = std::function<void(const SseEventData&)>;
  
  /**
   * Constructor
   *
   * @param dispatcher Event dispatcher for async operations
   * @param config State machine configuration
   */
  SseCodecStateMachine(event::Dispatcher& dispatcher,
                      const SseCodecStateMachineConfig& config);
  
  virtual ~SseCodecStateMachine();
  
  // ===== Core State Machine Interface =====
  
  /**
   * Get current state
   */
  SseCodecState currentState() const {
    return current_state_.load(std::memory_order_acquire);
  }
  
  /**
   * Handle an event
   *
   * @param event The event to process
   * @param callback Completion callback (optional)
   * @return Immediate result (async completion via callback)
   */
  SseCodecStateTransitionResult handleEvent(SseCodecEvent event,
                                           CompletionCallback callback = nullptr);
  
  /**
   * Transition to a new state
   *
   * @param new_state Target state
   * @param event Triggering event
   * @param reason Reason for transition
   * @param callback Completion callback
   */
  SseCodecStateTransitionResult transitionTo(SseCodecState new_state,
                                             SseCodecEvent event,
                                             const std::string& reason,
                                             CompletionCallback callback = nullptr);
  
  /**
   * Force transition (bypasses validation)
   */
  void forceTransition(SseCodecState new_state, const std::string& reason);
  
  /**
   * Schedule transition on next event loop iteration
   */
  void scheduleTransition(SseCodecState new_state,
                         SseCodecEvent event,
                         const std::string& reason,
                         CompletionCallback callback = nullptr);
  
  // ===== Stream Operations =====
  
  /**
   * Start SSE stream
   */
  void startStream(CompletionCallback callback = nullptr);
  
  /**
   * Close SSE stream
   */
  void closeStream(CompletionCallback callback = nullptr);
  
  /**
   * Send keep-alive
   */
  void sendKeepAlive(CompletionCallback callback = nullptr);
  
  // ===== State Queries =====
  
  bool canStartStream() const {
    return current_state_ == SseCodecState::Idle;
  }
  
  bool isStreaming() const {
    return current_state_ == SseCodecState::StreamActive || 
           current_state_ == SseCodecState::SendingEvent;
  }
  
  bool canSendEvent() const {
    return current_state_ == SseCodecState::StreamActive;
  }
  
  bool hasError() const {
    return current_state_ == SseCodecState::Error;
  }
  
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
  const std::deque<SseCodecStateTransitionContext>& getStateHistory() const {
    return state_history_;
  }
  
  // ===== State Actions =====
  
  /**
   * Register async entry action for a state
   */
  void setEntryAction(SseCodecState state, StateAction action);
  
  /**
   * Register async exit action for a state
   */
  void setExitAction(SseCodecState state, StateAction action);
  
  // ===== Validation =====
  
  /**
   * Add custom transition validator
   */
  void addTransitionValidator(ValidationCallback validator);
  
  /**
   * Check if transition is valid
   */
  bool isTransitionValid(SseCodecState from, SseCodecState to,
                        SseCodecEvent event) const;
  
  
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
   * Get events sent
   */
  uint64_t getEventsSent() const {
    return events_sent_.load(std::memory_order_relaxed);
  }
  
  /**
   * Get bytes sent
   */
  uint64_t getBytesSent() const {
    // TODO: Track bytes sent when integrated with transport
    return 0;
  }
  
  // ===== Timer Management =====
  
  /**
   * Start keep-alive timer
   */
  void startKeepAliveTimer();
  
  /**
   * Stop keep-alive timer
   */
  void stopKeepAliveTimer();
  
  /**
   * Set event timeout
   */
  void setEventTimeout(std::chrono::milliseconds timeout);
  
  // ===== Utility =====
  
  /**
   * Get human-readable state name
   */
  static std::string getStateName(SseCodecState state);
  
  /**
   * Get human-readable event name
   */
  static std::string getEventName(SseCodecEvent event);
  
  /**
   * Format SSE event for transmission
   */
  static std::string formatEvent(const SseEventData& event_data);
  
protected:
  // ===== Protected Methods for Extension =====
  
  /**
   * Called before exiting a state
   */
  virtual void onStateExit(SseCodecState state, CompletionCallback callback);
  
  /**
   * Called after entering a state
   */
  virtual void onStateEnter(SseCodecState state, CompletionCallback callback);
  
  /**
   * Get valid transitions for a state
   */
  virtual std::unordered_set<SseCodecState> getValidTransitions(
      SseCodecState from) const;
  
  /**
   * Handle state-specific timeout
   */
  virtual void onStateTimeout(SseCodecState state);
  
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
  void executeTransition(SseCodecState new_state,
                        SseCodecEvent event,
                        const std::string& reason,
                        CompletionCallback callback);
  
  /**
   * Execute async entry/exit actions
   */
  void executeEntryAction(SseCodecState state, std::function<void()> done);
  void executeExitAction(SseCodecState state, std::function<void()> done);
  
  /**
   * Notify state change listeners
   */
  void notifyStateChange(const SseCodecStateTransitionContext& context);
  
  /**
   * Record state transition
   */
  void recordStateTransition(const SseCodecStateTransitionContext& context);
  
  /**
   * Timer callbacks
   */
  void onKeepAliveTimer();
  void onEventTimeout();
  
  // ===== Members =====
  
  // Core
  event::Dispatcher& dispatcher_;
  SseCodecStateMachineConfig config_;
  
  // Current state
  std::atomic<SseCodecState> current_state_{SseCodecState::Idle};
  std::chrono::steady_clock::time_point state_entry_time_;
  
  // State history
  std::deque<SseCodecStateTransitionContext> state_history_;
  static constexpr size_t kMaxHistorySize = 100;
  
  // Valid transitions
  std::unordered_map<SseCodecState, std::unordered_set<SseCodecState>>
      valid_transitions_;
  
  // State actions
  std::unordered_map<SseCodecState, StateAction> entry_actions_;
  std::unordered_map<SseCodecState, StateAction> exit_actions_;
  
  // Validators
  std::vector<ValidationCallback> custom_validators_;
  
  // Listeners
  std::vector<StateChangeCallback> state_change_listeners_;
  
  // Timers
  event::TimerPtr keep_alive_timer_;
  event::TimerPtr event_timeout_timer_;
  
  // Metrics
  std::atomic<uint64_t> total_transitions_{0};
  std::atomic<uint64_t> events_sent_{0};
  
  // Stream state
  bool transition_in_progress_{false};
  std::string last_event_id_;
};


} // namespace filter
} // namespace mcp

#endif // MCP_FILTER_SSE_CODEC_STATE_MACHINE_H