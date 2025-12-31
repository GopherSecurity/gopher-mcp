/**
 * @file stdio_echo_client.cc
 * @brief Basic MCP echo client using reusable components
 *
 * This demonstrates the original basic echo client refactored to use
 * the new transport-agnostic base classes with stdio transport.
 *
 * Architecture:
 * - Uses transport-agnostic base classes for echo functionality
 * - Stdio transport handles stdin/stdout communication
 * - Async request/response tracking with futures
 * - Demonstrates builder pattern for message construction
 *
 * Message Flow:
 * 1. Client sends JSON-RPC request/notification to stdout
 * 2. Request is tracked with unique ID for async response matching
 * 3. Server processes request and sends response
 * 4. Client reads response from stdin via StdioTransport
 * 5. Base class matches response by ID and completes promise
 *
 * Protocol:
 * - Each JSON-RPC message is newline-delimited
 * - Requests are tracked by ID for async response handling
 * - Supports both requests (with responses) and notifications
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <signal.h>
#include <thread>
#include <vector>

#include "mcp/echo/echo_basic.h"
#include "mcp/echo/stdio_transport.h"

namespace mcp {
namespace examples {

// Global client instance for signal handling
std::unique_ptr<echo::EchoClientBase> g_client;
std::atomic<bool> g_shutdown(false);

void signalHandler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    std::cerr << "\n[INFO] Received signal " << signal << ", shutting down..."
              << std::endl;
    g_shutdown = true;
    if (g_client) {
      g_client->stop();
    }
  }
}

/**
 * Basic echo client using reusable base class
 *
 * This is the simplified version that uses transport-agnostic base classes.
 * All the complex connection management, thread safety, request tracking,
 * and I/O handling is encapsulated in the base classes.
 *
 * The original thread safety requirements are now handled by:
 * 1. StdioTransport runs a separate read thread for stdin
 * 2. Base class manages pending requests with thread-safe map
 * 3. Futures provide clean async response handling
 * 4. No dispatcher/event loop needed for basic functionality
 */
class BasicEchoClient : public echo::EchoClientBase {
 public:
  BasicEchoClient(echo::EchoTransportBasePtr transport)
      : EchoClientBase(std::move(transport)) {
    // Configure base settings
    config_.client_name = "Basic MCP Echo Client";
    config_.enable_logging = true;
  }

 protected:
  // Override handleNotification to add custom behavior
  // Notification handling flow:
  // 1. Log received notification
  // 2. Check for echo notifications ("echo/" prefix)
  // 3. Handle special server messages (e.g., "server.ready")
  void handleNotification(const jsonrpc::Notification& notification) override {
    logInfo("Received notification: " + notification.method);

    // Special handling for echo notifications
    if (notification.method.find("echo/") == 0) {
      std::string original_method = notification.method.substr(5);
      logInfo("Server echoed our '" + original_method + "' notification");
    }

    // Special handling for server messages
    if (notification.method == "server.ready") {
      logInfo("Server is ready to accept requests");
    } else if (notification.method == "pong") {
      logInfo("Received pong response");
    }
  }

 public:
  // Run automated test sequence
  // Demonstrates various message patterns:
  // - Simple requests without parameters
  // - Requests with complex parameters
  // - Fire-and-forget notifications
  // - Multiple concurrent requests with futures
  // - Builder pattern usage for message construction
  void runTestSequence() {
    logInfo("Starting test sequence...");

    // Test 1: Simple request
    {
      logInfo("Test 1: Simple ping request");
      auto future = sendRequest("ping");

      try {
        auto response = future.get();
        if (!response.error.has_value()) {
          logInfo("Ping successful!");
        }
      } catch (const std::exception& e) {
        logError("Ping failed: " + std::string(e.what()));
      }
    }

    // Test 2: Request with parameters
    {
      logInfo("Test 2: Request with params");
      auto params = make<Metadata>()
                        .add("message", "Hello, Echo Server!")
                        .add("count", static_cast<int64_t>(42))
                        .build();

      auto future = sendRequest("echo.test", params);

      try {
        auto response = future.get();
        if (!response.error.has_value()) {
          logInfo("Echo test successful!");
        }
      } catch (const std::exception& e) {
        logError("Echo test failed: " + std::string(e.what()));
      }
    }

    // Test 3: Notification
    {
      logInfo("Test 3: Sending notification");
      auto params = make<Metadata>()
                        .add("level", "info")
                        .add("message", "Test notification from client")
                        .build();

      sendNotification("log", params);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Test 4: Multiple concurrent requests
    {
      logInfo("Test 4: Multiple concurrent requests");
      std::vector<std::future<jsonrpc::Response>> futures;

      for (int i = 0; i < 5; ++i) {
        auto params =
            make<Metadata>()
                .add("index", static_cast<int64_t>(i))
                .add(
                    "timestamp",
                    std::chrono::system_clock::now().time_since_epoch().count())
                .build();

        futures.push_back(sendRequest("batch.test", params));
      }

      // Wait for all responses
      int success_count = 0;
      for (size_t i = 0; i < futures.size(); ++i) {
        try {
          auto response = futures[i].get();
          if (!response.error.has_value()) {
            success_count++;
          }
        } catch (...) {
          // Request failed
        }
      }

      logInfo("Batch test: " + std::to_string(success_count) + "/" +
              std::to_string(futures.size()) + " requests succeeded");
    }

    // Test 5: Ping-pong test
    {
      logInfo("Test 5: Ping notification (expecting pong)");
      sendNotification("ping");
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    logInfo("Test sequence complete");
  }

  // Run manual test mode
  void runManualTests() {
    logInfo("Running manual tests...");

    // Send a simple test request
    auto params =
        make<Metadata>().add("message", "Hello from manual mode!").build();

    auto future = sendRequest("manual.test", params);

    try {
      auto response = future.get();
      if (!response.error.has_value()) {
        logInfo("Manual test succeeded!");
      }
    } catch (const std::exception& e) {
      logError("Manual test failed: " + std::string(e.what()));
    }

    // Send periodic pings
    for (int i = 0; i < 3; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));

      auto ping_params =
          make<Metadata>().add("count", static_cast<int64_t>(i + 1)).build();

      sendNotification("ping", ping_params);
    }

    logInfo("Manual tests complete");
  }
};

}  // namespace examples
}  // namespace mcp

void printUsage(const char* program) {
  std::cout << "Usage: " << program << " [options] [mode]\n"
            << "Options:\n"
            << "  -v, --verbose    Enable verbose logging\n"
            << "  -h, --help       Show this help message\n"
            << "Modes:\n"
            << "  auto             Run automated test sequence (default)\n"
            << "  manual           Send manual test messages\n"
            << "  interactive      Interactive mode (not yet implemented)\n";
}

int main(int argc, char* argv[]) {
  // Client initialization and mode selection flow:
  // 1. Parse command-line arguments for mode selection
  // 2. Set up signal handlers for graceful shutdown
  // 3. Create stdio transport for I/O handling
  // 4. Create and configure echo client
  // 5. Start client (connects via stdio)
  // 6. Execute mode-specific logic (auto/manual/interactive)
  // 7. Run until shutdown signal or completion
  // 8. Clean up and exit

  using namespace mcp;
  using namespace mcp::examples;

  // Parse command line arguments
  std::string mode = "auto";
  bool verbose = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--verbose" || arg == "-v") {
      verbose = true;
    } else if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      return 0;
    } else if (arg == "auto" || arg == "manual" || arg == "interactive") {
      mode = arg;
    }
  }

  // Setup signal handlers
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);
#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);  // SIGPIPE doesn't exist on Windows
#endif

  try {
    std::cerr << "MCP Stdio Echo Client (Basic)\n"
              << "==============================\n"
              << "Mode: " << mode << "\n";

    std::cerr << "Starting stdio echo client...\n";

    // Create stdio transport
    auto transport = echo::createStdioTransport();

    // Create and start client
    g_client = std::make_unique<BasicEchoClient>(std::move(transport));

    echo::EchoClientBase::Config config;
    config.enable_logging = verbose;

    if (!g_client->start()) {
      std::cerr << "Failed to start echo client\n";
      return 1;
    }

    std::cerr << "Echo client started.\n";
    std::cerr << "Connected to server via stdio.\n\n";

    // Get basic client for running tests
    auto basic_client = static_cast<BasicEchoClient*>(g_client.get());

    // Run mode-specific logic
    if (mode == "auto") {
      // Run automated test sequence
      basic_client->runTestSequence();

      // Wait a bit then shutdown (but exit early if disconnected)
      for (int i = 0; i < 20 && g_client->isRunning(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      std::cerr << "\nTest sequence finished. Shutting down...\n";
      g_shutdown = true;

    } else if (mode == "manual") {
      // Run manual tests
      basic_client->runManualTests();

      // Keep running for a while
      std::cerr << "\nManual tests complete. Press Ctrl+C to exit.\n";
      while (!g_shutdown && g_client->isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

    } else if (mode == "interactive") {
      std::cerr << "Interactive mode not yet implemented.\n"
                << "Use 'auto' or 'manual' mode instead.\n"
                << "Press Ctrl+C to exit.\n";

      while (!g_shutdown && g_client->isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }

    // Stop client if not already stopped
    if (g_client && g_client->isRunning()) {
      g_client->stop();
    }

    std::cerr << "\nClient stopped.\n";

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}