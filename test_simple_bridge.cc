#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <thread>
#include "mcp/transport/stdio_pipe_transport.h"

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
  
  // Create transport
  mcp::transport::StdioPipeTransportConfig config;
  config.stdin_fd = test_stdin[0];
  config.stdout_fd = test_stdout[1];
  config.non_blocking = true;
  
  auto transport = std::make_unique<mcp::transport::StdioPipeTransport>(config);
  auto result = transport->initialize();
  
  if (!std::holds_alternative<std::nullptr_t>(result)) {
    std::cerr << "Failed to initialize transport\n";
    return 1;
  }
  
  // Get the pipe socket
  auto socket = transport->takePipeSocket();
  if (!socket) {
    std::cerr << "Failed to get pipe socket\n";
    return 1;
  }
  
  std::cout << "Transport initialized successfully\n";
  std::cout << "Socket FD: " << socket->ioHandle().fd() << "\n";
  
  // Write test data to stdin
  std::string test_data = "Hello from test!\n";
  write(test_stdin[1], test_data.c_str(), test_data.size());
  
  // Give time for bridging
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // Try to read from the socket
  char buffer[256];
  ssize_t n = read(socket->ioHandle().fd(), buffer, sizeof(buffer));
  if (n > 0) {
    std::cout << "Read from socket: " << std::string(buffer, n) << "\n";
  } else {
    std::cout << "No data read from socket (n=" << n << ", errno=" << errno << ")\n";
  }
  
  // Write to socket and see if it appears on stdout
  std::string response = "Response from socket\n";
  write(socket->ioHandle().fd(), response.c_str(), response.size());
  
  // Give time for bridging
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // Read from stdout pipe
  n = read(test_stdout[0], buffer, sizeof(buffer));
  if (n > 0) {
    std::cout << "Read from stdout: " << std::string(buffer, n) << "\n";
  } else {
    std::cout << "No data read from stdout (n=" << n << ", errno=" << errno << ")\n";
  }
  
  // Clean up
  close(test_stdin[0]);
  close(test_stdin[1]);
  close(test_stdout[0]);
  close(test_stdout[1]);
  
  return 0;
}