/**
 * @file stdio_echo_server_advanced.cc
 * @brief Advanced echo server using new reusable transport-agnostic components
 *
 * This example demonstrates using the refactored echo architecture with:
 * - Transport-agnostic design (currently using stdio)
 * - Reusable AdvancedEchoServer with all advanced features
 * - Clean separation of transport and application logic
 *
 * Original implementation backed up in stdio_echo_server_advanced.cc.backup
 */

#include <algorithm>
#include <chrono>
#include <iostream>
#include <signal.h>
#include <thread>

#include "mcp/echo/echo_server_advanced.h"
#include "mcp/echo/echo_stdio_transport_advanced.h"

using namespace mcp;
using namespace mcp::echo;

namespace {

// Global server for signal handling
std::shared_ptr<AdvancedEchoServer> g_server;
std::atomic<bool> g_shutdown(false);

void signal_handler(int signal) {
  std::cerr << "\n[INFO] Received signal " << signal
            << ", initiating shutdown..." << std::endl;
  g_shutdown = true;
  if (g_server) {
    g_server->stop();
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  // Install signal handlers
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  std::cerr
      << "=================================================================="
      << std::endl;
  std::cerr << "Advanced Echo Server (Refactored with Transport Abstraction)"
            << std::endl;
  std::cerr
      << "=================================================================="
      << std::endl;
  std::cerr << "Features:" << std::endl;
  std::cerr << "  - Transport-agnostic architecture (stdio, tcp, http ready)"
            << std::endl;
  std::cerr << "  - Flow control with watermark-based backpressure"
            << std::endl;
  std::cerr << "  - Worker thread model for scalability" << std::endl;
  std::cerr << "  - Request/notification processing" << std::endl;
  std::cerr << "  - Comprehensive metrics and monitoring" << std::endl;
  std::cerr << "  - Custom handler registration" << std::endl;
  std::cerr
      << "=================================================================="
      << std::endl;

  // Configure the server
  EchoServerConfig config;
  config.enable_metrics = true;
  config.metrics_interval = std::chrono::seconds(30);
  config.num_workers = 2;
  config.buffer_high_watermark = 1024 * 1024;  // 1MB
  config.buffer_low_watermark = 256 * 1024;    // 256KB
  config.request_timeout = std::chrono::seconds(30);
  config.echo_requests = true;
  config.echo_notifications = true;
  config.add_timestamp = true;
  config.add_server_info = true;

  // Create stdio transport (can easily swap with TCP, HTTP, etc.)
  auto transport = std::make_shared<StdioEchoTransport>();

  // Create the advanced echo server
  g_server = std::make_shared<AdvancedEchoServer>(transport, config);

  // Register custom handlers

  // Custom ping handler
  g_server->registerHandler("ping", [](const jsonrpc::Request& request) {
    return make<jsonrpc::Response>(request.id)
        .result(jsonrpc::ResponseResult(
            make<Metadata>()
                .add("pong", true)
                .add("timestamp",
                     std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count())
                .build()))
        .build();
  });

  // Custom stats handler
  g_server->registerHandler(
      "server/stats", [](const jsonrpc::Request& request) {
        const auto& stats = g_server->getStats();
        return make<jsonrpc::Response>(request.id)
            .result(jsonrpc::ResponseResult(
                make<Metadata>()
                    .add("connections_total",
                         static_cast<int64_t>(stats.connections_total.load()))
                    .add("connections_active",
                         static_cast<int64_t>(stats.connections_active.load()))
                    .add("requests_total",
                         static_cast<int64_t>(stats.requests_total.load()))
                    .add("requests_success",
                         static_cast<int64_t>(stats.requests_success.load()))
                    .add("requests_failed",
                         static_cast<int64_t>(stats.requests_failed.load()))
                    .add("notifications_total",
                         static_cast<int64_t>(stats.notifications_total.load()))
                    .add("bytes_received",
                         static_cast<int64_t>(stats.bytes_received.load()))
                    .add("bytes_sent",
                         static_cast<int64_t>(stats.bytes_sent.load()))
                    .build()))
            .build();
      });

  // Custom echo with modification
  g_server->registerHandler(
      "echo/uppercase", [](const jsonrpc::Request& request) {
        std::string message = "ECHO";
        if (request.params.has_value()) {
          auto params = request.params.value();
          auto it = params.find("message");
          if (it != params.end()) {
            auto msg_val = it->second;
            if (holds_alternative<std::string>(msg_val)) {
              message = get<std::string>(msg_val);
              // Convert to uppercase
              std::transform(message.begin(), message.end(), message.begin(),
                             ::toupper);
            }
          }
        }

        return make<jsonrpc::Response>(request.id)
            .result(jsonrpc::ResponseResult(make<Metadata>()
                                                .add("echo", true)
                                                .add("message", message)
                                                .add("transformed", "uppercase")
                                                .build()))
            .build();
      });

  // Custom notification handler for logging
  g_server->registerNotificationHandler(
      "log", [](const jsonrpc::Notification& notification) {
        std::cerr << "[LOG] Received log notification: " << notification.method;
        if (notification.params.has_value()) {
          auto params = notification.params.value();
          auto level_it = params.find("level");
          if (level_it != params.end()) {
            if (holds_alternative<std::string>(level_it->second)) {
              std::cerr << " [" << get<std::string>(level_it->second) << "]";
            }
          }
          auto msg_it = params.find("message");
          if (msg_it != params.end()) {
            if (holds_alternative<std::string>(msg_it->second)) {
              std::cerr << " - " << get<std::string>(msg_it->second);
            }
          }
        }
        std::cerr << std::endl;
      });

  // Custom heartbeat handler
  g_server->registerNotificationHandler(
      "heartbeat", [](const jsonrpc::Notification& notification) {
        static int heartbeat_count = 0;
        heartbeat_count++;
        if (heartbeat_count % 10 == 0) {
          std::cerr << "[HEARTBEAT] Received " << heartbeat_count
                    << " heartbeats" << std::endl;
        }
      });

  // Start the server
  auto start_result = g_server->start("stdio://localhost");
  if (holds_alternative<Error>(start_result)) {
    std::cerr << "[ERROR] Failed to start server: "
              << get<Error>(start_result).message << std::endl;
    return 1;
  }

  std::cerr << "[INFO] Server started successfully" << std::endl;
  std::cerr << "[INFO] Transport type: " << transport->getTransportType()
            << std::endl;
  std::cerr << "[INFO] Number of workers: " << config.num_workers << std::endl;
  std::cerr << "[INFO] Flow control watermarks: "
            << "High=" << config.buffer_high_watermark
            << ", Low=" << config.buffer_low_watermark << std::endl;
  std::cerr
      << "[INFO] Custom handlers registered: ping, server/stats, echo/uppercase"
      << std::endl;
  std::cerr << "[INFO] Press Ctrl+C to shutdown" << std::endl;
  std::cerr
      << "=================================================================="
      << std::endl;

  // Main server loop
  while (!g_shutdown && g_server->isRunning()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Periodic health check
    const auto& stats = g_server->getStats();

    // Alert on high error rate
    if (stats.errors_total > 100) {
      std::cerr << "[WARN] High error count detected: " << stats.errors_total
                << " errors" << std::endl;
    }

    // Alert on high failure rate
    if (stats.requests_failed > 0 && stats.requests_total > 0) {
      double failure_rate = static_cast<double>(stats.requests_failed) /
                            static_cast<double>(stats.requests_total);
      if (failure_rate > 0.1) {  // More than 10% failure rate
        std::cerr << "[WARN] High failure rate: " << (failure_rate * 100) << "%"
                  << std::endl;
      }
    }
  }

  // Graceful shutdown
  std::cerr << "\n[INFO] Shutting down server..." << std::endl;
  g_server->stop();

  // Print final statistics
  const auto& stats = g_server->getStats();
  std::cerr
      << "\n=================================================================="
      << std::endl;
  std::cerr << "Final Statistics:" << std::endl;
  std::cerr
      << "=================================================================="
      << std::endl;
  std::cerr << "  Total connections: " << stats.connections_total << std::endl;
  std::cerr << "  Active connections: " << stats.connections_active
            << std::endl;
  std::cerr << "  Total requests processed: " << stats.requests_total
            << std::endl;
  std::cerr << "  Successful requests: " << stats.requests_success << std::endl;
  std::cerr << "  Failed requests: " << stats.requests_failed << std::endl;
  std::cerr << "  Total notifications: " << stats.notifications_total
            << std::endl;
  std::cerr << "  Total errors: " << stats.errors_total << std::endl;
  std::cerr << "  Total bytes received: " << stats.bytes_received << std::endl;
  std::cerr << "  Total bytes sent: " << stats.bytes_sent << std::endl;

  if (stats.requests_success > 0) {
    uint64_t avg_latency =
        stats.request_duration_ms_total / stats.requests_success;
    std::cerr << "  Average request latency: " << avg_latency << " ms"
              << std::endl;
    std::cerr << "  Min request latency: " << stats.request_duration_ms_min
              << " ms" << std::endl;
    std::cerr << "  Max request latency: " << stats.request_duration_ms_max
              << " ms" << std::endl;
  }

  // Calculate uptime
  auto now = std::chrono::steady_clock::now();
  // Note: Actual uptime calculation would need start time tracking

  std::cerr
      << "=================================================================="
      << std::endl;
  std::cerr << "[INFO] Server shutdown complete" << std::endl;

  return 0;
}