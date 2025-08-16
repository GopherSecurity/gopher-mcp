/**
 * @file filter_chain_state_machine.h
 * @brief Filter Chain State Machine following production proxy patterns
 *
 * This state machine manages the lifecycle of filter chains in network
 * connections, coordinating filter initialization, data flow, and termination.
 * It follows industrial proxy design patterns with:
 * - Thread-safe, lock-free operation using dispatcher thread confinement
 * - Comprehensive state coverage for all filter chain phases
 * - Observable state transitions with callbacks
 * - Integration with MCP's buffer, event, and connection abstractions
 */

#ifndef MCP_NETWORK_FILTER_CHAIN_STATE_MACHINE_H
#define MCP_NETWORK_FILTER_CHAIN_STATE_MACHINE_H

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mcp/buffer.h"
#include "mcp/core/compat.h"
#include "mcp/event/event_loop.h"
#include "mcp/network/connection.h"
#include "mcp/network/filter.h"

namespace mcp {
namespace network {

/**
 * Filter Chain States
 *
 * Comprehensive state model covering:
 * - Initialization and setup
 * - Data processing phases
 * - Backpressure and flow control
 * - Error handling and recovery
 * - Graceful shutdown
 */
enum class FilterChainState {
  // Initial states
  Uninitialized,  // Chain not yet created
  Initializing,   // Creating filter instances

  // Setup states
  Configuring,  // Applying filter configuration
  Connecting,   // Filters connecting to upstream/downstream

  // Ready states
  Idle,    // Ready but no data flowing
  Active,  // Normal data processing

  // Data flow states
  ProcessingUpstream,    // Processing data from downstream to upstream
  ProcessingDownstream,  // Processing data from upstream to downstream

  // Flow control states
  Paused,     // Temporarily paused (backpressure)
  Buffering,  // Buffering data due to slow consumer
  Draining,   // Draining buffered data
  Flushing,   // Flushing all pending data

  // Filter iteration states
  IteratingRead,     // Iterating through read filters
  IteratingWrite,    // Iterating through write filters
  StoppedIteration,  // Filter stopped iteration

  // Watermark states
  AboveHighWatermark,  // Above high watermark, apply backpressure
  BelowLowWatermark,   // Below low watermark, resume flow

  // Error states
  FilterError,    // Filter reported error
  ProtocolError,  // Protocol violation detected
  Recovering,     // Attempting recovery

  // Shutdown states
  Closing,     // Graceful close initiated
  Aborting,    // Immediate abort requested
  Terminated,  // Chain terminated

  // Terminal states
  Closed,  // Clean shutdown completed
  Failed   // Unrecoverable error
};

/**
 * Filter Chain Events
 */
enum class FilterChainEvent {
  // Initialization events
  InitializeRequested,
  FilterAdded,
  FilterRemoved,
  ConfigurationComplete,

  // Connection events
  ConnectionEstablished,
  ConnectionClosed,
  ConnectionError,

  // Data events
  DataReceived,
  DataToSend,
  EndOfStream,

  // Filter events
  FilterContinue,
  FilterStopIteration,
  FilterError,
  FilterRequestClose,

  // Flow control events
  PauseRequested,
  ResumeRequested,
  BufferHighWatermark,
  BufferLowWatermark,

  // Shutdown events
  CloseRequested,
  AbortRequested,
  DrainComplete,
  FlushComplete
};

/**
 * Filter chain mode
 */
enum class FilterChainMode {
  ReadOnly,      // Only read filters
  WriteOnly,     // Only write filters
  Bidirectional  // Both read and write filters
};

/**
 * Filter chain configuration
 */
struct FilterChainConfig {
  FilterChainMode mode = FilterChainMode::Bidirectional;

  // Buffer limits
  uint32_t max_buffered_bytes = 1024 * 1024;  // 1MB default
  uint32_t high_watermark = 768 * 1024;       // 768KB
  uint32_t low_watermark = 256 * 1024;        // 256KB

  // Timeouts
  std::chrono::milliseconds initialization_timeout{5000};
  std::chrono::milliseconds drain_timeout{30000};
  std::chrono::milliseconds idle_timeout{0};  // 0 = disabled

  // Behavior flags
  bool continue_on_filter_error = false;
  bool strict_filter_ordering = true;
  bool enable_filter_stats = true;

  // Callbacks
  std::function<void(FilterChainState, FilterChainState, const std::string&)>
      state_change_callback;
  std::function<void(const std::string&)> error_callback;
};

// Forward declaration of FilterEntry
struct FilterEntry;

/**
 * Filter chain statistics
 */
struct FilterChainStats {
  size_t filters_initialized = 0;
  size_t filters_failed = 0;
  size_t bytes_processed_upstream = 0;
  size_t bytes_processed_downstream = 0;
  size_t iterations_stopped = 0;
  size_t buffer_overflows = 0;
  std::chrono::nanoseconds total_processing_time{0};
};

/**
 * Filter Chain State Machine
 *
 * Thread-safe state machine managing filter chain lifecycle.
 * All operations must be called from the dispatcher thread.
 */
class FilterChainStateMachine {
 public:
  FilterChainStateMachine(event::Dispatcher& dispatcher,
                          Connection& connection,
                          const FilterChainConfig& config);

  ~FilterChainStateMachine();

  // State machine control
  bool initialize();
  bool start();
  bool pause();
  bool resume();
  bool close();
  bool abort();

  // Filter management
  bool addReadFilter(ReadFilterSharedPtr filter, const std::string& name);
  bool addWriteFilter(WriteFilterSharedPtr filter, const std::string& name);
  bool removeFilter(const std::string& name);
  void reorderFilters(const std::vector<std::string>& order);

  // Data flow
  FilterStatus onData(Buffer& data, bool end_stream);
  FilterStatus onWrite(Buffer& data, bool end_stream);

  // Additional filter management methods
  void addReadFilter(ReadFilterSharedPtr filter);
  void addWriteFilter(WriteFilterSharedPtr filter);
  void addFilter(FilterSharedPtr filter);
  bool initializeReadFilters();
  void onContinueReading(ReadFilter* filter);

  // State queries
  FilterChainState currentState() const { return current_state_.load(); }
  bool isActive() const;
  bool canProcessData() const;
  bool isTerminal() const;

  // Statistics
  const FilterChainStats& stats() const { return stats_; }
  size_t activeFilterCount() const;
  size_t bufferedBytes() const;

  // Event handling
  bool handleEvent(FilterChainEvent event);

 private:
  // State management
  bool transitionTo(FilterChainState new_state,
                    FilterChainEvent event,
                    const std::string& reason);
  bool isValidTransition(FilterChainState from, FilterChainState to) const;
  void onStateEnter(FilterChainState state);
  void onStateExit(FilterChainState state);

  // Filter iteration
  FilterStatus iterateReadFilters(Buffer& data, bool end_stream);
  FilterStatus iterateWriteFilters(Buffer& data, bool end_stream);
  void continueIteration();

  // Buffer management
  void checkWatermarks();
  void applyBackpressure();
  void releaseBackpressure();
  bool bufferData(Buffer& data);
  void drainBuffers();

  // Timer management
  void startInitializationTimer();
  void startDrainTimer();
  void startIdleTimer();
  void cancelTimers();

  // Error handling
  void handleFilterError(const std::string& filter_name,
                         const std::string& error);
  bool attemptRecovery();

  // Helpers
  void assertInDispatcherThread() const;
  void updateStats(size_t bytes, bool upstream);

 private:
  // Core components
  event::Dispatcher& dispatcher_;
  Connection& connection_;
  FilterChainConfig config_;

  // State
  std::atomic<FilterChainState> current_state_{FilterChainState::Uninitialized};
  std::atomic<size_t> transition_count_{0};

  // Filters
  std::vector<FilterEntry> read_filters_;
  std::vector<FilterEntry> write_filters_;
  std::unordered_map<std::string, FilterEntry*> filter_map_;

  // Iteration state
  size_t read_filter_index_{0};
  size_t write_filter_index_{0};
  bool iteration_stopped_{false};
  ReadFilter* stopped_read_filter_{nullptr};
  WriteFilter* stopped_write_filter_{nullptr};

  // Buffers
  OwnedBuffer upstream_buffer_;
  OwnedBuffer downstream_buffer_;
  std::atomic<size_t> buffered_bytes_{0};

  // Flow control
  bool backpressure_applied_{false};
  bool above_high_watermark_{false};

  // Timers
  event::TimerPtr initialization_timer_;
  event::TimerPtr drain_timer_;
  event::TimerPtr idle_timer_;

  // Statistics
  FilterChainStats stats_;
  std::chrono::steady_clock::time_point last_activity_;

  // Thread safety
  mutable std::atomic<std::thread::id> dispatcher_thread_id_;
};

/**
 * Filter Chain State Patterns
 *
 * Helper functions for common state patterns
 */
class FilterChainStatePatterns {
 public:
  static bool canProcessData(FilterChainState state);
  static bool isFlowControlled(FilterChainState state);
  static bool isErrorState(FilterChainState state);
  static bool isTerminal(FilterChainState state);
  static bool isIterating(FilterChainState state);
};

/**
 * Filter Chain Builder
 *
 * Fluent interface for building filter chains
 */
class FilterChainBuilder {
 public:
  FilterChainBuilder& withMode(FilterChainMode mode);
  FilterChainBuilder& withBufferLimits(uint32_t max_bytes);
  FilterChainBuilder& withWatermarks(uint32_t high, uint32_t low);
  FilterChainBuilder& withInitTimeout(std::chrono::milliseconds timeout);
  FilterChainBuilder& withDrainTimeout(std::chrono::milliseconds timeout);
  FilterChainBuilder& withIdleTimeout(std::chrono::milliseconds timeout);
  FilterChainBuilder& continueOnError(bool enabled);
  FilterChainBuilder& withStrictOrdering(bool enabled);
  FilterChainBuilder& withStats(bool enabled);
  FilterChainBuilder& withStateChangeCallback(
      std::function<
          void(FilterChainState, FilterChainState, const std::string&)> cb);
  FilterChainBuilder& withErrorCallback(
      std::function<void(const std::string&)> cb);

  FilterChainBuilder& addReadFilter(const std::string& name,
                                    FilterFactoryCb factory);
  FilterChainBuilder& addWriteFilter(const std::string& name,
                                     FilterFactoryCb factory);

  std::unique_ptr<FilterChainStateMachine> build(event::Dispatcher& dispatcher,
                                                 Connection& connection);

 private:
  FilterChainConfig config_;
  std::vector<std::pair<std::string, FilterFactoryCb>> read_filter_factories_;
  std::vector<std::pair<std::string, FilterFactoryCb>> write_filter_factories_;
};

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_FILTER_CHAIN_STATE_MACHINE_H