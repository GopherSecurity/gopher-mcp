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
#include "mcp/client/transport_probe.h"
#include "mcp/event/event_loop.h"
#include "mcp/mcp_application_base.h"  // TODO: Migrate to mcp_application_base_refactored.h
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/filter.h"
#include "mcp/protocol/mcp_protocol_state_machine.h"
#include "mcp/protocol/mrtr.h"
#include "mcp/protocol/subscriptions.h"
#include "mcp/transport/streamable_http_config.h"
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
  std::string protocol_version = protocol::kDefaultProtocolVersion;
  std::string client_name = "mcp-cpp-client";
  std::string client_version = "1.0.0";

  // Transport configuration
  TransportType preferred_transport = TransportType::Stdio;
  bool auto_negotiate_transport = true;
  std::map<std::string, std::string> http_headers;

  // Streamable HTTP endpoint path and the protocol revisions this client
  // is willing to speak over it.
  transport::StreamableHttpClientConfig streamable_http;

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
  std::map<std::string, std::string> http_headers;
  std::chrono::steady_clock::time_point start_time;
  std::promise<Response> promise;
  size_t retry_count{0};
  bool is_batch{false};
  optional<ProgressToken> progress_token;

  // Streamable HTTP: this request has already been sent again under a
  // new session once. A second 404 is answered rather than recovered
  // from, so a server that has forgotten how to remember cannot make
  // one request bounce between it and the client forever.
  bool session_retried{false};

  // How many rounds of the server asking for something have already
  // happened for this request. Bounded, so a server that answers every
  // round with another question cannot keep one request going forever.
  size_t input_rounds{0};

  // How many times the answer to this request has been asked for again
  // after arriving as a stream that was cut off. Bounded, so a server
  // that cannot finish an answer cannot keep one request alive forever.
  size_t resume_attempts{0};

  // Called on the dispatcher thread when the response arrives, for work
  // that has to continue there. The future is how a caller waits; this
  // is how the client itself carries on, without the blocking get()
  // that would deadlock the thread the response arrives on.
  std::function<void(const jsonrpc::Response&)> on_response;

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
  static std::chrono::milliseconds reconnectWaitBudgetForRequestTimeout(
      std::chrono::milliseconds request_timeout);

  // Shutdown the client (stops workers and event loop)
  void shutdown() override;

  // Check if shutting down
  bool isShuttingDown() const { return shutting_down_; }

  // Initialize protocol - must be called after connect
  std::future<InitializeResult> initializeProtocol();

  // Request methods with future-based async API
  std::future<Response> sendRequest(const std::string& method,
                                    const optional<Metadata>& params = nullopt);
  std::future<Response> sendRequest(
      const std::string& method,
      const optional<Metadata>& params,
      const std::map<std::string, std::string>& http_headers);

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
  std::future<ListToolsResult> listTools(
      const optional<Cursor>& cursor,
      const std::map<std::string, std::string>& http_headers);
  std::future<CallToolResult> callTool(
      const std::string& name, const optional<Metadata>& arguments = nullopt);
  std::future<CallToolResult> callTool(
      const std::string& name,
      const optional<Metadata>& arguments,
      const std::map<std::string, std::string>& http_headers);

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

  // Notification handling - register a callback for a given notification
  // method (e.g. "notifications/resources/updated"). This lets applications
  // observe server-initiated notifications such as resource updates.
  //
  // The handler is invoked in the dispatcher thread when a matching
  // notification arrives. Registering a handler for a method that already has
  // one replaces the previous handler. Mirrors the server-side
  // registerNotificationHandler design.
  void registerNotificationHandler(
      const std::string& method,
      std::function<void(const jsonrpc::Notification&)> handler);

  /**
   * Answer a request the server makes of this client.
   *
   * A server may ask its client something mid-request — to sample, to
   * choose — and wait for the answer before it can finish. Without a
   * handler every such question is refused, which is an answer but not
   * a useful one. The handler returns the result; throwing turns into
   * an error response rather than into an unanswered question, because
   * a server waiting on this has nothing else to wait for.
   *
   * Safe to call from any thread; the handler runs on the dispatcher.
   */
  void registerRequestHandler(
      const std::string& method,
      std::function<jsonrpc::ResponseResult(const jsonrpc::Request&)> handler);

  /**
   * Hold a subscription open, and be told what it asked to hear.
   *
   * The newest revision has no standalone stream and no
   * `resources/subscribe`: a client says what it wants to hear and the
   * answer to that request never comes until the subscription ends. So
   * this looks like a request whose answer takes as long as the
   * subscription does, because that is what it is.
   *
   * Several may be held at once, each with its own filter and its own
   * id, each on a connection of its own — a subscription sharing the
   * request connection would queue every other request behind it.
   *
   * @param what On_notification is called for everything this
   *        subscription asked for, on the dispatcher thread, and for the
   *        acknowledgement that says which of it will actually arrive.
   * @return The subscription's id, which every message on it carries,
   *         and which ends it. Zero when none could be opened — no
   *         connection, or not this revision.
   */
  int64_t listen(
      const protocol::modern::NotificationFilter& what,
      std::function<void(const jsonrpc::Notification&)> on_notification);

  /**
   * End one.
   *
   * By letting go of its connection, there being no message that ends a
   * subscription in this revision — a client that stops reading has
   * ended it, and that is the whole of the asking.
   */
  void stopListening(int64_t subscription);

  /** How many subscriptions this client is holding. */
  size_t subscriptionsHeld() const;

  // Progress tracking - register callback for progress updates
  void trackProgress(const ProgressToken& token,
                     std::function<void(double)> callback);

  // Get client statistics
  const McpClientStats& getClientStats() const { return client_stats_; }

  /**
   * True while the server is holding a stream open to this client.
   *
   * What it answers is whether anything the server says unprompted has
   * somewhere to arrive. False before the stream is established, after
   * it is lost, and for as long as a server that serves no stream is
   * being talked to.
   */
  bool isServerStreamOpen() const { return server_stream_open_; }

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
    void onTransportStatus(int status_code,
                           const optional<RequestId>& request_id,
                           const std::string& detail) override {
      client_.handleTransportStatus(status_code, request_id, detail);
    }
    void onClientStreamEvent(ClientStreamEvent event,
                             const optional<RequestId>& request_id,
                             const std::string& last_event_id) override {
      client_.handleClientStreamEvent(event, request_id, last_event_id);
    }
    void onMessageEndpoint(const std::string& endpoint) override {
      client_.handleMessageEndpoint(endpoint);
    }

   private:
    McpClient& client_;
  };

  // Internal message handlers
  void handleRequest(const Request& request);
  void handleNotification(const Notification& notification);
  void handleResponse(const Response& response);
  void handleConnectionEvent(network::ConnectionEvent event);
  void handleError(const Error& error);

  // What an HTTP status means for the request it answered. Dispatcher
  // thread. See McpProtocolCallbacks::onTransportStatus.
  void handleTransportStatus(int status_code,
                             const optional<RequestId>& request_id,
                             const std::string& detail);

  // What became of the client's stream. Dispatcher thread.
  // See McpProtocolCallbacks::onClientStreamEvent.
  void handleClientStreamEvent(ClientStreamEvent event,
                               const optional<RequestId>& request_id,
                               const std::string& last_event_id);

  // The older transport has said where to post, which is the only proof
  // that it is what this server speaks. Dispatcher thread.
  void handleMessageEndpoint(const std::string& endpoint);

 private:
  // Internal request handling
  RequestId generateRequestId();
  std::shared_ptr<RequestContext> createRequestContext(
      const std::string& method, const optional<Metadata>& params);
  std::shared_ptr<RequestContext> createRequestContext(
      const std::string& method,
      const optional<Metadata>& params,
      const std::map<std::string, std::string>& http_headers);
  void sendRequestInternal(std::shared_ptr<RequestContext> context);
  void handleTimeout(std::shared_ptr<RequestContext> context);
  void retryRequest(std::shared_ptr<RequestContext> context);

  // What the initialize request offers, asked for in two places: the
  // first handshake and the one that follows a session the server has
  // forgotten. They have to say the same thing.
  Metadata buildInitializeParams() const;

  // Send a request the client itself is waiting on, carrying on from
  // the dispatcher thread when the answer comes rather than blocking a
  // caller's future. Dispatcher thread.
  void sendInternalRequest(const std::string& method,
                           const optional<Metadata>& params,
                           std::function<void(const Response&)> on_response);

  // Tell the server the handshake is complete. Sent after every
  // successful initialize, including the ones nobody asked for.
  void sendInitializedNotification();

  // Start a new session because the server has forgotten the old one,
  // and send again, once each, the requests held for it. Dispatcher
  // thread; does nothing when one is already under way, which is what
  // makes several requests failing at once cost a single handshake.
  void startReinitialize();

  // Answer a request with an error and stop tracking it.
  void completeRequestWithError(const std::shared_ptr<RequestContext>& context,
                                const Error& error);

  // Ask for the stream, now or after the window below has passed.
  // Dispatcher thread; does nothing where the server has already said it
  // does not serve one.
  void openServerStream(const std::string& last_event_id);
  void scheduleServerStreamReopen(const std::string& last_event_id);

  // Ask for the rest of an answer that was cut off, on a stream carrying
  // the cursor it stopped at — or answer the request, once it has been
  // asked for as many times as it is going to be.
  void resumeAnswer(const std::shared_ptr<RequestContext>& context,
                    const std::string& last_event_id);

  // Internal reconnection logic (must be called on dispatcher thread)
  VoidResult reconnectInternal();
  void clearConnectionCallbacksForShutdown();

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

  /**
   * True when what the server speaks is to be found out by asking it
   * rather than assumed. False for an explicitly chosen transport, for
   * a scheme that is not HTTP, and where negotiation has been turned
   * off — each of which is somebody having already decided.
   */
  bool detectsTransport(const std::string& uri) const;

  // The ladder. Each rung reports to the next; whichever one settles
  // brings up a transport or fails the connect, once. Dispatcher thread.
  void runTransportLadder(const std::string& uri);
  void runClassicRung(const std::string& uri);
  void runLegacyRung(const std::string& uri);

  // Bring up a transport that has been decided on.
  void startTransport(TransportType transport);

  // Give up, saying what was asked and what each answer was. One error,
  // because three of them leave a caller to work out which mattered.
  void failDetection(const std::string& reason);

  // Settle the promise connect() is waiting on, if it is still waiting.
  void settleConnect(const VoidResult& result);

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
  static constexpr int kConnectionIdleTimeoutSec =
      30;  // Increased for SSE - server responses may take time

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

  // Application-registered notification handlers, keyed by notification method.
  // Guarded by a mutex because registration may happen on the application
  // thread while dispatch (lookup) happens on the dispatcher thread. Mirrors
  // the server's notification_handlers_/handlers_mutex_ design.
  std::map<std::string, std::function<void(const jsonrpc::Notification&)>>
      notification_handlers_;
  std::mutex notification_handlers_mutex_;

  // Questions this client can answer when a server asks them.
  std::map<std::string,
           std::function<jsonrpc::ResponseResult(const jsonrpc::Request&)>>
      request_handlers_;
  mutable std::mutex request_handlers_mutex_;

  /** What each subscription this client holds wants to be told. */
  std::map<int64_t, std::function<void(const jsonrpc::Notification&)>>
      subscriptions_;
  mutable std::mutex subscriptions_mutex_;

  // Protocol state
  bool initialized_{false};
  ServerCapabilities server_capabilities_;

  // Streamable HTTP only: what this client holds between requests — the
  // session id the server minted, the revision the handshake settled on,
  // and which request each response is answering. Made on the first
  // connection and kept across reconnects, because a session is the
  // conversation rather than the socket it happens over.
  transport::StreamableHttpClientSessionPtr streamable_session_;

  // A new session is being started because the server forgot the last
  // one. One at a time: every request in flight when a session expires
  // is answered 404, and they queue against the one handshake rather
  // than each asking for their own.
  bool reinitializing_{false};

  // Requests waiting for that handshake, to be sent again once it
  // lands. Held rather than answered, because the server did not run
  // them — a 404 is a refusal to look at the body.
  std::vector<std::shared_ptr<RequestContext>> held_for_new_session_;

  // The server said it does not serve a standalone stream. A standing
  // answer rather than a passing one, so it is remembered and not asked
  // again for as long as this client lives.
  bool server_stream_refused_{false};

  // The server has a stream to reach this client on. Read from other
  // threads, written on the dispatcher.
  std::atomic<bool> server_stream_open_{false};

  // How many times in a row asking for the stream back has been tried,
  // which is what decides how long to wait before the next one. Reset
  // by a stream that opens.
  size_t server_stream_attempts_{0};
  event::TimerPtr server_stream_timer_;
  // Where the stream that is being waited for should carry on from.
  std::string pending_stream_cursor_;

  // The request whose answer the stream is currently carrying, if it is
  // carrying one. A stream opened to pick up an interrupted answer is
  // still the stream, so losing it again is another failed attempt at
  // that answer and not merely a stream that closed.
  optional<RequestId> stream_recovering_;

  // Working out what the server speaks.
  //
  // The oldest transport is not asked about, it is attempted: it has
  // proved itself when the server says where to post, and until then a
  // connection that is merely up proves nothing. So while this is set,
  // a connection coming up is not success — which is the one thing
  // about the connect that the ladder changes.
  bool legacy_probing_{false};
  event::TimerPtr legacy_probe_timer_;

  // What the rungs before this one were told, kept so that giving up
  // can say what was asked rather than only that it did not work.
  std::string ladder_notes_;

  TransportProbePtr modern_probe_;
  std::unique_ptr<ClassicProbe> classic_probe_;

  // What was settled on, so a reconnect carries on with the transport
  // this server was found to speak rather than asking a question that
  // has already been answered.
  optional<TransportType> settled_transport_;

  // Backoff for asking for the stream back — exponential, capped, and
  // jittered so that every client of a server that has just come back
  // does not arrive at once.
  std::unique_ptr<RetryManager> server_stream_backoff_;
  std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};

  // Protocol state machine for managing MCP protocol lifecycle
  std::unique_ptr<protocol::McpProtocolStateMachine> protocol_state_machine_;

  // Periodic task management using dispatcher timers
  void schedulePeriodicTasks();
  void processQueuedRequests();

  // Timer handles for periodic tasks
  event::TimerPtr timeout_timer_;
  event::TimerPtr retry_timer_;

  // Connection completion promise - set when handleConnectionEvent fires
  std::shared_ptr<std::promise<VoidResult>> pending_connect_promise_;
  std::mutex connect_promise_mutex_;

  // Protocol state coordination
  /**
   * Whether what came back is a question rather than this request's
   * answer.
   *
   * Only ever true in the newest revision. No older one can ask, so no
   * older one's answer is read this way.
   */
  bool answerIsAQuestion(const jsonrpc::Response& response) const;

  /**
   * What this client can do, as the newest revision has every request
   * declare it.
   *
   * Follows from what it can actually answer rather than from what it
   * was configured to claim: a server refuses to ask for anything not
   * declared, so a client with a handler and no declaration would be
   * refused the one question it could have answered.
   */
  json::JsonValue declaredCapabilities() const;

  /** A response's result as JSON, however it happens to be held. */
  bool resultAsJson(const jsonrpc::Response& response,
                    json::JsonValue* out) const;

  /**
   * Answer what the server asked and send the whole request again.
   *
   * Under an id of its own: the two rounds are independent requests, and
   * the caller's wait moves to the new one, so nothing needs to be told
   * that its request now has a different name.
   *
   * @param why_not Filled when this could not be done, in which case the
   *                request is failed with it rather than handed a result
   *                that is not an answer.
   * @return True when the request has gone out again, so there is
   *         nothing left to complete.
   */
  bool askAndSendAgain(const std::shared_ptr<RequestContext>& request,
                       const jsonrpc::Response& response,
                       Error* why_not);

  /**
   * Put one of the server's questions to this client's own handlers.
   *
   * The same ones that answer a server which asks by sending a request:
   * what is being asked has not changed with the era, only how the
   * asking travels. Null when nothing could answer it, which the server
   * reads as the question by that name going unanswered.
   */
  json::JsonValue askOurselves(const protocol::modern::InputRequest& asked);

  /**
   * Let go of one subscription: forget what it wanted, and let go of the
   * connection it was held on.
   *
   * @return False when this client was not holding it, which a second
   *         ending or an unrelated request completing both look like.
   */
  bool releaseSubscription(int64_t subscription);

  /**
   * Run something on the dispatcher and wait for it, or run it here when
   * this already is the dispatcher.
   *
   * @return False when it did not run — there is no dispatcher, this
   *         client is shutting down, or the wait ran out. Bounded on
   *         purpose: a loop being told to stop may never reach what was
   *         posted to it, and waiting forever for that is a hang.
   */
  bool runOnDispatcher(const std::function<void()>& work);

  /**
   * Hand a notification to the subscription it belongs to, if any.
   *
   * Every message on a subscription carries its id, because on a
   * transport where several share one client there is nothing else to
   * tell them apart by.
   *
   * @return True when it belonged to one, and so is not the
   *         application's to route by method.
   */
  bool routeToSubscription(const jsonrpc::Notification& notification);

  /** Deliver an answer to whoever is waiting for it, and account for it. */
  void completeRequest(const std::shared_ptr<RequestContext>& request,
                       const jsonrpc::Response& response);

  static InitializeResult parseInitializeResponse(
      const jsonrpc::Response& response, const std::string& protocol_version);

  /**
   * The same, from the answer an era without a handshake gives instead.
   *
   * @param protocol_version What is being spoken, settled before any of
   *                         this was sent. Not read out of the answer,
   *                         which lists what the server serves.
   */
  static InitializeResult parseDiscoverResponse(
      const jsonrpc::Response& response, const std::string& protocol_version);
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
