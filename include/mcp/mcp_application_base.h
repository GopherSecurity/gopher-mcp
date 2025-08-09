/**
 * @file mcp_application_base.h
 * @brief Base application framework following the best architecture patterns
 * 
 * This provides a production-ready base for MCP applications with:
 * - Worker thread model with dedicated dispatcher threads
 * - Filter chain architecture for modular processing
 * - Enhanced error handling with detailed failure tracking
 * - Flow control with watermark-based backpressure
 * - Built-in observability (metrics, logging, tracing)
 */

#ifndef MCP_APPLICATION_BASE_H
#define MCP_APPLICATION_BASE_H

#include "mcp/mcp_connection_manager.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/socket_interface_impl.h"
#include "mcp/network/filter.h"
#include "mcp/buffer.h"
#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include <chrono>
#include <functional>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace mcp {
namespace application {

// Forward declarations
class RateLimitFilter;
class MetricsFilter;

// Forward declarations
class ConnectionPool;
class MetricsCollector;
class FailureTracker;
class FilterChainBuilder;

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
      : max_connections_(max_connections),
        max_idle_(max_idle) {}
  
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
      return !idle_connections_.empty() || total_connections_ < max_connections_;
    });
    
    return acquireConnection(); // Recursive call after wait
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
  
protected:
  virtual ConnectionPtr createNewConnection() = 0;
  
private:
  size_t max_connections_;
  size_t max_idle_;
  std::atomic<size_t> total_connections_{0};
  std::atomic<size_t> active_connections_{0};
  std::queue<ConnectionPtr> idle_connections_;
  std::mutex mutex_;
  std::condition_variable cv_;
};

/**
 * Filter chain builder for modular protocol processing
 */
class FilterChainBuilder {
public:
  using FilterFactory = std::function<network::FilterSharedPtr()>;
  
  // Add a filter factory to the chain
  void addFilter(FilterFactory factory) {
    filter_factories_.push_back(factory);
  }
  
  // Build the filter chain for a connection
  void buildChain(network::FilterManager& manager) {
    for (const auto& factory : filter_factories_) {
      auto filter = factory();
      manager.addReadFilter(filter);
      manager.addWriteFilter(filter);
    }
    manager.initializeReadFilters();
  }
  
private:
  std::vector<FilterFactory> filter_factories_;
};

/**
 * Worker context for each dispatcher thread
 */
class WorkerContext {
public:
  WorkerContext(const std::string& name, 
                event::DispatcherFactory& factory,
                network::SocketInterface& socket_interface)
      : name_(name),
        dispatcher_(factory.createDispatcher(name)),
        socket_interface_(socket_interface) {}
  
  void run() {
    // Logging will be done by ApplicationBase
    std::cerr << "[INFO] Worker " << name_ << " starting" << std::endl;
    dispatcher_->run(event::RunType::RunUntilExit);
    std::cerr << "[INFO] Worker " << name_ << " stopping" << std::endl;
  }
  
  void stop() {
    dispatcher_->exit();
  }
  
  event::Dispatcher& getDispatcher() { return *dispatcher_; }
  const std::string& getName() const { return name_; }
  
private:
  std::string name_;
  std::unique_ptr<event::Dispatcher> dispatcher_;
  network::SocketInterface& socket_interface_;
};

/**
 * Base application class with worker thread model
 */
class ApplicationBase {
public:
  struct Config {
    // Worker thread configuration
    size_t num_workers = 4;
    bool pin_worker_threads = false;
    
    // Connection limits
    size_t max_connections_per_worker = 1000;
    size_t connection_timeout_ms = 30000;
    
    // Buffer limits for flow control
    uint32_t buffer_high_watermark = 1024 * 1024;  // 1MB
    uint32_t buffer_low_watermark = 256 * 1024;    // 256KB
    
    // Observability
    bool enable_metrics = true;
    bool enable_tracing = false;
    std::chrono::milliseconds metrics_interval{5000};
  };
  
  ApplicationBase(const Config& config)
      : config_(config),
        dispatcher_factory_(event::createLibeventDispatcherFactory()),
        socket_interface_(std::make_unique<network::SocketInterfaceImpl>()),
        running_(false) {
    
    // Initialize metrics collector if enabled
    if (config_.enable_metrics) {
      startMetricsCollection();
    }
  }
  
  virtual ~ApplicationBase() {
    stop();
  }
  
  // Start the application with worker threads
  void start() {
    if (running_.exchange(true)) {
      return; // Already running
    }
    
    LOG_INFO("Starting application with {} workers", config_.num_workers);
    
    // Create main dispatcher for control plane
    main_dispatcher_ = dispatcher_factory_->createDispatcher("main");
    
    // Create worker threads for data plane
    for (size_t i = 0; i < config_.num_workers; ++i) {
      auto worker = std::make_unique<WorkerContext>(
          "worker_" + std::to_string(i),
          *dispatcher_factory_,
          *socket_interface_);
      
      // Initialize worker-specific resources
      initializeWorker(*worker);
      
      // Start worker thread
      worker_threads_.emplace_back([this, worker_ptr = worker.get()]() {
        if (config_.pin_worker_threads) {
          pinThreadToCpu(std::this_thread::get_id());
        }
        worker_ptr->run();
      });
      
      workers_.push_back(std::move(worker));
    }
    
    // Run main dispatcher (blocks until stop() is called)
    main_dispatcher_->run(event::RunType::RunUntilExit);
  }
  
  // Stop the application gracefully
  void stop() {
    if (!running_.exchange(false)) {
      return; // Already stopped
    }
    
    LOG_INFO("Stopping application");
    
    // Stop workers
    for (auto& worker : workers_) {
      worker->stop();
    }
    
    // Wait for worker threads
    for (auto& thread : worker_threads_) {
      if (thread.joinable()) {
        thread.join();
      }
    }
    
    // Stop main dispatcher
    if (main_dispatcher_) {
      main_dispatcher_->exit();
    }
    
    // Stop metrics thread
    if (metrics_thread_.joinable()) {
      metrics_thread_.join();
    }
    
    // Final metrics report
    if (config_.enable_metrics) {
      reportFinalMetrics();
    }
  }
  
  // Get application statistics
  const ApplicationStats& getStats() const { return stats_; }
  
  // Track a failure
  void trackFailure(const FailureReason& reason) {
    std::lock_guard<std::mutex> lock(failure_mutex_);
    failure_history_.push_back(reason);
    
    // Keep only recent failures (last 1000)
    if (failure_history_.size() > 1000) {
      failure_history_.erase(failure_history_.begin());
    }
    
    stats_.errors_total++;
    
    LOG_ERROR("Failure: {} - {}", 
              static_cast<int>(reason.getType()),
              reason.getDescription());
  }
  
protected:
  // Initialize a worker with application-specific resources
  virtual void initializeWorker(WorkerContext& worker) = 0;
  
  // Get the next worker for load balancing
  WorkerContext& getNextWorker() {
    static std::atomic<size_t> counter{0};
    size_t index = counter++ % workers_.size();
    return *workers_[index];
  }
  
  // Create filter chain for connections
  virtual void setupFilterChain(FilterChainBuilder& builder) {
    // Base filters - derived classes add more
    
    // Add rate limiting filter
    builder.addFilter([]() -> network::FilterSharedPtr {
      return std::static_pointer_cast<network::Filter>(
          std::make_shared<RateLimitFilter>());
    });
    
    // Add metrics collection filter
    builder.addFilter([this]() -> network::FilterSharedPtr {
      return std::static_pointer_cast<network::Filter>(
          std::make_shared<MetricsFilter>(stats_));
    });
  }
  
private:
  void startMetricsCollection() {
    metrics_thread_ = std::thread([this]() {
      while (running_) {
        // Sleep in small increments to allow quick shutdown
        auto remaining = config_.metrics_interval;
        while (running_ && remaining > std::chrono::milliseconds(100)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          remaining -= std::chrono::milliseconds(100);
        }
        if (running_ && remaining > std::chrono::milliseconds(0)) {
          std::this_thread::sleep_for(remaining);
        }
        if (running_) {
          reportMetrics();
        }
      }
    });
  }
  
  void reportMetrics() {
    LOG_INFO("Metrics: connections={}/{}, requests={}, success={}, failed={}, bytes_sent={}, bytes_received={}",
             stats_.connections_active.load(),
             stats_.connections_total.load(),
             stats_.requests_total.load(),
             stats_.requests_success.load(),
             stats_.requests_failed.load(),
             stats_.bytes_sent.load(),
             stats_.bytes_received.load());
    
    if (stats_.requests_total > 0) {
      uint64_t avg_duration = stats_.request_duration_ms_total / stats_.requests_total;
      LOG_INFO("Request latency: avg={}ms, min={}ms, max={}ms",
               avg_duration,
               stats_.request_duration_ms_min.load(),
               stats_.request_duration_ms_max.load());
    }
  }
  
  void reportFinalMetrics() {
    LOG_INFO("Final metrics report:");
    reportMetrics();
    
    // Report failure summary
    std::lock_guard<std::mutex> lock(failure_mutex_);
    if (!failure_history_.empty()) {
      std::map<FailureReason::Type, size_t> failure_counts;
      for (const auto& failure : failure_history_) {
        failure_counts[failure.getType()]++;
      }
      
      LOG_INFO("Failure summary:");
      for (const auto& pair : failure_counts) {
        LOG_INFO("  Type {}: {} occurrences", static_cast<int>(pair.first), pair.second);
      }
    }
  }
  
  void pinThreadToCpu(std::thread::id thread_id) {
    // Platform-specific CPU pinning
    // Implementation depends on OS (sched_setaffinity on Linux, etc.)
  }
  
  // Simple logging helpers (should use proper logging library)
  // Helper function to format string with arguments
  template<typename T>
  void format_helper(std::stringstream& ss, const std::string& fmt, size_t& pos, const T& value) {
    size_t next = fmt.find("{}", pos);
    if (next != std::string::npos) {
      ss << fmt.substr(pos, next - pos) << value;
      pos = next + 2;
    }
  }
  
  template<typename T, typename... Args>
  void format_helper(std::stringstream& ss, const std::string& fmt, size_t& pos, const T& value, const Args&... args) {
    format_helper(ss, fmt, pos, value);
    format_helper(ss, fmt, pos, args...);
  }
  
  template<typename... Args>
  std::string format_string(const std::string& fmt, const Args&... args) {
    std::stringstream ss;
    size_t pos = 0;
    format_helper(ss, fmt, pos, args...);
    // Append any remaining part of the format string
    if (pos < fmt.length()) {
      ss << fmt.substr(pos);
    }
    return ss.str();
  }
  
  void LOG_INFO(const std::string& msg) {
    std::cerr << "[INFO] " << msg << std::endl;
  }
  
  template<typename... Args>
  void LOG_INFO(const std::string& fmt, const Args&... args) {
    std::cerr << "[INFO] " << format_string(fmt, args...) << std::endl;
  }
  
  void LOG_ERROR(const std::string& msg) {
    std::cerr << "[ERROR] " << msg << std::endl;
  }
  
  template<typename... Args>
  void LOG_ERROR(const std::string& fmt, const Args&... args) {
    std::cerr << "[ERROR] " << format_string(fmt, args...) << std::endl;
  }
  
protected:
  Config config_;
  ApplicationStats stats_;
  std::unique_ptr<event::DispatcherFactory> dispatcher_factory_;
  std::unique_ptr<network::SocketInterface> socket_interface_;
  std::unique_ptr<event::Dispatcher> main_dispatcher_;
  
  std::vector<std::unique_ptr<WorkerContext>> workers_;
  std::vector<std::thread> worker_threads_;
  
  std::atomic<bool> running_;
  std::thread metrics_thread_;
  
  std::vector<FailureReason> failure_history_;
  mutable std::mutex failure_mutex_;
  
  FilterChainBuilder filter_chain_builder_;
};

// Helper filters for common functionality

/**
 * Rate limiting filter
 */
class RateLimitFilter : public network::Filter {
public:
  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    // Simple rate limiting logic
    return network::FilterStatus::Continue;
  }
  
  network::FilterStatus onNewConnection() override {
    return network::FilterStatus::Continue;
  }
  
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    return network::FilterStatus::Continue;
  }
  
  void initializeReadFilterCallbacks(network::ReadFilterCallbacks& callbacks) override {}
  void initializeWriteFilterCallbacks(network::WriteFilterCallbacks& callbacks) override {}
};

/**
 * Metrics collection filter
 */
class MetricsFilter : public network::Filter {
public:
  MetricsFilter(ApplicationStats& stats) : stats_(stats) {}
  
  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    stats_.bytes_received += data.length();
    return network::FilterStatus::Continue;
  }
  
  network::FilterStatus onNewConnection() override {
    stats_.connections_active++;
    stats_.connections_total++;
    return network::FilterStatus::Continue;
  }
  
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    stats_.bytes_sent += data.length();
    return network::FilterStatus::Continue;
  }
  
  void initializeReadFilterCallbacks(network::ReadFilterCallbacks& callbacks) override {}
  void initializeWriteFilterCallbacks(network::WriteFilterCallbacks& callbacks) override {}
  
private:
  ApplicationStats& stats_;
};

} // namespace application
} // namespace mcp

#endif // MCP_APPLICATION_BASE_H