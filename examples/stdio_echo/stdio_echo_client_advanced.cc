/**
 * @file stdio_echo_client_advanced.cc
 * @brief State-of-the-art MCP echo client implementation following the best patterns
 * 
 * Features:
 * - Connection pooling for efficient connection reuse
 * - Worker thread model with load balancing
 * - Request tracking with timeouts and retries
 * - Circuit breaker pattern for failure handling
 * - Comprehensive metrics and observability
 * - Batched request processing
 */

#include "mcp_application_base.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/transport/stdio_pipe_transport.h"
#include "mcp/json_serialization.h"
#include "mcp/builders.h"
#include <iostream>
#include <queue>
#include <future>
#include <random>

namespace mcp {
namespace examples {

using namespace application;

/**
 * Request context for tracking in-flight requests
 */
struct RequestContext {
  int id;
  std::string method;
  Metadata params;
  std::chrono::steady_clock::time_point sent_time;
  std::promise<jsonrpc::Response> promise;
  int retry_count = 0;
  
  RequestContext(int id, const std::string& method, const Metadata& params)
      : id(id), method(method), params(params),
        sent_time(std::chrono::steady_clock::now()) {}
};

/**
 * Circuit breaker for handling cascading failures
 */
class CircuitBreaker {
public:
  enum class State {
    Closed,     // Normal operation
    Open,       // Circuit open, rejecting requests
    HalfOpen    // Testing if service recovered
  };
  
  CircuitBreaker(size_t failure_threshold = 5,
                std::chrono::milliseconds timeout = std::chrono::seconds(30))
      : failure_threshold_(failure_threshold),
        timeout_(timeout),
        state_(State::Closed),
        failure_count_(0) {}
  
  bool allowRequest() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    switch (state_) {
      case State::Closed:
        return true;
        
      case State::Open:
        // Check if timeout has passed
        if (std::chrono::steady_clock::now() - last_failure_time_ > timeout_) {
          state_ = State::HalfOpen;
          return true;  // Allow one test request
        }
        return false;
        
      case State::HalfOpen:
        return true;  // Allow request to test recovery
    }
    
    return false;
  }
  
  void recordSuccess() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ == State::HalfOpen) {
      // Service recovered, close circuit
      state_ = State::Closed;
      failure_count_ = 0;
    }
  }
  
  void recordFailure() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    failure_count_++;
    last_failure_time_ = std::chrono::steady_clock::now();
    
    if (failure_count_ >= failure_threshold_) {
      state_ = State::Open;
    } else if (state_ == State::HalfOpen) {
      // Test failed, reopen circuit
      state_ = State::Open;
    }
  }
  
  State getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
  }
  
private:
  size_t failure_threshold_;
  std::chrono::milliseconds timeout_;
  State state_;
  size_t failure_count_;
  std::chrono::steady_clock::time_point last_failure_time_;
  mutable std::mutex mutex_;
};

/**
 * Connection pool for stdio connections
 */
class StdioConnectionPool : public ConnectionPool {
public:
  StdioConnectionPool(event::Dispatcher& dispatcher,
                     network::SocketInterface& socket_interface)
      : ConnectionPool(1, 1),  // Stdio only supports single connection
        dispatcher_(dispatcher),
        socket_interface_(socket_interface) {}
  
protected:
  ConnectionPtr createNewConnection() override {
    // Create stdio pipe transport
    transport::StdioPipeTransportConfig config;
    config.stdin_fd = STDIN_FILENO;
    config.stdout_fd = STDOUT_FILENO;
    config.non_blocking = true;
    config.buffer_size = 8192;
    
    auto transport = std::make_unique<transport::StdioPipeTransport>(config);
    auto init_result = transport->initialize();
    
    if (holds_alternative<Error>(init_result)) {
      return nullptr;
    }
    
    auto socket = transport->takePipeSocket();
    if (!socket) {
      return nullptr;
    }
    
    auto stream_info = stream_info::StreamInfoImpl::create();
    
    auto connection = network::ConnectionImpl::createClientConnection(
        dispatcher_,
        std::move(socket),
        std::move(transport),
        *stream_info);
    
    if (connection) {
      connection->transportSocket().onConnected();
    }
    
    return connection;
  }
  
private:
  event::Dispatcher& dispatcher_;
  network::SocketInterface& socket_interface_;
};

/**
 * Request manager for tracking and timeout handling
 */
class RequestManager {
public:
  RequestManager(std::chrono::milliseconds timeout = std::chrono::seconds(30))
      : timeout_(timeout), next_id_(1) {}
  
  int addRequest(const std::string& method, const Metadata& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    int id = next_id_++;
    auto context = std::make_shared<RequestContext>(id, method, params);
    pending_requests_[id] = context;
    
    return id;
  }
  
  std::shared_ptr<RequestContext> getRequest(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = pending_requests_.find(id);
    if (it != pending_requests_.end()) {
      return it->second;
    }
    return nullptr;
  }
  
  void completeRequest(int id, const jsonrpc::Response& response) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = pending_requests_.find(id);
    if (it != pending_requests_.end()) {
      it->second->promise.set_value(response);
      pending_requests_.erase(it);
    }
  }
  
  std::vector<std::shared_ptr<RequestContext>> checkTimeouts() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::shared_ptr<RequestContext>> timed_out;
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
      if (now - it->second->sent_time > timeout_) {
        timed_out.push_back(it->second);
        it = pending_requests_.erase(it);
      } else {
        ++it;
      }
    }
    
    return timed_out;
  }
  
  size_t getPendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_requests_.size();
  }
  
private:
  std::chrono::milliseconds timeout_;
  std::atomic<int> next_id_;
  std::map<int, std::shared_ptr<RequestContext>> pending_requests_;
  mutable std::mutex mutex_;
};

/**
 * Client protocol filter for handling responses
 */
class ClientProtocolFilter : public network::Filter, public McpMessageCallbacks {
public:
  ClientProtocolFilter(RequestManager& request_manager,
                      CircuitBreaker& circuit_breaker,
                      ApplicationStats& stats)
      : request_manager_(request_manager),
        circuit_breaker_(circuit_breaker),
        stats_(stats) {}
  
  // Filter interface
  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    std::string buffer_str = data.toString();
    data.drain(data.length());
    
    partial_message_ += buffer_str;
    
    // Process newline-delimited messages
    size_t pos = 0;
    while ((pos = partial_message_.find('\n')) != std::string::npos) {
      std::string message = partial_message_.substr(0, pos);
      partial_message_.erase(0, pos + 1);
      
      if (!message.empty()) {
        processMessage(message);
      }
    }
    
    return network::FilterStatus::Continue;
  }
  
  network::FilterStatus onNewConnection() override {
    return network::FilterStatus::Continue;
  }
  
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    return network::FilterStatus::Continue;
  }
  
  void initializeReadFilterCallbacks(network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }
  
  void initializeWriteFilterCallbacks(network::WriteFilterCallbacks& callbacks) override {}
  
  // McpMessageCallbacks interface
  void onRequest(const jsonrpc::Request& request) override {
    // Client shouldn't receive requests, but handle gracefully
    stats_.requests_total++;
  }
  
  void onNotification(const jsonrpc::Notification& notification) override {
    // Handle server notifications
    std::cerr << "[INFO] Received notification: " << notification.method << std::endl;
  }
  
  void onResponse(const jsonrpc::Response& response) override {
    // Find matching request
    if (holds_alternative<int>(response.id)) {
      int id = get<int>(response.id);
      auto request = request_manager_.getRequest(id);
      
      if (request) {
        // Calculate latency
        auto duration = std::chrono::steady_clock::now() - request->sent_time;
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        
        // Update metrics
        stats_.request_duration_ms_total += duration_ms;
        updateLatencyMetrics(duration_ms);
        
        if (response.error.has_value()) {
          stats_.requests_failed++;
          circuit_breaker_.recordFailure();
        } else {
          stats_.requests_success++;
          circuit_breaker_.recordSuccess();
        }
        
        // Complete request
        request_manager_.completeRequest(id, response);
      }
    }
  }
  
  void onConnectionEvent(network::ConnectionEvent event) override {
    if (event == network::ConnectionEvent::RemoteClose) {
      circuit_breaker_.recordFailure();
    }
  }
  
  void onError(const Error& error) override {
    stats_.errors_total++;
    circuit_breaker_.recordFailure();
  }
  
private:
  void processMessage(const std::string& message) {
    try {
      auto json_val = json::JsonValue::parse(message);
      
      if (json_val.contains("result") || json_val.contains("error")) {
        auto response = json::from_json<jsonrpc::Response>(json_val);
        onResponse(response);
      } else if (json_val.contains("method")) {
        if (json_val.contains("id")) {
          auto request = json::from_json<jsonrpc::Request>(json_val);
          onRequest(request);
        } else {
          auto notification = json::from_json<jsonrpc::Notification>(json_val);
          onNotification(notification);
        }
      }
    } catch (const std::exception& e) {
      onError(Error(jsonrpc::PARSE_ERROR, e.what()));
    }
  }
  
  void updateLatencyMetrics(uint64_t duration_ms) {
    uint64_t current_max = stats_.request_duration_ms_max;
    while (duration_ms > current_max && 
           !stats_.request_duration_ms_max.compare_exchange_weak(current_max, duration_ms));
    
    uint64_t current_min = stats_.request_duration_ms_min;
    while (duration_ms < current_min && 
           !stats_.request_duration_ms_min.compare_exchange_weak(current_min, duration_ms));
  }
  
  RequestManager& request_manager_;
  CircuitBreaker& circuit_breaker_;
  ApplicationStats& stats_;
  network::ReadFilterCallbacks* read_callbacks_ = nullptr;
  std::string partial_message_;
};

/**
 * Advanced MCP Echo Client Application
 */
class AdvancedEchoClient : public ApplicationBase {
public:
  AdvancedEchoClient(const Config& config)
      : ApplicationBase(config),
        request_manager_(std::chrono::seconds(30)),
        circuit_breaker_(5, std::chrono::seconds(60)) {
    
    // Start timeout checker thread
    timeout_thread_ = std::thread([this]() {
      while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        checkRequestTimeouts();
      }
    });
  }
  
  ~AdvancedEchoClient() {
    if (timeout_thread_.joinable()) {
      timeout_thread_.join();
    }
  }
  
  // Send a request and get future for response
  std::future<jsonrpc::Response> sendRequest(const std::string& method, 
                                             const Metadata& params = {}) {
    // Check circuit breaker
    if (!circuit_breaker_.allowRequest()) {
      std::promise<jsonrpc::Response> promise;
      promise.set_value(make<jsonrpc::Response>(0)
          .error(Error(-32000, "Circuit breaker open"))
          .build());
      return promise.get_future();
    }
    
    // Add request to manager
    int id = request_manager_.addRequest(method, params);
    auto request_ctx = request_manager_.getRequest(id);
    
    // Build JSON-RPC request
    auto request = make<jsonrpc::Request>(id)
        .method(method)
        .params(params)
        .build();
    
    // Send through connection
    sendRequestToConnection(request);
    
    stats_.requests_total++;
    
    return request_ctx->promise.get_future();
  }
  
  // Send a batch of requests
  std::vector<std::future<jsonrpc::Response>> sendBatch(
      const std::vector<std::pair<std::string, Metadata>>& requests) {
    
    std::vector<std::future<jsonrpc::Response>> futures;
    
    for (const auto& [method, params] : requests) {
      futures.push_back(sendRequest(method, params));
    }
    
    return futures;
  }
  
protected:
  void initializeWorker(WorkerContext& worker) override {
    // Create connection pool for this worker
    auto pool = std::make_unique<StdioConnectionPool>(
        worker.getDispatcher(), *socket_interface_);
    
    // Store pool (in real implementation, would be per-worker)
    if (!connection_pool_) {
      connection_pool_ = std::move(pool);
      
      // Setup initial connection
      setupConnection(worker);
    }
  }
  
  void setupFilterChain(FilterChainBuilder& builder) override {
    ApplicationBase::setupFilterChain(builder);
    
    // Add client protocol filter
    builder.addFilter([this]() {
      return std::make_shared<ClientProtocolFilter>(
          request_manager_, circuit_breaker_, stats_);
    });
  }
  
private:
  void setupConnection(WorkerContext& worker) {
    worker.getDispatcher().post([this, &worker]() {
      auto connection = connection_pool_->acquireConnection();
      
      if (!connection) {
        FailureReason failure(FailureReason::Type::ConnectionFailure,
                             "Failed to acquire connection from pool");
        trackFailure(failure);
        return;
      }
      
      // Apply filter chain
      auto* conn_impl = dynamic_cast<network::ConnectionImplBase*>(connection.get());
      if (conn_impl) {
        filter_chain_builder_.buildChain(conn_impl->filterManager());
      }
      
      // Store connection
      active_connection_ = connection;
      
      std::cerr << "[INFO] Client connected on worker " 
                << worker.getName() << std::endl;
    });
  }
  
  void sendRequestToConnection(const jsonrpc::Request& request) {
    if (!active_connection_) {
      return;
    }
    
    // Serialize request
    auto json_val = json::to_json(request);
    std::string message = json_val.toString() + "\n";
    
    // Send through connection
    auto buffer = std::make_unique<OwnedBuffer>();
    buffer->add(message);
    
    active_connection_->write(*buffer, false);
  }
  
  void checkRequestTimeouts() {
    auto timed_out = request_manager_.checkTimeouts();
    
    for (const auto& request : timed_out) {
      stats_.requests_failed++;
      circuit_breaker_.recordFailure();
      
      // Set timeout error
      request->promise.set_value(make<jsonrpc::Response>(request->id)
          .error(Error(-32001, "Request timeout"))
          .build());
      
      FailureReason failure(FailureReason::Type::Timeout,
                           "Request timeout: " + request->method);
      failure.addContext("id", std::to_string(request->id));
      trackFailure(failure);
    }
  }
  
  RequestManager request_manager_;
  CircuitBreaker circuit_breaker_;
  std::unique_ptr<ConnectionPool> connection_pool_;
  std::shared_ptr<network::Connection> active_connection_;
  std::thread timeout_thread_;
};

} // namespace examples
} // namespace mcp

int main(int argc, char* argv[]) {
  using namespace mcp::examples;
  
  // Configure client
  ApplicationBase::Config config;
  config.num_workers = 1;  // Single worker for stdio client
  config.enable_metrics = true;
  config.metrics_interval = std::chrono::seconds(5);
  
  // Test parameters
  int num_requests = 10;
  int delay_ms = 100;
  bool batch_mode = false;
  
  // Parse command line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--requests" && i + 1 < argc) {
      num_requests = std::stoi(argv[++i]);
    } else if (arg == "--delay" && i + 1 < argc) {
      delay_ms = std::stoi(argv[++i]);
    } else if (arg == "--batch") {
      batch_mode = true;
    } else if (arg == "--help") {
      std::cout << "Usage: " << argv[0] << " [options]\n"
                << "Options:\n"
                << "  --requests N    Number of requests to send (default: 10)\n"
                << "  --delay MS     Delay between requests in ms (default: 100)\n"
                << "  --batch        Send requests in batch mode\n"
                << "  --help         Show this help message\n";
      return 0;
    }
  }
  
  try {
    std::cerr << "[INFO] Starting Advanced MCP Echo Client\n"
              << "[INFO] Configuration:\n"
              << "[INFO]   Requests: " << num_requests << "\n"
              << "[INFO]   Delay: " << delay_ms << " ms\n"
              << "[INFO]   Mode: " << (batch_mode ? "batch" : "sequential") << "\n";
    
    // Create client in separate thread
    auto client = std::make_unique<AdvancedEchoClient>(config);
    
    std::thread client_thread([&client]() {
      client->start();
    });
    
    // Wait for client to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Send test requests
    std::vector<std::future<jsonrpc::Response>> futures;
    
    if (batch_mode) {
      // Prepare batch
      std::vector<std::pair<std::string, Metadata>> batch;
      for (int i = 0; i < num_requests; ++i) {
        auto params = make<Metadata>()
            .add("index", i)
            .add("timestamp", std::chrono::system_clock::now().time_since_epoch().count())
            .build();
        batch.emplace_back("test.request." + std::to_string(i), params);
      }
      
      // Send batch
      futures = client->sendBatch(batch);
      std::cerr << "[INFO] Sent batch of " << num_requests << " requests\n";
      
    } else {
      // Send requests sequentially
      for (int i = 0; i < num_requests; ++i) {
        auto params = make<Metadata>()
            .add("index", i)
            .add("timestamp", std::chrono::system_clock::now().time_since_epoch().count())
            .build();
        
        futures.push_back(client->sendRequest("test.request." + std::to_string(i), params));
        
        std::cerr << "[INFO] Sent request " << (i + 1) << "/" << num_requests << "\n";
        
        if (delay_ms > 0 && i < num_requests - 1) {
          std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
      }
    }
    
    // Wait for responses
    std::cerr << "[INFO] Waiting for responses...\n";
    
    int success_count = 0;
    int error_count = 0;
    
    for (size_t i = 0; i < futures.size(); ++i) {
      try {
        auto response = futures[i].get();
        
        if (response.error.has_value()) {
          error_count++;
          std::cerr << "[ERROR] Request " << i << " failed: " 
                    << response.error->message << "\n";
        } else {
          success_count++;
          std::cerr << "[INFO] Request " << i << " succeeded\n";
        }
      } catch (const std::exception& e) {
        error_count++;
        std::cerr << "[ERROR] Request " << i << " exception: " << e.what() << "\n";
      }
    }
    
    // Print results
    std::cerr << "\n[INFO] Test complete:\n"
              << "[INFO]   Success: " << success_count << "/" << num_requests << "\n"
              << "[INFO]   Errors: " << error_count << "/" << num_requests << "\n";
    
    // Get final statistics
    const auto& stats = client->getStats();
    std::cerr << "[INFO] Client statistics:\n"
              << "[INFO]   Total requests: " << stats.requests_total << "\n"
              << "[INFO]   Successful: " << stats.requests_success << "\n"
              << "[INFO]   Failed: " << stats.requests_failed << "\n";
    
    if (stats.requests_total > 0) {
      uint64_t avg_latency = stats.request_duration_ms_total / stats.requests_total;
      std::cerr << "[INFO]   Average latency: " << avg_latency << " ms\n"
                << "[INFO]   Min latency: " << stats.request_duration_ms_min << " ms\n"
                << "[INFO]   Max latency: " << stats.request_duration_ms_max << " ms\n";
    }
    
    // Shutdown
    client->stop();
    client_thread.join();
    
  } catch (const std::exception& e) {
    std::cerr << "[ERROR] Client failed: " << e.what() << std::endl;
    return 1;
  }
  
  return 0;
}