#include "mcp/transport/stdio_transport_socket.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/socket_interface_impl.h"
#include "mcp/network/address_impl.h"
#include "mcp/buffer.h"
#include "mcp/json/json_serialization.h"
#include "mcp/builders.h"
#include <iostream>
#include <signal.h>
#include <atomic>
#include <memory>
#include <unistd.h>
#include <thread>
#include <chrono>

namespace mcp {
namespace examples {

/**
 * Echo server implementation for MCP over stdio transport
 * 
 * Architecture:
 * - Listens for JSON-RPC messages on stdin
 * - Echoes responses/notifications back to stdout
 * - Demonstrates builder pattern for message construction
 * - Uses libevent dispatcher for async I/O handling
 * 
 * Message Flow:
 * 1. Server reads JSON-RPC message from stdin
 * 2. Dispatcher invokes appropriate callback (onRequest/onNotification)
 * 3. Server constructs echo response with metadata
 * 4. Response is written to stdout for client to read
 * 
 * Protocol:
 * - Each JSON-RPC message is newline-delimited
 * - Requests get responses with matching IDs
 * - Notifications get echo notifications with "echo/" prefix
 * - "shutdown" notification triggers graceful shutdown
 */
class StdioEchoServer : public McpMessageCallbacks {
public:
  StdioEchoServer(event::Dispatcher& dispatcher)
      : dispatcher_(dispatcher),
        running_(false) {
    // Initialize connection config for stdio
    McpConnectionConfig config;
    config.transport_type = TransportType::Stdio;
    config.stdio_config = transport::StdioTransportSocketConfig{
        .stdin_fd = STDIN_FILENO,
        .stdout_fd = STDOUT_FILENO,
        .non_blocking = true
    };
    config.use_message_framing = false;  // Disable framing for simpler testing
    
    // Create socket interface
    socket_interface_ = std::make_unique<network::SocketInterfaceImpl>();
    
    // Create connection manager
    connection_manager_ = std::make_unique<McpConnectionManager>(
        dispatcher_, *socket_interface_, config);
    
    // Set this as the message callback handler
    connection_manager_->setMessageCallbacks(*this);
  }
  
  ~StdioEchoServer() {
    stop();
  }
  
  bool start() {
    if (running_) {
      return true;
    }
    
    std::cerr << "Starting stdio echo server...\n";
    
    // Thread Safety Critical Section - Detailed Flow:
    // 
    // Problem: Dispatcher requires all I/O operations to happen on its thread.
    // The thread_id_ is set when dispatcher->run() is first called.
    // 
    // Without post():
    // 1. main() creates dispatcher (thread_id_ not set)
    // 2. start() calls connect() from main thread
    // 3. connect() tries to create file events
    // 4. createFileEvent() checks isThreadSafe()
    // 5. FAILS: current_thread != thread_id_ (which is unset)
    // 
    // With post():
    // 1. main() creates dispatcher
    // 2. start() posts connection task
    // 3. main() calls run(), setting thread_id_
    // 4. Posted task executes in dispatcher thread
    // 5. connect() succeeds: current_thread == thread_id_
    dispatcher_.post([this]() {
      // Connect using stdio transport
      auto result = connection_manager_->connect();
      if (holds_alternative<Error>(result)) {
        std::cerr << "Failed to start stdio transport: " 
                  << get<Error>(result).message << "\n";
        running_ = false;
        return;
      }
      
      running_ = true;
      std::cerr << "Echo server started. Waiting for JSON-RPC messages on stdin...\n";
    });
    
    // Set running_ to true optimistically since post() was successful
    // The actual connection will happen in the dispatcher thread
    running_ = true;
    return true;
  }
  
  void stop() {
    if (running_) {
      std::cerr << "Stopping echo server...\n";
      connection_manager_->close();
      running_ = false;
    }
  }
  
  bool isRunning() const {
    return running_;
  }

  // McpMessageCallbacks interface implementation
  // 
  // Request handling flow:
  // 1. Log received request with method and ID
  // 2. Build echo response with metadata
  // 3. Send response back to client via stdout
  void onRequest(const jsonrpc::Request& request) override {
    std::cerr << "Received request: " << request.method 
              << " (id: " << serializeRequestId(request.id) << ")\n";
    
    // Build echo response using builder pattern
    auto response = make<jsonrpc::Response>(request.id)
        .result(createEchoResult(request.method, request.params))
        .build();
    
    // Send response
    auto send_result = connection_manager_->sendResponse(response);
    if (holds_alternative<Error>(send_result)) {
      std::cerr << "Failed to send response: " 
                << get<Error>(send_result).message << "\n";
    } else {
      std::cerr << "Sent echo response\n";
    }
  }
  
  void onNotification(const jsonrpc::Notification& notification) override {
    std::cerr << "Received notification: " << notification.method << "\n";
    
    // Notification handling flow:
    // 1. Check for special "shutdown" notification
    // 2. For others, create echo with "echo/" prefix
    // 3. Send echo notification back to client
    
    // Check for shutdown notification
    if (notification.method == "shutdown") {
      std::cerr << "Received shutdown notification\n";
      dispatcher_.post([this]() { stop(); });
      return;
    }
    
    // Build echo notification using builder pattern
    auto echo_notif = make<jsonrpc::Notification>("echo/" + notification.method)
        .params(createEchoParams(notification.method, notification.params))
        .build();
    
    // Send echo notification
    auto send_result = connection_manager_->sendNotification(echo_notif);
    if (holds_alternative<Error>(send_result)) {
      std::cerr << "Failed to send notification: " 
                << get<Error>(send_result).message << "\n";
    } else {
      std::cerr << "Sent echo notification\n";
    }
  }
  
  void onResponse(const jsonrpc::Response& response) override {
    std::cerr << "Received response (id: " 
              << serializeRequestId(response.id) << ")\n";
    // Server typically doesn't receive responses, but we'll log them
  }
  
  void onConnectionEvent(network::ConnectionEvent event) override {
    switch (event) {
      case network::ConnectionEvent::Connected:
        std::cerr << "Connection established\n";
        break;
      case network::ConnectionEvent::RemoteClose:
        std::cerr << "Connection closed by remote\n";
        dispatcher_.post([this]() { stop(); });
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
  // Helper to create echo response with metadata about the original request
  jsonrpc::ResponseResult createEchoResult(const std::string& method,
                                          const optional<Metadata>& params) {
    // Build result metadata using builder pattern
    auto result = make<Metadata>()
        .add("echo", true)
        .add("method", method)
        .add("timestamp", std::chrono::system_clock::now().time_since_epoch().count())
        .add("params_count", params.has_value() ? static_cast<int>(params.value().size()) : 0)
        .build();
    
    return jsonrpc::ResponseResult(result);
  }
  
  Metadata createEchoParams(const std::string& original_method,
                           const optional<Metadata>& original_params) {
    // Build params using builder pattern
    return make<Metadata>()
        .add("original_method", original_method)
        .add("timestamp", std::chrono::system_clock::now().time_since_epoch().count())
        .add("original_params_count", 
             original_params.has_value() ? static_cast<int>(original_params.value().size()) : 0)
        .build();
  }
  
  std::string serializeRequestId(const RequestId& id) const {
    if (mcp::holds_alternative<int>(id)) {
      return std::to_string(mcp::get<int>(id));
    } else {
      return mcp::get<std::string>(id);
    }
  }
  
  event::Dispatcher& dispatcher_;
  std::unique_ptr<network::SocketInterface> socket_interface_;
  std::unique_ptr<McpConnectionManager> connection_manager_;
  std::atomic<bool> running_;
};

} // namespace examples
} // namespace mcp

// Global server instance for signal handling
std::unique_ptr<mcp::examples::StdioEchoServer> g_server;
std::atomic<bool> g_shutdown(false);

void signalHandler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    std::cerr << "\nReceived signal " << signal << ", shutting down...\n";
    g_shutdown = true;
    if (g_server) {
      g_server->stop();
    }
  }
}

int main(int argc, char* argv[]) {
  // Server initialization and event loop
  // Flow:
  // 1. Set up signal handlers for graceful shutdown
  // 2. Create libevent dispatcher for async I/O
  // 3. Create and start echo server
  // 4. Run event loop until shutdown signal
  // 5. Clean up and exit
  
  std::cerr << "MCP Stdio Echo Server\n";
  std::cerr << "=====================\n";
  std::cerr << "This server echoes all JSON-RPC messages received on stdin\n";
  std::cerr << "back to stdout with additional metadata.\n\n";
  
  // Set up signal handlers
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);
  signal(SIGPIPE, SIG_IGN);
  
  // Create event loop
  auto dispatcher_factory = mcp::event::createLibeventDispatcherFactory();
  auto dispatcher = dispatcher_factory->createDispatcher("echo-server");
  
  // Create and start server
  g_server = std::make_unique<mcp::examples::StdioEchoServer>(*dispatcher);
  
  if (!g_server->start()) {
    std::cerr << "Failed to start echo server\n";
    return 1;
  }
  
  // Run event loop
  std::cerr << "Running event loop. Press Ctrl+C to stop.\n";
  std::cerr << "Send JSON-RPC messages to stdin, e.g.:\n";
  std::cerr << R"({"jsonrpc":"2.0","id":1,"method":"test","params":{"hello":"world"}})" << "\n\n";
  
  // Main event loop:
  // - Process I/O events via dispatcher
  // - Check for shutdown signal
  // - Small sleep to prevent CPU spinning
  while (!g_shutdown && g_server->isRunning()) {
    dispatcher->run(mcp::event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  std::cerr << "Server stopped.\n";
  return 0;
}