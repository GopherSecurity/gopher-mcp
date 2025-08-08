#include "mcp/transport/stdio_transport_socket.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/socket_interface_impl.h"
#include "mcp/network/address_impl.h"
#include "mcp/buffer.h"
#include "mcp/json_serialization.h"
#include "mcp/builders.h"
#include <iostream>
#include <signal.h>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <unistd.h>

namespace mcp {
namespace examples {

/**
 * Echo client implementation for MCP over stdio transport
 * Sends JSON-RPC messages and receives echo responses using builder pattern
 */
class StdioEchoClient : public McpMessageCallbacks {
public:
  StdioEchoClient(event::Dispatcher& dispatcher)
      : dispatcher_(dispatcher),
        running_(false),
        next_request_id_(1) {
    // Initialize connection config for stdio
    McpConnectionConfig config;
    config.transport_type = TransportType::Stdio;
    config.stdio_config = transport::StdioTransportSocketConfig{
        .stdin_fd = STDIN_FILENO,
        .stdout_fd = STDOUT_FILENO,
        .non_blocking = true
    };
    config.use_message_framing = true;
    
    // Create socket interface
    socket_interface_ = std::make_unique<network::SocketInterfaceImpl>();
    
    // Create connection manager
    connection_manager_ = std::make_unique<McpConnectionManager>(
        dispatcher_, *socket_interface_, config);
    
    // Set this as the message callback handler
    connection_manager_->setMessageCallbacks(*this);
  }
  
  ~StdioEchoClient() {
    stop();
  }
  
  bool start() {
    if (running_) {
      return true;
    }
    
    std::cerr << "Starting stdio echo client...\n";
    
    // Connect using stdio transport
    auto result = connection_manager_->connect();
    if (holds_alternative<Error>(result)) {
      std::cerr << "Failed to start stdio transport: " 
                << get<Error>(result).message << "\n";
      return false;
    }
    
    running_ = true;
    std::cerr << "Echo client started.\n";
    return true;
  }
  
  void stop() {
    if (running_) {
      std::cerr << "Stopping echo client...\n";
      
      // Send shutdown notification
      sendShutdownNotification();
      
      // Give time for shutdown to be sent
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      
      connection_manager_->close();
      running_ = false;
    }
  }
  
  bool isRunning() const {
    return running_;
  }
  
  /**
   * Send a test request to the server
   */
  void sendTestRequest(const std::string& method, const Metadata& params = {}) {
    if (!running_) {
      std::cerr << "Client not running\n";
      return;
    }
    
    int request_id = next_request_id_++;
    
    // Build request using builder pattern from builders.h
    auto builder = make<jsonrpc::Request>(make_request_id(request_id), method);
    if (!params.empty()) {
      builder.params(params);
    }
    auto request = builder.build();
    
    // Track pending request
    auto start_time = std::chrono::steady_clock::now();
    pending_requests_[request_id] = {method, start_time};
    
    std::cerr << "Sending request #" << request_id 
              << " (method: " << method << ")\n";
    
    auto result = connection_manager_->sendRequest(request);
    if (holds_alternative<Error>(result)) {
      std::cerr << "Failed to send request: " 
                << get<Error>(result).message << "\n";
      pending_requests_.erase(request_id);
    }
  }
  
  /**
   * Send a test notification to the server
   */
  void sendTestNotification(const std::string& method, const Metadata& params = {}) {
    if (!running_) {
      std::cerr << "Client not running\n";
      return;
    }
    
    // Build notification using builder pattern from builders.h
    auto builder = make<jsonrpc::Notification>(method);
    if (!params.empty()) {
      builder.params(params);
    }
    auto notification = builder.build();
    
    std::cerr << "Sending notification: " << method << "\n";
    
    auto result = connection_manager_->sendNotification(notification);
    if (holds_alternative<Error>(result)) {
      std::cerr << "Failed to send notification: " 
                << get<Error>(result).message << "\n";
    }
  }
  
  /**
   * Run automated test sequence
   */
  void runTestSequence() {
    std::cerr << "\n=== Starting test sequence ===\n";
    
    // Test 1: Simple request without params
    std::cerr << "\nTest 1: Simple request\n";
    sendTestRequest("ping");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Test 2: Request with params using builder pattern
    std::cerr << "\nTest 2: Request with params\n";
    auto params2 = make<Metadata>()
        .add("message", "Hello, Echo Server!")
        .add("count", 42)
        .build();
    sendTestRequest("echo.test", params2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Test 3: Notification using builder
    std::cerr << "\nTest 3: Notification\n";
    auto params3 = make<Metadata>()
        .add("level", "info")
        .add("message", "Test notification from client")
        .build();
    sendTestNotification("log", params3);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Test 4: Multiple requests in sequence
    std::cerr << "\nTest 4: Multiple rapid requests\n";
    for (int i = 0; i < 5; ++i) {
      auto params = make<Metadata>()
          .add("index", i)
          .add("timestamp", std::chrono::system_clock::now().time_since_epoch().count())
          .build();
      sendTestRequest("batch.test", params);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Test 5: Complex nested params using various builders
    std::cerr << "\nTest 5: Complex params with builders\n";
    
    // Use SamplingParamsBuilder as an example
    auto sampling = SamplingParamsBuilder()
        .temperature(0.7)
        .maxTokens(100)
        .stopSequence("END")
        .metadata("test_key", "test_value")
        .build();
    
    // Use ToolBuilder as another example
    auto tool = ToolBuilder("calculator")
        .description("A simple calculator tool")
        .parameter("expression", "string", "Mathematical expression to evaluate", true)
        .build();
    
    // Build complex params with multiple builder results
    auto complex_params = make<Metadata>()
        .add("string_value", "test")
        .add("number_value", 3.14159)
        .add("boolean_value", true)
        .build();
    
    // Note: In a real implementation, you'd serialize the complex objects
    // For now, just send the params
    sendTestRequest("complex.test", complex_params);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::cerr << "\n=== Test sequence complete ===\n";
    std::cerr << "Pending requests: " << pending_requests_.size() << "\n";
  }

  // McpMessageCallbacks interface
  void onRequest(const jsonrpc::Request& request) override {
    std::cerr << "Received unexpected request from server: " 
              << request.method << "\n";
    
    // Clients typically don't receive requests, but we'll respond anyway
    auto response = make<jsonrpc::Response>(request.id)
        .result(jsonrpc::ResponseResult(
            make<Metadata>()
                .add("unexpected", true)
                .add("message", "Client received unexpected request")
                .build()))
        .build();
    
    connection_manager_->sendResponse(response);
  }
  
  void onNotification(const jsonrpc::Notification& notification) override {
    std::cerr << "Received notification: " << notification.method;
    
    if (notification.params.has_value()) {
      // Try to extract some info from params
      std::cerr << " (has params)";
    }
    std::cerr << "\n";
  }
  
  void onResponse(const jsonrpc::Response& response) override {
    // Extract request ID
    int request_id = -1;
    if (mcp::holds_alternative<int>(response.id)) {
      request_id = mcp::get<int>(response.id);
    }
    
    std::cerr << "Received response for request #" << request_id;
    
    // Check if we have this pending request
    auto it = pending_requests_.find(request_id);
    if (it != pending_requests_.end()) {
      auto elapsed = std::chrono::steady_clock::now() - it->second.start_time;
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
      
      std::cerr << " (method: " << it->second.method 
                << ", RTT: " << ms << "ms)";
      
      pending_requests_.erase(it);
    }
    
    if (response.error.has_value()) {
      std::cerr << " - ERROR: " << response.error.value().message;
    } else if (response.result.has_value()) {
      std::cerr << " - SUCCESS";
    }
    
    std::cerr << "\n";
  }
  
  void onConnectionEvent(network::ConnectionEvent event) override {
    switch (event) {
      case network::ConnectionEvent::Connected:
        std::cerr << "Connection established\n";
        break;
      case network::ConnectionEvent::RemoteClose:
        std::cerr << "Connection closed by server\n";
        dispatcher_.post([this]() { 
          running_ = false;
        });
        break;
      case network::ConnectionEvent::LocalClose:
        std::cerr << "Connection closed locally\n";
        break;
      case network::ConnectionEvent::ConnectedZeroRtt:
        std::cerr << "Connection established (Zero-RTT)\n";
        break;
    }
  }
  
  void onError(const Error& error) override {
    std::cerr << "Connection error: " << error.message 
              << " (code: " << error.code << ")\n";
  }

private:
  void sendShutdownNotification() {
    auto shutdown = make<jsonrpc::Notification>("shutdown").build();
    connection_manager_->sendNotification(shutdown);
  }
  
  struct PendingRequest {
    std::string method;
    std::chrono::steady_clock::time_point start_time;
  };
  
  event::Dispatcher& dispatcher_;
  std::unique_ptr<network::SocketInterface> socket_interface_;
  std::unique_ptr<McpConnectionManager> connection_manager_;
  std::atomic<bool> running_;
  std::atomic<int> next_request_id_;
  std::unordered_map<int, PendingRequest> pending_requests_;
};

} // namespace examples
} // namespace mcp

// Global client instance for signal handling
std::unique_ptr<mcp::examples::StdioEchoClient> g_client;
std::atomic<bool> g_shutdown(false);

void signalHandler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    std::cerr << "\nReceived signal " << signal << ", shutting down...\n";
    g_shutdown = true;
    if (g_client) {
      g_client->stop();
    }
  }
}

void printUsage() {
  std::cerr << "Usage:\n";
  std::cerr << "  stdio_echo_client [mode]\n\n";
  std::cerr << "Modes:\n";
  std::cerr << "  auto       - Run automated test sequence (default)\n";
  std::cerr << "  manual     - Send manual test messages\n";
  std::cerr << "  interactive - Interactive mode (send custom messages)\n\n";
}

int main(int argc, char* argv[]) {
  std::string mode = "auto";
  if (argc > 1) {
    mode = argv[1];
    if (mode == "-h" || mode == "--help") {
      printUsage();
      return 0;
    }
  }
  
  std::cerr << "MCP Stdio Echo Client\n";
  std::cerr << "=====================\n";
  std::cerr << "Mode: " << mode << "\n\n";
  
  // Set up signal handlers
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);
  signal(SIGPIPE, SIG_IGN);
  
  // Create event loop
  auto dispatcher_factory = mcp::event::createLibeventDispatcherFactory();
  auto dispatcher = dispatcher_factory->createDispatcher("echo-client");
  
  // Create and start client
  g_client = std::make_unique<mcp::examples::StdioEchoClient>(*dispatcher);
  
  if (!g_client->start()) {
    std::cerr << "Failed to start echo client\n";
    return 1;
  }
  
  if (mode == "auto") {
    // Run automated test sequence
    dispatcher->post([&]() {
      g_client->runTestSequence();
      
      // Schedule shutdown after tests complete
      dispatcher->createTimer([&]() {
        std::cerr << "\nTest sequence finished. Shutting down...\n";
        g_shutdown = true;
        g_client->stop();
      })->enableTimer(std::chrono::seconds(2));
    });
  } else if (mode == "manual") {
    // Send a few manual test messages using builders
    dispatcher->post([&]() {
      auto params = mcp::make<mcp::Metadata>()
          .add("message", "Hello from manual mode!")
          .build();
      g_client->sendTestRequest("manual.test", params);
    });
    
    // Schedule periodic pings
    auto ping_timer = dispatcher->createTimer([&]() {
      static int ping_count = 0;
      auto params = mcp::make<mcp::Metadata>()
          .add("count", ++ping_count)
          .build();
      g_client->sendTestRequest("ping", params);
    });
    ping_timer->enableTimer(std::chrono::seconds(5));
  } else if (mode == "interactive") {
    std::cerr << "Interactive mode. Commands:\n";
    std::cerr << "  request <method> [json_params]  - Send request\n";
    std::cerr << "  notify <method> [json_params]   - Send notification\n";
    std::cerr << "  quit                            - Exit\n\n";
    
    // Note: In a real implementation, you'd read from a separate input
    // For this example, we'll just demonstrate the structure
    std::cerr << "Interactive mode not fully implemented in this example.\n";
    std::cerr << "Use 'auto' or 'manual' mode instead.\n";
  }
  
  // Run event loop
  while (!g_shutdown && g_client->isRunning()) {
    dispatcher->run(mcp::event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  std::cerr << "Client stopped.\n";
  return 0;
}