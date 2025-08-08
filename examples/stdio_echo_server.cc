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
#include <unistd.h>
#include <thread>
#include <chrono>

namespace mcp {
namespace examples {

/**
 * Echo server implementation for MCP over stdio transport
 * Receives JSON-RPC messages and echoes them back using builder pattern
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
    
    // Thread safety fix - Current problematic flow:
    // 1. main() creates dispatcher but doesn't call run() yet
    // 2. start() calls connection_manager_->connect()
    // 3. connect() creates StdioTransportSocket
    // 4. StdioTransportSocket constructor calls dispatcher->createFileEvent() for stdin/stdout
    // 5. createFileEvent() has assert(isThreadSafe())
    // 6. isThreadSafe() checks: current_thread == thread_id_
    // 7. But thread_id_ is ONLY set when dispatcher->run() is called!
    // 8. ASSERTION FAILS because thread_id_ is not set yet
    //
    // Fixed flow using post():
    // 1. main() creates dispatcher
    // 2. start() uses dispatcher_.post() to defer connection
    // 3. main() calls dispatcher->run() which sets thread_id_ = current_thread
    // 4. Now posted callback executes IN the dispatcher thread
    // 5. connection_manager_->connect() called from dispatcher thread
    // 6. createFileEvent() called, isThreadSafe() returns true
    // 7. Everything works!
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

  // McpMessageCallbacks interface
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
  
  while (!g_shutdown && g_server->isRunning()) {
    dispatcher->run(mcp::event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  std::cerr << "Server stopped.\n";
  return 0;
}