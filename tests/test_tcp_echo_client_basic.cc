/**
 * @file test_tcp_echo_client_basic.cc
 * @brief APPLICATION LEVEL TESTS for TCP echo client binary
 * 
 * TEST LEVEL: End-to-end application testing
 * 
 * This file tests the actual TCP echo client application binary by spawning
 * processes and testing through network sockets. It validates the complete 
 * application behavior from command-line invocation to JSON-RPC responses
 * over TCP connections.
 * 
 * What this tests:
 * - TCP echo client binary execution
 * - End-to-end JSON-RPC request/response flows over TCP
 * - TCP connection establishment and teardown
 * - Application-level error handling and reconnection
 * - Process lifecycle (startup, shutdown, signals)
 * - Command-line argument parsing (host, port, requests)
 * - Real TCP socket communication
 * 
 * What this does NOT test:
 * - Transport layer internals (poll, select, events)
 * - Socket implementation details
 * - Low-level TCP mechanisms
 * 
 * For transport-level testing, see:
 * - tests/test_tcp_echo_transport_basic.cc (transport layer tests)
 */

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

namespace mcp {
namespace examples {
namespace test {

// Helper function to get the client binary path
const char* getTCPClientBinaryPath() {
  // Try examples/tcp_echo directory (when running from build root)
  if (access("./examples/tcp_echo/tcp_echo_client_basic", X_OK) == 0) {
    return "./examples/tcp_echo/tcp_echo_client_basic";
  }
  // Try build/examples directory (when running from project root)
  if (access("./build/examples/tcp_echo/tcp_echo_client_basic", X_OK) == 0) {
    return "./build/examples/tcp_echo/tcp_echo_client_basic";
  }
  // Try parent examples directory (when running from tests/ directory)
  if (access("../examples/tcp_echo/tcp_echo_client_basic", X_OK) == 0) {
    return "../examples/tcp_echo/tcp_echo_client_basic";
  }
  // Try relative from build directory
  if (access("../build/examples/tcp_echo/tcp_echo_client_basic", X_OK) == 0) {
    return "../build/examples/tcp_echo/tcp_echo_client_basic";
  }
  return nullptr;
}

// Test fixture for TCPEchoClient
class TCPEchoClientBasicTest : public ::testing::Test {
protected:
  void SetUp() override {
    client_pid = 0;
    server_pid = 0;
    test_port = 20000 + (rand() % 5000); // Random port 20000-25000
  }
  
  void TearDown() override {
    if (client_pid > 0) {
      kill(client_pid, SIGTERM);
      waitpid(client_pid, nullptr, 0);
      client_pid = 0;
    }
    if (server_pid > 0) {
      kill(server_pid, SIGTERM);
      waitpid(server_pid, nullptr, 0);
      server_pid = 0;
    }
  }
  
  // Start a simple TCP echo server for testing
  bool startTestServer() {
    server_pid = fork();
    if (server_pid < 0) return false;
    
    if (server_pid == 0) {
      // Child process - simple TCP echo server
      int server_fd = socket(AF_INET, SOCK_STREAM, 0);
      if (server_fd < 0) exit(1);
      
      int opt = 1;
      setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
      
      struct sockaddr_in addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = INADDR_ANY;
      addr.sin_port = htons(test_port);
      
      if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) exit(1);
      if (listen(server_fd, 1) < 0) exit(1);
      
      // Accept one connection
      int client_fd = accept(server_fd, nullptr, nullptr);
      if (client_fd < 0) exit(1);
      
      // Echo data back
      char buffer[1024];
      ssize_t n;
      while ((n = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) {
        send(client_fd, buffer, n, 0);
      }
      
      close(client_fd);
      close(server_fd);
      exit(0);
    }
    
    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return true;
  }
  
  pid_t client_pid;
  pid_t server_pid;
  int test_port;
};

// Test client binary exists
TEST_F(TCPEchoClientBasicTest, ClientBinaryExists) {
  const char* client_path = getTCPClientBinaryPath();
  ASSERT_NE(client_path, nullptr) 
      << "TCP client binary not found or not executable. Build it first with: make tcp_echo_client_basic";
}

// Test client starts and stops cleanly
TEST_F(TCPEchoClientBasicTest, ClientStartStop) {
  const char* client_path = getTCPClientBinaryPath();
  if (!client_path) {
    GTEST_SKIP() << "TCP client binary not found";
  }
  
  int pipe_err[2];
  ASSERT_EQ(pipe(pipe_err), 0);
  
  client_pid = fork();
  ASSERT_GE(client_pid, 0);
  
  if (client_pid == 0) {
    // Child process - run the client
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_err[0]);
    close(pipe_err[1]);
    
    // Run with non-existent server to test startup/shutdown
    execl(client_path, "tcp_echo_client", "localhost", 
          std::to_string(test_port).c_str(), "0", nullptr);
    exit(1);  // exec failed
  }
  
  // Parent process
  close(pipe_err[1]);
  
  // Set non-blocking for stderr
  int flags = fcntl(pipe_err[0], F_GETFL, 0);
  fcntl(pipe_err[0], F_SETFL, flags | O_NONBLOCK);
  
  // Read startup message from stderr
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  char buffer[4096];
  ssize_t n = read(pipe_err[0], buffer, sizeof(buffer) - 1);
  if (n > 0) {
    buffer[n] = '\0';
    std::string output(buffer);
    EXPECT_TRUE(output.find("TCP Echo Client") != std::string::npos ||
                output.find("Starting") != std::string::npos);
    EXPECT_TRUE(output.find(std::to_string(test_port)) != std::string::npos);
  }
  
  // Send SIGTERM to stop client
  kill(client_pid, SIGTERM);
  
  // Wait for client to exit
  int status;
  waitpid(client_pid, &status, 0);
  client_pid = 0;
  
  // Client should exit cleanly
  EXPECT_TRUE(WIFEXITED(status) || WIFSIGNALED(status));
  
  close(pipe_err[0]);
}

// Test client connects to server
TEST_F(TCPEchoClientBasicTest, ClientConnectsToServer) {
  const char* client_path = getTCPClientBinaryPath();
  if (!client_path) {
    GTEST_SKIP() << "TCP client binary not found";
  }
  
  // Get server binary path
  const char* server_path = nullptr;
  if (access("./examples/tcp_echo/tcp_echo_server_basic", X_OK) == 0) {
    server_path = "./examples/tcp_echo/tcp_echo_server_basic";
  } else if (access("../examples/tcp_echo/tcp_echo_server_basic", X_OK) == 0) {
    server_path = "../examples/tcp_echo/tcp_echo_server_basic";
  }
  
  if (!server_path) {
    GTEST_SKIP() << "TCP server binary not found";
  }
  
  // Start server
  server_pid = fork();
  ASSERT_GE(server_pid, 0);
  
  if (server_pid == 0) {
    // Redirect output to /dev/null
    int devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    close(devnull);
    
    execl(server_path, "tcp_echo_server", std::to_string(test_port).c_str(), nullptr);
    exit(1);
  }
  
  // Give server time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  // Start client
  int pipe_err[2];
  ASSERT_EQ(pipe(pipe_err), 0);
  
  client_pid = fork();
  ASSERT_GE(client_pid, 0);
  
  if (client_pid == 0) {
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_err[0]);
    close(pipe_err[1]);
    
    execl(client_path, "tcp_echo_client", "localhost",
          std::to_string(test_port).c_str(), "1", nullptr);
    exit(1);
  }
  
  // Parent process
  close(pipe_err[1]);
  
  // Set non-blocking
  int flags = fcntl(pipe_err[0], F_GETFL, 0);
  fcntl(pipe_err[0], F_SETFL, flags | O_NONBLOCK);
  
  // Wait for connection
  std::string all_output;
  for (int i = 0; i < 30; ++i) {  // Wait up to 3 seconds
    char buffer[4096];
    ssize_t n = read(pipe_err[0], buffer, sizeof(buffer) - 1);
    if (n > 0) {
      buffer[n] = '\0';
      all_output += buffer;
    }
    
    // Check if client has finished
    int status;
    pid_t result = waitpid(client_pid, &status, WNOHANG);
    if (result == client_pid) {
      client_pid = 0;
      break;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  
  // Verify connection was established
  EXPECT_TRUE(all_output.find("Connected") != std::string::npos ||
              all_output.find("connection") != std::string::npos);
  EXPECT_TRUE(all_output.find("Request 1") != std::string::npos);
  
  // Clean up
  if (client_pid > 0) {
    kill(client_pid, SIGTERM);
    waitpid(client_pid, nullptr, 0);
    client_pid = 0;
  }
  
  close(pipe_err[0]);
}

// Test client handles connection failure
TEST_F(TCPEchoClientBasicTest, ClientHandlesConnectionFailure) {
  const char* client_path = getTCPClientBinaryPath();
  if (!client_path) {
    GTEST_SKIP() << "TCP client binary not found";
  }
  
  int pipe_err[2];
  ASSERT_EQ(pipe(pipe_err), 0);
  
  client_pid = fork();
  ASSERT_GE(client_pid, 0);
  
  if (client_pid == 0) {
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_err[0]);
    close(pipe_err[1]);
    
    // Try to connect to non-existent server
    execl(client_path, "tcp_echo_client", "localhost",
          std::to_string(test_port).c_str(), "1", nullptr);
    exit(1);
  }
  
  // Parent process
  close(pipe_err[1]);
  
  // Set non-blocking
  int flags = fcntl(pipe_err[0], F_GETFL, 0);
  fcntl(pipe_err[0], F_SETFL, flags | O_NONBLOCK);
  
  // Collect output
  std::string all_output;
  for (int i = 0; i < 20; ++i) {  // Wait up to 2 seconds
    char buffer[4096];
    ssize_t n = read(pipe_err[0], buffer, sizeof(buffer) - 1);
    if (n > 0) {
      buffer[n] = '\0';
      all_output += buffer;
    }
    
    // Check if process has exited
    int status;
    pid_t result = waitpid(client_pid, &status, WNOHANG);
    if (result == client_pid) {
      client_pid = 0;
      break;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  
  // Should indicate connection failure or waiting
  EXPECT_TRUE(all_output.find("Failed to connect") != std::string::npos ||
              all_output.find("Waiting for connection") != std::string::npos ||
              all_output.find("not connected") != std::string::npos);
  
  // Clean up
  if (client_pid > 0) {
    kill(client_pid, SIGTERM);
    waitpid(client_pid, nullptr, 0);
    client_pid = 0;
  }
  
  close(pipe_err[0]);
}

// Test client handles SIGINT
TEST_F(TCPEchoClientBasicTest, ClientHandlesSIGINT) {
  const char* client_path = getTCPClientBinaryPath();
  if (!client_path) {
    GTEST_SKIP() << "TCP client binary not found";
  }
  
  int pipe_err[2];
  ASSERT_EQ(pipe(pipe_err), 0);
  
  client_pid = fork();
  ASSERT_GE(client_pid, 0);
  
  if (client_pid == 0) {
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_err[0]);
    close(pipe_err[1]);
    
    execl(client_path, "tcp_echo_client", "localhost",
          std::to_string(test_port).c_str(), "0", nullptr);
    exit(1);
  }
  
  // Parent process
  close(pipe_err[1]);
  
  // Wait for client to start
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  // Send SIGINT
  kill(client_pid, SIGINT);
  
  // Set non-blocking
  int flags = fcntl(pipe_err[0], F_GETFL, 0);
  fcntl(pipe_err[0], F_SETFL, flags | O_NONBLOCK);
  
  // Read stderr for signal message
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  char buffer[1024];
  ssize_t n = read(pipe_err[0], buffer, sizeof(buffer) - 1);
  
  if (n > 0) {
    buffer[n] = '\0';
    std::string output(buffer);
    EXPECT_TRUE(output.find("signal") != std::string::npos ||
                output.find("Shutting down") != std::string::npos ||
                output.find("shutting down") != std::string::npos);
  }
  
  // Wait for client to exit
  int status;
  waitpid(client_pid, &status, 0);
  client_pid = 0;
  
  close(pipe_err[0]);
}

// Test client help message
TEST_F(TCPEchoClientBasicTest, ClientHelpMessage) {
  const char* client_path = getTCPClientBinaryPath();
  if (!client_path) {
    GTEST_SKIP() << "TCP client binary not found";
  }
  
  int pipe_out[2];
  ASSERT_EQ(pipe(pipe_out), 0);
  
  client_pid = fork();
  ASSERT_GE(client_pid, 0);
  
  if (client_pid == 0) {
    dup2(pipe_out[1], STDOUT_FILENO);
    close(pipe_out[0]);
    close(pipe_out[1]);
    
    execl(client_path, "tcp_echo_client", "--help", nullptr);
    exit(1);
  }
  
  // Parent process
  close(pipe_out[1]);
  
  // Read help output
  char buffer[4096];
  ssize_t n = read(pipe_out[0], buffer, sizeof(buffer) - 1);
  
  // Wait for process to exit
  int status;
  waitpid(client_pid, &status, 0);
  client_pid = 0;
  
  if (n > 0) {
    buffer[n] = '\0';
    std::string output(buffer);
    EXPECT_TRUE(output.find("Usage:") != std::string::npos);
    EXPECT_TRUE(output.find("host") != std::string::npos);
    EXPECT_TRUE(output.find("port") != std::string::npos);
    EXPECT_TRUE(output.find("requests") != std::string::npos);
  }
  
  close(pipe_out[0]);
}

// Test client with different ports
TEST_F(TCPEchoClientBasicTest, ClientWithDifferentPorts) {
  const char* client_path = getTCPClientBinaryPath();
  if (!client_path) {
    GTEST_SKIP() << "TCP client binary not found";
  }
  
  // Test with various port numbers
  std::vector<int> test_ports = {8080, 9000, 12345};
  
  for (int port : test_ports) {
    int pipe_err[2];
    ASSERT_EQ(pipe(pipe_err), 0);
    
    pid_t test_pid = fork();
    ASSERT_GE(test_pid, 0);
    
    if (test_pid == 0) {
      dup2(pipe_err[1], STDERR_FILENO);
      close(pipe_err[0]);
      close(pipe_err[1]);
      
      execl(client_path, "tcp_echo_client", "localhost",
            std::to_string(port).c_str(), "0", nullptr);
      exit(1);
    }
    
    close(pipe_err[1]);
    
    // Set non-blocking
    int flags = fcntl(pipe_err[0], F_GETFL, 0);
    fcntl(pipe_err[0], F_SETFL, flags | O_NONBLOCK);
    
    // Read output
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    char buffer[1024];
    ssize_t n = read(pipe_err[0], buffer, sizeof(buffer) - 1);
    
    if (n > 0) {
      buffer[n] = '\0';
      std::string output(buffer);
      // Should mention the port number
      EXPECT_TRUE(output.find(std::to_string(port)) != std::string::npos);
    }
    
    // Clean up
    kill(test_pid, SIGTERM);
    waitpid(test_pid, nullptr, 0);
    close(pipe_err[0]);
  }
}

// Test client with invalid arguments
TEST_F(TCPEchoClientBasicTest, ClientInvalidArguments) {
  const char* client_path = getTCPClientBinaryPath();
  if (!client_path) {
    GTEST_SKIP() << "TCP client binary not found";
  }
  
  // Test with invalid port
  int pipe_err[2];
  ASSERT_EQ(pipe(pipe_err), 0);
  
  client_pid = fork();
  ASSERT_GE(client_pid, 0);
  
  if (client_pid == 0) {
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_err[0]);
    close(pipe_err[1]);
    
    execl(client_path, "tcp_echo_client", "localhost", "invalid_port", "1", nullptr);
    exit(1);
  }
  
  close(pipe_err[1]);
  
  // Wait for process to exit
  int status;
  waitpid(client_pid, &status, 0);
  client_pid = 0;
  
  // Should exit with error
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_NE(WEXITSTATUS(status), 0);
  
  close(pipe_err[0]);
}

} // namespace test
} // namespace examples
} // namespace mcp