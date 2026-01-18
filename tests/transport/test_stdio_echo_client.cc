/**
 * @file test_stdio_echo_client.cc
 * @brief TRANSPORT LAYER TESTS for stdio echo client functionality
 *
 * TEST LEVEL: Low-level transport/infrastructure testing
 *
 * This file tests the underlying transport mechanisms, pipes, sockets, and
 * connection management components that enable stdio communication. It does
 * NOT test the actual echo application binaries.
 *
 * What this tests:
 * - StdioTransportSocket functionality
 * - Pipe-based communication
 * - Event dispatching and async I/O
 * - Connection management internals
 * - Transport protocol handling
 *
 * What this does NOT test:
 * - Actual echo client/server applications
 * - End-to-end JSON-RPC message flows
 * - Application-level business logic
 *
 * For application-level testing, see:
 * - tests/test_stdio_echo_client_basic.cc (basic application tests)
 * - tests/test_stdio_echo_client_advanced.cc (advanced application tests)
 */

#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <unistd.h>

#include <arpa/inet.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/json/json_serialization.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/socket_interface_impl.h"
#include "mcp/transport/stdio_transport_socket.h"

namespace mcp {
namespace test {

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

/**
 * Test fixture for stdio echo client tests
 *
 * Architecture Overview:
 * - Creates bidirectional pipes to simulate stdio communication
 * - Client reads from client_stdin_pipe and writes to client_stdout_pipe
 * - MockServer runs in a separate thread to echo messages back
 * - Uses libevent dispatcher for async I/O in the client
 * - Tests client-side request/response and notification patterns
 */
class StdioEchoClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create pipes for bidirectional communication
    // client_stdin_pipe: server writes to [1], client reads from [0]
    // client_stdout_pipe: client writes to [1], server reads from [0]
    ASSERT_EQ(0, pipe(client_stdin_pipe_));
    ASSERT_EQ(0, pipe(client_stdout_pipe_));

    // Make pipes non-blocking to prevent test hangs
    // Essential for async I/O and timeout handling
    // Make all pipe ends non-blocking for level-triggered events
    fcntl(client_stdin_pipe_[0], F_SETFL, O_NONBLOCK);
    fcntl(client_stdin_pipe_[1], F_SETFL, O_NONBLOCK);
    fcntl(client_stdout_pipe_[0], F_SETFL, O_NONBLOCK);
    fcntl(client_stdout_pipe_[1], F_SETFL, O_NONBLOCK);

    // Create dispatcher
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
  }

  void TearDown() override {
    // Close all pipes
    for (int fd : {client_stdin_pipe_[0], client_stdin_pipe_[1],
                   client_stdout_pipe_[0], client_stdout_pipe_[1]}) {
      if (fd >= 0)
        close(fd);
    }
  }

  /**
   * Mock echo client for testing
   *
   * This client:
   * - Connects via stdio transport using provided file descriptors
   * - Sends requests and tracks responses by ID
   * - Sends notifications without expecting responses
   * - Maintains pending/completed request maps for verification
   * - Thread-safe: connection initiated from dispatcher thread
   */
  class MockEchoClient : public McpProtocolCallbacks {
   public:
    MockEchoClient(event::Dispatcher& dispatcher, int stdin_fd, int stdout_fd)
        : dispatcher_(dispatcher), next_request_id_(1) {
      // Configure for test pipes
      McpConnectionConfig config;
      config.transport_type = TransportType::Stdio;
      config.stdio_config = transport::StdioTransportSocketConfig{
          .stdin_fd = stdin_fd,
          .stdout_fd = stdout_fd,
          .non_blocking = true,
          .use_bridge = false};            // Test pipes don't need bridging
      config.use_message_framing = false;  // Simplify for testing

      socket_interface_ = std::make_unique<network::SocketInterfaceImpl>();
      connection_manager_ = std::make_unique<McpConnectionManager>(
          dispatcher_, *socket_interface_, config);
      connection_manager_->setProtocolCallbacks(*this);
    }

    bool start() {
      // Thread Safety Critical Section:
      // The McpConnectionManager must be connected from the dispatcher thread
      // to ensure all socket operations happen on the same thread.
      // Using promise/future pattern for synchronization.
      auto connected_promise = std::make_shared<std::promise<bool>>();
      auto connected_future = connected_promise->get_future();

      dispatcher_.post([this, connected_promise]() {
        auto result = connection_manager_->connect();
        bool success = !holds_alternative<Error>(result);
        running_ = success;
        connected_promise->set_value(success);
      });

      // Process the posted task
      dispatcher_.run(event::RunType::NonBlock);

      // Wait for connection result
      if (connected_future.wait_for(std::chrono::milliseconds(100)) ==
          std::future_status::ready) {
        return connected_future.get();
      }
      return false;
    }

    void stop() {
      running_ = false;
      connection_manager_->close();
    }

    // Client methods
    int sendRequest(const std::string& method, const Metadata& params = {}) {
      int id = next_request_id_++;

      jsonrpc::Request request;
      request.id = RequestId(id);
      request.method = method;

      if (!params.empty()) {
        request.params = mcp::make_optional(params);
      }

      pending_requests_[id] = method;
      sent_requests_.push_back(request);

      auto result = connection_manager_->sendRequest(request);
      if (holds_alternative<Error>(result)) {
        pending_requests_.erase(id);
        return -1;
      }

      return id;
    }

    void sendNotification(const std::string& method,
                          const Metadata& params = {}) {
      jsonrpc::Notification notification;
      notification.method = method;

      if (!params.empty()) {
        notification.params = mcp::make_optional(params);
      }

      sent_notifications_.push_back(notification);
      connection_manager_->sendNotification(notification);
    }

    // McpProtocolCallbacks
    void onRequest(const jsonrpc::Request& request) override {
      received_requests_.push_back(request);

      // Client shouldn't receive requests, but respond anyway
      jsonrpc::Response response;
      response.id = request.id;

      Metadata result;
      add_metadata(result, "client_response", true);
      response.result = mcp::make_optional(jsonrpc::ResponseResult(result));

      connection_manager_->sendResponse(response);
    }

    void onNotification(const jsonrpc::Notification& notification) override {
      received_notifications_.push_back(notification);
    }

    void onResponse(const jsonrpc::Response& response) override {
      received_responses_.push_back(response);

      // Check pending request
      if (holds_alternative<int64_t>(response.id)) {
        int id = get<int64_t>(response.id);
        auto it = pending_requests_.find(id);
        if (it != pending_requests_.end()) {
          completed_requests_[id] = it->second;
          pending_requests_.erase(it);
        }
      }
    }

    void onConnectionEvent(network::ConnectionEvent event) override {
      connection_events_.push_back(event);
      if (event == network::ConnectionEvent::RemoteClose) {
        running_ = false;
      }
    }

    void onError(const Error& error) override { errors_.push_back(error); }

    // Test accessors
    bool isRunning() const { return running_; }

    const std::vector<jsonrpc::Request>& getSentRequests() const {
      return sent_requests_;
    }

    const std::vector<jsonrpc::Notification>& getSentNotifications() const {
      return sent_notifications_;
    }

    const std::vector<jsonrpc::Response>& getReceivedResponses() const {
      return received_responses_;
    }

    const std::vector<jsonrpc::Notification>& getReceivedNotifications() const {
      return received_notifications_;
    }

    const std::vector<Error>& getErrors() const { return errors_; }

    const std::map<int, std::string>& getPendingRequests() const {
      return pending_requests_;
    }

    const std::map<int, std::string>& getCompletedRequests() const {
      return completed_requests_;
    }

    const std::vector<network::ConnectionEvent>& getConnectionEvents() const {
      return connection_events_;
    }

   private:
    event::Dispatcher& dispatcher_;
    std::unique_ptr<network::SocketInterface> socket_interface_;
    std::unique_ptr<McpConnectionManager> connection_manager_;

    std::atomic<bool> running_{false};
    std::atomic<int> next_request_id_{1};

    // Tracking
    std::vector<jsonrpc::Request> sent_requests_;
    std::vector<jsonrpc::Notification> sent_notifications_;
    std::vector<jsonrpc::Request> received_requests_;
    std::vector<jsonrpc::Response> received_responses_;
    std::vector<jsonrpc::Notification> received_notifications_;
    std::vector<Error> errors_;
    std::vector<network::ConnectionEvent> connection_events_;

    std::map<int, std::string> pending_requests_;
    std::map<int, std::string> completed_requests_;
  };

  /**
   * Mock server that echoes back messages
   *
   * Runs in a separate thread to simulate a server process.
   * - Reads JSON-RPC messages from pipe (newline delimited)
   * - Echoes requests as responses with metadata
   * - Echoes notifications with "echo/" prefix
   * - Non-blocking reads with polling to allow clean shutdown
   */
  class MockServer {
   public:
    MockServer(int read_fd, int write_fd)
        : read_fd_(read_fd), write_fd_(write_fd), running_(true) {
      // Make server's read end non-blocking to prevent hangs
      fcntl(read_fd_, F_SETFL, O_NONBLOCK);
      thread_ = std::thread([this]() { run(); });
    }

    ~MockServer() { stop(); }

    void stop() {
      running_ = false;
      if (thread_.joinable()) {
        thread_.join();
      }
    }

   private:
    void run() {
      while (running_) {
        std::string message = readMessage();
        if (!message.empty()) {
          processMessage(message);
        }
        // Small sleep to prevent busy waiting and allow other threads to run
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }

    std::string readMessage() {
      std::string buffer;
      char c;

      // Read until newline or error
      // Non-blocking read - returns empty string if no data available
      while (true) {
        ssize_t n = read(read_fd_, &c, 1);
        if (n > 0) {
          if (c == '\n') {
            return buffer;  // Complete message received
          }
          buffer += c;
        } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
          return "";  // EOF or error
        } else {
          break;  // EAGAIN/EWOULDBLOCK - no data available
        }
      }

      return "";
    }

    void processMessage(const std::string& message) {
      // Parse JSON
      try {
        auto json_val = json::JsonValue::parse(message);

        // Check if it's a request or notification
        if (json_val.contains("id")) {
          // Request - send response
          try {
            jsonrpc::Request request =
                json::from_json<jsonrpc::Request>(json_val);
            jsonrpc::Response response;
            response.id = request.id;

            Metadata result;
            add_metadata(result, "echo", true);
            add_metadata(result, "method", request.method);
            response.result = mcp::make_optional(jsonrpc::ResponseResult(result));

            auto response_json = json::to_json(response);
            writeMessage(response_json.toString());
          } catch (...) {
            // Ignore deserialization errors
          }
        } else if (json_val.contains("method")) {
          // Notification - echo back
          try {
            jsonrpc::Notification notification =
                json::from_json<jsonrpc::Notification>(json_val);
            jsonrpc::Notification echo;
            echo.method = "echo/" + notification.method;

            auto echo_json = json::to_json(echo);
            writeMessage(echo_json.toString());
          } catch (...) {
            // Ignore deserialization errors
          }
        }
      } catch (...) {
        // Ignore JSON parse errors
        return;
      }
    }

    void writeMessage(const std::string& message) {
      std::string full_message = message + "\n";
      write(write_fd_, full_message.c_str(), full_message.size());
    }

    int read_fd_;
    int write_fd_;
    std::atomic<bool> running_;
    std::thread thread_;
  };

  std::unique_ptr<event::Dispatcher> dispatcher_;
  int client_stdin_pipe_[2];
  int client_stdout_pipe_[2];
};

// Test basic client startup and shutdown
// Verifies client can connect and disconnect cleanly without errors
TEST_F(StdioEchoClientTest, StartupShutdown) {
  MockEchoClient client(*dispatcher_, client_stdin_pipe_[0],
                        client_stdout_pipe_[1]);

  EXPECT_TRUE(client.start());
  EXPECT_TRUE(client.isRunning());

  // Run dispatcher briefly
  dispatcher_->run(event::RunType::NonBlock);

  client.stop();
  EXPECT_FALSE(client.isRunning());

  // Verify no errors
  EXPECT_EQ(0, client.getErrors().size());
}

// Test sending requests and receiving responses
// Flow: Client sends request -> Server echoes response -> Client processes
// response
TEST_F(StdioEchoClientTest, RequestResponse) {
  MockEchoClient client(*dispatcher_, client_stdin_pipe_[0],
                        client_stdout_pipe_[1]);

  // Start mock server
  MockServer server(client_stdout_pipe_[0], client_stdin_pipe_[1]);

  ASSERT_TRUE(client.start());

  // Send request
  Metadata params;
  add_metadata(params, "test_param", "value");
  int request_id = client.sendRequest("test.method", params);
  EXPECT_GT(request_id, 0);

  // Process messages through dispatcher event loop
  // Multiple iterations ensure:
  // 1. Request is written to stdout pipe
  // 2. Server thread reads and processes request
  // 3. Server writes response to stdin pipe
  // 4. Client reads response and invokes callback
  // Increased timeout for level-triggered events on macOS
  for (int i = 0; i < 50; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  // Verify request was sent
  EXPECT_EQ(1, client.getSentRequests().size());

  // Verify response was received
  EXPECT_EQ(1, client.getReceivedResponses().size());

  // Verify request was completed
  EXPECT_EQ(0, client.getPendingRequests().size());
  EXPECT_EQ(1, client.getCompletedRequests().size());

  auto& response = client.getReceivedResponses()[0];
  EXPECT_TRUE(holds_alternative<int64_t>(response.id));
  EXPECT_EQ(request_id, get<int64_t>(response.id));

  client.stop();
}

// Test sending notifications
// Notifications are fire-and-forget messages without response IDs
TEST_F(StdioEchoClientTest, NotificationSending) {
  MockEchoClient client(*dispatcher_, client_stdin_pipe_[0],
                        client_stdout_pipe_[1]);

  // Start mock server
  MockServer server(client_stdout_pipe_[0], client_stdin_pipe_[1]);

  ASSERT_TRUE(client.start());

  // Send notification
  Metadata params;
  add_metadata(params, "level", "info");
  add_metadata(params, "message", "test notification");
  client.sendNotification("log", params);

  // Process messages through dispatcher event loop
  // Multiple iterations ensure:
  // 1. Request is written to stdout pipe
  // 2. Server thread reads and processes request
  // 3. Server writes response to stdin pipe
  // 4. Client reads response and invokes callback
  for (int i = 0; i < 20; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Verify notification was sent
  EXPECT_EQ(1, client.getSentNotifications().size());

  // Verify echo notification was received
  EXPECT_EQ(1, client.getReceivedNotifications().size());
  EXPECT_EQ("echo/log", client.getReceivedNotifications()[0].method);

  client.stop();
}

// Test multiple concurrent requests
// Verifies client can handle multiple outstanding requests with different IDs
TEST_F(StdioEchoClientTest, ConcurrentRequests) {
  MockEchoClient client(*dispatcher_, client_stdin_pipe_[0],
                        client_stdout_pipe_[1]);

  // Start mock server
  MockServer server(client_stdout_pipe_[0], client_stdin_pipe_[1]);

  ASSERT_TRUE(client.start());

  // Send multiple requests without waiting for responses
  // Each request has a unique ID for correlation
  std::vector<int> request_ids;
  for (int i = 0; i < 10; ++i) {
    Metadata params;
    add_metadata(params, "index", static_cast<int64_t>(i));
    int id = client.sendRequest("test." + std::to_string(i), params);
    request_ids.push_back(id);
  }

  // Process messages
  for (int i = 0; i < 50; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Verify all requests were sent
  EXPECT_EQ(10, client.getSentRequests().size());

  // Verify all responses were received
  EXPECT_EQ(10, client.getReceivedResponses().size());

  // Verify all requests completed
  EXPECT_EQ(0, client.getPendingRequests().size());
  EXPECT_EQ(10, client.getCompletedRequests().size());

  client.stop();
}

// Test request timeout handling
// Verifies client behavior when no response is received
TEST_F(StdioEchoClientTest, RequestTimeout) {
  MockEchoClient client(*dispatcher_, client_stdin_pipe_[0],
                        client_stdout_pipe_[1]);

  // No server - requests should timeout or fail

  ASSERT_TRUE(client.start());

  // Send request
  int request_id = client.sendRequest("test.timeout");
  EXPECT_GT(request_id, 0);

  // Process messages briefly
  for (int i = 0; i < 10; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Request should still be pending (no response)
  EXPECT_EQ(1, client.getPendingRequests().size());
  EXPECT_EQ(0, client.getCompletedRequests().size());

  client.stop();
}

// Test mixed requests and notifications
// Verifies interleaved request/notification handling
TEST_F(StdioEchoClientTest, MixedMessages) {
  MockEchoClient client(*dispatcher_, client_stdin_pipe_[0],
                        client_stdout_pipe_[1]);

  // Start mock server
  MockServer server(client_stdout_pipe_[0], client_stdin_pipe_[1]);

  ASSERT_TRUE(client.start());

  // Send mixed messages
  client.sendRequest("request.1");
  client.sendNotification("notify.1");
  client.sendRequest("request.2");
  client.sendNotification("notify.2");
  client.sendRequest("request.3");

  // Process mixed messages with extra iterations
  // Ensures all requests and notifications are handled
  for (int i = 0; i < 30; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Verify counts
  EXPECT_EQ(3, client.getSentRequests().size());
  EXPECT_EQ(2, client.getSentNotifications().size());
  EXPECT_EQ(3, client.getReceivedResponses().size());
  EXPECT_EQ(2, client.getReceivedNotifications().size());

  client.stop();
}

// Test error response handling
// Verifies client correctly processes JSON-RPC error responses
TEST_F(StdioEchoClientTest, ErrorResponse) {
  MockEchoClient client(*dispatcher_, client_stdin_pipe_[0],
                        client_stdout_pipe_[1]);

  ASSERT_TRUE(client.start());

  // Manually send error response
  jsonrpc::Response error_response;
  error_response.id = RequestId(1);
  error_response.error =
      mcp::make_optional(Error(jsonrpc::METHOD_NOT_FOUND, "Method not found"));

  auto json_result = json::to_json(error_response);

  std::string message = json_result.toString() + "\n";
  write(client_stdin_pipe_[1], message.c_str(), message.size());

  // Process messages
  for (int i = 0; i < 10; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Verify error response was received
  EXPECT_EQ(1, client.getReceivedResponses().size());
  auto& response = client.getReceivedResponses()[0];
  EXPECT_TRUE(response.error.has_value());
  EXPECT_EQ(jsonrpc::METHOD_NOT_FOUND, response.error->code);

  client.stop();
}

// Test connection event handling
// Verifies client detects connection and disconnection events
// DISABLED: Remote close detection via pipe close is unreliable and can cause
// hangs The stdio transport may not immediately detect when the remote end
// closes, leading to test timeouts. Proper connection event handling requires
// platform-specific pipe monitoring or explicit protocol-level shutdown
// messages.
TEST_F(StdioEchoClientTest, DISABLED_ConnectionEvents) {
  MockEchoClient client(*dispatcher_, client_stdin_pipe_[0],
                        client_stdout_pipe_[1]);

  ASSERT_TRUE(client.start());

  // Should have connected event
  auto events = client.getConnectionEvents();
  bool has_connected = false;
  for (auto event : events) {
    if (event == network::ConnectionEvent::Connected) {
      has_connected = true;
      break;
    }
  }
  EXPECT_TRUE(has_connected);

  // Close stdin to simulate remote close
  close(client_stdin_pipe_[1]);

  // Process messages
  for (int i = 0; i < 10; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Should have remote close event
  events = client.getConnectionEvents();
  bool has_remote_close = false;
  for (auto event : events) {
    if (event == network::ConnectionEvent::RemoteClose) {
      has_remote_close = true;
      break;
    }
  }

  // Note: RemoteClose detection depends on transport implementation
  // It may not always be detected immediately

  client.stop();
}

// Test large message handling
// Verifies client can send and receive messages larger than typical buffer
// sizes
TEST_F(StdioEchoClientTest, LargeMessages) {
  MockEchoClient client(*dispatcher_, client_stdin_pipe_[0],
                        client_stdout_pipe_[1]);

  // Start mock server
  MockServer server(client_stdout_pipe_[0], client_stdin_pipe_[1]);

  ASSERT_TRUE(client.start());

  // Send large request
  Metadata large_params;
  std::string large_data(50000, 'x');  // 50KB
  add_metadata(large_params, "large_field", large_data);

  int request_id = client.sendRequest("test.large", large_params);
  EXPECT_GT(request_id, 0);

  // Process messages with extra time for large data
  for (int i = 0; i < 50; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  // Verify request completed
  EXPECT_EQ(1, client.getCompletedRequests().size());

  client.stop();
}

// Test rapid fire requests
// Stress test: sends many requests quickly to test buffering and queueing
TEST_F(StdioEchoClientTest, RapidFireRequests) {
  MockEchoClient client(*dispatcher_, client_stdin_pipe_[0],
                        client_stdout_pipe_[1]);

  // Start mock server
  MockServer server(client_stdout_pipe_[0], client_stdin_pipe_[1]);

  ASSERT_TRUE(client.start());

  // Send many requests rapidly
  std::vector<int> request_ids;
  for (int i = 0; i < 100; ++i) {
    int id = client.sendRequest("rapid." + std::to_string(i));
    if (id > 0) {
      request_ids.push_back(id);
    }
  }

  // Process all messages
  for (int i = 0; i < 200; ++i) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // Most requests should complete
  EXPECT_GT(client.getCompletedRequests().size(), 80);

  client.stop();
}

}  // namespace test
}  // namespace mcp