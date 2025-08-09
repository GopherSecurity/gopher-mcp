/**
 * @file stdio_echo_server_advanced.cc
 * @brief State-of-the-art MCP echo server implementation following the best patterns
 * 
 * Features:
 * - Worker thread model with dedicated dispatcher threads
 * - Filter chain architecture for modular message processing
 * - Enhanced error handling with detailed failure tracking
 * - Flow control with watermark-based backpressure
 * - Built-in observability (metrics, logging, tracing)
 * - Graceful shutdown and resource cleanup
 */

#include "mcp/mcp_application_base.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/transport/stdio_pipe_transport.h"
#include "mcp/network/connection_impl.h"
#include "mcp/stream_info/stream_info_impl.h"
#include "mcp/json/json_serialization.h"
#include "mcp/builders.h"
#include <iostream>
#include <csignal>
#include <atomic>

namespace mcp {
namespace examples {

using namespace application;

/**
 * MCP protocol filter for processing JSON-RPC messages
 */
class McpProtocolFilter : public network::Filter, public McpMessageCallbacks {
public:
  McpProtocolFilter(ApplicationStats& stats, 
                    std::function<void(const FailureReason&)> failure_callback)
      : stats_(stats), 
        failure_callback_(failure_callback),
        request_start_time_{} {}
  
  // Filter interface
  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    // Parse JSON-RPC messages from buffer
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
    stats_.connections_active++;
    return network::FilterStatus::Continue;
  }
  
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    // Add framing for outgoing messages if needed
    return network::FilterStatus::Continue;
  }
  
  void initializeReadFilterCallbacks(network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }
  
  void initializeWriteFilterCallbacks(network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }
  
  // McpMessageCallbacks interface
  void onRequest(const jsonrpc::Request& request) override {
    request_start_time_ = std::chrono::steady_clock::now();
    stats_.requests_total++;
    
    try {
      // Create echo response
      auto response = mcp::make<mcp::jsonrpc::Response>(request.id)
          .result(jsonrpc::ResponseResult(make<Metadata>()
              .add("echo", true)
              .add("method", request.method)
              .add("timestamp", std::chrono::system_clock::now().time_since_epoch().count())
              .build()))
          .build();
      
      sendResponse(response);
      
      // Track metrics
      auto duration = std::chrono::steady_clock::now() - request_start_time_;
      auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
      updateLatencyMetrics(duration_ms);
      
      stats_.requests_success++;
      
    } catch (const std::exception& e) {
      stats_.requests_failed++;
      
      FailureReason failure(FailureReason::Type::ProtocolError,
                           std::string("Failed to process request: ") + e.what());
      failure.addContext("method", request.method);
      failure_callback_(failure);
      
      // Send error response
      auto error_response = mcp::make<mcp::jsonrpc::Response>(request.id)
          .error(mcp::Error(mcp::jsonrpc::INTERNAL_ERROR, e.what()))
          .build();
      
      sendResponse(error_response);
    }
  }
  
  void onNotification(const jsonrpc::Notification& notification) override {
    // Handle special notifications
    if (notification.method == "shutdown") {
      // Initiate graceful shutdown
      if (read_callbacks_) {
        read_callbacks_->connection().close(network::ConnectionCloseType::FlushWrite);
      }
      return;
    }
    
    // Echo notification back with prefix
    auto echo = mcp::make<mcp::jsonrpc::Notification>("echo/" + notification.method)
        .params(mcp::make<mcp::Metadata>()
            .add("original_method", notification.method)
            .add("timestamp", std::chrono::system_clock::now().time_since_epoch().count())
            .build())
        .build();
    
    sendNotification(echo);
  }
  
  void onResponse(const jsonrpc::Response& response) override {
    // Server shouldn't receive responses, log anomaly
    FailureReason failure(FailureReason::Type::ProtocolError,
                         "Server received unexpected response");
    failure_callback_(failure);
  }
  
  void onConnectionEvent(network::ConnectionEvent event) override {
    if (event == network::ConnectionEvent::RemoteClose ||
        event == network::ConnectionEvent::LocalClose) {
      stats_.connections_active--;
    }
  }
  
  void onError(const Error& error) override {
    FailureReason failure(FailureReason::Type::ProtocolError,
                         error.message);
    failure.addContext("code", std::to_string(error.code));
    failure_callback_(failure);
  }
  
private:
  void processMessage(const std::string& message) {
    try {
      auto json_val = json::JsonValue::parse(message);
      
      // Determine message type and dispatch
      if (json_val.contains("method")) {
        if (json_val.contains("id")) {
          auto request = json::from_json<jsonrpc::Request>(json_val);
          onRequest(request);
        } else {
          auto notification = json::from_json<jsonrpc::Notification>(json_val);
          onNotification(notification);
        }
      } else if (json_val.contains("result") || json_val.contains("error")) {
        auto response = json::from_json<jsonrpc::Response>(json_val);
        onResponse(response);
      }
    } catch (const std::exception& e) {
      onError(Error(jsonrpc::PARSE_ERROR, e.what()));
    }
  }
  
  void sendResponse(const jsonrpc::Response& response) {
    auto json_val = json::to_json(response);
    std::string message = json_val.toString() + "\n";
    
    auto buffer = std::make_unique<OwnedBuffer>();
    buffer->add(message);
    
    if (read_callbacks_) {
      read_callbacks_->connection().write(*buffer, false);
    }
  }
  
  void sendNotification(const jsonrpc::Notification& notification) {
    auto json_val = json::to_json(notification);
    std::string message = json_val.toString() + "\n";
    
    auto buffer = std::make_unique<OwnedBuffer>();
    buffer->add(message);
    
    if (read_callbacks_) {
      read_callbacks_->connection().write(*buffer, false);
    }
  }
  
  void updateLatencyMetrics(uint64_t duration_ms) {
    stats_.request_duration_ms_total += duration_ms;
    
    uint64_t current_max = stats_.request_duration_ms_max;
    while (duration_ms > current_max && 
           !stats_.request_duration_ms_max.compare_exchange_weak(current_max, duration_ms));
    
    uint64_t current_min = stats_.request_duration_ms_min;
    while (duration_ms < current_min && 
           !stats_.request_duration_ms_min.compare_exchange_weak(current_min, duration_ms));
  }
  
  ApplicationStats& stats_;
  std::function<void(const FailureReason&)> failure_callback_;
  network::ReadFilterCallbacks* read_callbacks_ = nullptr;
  network::WriteFilterCallbacks* write_callbacks_ = nullptr;
  std::string partial_message_;
  std::chrono::steady_clock::time_point request_start_time_;
};

/**
 * Flow control filter implementing watermark-based backpressure
 */
class FlowControlFilter : public network::Filter {
public:
  FlowControlFilter(uint32_t high_watermark, uint32_t low_watermark)
      : high_watermark_(high_watermark),
        low_watermark_(low_watermark),
        buffer_size_(0),
        above_watermark_(false) {}
  
  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    buffer_size_ += data.length();
    
    // Check if we exceeded high watermark
    if (!above_watermark_ && buffer_size_ > high_watermark_) {
      above_watermark_ = true;
      if (read_callbacks_) {
        // Disable reading from socket
        read_callbacks_->connection().readDisable(true);
      }
    }
    
    return network::FilterStatus::Continue;
  }
  
  network::FilterStatus onNewConnection() override {
    return network::FilterStatus::Continue;
  }
  
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    // Data is being written out, reduce buffer size
    size_t written = data.length();
    if (written <= buffer_size_) {
      buffer_size_ -= written;
    } else {
      buffer_size_ = 0;
    }
    
    // Check if we dropped below low watermark
    if (above_watermark_ && buffer_size_ < low_watermark_) {
      above_watermark_ = false;
      if (read_callbacks_) {
        // Re-enable reading from socket
        read_callbacks_->connection().readDisable(false);
      }
    }
    
    return network::FilterStatus::Continue;
  }
  
  void initializeReadFilterCallbacks(network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }
  
  void initializeWriteFilterCallbacks(network::WriteFilterCallbacks& callbacks) override {}
  
private:
  uint32_t high_watermark_;
  uint32_t low_watermark_;
  std::atomic<size_t> buffer_size_;
  std::atomic<bool> above_watermark_;
  network::ReadFilterCallbacks* read_callbacks_ = nullptr;
};

/**
 * Advanced MCP Echo Server Application
 */
class AdvancedEchoServer : public ApplicationBase {
public:
  AdvancedEchoServer(const Config& config) 
      : ApplicationBase(config),
        stdio_transport_created_(false) {}
  
protected:
  void initializeWorker(WorkerContext& worker) override {
    // Each worker gets its own connection manager
    // For stdio, only the first worker handles it
    if (!stdio_transport_created_.exchange(true)) {
      setupStdioConnection(worker);
    }
  }
  
  void setupFilterChain(FilterChainBuilder& builder) override {
    // Add base filters
    ApplicationBase::setupFilterChain(builder);
    
    // Add flow control filter
    builder.addFilter([this]() {
      return std::make_shared<FlowControlFilter>(
          config_.buffer_high_watermark,
          config_.buffer_low_watermark);
    });
    
    // Add MCP protocol filter
    builder.addFilter([this]() {
      return std::make_shared<McpProtocolFilter>(
          stats_,
          [this](const FailureReason& failure) { trackFailure(failure); });
    });
  }
  
private:
  void setupStdioConnection(WorkerContext& worker) {
    // Create stdio pipe transport configuration
    transport::StdioPipeTransportConfig transport_config;
    transport_config.stdin_fd = STDIN_FILENO;
    transport_config.stdout_fd = STDOUT_FILENO;
    transport_config.non_blocking = true;
    transport_config.buffer_size = 8192;
    
    // Create and initialize transport
    auto transport = std::make_unique<transport::StdioPipeTransport>(transport_config);
    auto init_result = transport->initialize();
    
    if (holds_alternative<Error>(init_result)) {
      auto& error = get<Error>(init_result);
      FailureReason failure(FailureReason::Type::ConnectionFailure,
                           "Failed to initialize stdio transport: " + error.message);
      trackFailure(failure);
      return;
    }
    
    // Get the pipe socket
    auto socket = transport->takePipeSocket();
    if (!socket) {
      FailureReason failure(FailureReason::Type::ConnectionFailure,
                           "Failed to get pipe socket from transport");
      trackFailure(failure);
      return;
    }
    
    // Create stream info
    auto stream_info = stream_info::StreamInfoImpl::create();
    
    // Wrap unique_ptrs in shared_ptrs for lambda capture (C++14 compatibility)
    auto socket_ptr = std::make_shared<std::unique_ptr<network::ConnectionSocketImpl>>(std::move(socket));
    auto transport_ptr = std::make_shared<std::unique_ptr<transport::StdioPipeTransport>>(std::move(transport));
    
    // Post connection creation to worker's dispatcher
    worker.getDispatcher().post([this, &worker, socket_ptr, transport_ptr, stream_info]() mutable {
      // Create connection in dispatcher thread
      auto connection = network::ConnectionImpl::createServerConnection(
          worker.getDispatcher(),
          std::move(*socket_ptr),
          std::move(*transport_ptr),
          *stream_info);
      
      if (!connection) {
        FailureReason failure(FailureReason::Type::ConnectionFailure,
                             "Failed to create server connection");
        trackFailure(failure);
        return;
      }
      
      // Apply filter chain
      auto* conn_impl = dynamic_cast<network::ConnectionImplBase*>(connection.get());
      if (conn_impl) {
        filter_chain_builder_.buildChain(conn_impl->filterManager());
      }
      
      // Store connection
      stdio_connection_ = std::move(connection);
      
      // Notify transport of connection
      stdio_connection_->transportSocket().onConnected();
      
      std::cerr << "[INFO] Stdio echo server ready on worker " 
                << worker.getName() << std::endl;
    });
  }
  
  std::atomic<bool> stdio_transport_created_;
  std::shared_ptr<network::Connection> stdio_connection_;
};

// Global server instance for signal handling
std::unique_ptr<AdvancedEchoServer> g_server;

void signalHandler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    std::cerr << "\n[INFO] Received signal " << signal 
              << ", initiating graceful shutdown..." << std::endl;
    if (g_server) {
      g_server->stop();
    }
  }
}

} // namespace examples
} // namespace mcp

int main(int argc, char* argv[]) {
  using namespace mcp::examples;
  
  // Configure server
  ApplicationBase::Config config;
  config.num_workers = 2;  // Use 2 worker threads for stdio
  config.max_connections_per_worker = 10;
  config.buffer_high_watermark = 1024 * 1024;  // 1MB
  config.buffer_low_watermark = 256 * 1024;    // 256KB
  config.enable_metrics = true;
  config.metrics_interval = std::chrono::seconds(10);
  
  // Parse command line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--workers" && i + 1 < argc) {
      config.num_workers = std::stoul(argv[++i]);
    } else if (arg == "--metrics-interval" && i + 1 < argc) {
      config.metrics_interval = std::chrono::seconds(std::stoul(argv[++i]));
    } else if (arg == "--no-metrics") {
      config.enable_metrics = false;
    } else if (arg == "--help") {
      std::cout << "Usage: " << argv[0] << " [options]\n"
                << "Options:\n"
                << "  --workers N           Number of worker threads (default: 2)\n"
                << "  --metrics-interval S  Metrics reporting interval in seconds (default: 10)\n"
                << "  --no-metrics         Disable metrics collection\n"
                << "  --help              Show this help message\n";
      return 0;
    }
  }
  
  // Setup signal handlers
  signal(SIGINT, mcp::examples::signalHandler);
  signal(SIGTERM, mcp::examples::signalHandler);
  signal(SIGPIPE, SIG_IGN);  // Ignore broken pipe
  
  try {
    // Create and start server
    std::cerr << "[INFO] Starting Advanced MCP Echo Server\n"
              << "[INFO] Configuration:\n"
              << "[INFO]   Workers: " << config.num_workers << "\n"
              << "[INFO]   Buffer high watermark: " << config.buffer_high_watermark << " bytes\n"
              << "[INFO]   Buffer low watermark: " << config.buffer_low_watermark << " bytes\n"
              << "[INFO]   Metrics: " << (config.enable_metrics ? "enabled" : "disabled") << "\n";
    
    mcp::examples::g_server = std::make_unique<AdvancedEchoServer>(config);
    mcp::examples::g_server->start();  // Blocks until stop() is called
    
    // Print final statistics
    const auto& stats = mcp::examples::g_server->getStats();
    std::cerr << "\n[INFO] Server shutdown complete\n"
              << "[INFO] Final statistics:\n"
              << "[INFO]   Total connections: " << stats.connections_total << "\n"
              << "[INFO]   Total requests: " << stats.requests_total << "\n"
              << "[INFO]   Successful requests: " << stats.requests_success << "\n"
              << "[INFO]   Failed requests: " << stats.requests_failed << "\n"
              << "[INFO]   Total errors: " << stats.errors_total << "\n";
    
  } catch (const std::exception& e) {
    std::cerr << "[ERROR] Server failed: " << e.what() << std::endl;
    return 1;
  }
  
  // Explicitly reset the server to ensure clean shutdown
  mcp::examples::g_server.reset();
  
  return 0;
}