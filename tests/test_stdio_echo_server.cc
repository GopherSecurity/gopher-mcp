#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mcp/transport/stdio_transport_socket.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/socket_interface_impl.h"
#include "mcp/buffer.h"
#include "mcp/json_serialization.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <future>

namespace mcp {
namespace test {

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::AtLeast;

/**
 * Test fixture for stdio echo server tests
 */
class StdioEchoServerTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create pipes for bidirectional communication
    ASSERT_EQ(0, pipe(server_stdin_pipe_));
    ASSERT_EQ(0, pipe(server_stdout_pipe_));
    ASSERT_EQ(0, pipe(client_stdin_pipe_));
    ASSERT_EQ(0, pipe(client_stdout_pipe_));
    
    // Make pipes non-blocking
    fcntl(server_stdin_pipe_[0], F_SETFL, O_NONBLOCK);
    fcntl(server_stdout_pipe_[1], F_SETFL, O_NONBLOCK);
    fcntl(client_stdin_pipe_[0], F_SETFL, O_NONBLOCK);
    fcntl(client_stdout_pipe_[1], F_SETFL, O_NONBLOCK);
    
    // Create dispatcher
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
  }
  
  void TearDown() override {
    // Close all pipes
    for (int fd : {server_stdin_pipe_[0], server_stdin_pipe_[1],
                   server_stdout_pipe_[0], server_stdout_pipe_[1],
                   client_stdin_pipe_[0], client_stdin_pipe_[1],
                   client_stdout_pipe_[0], client_stdout_pipe_[1]}) {
      if (fd >= 0) close(fd);
    }
  }
  
  /**
   * Mock echo server for testing
   */
  class MockEchoServer : public McpMessageCallbacks {
  public:
    MockEchoServer(event::Dispatcher& dispatcher,
                   int stdin_fd, int stdout_fd)
        : dispatcher_(dispatcher) {
      // Configure for test pipes
      McpConnectionConfig config;
      config.transport_type = TransportType::Stdio;
      config.stdio_config = transport::StdioTransportSocketConfig{
          .stdin_fd = stdin_fd,
          .stdout_fd = stdout_fd,
          .non_blocking = true
      };
      config.use_message_framing = false; // Simplify for testing
      
      socket_interface_ = std::make_unique<network::SocketInterfaceImpl>();
      connection_manager_ = std::make_unique<McpConnectionManager>(
          dispatcher_, *socket_interface_, config);
      connection_manager_->setMessageCallbacks(*this);
    }
    
    bool start() {
      // Defer connection to dispatcher thread to ensure thread safety
      // The connection must be established from within the dispatcher thread
      std::promise<bool> connected_promise;
      auto connected_future = connected_promise.get_future();
      
      dispatcher_.post([this, &connected_promise]() {
        auto result = connection_manager_->connect();
        connected_promise.set_value(!holds_alternative<Error>(result));
      });
      
      // Process the posted task
      dispatcher_.run(event::RunType::NonBlock);
      
      // Wait for connection result
      if (connected_future.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
        return connected_future.get();
      }
      return false;
    }
    
    void stop() {
      connection_manager_->close();
    }
    
    // McpMessageCallbacks
    void onRequest(const jsonrpc::Request& request) override {
      request_count_++;
      last_request_ = request;
      
      // Echo response
      jsonrpc::Response response;
      response.id = request.id;
      
      Metadata result;
      add_metadata(result, "echo", true);
      add_metadata(result, "method", request.method);
      
      if (request.params.has_value()) {
        add_metadata(result, "has_params", true);
      }
      
      response.result = make_optional(jsonrpc::ResponseResult(result));
      connection_manager_->sendResponse(response);
    }
    
    void onNotification(const jsonrpc::Notification& notification) override {
      notification_count_++;
      last_notification_ = notification;
      
      if (notification.method == "shutdown") {
        dispatcher_.post([this]() { stop(); });
        return;
      }
      
      // Echo notification
      jsonrpc::Notification echo;
      echo.method = "echo/" + notification.method;
      
      Metadata params;
      add_metadata(params, "original", notification.method);
      echo.params = make_optional(params);
      
      connection_manager_->sendNotification(echo);
    }
    
    void onResponse(const jsonrpc::Response& response) override {
      response_count_++;
      last_response_ = response;
    }
    
    void onConnectionEvent(network::ConnectionEvent event) override {
      last_event_ = event;
      if (event == network::ConnectionEvent::RemoteClose) {
        dispatcher_.post([this]() { stop(); });
      }
    }
    
    void onError(const Error& error) override {
      error_count_++;
      last_error_ = error;
    }
    
    // Test accessors
    int getRequestCount() const { return request_count_; }
    int getNotificationCount() const { return notification_count_; }
    int getResponseCount() const { return response_count_; }
    int getErrorCount() const { return error_count_; }
    
    const optional<jsonrpc::Request>& getLastRequest() const { 
      return last_request_; 
    }
    const optional<jsonrpc::Notification>& getLastNotification() const { 
      return last_notification_; 
    }
    const optional<jsonrpc::Response>& getLastResponse() const { 
      return last_response_; 
    }
    const optional<Error>& getLastError() const { 
      return last_error_; 
    }
    const optional<network::ConnectionEvent>& getLastEvent() const {
      return last_event_;
    }
    
  private:
    event::Dispatcher& dispatcher_;
    std::unique_ptr<network::SocketInterface> socket_interface_;
    std::unique_ptr<McpConnectionManager> connection_manager_;
    
    std::atomic<int> request_count_{0};
    std::atomic<int> notification_count_{0};
    std::atomic<int> response_count_{0};
    std::atomic<int> error_count_{0};
    
    optional<jsonrpc::Request> last_request_;
    optional<jsonrpc::Notification> last_notification_;
    optional<jsonrpc::Response> last_response_;
    optional<Error> last_error_;
    optional<network::ConnectionEvent> last_event_;
  };
  
  /**
   * Write JSON-RPC message to pipe
   */
  void writeMessage(int fd, const std::string& json) {
    // Write with newline delimiter
    std::string message = json + "\n";
    ssize_t written = 0;
    while (written < static_cast<ssize_t>(message.size())) {
      ssize_t n = write(fd, message.c_str() + written, message.size() - written);
      if (n > 0) {
        written += n;
      } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        break;
      }
    }
  }
  
  /**
   * Read JSON-RPC message from pipe
   */
  std::string readMessage(int fd, int timeout_ms = 1000) {
    std::string buffer;
    char c;
    auto start = std::chrono::steady_clock::now();
    
    while (true) {
      ssize_t n = read(fd, &c, 1);
      if (n > 0) {
        if (c == '\n') {
          return buffer;
        }
        buffer += c;
      } else if (n == 0) {
        // EOF
        return buffer;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Check timeout
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout_ms) {
          return buffer;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      } else {
        return "";
      }
    }
  }
  
  std::unique_ptr<event::Dispatcher> dispatcher_;
  int server_stdin_pipe_[2];
  int server_stdout_pipe_[2];
  int client_stdin_pipe_[2];
  int client_stdout_pipe_[2];
};

// Test basic server startup and shutdown
TEST_F(StdioEchoServerTest, StartupShutdown) {
  MockEchoServer server(*dispatcher_,
                       server_stdin_pipe_[0],
                       server_stdout_pipe_[1]);
  
  // Start server (connection happens in dispatcher thread)
  EXPECT_TRUE(server.start());
  
  // Run dispatcher briefly to process any remaining events
  for (int i = 0; i < 3; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  server.stop();
  
  // Verify no errors
  EXPECT_EQ(0, server.getErrorCount());
}

// Test request echo
TEST_F(StdioEchoServerTest, RequestEcho) {
  MockEchoServer server(*dispatcher_,
                       server_stdin_pipe_[0],
                       server_stdout_pipe_[1]);
  
  ASSERT_TRUE(server.start());
  
  // Send request
  std::string request_json = R"({"jsonrpc":"2.0","id":1,"method":"test.echo","params":{"message":"hello"}})";
  writeMessage(server_stdin_pipe_[1], request_json);
  
  // Process messages
  for (int i = 0; i < 10; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  // Verify request was received
  EXPECT_EQ(1, server.getRequestCount());
  ASSERT_TRUE(server.getLastRequest().has_value());
  EXPECT_EQ("test.echo", server.getLastRequest()->method);
  
  // Read response
  std::string response = readMessage(server_stdout_pipe_[0]);
  EXPECT_FALSE(response.empty());
  
  // Parse and verify response
  auto response_json = json::JsonValue::parse(response);
  
  jsonrpc::Response parsed_response = json::from_json<jsonrpc::Response>(response_json);
  
  EXPECT_TRUE(holds_alternative<int>(parsed_response.id));
  EXPECT_EQ(1, get<int>(parsed_response.id));
  EXPECT_TRUE(parsed_response.result.has_value());
  
  server.stop();
}

// Test notification echo
TEST_F(StdioEchoServerTest, NotificationEcho) {
  MockEchoServer server(*dispatcher_,
                       server_stdin_pipe_[0],
                       server_stdout_pipe_[1]);
  
  ASSERT_TRUE(server.start());
  
  // Send notification
  std::string notif_json = R"({"jsonrpc":"2.0","method":"log","params":{"level":"info","message":"test"}})";
  writeMessage(server_stdin_pipe_[1], notif_json);
  
  // Process messages
  for (int i = 0; i < 10; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  // Verify notification was received
  EXPECT_EQ(1, server.getNotificationCount());
  ASSERT_TRUE(server.getLastNotification().has_value());
  EXPECT_EQ("log", server.getLastNotification()->method);
  
  // Read echo notification
  std::string echo = readMessage(server_stdout_pipe_[0]);
  EXPECT_FALSE(echo.empty());
  
  // Parse and verify echo
  auto echo_json = json::JsonValue::parse(echo);
  
  jsonrpc::Notification parsed_echo = json::from_json<jsonrpc::Notification>(echo_json);
  
  EXPECT_EQ("echo/log", parsed_echo.method);
  
  server.stop();
}

// Test multiple requests in sequence
TEST_F(StdioEchoServerTest, MultipleRequests) {
  MockEchoServer server(*dispatcher_,
                       server_stdin_pipe_[0],
                       server_stdout_pipe_[1]);
  
  ASSERT_TRUE(server.start());
  
  // Send multiple requests
  for (int i = 1; i <= 5; ++i) {
    std::string request = R"({"jsonrpc":"2.0","id":)" + std::to_string(i) + 
                         R"(,"method":"test.)" + std::to_string(i) + R"("})";
    writeMessage(server_stdin_pipe_[1], request);
  }
  
  // Process all messages
  for (int i = 0; i < 20; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  // Verify all requests were received
  EXPECT_EQ(5, server.getRequestCount());
  
  // Read all responses
  int response_count = 0;
  while (true) {
    std::string response = readMessage(server_stdout_pipe_[0], 100);
    if (response.empty()) break;
    response_count++;
  }
  
  EXPECT_EQ(5, response_count);
  
  server.stop();
}

// Test shutdown notification
TEST_F(StdioEchoServerTest, ShutdownNotification) {
  MockEchoServer server(*dispatcher_,
                       server_stdin_pipe_[0],
                       server_stdout_pipe_[1]);
  
  ASSERT_TRUE(server.start());
  
  // Send shutdown notification
  std::string shutdown_json = R"({"jsonrpc":"2.0","method":"shutdown"})";
  writeMessage(server_stdin_pipe_[1], shutdown_json);
  
  // Process messages
  for (int i = 0; i < 10; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  // Verify shutdown was received
  EXPECT_EQ(1, server.getNotificationCount());
  ASSERT_TRUE(server.getLastNotification().has_value());
  EXPECT_EQ("shutdown", server.getLastNotification()->method);
}

// Test error handling for invalid JSON
TEST_F(StdioEchoServerTest, InvalidJsonHandling) {
  MockEchoServer server(*dispatcher_,
                       server_stdin_pipe_[0],
                       server_stdout_pipe_[1]);
  
  ASSERT_TRUE(server.start());
  
  // Send invalid JSON
  std::string invalid_json = R"({"jsonrpc":"2.0","id":1,"method":)"; // Incomplete
  writeMessage(server_stdin_pipe_[1], invalid_json);
  
  // Process messages
  for (int i = 0; i < 10; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  // Should handle gracefully without crash
  // Error count might increase depending on implementation
  
  // Send valid message after invalid one
  std::string valid_json = R"({"jsonrpc":"2.0","id":2,"method":"test"})";
  writeMessage(server_stdin_pipe_[1], valid_json);
  
  // Process messages
  for (int i = 0; i < 10; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  // Should recover and process valid message
  EXPECT_GE(server.getRequestCount(), 1);
  
  server.stop();
}

// Test large message handling
TEST_F(StdioEchoServerTest, LargeMessageHandling) {
  MockEchoServer server(*dispatcher_,
                       server_stdin_pipe_[0],
                       server_stdout_pipe_[1]);
  
  ASSERT_TRUE(server.start());
  
  // Create large params
  Metadata large_params;
  std::string large_string(10000, 'x'); // 10KB string
  add_metadata(large_params, "large_data", large_string);
  
  jsonrpc::Request request;
  request.id = RequestId(1);
  request.method = "test.large";
  request.params = make_optional(large_params);
  
  // Serialize and send
  auto json_result = json::to_json(request);
  writeMessage(server_stdin_pipe_[1], json_result.toString());
  
  // Process messages with more time for large message
  for (int i = 0; i < 20; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  
  // Verify request was received
  EXPECT_EQ(1, server.getRequestCount());
  ASSERT_TRUE(server.getLastRequest().has_value());
  EXPECT_EQ("test.large", server.getLastRequest()->method);
  
  server.stop();
}

// Test concurrent reads and writes
TEST_F(StdioEchoServerTest, ConcurrentReadWrite) {
  MockEchoServer server(*dispatcher_,
                       server_stdin_pipe_[0],
                       server_stdout_pipe_[1]);
  
  ASSERT_TRUE(server.start());
  
  std::atomic<bool> stop(false);
  std::atomic<int> sent_count(0);
  std::atomic<int> received_count(0);
  
  // Writer thread
  std::thread writer([&]() {
    while (!stop) {
      int id = ++sent_count;
      std::string request = R"({"jsonrpc":"2.0","id":)" + std::to_string(id) + 
                           R"(,"method":"concurrent.test"})";
      writeMessage(server_stdin_pipe_[1], request);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  });
  
  // Reader thread
  std::thread reader([&]() {
    while (!stop) {
      std::string response = readMessage(server_stdout_pipe_[0], 100);
      if (!response.empty()) {
        received_count++;
      }
    }
  });
  
  // Run for a short time
  auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500)) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  stop = true;
  writer.join();
  reader.join();
  
  // Verify messages were processed
  EXPECT_GT(server.getRequestCount(), 0);
  EXPECT_GT(received_count.load(), 0);
  
  // Should have similar counts (allowing for some in-flight)
  EXPECT_NEAR(sent_count.load(), received_count.load(), 2);
  
  server.stop();
}

} // namespace test
} // namespace mcp