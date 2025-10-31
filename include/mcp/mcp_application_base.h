/**
 * @file mcp_application_base.h
 * @brief Base application framework using external filter implementations
 *
 * This provides a production-ready base for MCP applications with:
 * - Worker thread model with dedicated dispatcher threads
 * - Filter chain architecture using external filter implementations
 * - Enhanced error handling with detailed failure tracking
 * - Flow control with watermark-based backpressure
 * - Built-in observability (metrics, logging, tracing)
 */

#ifndef MCP_APPLICATION_BASE_H
#define MCP_APPLICATION_BASE_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>
#include <vector>

#include "mcp/buffer.h"
#include "mcp/builders.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/backpressure_filter.h"
#include "mcp/filter/circuit_breaker_filter.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/filter/metrics_filter.h"
#include "mcp/filter/rate_limit_filter.h"
#include "mcp/filter/request_validation_filter.h"
#include "mcp/json/json_serialization.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/filter.h"
#include "mcp/network/socket_interface.h"
#include "mcp/network/socket_interface_impl.h"

// Production-style assertion macro
#ifndef ASSERT
#ifdef NDEBUG
#define ASSERT(x) ((void)0)
#else
#define ASSERT(x)                                                       \
  do {                                                                  \
    if (!(x)) {                                                         \
      std::cerr << "Assertion failed: " #x << " at " << __FILE__ << ":" \
                << __LINE__ << std::endl;                               \
      std::abort();                                                     \
    }                                                                   \
  } while (0)
#endif
#endif

namespace mcp {
namespace application {

// Forward declarations
class WorkerContext;
class FilterChainFactory;
class WorkerFactory;

// Forward declarations
class ConnectionPool;
class FailureTracker;
class FilterChainBuilder;

/**
 * Base class for metrics tracking filters (legacy API compatibility)
 *
 * Tracks bytes sent/received and updates application statistics.
 * Derived classes can extend this for specific metrics tracking.
 */
class MetricsTrackingFilter : public network::NetworkFilterBase {
 public:
  MetricsTrackingFilter(std::atomic<uint64_t>& bytes_received,
                        std::atomic<uint64_t>& bytes_sent)
      : bytes_received_(bytes_received), bytes_sent_(bytes_sent) {}

  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    bytes_received_ += data.length();
    return onDataMetrics(data, end_stream);
  }

  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    bytes_sent_ += data.length();
    return onWriteMetrics(data, end_stream);
  }

  network::FilterStatus onNewConnection() override {
    return network::FilterStatus::Continue;
  }

 protected:
  // Override these for additional metrics tracking
  virtual network::FilterStatus onDataMetrics(Buffer& data, bool end_stream) {
    return network::FilterStatus::Continue;
  }

  virtual network::FilterStatus onWriteMetrics(Buffer& data, bool end_stream) {
    return network::FilterStatus::Continue;
  }

 private:
  std::atomic<uint64_t>& bytes_received_;
  std::atomic<uint64_t>& bytes_sent_;
};

/**
 * Common adapter from McpProtocolCallbacks to
 * JsonRpcProtocolFilter::MessageHandler
 *
 * This adapter bridges the MCP protocol callback interface to the JSON-RPC
 * filter message handler interface, allowing ApplicationBase-derived classes to
 * use the unified JsonRpcProtocolFilter.
 */
class McpToJsonRpcAdapter
    : public filter::JsonRpcProtocolFilter::MessageHandler {
 public:
  explicit McpToJsonRpcAdapter(McpProtocolCallbacks& callbacks)
      : callbacks_(callbacks) {}

  void onRequest(const jsonrpc::Request& request) override {
    callbacks_.onRequest(request);
  }

  void onNotification(const jsonrpc::Notification& notification) override {
    callbacks_.onNotification(notification);
  }

  void onResponse(const jsonrpc::Response& response) override {
    callbacks_.onResponse(response);
  }

  void onProtocolError(const Error& error) override {
    callbacks_.onError(error);
  }

 private:
  McpProtocolCallbacks& callbacks_;
};

/**
 * Application statistics for observability
 */
struct ApplicationStats {
  std::atomic<uint64_t> connections_total{0};
  std::atomic<uint64_t> connections_active{0};
  std::atomic<uint64_t> requests_total{0};
  std::atomic<uint64_t> requests_success{0};
  std::atomic<uint64_t> requests_failed{0};
  std::atomic<uint64_t> bytes_sent{0};
  std::atomic<uint64_t> bytes_received{0};
  std::atomic<uint64_t> errors_total{0};

  // Latency tracking
  std::atomic<uint64_t> request_duration_ms_total{0};
  std::atomic<uint64_t> request_duration_ms_max{0};
  std::atomic<uint64_t> request_duration_ms_min{UINT64_MAX};
};

/**
 * Metrics callbacks implementation for MetricsFilter
 */
class ApplicationMetricsCallbacks
    : public filter::MetricsFilter::MetricsCallbacks {
 public:
  explicit ApplicationMetricsCallbacks(ApplicationStats& stats)
      : stats_(stats) {}

  void onMetricsUpdate(const filter::ConnectionMetrics& metrics) override {
    // Update application stats from metrics using atomic store
    stats_.bytes_sent.store(metrics.bytes_sent);
    stats_.bytes_received.store(metrics.bytes_received);
    stats_.requests_total.store(metrics.requests_sent);

    // Update latency stats
    if (metrics.max_latency_ms > stats_.request_duration_ms_max.load()) {
      stats_.request_duration_ms_max.store(metrics.max_latency_ms);
    }
    if (metrics.min_latency_ms < stats_.request_duration_ms_min.load()) {
      stats_.request_duration_ms_min.store(metrics.min_latency_ms);
    }
  }

  void onThresholdExceeded(const std::string& metric_name,
                           uint64_t value,
                           uint64_t threshold) override {
    std::cerr << "[WARNING] Metric threshold exceeded: " << metric_name
              << " (value: " << value << ", threshold: " << threshold << ")"
              << std::endl;
  }

 private:
  ApplicationStats& stats_;
};

/**
 * Enhanced error information with failure tracking
 */
class FailureReason {
 public:
  enum class Type {
    ConnectionFailure,
    ProtocolError,
    Timeout,
    ResourceExhaustion,
    InvalidMessage,
    InternalError
  };

  FailureReason(Type type, const std::string& description)
      : type_(type),
        description_(description),
        timestamp_(std::chrono::steady_clock::now()) {}

  void addContext(const std::string& key, const std::string& value) {
    context_[key] = value;
  }

  void addStackFrame(const std::string& frame) {
    stack_trace_.push_back(frame);
  }

  Type getType() const { return type_; }
  const std::string& getDescription() const { return description_; }
  const auto& getTimestamp() const { return timestamp_; }
  const auto& getContext() const { return context_; }
  const auto& getStackTrace() const { return stack_trace_; }

 private:
  Type type_;
  std::string description_;
  std::chrono::steady_clock::time_point timestamp_;
  std::map<std::string, std::string> context_;
  std::vector<std::string> stack_trace_;
};

/**
 * LinkedObject provides O(1) insertion/removal for doubly-linked lists
 * Following production pattern for efficient connection management
 */
template <typename T>
class LinkedObject {
 public:
  LinkedObject() = default;
  virtual ~LinkedObject() {
    // Ensure removed from any list before destruction
    ASSERT(!list_entry_.next_);
    ASSERT(!list_entry_.prev_);
  }

  // Move between lists in O(1)
  void moveBetweenLists(std::list<std::unique_ptr<T>>& from,
                        std::list<std::unique_ptr<T>>& to) {
    // Extract the element at our stored position
    auto node = std::move(*entry_);
    from.erase(entry_);
    to.push_back(std::move(node));
    entry_ = std::prev(to.end());
  }

  // Remove from list in O(1)
  void removeFromList(std::list<std::unique_ptr<T>>& list) {
    list.erase(entry_);
    entry_ = {};
  }

  // LinkedList helper for moving into lists
  struct LinkedList {
    static void moveIntoList(std::unique_ptr<T>&& item,
                             std::list<std::unique_ptr<T>>& list) {
      auto* raw = item.get();
      list.push_back(std::move(item));
      static_cast<LinkedObject<T>*>(raw)->entry_ = std::prev(list.end());
    }
  };

 private:
  friend struct LinkedList;

  // Store iterator for O(1) removal
  typename std::list<std::unique_ptr<T>>::iterator entry_;

  // For debug assertions
  struct ListEntry {
    void* next_{nullptr};
    void* prev_{nullptr};
  } list_entry_;
};

/**
 * Base class for connection pool clients
 * Following production pattern - no locks, single dispatcher thread
 */
class PooledConnection : public LinkedObject<PooledConnection> {
 public:
  using ConnectionPtr = std::shared_ptr<network::Connection>;

  enum class State {
    Connecting,  // Connection being established
    Ready,       // Available for streams
    Busy,        // At stream limit
    Draining,    // No new streams, will close when idle
    Closed       // Connection closed
  };

  PooledConnection(ConnectionPtr conn, uint32_t stream_limit)
      : connection_(conn),
        remaining_streams_(stream_limit),
        concurrent_stream_limit_(stream_limit) {}

  virtual ~PooledConnection() = default;

  State state() const { return state_; }
  void setState(State state) { state_ = state; }

  bool readyForStream() const {
    return state_ == State::Ready && currentUnusedCapacity() > 0;
  }

  int64_t currentUnusedCapacity() const {
    return std::min<int64_t>(remaining_streams_,
                             concurrent_stream_limit_ - active_streams_);
  }

  void onStreamCreated() {
    active_streams_++;
    remaining_streams_--;
    if (currentUnusedCapacity() == 0) {
      state_ = State::Busy;
    }
  }

  void onStreamClosed() {
    active_streams_--;
    if (state_ == State::Busy && currentUnusedCapacity() > 0) {
      state_ = State::Ready;
    }
  }

  ConnectionPtr connection_;
  uint32_t remaining_streams_;
  uint32_t concurrent_stream_limit_;
  uint32_t active_streams_{0};

 private:
  State state_{State::Connecting};
};

/**
 * Lock-free connection pool for efficient connection reuse
 * All operations must be called from the dispatcher thread
 */
class ConnectionPool {
 public:
  using ConnectionPtr = std::shared_ptr<network::Connection>;
  using PooledConnectionPtr = std::unique_ptr<PooledConnection>;

  virtual ~ConnectionPool() {
    // Destroy all connections
    ready_connections_.clear();
    busy_connections_.clear();
    connecting_connections_.clear();
  }

  ConnectionPool(event::Dispatcher& dispatcher,
                 size_t max_connections,
                 uint32_t streams_per_connection)
      : dispatcher_(dispatcher),
        max_connections_(max_connections),
        streams_per_connection_(streams_per_connection) {}

  // Request a connection for a new stream (called from dispatcher thread)
  ConnectionPtr acquireConnection() {
    ASSERT(dispatcher_.isThreadSafe());

    // First try ready connections
    if (!ready_connections_.empty()) {
      auto& pooled = *ready_connections_.front();
      if (pooled.readyForStream()) {
        pooled.onStreamCreated();
        if (!pooled.readyForStream()) {
          // Move to busy list
          transitionConnectionState(pooled, PooledConnection::State::Busy);
        }
        return pooled.connection_;
      }
    }

    // Create new connection if under limit
    if (total_connections_ < max_connections_) {
      return createNewConnectionForStream();
    }

    // No available connection
    return nullptr;
  }

  // Release a stream on a connection (called from dispatcher thread)
  void releaseStream(ConnectionPtr conn) {
    ASSERT(dispatcher_.isThreadSafe());

    // Find the pooled connection
    PooledConnection* pooled = findPooledConnection(conn.get());
    if (!pooled) {
      return;
    }

    pooled->onStreamClosed();

    // Move from busy to ready if it has capacity
    if (pooled->state() == PooledConnection::State::Busy &&
        pooled->readyForStream()) {
      transitionConnectionState(*pooled, PooledConnection::State::Ready);
    }
  }

  // Called when connection is established
  void onConnectionConnected(network::Connection* conn) {
    ASSERT(dispatcher_.isThreadSafe());

    PooledConnection* pooled = findPooledConnection(conn);
    if (pooled && pooled->state() == PooledConnection::State::Connecting) {
      transitionConnectionState(*pooled, PooledConnection::State::Ready);
    }
  }

  // Called when connection fails or closes
  void onConnectionClosed(network::Connection* conn) {
    ASSERT(dispatcher_.isThreadSafe());

    PooledConnection* pooled = findPooledConnection(conn);
    if (pooled) {
      removeConnection(*pooled);
    }
  }

  size_t getActiveConnections() const {
    return busy_connections_.size() + ready_connections_.size();
  }

  size_t getTotalConnections() const { return total_connections_; }

 protected:
  virtual ConnectionPtr createNewConnection() = 0;

 private:
  ConnectionPtr createNewConnectionForStream() {
    auto conn = createNewConnection();
    if (conn) {
      auto pooled =
          std::make_unique<PooledConnection>(conn, streams_per_connection_);
      pooled->setState(PooledConnection::State::Connecting);
      pooled->onStreamCreated();  // Reserve for current stream

      // Add to connecting list
      LinkedObject<PooledConnection>::LinkedList::moveIntoList(
          std::move(pooled), connecting_connections_);
      total_connections_++;

      return conn;
    }
    return nullptr;
  }

  void transitionConnectionState(PooledConnection& pooled,
                                 PooledConnection::State new_state) {
    auto& current_list = owningList(pooled.state());
    auto& target_list = owningList(new_state);

    if (&current_list != &target_list) {
      pooled.setState(new_state);
      pooled.moveBetweenLists(current_list, target_list);
    } else {
      pooled.setState(new_state);
    }
  }

  std::list<PooledConnectionPtr>& owningList(PooledConnection::State state) {
    switch (state) {
      case PooledConnection::State::Connecting:
        return connecting_connections_;
      case PooledConnection::State::Ready:
        return ready_connections_;
      case PooledConnection::State::Busy:
      case PooledConnection::State::Draining:
        return busy_connections_;
      default:
        ASSERT(false);
        return busy_connections_;
    }
  }

  PooledConnection* findPooledConnection(network::Connection* raw_conn) {
    for (auto& pooled : ready_connections_) {
      if (pooled->connection_.get() == raw_conn) {
        return pooled.get();
      }
    }
    for (auto& pooled : busy_connections_) {
      if (pooled->connection_.get() == raw_conn) {
        return pooled.get();
      }
    }
    for (auto& pooled : connecting_connections_) {
      if (pooled->connection_.get() == raw_conn) {
        return pooled.get();
      }
    }
    return nullptr;
  }

  void removeConnection(PooledConnection& pooled) {
    auto& current_list = owningList(pooled.state());
    pooled.removeFromList(current_list);
    total_connections_--;
  }

  event::Dispatcher& dispatcher_;
  size_t max_connections_;
  uint32_t streams_per_connection_;
  size_t total_connections_{0};

  // Connection lists - no locks needed, dispatcher thread only
  std::list<PooledConnectionPtr> ready_connections_;
  std::list<PooledConnectionPtr> busy_connections_;
  std::list<PooledConnectionPtr> connecting_connections_;
};

/**
 * Worker context for each dispatcher thread (legacy API support)
 * Following best practices where each worker has its own dispatcher
 */
class WorkerContext {
 public:
  WorkerContext(const std::string& name,
                event::Dispatcher& dispatcher,
                network::SocketInterface& socket_interface)
      : name_(name),
        dispatcher_(dispatcher),
        socket_interface_(socket_interface) {}

  event::Dispatcher& getDispatcher() { return dispatcher_; }
  const std::string& getName() const { return name_; }
  network::SocketInterface& getSocketInterface() { return socket_interface_; }

 private:
  std::string name_;
  event::Dispatcher& dispatcher_;
  network::SocketInterface& socket_interface_;
};

/**
 * Filter chain builder for constructing processing pipelines
 * Enhanced to support both old and new APIs following best practices
 */
class FilterChainBuilder {
 public:
  using FilterPtr = std::shared_ptr<network::Filter>;

  FilterChainBuilder(event::Dispatcher& dispatcher,
                     ApplicationStats& stats,
                     network::WriteFilterCallbacks* write_callbacks = nullptr)
      : dispatcher_(dispatcher),
        stats_(stats),
        write_callbacks_(write_callbacks) {}

  // Provide access to dispatcher for filter creation
  event::Dispatcher& getDispatcher() { return dispatcher_; }

  // Add rate limiting filter
  FilterChainBuilder& withRateLimiting(
      const filter::RateLimitConfig& config,
      network::Connection* connection = nullptr) {
    auto callbacks = std::make_shared<RateLimitCallbacks>(stats_, connection,
                                                          write_callbacks_);
    auto filter = std::make_shared<filter::RateLimitFilter>(*callbacks, config);
    filters_.push_back(filter);
    rate_limit_callbacks_ = callbacks;
    return *this;
  }

  // Add metrics collection filter
  FilterChainBuilder& withMetrics(const filter::MetricsFilter::Config& config) {
    auto callbacks = std::make_shared<ApplicationMetricsCallbacks>(stats_);
    auto filter = std::make_shared<filter::MetricsFilter>(callbacks, config);
    filters_.push_back(filter);
    metrics_filter_ = filter;
    return *this;
  }

  // Add circuit breaker filter
  FilterChainBuilder& withCircuitBreaker(
      const filter::CircuitBreakerConfig& config,
      network::Connection* connection = nullptr) {
    auto callbacks = std::make_shared<CircuitBreakerCallbacks>(
        stats_, connection, write_callbacks_);
    auto filter =
        std::make_shared<filter::CircuitBreakerFilter>(*callbacks, config);
    filters_.push_back(filter);
    circuit_breaker_callbacks_ = callbacks;
    return *this;
  }

  // Add backpressure filter
  FilterChainBuilder& withBackpressure(
      const filter::BackpressureConfig& config,
      network::Connection* connection = nullptr) {
    auto callbacks = std::make_shared<BackpressureCallbacks>(stats_, connection,
                                                             write_callbacks_);
    auto filter =
        std::make_shared<filter::BackpressureFilter>(*callbacks, config);
    filters_.push_back(filter);
    backpressure_callbacks_ = callbacks;
    return *this;
  }

  // Add request validation filter
  FilterChainBuilder& withRequestValidation(
      const filter::RequestValidationConfig& config,
      network::Connection* connection = nullptr) {
    auto callbacks = std::make_shared<RequestValidationCallbacks>(
        stats_, connection, write_callbacks_);
    auto filter =
        std::make_shared<filter::RequestValidationFilter>(*callbacks, config);
    filters_.push_back(filter);
    validation_callbacks_ = callbacks;
    return *this;
  }

  // Build the filter chain
  std::vector<FilterPtr> build() { return filters_; }

  // Legacy API support - add filter with factory function
  FilterChainBuilder& addFilter(
      std::function<network::FilterSharedPtr()> factory) {
    auto filter = factory();
    filters_.push_back(filter);
    return *this;
  }

  // Legacy API support - add filter instance directly
  FilterChainBuilder& addFilterInstance(network::FilterSharedPtr filter) {
    filters_.push_back(filter);
    return *this;
  }

  // Get metrics filter for external access
  std::shared_ptr<filter::MetricsFilter> getMetricsFilter() const {
    return metrics_filter_;
  }

 private:
  // Callback implementations
  class RateLimitCallbacks : public filter::RateLimitFilter::Callbacks {
   public:
    RateLimitCallbacks(ApplicationStats& stats,
                       network::Connection* connection = nullptr,
                       network::WriteFilterCallbacks* write_callbacks = nullptr)
        : stats_(stats),
          connection_(connection),
          write_callbacks_(write_callbacks) {}

    void onRequestAllowed() override {
      // Request was allowed through - update stats
      stats_.requests_total++;
    }

    void onRequestLimited(std::chrono::milliseconds retry_after) override {
      stats_.requests_failed++;

      // Send rate limit error response to client
      if (connection_) {
        // Create error data as map<string, string> for ErrorData
        std::map<std::string, std::string> errorData;
        errorData["retry_after_ms"] = std::to_string(retry_after.count());
        errorData["error_type"] = "rate_limit";

        // Use builders to create error response
        auto error = make<Error>(-32429, "Rate limit exceeded").data(errorData);

        auto response = make<jsonrpc::Response>(last_request_id_).error(error);

        // Send error response through connection
        if (write_callbacks_) {
          auto response_obj = response.build();
          // Note: Actual implementation would serialize and send the response
          // through the write callbacks. This requires access to the buffer
          // implementation which is not exposed in the public API.
          // The connection would handle this through its filter chain.
        }
      }
    }

    void onRateLimitWarning(int remaining) override {
      // Could trigger alerts or adjust behavior when approaching limit
      if (remaining < 10) {
        // Critical warning - might want to start queuing or buffering
        stats_.errors_total++;
      }
    }

    void setLastRequestId(const RequestId& id) { last_request_id_ = id; }

   private:
    ApplicationStats& stats_;
    network::Connection* connection_;
    network::WriteFilterCallbacks* write_callbacks_;
    RequestId last_request_id_;
  };

  class CircuitBreakerCallbacks
      : public filter::CircuitBreakerFilter::Callbacks {
   public:
    CircuitBreakerCallbacks(
        ApplicationStats& stats,
        network::Connection* connection = nullptr,
        network::WriteFilterCallbacks* write_callbacks = nullptr)
        : stats_(stats),
          connection_(connection),
          write_callbacks_(write_callbacks) {}

    void onStateChange(filter::CircuitState old_state,
                       filter::CircuitState new_state,
                       const std::string& reason) override {
      // Handle state transitions
      if (new_state == filter::CircuitState::OPEN) {
        // Circuit is open - stop accepting new requests
        is_circuit_open_ = true;

        // Could trigger failover to backup service or cache
        // Could notify load balancer to redirect traffic
        stats_.errors_total++;
      } else if (new_state == filter::CircuitState::HALF_OPEN) {
        // Testing if service recovered - allow limited traffic
        is_circuit_open_ = false;
      } else if (new_state == filter::CircuitState::CLOSED) {
        // Service healthy - resume normal operations
        is_circuit_open_ = false;
      }
    }

    void onRequestBlocked(const std::string& method) override {
      stats_.requests_failed++;

      // Send service unavailable error to client
      if (connection_) {
        // Create error data as map<string, string> for ErrorData
        std::map<std::string, std::string> errorData;
        errorData["method"] = method;
        errorData["error_type"] = "circuit_breaker_open";
        errorData["retry_after_ms"] = "5000";  // Suggest retry after 5 seconds

        // Use builders to create error response
        auto error = make<Error>(-32503, "Service temporarily unavailable")
                         .data(errorData);

        auto response = make<jsonrpc::Response>(last_request_id_).error(error);

        // Send error response through connection
        if (write_callbacks_) {
          auto response_obj = response.build();
          // Note: Actual implementation would serialize and send the response
          // through the write callbacks. This requires access to the buffer
          // implementation which is not exposed in the public API.
          // The connection would handle this through its filter chain.
        }
      }
    }

    void onHealthUpdate(double success_rate, uint64_t latency_ms) override {
      // Track health metrics for monitoring/alerting
      if (success_rate < 0.5) {  // Less than 50% success rate
        // Could trigger alerts or start pre-emptive actions
        stats_.errors_total++;
      }

      // Update latency tracking
      if (latency_ms > stats_.request_duration_ms_max.load()) {
        stats_.request_duration_ms_max.store(latency_ms);
      }
    }

    void setLastRequestId(const RequestId& id) { last_request_id_ = id; }

    bool isCircuitOpen() const { return is_circuit_open_; }

   private:
    ApplicationStats& stats_;
    network::Connection* connection_;
    network::WriteFilterCallbacks* write_callbacks_;
    RequestId last_request_id_;
    std::atomic<bool> is_circuit_open_{false};
  };

  class BackpressureCallbacks : public filter::BackpressureFilter::Callbacks {
   public:
    BackpressureCallbacks(
        ApplicationStats& stats,
        network::Connection* connection = nullptr,
        network::WriteFilterCallbacks* write_callbacks = nullptr)
        : stats_(stats),
          connection_(connection),
          write_callbacks_(write_callbacks) {}

    void onBackpressureApplied() override {
      is_backpressure_active_ = true;

      // Pause reading from connection to prevent buffer overflow
      if (connection_) {
        connection_->readDisable(true);
      }

      // Could also:
      // - Start buffering to disk if critical
      // - Notify upstream to slow down
      // - Switch to degraded mode with reduced functionality
    }

    void onBackpressureReleased() override {
      is_backpressure_active_ = false;

      // Resume reading from connection
      if (connection_) {
        connection_->readDisable(false);
      }

      // Could also:
      // - Process any buffered data
      // - Notify upstream that normal rate can resume
      // - Switch back to normal mode
    }

    void onDataDropped(size_t bytes) override {
      stats_.errors_total++;
      dropped_bytes_ += bytes;

      // Critical situation - data loss occurring
      if (connection_) {
        // Send warning to client about data loss
        Metadata params;
        params["bytes_dropped"] = static_cast<int64_t>(bytes);
        params["total_dropped"] = static_cast<int64_t>(dropped_bytes_.load());
        params["reason"] = std::string("buffer_overflow");

        auto notification =
            make<jsonrpc::Notification>("backpressure.data_dropped")
                .params(params);

        // Send notification through connection
        if (write_callbacks_) {
          auto notification_obj = notification.build();
          // Note: Actual implementation would serialize and send the
          // notification through the write callbacks. This requires access to
          // the buffer implementation which is not exposed in the public API.
          // The connection would handle this through its filter chain.
        }
      }

      // Could trigger emergency measures:
      // - Force disconnect abusive clients
      // - Enable more aggressive rate limiting
      // - Alert operations team
    }

    bool isBackpressureActive() const { return is_backpressure_active_; }
    size_t getDroppedBytes() const { return dropped_bytes_; }

   private:
    ApplicationStats& stats_;
    network::Connection* connection_;
    network::WriteFilterCallbacks* write_callbacks_;
    std::atomic<bool> is_backpressure_active_{false};
    std::atomic<size_t> dropped_bytes_{0};
  };

  class RequestValidationCallbacks
      : public filter::RequestValidationFilter::ValidationCallbacks {
   public:
    RequestValidationCallbacks(
        ApplicationStats& stats,
        network::Connection* connection = nullptr,
        network::WriteFilterCallbacks* write_callbacks = nullptr)
        : stats_(stats),
          connection_(connection),
          write_callbacks_(write_callbacks) {}

    void onRequestValidated(const std::string& method) override {
      // Request passed validation - update success stats
      stats_.requests_success++;
      validated_requests_++;
    }

    void onRequestRejected(const std::string& method,
                           const std::string& reason) override {
      stats_.requests_failed++;
      rejected_requests_++;

      // Send validation error response to client
      if (connection_) {
        // Create error data as map<string, string> for ErrorData
        std::map<std::string, std::string> errorData;
        errorData["method"] = method;
        errorData["reason"] = reason;
        errorData["error_type"] = "validation_failed";

        // Use builders to create error response
        auto error =
            make<Error>(-32602, "Request validation failed").data(errorData);

        auto response = make<jsonrpc::Response>(last_request_id_).error(error);

        // Send error response through connection
        if (write_callbacks_) {
          auto response_obj = response.build();
          // Note: Actual implementation would serialize and send the response
          // through the write callbacks. This requires access to the buffer
          // implementation which is not exposed in the public API.
          // The connection would handle this through its filter chain.
        }

        // For security violations, might want to:
        // - Log the attempt for security audit
        // - Increment security violation counter
        // - Block client after repeated violations
        if (reason.find("security") != std::string::npos ||
            reason.find("injection") != std::string::npos) {
          security_violations_++;

          // Block client after too many security violations
          if (security_violations_ > 5 && connection_) {
            connection_->close(network::ConnectionCloseType::NoFlush);
          }
        }
      }
    }

    void onRateLimitExceeded(const std::string& method) override {
      stats_.requests_failed++;
      rate_limit_violations_++;

      // Send rate limit error for method-specific limits
      if (connection_) {
        // Create error data as map<string, string> for ErrorData
        std::map<std::string, std::string> errorData;
        errorData["method"] = method;
        errorData["error_type"] = "method_rate_limit";
        errorData["retry_after_ms"] = "1000";

        // Use builders to create error response
        auto error =
            make<Error>(-32429, "Method rate limit exceeded").data(errorData);

        auto response = make<jsonrpc::Response>(last_request_id_).error(error);

        // Send error response through connection
        if (write_callbacks_) {
          auto response_obj = response.build();
          // Note: Actual implementation would serialize and send the response
          // through the write callbacks. This requires access to the buffer
          // implementation which is not exposed in the public API.
          // The connection would handle this through its filter chain.
        }
      }
    }

    void setLastRequestId(const RequestId& id) { last_request_id_ = id; }

    size_t getValidatedRequests() const { return validated_requests_; }
    size_t getRejectedRequests() const { return rejected_requests_; }
    size_t getSecurityViolations() const { return security_violations_; }

   private:
    ApplicationStats& stats_;
    network::Connection* connection_;
    network::WriteFilterCallbacks* write_callbacks_;
    RequestId last_request_id_;
    std::atomic<size_t> validated_requests_{0};
    std::atomic<size_t> rejected_requests_{0};
    std::atomic<size_t> security_violations_{0};
    std::atomic<size_t> rate_limit_violations_{0};
  };

  event::Dispatcher& dispatcher_;
  ApplicationStats& stats_;
  network::WriteFilterCallbacks* write_callbacks_;
  std::vector<FilterPtr> filters_;

  // Store specific filter references
  std::shared_ptr<filter::MetricsFilter> metrics_filter_;
  std::shared_ptr<RateLimitCallbacks> rate_limit_callbacks_;
  std::shared_ptr<CircuitBreakerCallbacks> circuit_breaker_callbacks_;
  std::shared_ptr<BackpressureCallbacks> backpressure_callbacks_;
  std::shared_ptr<RequestValidationCallbacks> validation_callbacks_;
};

/**
 * Base class for MCP applications following production architecture
 *
 * Provides:
 * - Worker thread management
 * - Filter chain architecture using external filters
 * - Connection pooling
 * - Metrics and observability
 * - Graceful shutdown
 */
class ApplicationBase {
 public:
  /**
   * Application configuration
   */
  struct Config {
    // Legacy config compatibility
    size_t num_workers = 4;                        // For old API compatibility
    uint32_t buffer_high_watermark = 1024 * 1024;  // 1MB - legacy API
    uint32_t buffer_low_watermark = 256 * 1024;    // 256KB - legacy API
    std::string name = "MCPApplication";
    size_t worker_threads = 4;
    size_t connection_pool_size = 10;
    size_t max_idle_connections = 5;
    bool enable_metrics = true;
    bool enable_rate_limiting = true;
    bool enable_circuit_breaker = true;
    bool enable_backpressure = true;
    bool enable_request_validation = false;
    std::chrono::seconds shutdown_timeout{30};

    // Filter configurations
    filter::RateLimitConfig rate_limit_config;
    filter::MetricsFilter::Config metrics_config;
    filter::CircuitBreakerConfig circuit_breaker_config;
    filter::BackpressureConfig backpressure_config;
    filter::RequestValidationConfig validation_config;
  };

  ApplicationBase(const Config& config)
      : config_(config), shutdown_requested_(false), workers_started_(false) {
    std::cerr << "[INFO] Initializing application: " << config_.name
              << std::endl;
  }

  virtual ~ApplicationBase() { shutdown(); }

  /**
   * Initialize the application
   * Following best practice: create workers with dispatchers
   */
  virtual bool initialize() {
    // Initialize socket interface for legacy API
    if (!socket_interface_) {
      socket_interface_ = std::make_unique<network::SocketInterfaceImpl>();
    }

    // Create main dispatcher early (before workers)
    if (!main_dispatcher_) {
      main_dispatcher_owned_ =
          std::make_unique<event::LibeventDispatcher>("main");
      main_dispatcher_ = main_dispatcher_owned_.get();
      std::cerr << "[INFO] Created main dispatcher" << std::endl;
    }

    std::cerr << "[INFO] Initializing application with "
              << config_.worker_threads << " workers" << std::endl;

    // Create worker threads
    for (size_t i = 0; i < config_.worker_threads; ++i) {
      auto worker =
          std::make_unique<WorkerThread>("worker_" + std::to_string(i));
      workers_.push_back(std::move(worker));

      // Create WorkerContext for legacy API support
      // Note: We'll initialize these properly when workers start
      worker_contexts_.push_back(nullptr);
    }

    // Initialize connection pool
    connection_pool_ = createConnectionPool();

    // Mark as initialized for legacy API compatibility
    initialized_ = true;

    return true;
  }

  /**
   * Start the application
   */
  virtual bool start() {
    if (workers_started_) {
      return true;
    }

    std::cerr << "[INFO] Starting application workers" << std::endl;

    // Start worker threads
    for (size_t i = 0; i < workers_.size(); ++i) {
      workers_[i]->start();

      // Create WorkerContext for legacy API after worker starts
      if (workers_[i]->getDispatcher()) {
        worker_contexts_[i] = std::make_unique<WorkerContext>(
            "worker_" + std::to_string(i), *workers_[i]->getDispatcher(),
            *socket_interface_);

        // Initialize worker using legacy API if derived class overrides it
        initializeWorker(*worker_contexts_[i]);
      }
    }

    workers_started_ = true;
    running_ = true;      // Legacy API compatibility
    initialized_ = true;  // Legacy API compatibility

    // Application-specific startup
    return onStart();
  }

  /**
   * Run the main event loop
   */
  virtual void run() {
    std::cerr << "[INFO] Running main event loop" << std::endl;

    // Main dispatcher should already be created in initialize()
    if (!main_dispatcher_) {
      std::cerr
          << "[ERROR] Main dispatcher not initialized. Call initialize() first."
          << std::endl;
      return;
    }

    // Run until shutdown requested
    while (!shutdown_requested_) {
      main_dispatcher_->run(event::RunType::NonBlock);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      // Process any pending work
      processPendingWork();
    }
  }

  /**
   * Shutdown the application
   */
  virtual void shutdown() {
    if (shutdown_requested_) {
      return;
    }

    std::cerr << "[INFO] Shutting down application" << std::endl;
    shutdown_requested_ = true;
    running_ = false;      // Legacy API compatibility
    initialized_ = false;  // Legacy API compatibility

    // Application-specific shutdown
    onShutdown();

    // Stop worker threads
    for (auto& worker : workers_) {
      worker->stop();
    }

    // Wait for workers with timeout
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start <
           config_.shutdown_timeout) {
      bool all_stopped = true;
      for (const auto& worker : workers_) {
        if (!worker->isStopped()) {
          all_stopped = false;
          break;
        }
      }

      if (all_stopped) {
        break;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Clean up owned dispatcher
    if (main_dispatcher_owned_) {
      main_dispatcher_ = nullptr;
      main_dispatcher_owned_.reset();
    }

    std::cerr << "[INFO] Application shutdown complete" << std::endl;
  }

  /**
   * Get application statistics
   */
  const ApplicationStats& getStats() const { return stats_; }

  /**
   * Create filter chain for connection
   * Enhanced to support legacy setupFilterChain API
   */
  std::vector<std::shared_ptr<network::Filter>> createFilterChain(
      event::Dispatcher& dispatcher) {
    FilterChainBuilder builder(dispatcher, stats_);

    // Add filters based on configuration
    if (config_.enable_circuit_breaker) {
      builder.withCircuitBreaker(config_.circuit_breaker_config);
    }

    if (config_.enable_rate_limiting) {
      builder.withRateLimiting(config_.rate_limit_config);
    }

    if (config_.enable_metrics) {
      builder.withMetrics(config_.metrics_config);
    }

    if (config_.enable_request_validation) {
      builder.withRequestValidation(config_.validation_config);
    }

    if (config_.enable_backpressure) {
      builder.withBackpressure(config_.backpressure_config);
    }

    // Call legacy setupFilterChain for derived classes
    setupFilterChain(builder);

    return builder.build();
  }

 protected:
  // Override these in derived classes
  virtual bool onStart() { return true; }
  virtual void onShutdown() {}
  virtual void processPendingWork() {}
  virtual std::unique_ptr<ConnectionPool> createConnectionPool() {
    return nullptr;
  }

  // Legacy API support - override this in derived classes that need old API
  virtual void initializeWorker(WorkerContext& worker) {
    // Default implementation - derived classes override if needed
    // Workers are initialized separately for better isolation
  }

  // Legacy API support - setup filter chain using old builder API
  virtual void setupFilterChain(FilterChainBuilder& builder) {
    // Default implementation adds standard filters
    // Derived classes can override to customize
  }

  // Helper to create JSON-RPC filter (legacy API support)
  struct JsonRpcFilterBundle {
    std::shared_ptr<McpToJsonRpcAdapter> adapter;
    network::FilterSharedPtr filter;
  };

  std::shared_ptr<JsonRpcFilterBundle> createJsonRpcFilter(
      McpProtocolCallbacks& callbacks,
      event::Dispatcher& dispatcher,
      bool is_server,
      bool use_framing = true) {
    auto bundle = std::make_shared<JsonRpcFilterBundle>();

    // Create adapter
    bundle->adapter = std::make_shared<McpToJsonRpcAdapter>(callbacks);

    // Create filter with dispatcher
    bundle->filter = std::make_shared<filter::JsonRpcProtocolFilter>(
        *bundle->adapter, dispatcher, is_server);

    // Note: use_framing is handled at transport layer, not JSON-RPC layer

    return bundle;
  }

  // Get next worker for load balancing (legacy API)
  WorkerContext* getNextWorker() {
    if (worker_contexts_.empty())
      return nullptr;
    static std::atomic<size_t> counter{0};
    size_t index = counter++ % worker_contexts_.size();
    return worker_contexts_[index].get();
  }

  // Message callbacks - can be overridden in derived classes if needed
  virtual void onRequest(const jsonrpc::Request& request) {}
  virtual void onNotification(const jsonrpc::Notification& notification) {}
  virtual void onResponse(const jsonrpc::Response& response) {}
  virtual void onError(const Error& error) { stats_.errors_total++; }

  /**
   * Worker thread for processing
   */
  class WorkerThread {
   public:
    WorkerThread(const std::string& name)
        : name_(name), running_(false), stopped_(true) {}

    ~WorkerThread() { stop(); }

    void start() {
      if (running_) {
        return;
      }

      running_ = true;
      stopped_ = false;
      thread_ = std::thread([this] { run(); });
    }

    void stop() {
      if (!running_) {
        return;
      }

      running_ = false;

      if (thread_.joinable()) {
        thread_.join();
      }

      stopped_ = true;
    }

    bool isStopped() const { return stopped_; }

    event::Dispatcher* getDispatcher() { return dispatcher_.get(); }

   private:
    void run() {
      std::cerr << "[INFO] Worker " << name_ << " starting" << std::endl;

      // Create dispatcher for this thread
      dispatcher_ = std::make_unique<event::LibeventDispatcher>(name_);

      // Run event loop
      while (running_) {
        dispatcher_->run(event::RunType::NonBlock);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }

      std::cerr << "[INFO] Worker " << name_ << " stopped" << std::endl;
    }

    std::string name_;
    std::atomic<bool> running_;
    std::atomic<bool> stopped_;
    std::thread thread_;
    std::unique_ptr<event::Dispatcher> dispatcher_;
  };

  // Configuration
  Config config_;

  // Application state
  std::atomic<bool> shutdown_requested_;
  std::atomic<bool> workers_started_;

  // Worker threads
  std::vector<std::unique_ptr<WorkerThread>> workers_;
  event::Dispatcher* main_dispatcher_ = nullptr;
  std::unique_ptr<event::Dispatcher>
      main_dispatcher_owned_;  // Owned dispatcher

  // Connection pool
  std::unique_ptr<ConnectionPool> connection_pool_;

  // Statistics
  ApplicationStats stats_;

  // Legacy API support members
  std::vector<std::unique_ptr<WorkerContext>> worker_contexts_;
  std::unique_ptr<network::SocketInterface> socket_interface_;

  // Legacy compatibility flags
  std::atomic<bool> initialized_{false};
  std::atomic<bool> running_{false};

  // Support old API methods
  void stop() { shutdown(); }
  void runEventLoop() { run(); }

  // Track failures for legacy API
  void trackFailure(const FailureReason& reason) { stats_.errors_total++; }
};

}  // namespace application
}  // namespace mcp

#endif  // MCP_APPLICATION_BASE_H
