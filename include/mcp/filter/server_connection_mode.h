/**
 * @file server_connection_mode.h
 * @brief Server-side connection mode state machine
 *
 * Manages the per-connection mode determination for MCP server filters.
 * Each accepted connection is classified into exactly one mode during
 * HTTP header parsing, and the mode is immutable for the connection's
 * lifetime. This replaces the ad-hoc boolean flags (sse_server_mode_,
 * sse_writing_handshake_, sse_headers_written_) with a validated state
 * machine that provides transition history and observable callbacks.
 *
 * Design principles:
 * - Thread-confined to dispatcher thread (lock-free, no mutex)
 * - Mode determined once (Undetermined -> PlainHttp|SseStream|CallbackProxy)
 * - RAII HandshakeWriteGuard for the reentrancy guard pattern
 * - Observable via callbacks for debugging and logging
 * - Per-connection instance owned by the filter via unique_ptr
 *
 * Connection modes:
 *   PlainHttp     — POST /mcp, normal request/response
 *   SseStream     — GET /sse, long-lived event stream
 *   CallbackProxy — POST /callback/{id}, proxy response to SSE stream
 */

#ifndef MCP_FILTER_SERVER_CONNECTION_MODE_H
#define MCP_FILTER_SERVER_CONNECTION_MODE_H

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
 * Server connection modes
 *
 * Determined once during onHeaders based on the HTTP method and path.
 * The mode is immutable after determination — a connection that starts
 * as SseStream cannot become PlainHttp or vice versa.
 */
enum class ServerConnMode {
  // Headers not yet received — mode not yet known
  Undetermined,

  // POST /mcp (or other non-SSE path) — normal HTTP request/response.
  // Each request gets a full HTTP response and the connection may be
  // reused via keep-alive.
  PlainHttp,

  // GET /sse — long-lived SSE event stream. The server writes HTTP
  // response headers once, then streams SSE events indefinitely. The
  // connection stays open until the client disconnects.
  SseStream,

  // POST /callback/{session_id} — the client is posting a JSON-RPC
  // request body that should be processed normally, but the response
  // must be routed through the SSE stream identified by session_id
  // rather than written back on this POST connection.
  CallbackProxy,

  // Terminal: an unrecoverable error occurred.
  Error,

  // Terminal: the connection has been closed cleanly.
  Closed
};

/**
 * Server connection events
 */
enum class ServerConnEvent {
  // Mode determination (exactly one fires per connection)
  PlainHttpDetected,
  SseGetDetected,
  CallbackPostDetected,

  // SSE headers written (SseStream only, monotonic)
  SseHeadersWritten,

  // Terminal
  StreamError,
  Close
};

/**
 * State transition result
 */
struct ServerConnTransitionResult {
  bool success{false};
  std::string error_message;
  ServerConnMode resulting_mode;

  static ServerConnTransitionResult Success(ServerConnMode mode) {
    return {true, "", mode};
  }
  static ServerConnTransitionResult Failure(const std::string& error) {
    return {false, error, ServerConnMode::Error};
  }
};

/**
 * State transition context
 */
struct ServerConnTransitionContext {
  ServerConnMode from_mode;
  ServerConnMode to_mode;
  ServerConnEvent triggering_event;
  std::chrono::steady_clock::time_point timestamp;
  std::string reason;
  std::chrono::milliseconds time_in_previous_mode;
};

/**
 * Configuration
 */
struct ServerConnModeConfig {
  std::function<void(const ServerConnTransitionContext&)> state_change_callback;
  std::function<void(const std::string&)> error_callback;
};

/**
 * RAII guard for the handshake write reentrancy flag.
 *
 * While a HandshakeWriteGuard is alive, isWritingHandshake() returns
 * true. This prevents the filter's own onWrite from re-framing the
 * raw HTTP bytes that are being written inline during the SSE
 * handshake (GET /sse response prelude or POST /callback 202).
 *
 * The guard is always stack-scoped and cannot outlive the call frame
 * where it was created, so it is safe even if connection().write()
 * throws or triggers a callback that destroys the filter.
 */
class HandshakeWriteGuard {
 public:
  // Forward declaration — implementation defined after ServerConnectionMode
  explicit HandshakeWriteGuard(class ServerConnectionMode& mode);
  ~HandshakeWriteGuard();

  // Non-copyable, non-movable
  HandshakeWriteGuard(const HandshakeWriteGuard&) = delete;
  HandshakeWriteGuard& operator=(const HandshakeWriteGuard&) = delete;

 private:
  class ServerConnectionMode& mode_;
};

/**
 * Server Connection Mode State Machine
 *
 * Manages the per-connection mode lifecycle. Thread-confined to the
 * dispatcher thread. Each connection's filter instance owns its own
 * mode via unique_ptr.
 */
class ServerConnectionMode {
 public:
  using StateChangeCallback =
      std::function<void(const ServerConnTransitionContext&)>;
  using CompletionCallback = std::function<void(bool success)>;

  ServerConnectionMode(event::Dispatcher& dispatcher,
                       const ServerConnModeConfig& config);
  ~ServerConnectionMode();

  // ===== Core Interface =====

  ServerConnMode currentMode() const { return current_mode_; }

  /**
   * Handle an event, triggering the appropriate mode transition.
   */
  ServerConnTransitionResult handleEvent(ServerConnEvent event,
                                         CompletionCallback callback = nullptr);

  // ===== Mode Queries =====

  /** True only when mode is SseStream */
  bool isSseStream() const { return current_mode_ == ServerConnMode::SseStream; }

  /** True only when mode is CallbackProxy */
  bool isCallbackProxy() const {
    return current_mode_ == ServerConnMode::CallbackProxy;
  }

  /** True only when mode is PlainHttp */
  bool isPlainHttp() const {
    return current_mode_ == ServerConnMode::PlainHttp;
  }

  /** True while a HandshakeWriteGuard is alive */
  bool isWritingHandshake() const { return handshake_write_depth_ > 0; }

  /** True after SseHeadersWritten event has been processed (monotonic) */
  bool sseHeadersWritten() const { return sse_headers_written_; }

  bool hasError() const { return current_mode_ == ServerConnMode::Error; }

  // ===== State Observers =====

  void addStateChangeListener(StateChangeCallback callback);
  void clearStateChangeListeners();

  const std::deque<ServerConnTransitionContext>& getStateHistory() const {
    return state_history_;
  }

  // ===== Metrics =====

  std::chrono::milliseconds getTimeInCurrentMode() const;
  uint64_t getTotalTransitions() const { return total_transitions_; }

  // ===== Utility =====

  static std::string getModeName(ServerConnMode mode);
  static std::string getEventName(ServerConnEvent event);

  // ===== Handshake Write Guard Support =====
  // Called by HandshakeWriteGuard only — not for direct use.
  void beginHandshakeWrite();
  void endHandshakeWrite();

 protected:
  void assertInDispatcherThread() const {
    // All methods must be called from dispatcher thread.
  }

 private:
  void initializeTransitions();

  bool isTransitionValid(ServerConnMode from, ServerConnMode to) const;

  void executeTransition(ServerConnMode new_mode,
                         ServerConnEvent event,
                         const std::string& reason,
                         CompletionCallback callback);

  void notifyStateChange(const ServerConnTransitionContext& context);
  void recordTransition(const ServerConnTransitionContext& context);

  // Core
  event::Dispatcher& dispatcher_;
  ServerConnModeConfig config_;

  // Current mode — plain enum, dispatcher-thread confined
  ServerConnMode current_mode_{ServerConnMode::Undetermined};
  std::chrono::steady_clock::time_point mode_entry_time_;

  // State history
  std::deque<ServerConnTransitionContext> state_history_;
  static constexpr size_t kMaxHistorySize = 50;

  // Transition matrix
  std::unordered_map<ServerConnMode, std::unordered_set<ServerConnMode>>
      valid_transitions_;

  // Listeners
  std::vector<StateChangeCallback> state_change_listeners_;

  // Metrics
  uint64_t total_transitions_{0};

  // Sub-state: handshake write reentrancy guard (ref-counted for safety)
  int handshake_write_depth_{0};

  // Sub-state: SSE response headers written (monotonic: false -> true)
  bool sse_headers_written_{false};

  // Reentrancy guard
  bool transition_in_progress_{false};
};

// ===== Inline HandshakeWriteGuard =====

inline HandshakeWriteGuard::HandshakeWriteGuard(ServerConnectionMode& mode)
    : mode_(mode) {
  mode_.beginHandshakeWrite();
}

inline HandshakeWriteGuard::~HandshakeWriteGuard() {
  mode_.endHandshakeWrite();
}

}  // namespace filter
}  // namespace mcp

#endif  // MCP_FILTER_SERVER_CONNECTION_MODE_H
