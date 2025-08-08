#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include "mcp/mcp_connection_manager.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/socket_interface_impl.h"

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
  
  std::cout << "Connection manager created\n";
  
  // Try to connect directly (not in dispatcher thread)
  std::cout << "Attempting direct connect...\n";
  auto result = connection_manager->connect();
  
  if (std::holds_alternative<mcp::Error>(result)) {
    auto& err = std::get<mcp::Error>(result);
    std::cerr << "Connect failed: " << err.message << "\n";
  } else {
    std::cout << "Connect succeeded!\n";
  }
  
  // Clean up
  close(test_stdin[0]);
  close(test_stdin[1]);
  close(test_stdout[0]);
  close(test_stdout[1]);
  
  return 0;
}