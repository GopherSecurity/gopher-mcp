/**
 * @file connection_state_machine.h
 * @brief Production-grade Connection State Machine following industrial proxy patterns
 *
 * This implements a comprehensive connection state machine that:
 * - Manages the full connection lifecycle from socket creation to closure
 * - Coordinates between transport socket, filter chain, and event loop
 * - Provides automatic error recovery and reconnection capabilities
 * - Supports both client and server connection modes
 * - Thread-safe through dispatcher thread confinement
 * - Observable state transitions for monitoring and debugging
 *
 * Design principles following production best practices:
 * - Lock-free: All operations in dispatcher thread (no mutexes needed)
 * - Event-driven: State changes triggered by I/O events and timers
 * - Composable: Works with filter chains and transport sockets
 * - Observable: Comprehensive state tracking with callbacks
 * - Resilient: Graceful error handling with configurable recovery
 * - Performant: Zero-copy I/O with watermark flow control
 *
 * Architecture inspired by:
 * - Production proxy connection management patterns
 * - Industrial connection pooling strategies
 * - MCP's existing SSL and HTTP+SSE state machines
 * - Event-driven network architectures
 *
 * Thread model:
 * - ALL state transitions happen in dispatcher thread
 * - Callbacks are invoked in dispatcher thread context
 * - No synchronization primitives needed
 */

#ifndef MCP_NETWORK_CONNECTION_STATE_MACHINE_H
#define MCP_NETWORK_CONNECTION_STATE_MACHINE_H

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mcp/buffer.h"
#include "mcp/core/result.h"
#include "mcp/event/event_loop.h"
#include "mcp/network/connection.h"
#include "mcp/network/filter.h"
#include "mcp/network/socket.h"
#include "mcp/network/transport_socket.h"
#include "mcp/stream_info/stream_info.h"

namespace mcp {
namespace network {

/**
 * Connection States
 *
 * Comprehensive state model covering the full connection lifecycle.
 * States are designed to match production connection management patterns
 * while providing clear semantics for each phase.
 *
 * State categories:
 * - Initial: Pre-connection states
 * - Connecting: Connection establishment phase
 * - Connected: Active connection states
 * - Closing: Shutdown sequence
 * - Terminal: Final states
 * - Recovery: Error recovery states
 */
enum class ConnectionState {
  // ===== Initial States =====
  Uninitialized,      // Connection not yet initialized
  Initialized,        // Socket created, ready to connect/accept
  
  // ===== Connecting States (Client) =====
  Resolving,          // DNS resolution in progress
  Connecting,         // TCP connection in progress
  TcpConnected,       // TCP established, transport socket handshake pending
  HandshakeInProgress,// Transport socket handshake (SSL, etc.)
  
  // ===== Accepting States (Server) =====
  Listening,          // Server socket listening for connections
  Accepting,          // Accepting incoming connection
  Accepted,           // Connection accepted, handshake pending
  
  // ===== Connected States =====
  Connected,          // Fully connected, ready for data transfer
  Reading,            // Actively reading data
  Writing,            // Actively writing data
  Idle,               // Connected but no active I/O
  Processing,         // Processing data through filter chain
  
  // ===== Flow Control States =====
  ReadDisabled,       // Read disabled (backpressure or application)
  WriteDisabled,      // Write disabled (backpressure)
  Paused,             // Both read and write paused
  
  // ===== Closing States =====
  HalfClosedLocal,    // Local side closed, remote still open
  HalfClosedRemote,   // Remote side closed, local still open
  Closing,            // Graceful close initiated
  Draining,           // Draining buffers before close
  Flushing,           // Flushing write buffer
  
  // ===== Terminal States =====
  Closed,             // Connection closed normally
  Error,              // Connection terminated due to error
  Aborted,            // Connection aborted (immediate close)
  
  // ===== Recovery States =====
  Reconnecting,       // Attempting to reconnect
  WaitingToReconnect, // In backoff period before reconnect
  Recovering          // Recovering from temporary error
};

/**
 * Connection mode - determines behavior and valid transitions
 */
enum class ConnectionMode {
  Client,             // Client-initiated connection
  Server,             // Server-accepted connection
  Bidirectional       // Can act as both (for proxies)
};

/**
 * Connection events that can trigger state transitions
 */
enum class ConnectionStateMachineEvent {
  // Socket events
  SocketCreated,
  SocketBound,
  SocketListening,
  SocketConnected,
  SocketAccepted,
  SocketClosed,
  SocketError,
  
  // I/O events
  ReadReady,
  WriteReady,
  ReadComplete,
  WriteComplete,
  EndOfStream,
  
  // Transport socket events
  HandshakeStarted,
  HandshakeComplete,
  HandshakeFailed,
  
  // Filter chain events
  FilterChainInitialized,
  FilterChainContinue,
  FilterChainStop,
  
  // Application events
  ConnectionRequested,
  CloseRequested,
  ResetRequested,
  ReadDisableRequested,
  WriteDisableRequested,
  
  // Timer events
  ConnectTimeout,
  IdleTimeout,
  DrainTimeout,
  
  // Error recovery events
  ReconnectRequested,
  RecoveryComplete,
  RecoveryFailed
};

/**
 * State transition context with detailed information
 */
struct StateTransitionContext {
  ConnectionState from_state;
  ConnectionState to_state;
  ConnectionStateMachineEvent triggering_event;
  std::chrono::steady_clock::time_point timestamp;
  std::string reason;
  optional<std::string> error_details;
  
  // Metrics
  std::chrono::milliseconds time_in_previous_state;
  uint64_t bytes_read_in_state{0};
  uint64_t bytes_written_in_state{0};
};

/**
 * Connection state machine configuration
 */
struct ConnectionStateMachineConfig {
  // Mode
  ConnectionMode mode{ConnectionMode::Client};
  
  // Timeouts
  std::chrono::milliseconds connect_timeout{30000};
  std::chrono::milliseconds handshake_timeout{10000};
  std::chrono::milliseconds idle_timeout{0};        // 0 = no timeout
  std::chrono::milliseconds drain_timeout{5000};
  std::chrono::milliseconds delayed_close_timeout{1000};
  
  // Reconnection
  bool enable_auto_reconnect{false};
  uint32_t max_reconnect_attempts{3};
  std::chrono::milliseconds initial_reconnect_delay{1000};
  std::chrono::milliseconds max_reconnect_delay{30000};
  double reconnect_backoff_multiplier{2.0};
  
  // Flow control
  uint32_t read_buffer_limit{1048576};      // 1MB default
  uint32_t write_buffer_limit{1048576};     // 1MB default
  uint32_t high_watermark{655360};          // 640KB
  uint32_t low_watermark{163840};           // 160KB
  
  // Error handling
  bool abort_on_error{false};
  uint32_t max_consecutive_errors{10};
  
  // Features
  bool enable_half_close{true};
  bool detect_early_close{true};
  bool enable_tcp_nodelay{true};
  
  // Callbacks
  std::function<void(const StateTransitionContext&)> state_change_callback;
  std::function<void(const std::string&)> error_callback;
  std::function<void(ConnectionState)> recovery_callback;
};

/**
 * Connection State Machine
 *
 * Manages the complete lifecycle of a network connection, coordinating
 * between the socket, transport layer, filter chain, and event loop.
 *
 * This class follows production connection management patterns:
 * - Thread-confined to dispatcher thread
 * - Event-driven state transitions
 * - Composable with filters and transport sockets
 * - Observable for monitoring and debugging
 *
 * Usage example:
 * @code
 * auto config = ConnectionStateMachineConfig{};
 * config.mode = ConnectionMode::Client;
 * config.enable_auto_reconnect = true;
 * 
 * auto state_machine = std::make_unique<ConnectionStateMachine>(
 *     dispatcher, connection, config);
 * 
 * state_machine->addStateChangeListener([](const auto& ctx) {
 *   LOG(INFO) << "State: " << ctx.from_state << " -> " << ctx.to_state;
 * });
 * 
 * state_machine->connect(address);
 * @endcode
 */
class ConnectionStateMachine : public ConnectionCallbacks,
                               public FilterManagerCallbacks,
                               public TransportSocketCallbacks {
public:
  using StateChangeCallback = std::function<void(const StateTransitionContext&)>;
  using CompletionCallback = std::function<void(bool success)>;
  using EventHandler = std::function<void(ConnectionStateMachineEvent)>;
  
  /**
   * Constructor
   *
   * @param dispatcher Event dispatcher for async operations
   * @param connection Connection instance to manage
   * @param config State machine configuration
   */
  ConnectionStateMachine(event::Dispatcher& dispatcher,
                        Connection& connection,
                        const ConnectionStateMachineConfig& config);
  
  ~ConnectionStateMachine() override;
  
  // ===== Core State Machine Interface =====
  
  /**
   * Get current state
   */
  ConnectionState currentState() const {
    return current_state_.load(std::memory_order_acquire);
  }
  
  /**
   * Handle an event
   * 
   * @param event The event to process
   * @return true if event was handled, false if ignored
   */
  bool handleEvent(ConnectionStateMachineEvent event);
  
  /**
   * Force transition to a state (bypasses validation)
   * Use only for error recovery scenarios
   */
  void forceTransition(ConnectionState new_state, const std::string& reason);
  
  // ===== Connection Operations =====
  
  /**
   * Initiate connection (client mode)
   */
  void connect(const Address::InstanceConstSharedPtr& address,
               CompletionCallback callback = nullptr);
  
  /**
   * Start listening (server mode)
   */
  void listen(const Address::InstanceConstSharedPtr& address,
             CompletionCallback callback = nullptr);
  
  /**
   * Accept a connection (server mode)
   */
  void accept(ConnectionSocketPtr&& socket,
             CompletionCallback callback = nullptr);
  
  /**
   * Close the connection
   */
  void close(ConnectionCloseType type = ConnectionCloseType::FlushWrite);
  
  /**
   * Reset the connection (immediate close)
   */
  void reset();
  
  // ===== Flow Control =====
  
  /**
   * Enable/disable reading
   */
  void setReadDisabled(bool disabled);
  
  /**
   * Enable/disable writing
   */
  void setWriteDisabled(bool disabled);
  
  /**
   * Check if connection can read
   */
  bool canRead() const;
  
  /**
   * Check if connection can write
   */
  bool canWrite() const;
  
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
  const std::deque<StateTransitionContext>& getStateHistory() const {
    return state_history_;
  }
  
  // ===== Metrics and Monitoring =====
  
  /**
   * Get time in current state
   */
  std::chrono::milliseconds getTimeInCurrentState() const;
  
  /**
   * Get connection statistics
   */
  ConnectionStats getStats() const;
  
  /**
   * Get total state transitions
   */
  uint64_t getTotalTransitions() const {
    return total_transitions_.load(std::memory_order_relaxed);
  }
  
  // ===== ConnectionCallbacks Interface =====
  
  void onEvent(ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;
  
  // ===== FilterManagerCallbacks Interface =====
  
  void onContinueReading(ReadBufferSource* source,
                         bool end_stream) override;
  void onFilterError(FilterErrorReason reason) override;
  
  // ===== TransportSocketCallbacks Interface =====
  
  IoHandle& ioHandle() override;
  const IoHandle& ioHandle() const override;
  Connection& connection() override { return connection_; }
  void raiseEvent(ConnectionEvent event) override;
  bool shouldDrainReadBuffer() override;
  void setTransportSocketIsReadable() override;
  void flushWriteBuffer() override;
  
protected:
  // ===== State Transition Logic =====
  
  /**
   * Execute state transition
   */
  bool transitionTo(ConnectionState new_state,
                   ConnectionStateMachineEvent event,
                   const std::string& reason);
  
  /**
   * Check if transition is valid
   */
  bool isValidTransition(ConnectionState from,
                        ConnectionState to,
                        ConnectionStateMachineEvent event) const;
  
  /**
   * Get valid next states for current state
   */
  std::unordered_set<ConnectionState> getValidNextStates() const;
  
  /**
   * Called when entering a state
   */
  virtual void onStateEnter(ConnectionState state);
  
  /**
   * Called when exiting a state
   */
  virtual void onStateExit(ConnectionState state);
  
  // ===== Event Handlers =====
  
  void handleSocketEvent(ConnectionStateMachineEvent event);
  void handleIoEvent(ConnectionStateMachineEvent event);
  void handleTransportEvent(ConnectionStateMachineEvent event);
  void handleFilterEvent(ConnectionStateMachineEvent event);
  void handleApplicationEvent(ConnectionStateMachineEvent event);
  void handleTimerEvent(ConnectionStateMachineEvent event);
  void handleRecoveryEvent(ConnectionStateMachineEvent event);
  
  // ===== Timer Management =====
  
  void startConnectTimer();
  void startHandshakeTimer();
  void startIdleTimer();
  void startDrainTimer();
  void startReconnectTimer();
  
  void cancelAllTimers();
  
  // ===== Error Recovery =====
  
  void initiateReconnection();
  void handleReconnectTimeout();
  void handleRecoverySuccess();
  void handleRecoveryFailure(const std::string& reason);
  
  // ===== Helper Methods =====
  
  /**
   * Assert we're in dispatcher thread
   */
  void assertInDispatcherThread() const {
    // In production: ASSERT(dispatcher_.isThreadSafe())
  }
  
  /**
   * Notify state change listeners
   */
  void notifyStateChange(const StateTransitionContext& context);
  
  /**
   * Record state transition in history
   */
  void recordStateTransition(const StateTransitionContext& context);
  
  /**
   * Update connection metrics
   */
  void updateMetrics();
  
  /**
   * Get human-readable state name
   */
  static std::string getStateName(ConnectionState state);
  
  /**
   * Get human-readable event name
   */
  static std::string getEventName(ConnectionStateMachineEvent event);
  
private:
  // ===== Core Members =====
  
  event::Dispatcher& dispatcher_;
  Connection& connection_;
  ConnectionStateMachineConfig config_;
  
  // Current state (atomic for safe cross-thread reads)
  std::atomic<ConnectionState> current_state_{ConnectionState::Uninitialized};
  
  // State timing
  std::chrono::steady_clock::time_point state_entry_time_;
  
  // State history
  std::deque<StateTransitionContext> state_history_;
  static constexpr size_t kMaxHistorySize = 100;
  
  // Transition tracking
  std::atomic<uint64_t> total_transitions_{0};
  bool transition_in_progress_{false};
  
  // ===== Event Handling =====
  
  // Event queue for deferred processing
  std::queue<ConnectionStateMachineEvent> event_queue_;
  bool processing_events_{false};
  
  // Event handlers by state
  std::unordered_map<ConnectionState, 
                     std::unordered_map<ConnectionStateMachineEvent, EventHandler>> 
      state_event_handlers_;
  
  // ===== Timers =====
  
  event::TimerPtr connect_timer_;
  event::TimerPtr handshake_timer_;
  event::TimerPtr idle_timer_;
  event::TimerPtr drain_timer_;
  event::TimerPtr reconnect_timer_;
  
  // ===== Reconnection State =====
  
  uint32_t reconnect_attempts_{0};
  std::chrono::milliseconds current_reconnect_delay_;
  Address::InstanceConstSharedPtr reconnect_address_;
  
  // ===== Flow Control =====
  
  uint32_t read_disable_count_{0};
  uint32_t write_disable_count_{0};
  bool read_end_stream_{false};
  bool write_end_stream_{false};
  
  // ===== Metrics =====
  
  ConnectionStats stats_;
  uint64_t state_bytes_read_{0};
  uint64_t state_bytes_written_{0};
  uint32_t consecutive_errors_{0};
  
  // ===== Callbacks =====
  
  std::vector<StateChangeCallback> state_change_listeners_;
  std::vector<CompletionCallback> pending_callbacks_;
  
  // ===== Filter Chain Integration =====
  
  FilterManagerPtr filter_manager_;
  bool filter_chain_initialized_{false};
  
  // ===== Transport Socket Integration =====
  
  TransportSocketPtr transport_socket_;
  bool transport_connected_{false};
};

/**
 * Connection state machine factory
 */
class ConnectionStateMachineFactory {
public:
  virtual ~ConnectionStateMachineFactory() = default;
  
  /**
   * Create a connection state machine
   */
  virtual std::unique_ptr<ConnectionStateMachine> createStateMachine(
      event::Dispatcher& dispatcher,
      Connection& connection,
      const ConnectionStateMachineConfig& config) = 0;
  
  /**
   * Get default configuration
   */
  virtual ConnectionStateMachineConfig getDefaultConfig() const {
    return ConnectionStateMachineConfig{};
  }
};

/**
 * Helper class for common connection patterns
 */
class ConnectionStatePatterns {
public:
  /**
   * Check if state allows reading
   */
  static bool canRead(ConnectionState state);
  
  /**
   * Check if state allows writing
   */
  static bool canWrite(ConnectionState state);
  
  /**
   * Check if state is terminal
   */
  static bool isTerminal(ConnectionState state);
  
  /**
   * Check if state is in connecting phase
   */
  static bool isConnecting(ConnectionState state);
  
  /**
   * Check if state is connected
   */
  static bool isConnected(ConnectionState state);
  
  /**
   * Check if state is in closing phase
   */
  static bool isClosing(ConnectionState state);
  
  /**
   * Check if state allows reconnection
   */
  static bool canReconnect(ConnectionState state);
  
  /**
   * Get appropriate close state based on current state
   */
  static ConnectionState getCloseState(ConnectionState current,
                                       ConnectionCloseType type);
};

/**
 * Connection state machine builder for fluent configuration
 */
class ConnectionStateMachineBuilder {
public:
  ConnectionStateMachineBuilder& withMode(ConnectionMode mode) {
    config_.mode = mode;
    return *this;
  }
  
  ConnectionStateMachineBuilder& withConnectTimeout(std::chrono::milliseconds timeout) {
    config_.connect_timeout = timeout;
    return *this;
  }
  
  ConnectionStateMachineBuilder& withAutoReconnect(bool enable,
                                                   uint32_t max_attempts = 3) {
    config_.enable_auto_reconnect = enable;
    config_.max_reconnect_attempts = max_attempts;
    return *this;
  }
  
  ConnectionStateMachineBuilder& withBufferLimits(uint32_t read_limit,
                                                  uint32_t write_limit) {
    config_.read_buffer_limit = read_limit;
    config_.write_buffer_limit = write_limit;
    return *this;
  }
  
  ConnectionStateMachineBuilder& withWatermarks(uint32_t high, uint32_t low) {
    config_.high_watermark = high;
    config_.low_watermark = low;
    return *this;
  }
  
  ConnectionStateMachineBuilder& withStateChangeCallback(
      std::function<void(const StateTransitionContext&)> callback) {
    config_.state_change_callback = callback;
    return *this;
  }
  
  std::unique_ptr<ConnectionStateMachine> build(event::Dispatcher& dispatcher,
                                                Connection& connection) {
    return std::make_unique<ConnectionStateMachine>(dispatcher, connection, config_);
  }
  
private:
  ConnectionStateMachineConfig config_;
};

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_CONNECTION_STATE_MACHINE_H