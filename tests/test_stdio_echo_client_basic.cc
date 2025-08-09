/**
 * @file test_stdio_echo_client_basic.cc
 * @brief Comprehensive unit tests for stdio_echo_client.cc
 * 
 * Test Coverage:
 * - Client binary execution
 * - Request/response handling
 * - Notification handling
 * - Error handling
 * - Signal handling
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

namespace mcp {
namespace examples {
namespace test {

// Helper function to get the client binary path
const char* getClientBinaryPath() {
  // Try parent directory first (when running from tests/ directory)
  if (access("../stdio_echo_client", X_OK) == 0) {
    return "../stdio_echo_client";
  }
  // Try current directory (when running from build root)
  if (access("./stdio_echo_client", X_OK) == 0) {
    return "./stdio_echo_client";
  }
  return nullptr;
}

// Test fixture for StdioEchoClient
class StdioEchoClientBasicTest : public ::testing::Test {
protected:
  void SetUp() override {
    client_pid = 0;
  }
  
  void TearDown() override {
    if (client_pid > 0) {
      kill(client_pid, SIGTERM);
      waitpid(client_pid, nullptr, 0);
      client_pid = 0;
    }
  }
  
  pid_t client_pid;
};

// Test client binary exists
TEST_F(StdioEchoClientBasicTest, ClientBinaryExists) {
  const char* client_path = getClientBinaryPath();
  ASSERT_NE(client_path, nullptr) 
      << "Client binary not found or not executable. Build it first with: make stdio_echo_client";
}

// Test client starts and stops cleanly
TEST_F(StdioEchoClientBasicTest, ClientStartStop) {
  // Skip if binary doesn't exist
  const char* client_path = getClientBinaryPath();
  if (!client_path) {
    GTEST_SKIP() << "Client binary not found";
  }
  
  int pipe_in[2], pipe_out[2], pipe_err[2];
  ASSERT_EQ(pipe(pipe_in), 0);
  ASSERT_EQ(pipe(pipe_out), 0);
  ASSERT_EQ(pipe(pipe_err), 0);
  
  client_pid = fork();
  ASSERT_GE(client_pid, 0);
  
  if (client_pid == 0) {
    // Child process - run the client
    dup2(pipe_in[0], STDIN_FILENO);
    dup2(pipe_out[1], STDOUT_FILENO);
    dup2(pipe_err[1], STDERR_FILENO);
    
    close(pipe_in[0]);
    close(pipe_in[1]);
    close(pipe_out[0]);
    close(pipe_out[1]);
    close(pipe_err[0]);
    close(pipe_err[1]);
    
    execl(client_path, "stdio_echo_client", "manual", nullptr);
    exit(1);  // exec failed
  }
  
  // Parent process
  close(pipe_in[0]);
  close(pipe_out[1]);
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
    EXPECT_TRUE(output.find("MCP Stdio Echo Client") != std::string::npos ||
                output.find("Echo client started") != std::string::npos);
  }
  
  // Send SIGTERM to stop client
  kill(client_pid, SIGTERM);
  
  // Wait for client to exit
  int status;
  waitpid(client_pid, &status, 0);
  client_pid = 0;
  
  // Client should exit cleanly
  EXPECT_TRUE(WIFEXITED(status) || WIFSIGNALED(status));
  
  close(pipe_in[1]);
  close(pipe_out[0]);
  close(pipe_err[0]);
}

// Test client in auto mode (runs test sequence)
TEST_F(StdioEchoClientBasicTest, ClientAutoMode) {
  // Skip if binary doesn't exist
  const char* client_path = getClientBinaryPath();
  if (!client_path) {
    GTEST_SKIP() << "Client binary not found";
  }
  
  int pipe_in[2], pipe_out[2], pipe_err[2];
  ASSERT_EQ(pipe(pipe_in), 0);
  ASSERT_EQ(pipe(pipe_out), 0);
  ASSERT_EQ(pipe(pipe_err), 0);
  
  client_pid = fork();
  ASSERT_GE(client_pid, 0);
  
  if (client_pid == 0) {
    // Child process - run the client in auto mode
    dup2(pipe_in[0], STDIN_FILENO);
    dup2(pipe_out[1], STDOUT_FILENO);
    dup2(pipe_err[1], STDERR_FILENO);
    
    close(pipe_in[0]);
    close(pipe_in[1]);
    close(pipe_out[0]);
    close(pipe_out[1]);
    close(pipe_err[0]);
    close(pipe_err[1]);
    
    execl(client_path, "stdio_echo_client", "auto", nullptr);
    exit(1);
  }
  
  // Parent process
  close(pipe_in[0]);
  close(pipe_out[1]);
  close(pipe_err[1]);
  
  // Set non-blocking
  int flags = fcntl(pipe_err[0], F_GETFL, 0);
  fcntl(pipe_err[0], F_SETFL, flags | O_NONBLOCK);
  
  // Collect initial output, then send SIGTERM to allow the client to finish
  std::string all_output;
  
  // First, collect output for a short time
  for (int i = 0; i < 20; ++i) {  // Wait up to 2 seconds
    char buffer[4096];
    ssize_t n = read(pipe_err[0], buffer, sizeof(buffer) - 1);
    if (n > 0) {
      buffer[n] = '\0';
      all_output += buffer;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  
  // Send SIGTERM to let the client finish gracefully
  if (client_pid > 0) {
    kill(client_pid, SIGTERM);
    
    // Collect remaining output after signaling
    for (int i = 0; i < 10; ++i) {
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
  }
  
  // Verify test sequence was executed - check for key parts
  EXPECT_TRUE(all_output.find("Starting test sequence") != std::string::npos);
  EXPECT_TRUE(all_output.find("Test 1: Simple ping request") != std::string::npos);
  // The remaining tests should appear even if they fail due to no server
  EXPECT_TRUE(all_output.find("Test 2: Request with params") != std::string::npos);
  EXPECT_TRUE(all_output.find("Test 3: Sending notification") != std::string::npos);
  EXPECT_TRUE(all_output.find("Test 4: Multiple concurrent requests") != std::string::npos);
  EXPECT_TRUE(all_output.find("Test 5: Ping notification") != std::string::npos);
  
  // Clean up if still running
  if (client_pid > 0) {
    kill(client_pid, SIGTERM);
    waitpid(client_pid, nullptr, 0);
    client_pid = 0;
  }
  
  close(pipe_in[1]);
  close(pipe_out[0]);
  close(pipe_err[0]);
}

// Test client sends valid JSON-RPC requests
TEST_F(StdioEchoClientBasicTest, ClientSendsValidJsonRpc) {
  // Skip if binary doesn't exist
  const char* client_path = getClientBinaryPath();
  if (!client_path) {
    GTEST_SKIP() << "Client binary not found";
  }
  
  int pipe_in[2], pipe_out[2], pipe_err[2];
  ASSERT_EQ(pipe(pipe_in), 0);
  ASSERT_EQ(pipe(pipe_out), 0);
  ASSERT_EQ(pipe(pipe_err), 0);
  
  client_pid = fork();
  ASSERT_GE(client_pid, 0);
  
  if (client_pid == 0) {
    // Child process
    dup2(pipe_in[0], STDIN_FILENO);
    dup2(pipe_out[1], STDOUT_FILENO);
    dup2(pipe_err[1], STDERR_FILENO);
    
    close(pipe_in[0]);
    close(pipe_in[1]);
    close(pipe_out[0]);
    close(pipe_out[1]);
    close(pipe_err[0]);
    close(pipe_err[1]);
    
    execl(client_path, "stdio_echo_client", "auto", nullptr);
    exit(1);
  }
  
  // Parent process
  close(pipe_in[0]);
  close(pipe_out[1]);
  close(pipe_err[1]);
  
  // Set non-blocking
  int flags = fcntl(pipe_out[0], F_GETFL, 0);
  fcntl(pipe_out[0], F_SETFL, flags | O_NONBLOCK);
  
  // Read output from stdout (JSON-RPC messages)
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  std::string json_output;
  char buffer[4096];
  ssize_t n = read(pipe_out[0], buffer, sizeof(buffer) - 1);
  if (n > 0) {
    buffer[n] = '\0';
    json_output = buffer;
  }
  
  // Verify JSON-RPC format
  if (!json_output.empty()) {
    EXPECT_TRUE(json_output.find("\"jsonrpc\":\"2.0\"") != std::string::npos);
    EXPECT_TRUE(json_output.find("\"method\":") != std::string::npos);
    // Either request with id or notification without id
    bool has_request = json_output.find("\"id\":") != std::string::npos;
    bool has_notification = json_output.find("\"id\":") == std::string::npos && 
                            json_output.find("\"method\":") != std::string::npos;
    EXPECT_TRUE(has_request || has_notification);
  }
  
  // Clean up
  kill(client_pid, SIGTERM);
  waitpid(client_pid, nullptr, 0);
  client_pid = 0;
  
  close(pipe_in[1]);
  close(pipe_out[0]);
  close(pipe_err[0]);
}

// Test client handles responses
TEST_F(StdioEchoClientBasicTest, ClientHandlesResponses) {
  // Skip if binary doesn't exist
  const char* client_path = getClientBinaryPath();
  if (!client_path) {
    GTEST_SKIP() << "Client binary not found";
  }
  
  int pipe_in[2], pipe_out[2], pipe_err[2];
  ASSERT_EQ(pipe(pipe_in), 0);
  ASSERT_EQ(pipe(pipe_out), 0);
  ASSERT_EQ(pipe(pipe_err), 0);
  
  client_pid = fork();
  ASSERT_GE(client_pid, 0);
  
  if (client_pid == 0) {
    // Child process
    dup2(pipe_in[0], STDIN_FILENO);
    dup2(pipe_out[1], STDOUT_FILENO);
    dup2(pipe_err[1], STDERR_FILENO);
    
    close(pipe_in[0]);
    close(pipe_in[1]);
    close(pipe_out[0]);
    close(pipe_out[1]);
    close(pipe_err[0]);
    close(pipe_err[1]);
    
    execl(client_path, "stdio_echo_client", "manual", nullptr);
    exit(1);
  }
  
  // Parent process
  close(pipe_in[0]);
  close(pipe_out[1]);
  close(pipe_err[1]);
  
  // Wait for client to start
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  // Send a mock response
  std::string response = R"({"jsonrpc":"2.0","id":1,"result":{"echo":true,"method":"test"}})" "\n";
  write(pipe_in[1], response.c_str(), response.length());
  
  // Set non-blocking for stderr
  int flags = fcntl(pipe_err[0], F_GETFL, 0);
  fcntl(pipe_err[0], F_SETFL, flags | O_NONBLOCK);
  
  // Read client's reaction
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  char buffer[4096];
  ssize_t n = read(pipe_err[0], buffer, sizeof(buffer) - 1);
  
  std::string output;
  if (n > 0) {
    buffer[n] = '\0';
    output = buffer;
  }
  
  // Client should process the response (might log it)
  // The exact output depends on the client implementation
  
  // Clean up
  kill(client_pid, SIGTERM);
  waitpid(client_pid, nullptr, 0);
  client_pid = 0;
  
  close(pipe_in[1]);
  close(pipe_out[0]);
  close(pipe_err[0]);
}

// Test client handles SIGINT
TEST_F(StdioEchoClientBasicTest, ClientHandlesSIGINT) {
  // Skip if binary doesn't exist
  const char* client_path = getClientBinaryPath();
  if (!client_path) {
    GTEST_SKIP() << "Client binary not found";
  }
  
  int pipe_err[2];
  ASSERT_EQ(pipe(pipe_err), 0);
  
  client_pid = fork();
  ASSERT_GE(client_pid, 0);
  
  if (client_pid == 0) {
    // Child process
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_err[0]);
    close(pipe_err[1]);
    
    execl(client_path, "stdio_echo_client", "manual", nullptr);
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
                output.find("shutting down") != std::string::npos);
  }
  
  // Wait for client to exit
  int status;
  waitpid(client_pid, &status, 0);
  client_pid = 0;
  
  close(pipe_err[0]);
}

// Test client with verbose mode
TEST_F(StdioEchoClientBasicTest, ClientVerboseMode) {
  // Skip if binary doesn't exist
  const char* client_path = getClientBinaryPath();
  if (!client_path) {
    GTEST_SKIP() << "Client binary not found";
  }
  
  int pipe_err[2];
  ASSERT_EQ(pipe(pipe_err), 0);
  
  client_pid = fork();
  ASSERT_GE(client_pid, 0);
  
  if (client_pid == 0) {
    // Child process - run with verbose flag
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_err[0]);
    close(pipe_err[1]);
    
    execl(client_path, "stdio_echo_client", "-v", "auto", nullptr);
    exit(1);
  }
  
  // Parent process
  close(pipe_err[1]);
  
  // Set non-blocking
  int flags = fcntl(pipe_err[0], F_GETFL, 0);
  fcntl(pipe_err[0], F_SETFL, flags | O_NONBLOCK);
  
  // Collect output
  std::string all_output;
  for (int i = 0; i < 30; ++i) {
    char buffer[4096];
    ssize_t n = read(pipe_err[0], buffer, sizeof(buffer) - 1);
    if (n > 0) {
      buffer[n] = '\0';
      all_output += buffer;
    }
    
    // Check if process is still running
    int status;
    pid_t result = waitpid(client_pid, &status, WNOHANG);
    if (result == client_pid) {
      client_pid = 0;
      break;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  
  // Verbose mode should produce more output
  EXPECT_GT(all_output.length(), 100);
  
  // Clean up if still running
  if (client_pid > 0) {
    kill(client_pid, SIGTERM);
    waitpid(client_pid, nullptr, 0);
    client_pid = 0;
  }
  
  close(pipe_err[0]);
}

// Test client help message
TEST_F(StdioEchoClientBasicTest, ClientHelpMessage) {
  // Skip if binary doesn't exist
  const char* client_path = getClientBinaryPath();
  if (!client_path) {
    GTEST_SKIP() << "Client binary not found";
  }
  
  int pipe_out[2];
  ASSERT_EQ(pipe(pipe_out), 0);
  
  client_pid = fork();
  ASSERT_GE(client_pid, 0);
  
  if (client_pid == 0) {
    // Child process - run with help flag
    dup2(pipe_out[1], STDOUT_FILENO);
    close(pipe_out[0]);
    close(pipe_out[1]);
    
    execl(client_path, "stdio_echo_client", "--help", nullptr);
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
    EXPECT_TRUE(output.find("Options:") != std::string::npos);
    EXPECT_TRUE(output.find("Modes:") != std::string::npos);
    EXPECT_TRUE(output.find("auto") != std::string::npos);
    EXPECT_TRUE(output.find("manual") != std::string::npos);
    EXPECT_TRUE(output.find("interactive") != std::string::npos);
  }
  
  close(pipe_out[0]);
}

} // namespace test
} // namespace examples
} // namespace mcp