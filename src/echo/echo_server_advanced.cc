/**
 * @file echo_server_advanced.cc
 * @brief Implementation of reusable advanced echo server
 */

#include "mcp/echo/echo_server_advanced.h"
#include "mcp/json/json_serialization.h"
#include <iostream>
#include <sstream>
#include <iomanip>

namespace mcp {
namespace echo {

// FlowControlFilter implementation
FlowControlFilter::FlowControlFilter(uint32_t high_watermark, uint32_t low_watermark)
    : high_watermark_(high_watermark),
      low_watermark_(low_watermark),
      buffer_size_(0),
      above_watermark_(false) {}

bool FlowControlFilter::processData(size_t data_size) {
  buffer_size_ += data_size;
  
  if (!above_watermark_ && buffer_size_ > high_watermark_) {
    above_watermark_ = true;
    return false; // Signal to disable reading
  }
  
  return true; // Continue reading
}

bool FlowControlFilter::processWrite(size_t written_size) {
  if (written_size <= buffer_size_) {
    buffer_size_ -= written_size;
  } else {
    buffer_size_ = 0;
  }
  
  if (above_watermark_ && buffer_size_ < low_watermark_) {
    above_watermark_ = false;
    return true; // Signal to re-enable reading
  }
  
  return false; // No change in read state
}

// RequestProcessor implementation
RequestProcessor::RequestProcessor(const EchoServerConfig& config)
    : config_(config) {}

jsonrpc::Response RequestProcessor::processRequest(const jsonrpc::Request& request) {
  try {
    auto echo_metadata = createEchoMetadata(request.method, request.params);
    
    return make<jsonrpc::Response>(request.id)
        .result(jsonrpc::ResponseResult(echo_metadata))
        .build();
  } catch (const std::exception& e) {
    return make<jsonrpc::Response>(request.id)
        .error(Error(jsonrpc::INTERNAL_ERROR, e.what()))
        .build();
  }
}

optional<jsonrpc::Notification> RequestProcessor::processNotification(
    const jsonrpc::Notification& notification) {
  
  if (!config_.echo_notifications) {
    return nullopt;
  }
  
  try {
    auto echo_metadata = createEchoMetadata(notification.method, notification.params);
    
    return make<jsonrpc::Notification>("echo/" + notification.method)
        .params(echo_metadata)
        .build();
  } catch (const std::exception& e) {
    // Log error but don't send error notification
    std::cerr << "[ERROR] Failed to process notification: " << e.what() << std::endl;
    return nullopt;
  }
}

Metadata RequestProcessor::createEchoMetadata(const std::string& method,
                                             const optional<Metadata>& params) {
  auto builder = make<Metadata>()
      .add("echo", true)
      .add("method", method);
  
  if (params.has_value()) {
    // Convert params to a simpler representation for echo
    // Since Metadata itself can't be stored in the variant, we convert to string
    auto params_json = json::to_json(params.value());
    builder.add("original_params", params_json.toString());
  }
  
  if (config_.add_timestamp) {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    builder.add("timestamp", timestamp);
  }
  
  if (config_.add_server_info) {
    builder.add("server", "advanced-echo-server")
           .add("version", "2.0.0");
  }
  
  return builder.build();
}

// AdvancedEchoServer implementation
AdvancedEchoServer::AdvancedEchoServer(EchoTransportPtr transport,
                                     const EchoServerConfig& config)
    : transport_(std::move(transport)),
      config_(config) {
  
  processor_ = std::make_unique<RequestProcessor>(config_);
  primary_connection_ = std::make_unique<ConnectionContext>();
  primary_connection_->connection_id = "primary";
  primary_connection_->connect_time = std::chrono::steady_clock::now();
  primary_connection_->flow_control = std::make_unique<FlowControlFilter>(
      config_.buffer_high_watermark,
      config_.buffer_low_watermark);
}

AdvancedEchoServer::~AdvancedEchoServer() {
  stop();
}

variant<Success, Error> AdvancedEchoServer::start(const std::string& endpoint) {
  if (running_) {
    return Error(-1, "Server already running");
  }
  
  // Initialize transport
  auto init_result = transport_->initialize();
  if (holds_alternative<Error>(init_result)) {
    return get<Error>(init_result);
  }
  
  // Set callbacks
  EchoTransportAdvanced::Callbacks callbacks;
  callbacks.onDataReceived = [this](const std::string& data) {
    handleDataReceived(data);
  };
  callbacks.onStatusChange = [this](EchoTransportAdvanced::Status status) {
    handleStatusChange(status);
  };
  callbacks.onError = [this](const Error& error) {
    handleError(error);
  };
  transport_->setCallbacks(callbacks);
  
  // Start listening
  auto listen_result = transport_->listen(endpoint);
  if (holds_alternative<Error>(listen_result)) {
    return get<Error>(listen_result);
  }
  
  running_ = true;
  stats_.connections_active = 1;
  
  // Start worker threads
  for (size_t i = 0; i < config_.num_workers; ++i) {
    worker_threads_.emplace_back(&AdvancedEchoServer::workerThread, this, i);
  }
  
  // Start metrics thread if enabled
  if (config_.enable_metrics) {
    metrics_thread_ = std::thread([this]() {
      while (running_) {
        std::this_thread::sleep_for(config_.metrics_interval);
        printMetrics();
      }
    });
  }
  
  std::cerr << "[INFO] Echo server started on " << endpoint 
            << " with " << config_.num_workers << " workers" << std::endl;
  
  return Success{};
}

void AdvancedEchoServer::stop() {
  if (!running_) {
    return;
  }
  
  running_ = false;
  
  // Stop transport
  transport_->close();
  
  // Join threads
  for (auto& thread : worker_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  worker_threads_.clear();
  
  if (metrics_thread_.joinable()) {
    metrics_thread_.join();
  }
  
  std::cerr << "[INFO] Echo server stopped" << std::endl;
}

void AdvancedEchoServer::resetStats() {
  stats_.connections_total = 0;
  stats_.connections_active = 0;
  stats_.requests_total = 0;
  stats_.requests_success = 0;
  stats_.requests_failed = 0;
  stats_.notifications_total = 0;
  stats_.errors_total = 0;
  stats_.request_duration_ms_total = 0;
  stats_.request_duration_ms_min = UINT64_MAX;
  stats_.request_duration_ms_max = 0;
  stats_.bytes_received = 0;
  stats_.bytes_sent = 0;
}

void AdvancedEchoServer::registerHandler(
    const std::string& method,
    std::function<jsonrpc::Response(const jsonrpc::Request&)> handler) {
  
  std::lock_guard<std::mutex> lock(mutex_);
  request_handlers_[method] = handler;
}

void AdvancedEchoServer::registerNotificationHandler(
    const std::string& method,
    std::function<void(const jsonrpc::Notification&)> handler) {
  
  std::lock_guard<std::mutex> lock(mutex_);
  notification_handlers_[method] = handler;
}

void AdvancedEchoServer::handleDataReceived(const std::string& data) {
  stats_.bytes_received += data.length();
  
  // For now, use primary connection (stdio is single connection)
  auto* context = primary_connection_.get();
  context->bytes_received += data.length();
  
  // Check flow control
  if (!context->flow_control->processData(data.length())) {
    // Would disable reading if transport supported it
    std::cerr << "[WARN] Flow control: High watermark reached" << std::endl;
  }
  
  context->partial_message += data;
  
  // Process newline-delimited messages
  size_t pos = 0;
  while ((pos = context->partial_message.find('\n')) != std::string::npos) {
    std::string message = context->partial_message.substr(0, pos);
    context->partial_message.erase(0, pos + 1);
    
    if (!message.empty()) {
      processMessage(message, context);
    }
  }
}

void AdvancedEchoServer::handleStatusChange(EchoTransportAdvanced::Status status) {
  if (status == EchoTransportAdvanced::Status::Connected) {
    stats_.connections_total++;
    stats_.connections_active++;
  } else if (status == EchoTransportAdvanced::Status::Disconnected) {
    if (stats_.connections_active > 0) {
      stats_.connections_active--;
    }
  }
}

void AdvancedEchoServer::handleError(const Error& error) {
  stats_.errors_total++;
  std::cerr << "[ERROR] Transport error: " << error.message << std::endl;
}

void AdvancedEchoServer::processMessage(const std::string& message,
                                       ConnectionContext* context) {
  try {
    auto json_val = json::JsonValue::parse(message);
    
    if (json_val.contains("method")) {
      if (json_val.contains("id")) {
        // Request
        auto request = json::from_json<jsonrpc::Request>(json_val);
        handleRequest(request, context);
      } else {
        // Notification
        auto notification = json::from_json<jsonrpc::Notification>(json_val);
        handleNotification(notification, context);
      }
    }
  } catch (const std::exception& e) {
    stats_.errors_total++;
    std::cerr << "[ERROR] Failed to parse message: " << e.what() << std::endl;
    
    // Send parse error response
    auto error_response = make<jsonrpc::Response>(0)
        .error(Error(jsonrpc::PARSE_ERROR, e.what()))
        .build();
    sendResponse(error_response, context);
  }
}

void AdvancedEchoServer::handleRequest(const jsonrpc::Request& request,
                                      ConnectionContext* context) {
  auto start_time = std::chrono::steady_clock::now();
  stats_.requests_total++;
  context->requests_processed++;
  
  jsonrpc::Response response;
  
  // Check for custom handler
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = request_handlers_.find(request.method);
    if (it != request_handlers_.end()) {
      response = it->second(request);
    } else if (request.method == "shutdown") {
      // Special shutdown handling
      response = make<jsonrpc::Response>(request.id)
          .result(jsonrpc::ResponseResult(make<Metadata>()
              .add("status", "shutting_down")
              .build()))
          .build();
      
      // Schedule shutdown
      std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        stop();
      }).detach();
    } else {
      // Default echo processing
      response = processor_->processRequest(request);
    }
  }
  
  // Update metrics
  auto duration = std::chrono::steady_clock::now() - start_time;
  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  updateLatencyMetrics(duration_ms);
  
  if (response.error.has_value()) {
    stats_.requests_failed++;
  } else {
    stats_.requests_success++;
  }
  
  sendResponse(response, context);
}

void AdvancedEchoServer::handleNotification(const jsonrpc::Notification& notification,
                                           ConnectionContext* context) {
  stats_.notifications_total++;
  
  // Check for custom handler
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = notification_handlers_.find(notification.method);
    if (it != notification_handlers_.end()) {
      it->second(notification);
      return;
    }
  }
  
  // Special shutdown notification
  if (notification.method == "shutdown") {
    std::thread([this]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      stop();
    }).detach();
    return;
  }
  
  // Default echo processing
  auto echo_notification = processor_->processNotification(notification);
  if (echo_notification.has_value()) {
    sendNotification(echo_notification.value(), context);
  }
}

void AdvancedEchoServer::sendResponse(const jsonrpc::Response& response,
                                     ConnectionContext* context) {
  auto json_val = json::to_json(response);
  std::string message = json_val.toString() + "\n";
  
  auto send_result = transport_->send(message);
  if (holds_alternative<Success>(send_result)) {
    stats_.bytes_sent += message.length();
    context->bytes_sent += message.length();
    
    // Update flow control
    if (context->flow_control->processWrite(message.length())) {
      // Would re-enable reading if transport supported it
    }
  } else {
    stats_.errors_total++;
  }
}

void AdvancedEchoServer::sendNotification(const jsonrpc::Notification& notification,
                                         ConnectionContext* context) {
  auto json_val = json::to_json(notification);
  std::string message = json_val.toString() + "\n";
  
  auto send_result = transport_->send(message);
  if (holds_alternative<Success>(send_result)) {
    stats_.bytes_sent += message.length();
    context->bytes_sent += message.length();
    
    // Update flow control
    context->flow_control->processWrite(message.length());
  } else {
    stats_.errors_total++;
  }
}

void AdvancedEchoServer::updateLatencyMetrics(uint64_t duration_ms) {
  stats_.request_duration_ms_total += duration_ms;
  
  uint64_t current_max = stats_.request_duration_ms_max;
  while (duration_ms > current_max && 
         !stats_.request_duration_ms_max.compare_exchange_weak(current_max, duration_ms));
  
  uint64_t current_min = stats_.request_duration_ms_min;
  while (duration_ms < current_min && 
         !stats_.request_duration_ms_min.compare_exchange_weak(current_min, duration_ms));
}

void AdvancedEchoServer::printMetrics() {
  std::cerr << "\n[METRICS] Echo Server Statistics:\n"
            << "  Connections total: " << stats_.connections_total << "\n"
            << "  Connections active: " << stats_.connections_active << "\n"
            << "  Requests total: " << stats_.requests_total << "\n"
            << "  Requests success: " << stats_.requests_success << "\n"
            << "  Requests failed: " << stats_.requests_failed << "\n"
            << "  Notifications total: " << stats_.notifications_total << "\n"
            << "  Errors total: " << stats_.errors_total << "\n"
            << "  Bytes received: " << stats_.bytes_received << "\n"
            << "  Bytes sent: " << stats_.bytes_sent << "\n";
  
  if (stats_.requests_success > 0) {
    uint64_t avg_latency = stats_.request_duration_ms_total / stats_.requests_success;
    std::cerr << "  Average latency: " << avg_latency << " ms\n"
              << "  Min latency: " << stats_.request_duration_ms_min << " ms\n"
              << "  Max latency: " << stats_.request_duration_ms_max << " ms\n";
  }
  
  if (primary_connection_) {
    auto uptime = std::chrono::steady_clock::now() - primary_connection_->connect_time;
    auto uptime_sec = std::chrono::duration_cast<std::chrono::seconds>(uptime).count();
    std::cerr << "  Uptime: " << uptime_sec << " seconds\n";
  }
  
  std::cerr << std::endl;
}

void AdvancedEchoServer::workerThread(size_t worker_id) {
  // Worker threads would be used for multi-connection transports
  // For stdio, the main thread handles everything
  while (running_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

} // namespace echo
} // namespace mcp