#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <thread>
#include <sys/select.h>
#include "mcp/mcp_connection_manager.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/socket_interface_impl.h"
#include "mcp/json_serialization.h"

class TestCallbacks : public mcp::McpMessageCallbacks {
public:
  void onRequest(const mcp::jsonrpc::Request& request) override {
    std::cout << "Received request: " << request.method << "\n";
    request_count_++;
  }
  
  void onNotification(const mcp::jsonrpc::Notification& notification) override {
    std::cout << "Received notification: " << notification.method << "\n";
  }
  
  void onResponse(const mcp::jsonrpc::Response& response) override {
    std::cout << "Received response\n";
  }
  
  void onConnectionEvent(mcp::network::ConnectionEvent event) override {
    std::cout << "Connection event: " << static_cast<int>(event) << "\n";
  }
  
  void onError(const mcp::Error& error) override {
    std::cout << "Error: " << error.message << "\n";
  }
  
  int request_count_ = 0;
};

int main() {
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
  config.stdio_config = mcp::transport::StdioTransportSocketConfig{
      .stdin_fd = test_stdin[0],
      .stdout_fd = test_stdout[1],
      .non_blocking = true
  };
  config.use_message_framing = false;
  
  std::cout << "Config created\n";
  
  auto socket_interface = std::make_unique<mcp::network::SocketInterfaceImpl>();
  auto connection_manager = std::make_unique<mcp::McpConnectionManager>(
      *dispatcher, *socket_interface, config);
  
  TestCallbacks callbacks;
  connection_manager->setMessageCallbacks(callbacks);
  
  std::cout << "Connection manager created\n";
  
  // Connect in dispatcher thread
  std::promise<bool> connect_promise;
  auto connect_future = connect_promise.get_future();
  
  dispatcher->post([&]() {
    auto result = connection_manager->connect();
    bool success = !std::holds_alternative<mcp::Error>(result);
    std::cout << "Connect result: " << (success ? "success" : "failed") << "\n";
    connect_promise.set_value(success);
  });
  
  // Run dispatcher to process connect
  dispatcher->run(mcp::event::RunType::NonBlock);
  
  if (!connect_future.get()) {
    std::cerr << "Failed to connect\n";
    return 1;
  }
  
  std::cout << "Connected\n";
  
  // Write a JSON-RPC request to the pipe
  std::string request = R"({"jsonrpc":"2.0","id":1,"method":"test","params":{"hello":"world"}})" "\n";
  std::cout << "Writing request: " << request;
  ssize_t n = write(test_stdin[1], request.c_str(), request.size());
  std::cout << "Wrote " << n << " bytes\n";
  
  // Check if data is available to read
  fd_set readfds;
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  
  FD_ZERO(&readfds);
  FD_SET(test_stdin[0], &readfds);
  
  int ready = select(test_stdin[0] + 1, &readfds, NULL, NULL, &tv);
  std::cout << "Select returned: " << ready << " (data " << (ready > 0 ? "available" : "not available") << ")\n";
  
  // Run dispatcher multiple times to process messages
  std::cout << "Processing messages...\n";
  for (int i = 0; i < 10; ++i) {
    dispatcher->run(mcp::event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    if (callbacks.request_count_ > 0) {
      std::cout << "Request processed!\n";
      break;
    }
  }
  
  if (callbacks.request_count_ == 0) {
    std::cout << "No requests received :(\n";
  }
  
  // Clean up
  close(test_stdin[0]);
  close(test_stdin[1]);
  close(test_stdout[0]);
  close(test_stdout[1]);
  
  return callbacks.request_count_ > 0 ? 0 : 1;
}