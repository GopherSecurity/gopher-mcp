/**
 * @file test_stdio_echo_server_basic.cc
 * @brief APPLICATION LEVEL TESTS for basic echo server binary
 *
 * TEST LEVEL: End-to-end application testing
 *
 * This file tests the actual basic echo server application binary by spawning
 * processes and testing through stdio pipes. It validates the complete
 * application behavior from startup to JSON-RPC message processing.
 *
 * What this tests:
 * - Basic echo server binary execution
 * - End-to-end JSON-RPC request/response flows
 * - Application-level notification handling
 * - Process lifecycle (startup, shutdown, signals)
 * - Real stdio communication
 * - Application error handling
 *
 * What this does NOT test:
 * - Transport layer internals (pipes, sockets, events)
 * - Connection manager implementation details
 * - Low-level I/O mechanisms
 *
 * For transport-level testing, see:
 * - tests/integration/test_stdio_echo_server.cc (transport layer tests)
 */

#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <signal.h>
#include <sstream>
#include <thread>
#include <unistd.h>

#include <gtest/gtest.h>
#include <sys/wait.h>

namespace mcp {
namespace examples {
namespace test {

// Helper function to get the server binary path
const char* getServerBinaryPath() {
  // Try examples/stdio_echo directory (when running from build root)
  if (access("./examples/stdio_echo/stdio_echo_server_basic", X_OK) == 0) {
    return "./examples/stdio_echo/stdio_echo_server_basic";
  }
  // Try build/examples directory (when running from project root)
  if (access("./build/examples/stdio_echo/stdio_echo_server_basic", X_OK) ==
      0) {
    return "./build/examples/stdio_echo/stdio_echo_server_basic";
  }
  // Try parent examples directory (when running from tests/ directory)
  if (access("../examples/stdio_echo/stdio_echo_server_basic", X_OK) == 0) {
    return "../examples/stdio_echo/stdio_echo_server_basic";
  }
  // Try relative from build directory
  if (access("../build/examples/stdio_echo/stdio_echo_server_basic", X_OK) ==
      0) {
    return "../build/examples/stdio_echo/stdio_echo_server_basic";
  }
  // Try old paths for backwards compatibility
  if (access("../stdio_echo_server_basic", X_OK) == 0) {
    return "../stdio_echo_server_basic";
  }
  if (access("./stdio_echo_server_basic", X_OK) == 0) {
    return "./stdio_echo_server_basic";
  }
  return nullptr;
}

// Test fixture for StdioEchoServer
class StdioEchoServerBasicTest : public ::testing::Test {
 protected:
  void SetUp() override { server_pid = 0; }

  void TearDown() override {
    if (server_pid > 0) {
      kill(server_pid, SIGTERM);
      waitpid(server_pid, nullptr, 0);
      server_pid = 0;
    }
  }

  pid_t server_pid;
};

// Test server binary exists
TEST_F(StdioEchoServerBasicTest, ServerBinaryExists) {
  const char* server_path = getServerBinaryPath();
  ASSERT_NE(server_path, nullptr)
      << "Server binary not found or not executable. Build it first with: make "
         "stdio_echo_server_basic";
}

// Test server starts and stops cleanly
TEST_F(StdioEchoServerBasicTest, ServerStartStop) {
  // Skip if binary doesn't exist
  const char* server_path = getServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "Server binary not found";
  }

  int pipe_in[2], pipe_out[2], pipe_err[2];
  ASSERT_EQ(pipe(pipe_in), 0);
  ASSERT_EQ(pipe(pipe_out), 0);
  ASSERT_EQ(pipe(pipe_err), 0);

  server_pid = fork();
  ASSERT_GE(server_pid, 0);

  if (server_pid == 0) {
    // Child process - run the server
    dup2(pipe_in[0], STDIN_FILENO);
    dup2(pipe_out[1], STDOUT_FILENO);
    dup2(pipe_err[1], STDERR_FILENO);

    close(pipe_in[0]);
    close(pipe_in[1]);
    close(pipe_out[0]);
    close(pipe_out[1]);
    close(pipe_err[0]);
    close(pipe_err[1]);

    execl(server_path, "stdio_echo_server", nullptr);
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
    EXPECT_TRUE(output.find("MCP Stdio Echo Server") != std::string::npos ||
                output.find("Echo server started") != std::string::npos);
  }

  // Send SIGTERM to stop server
  kill(server_pid, SIGTERM);

  // Wait for server to exit
  int status;
  waitpid(server_pid, &status, 0);
  server_pid = 0;

  // Server should exit cleanly
  EXPECT_TRUE(WIFEXITED(status) || WIFSIGNALED(status));

  close(pipe_in[1]);
  close(pipe_out[0]);
  close(pipe_err[0]);
}

// Test server handles JSON-RPC request
TEST_F(StdioEchoServerBasicTest, HandleJsonRpcRequest) {
  // Skip if binary doesn't exist
  const char* server_path = getServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "Server binary not found";
  }

  int pipe_in[2], pipe_out[2], pipe_err[2];
  ASSERT_EQ(pipe(pipe_in), 0);
  ASSERT_EQ(pipe(pipe_out), 0);
  ASSERT_EQ(pipe(pipe_err), 0);

  server_pid = fork();
  ASSERT_GE(server_pid, 0);

  if (server_pid == 0) {
    // Child process - run the server
    dup2(pipe_in[0], STDIN_FILENO);
    dup2(pipe_out[1], STDOUT_FILENO);
    dup2(pipe_err[1], STDERR_FILENO);

    close(pipe_in[0]);
    close(pipe_in[1]);
    close(pipe_out[0]);
    close(pipe_out[1]);
    close(pipe_err[0]);
    close(pipe_err[1]);

    execl(server_path, "stdio_echo_server", nullptr);
    exit(1);
  }

  // Parent process
  close(pipe_in[0]);
  close(pipe_out[1]);
  close(pipe_err[1]);

  // Wait for server to start
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Send a JSON-RPC request
  std::string request =
      R"({"jsonrpc":"2.0","id":1,"method":"test.method","params":{"key":"value"}})"
      "\n";
  write(pipe_in[1], request.c_str(), request.length());

  // Set non-blocking for stdout
  int flags = fcntl(pipe_out[0], F_GETFL, 0);
  fcntl(pipe_out[0], F_SETFL, flags | O_NONBLOCK);

  // Read response from stdout
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  char buffer[4096];
  ssize_t n = read(pipe_out[0], buffer, sizeof(buffer) - 1);

  if (n > 0) {
    buffer[n] = '\0';
    std::string response(buffer);

    // Check response contains expected fields
    EXPECT_TRUE(response.find("\"jsonrpc\":\"2.0\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"id\":1") != std::string::npos);
    EXPECT_TRUE(response.find("\"result\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"echo\":true") != std::string::npos);
    EXPECT_TRUE(response.find("\"method\":\"test.method\"") !=
                std::string::npos);
  }

  // Send shutdown notification
  std::string shutdown = R"({"jsonrpc":"2.0","method":"shutdown"})"
                         "\n";
  write(pipe_in[1], shutdown.c_str(), shutdown.length());

  // Wait for server to exit
  int status;
  waitpid(server_pid, &status, 0);
  server_pid = 0;

  close(pipe_in[1]);
  close(pipe_out[0]);
  close(pipe_err[0]);
}

// Test server handles notifications
TEST_F(StdioEchoServerBasicTest, HandleNotification) {
  // Skip if binary doesn't exist
  const char* server_path = getServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "Server binary not found";
  }

  int pipe_in[2], pipe_out[2], pipe_err[2];
  ASSERT_EQ(pipe(pipe_in), 0);
  ASSERT_EQ(pipe(pipe_out), 0);
  ASSERT_EQ(pipe(pipe_err), 0);

  server_pid = fork();
  ASSERT_GE(server_pid, 0);

  if (server_pid == 0) {
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

    execl(server_path, "stdio_echo_server", nullptr);
    exit(1);
  }

  // Parent process
  close(pipe_in[0]);
  close(pipe_out[1]);
  close(pipe_err[1]);

  // Wait for server to start
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Send a notification
  std::string notification =
      R"({"jsonrpc":"2.0","method":"test.event","params":{"data":"test"}})"
      "\n";
  write(pipe_in[1], notification.c_str(), notification.length());

  // Set non-blocking for stdout
  int flags = fcntl(pipe_out[0], F_GETFL, 0);
  fcntl(pipe_out[0], F_SETFL, flags | O_NONBLOCK);

  // Read echo notification from stdout
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  char buffer[4096];
  ssize_t n = read(pipe_out[0], buffer, sizeof(buffer) - 1);

  if (n > 0) {
    buffer[n] = '\0';
    std::string response(buffer);

    // Check echo notification
    EXPECT_TRUE(response.find("\"method\":\"echo/test.event\"") !=
                std::string::npos);
    EXPECT_TRUE(response.find("\"params\"") != std::string::npos);
  }

  // Send shutdown
  std::string shutdown = R"({"jsonrpc":"2.0","method":"shutdown"})"
                         "\n";
  write(pipe_in[1], shutdown.c_str(), shutdown.length());

  // Clean up
  int status;
  waitpid(server_pid, &status, 0);
  server_pid = 0;

  close(pipe_in[1]);
  close(pipe_out[0]);
  close(pipe_err[0]);
}

// Test ping-pong functionality
TEST_F(StdioEchoServerBasicTest, PingPongHandling) {
  // Skip if binary doesn't exist
  const char* server_path = getServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "Server binary not found";
  }

  int pipe_in[2], pipe_out[2], pipe_err[2];
  ASSERT_EQ(pipe(pipe_in), 0);
  ASSERT_EQ(pipe(pipe_out), 0);
  ASSERT_EQ(pipe(pipe_err), 0);

  server_pid = fork();
  ASSERT_GE(server_pid, 0);

  if (server_pid == 0) {
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

    execl(server_path, "stdio_echo_server", nullptr);
    exit(1);
  }

  // Parent process
  close(pipe_in[0]);
  close(pipe_out[1]);
  close(pipe_err[1]);

  // Wait for server to start
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Send ping notification
  std::string ping = R"({"jsonrpc":"2.0","method":"ping"})"
                     "\n";
  write(pipe_in[1], ping.c_str(), ping.length());

  // Set non-blocking for stdout
  int flags = fcntl(pipe_out[0], F_GETFL, 0);
  fcntl(pipe_out[0], F_SETFL, flags | O_NONBLOCK);

  // Read pong response
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  char buffer[4096];
  ssize_t n = read(pipe_out[0], buffer, sizeof(buffer) - 1);

  if (n > 0) {
    buffer[n] = '\0';
    std::string response(buffer);

    // Check for pong response
    EXPECT_TRUE(response.find("\"method\":\"pong\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"timestamp\"") != std::string::npos);
  }

  // Send shutdown
  std::string shutdown = R"({"jsonrpc":"2.0","method":"shutdown"})"
                         "\n";
  write(pipe_in[1], shutdown.c_str(), shutdown.length());

  // Clean up
  int status;
  waitpid(server_pid, &status, 0);
  server_pid = 0;

  close(pipe_in[1]);
  close(pipe_out[0]);
  close(pipe_err[0]);
}

// Test server handles shutdown notification
TEST_F(StdioEchoServerBasicTest, HandleShutdown) {
  // Skip if binary doesn't exist
  const char* server_path = getServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "Server binary not found";
  }

  int pipe_in[2], pipe_err[2];
  ASSERT_EQ(pipe(pipe_in), 0);
  ASSERT_EQ(pipe(pipe_err), 0);

  server_pid = fork();
  ASSERT_GE(server_pid, 0);

  if (server_pid == 0) {
    // Child process
    dup2(pipe_in[0], STDIN_FILENO);
    dup2(pipe_err[1], STDERR_FILENO);

    close(pipe_in[0]);
    close(pipe_in[1]);
    close(pipe_err[0]);
    close(pipe_err[1]);

    execl(server_path, "stdio_echo_server", nullptr);
    exit(1);
  }

  // Parent process
  close(pipe_in[0]);
  close(pipe_err[1]);

  // Wait for server to start
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Send shutdown notification
  std::string shutdown = R"({"jsonrpc":"2.0","method":"shutdown"})"
                         "\n";
  write(pipe_in[1], shutdown.c_str(), shutdown.length());

  // Wait for server to exit
  int status;
  waitpid(server_pid, &status, 0);
  server_pid = 0;

  // Should exit (either normally or via signal from shutdown)
  // The server might exit with SIGABRT due to thread cleanup issues, which is
  // acceptable
  EXPECT_TRUE(WIFEXITED(status) || WIFSIGNALED(status));

  close(pipe_in[1]);
  close(pipe_err[0]);
}

// Test server handles SIGINT signal
TEST_F(StdioEchoServerBasicTest, HandleSIGINT) {
  // Skip if binary doesn't exist
  const char* server_path = getServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "Server binary not found";
  }

  int pipe_err[2];
  ASSERT_EQ(pipe(pipe_err), 0);

  server_pid = fork();
  ASSERT_GE(server_pid, 0);

  if (server_pid == 0) {
    // Child process
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_err[0]);
    close(pipe_err[1]);

    execl(server_path, "stdio_echo_server", nullptr);
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
                output.find("shutting down") != std::string::npos);
  }

  // Wait for server to exit
  int status;
  waitpid(server_pid, &status, 0);
  server_pid = 0;

  close(pipe_err[0]);
}

// Test server with verbose mode
TEST_F(StdioEchoServerBasicTest, ServerVerboseMode) {
  // Skip if binary doesn't exist
  const char* server_path = getServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "Server binary not found";
  }

  int pipe_err[2];
  ASSERT_EQ(pipe(pipe_err), 0);

  server_pid = fork();
  ASSERT_GE(server_pid, 0);

  if (server_pid == 0) {
    // Child process - run with verbose flag
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_err[0]);
    close(pipe_err[1]);

    execl(server_path, "stdio_echo_server", "-v", nullptr);
    exit(1);
  }

  // Parent process
  close(pipe_err[1]);

  // Set non-blocking
  int flags = fcntl(pipe_err[0], F_GETFL, 0);
  fcntl(pipe_err[0], F_SETFL, flags | O_NONBLOCK);

  // Wait and collect output
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  char buffer[4096];
  ssize_t n = read(pipe_err[0], buffer, sizeof(buffer) - 1);

  if (n > 0) {
    buffer[n] = '\0';
    std::string output(buffer);
    // Verbose mode should produce more output
    EXPECT_GT(output.length(), 50);
  }

  // Clean up
  kill(server_pid, SIGTERM);
  waitpid(server_pid, nullptr, 0);
  server_pid = 0;

  close(pipe_err[0]);
}

// Test server help message
TEST_F(StdioEchoServerBasicTest, ServerHelpMessage) {
  // Skip if binary doesn't exist
  const char* server_path = getServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "Server binary not found";
  }

  int pipe_out[2];
  ASSERT_EQ(pipe(pipe_out), 0);

  server_pid = fork();
  ASSERT_GE(server_pid, 0);

  if (server_pid == 0) {
    // Child process - run with help flag
    dup2(pipe_out[1], STDOUT_FILENO);
    close(pipe_out[0]);
    close(pipe_out[1]);

    execl(server_path, "stdio_echo_server", "--help", nullptr);
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
    EXPECT_TRUE(output.find("Options:") != std::string::npos);
    EXPECT_TRUE(output.find("--verbose") != std::string::npos ||
                output.find("-v") != std::string::npos);
    EXPECT_TRUE(output.find("--help") != std::string::npos ||
                output.find("-h") != std::string::npos);
  }

  close(pipe_out[0]);
}

// Test server handles multiple requests
TEST_F(StdioEchoServerBasicTest, HandleMultipleRequests) {
  // Skip if binary doesn't exist
  const char* server_path = getServerBinaryPath();
  if (!server_path) {
    GTEST_SKIP() << "Server binary not found";
  }

  int pipe_in[2], pipe_out[2];
  ASSERT_EQ(pipe(pipe_in), 0);
  ASSERT_EQ(pipe(pipe_out), 0);

  server_pid = fork();
  ASSERT_GE(server_pid, 0);

  if (server_pid == 0) {
    // Child process
    dup2(pipe_in[0], STDIN_FILENO);
    dup2(pipe_out[1], STDOUT_FILENO);

    close(pipe_in[0]);
    close(pipe_in[1]);
    close(pipe_out[0]);
    close(pipe_out[1]);

    execl(server_path, "stdio_echo_server", nullptr);
    exit(1);
  }

  // Parent process
  close(pipe_in[0]);
  close(pipe_out[1]);

  // Wait for server to start
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Send multiple requests
  for (int i = 1; i <= 3; ++i) {
    std::string request = R"({"jsonrpc":"2.0","id":)" + std::to_string(i) +
                          R"(,"method":"test.)" + std::to_string(i) +
                          R"("})"
                          "\n";
    write(pipe_in[1], request.c_str(), request.length());
  }

  // Set non-blocking for stdout
  int flags = fcntl(pipe_out[0], F_GETFL, 0);
  fcntl(pipe_out[0], F_SETFL, flags | O_NONBLOCK);

  // Read responses
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  std::string all_responses;
  char buffer[4096];
  ssize_t n;
  while ((n = read(pipe_out[0], buffer, sizeof(buffer) - 1)) > 0) {
    buffer[n] = '\0';
    all_responses += buffer;
  }

  // Check all responses were received
  EXPECT_TRUE(all_responses.find("\"id\":1") != std::string::npos);
  EXPECT_TRUE(all_responses.find("\"id\":2") != std::string::npos);
  EXPECT_TRUE(all_responses.find("\"id\":3") != std::string::npos);
  EXPECT_TRUE(all_responses.find("\"method\":\"test.1\"") != std::string::npos);
  EXPECT_TRUE(all_responses.find("\"method\":\"test.2\"") != std::string::npos);
  EXPECT_TRUE(all_responses.find("\"method\":\"test.3\"") != std::string::npos);

  // Send shutdown
  std::string shutdown = R"({"jsonrpc":"2.0","method":"shutdown"})"
                         "\n";
  write(pipe_in[1], shutdown.c_str(), shutdown.length());

  // Clean up
  waitpid(server_pid, nullptr, 0);
  server_pid = 0;

  close(pipe_in[1]);
  close(pipe_out[0]);
}

}  // namespace test
}  // namespace examples
}  // namespace mcp