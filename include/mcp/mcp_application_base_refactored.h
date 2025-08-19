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
  FilterChainBuilder& withRateLimiting(const filter::RateLimitConfig& config) {
    auto callbacks = std::make_shared<RateLimitCallbacks>();
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
  FilterChainBuilder& withCircuitBreaker(const filter::CircuitBreakerConfig& config) {
    auto callbacks = std::make_shared<CircuitBreakerCallbacks>();
    auto filter = std::make_shared<filter::CircuitBreakerFilter>(*callbacks, config);
    filters_.push_back(filter);
    circuit_breaker_callbacks_ = callbacks;
    return *this;
  }

  // Add backpressure filter
  FilterChainBuilder& withBackpressure(const filter::BackpressureConfig& config) {
    auto callbacks = std::make_shared<BackpressureCallbacks>();
    auto filter = std::make_shared<filter::BackpressureFilter>(*callbacks, config);
    filters_.push_back(filter);
    backpressure_callbacks_ = callbacks;
    return *this;
  }

  // Add request validation filter
  FilterChainBuilder& withRequestValidation(const filter::RequestValidationConfig& config) {
    auto callbacks = std::make_shared<RequestValidationCallbacks>();
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
    void onRequestAllowed() override {
      // Request was allowed through
    }

    void onRequestLimited(std::chrono::milliseconds retry_after) override {
      std::cerr << "[RATE_LIMIT] Request rate limited, retry after: " 
                << retry_after.count() << "ms" << std::endl;
    }

    void onRateLimitWarning(int remaining) override {
      std::cerr << "[RATE_LIMIT] Warning: " << remaining 
                << "% capacity remaining" << std::endl;
    }
  };

  class CircuitBreakerCallbacks : public filter::CircuitBreakerFilter::Callbacks {
   public:
    void onStateChange(filter::CircuitState old_state,
                      filter::CircuitState new_state,
                      const std::string& reason) override {
      std::cerr << "[CIRCUIT_BREAKER] State change: " 
                << static_cast<int>(old_state) << " -> " 
                << static_cast<int>(new_state)
                << " (" << reason << ")" << std::endl;
    }

    void onRequestBlocked(const std::string& method) override {
      std::cerr << "[CIRCUIT_BREAKER] Request blocked: " << method << std::endl;
    }

    void onHealthUpdate(double success_rate, uint64_t latency_ms) override {
      // Could log or track health metrics
    }
  };

  class BackpressureCallbacks : public filter::BackpressureFilter::Callbacks {
   public:
    void onBackpressureApplied() override {
      std::cerr << "[BACKPRESSURE] Backpressure applied" << std::endl;
    }

    void onBackpressureReleased() override {
      std::cerr << "[BACKPRESSURE] Backpressure released" << std::endl;
    }

    void onDataDropped(size_t bytes) override {
      std::cerr << "[BACKPRESSURE] Data dropped: " << bytes << " bytes" 
                << std::endl;
    }
  };

  class RequestValidationCallbacks : public filter::RequestValidationFilter::ValidationCallbacks {
   public:
    void onRequestValidated(const std::string& method) override {
      // Request passed validation
    }

    void onRequestRejected(const std::string& method,
                          const std::string& reason) override {
      std::cerr << "[VALIDATION] Request validation failed for " << method 
                << ": " << reason << std::endl;
    }

    void onRateLimitExceeded(const std::string& method) override {
      std::cerr << "[VALIDATION] Rate limit exceeded for method: " << method
                << std::endl;
    }
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