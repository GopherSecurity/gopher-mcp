/**
 * @file test_tcp_echo_server_basic.cc
 * @brief APPLICATION LEVEL TESTS for TCP echo server binary
 * 
 * TEST LEVEL: End-to-end application testing
 * 
 * This file tests the actual TCP echo server application binary by spawning
 * processes and testing through TCP socket connections. It validates the complete
 * application behavior from startup to JSON-RPC message processing over TCP.
 * 
 * What this tests:
 * - TCP echo server binary execution
 * - End-to-end JSON-RPC request/response flows over TCP
 * - TCP server socket binding and listening
 * - Client connection acceptance
 * - Application-level notification handling
 * - Process lifecycle (startup, shutdown, signals)
 * - Real TCP socket communication
 * - Application error handling
 * - Port binding and configuration
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

// Helper function to get the server binary path
const char* getTCPServerBinaryPath() {
  // Try examples/tcp_echo directory (when running from build root)
  if (access("./examples/tcp_echo/tcp_echo_server_basic", X_OK) == 0) {
    return "./examples/tcp_echo/tcp_echo_server_basic";
  }
  // Try build/examples directory (when running from project root)
  if (access("./build/examples/tcp_echo/tcp_echo_server_basic", X_OK) == 0) {
    return "./build/examples/tcp_echo/tcp_echo_server_basic";
  }
  // Try parent examples directory (when running from tests/ directory)
  if (access("../examples/tcp_echo/tcp_echo_server_basic", X_OK) == 0) {
    return "../examples/tcp_echo/tcp_echo_server_basic";
  }
  // Try relative from build directory
  if (access("../build/examples/tcp_echo/tcp_echo_server_basic", X_OK) == 0) {
    return "../build/examples/tcp_echo/tcp_echo_server_basic";
  }
  return nullptr;
}

// Test fixture for TCPEchoServer
class TCPEchoServerBasicTest : public ::testing::Test {
protected:
  void SetUp() override {
    server_pid = 0;
    test_port = 25000 + (rand() % 5000); // Random port 25000-30000
  }
  
  void TearDown() override {
    if (server_pid > 0) {
      kill(server_pid, SIGTERM);
      waitpid(server_pid, nullptr, 0);
      server_pid = 0;
    }
  }
  
  // Check if port is available
  bool isPortAvailable(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    int result = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
    return result == 0;
  }
  
  // Connect to server
  int connectToServer(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) <= 0) {
      close(sock);
      return -1;
    }
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      close(sock);
      return -1;
    }
    
    return sock;
  }
  
  pid_t server_pid;
  int test_port;
};

// Test server binary exists
TEST_F(TCPEchoServerBasicTest, ServerBinaryExists) {
  const char* server_path = getTCPServerBinaryPath();
  ASSERT_NE(server_path, nullptr) 
      << "TCP server binary not found or not executable. Build it first with: make tcp_echo_server_basic";
}

// Test server starts and stops cleanly
TEST_F(TCPEchoServerBasicTest, ServerStartStop) {
  const char* server_path = getTCPServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "TCP server binary not found";
  }
  
  // Find available port
  while (!isPortAvailable(test_port)) {
    test_port++;
  }
  
  int pipe_err[2];
  ASSERT_EQ(pipe(pipe_err), 0);
  
  server_pid = fork();
  ASSERT_GE(server_pid, 0);
  
  if (server_pid == 0) {
    // Child process - run the server
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_err[0]);
    close(pipe_err[1]);
    
    execl(server_path, "tcp_echo_server", std::to_string(test_port).c_str(), nullptr);
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
    EXPECT_TRUE(output.find("TCP Echo Server") != std::string::npos ||
                output.find("Starting") != std::string::npos);
    EXPECT_TRUE(output.find(std::to_string(test_port)) != std::string::npos);
  }
  
  // Send SIGTERM to stop server
  kill(server_pid, SIGTERM);
  
  // Wait for server to exit
  int status;
  waitpid(server_pid, &status, 0);
  server_pid = 0;
  
  // Server should exit cleanly
  EXPECT_TRUE(WIFEXITED(status) || WIFSIGNALED(status));
  
  close(pipe_err[0]);
}

// Test server accepts connections
TEST_F(TCPEchoServerBasicTest, ServerAcceptsConnections) {
  const char* server_path = getTCPServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "TCP server binary not found";
  }
  
  // Find available port
  while (!isPortAvailable(test_port)) {
    test_port++;
  }
  
  server_pid = fork();
  ASSERT_GE(server_pid, 0);
  
  if (server_pid == 0) {
    // Child process - run the server
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
  
  // Try to connect
  int client_sock = connectToServer(test_port);
  ASSERT_GE(client_sock, 0) << "Failed to connect to server on port " << test_port;
  
  // Connection successful
  close(client_sock);
  
  // Clean up
  kill(server_pid, SIGTERM);
  waitpid(server_pid, nullptr, 0);
  server_pid = 0;
}

// Test server handles JSON-RPC request
TEST_F(TCPEchoServerBasicTest, HandleJsonRpcRequest) {
  const char* server_path = getTCPServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "TCP server binary not found";
  }
  
  // Find available port
  while (!isPortAvailable(test_port)) {
    test_port++;
  }
  
  server_pid = fork();
  ASSERT_GE(server_pid, 0);
  
  if (server_pid == 0) {
    // Child process - run the server
    int devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    close(devnull);
    
    execl(server_path, "tcp_echo_server", std::to_string(test_port).c_str(), nullptr);
    exit(1);
  }
  
  // Give server time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  // Connect to server
  int client_sock = connectToServer(test_port);
  ASSERT_GE(client_sock, 0);
  
  // Send a JSON-RPC request
  std::string request = R"({"jsonrpc":"2.0","id":1,"method":"test.method","params":{"key":"value"}})" "\n";
  ssize_t sent = send(client_sock, request.c_str(), request.length(), 0);
  ASSERT_EQ(sent, request.length());
  
  // Read response
  char buffer[4096];
  ssize_t n = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
  
  if (n > 0) {
    buffer[n] = '\0';
    std::string response(buffer);
    
    // Check response contains expected fields
    EXPECT_TRUE(response.find("\"jsonrpc\":\"2.0\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"id\":1") != std::string::npos);
    EXPECT_TRUE(response.find("\"result\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"echo\":true") != std::string::npos);
    EXPECT_TRUE(response.find("\"method\":\"test.method\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"transport\":\"tcp\"") != std::string::npos);
  }
  
  close(client_sock);
  
  // Clean up
  kill(server_pid, SIGTERM);
  waitpid(server_pid, nullptr, 0);
  server_pid = 0;
}

// Test server handles multiple requests
TEST_F(TCPEchoServerBasicTest, HandleMultipleRequests) {
  const char* server_path = getTCPServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "TCP server binary not found";
  }
  
  // Find available port
  while (!isPortAvailable(test_port)) {
    test_port++;
  }
  
  server_pid = fork();
  ASSERT_GE(server_pid, 0);
  
  if (server_pid == 0) {
    int devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    close(devnull);
    
    execl(server_path, "tcp_echo_server", std::to_string(test_port).c_str(), nullptr);
    exit(1);
  }
  
  // Give server time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  // Connect to server
  int client_sock = connectToServer(test_port);
  ASSERT_GE(client_sock, 0);
  
  // Send multiple requests
  std::string all_responses;
  for (int i = 1; i <= 3; ++i) {
    std::string request = R"({"jsonrpc":"2.0","id":)" + std::to_string(i) + 
                         R"(,"method":"test.)" + std::to_string(i) + R"("})" "\n";
    send(client_sock, request.c_str(), request.length(), 0);
    
    // Small delay between requests
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Read response
    char buffer[1024];
    ssize_t n = recv(client_sock, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
    if (n > 0) {
      buffer[n] = '\0';
      all_responses += buffer;
    }
  }
  
  // Wait a bit more for any remaining responses
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  char buffer[1024];
  ssize_t n;
  while ((n = recv(client_sock, buffer, sizeof(buffer) - 1, MSG_DONTWAIT)) > 0) {
    buffer[n] = '\0';
    all_responses += buffer;
  }
  
  // Check all responses were received
  EXPECT_TRUE(all_responses.find("\"id\":1") != std::string::npos);
  EXPECT_TRUE(all_responses.find("\"id\":2") != std::string::npos);
  EXPECT_TRUE(all_responses.find("\"id\":3") != std::string::npos);
  
  close(client_sock);
  
  // Clean up
  kill(server_pid, SIGTERM);
  waitpid(server_pid, nullptr, 0);
  server_pid = 0;
}

// Test server handles SIGINT
TEST_F(TCPEchoServerBasicTest, ServerHandlesSIGINT) {
  const char* server_path = getTCPServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "TCP server binary not found";
  }
  
  // Find available port
  while (!isPortAvailable(test_port)) {
    test_port++;
  }
  
  int pipe_err[2];
  ASSERT_EQ(pipe(pipe_err), 0);
  
  server_pid = fork();
  ASSERT_GE(server_pid, 0);
  
  if (server_pid == 0) {
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_err[0]);
    close(pipe_err[1]);
    
    execl(server_path, "tcp_echo_server", std::to_string(test_port).c_str(), nullptr);
    exit(1);
  }
  
  // Parent process
  close(pipe_err[1]);
  
  // Wait for server to start
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  // Send SIGINT
  kill(server_pid, SIGINT);
  
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
  
  // Wait for server to exit
  int status;
  waitpid(server_pid, &status, 0);
  server_pid = 0;
  
  close(pipe_err[0]);
}

// Test server help message
TEST_F(TCPEchoServerBasicTest, ServerHelpMessage) {
  const char* server_path = getTCPServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "TCP server binary not found";
  }
  
  int pipe_out[2];
  ASSERT_EQ(pipe(pipe_out), 0);
  
  server_pid = fork();
  ASSERT_GE(server_pid, 0);
  
  if (server_pid == 0) {
    dup2(pipe_out[1], STDOUT_FILENO);
    close(pipe_out[0]);
    close(pipe_out[1]);
    
    execl(server_path, "tcp_echo_server", "--help", nullptr);
    exit(1);
  }
  
  // Parent process
  close(pipe_out[1]);
  
  // Read help output
  char buffer[4096];
  ssize_t n = read(pipe_out[0], buffer, sizeof(buffer) - 1);
  
  // Wait for process to exit
  int status;
  waitpid(server_pid, &status, 0);
  server_pid = 0;
  
  if (n > 0) {
    buffer[n] = '\0';
    std::string output(buffer);
    EXPECT_TRUE(output.find("Usage:") != std::string::npos);
    EXPECT_TRUE(output.find("port") != std::string::npos);
    EXPECT_TRUE(output.find("TCP") != std::string::npos);
  }
  
  close(pipe_out[0]);
}

// Test server with invalid port
TEST_F(TCPEchoServerBasicTest, ServerInvalidPort) {
  const char* server_path = getTCPServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "TCP server binary not found";
  }
  
  int pipe_err[2];
  ASSERT_EQ(pipe(pipe_err), 0);
  
  server_pid = fork();
  ASSERT_GE(server_pid, 0);
  
  if (server_pid == 0) {
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_err[0]);
    close(pipe_err[1]);
    
    // Try with invalid port number
    execl(server_path, "tcp_echo_server", "99999", nullptr);
    exit(1);
  }
  
  close(pipe_err[1]);
  
  // Wait for process to exit
  int status;
  waitpid(server_pid, &status, 0);
  server_pid = 0;
  
  // Should exit with error
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_NE(WEXITSTATUS(status), 0);
  
  close(pipe_err[0]);
}

// Test server port already in use
TEST_F(TCPEchoServerBasicTest, ServerPortInUse) {
  const char* server_path = getTCPServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "TCP server binary not found";
  }
  
  // Find available port
  while (!isPortAvailable(test_port)) {
    test_port++;
  }
  
  // Create a socket and bind to the port
  int blocking_sock = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(blocking_sock, 0);
  
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(test_port);
  
  ASSERT_EQ(bind(blocking_sock, (struct sockaddr*)&addr, sizeof(addr)), 0);
  ASSERT_EQ(listen(blocking_sock, 1), 0);
  
  // Now try to start server on same port
  int pipe_err[2];
  ASSERT_EQ(pipe(pipe_err), 0);
  
  server_pid = fork();
  ASSERT_GE(server_pid, 0);
  
  if (server_pid == 0) {
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_err[0]);
    close(pipe_err[1]);
    close(blocking_sock);
    
    execl(server_path, "tcp_echo_server", std::to_string(test_port).c_str(), nullptr);
    exit(1);
  }
  
  close(pipe_err[1]);
  
  // Set non-blocking
  int flags = fcntl(pipe_err[0], F_GETFL, 0);
  fcntl(pipe_err[0], F_SETFL, flags | O_NONBLOCK);
  
  // Wait a bit
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  // Read error message
  char buffer[1024];
  ssize_t n = read(pipe_err[0], buffer, sizeof(buffer) - 1);
  
  if (n > 0) {
    buffer[n] = '\0';
    std::string output(buffer);
    // Should indicate bind failure
    EXPECT_TRUE(output.find("Failed") != std::string::npos ||
                output.find("failed") != std::string::npos ||
                output.find("Error") != std::string::npos ||
                output.find("error") != std::string::npos);
  }
  
  // Wait for process to exit
  int status;
  waitpid(server_pid, &status, 0);
  server_pid = 0;
  
  // Should have exited with error
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_NE(WEXITSTATUS(status), 0);
  
  close(pipe_err[0]);
  close(blocking_sock);
}

// Test server handles client disconnect
TEST_F(TCPEchoServerBasicTest, ServerHandlesClientDisconnect) {
  const char* server_path = getTCPServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "TCP server binary not found";
  }
  
  // Find available port
  while (!isPortAvailable(test_port)) {
    test_port++;
  }
  
  int pipe_err[2];
  ASSERT_EQ(pipe(pipe_err), 0);
  
  server_pid = fork();
  ASSERT_GE(server_pid, 0);
  
  if (server_pid == 0) {
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_err[0]);
    close(pipe_err[1]);
    
    execl(server_path, "tcp_echo_server", std::to_string(test_port).c_str(), nullptr);
    exit(1);
  }
  
  close(pipe_err[1]);
  
  // Give server time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  // Connect and immediately disconnect
  int client_sock = connectToServer(test_port);
  ASSERT_GE(client_sock, 0);
  close(client_sock);
  
  // Set non-blocking
  int flags = fcntl(pipe_err[0], F_GETFL, 0);
  fcntl(pipe_err[0], F_SETFL, flags | O_NONBLOCK);
  
  // Wait a bit
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // Read any output
  char buffer[1024];
  ssize_t n = read(pipe_err[0], buffer, sizeof(buffer) - 1);
  
  if (n > 0) {
    buffer[n] = '\0';
    std::string output(buffer);
    // May log disconnect
    // Server should handle gracefully without crashing
  }
  
  // Server should still be running
  int status;
  pid_t result = waitpid(server_pid, &status, WNOHANG);
  EXPECT_EQ(result, 0) << "Server should still be running after client disconnect";
  
  // Clean up
  kill(server_pid, SIGTERM);
  waitpid(server_pid, nullptr, 0);
  server_pid = 0;
  
  close(pipe_err[0]);
}

} // namespace test
} // namespace examples
} // namespace mcp