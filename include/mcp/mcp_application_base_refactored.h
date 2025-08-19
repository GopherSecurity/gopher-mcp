/**
 * @file mcp_application_base_refactored.h
 * @brief Refactored base application framework using external filter implementations
 *
 * This provides a production-ready base for MCP applications with:
 * - Worker thread model with dedicated dispatcher threads
 * - Filter chain architecture using external filter implementations
 * - Enhanced error handling with detailed failure tracking
 * - Flow control with watermark-based backpressure
 * - Built-in observability (metrics, logging, tracing)
 */

#ifndef MCP_APPLICATION_BASE_REFACTORED_H
#define MCP_APPLICATION_BASE_REFACTORED_H

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
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/mcp_jsonrpc_filter.h"
#include "mcp/filter/rate_limit_filter.h"
#include "mcp/filter/metrics_filter.h"
#include "mcp/filter/backpressure_filter.h"
#include "mcp/filter/circuit_breaker_filter.h"
#include "mcp/filter/request_validation_filter.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/filter.h"
#include "mcp/network/socket_interface_impl.h"
#include "mcp/builders.h"

namespace mcp {
namespace application {

// Forward declarations
class ConnectionPool;
class FailureTracker;
class FilterChainBuilder;

/**
 * Common adapter from McpMessageCallbacks to McpJsonRpcFilter::Callbacks
 *
 * This adapter bridges the MCP message callback interface to the JSON-RPC
 * filter callback interface, allowing ApplicationBase-derived classes to
 * use the unified McpJsonRpcFilter.
 */
class McpJsonRpcCallbackAdapter : public filter::McpJsonRpcFilter::Callbacks {
 public:
  explicit McpJsonRpcCallbackAdapter(McpMessageCallbacks& callbacks)
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
  McpMessageCallbacks& callbacks_;
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
class ApplicationMetricsCallbacks : public filter::MetricsFilter::MetricsCallbacks {
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
 * Connection pool for efficient connection reuse
 */
class ConnectionPool {
 public:
  using ConnectionPtr = std::shared_ptr<network::Connection>;

  virtual ~ConnectionPool() = default;

  ConnectionPool(size_t max_connections, size_t max_idle)
      : max_connections_(max_connections), max_idle_(max_idle) {}

  // Acquire a connection from the pool
  ConnectionPtr acquireConnection() {
    std::unique_lock<std::mutex> lock(mutex_);

    // Try to get an idle connection
    while (!idle_connections_.empty()) {
      auto conn = idle_connections_.front();
      idle_connections_.pop();

      if (conn && conn->state() == network::ConnectionState::Open) {
        active_connections_++;
        return conn;
      }
    }

    // Create new connection if under limit
    if (total_connections_ < max_connections_) {
      auto conn = createNewConnection();
      if (conn) {
        total_connections_++;
        active_connections_++;
        return conn;
      }
    }

    // Wait for a connection to become available
    cv_.wait(lock, [this] {
      return !idle_connections_.empty() ||
             total_connections_ < max_connections_;
    });

    return acquireConnection();  // Recursive call after wait
  }

  // Return a connection to the pool
  void releaseConnection(ConnectionPtr conn) {
    std::unique_lock<std::mutex> lock(mutex_);

    active_connections_--;

    if (!conn || conn->state() != network::ConnectionState::Open) {
      // Connection is closed, reduce total count
      total_connections_--;
    } else if (idle_connections_.size() < max_idle_) {
      // Add to idle pool
      idle_connections_.push(conn);
    } else {
      // Pool is full, close connection
      conn->close(network::ConnectionCloseType::NoFlush);
      total_connections_--;
    }

    cv_.notify_one();
  }

  size_t getActiveConnections() const { return active_connections_; }
  size_t getTotalConnections() const { return total_connections_; }
  size_t getIdleConnections() const { return idle_connections_.size(); }

 protected:
  virtual ConnectionPtr createNewConnection() = 0;

 private:
  size_t max_connections_;
  size_t max_idle_;
  size_t total_connections_ = 0;
  size_t active_connections_ = 0;

  std::queue<ConnectionPtr> idle_connections_;
  std::mutex mutex_;
  std::condition_variable cv_;
};

/**
 * Filter chain builder for constructing processing pipelines
 */
class FilterChainBuilder {
 public:
  using FilterPtr = std::shared_ptr<network::Filter>;

  FilterChainBuilder(event::Dispatcher& dispatcher,
                    ApplicationStats& stats)
      : dispatcher_(dispatcher), stats_(stats) {}

  // Add rate limiting filter
  FilterChainBuilder& withRateLimiting(const filter::RateLimitConfig& config,
                                       network::Connection* connection = nullptr) {
    auto callbacks = std::make_shared<RateLimitCallbacks>(stats_, connection);
    auto filter = std::make_shared<filter::RateLimitFilter>(
        *callbacks, config);
    filters_.push_back(filter);
    rate_limit_callbacks_ = callbacks;
    return *this;
  }

  // Add metrics collection filter  
  FilterChainBuilder& withMetrics(const filter::MetricsFilter::Config& config) {
    auto callbacks = std::make_shared<ApplicationMetricsCallbacks>(stats_);
    auto filter = std::make_shared<filter::MetricsFilter>(*callbacks, config);
    filters_.push_back(filter);
    metrics_filter_ = filter;
    return *this;
  }

  // Add circuit breaker filter
  FilterChainBuilder& withCircuitBreaker(const filter::CircuitBreakerConfig& config,
                                         network::Connection* connection = nullptr) {
    auto callbacks = std::make_shared<CircuitBreakerCallbacks>(stats_, connection);
    auto filter = std::make_shared<filter::CircuitBreakerFilter>(*callbacks, config);
    filters_.push_back(filter);
    circuit_breaker_callbacks_ = callbacks;
    return *this;
  }

  // Add backpressure filter
  FilterChainBuilder& withBackpressure(const filter::BackpressureConfig& config,
                                       network::Connection* connection = nullptr) {
    auto callbacks = std::make_shared<BackpressureCallbacks>(stats_, connection);
    auto filter = std::make_shared<filter::BackpressureFilter>(*callbacks, config);
    filters_.push_back(filter);
    backpressure_callbacks_ = callbacks;
    return *this;
  }

  // Add request validation filter
  FilterChainBuilder& withRequestValidation(const filter::RequestValidationConfig& config,
                                           network::Connection* connection = nullptr) {
    auto callbacks = std::make_shared<RequestValidationCallbacks>(stats_, connection);
    auto filter = std::make_shared<filter::RequestValidationFilter>(*callbacks, config);
    filters_.push_back(filter);
    validation_callbacks_ = callbacks;
    return *this;
  }

  // Build the filter chain
  std::vector<FilterPtr> build() {
    return filters_;
  }

  // Get metrics filter for external access
  std::shared_ptr<filter::MetricsFilter> getMetricsFilter() const {
    return metrics_filter_;
  }

 private:
  // Callback implementations
  class RateLimitCallbacks : public filter::RateLimitFilter::Callbacks {
   public:
    RateLimitCallbacks(ApplicationStats& stats, network::Connection* connection = nullptr)
        : stats_(stats), connection_(connection) {}

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
        auto error = make<Error>(-32429, "Rate limit exceeded")
            .data(errorData);
        
        auto response = make<jsonrpc::Response>(last_request_id_)
            .error(error);
        
        // TODO: Send error response through connection
        // This would typically be: connection_->sendResponse(response.build());
      }
    }

    void onRateLimitWarning(int remaining) override {
      // Could trigger alerts or adjust behavior when approaching limit
      if (remaining < 10) {
        // Critical warning - might want to start queuing or buffering
        stats_.errors_total++;
      }
    }
    
    void setLastRequestId(const RequestId& id) {
      last_request_id_ = id;
    }

   private:
    ApplicationStats& stats_;
    network::Connection* connection_;
    RequestId last_request_id_;
  };

  class CircuitBreakerCallbacks : public filter::CircuitBreakerFilter::Callbacks {
   public:
    CircuitBreakerCallbacks(ApplicationStats& stats, network::Connection* connection = nullptr)
        : stats_(stats), connection_(connection) {}

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
        errorData["retry_after_ms"] = "5000"; // Suggest retry after 5 seconds
        
        // Use builders to create error response
        auto error = make<Error>(-32503, "Service temporarily unavailable")
            .data(errorData);
        
        auto response = make<jsonrpc::Response>(last_request_id_)
            .error(error);
        
        // TODO: Send error response through connection
        // This would typically be: connection_->sendResponse(response.build());
      }
    }

    void onHealthUpdate(double success_rate, uint64_t latency_ms) override {
      // Track health metrics for monitoring/alerting
      if (success_rate < 0.5) { // Less than 50% success rate
        // Could trigger alerts or start pre-emptive actions
        stats_.errors_total++;
      }
      
      // Update latency tracking
      if (latency_ms > stats_.request_duration_ms_max.load()) {
        stats_.request_duration_ms_max.store(latency_ms);
      }
    }
    
    void setLastRequestId(const RequestId& id) {
      last_request_id_ = id;
    }
    
    bool isCircuitOpen() const { return is_circuit_open_; }

   private:
    ApplicationStats& stats_;
    network::Connection* connection_;
    RequestId last_request_id_;
    std::atomic<bool> is_circuit_open_{false};
  };

  class BackpressureCallbacks : public filter::BackpressureFilter::Callbacks {
   public:
    BackpressureCallbacks(ApplicationStats& stats, network::Connection* connection = nullptr)
        : stats_(stats), connection_(connection) {}

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
        
        auto notification = make<jsonrpc::Notification>("backpressure.data_dropped")
            .params(params);
        
        // TODO: Send notification through connection
        // This would typically be: connection_->sendNotification(notification);
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
    std::atomic<bool> is_backpressure_active_{false};
    std::atomic<size_t> dropped_bytes_{0};
  };

  class RequestValidationCallbacks : public filter::RequestValidationFilter::ValidationCallbacks {
   public:
    RequestValidationCallbacks(ApplicationStats& stats, network::Connection* connection = nullptr)
        : stats_(stats), connection_(connection) {}

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
        auto error = make<Error>(-32602, "Request validation failed")
            .data(errorData);
        
        auto response = make<jsonrpc::Response>(last_request_id_)
            .error(error);
        
        // TODO: Send error response through connection
        // This would typically be: connection_->sendResponse(response.build());
        
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
        auto error = make<Error>(-32429, "Method rate limit exceeded")
            .data(errorData);
        
        auto response = make<jsonrpc::Response>(last_request_id_)
            .error(error);
        
        // TODO: Send error response through connection
        // This would typically be: connection_->sendResponse(response.build());
      }
    }
    
    void setLastRequestId(const RequestId& id) {
      last_request_id_ = id;
    }
    
    size_t getValidatedRequests() const { return validated_requests_; }
    size_t getRejectedRequests() const { return rejected_requests_; }
    size_t getSecurityViolations() const { return security_violations_; }

   private:
    ApplicationStats& stats_;
    network::Connection* connection_;
    RequestId last_request_id_;
    std::atomic<size_t> validated_requests_{0};
    std::atomic<size_t> rejected_requests_{0};
    std::atomic<size_t> security_violations_{0};
    std::atomic<size_t> rate_limit_violations_{0};
  };

  event::Dispatcher& dispatcher_;
  ApplicationStats& stats_;
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
class ApplicationBase : public McpMessageCallbacks {
 public:
  /**
   * Application configuration
   */
  struct Config {
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
      : config_(config),
        shutdown_requested_(false),
        workers_started_(false) {
    std::cerr << "[INFO] Initializing application: " << config_.name 
              << std::endl;
  }

  virtual ~ApplicationBase() {
    shutdown();
  }

  /**
   * Initialize the application
   */
  virtual bool initialize() {
    std::cerr << "[INFO] Initializing application with " 
              << config_.worker_threads << " workers" << std::endl;

    // Create worker threads
    for (size_t i = 0; i < config_.worker_threads; ++i) {
      auto worker = std::make_unique<WorkerThread>("worker_" + std::to_string(i));
      workers_.push_back(std::move(worker));
    }

    // Initialize connection pool
    connection_pool_ = createConnectionPool();

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
    for (auto& worker : workers_) {
      worker->start();
    }

    workers_started_ = true;

    // Application-specific startup
    return onStart();
  }

  /**
   * Run the main event loop
   */
  virtual void run() {
    std::cerr << "[INFO] Running main event loop" << std::endl;

    // Create main dispatcher
    auto dispatcher = std::make_unique<event::LibeventDispatcher>("main");
    main_dispatcher_ = dispatcher.get();

    // Run until shutdown requested
    while (!shutdown_requested_) {
      dispatcher->run(event::RunType::NonBlock);
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

    // Application-specific shutdown
    onShutdown();

    // Stop worker threads
    for (auto& worker : workers_) {
      worker->stop();
    }

    // Wait for workers with timeout
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < config_.shutdown_timeout) {
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

    std::cerr << "[INFO] Application shutdown complete" << std::endl;
  }

  /**
   * Get application statistics
   */
  const ApplicationStats& getStats() const { return stats_; }

  /**
   * Create filter chain for connection
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

  // McpMessageCallbacks - override in derived classes
  void onRequest(const jsonrpc::Request& request) override {}
  void onNotification(const jsonrpc::Notification& notification) override {}
  void onResponse(const jsonrpc::Response& response) override {}
  void onError(const Error& error) override {
    stats_.errors_total++;
  }

  /**
   * Worker thread for processing
   */
  class WorkerThread {
   public:
    WorkerThread(const std::string& name)
        : name_(name), running_(false), stopped_(true) {}

    ~WorkerThread() {
      stop();
    }

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

  // Connection pool
  std::unique_ptr<ConnectionPool> connection_pool_;

  // Statistics
  ApplicationStats stats_;
};

}  // namespace application
}  // namespace mcp

#endif  // MCP_APPLICATION_BASE_REFACTORED_H