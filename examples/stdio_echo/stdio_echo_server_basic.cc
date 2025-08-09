/**
 * @file stdio_echo_server.cc
 * @brief Basic MCP echo server using reusable components
 * 
 * This demonstrates the original basic echo server refactored to use
 * the new transport-agnostic base classes with stdio transport.
 * 
 * Architecture:
 * - Uses transport-agnostic base classes for echo functionality
 * - Stdio transport handles stdin/stdout communication
 * - JSON-RPC messages are newline-delimited
 * - Demonstrates builder pattern for message construction
 * 
 * Message Flow:
 * 1. Server reads JSON-RPC message from stdin via StdioTransport
 * 2. Base class invokes handleRequest/handleNotification callbacks
 * 3. Server constructs echo response with metadata
 * 4. Response is written to stdout for client to read
 * 
 * Protocol:
 * - Each JSON-RPC message is newline-delimited
 * - Requests get responses with matching IDs
 * - Notifications get echo notifications with "echo/" prefix
 * - "shutdown" notification triggers graceful shutdown
 */

#include "mcp/echo/echo_base.h"
#include "mcp/echo/stdio_transport.h"
#include <iostream>
#include <signal.h>
#include <memory>
#include <thread>
#include <chrono>

namespace mcp {
namespace examples {

// Global server instance for signal handling
std::unique_ptr<echo::EchoServerBase> g_server;

void signalHandler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    std::cerr << "\n[INFO] Received signal " << signal 
              << ", shutting down..." << std::endl;
    if (g_server) {
      g_server->stop();
    }
  }
}

/**
 * Basic echo server using reusable base class
 * 
 * This is the simplified version that uses transport-agnostic base classes.
 * All the complex connection management, thread safety, and I/O handling
 * is encapsulated in the base classes and transport implementation.
 * 
 * The original thread safety requirements are now handled by:
 * 1. StdioTransport runs a separate read thread for stdin
 * 2. Base class handles all message parsing and dispatching
 * 3. No dispatcher/event loop needed for basic functionality
 */
class BasicEchoServer : public echo::EchoServerBase {
public:
  BasicEchoServer(echo::EchoTransportPtr transport)
      : EchoServerBase(std::move(transport)) {
    // Configure base settings
    config_.server_name = "Basic MCP Echo Server";
    config_.echo_notifications = true;
    config_.enable_logging = true;
  }
  
protected:
  // Override handleRequest to provide custom echo logic
  // Request handling flow:
  // 1. Log received request with method
  // 2. Build echo response with metadata
  // 3. Return response (base class handles sending)
  jsonrpc::Response handleRequest(const jsonrpc::Request& request) override {
    logInfo("Processing request: " + request.method);
    
    // Build echo response with metadata about the request
    auto response = make<jsonrpc::Response>(request.id)
        .result(jsonrpc::ResponseResult(make<Metadata>()
            .add("echo", true)
            .add("method", request.method)
            .add("server", config_.server_name)
            .add("params_count", 
                 request.params.has_value() ? static_cast<int>(request.params.value().size()) : 0)
            .add("timestamp", std::chrono::system_clock::now().time_since_epoch().count())
            .build()))
        .build();
    
    return response;
  }

  // Override handleNotification to provide custom behavior
  // Notification handling flow:
  // 1. Check for special notifications (e.g., "ping")
  // 2. Handle special cases with custom responses
  // 3. Delegate to base class for standard echo behavior
  void handleNotification(const jsonrpc::Notification& notification) override {
    // Special handling for test notifications
    if (notification.method == "ping") {
      // Send pong notification back
      auto pong = make<jsonrpc::Notification>("pong")
          .params(make<Metadata>()
              .add("timestamp", std::chrono::system_clock::now().time_since_epoch().count())
              .build())
          .build();
      
      auto json_val = json::to_json(pong);
      std::cout << json_val.toString() << "\n";
      std::cout.flush();
      return;
    }
    
    // Use base class handling for other notifications
    EchoServerBase::handleNotification(notification);
  }
};

} // namespace examples
} // namespace mcp

int main(int argc, char* argv[]) {
  // Server initialization flow:
  // 1. Set up signal handlers for graceful shutdown
  // 2. Create stdio transport for I/O handling
  // 3. Create and configure echo server
  // 4. Start server (begins listening on stdin)
  // 5. Run until shutdown signal received
  // 6. Clean up and exit
  
  using namespace mcp;
  using namespace mcp::examples;
  
  // Parse command line arguments
  bool verbose = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--verbose" || arg == "-v") {
      verbose = true;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: " << argv[0] << " [options]\n"
                << "Options:\n"
                << "  -v, --verbose    Enable verbose logging\n"
                << "  -h, --help       Show this help message\n";
      return 0;
    }
  }
  
  // Setup signal handlers
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);
  signal(SIGPIPE, SIG_IGN);
  
  try {
    std::cerr << "MCP Stdio Echo Server (Basic)\n"
              << "==============================\n"
              << "This server echoes all JSON-RPC messages received on stdin\n"
              << "back to stdout with additional metadata.\n\n";
    
    std::cerr << "Starting stdio echo server...\n";
    
    // Create stdio transport
    auto transport = echo::createStdioTransport();
    
    // Create and start server
    g_server = std::make_unique<BasicEchoServer>(std::move(transport));
    
    echo::EchoServerBase::Config config;
    config.enable_logging = verbose;
    
    if (!g_server->start()) {
      std::cerr << "Failed to start echo server\n";
      return 1;
    }
    
    std::cerr << "Echo server started. Waiting for JSON-RPC messages on stdin...\n";
    std::cerr << "Send JSON-RPC messages to stdin, e.g.:\n";
    std::cerr << R"({"jsonrpc":"2.0","id":1,"method":"test","params":{"hello":"world"}})" << "\n";
    std::cerr << "Send 'shutdown' notification to stop the server.\n\n";
    
    // Keep running until stopped
    while (g_server->isRunning()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cerr << "\nServer stopped.\n";
    
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  
  return 0;
}