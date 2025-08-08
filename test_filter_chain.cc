#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <thread>
#include <chrono>
#include <variant>
#include <sys/select.h>
#include "mcp/mcp_connection_manager.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/socket_interface_impl.h"
#include "mcp/json_serialization.h"

class DebugCallbacks : public mcp::McpMessageCallbacks {
public:
  void onRequest(const mcp::jsonrpc::Request& request) override {
    std::cout << "[DEBUG] onRequest: method=" << request.method << "\n";
    request_count_++;
  }
  
  void onNotification(const mcp::jsonrpc::Notification& notification) override {
    std::cout << "[DEBUG] onNotification: method=" << notification.method << "\n";
  }
  
  void onResponse(const mcp::jsonrpc::Response& response) override {
    std::cout << "[DEBUG] onResponse\n";
  }
  
  void onConnectionEvent(mcp::network::ConnectionEvent event) override {
    std::cout << "[DEBUG] onConnectionEvent: " << static_cast<int>(event) << "\n";
  }
  
  void onError(const mcp::Error& error) override {
    std::cout << "[DEBUG] onError: " << error.message << "\n";
  }
  
  int request_count_ = 0;
};

int main() {
  std::cout << "=== Testing Filter Chain Processing ===\n";
  
  // Create test pipes
  int test_stdin[2], test_stdout[2];
  if (pipe(test_stdin) != 0 || pipe(test_stdout) != 0) {
    std::cerr << "Failed to create pipes\n";
    return 1;
  }
  
  // Make non-blocking
  fcntl(test_stdin[0], F_SETFL, O_NONBLOCK);
  fcntl(test_stdout[1], F_SETFL, O_NONBLOCK);
  
  std::cout << "Pipes created\n";
  
  // Create dispatcher
  auto factory = mcp::event::createLibeventDispatcherFactory();
  auto dispatcher = factory->createDispatcher("test");
  std::cout << "Dispatcher created\n";
  
  // Configure connection
  mcp::McpConnectionConfig config;
  config.transport_type = mcp::TransportType::Stdio;
  mcp::transport::StdioTransportSocketConfig stdio_config;
  stdio_config.stdin_fd = test_stdin[0];  // Read end of stdin pipe
  stdio_config.stdout_fd = test_stdout[1]; // Write end of stdout pipe
  stdio_config.non_blocking = true;
  config.stdio_config = stdio_config;
  
  std::cout << "Configured stdin_fd=" << stdio_config.stdin_fd 
            << " stdout_fd=" << stdio_config.stdout_fd << "\n";
  config.use_message_framing = false;  // No framing for simple test
  
  auto socket_interface = std::make_unique<mcp::network::SocketInterfaceImpl>();
  auto connection_manager = std::make_unique<mcp::McpConnectionManager>(
      *dispatcher, *socket_interface, config);
  
  DebugCallbacks callbacks;
  connection_manager->setMessageCallbacks(callbacks);
  
  std::cout << "Connection manager created\n";
  
  // Connect in dispatcher thread
  bool connected = false;
  dispatcher->post([&]() {
    auto result = connection_manager->connect();
    connected = !std::holds_alternative<mcp::Error>(result);
    std::cout << "[DISPATCHER] Connect result: " << (connected ? "success" : "failed") << "\n";
  });
  
  // Run dispatcher to process connect
  std::cout << "Processing connection...\n";
  for (int i = 0; i < 5; ++i) {
    dispatcher->run(mcp::event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  if (!connected) {
    std::cerr << "Failed to connect\n";
    return 1;
  }
  
  std::cout << "Connected successfully\n";
  
  // Write a simple JSON-RPC request
  std::string request = R"({"jsonrpc":"2.0","id":1,"method":"test","params":{}})";
  request += "\n";  // Add newline delimiter
  
  std::cout << "Writing request to fd=" << test_stdin[1] << ": " << request;
  ssize_t n = write(test_stdin[1], request.c_str(), request.size());
  std::cout << "Wrote " << n << " bytes to pipe\n";
  
  // Check if data is available in the pipe
  fd_set readfds;
  struct timeval tv = {0, 0};
  FD_ZERO(&readfds);
  FD_SET(test_stdin[0], &readfds);
  int ready = select(test_stdin[0] + 1, &readfds, NULL, NULL, &tv);
  std::cout << "Select on fd=" << test_stdin[0] << " says " << ready << " fds ready\n";
  
  // Give bridge thread time to transfer data
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // Process messages
  std::cout << "Processing messages...\n";
  for (int i = 0; i < 20; ++i) {
    std::cout << "  Iteration " << i << "\n";
    dispatcher->run(mcp::event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    if (callbacks.request_count_ > 0) {
      std::cout << "Request processed!\n";
      break;
    }
  }
  
  if (callbacks.request_count_ == 0) {
    std::cout << "ERROR: No requests received\n";
    
    // Try to read directly from the pipe to see if data is there
    char buffer[256];
    ssize_t direct_read = read(test_stdin[0], buffer, sizeof(buffer));
    if (direct_read > 0) {
      buffer[direct_read] = '\0';
      std::cout << "Direct read got " << direct_read << " bytes: " << buffer;
    } else {
      std::cout << "Direct read got nothing (result=" << direct_read << ")\n";
    }
  }
  
  // Clean up
  close(test_stdin[0]);
  close(test_stdin[1]);
  close(test_stdout[0]);
  close(test_stdout[1]);
  
  return callbacks.request_count_ > 0 ? 0 : 1;
}