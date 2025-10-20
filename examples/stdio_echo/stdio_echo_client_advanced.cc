/**
 * @file stdio_echo_client_advanced.cc
 * @brief Advanced echo client using new reusable transport-agnostic components
 *
 * This example demonstrates using the refactored echo architecture with:
 * - Transport-agnostic design (currently using stdio)
 * - Reusable AdvancedEchoClient with all advanced features
 * - Clean separation of transport and application logic
 *
 * Original implementation backed up in stdio_echo_client_advanced.cc.backup
 */

#include <chrono>
#include <iostream>
#include <signal.h>
#include <thread>

#include "mcp/echo/echo_client_advanced.h"
#include "mcp/echo/echo_stdio_transport_advanced.h"

using namespace mcp;
using namespace mcp::echo;

namespace {

// Global client for signal handling
std::shared_ptr<AdvancedEchoClient> g_client;
std::atomic<bool> g_shutdown(false);

void signal_handler(int signal) {
  std::cerr << "\n[INFO] Received signal " << signal
            << ", initiating shutdown..." << std::endl;
  g_shutdown = true;
  if (g_client) {
    g_client->stop();
  }
}

void runDemoScenarios(AdvancedEchoClient& client) {
  std::cerr << "\n[DEMO] Running demonstration scenarios..." << std::endl;

  // Scenario 1: Simple echo request
  {
    std::cerr << "[DEMO] Scenario 1: Simple echo request" << std::endl;
    auto metadata = make<Metadata>()
                        .add("message", "Hello, Echo Server!")
                        .add("test_id", static_cast<int64_t>(1))
                        .build();

    auto future = client.sendRequest("echo", metadata);
    try {
      auto response = future.get();
      if (!response.error.has_value()) {
        std::cerr << "[DEMO] Echo request successful" << std::endl;
      } else {
        std::cerr << "[DEMO] Echo request failed: " << response.error->message
                  << std::endl;
      }
    } catch (const std::exception& e) {
      std::cerr << "[DEMO] Echo request exception: " << e.what() << std::endl;
    }
  }

  // Scenario 2: Batch requests
  {
    std::cerr << "\n[DEMO] Scenario 2: Batch requests" << std::endl;
    std::vector<std::pair<std::string, Metadata>> batch;

    for (int i = 0; i < 5; ++i) {
      auto metadata = make<Metadata>()
                          .add("batch_item", static_cast<int64_t>(i))
                          .add("message", "Batch message " + std::to_string(i))
                          .build();
      batch.push_back({"echo/batch", metadata});
    }

    auto futures = client.sendBatch(batch);
    std::cerr << "[DEMO] Sent batch of " << futures.size() << " requests"
              << std::endl;

    int success_count = 0;
    for (auto& future : futures) {
      try {
        auto response = future.get();
        if (!response.error.has_value()) {
          success_count++;
        }
      } catch (...) {
        // Ignore individual failures
      }
    }
    std::cerr << "[DEMO] Batch complete: " << success_count << "/"
              << futures.size() << " successful" << std::endl;
  }

  // Scenario 3: Stress test with rapid requests
  {
    std::cerr << "\n[DEMO] Scenario 3: Stress test with 20 rapid requests"
              << std::endl;
    std::vector<std::future<jsonrpc::Response>> stress_futures;

    for (int i = 0; i < 20; ++i) {
      auto metadata = make<Metadata>()
                          .add("stress_test", true)
                          .add("request_id", i)
                          .build();
      stress_futures.push_back(client.sendRequest("echo/stress", metadata));
    }

    // Wait for all to complete
    int completed = 0;
    for (auto& future : stress_futures) {
      if (future.wait_for(std::chrono::seconds(5)) ==
          std::future_status::ready) {
        completed++;
      }
    }
    std::cerr << "[DEMO] Stress test complete: " << completed
              << "/20 completed within timeout" << std::endl;
  }

  // Scenario 4: Notifications (fire-and-forget)
  {
    std::cerr << "\n[DEMO] Scenario 4: Sending notifications" << std::endl;
    for (int i = 0; i < 3; ++i) {
      auto metadata = make<Metadata>()
                          .add("notification_type", "heartbeat")
                          .add("sequence", i)
                          .build();

      auto result = client.sendNotification("heartbeat", metadata);
      if (holds_alternative<Success>(result)) {
        std::cerr << "[DEMO] Notification " << i << " sent successfully"
                  << std::endl;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  std::cerr << "\n[DEMO] All demonstration scenarios completed" << std::endl;
}

}  // namespace

int main(int argc, char* argv[]) {
  // Install signal handlers
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  std::cerr
      << "=================================================================="
      << std::endl;
  std::cerr << "Advanced Echo Client (Refactored with Transport Abstraction)"
            << std::endl;
  std::cerr
      << "=================================================================="
      << std::endl;
  std::cerr << "Features:" << std::endl;
  std::cerr << "  - Transport-agnostic architecture (stdio, tcp, http ready)"
            << std::endl;
  std::cerr << "  - Circuit breaker pattern for failure handling" << std::endl;
  std::cerr << "  - Request tracking with timeouts" << std::endl;
  std::cerr << "  - Batch request processing" << std::endl;
  std::cerr << "  - Comprehensive metrics and monitoring" << std::endl;
  std::cerr
      << "=================================================================="
      << std::endl;

  // Configure the client
  EchoClientConfig config;
  config.enable_metrics = true;
  config.metrics_interval = std::chrono::seconds(30);
  config.request_timeout = std::chrono::seconds(10);
  config.circuit_breaker_threshold = 5;
  config.circuit_breaker_timeout = std::chrono::seconds(30);
  config.max_retries = 3;

  // Create stdio transport (can easily swap with TCP, HTTP, etc.)
  auto transport = std::make_shared<StdioEchoTransport>();

  // Create the advanced echo client
  g_client = std::make_shared<AdvancedEchoClient>(transport, config);

  // Start the client
  auto start_result = g_client->start("stdio://localhost");
  if (holds_alternative<Error>(start_result)) {
    std::cerr << "[ERROR] Failed to start client: "
              << get<Error>(start_result).message << std::endl;
    return 1;
  }

  std::cerr << "[INFO] Client started successfully" << std::endl;
  std::cerr << "[INFO] Transport type: " << transport->getTransportType()
            << std::endl;
  std::cerr << "[INFO] Press Ctrl+C to shutdown" << std::endl;

  // Send initialization request
  {
    auto metadata = make<Metadata>()
                        .add("type", "initialization")
                        .add("client", "advanced-refactored")
                        .add("version", "2.0.0")
                        .add("feature_circuit_breaker", true)
                        .add("feature_batch", true)
                        .add("feature_metrics", true)
                        .build();

    auto future = g_client->sendRequest("initialize", metadata);

    try {
      auto response = future.get();
      if (response.error.has_value()) {
        std::cerr << "[ERROR] Initialization failed: "
                  << response.error->message << std::endl;
      } else {
        std::cerr << "[INFO] Initialization successful" << std::endl;
      }
    } catch (const std::exception& e) {
      std::cerr << "[ERROR] Initialization exception: " << e.what()
                << std::endl;
    }
  }

  // Run demonstration scenarios
  if (argc > 1 && std::string(argv[1]) == "--demo") {
    runDemoScenarios(*g_client);
  }

  // Main loop - send periodic requests
  int request_count = 0;
  while (!g_shutdown) {
    // Send echo request
    {
      auto metadata =
          make<Metadata>()
              .add("message", "Periodic echo request")
              .add("sequence", request_count++)
              .add("timestamp",
                   std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count())
              .build();

      auto future = g_client->sendRequest("echo/periodic", metadata);

      // Don't wait for response, let it complete asynchronously
      std::thread([future = std::move(future)]() mutable {
        try {
          auto response = future.get();
          if (!response.error.has_value()) {
            // Success logged internally by metrics
          }
        } catch (...) {
          // Timeout or other error, handled by client
        }
      }).detach();
    }

    // Send batch periodically
    if (request_count % 10 == 0 && request_count > 0) {
      std::vector<std::pair<std::string, Metadata>> batch;
      for (int i = 0; i < 3; ++i) {
        auto metadata = make<Metadata>()
                            .add("batch_sequence", request_count)
                            .add("item", i)
                            .build();
        batch.push_back({"echo/batch", metadata});
      }

      g_client->sendBatch(batch);
      std::cerr << "[INFO] Sent batch at sequence " << request_count
                << std::endl;
    }

    // Send heartbeat notification
    if (request_count % 5 == 0) {
      auto metadata = make<Metadata>()
                          .add("type", "heartbeat")
                          .add("sequence", request_count)
                          .build();

      g_client->sendNotification("heartbeat", metadata);
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));
  }

  // Send shutdown request
  {
    std::cerr << "\n[INFO] Sending shutdown request..." << std::endl;
    auto metadata = make<Metadata>()
                        .add("reason", "client_shutdown")
                        .add("total_requests", request_count)
                        .build();

    auto future = g_client->sendRequest("shutdown", metadata);

    try {
      if (future.wait_for(std::chrono::seconds(2)) ==
          std::future_status::ready) {
        auto response = future.get();
        if (!response.error.has_value()) {
          std::cerr << "[INFO] Server acknowledged shutdown" << std::endl;
        }
      }
    } catch (...) {
      // Ignore shutdown errors
    }
  }

  // Stop the client
  g_client->stop();

  // Print final statistics
  const auto& stats = g_client->getStats();
  std::cerr
      << "\n=================================================================="
      << std::endl;
  std::cerr << "Final Statistics:" << std::endl;
  std::cerr
      << "=================================================================="
      << std::endl;
  std::cerr << "  Total requests sent: " << stats.requests_total << std::endl;
  std::cerr << "  Successful responses: " << stats.requests_success
            << std::endl;
  std::cerr << "  Failed requests: " << stats.requests_failed << std::endl;
  std::cerr << "  Request timeouts: " << stats.requests_timeout << std::endl;
  std::cerr << "  Circuit breaker trips: " << stats.circuit_breaker_opens
            << std::endl;
  std::cerr << "  Total bytes sent: " << stats.bytes_sent << std::endl;
  std::cerr << "  Total bytes received: " << stats.bytes_received << std::endl;

  if (stats.requests_success > 0) {
    uint64_t avg_latency =
        stats.request_duration_ms_total / stats.requests_success;
    std::cerr << "  Average latency: " << avg_latency << " ms" << std::endl;
    std::cerr << "  Min latency: " << stats.request_duration_ms_min << " ms"
              << std::endl;
    std::cerr << "  Max latency: " << stats.request_duration_ms_max << " ms"
              << std::endl;
  }

  std::cerr
      << "=================================================================="
      << std::endl;
  std::cerr << "[INFO] Client shutdown complete" << std::endl;

  return 0;
}