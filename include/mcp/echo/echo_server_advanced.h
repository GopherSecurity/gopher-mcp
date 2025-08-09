/**
 * @file echo_server_advanced.h
 * @brief Reusable advanced echo server with transport abstraction
 */

#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "mcp/builders.h"
#include "mcp/echo/echo_transport_advanced.h"
#include "mcp/types.h"

namespace mcp {
namespace echo {

/**
 * @brief Flow control filter for watermark-based backpressure
 */
class FlowControlFilter {
 public:
  FlowControlFilter(uint32_t high_watermark, uint32_t low_watermark);

  bool processData(size_t data_size);
  bool processWrite(size_t written_size);

  size_t getBufferSize() const { return buffer_size_; }
  bool isAboveWatermark() const { return above_watermark_; }

 private:
  uint32_t high_watermark_;
  uint32_t low_watermark_;
  std::atomic<size_t> buffer_size_;
  std::atomic<bool> above_watermark_;
};

/**
 * @brief Advanced echo server statistics
 */
struct ServerStats {
  std::atomic<uint64_t> connections_total{0};
  std::atomic<uint64_t> connections_active{0};
  std::atomic<uint64_t> requests_total{0};
  std::atomic<uint64_t> requests_success{0};
  std::atomic<uint64_t> requests_failed{0};
  std::atomic<uint64_t> notifications_total{0};
  std::atomic<uint64_t> errors_total{0};
  std::atomic<uint64_t> request_duration_ms_total{0};
  std::atomic<uint64_t> request_duration_ms_min{UINT64_MAX};
  std::atomic<uint64_t> request_duration_ms_max{0};
  std::atomic<uint64_t> bytes_received{0};
  std::atomic<uint64_t> bytes_sent{0};
};

/**
 * @brief Configuration for advanced echo server
 */
struct EchoServerConfig {
  // Flow control settings
  uint32_t buffer_high_watermark = 1024 * 1024;  // 1MB
  uint32_t buffer_low_watermark = 256 * 1024;    // 256KB

  // Worker settings
  size_t num_workers = 2;
  size_t max_connections_per_worker = 10;

  // Request settings
  std::chrono::milliseconds request_timeout = std::chrono::seconds(30);
  size_t max_request_size = 10 * 1024 * 1024;  // 10MB

  // Metrics
  bool enable_metrics = true;
  std::chrono::seconds metrics_interval = std::chrono::seconds(10);

  // Echo behavior
  bool echo_requests = true;
  bool echo_notifications = true;
  bool add_timestamp = true;
  bool add_server_info = false;
};

/**
 * @brief Request processor for handling echo logic
 */
class RequestProcessor {
 public:
  RequestProcessor(const EchoServerConfig& config);

  jsonrpc::Response processRequest(const jsonrpc::Request& request);
  optional<jsonrpc::Notification> processNotification(
      const jsonrpc::Notification& notification);

 private:
  Metadata createEchoMetadata(const std::string& method,
                              const optional<Metadata>& params);

  const EchoServerConfig& config_;
};

/**
 * @brief Connection context for tracking client connections
 */
struct ConnectionContext {
  std::string connection_id;
  std::chrono::steady_clock::time_point connect_time;
  uint64_t requests_processed = 0;
  uint64_t bytes_received = 0;
  uint64_t bytes_sent = 0;
  std::string partial_message;
  std::unique_ptr<FlowControlFilter> flow_control;
};

/**
 * @brief Advanced echo server with transport abstraction
 *
 * Features:
 * - Transport-agnostic communication
 * - Worker thread model
 * - Flow control with watermarks
 * - Request/notification processing
 * - Connection management
 * - Comprehensive metrics
 * - Graceful shutdown
 */
class AdvancedEchoServer {
 public:
  AdvancedEchoServer(EchoTransportPtr transport,
                     const EchoServerConfig& config = {});
  ~AdvancedEchoServer();

  /**
   * @brief Start the server
   * @param endpoint Transport-specific endpoint
   * @return Success or error
   */
  variant<Success, Error> start(const std::string& endpoint);

  /**
   * @brief Stop the server
   */
  void stop();

  /**
   * @brief Check if server is running
   */
  bool isRunning() const { return running_; }

  /**
   * @brief Get server statistics
   */
  const ServerStats& getStats() const { return stats_; }

  /**
   * @brief Reset statistics
   */
  void resetStats();

  /**
   * @brief Register custom request handler
   * @param method Method name to handle
   * @param handler Function to process the request
   */
  void registerHandler(
      const std::string& method,
      std::function<jsonrpc::Response(const jsonrpc::Request&)> handler);

  /**
   * @brief Register custom notification handler
   * @param method Method name to handle
   * @param handler Function to process the notification
   */
  void registerNotificationHandler(
      const std::string& method,
      std::function<void(const jsonrpc::Notification&)> handler);

 private:
  void handleDataReceived(const std::string& data);
  void handleStatusChange(EchoTransportAdvanced::Status status);
  void handleError(const Error& error);
  void processMessage(const std::string& message, ConnectionContext* context);
  void handleRequest(const jsonrpc::Request& request,
                     ConnectionContext* context);
  void handleNotification(const jsonrpc::Notification& notification,
                          ConnectionContext* context);
  void sendResponse(const jsonrpc::Response& response,
                    ConnectionContext* context);
  void sendNotification(const jsonrpc::Notification& notification,
                        ConnectionContext* context);
  void updateLatencyMetrics(uint64_t duration_ms);
  void printMetrics();
  void workerThread(size_t worker_id);

  EchoTransportPtr transport_;
  EchoServerConfig config_;
  ServerStats stats_;

  std::unique_ptr<RequestProcessor> processor_;
  std::unique_ptr<ConnectionContext> primary_connection_;

  // Custom handlers
  std::unordered_map<std::string,
                     std::function<jsonrpc::Response(const jsonrpc::Request&)>>
      request_handlers_;
  std::unordered_map<std::string,
                     std::function<void(const jsonrpc::Notification&)>>
      notification_handlers_;

  std::atomic<bool> running_{false};
  std::vector<std::thread> worker_threads_;
  std::thread metrics_thread_;
  mutable std::mutex mutex_;

  // For multi-connection transports (TCP, HTTP)
  std::unordered_map<std::string, std::unique_ptr<ConnectionContext>>
      connections_;
  std::mutex connections_mutex_;
};

}  // namespace echo
}  // namespace mcp