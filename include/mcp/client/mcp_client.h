/**
 * @file mcp_client.h
 * @brief Enterprise-grade MCP client with production features
 *
 * This provides a production-ready MCP client with:
 * - Transport negotiation (stdio, HTTP+SSE, WebSocket)
 * - Connection pooling for efficient resource usage
 * - Circuit breaker pattern for failure handling
 * - Comprehensive metrics and monitoring
 * - Retry logic with exponential backoff
 * - Request timeout management
 * - Batch processing support
 * - Future-based async API
 * - Flow control with watermark-based backpressure
 * - Filter chain architecture for extensibility
 */

#ifndef MCP_CLIENT_H
#define MCP_CLIENT_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "mcp/buffer.h"
#include "mcp/builders.h"
#include "mcp/event/event_loop.h"
#include "mcp/mcp_application_base.h"  // TODO: Migrate to mcp_application_base_refactored.h
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/filter.h"
#include "mcp/protocol/mcp_protocol_state_machine.h"
#include "mcp/types.h"

namespace mcp {
namespace client {

// Import JSON-RPC types
using ::mcp::jsonrpc::Notification;
using ::mcp::jsonrpc::Request;
using ::mcp::jsonrpc::Response;

// Forward declarations
class RequestTracker;
class CircuitBreaker;
class RetryManager;
class MetricsCollector;

/**
 * Client configuration
 */
struct McpClientConfig : public application::ApplicationBase::Config {
  // Protocol configuration
  std::string protocol_version = "2024-11-05";
  std::string client_name = "mcp-cpp-client";
  std::string client_version = "1.0.0";

  // Transport configuration
  TransportType preferred_transport = TransportType::Stdio;
  bool auto_negotiate_transport = true;

  // Connection pool settings
  size_t connection_pool_size = 10;
  size_t max_idle_connections = 5;
  std::chrono::milliseconds connection_idle_timeout{60000};

  // Circuit breaker settings
  size_t circuit_breaker_threshold = 5;
  std::chrono::milliseconds circuit_breaker_timeout{30000};
  double circuit_breaker_error_rate = 0.5;

  // Retry configuration
  size_t max_retries = 3;
  std::chrono::milliseconds initial_retry_delay{1000};
  double retry_backoff_multiplier = 2.0;
  std::chrono::milliseconds max_retry_delay{30000};

  // Request management
  std::chrono::milliseconds request_timeout{30000};
  size_t max_concurrent_requests = 100;
  size_t batch_size = 10;

  // Flow control
  bool enable_flow_control = true;
  size_t request_queue_limit = 1000;

  // Capabilities
  ClientCapabilities capabilities;

  // Protocol state machine configuration
  std::chrono::milliseconds protocol_initialization_timeout{30000};
  std::chrono::milliseconds protocol_connection_timeout{10000};
  std::chrono::milliseconds protocol_drain_timeout{10000};
  bool protocol_auto_reconnect = true;
  size_t protocol_max_reconnect_attempts = 3;
  std::chrono::milliseconds protocol_reconnect_delay{1000};
};

/**
 * Client statistics with detailed metrics
 */
struct McpClientStats : public application::ApplicationStats {
  // Request metrics
  std::atomic<uint64_t> requests_retried{0};
  std::atomic<uint64_t> requests_batched{0};
  std::atomic<uint64_t> requests_queued{0};
  std::atomic<uint64_t> requests_timeout{0};

  // Circuit breaker metrics
  std::atomic<uint64_t> circuit_breaker_opens{0};
  std::atomic<uint64_t> circuit_breaker_closes{0};
  std::atomic<uint64_t> circuit_breaker_half_opens{0};

  // Connection pool metrics
  std::atomic<uint64_t> connection_pool_hits{0};
  std::atomic<uint64_t> connection_pool_misses{0};
  std::atomic<uint64_t> connection_pool_evictions{0};

  // Protocol metrics
  std::atomic<uint64_t> protocol_errors{0};
  std::atomic<uint64_t> transport_errors{0};

  // Resource metrics
  std::atomic<uint64_t> resources_read{0};
  std::atomic<uint64_t> tools_called{0};
  std::atomic<uint64_t> prompts_retrieved{0};
};

/**
 * Request context for tracking
 * Maintains all state for a single request including retry count and timing
 * Following production patterns for proper lifecycle management
 */
struct RequestContext {
  RequestId id;
  std::string method;
  optional<Metadata> params;
  std::chrono::steady_clock::time_point start_time;
  std::promise<Response> promise;
  size_t retry_count{0};
  bool is_batch{false};
  optional<ProgressToken> progress_token;

  // Timer-based timeout management
  event::TimerPtr timeout_timer;
  event::TimerPtr retry_timer;  // Timer for reconnect retries
  bool timeout_enabled{false};
  bool completed{false};  // Ensures single completion

  RequestContext(const RequestId& id, const std::string& method)
      : id(id), method(method), start_time(std::chrono::steady_clock::now()) {}

  ~RequestContext() {
    // Ensure timers are cleaned up
    if (timeout_timer && timeout_enabled) {
      timeout_timer->disableTimer();
    }
    if (retry_timer) {
      retry_timer->disableTimer();
    }
  }
};

/**
 * Circuit breaker implementation
 * Prevents cascading failures by temporarily blocking requests after failures
 *
 * State transitions:
 * CLOSED -> OPEN: When failure threshold is reached
 * OPEN -> HALF_OPEN: After timeout period
 * HALF_OPEN -> CLOSED: After successful test requests
 * HALF_OPEN -> OPEN: On any failure during test
 */
class CircuitBreaker {
 public:
  enum class State { CLOSED, OPEN, HALF_OPEN };

  CircuitBreaker(size_t threshold,
                 std::chrono::milliseconds timeout,
                 double error_rate)
      : failure_threshold_(threshold),
        timeout_duration_(timeout),
        error_rate_threshold_(error_rate) {}

  // Check if request is allowed based on circuit state
  bool allowRequest() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();

    switch (state_) {
      case State::CLOSED:
        // Circuit is closed, all requests allowed
        return true;

      case State::OPEN:
        // Circuit is open, check if timeout has elapsed
        if (now - last_failure_time_ >= timeout_duration_) {
          // Transition to half-open to test recovery
          state_ = State::HALF_OPEN;
          half_open_requests_ = 0;
          return true;
        }
        // Still in timeout period, reject request
        return false;

      case State::HALF_OPEN:
        // Allow limited test requests
        return half_open_requests_ < 3;
    }

    return false;
  }

  // Record successful request
  void recordSuccess() {
    std::lock_guard<std::mutex> lock(mutex_);

    consecutive_failures_ = 0;

    if (state_ == State::HALF_OPEN) {
      half_open_requests_++;
      if (half_open_requests_ >= 3) {
        // Enough successful requests, close the circuit
        state_ = State::CLOSED;
        failure_count_ = 0;
        request_count_ = 0;
      }
    }

    request_count_++;
  }

  // Record failed request and update circuit state
  void recordFailure() {
    std::lock_guard<std::mutex> lock(mutex_);

    failure_count_++;
    consecutive_failures_++;
    last_failure_time_ = std::chrono::steady_clock::now();

    if (state_ == State::HALF_OPEN) {
      // Any failure in half-open state immediately opens circuit
      state_ = State::OPEN;
      return;
    }

    // Check if we should open the circuit based on failure threshold or error
    // rate
    if (consecutive_failures_ >= failure_threshold_ ||
        (request_count_ > 10 &&
         static_cast<double>(failure_count_) / request_count_ >
             error_rate_threshold_)) {
      state_ = State::OPEN;
    }
  }

  State getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
  }

 private:
  mutable std::mutex mutex_;
  State state_{State::CLOSED};
  size_t failure_threshold_;
  std::chrono::milliseconds timeout_duration_;
  double error_rate_threshold_;

  size_t failure_count_{0};
  size_t request_count_{0};
  size_t consecutive_failures_{0};
  size_t half_open_requests_{0};
  std::chrono::steady_clock::time_point last_failure_time_;
};

/**
 * Request tracker with timeout management
 * Tracks all pending requests and identifies timeouts
 */
class RequestTracker {
 public:
  using RequestPtr = std::shared_ptr<RequestContext>;

  RequestTracker(std::chrono::milliseconds timeout) : timeout_(timeout) {}

  // Add request to tracking
  void trackRequest(RequestPtr request) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Extract int64_t ID from RequestId
    int64_t id =
        holds_alternative<int64_t>(request->id) ? get<int64_t>(request->id) : 0;
    pending_requests_[id] = request;
  }

  // Get request by ID without removing
  RequestPtr getRequest(const RequestId& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t int_id = holds_alternative<int64_t>(id) ? get<int64_t>(id) : 0;
    auto it = pending_requests_.find(int_id);
    if (it != pending_requests_.end()) {
      return it->second;
    }
    return nullptr;
  }

  // Remove and return request
  RequestPtr removeRequest(const RequestId& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t int_id = holds_alternative<int64_t>(id) ? get<int64_t>(id) : 0;
    auto it = pending_requests_.find(int_id);
    if (it != pending_requests_.end()) {
      auto request = it->second;
      pending_requests_.erase(it);
      return request;
    }
    return nullptr;
  }

  // Find and remove all timed out requests
  std::vector<RequestPtr> getTimedOutRequests() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RequestPtr> timed_out;

    auto now = std::chrono::steady_clock::now();
    for (const auto& pair : pending_requests_) {
      if (now - pair.second->start_time >= timeout_) {
        timed_out.push_back(pair.second);
      }
    }

    // Remove timed out requests from tracking
    for (const auto& request : timed_out) {
      int64_t id = holds_alternative<int64_t>(request->id)
                       ? get<int64_t>(request->id)
                       : 0;
      pending_requests_.erase(id);
    }

    return timed_out;
  }

  size_t getPendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_requests_.size();
  }

 private:
  mutable std::mutex mutex_;
  std::chrono::milliseconds timeout_;
  // Store using int ID extracted from RequestId
  std::unordered_map<int64_t, RequestPtr> pending_requests_;
};

/**
 * Retry manager with exponential backoff
 * Implements retry logic with jitter to avoid thundering herd
 */
class RetryManager {
 public:
  RetryManager(size_t max_retries,
               std::chrono::milliseconds initial_delay,
               double backoff_multiplier,
               std::chrono::milliseconds max_delay)
      : max_retries_(max_retries),
        initial_delay_(initial_delay),
        backoff_multiplier_(backoff_multiplier),
        max_delay_(max_delay) {}

  bool shouldRetry(size_t retry_count) const {
    return retry_count < max_retries_;
  }

  // Calculate delay with exponential backoff and jitter
  std::chrono::milliseconds getRetryDelay(size_t retry_count) const {
    // Calculate base delay with exponential backoff
    auto delay = initial_delay_;
    for (size_t i = 0; i < retry_count; ++i) {
      delay = std::chrono::milliseconds(
          static_cast<int64_t>(delay.count() * backoff_multiplier_));
      if (delay > max_delay_) {
        return max_delay_;
      }
    }

    // Add jitter (±20%) to avoid thundering herd
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.8, 1.2);

    return std::chrono::milliseconds(
        static_cast<int64_t>(delay.count() * dis(gen)));
  }

 private:
  size_t max_retries_;
  std::chrono::milliseconds initial_delay_;
  double backoff_multiplier_;
  std::chrono::milliseconds max_delay_;
};

/**
 * Enterprise-grade MCP Client
 *
 * Architecture:
 * - Inherits from ApplicationBase for worker thread model
 * - Implements McpProtocolCallbacks for protocol handling
 * - Uses filter chain for extensible message processing
 * - Manages connection pool for efficient resource usage
 * - Implements circuit breaker for failure isolation
 * - Provides future-based async API for all operations
 */
class McpClient : public application::ApplicationBase {
 public:
  McpClient(const McpClientConfig& config);
  ~McpClient() override;

  // Connection management
  VoidResult connect(const std::string& uri);
  VoidResult reconnect();  // Reconnect using stored URI
  void disconnect();
  bool isConnected() const { return connected_; }
  bool isConnectionOpen() const;  // Check actual connection state

  // Shutdown the client (stops workers and event loop)
  void shutdown() override;

  // Check if shutting down
  bool isShuttingDown() const { return shutting_down_; }

  // Initialize protocol - must be called after connect
  std::future<InitializeResult> initializeProtocol();

  // Request methods with future-based async API
  std::future<Response> sendRequest(const std::string& method,
                                    const optional<Metadata>& params = nullopt);

  // Batch processing - sends multiple requests efficiently
  std::vector<std::future<Response>> sendBatch(
      const std::vector<std::pair<std::string, optional<Metadata>>>& requests);

  // Notification (fire-and-forget)
  VoidResult sendNotification(const std::string& method,
                              const optional<Metadata>& params = nullopt);

  // Resource operations
  std::future<ListResourcesResult> listResources(
      const optional<Cursor>& cursor = nullopt);
  std::future<ReadResourceResult> readResource(const std::string& uri);
  std::future<VoidResult> subscribeResource(const std::string& uri);
  std::future<VoidResult> unsubscribeResource(const std::string& uri);

  // Tool operations
  std::future<ListToolsResult> listTools(
      const optional<Cursor>& cursor = nullopt);
  std::future<CallToolResult> callTool(
      const std::string& name, const optional<Metadata>& arguments = nullopt);

  // Prompt operations
  std::future<ListPromptsResult> listPrompts(
      const optional<Cursor>& cursor = nullopt);
  std::future<GetPromptResult> getPrompt(
      const std::string& name, const optional<Metadata>& arguments = nullopt);

  // Logging operations
  std::future<VoidResult> setLogLevel(enums::LoggingLevel::Value level);

  // Sampling/completion operations
  std::future<CreateMessageResult> createMessage(
      const std::vector<SamplingMessage>& messages,
      const optional<ModelPreferences>& preferences = nullopt);

  // Progress tracking - register callback for progress updates
  void trackProgress(const ProgressToken& token,
                     std::function<void(double)> callback);

  // Get client statistics
  const McpClientStats& getClientStats() const { return client_stats_; }

  // Set server capabilities (after initialization)
  void setServerCapabilities(const ServerCapabilities& caps) {
    server_capabilities_ = caps;
  }

 protected:
  // ApplicationBase overrides
  void initializeWorker(application::WorkerContext& worker) override;
  void setupFilterChain(application::FilterChainBuilder& builder) override;

  // Protocol callbacks handler (internal)
  class ProtocolCallbacksImpl : public mcp::McpProtocolCallbacks {
   public:
    ProtocolCallbacksImpl(McpClient& client) : client_(client) {}

    void onRequest(const Request& request) override {
      client_.handleRequest(request);
    }
    void onNotification(const Notification& notification) override {
      client_.handleNotification(notification);
    }
    void onResponse(const Response& response) override {
      client_.handleResponse(response);
    }
    void onConnectionEvent(network::ConnectionEvent event) override {
      client_.handleConnectionEvent(event);
    }
    void onError(const Error& error) override { client_.handleError(error); }

   private:
    McpClient& client_;
  };

  // Internal message handlers
  void handleRequest(const Request& request);
  void handleNotification(const Notification& notification);
  void handleResponse(const Response& response);
  void handleConnectionEvent(network::ConnectionEvent event);
  void handleError(const Error& error);

 private:
  // Internal request handling
  RequestId generateRequestId();
  std::shared_ptr<RequestContext> createRequestContext(
      const std::string& method, const optional<Metadata>& params);
  void sendRequestInternal(std::shared_ptr<RequestContext> context);
  void handleTimeout(std::shared_ptr<RequestContext> context);
  void retryRequest(std::shared_ptr<RequestContext> context);

  // Internal reconnection logic (must be called on dispatcher thread)
  VoidResult reconnectInternal();

  // Timer-based timeout management following production patterns
  void enableRequestTimeout(std::shared_ptr<RequestContext> context);
  void disableRequestTimeout(std::shared_ptr<RequestContext> context);
  void handleRequestTimeout(std::shared_ptr<RequestContext> context);

  // Connection pool implementation
  class ConnectionPoolImpl : public application::ConnectionPool {
   public:
    ConnectionPoolImpl(McpClient& client,
                       event::Dispatcher& dispatcher,
                       size_t max_connections,
                       uint32_t streams_per_connection)
        : ConnectionPool(dispatcher, max_connections, streams_per_connection),
          client_(client) {}

   protected:
    ConnectionPtr createNewConnection() override;

   private:
    McpClient& client_;
  };

  // Transport negotiation - determines best transport based on URI and
  // capabilities
  TransportType negotiateTransport(const std::string& uri);
  McpConnectionConfig createConnectionConfig(TransportType transport);

  // Progress handling
  void handleProgressNotification(const ProgressNotification& notification);

  // Metrics reporting
  void updateLatencyMetrics(uint64_t duration_ms);
  void reportDetailedMetrics();

 private:
  McpClientConfig config_;
  McpClientStats client_stats_;
  std::atomic<bool> shutting_down_{false};
  std::thread dispatcher_thread_;  // Dispatcher thread handle

  // Connection management
  std::unique_ptr<mcp::McpConnectionManager> connection_manager_;
  std::unique_ptr<ProtocolCallbacksImpl> protocol_callbacks_;
  std::unique_ptr<ConnectionPoolImpl> connection_pool_;
  std::atomic<bool> connected_{false};
  std::string current_uri_;

  // Connection activity tracking for detecting stale connections
  std::chrono::steady_clock::time_point last_activity_time_;
  static constexpr int kConnectionIdleTimeoutSec = 30;  // Increased for SSE - server responses may take time

  // Request management
  std::unique_ptr<RequestTracker> request_tracker_;
  std::unique_ptr<CircuitBreaker> circuit_breaker_;
  std::unique_ptr<RetryManager> retry_manager_;
  std::atomic<uint64_t> next_request_id_{1};

  // Request queue for flow control
  std::queue<std::shared_ptr<RequestContext>> request_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;

  // Progress tracking - use string representation as map key
  std::map<std::string, std::function<void(double)>> progress_callbacks_;
  std::mutex progress_mutex_;

  // Protocol state
  bool initialized_{false};
  ServerCapabilities server_capabilities_;

  // Protocol state machine for managing MCP protocol lifecycle
  std::unique_ptr<protocol::McpProtocolStateMachine> protocol_state_machine_;

  // Periodic task management using dispatcher timers
  void schedulePeriodicTasks();
  void processQueuedRequests();

  // Timer handles for periodic tasks
  event::TimerPtr timeout_timer_;
  event::TimerPtr retry_timer_;

  // Protocol state coordination
  void coordinateProtocolState();
  void handleProtocolStateChange(
      const protocol::ProtocolStateTransitionContext& context);
};

/**
 * Factory function for creating MCP client
 */
inline std::unique_ptr<McpClient> createMcpClient(
    const McpClientConfig& config = {}) {
  return std::make_unique<McpClient>(config);
}

}  // namespace client
}  // namespace mcp

#endif  // MCP_CLIENT_H