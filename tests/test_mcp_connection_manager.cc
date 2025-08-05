#include <gtest/gtest.h>
#include "mcp/mcp_connection_manager.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/socket_impl.h"
#include <memory>

namespace mcp {
namespace {

// Mock MCP message callbacks
class MockMcpMessageCallbacks : public McpMessageCallbacks {
public:
  void onRequest(const jsonrpc::Request& request) override {
    request_called_++;
    last_request_ = request;
  }
  
  void onNotification(const jsonrpc::Notification& notification) override {
    notification_called_++;
    last_notification_ = notification;
  }
  
  void onResponse(const jsonrpc::Response& response) override {
    response_called_++;
    last_response_ = response;
  }
  
  void onConnectionEvent(network::ConnectionEvent event) override {
    events_.push_back(event);
  }
  
  void onError(const Error& error) override {
    error_called_++;
    last_error_ = error;
  }
  
  // Test state
  int request_called_{0};
  int notification_called_{0};
  int response_called_{0};
  int error_called_{0};
  
  jsonrpc::Request last_request_;
  jsonrpc::Notification last_notification_;
  jsonrpc::Response last_response_;
  Error last_error_;
  
  std::vector<network::ConnectionEvent> events_;
};

// JsonRpcMessageFilter tests

class JsonRpcMessageFilterTest : public ::testing::Test {
protected:
  void SetUp() override {
    filter_ = std::make_unique<JsonRpcMessageFilter>(callbacks_);
  }
  
  MockMcpMessageCallbacks callbacks_;
  std::unique_ptr<JsonRpcMessageFilter> filter_;
};

TEST_F(JsonRpcMessageFilterTest, ParseRequest) {
  // Create JSON-RPC request
  nlohmann::json request = {
      {"jsonrpc", "2.0"},
      {"id", 123},
      {"method", "test_method"},
      {"params", {{"key", "value"}}}
  };
  
  // Add to buffer
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add(request.dump());
  buffer->add("\n");
  
  // Process through filter
  auto status = filter_->onData(*buffer, false);
  EXPECT_EQ(network::FilterStatus::Continue, status);
  
  // Verify callback
  EXPECT_EQ(1, callbacks_.request_called_);
  EXPECT_EQ(123, callbacks_.last_request_.id);
  EXPECT_EQ("test_method", callbacks_.last_request_.method);
  EXPECT_TRUE(callbacks_.last_request_.params.has_value());
}

TEST_F(JsonRpcMessageFilterTest, ParseNotification) {
  // Create JSON-RPC notification
  nlohmann::json notification = {
      {"jsonrpc", "2.0"},
      {"method", "notification_method"},
      {"params", {1, 2, 3}}
  };
  
  // Add to buffer
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add(notification.dump());
  buffer->add("\n");
  
  // Process through filter
  filter_->onData(*buffer, false);
  
  // Verify callback
  EXPECT_EQ(1, callbacks_.notification_called_);
  EXPECT_EQ("notification_method", callbacks_.last_notification_.method);
  EXPECT_TRUE(callbacks_.last_notification_.params.has_value());
}

TEST_F(JsonRpcMessageFilterTest, ParseResponse) {
  // Create JSON-RPC response
  nlohmann::json response = {
      {"jsonrpc", "2.0"},
      {"id", 456},
      {"result", {{"status", "ok"}}}
  };
  
  // Add to buffer
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add(response.dump());
  buffer->add("\n");
  
  // Process through filter
  filter_->onData(*buffer, false);
  
  // Verify callback
  EXPECT_EQ(1, callbacks_.response_called_);
  EXPECT_EQ(456, callbacks_.last_response_.id);
  EXPECT_TRUE(callbacks_.last_response_.result.has_value());
  EXPECT_FALSE(callbacks_.last_response_.error.has_value());
}

TEST_F(JsonRpcMessageFilterTest, ParseErrorResponse) {
  // Create JSON-RPC error response
  nlohmann::json response = {
      {"jsonrpc", "2.0"},
      {"id", 789},
      {"error", {
          {"code", -32601},
          {"message", "Method not found"},
          {"data", "test_method"}
      }}
  };
  
  // Add to buffer
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add(response.dump());
  buffer->add("\n");
  
  // Process through filter
  filter_->onData(*buffer, false);
  
  // Verify callback
  EXPECT_EQ(1, callbacks_.response_called_);
  EXPECT_EQ(789, callbacks_.last_response_.id);
  EXPECT_FALSE(callbacks_.last_response_.result.has_value());
  EXPECT_TRUE(callbacks_.last_response_.error.has_value());
  EXPECT_EQ(-32601, callbacks_.last_response_.error->code);
  EXPECT_EQ("Method not found", callbacks_.last_response_.error->message);
}

TEST_F(JsonRpcMessageFilterTest, ParseMultipleMessages) {
  // Add multiple messages
  auto buffer = std::make_unique<OwnedBuffer>();
  
  buffer->add("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"method1\"}\n");
  buffer->add("{\"jsonrpc\":\"2.0\",\"method\":\"notification1\"}\n");
  buffer->add("{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":\"ok\"}\n");
  
  // Process all at once
  filter_->onData(*buffer, false);
  
  // Verify all parsed
  EXPECT_EQ(1, callbacks_.request_called_);
  EXPECT_EQ(1, callbacks_.notification_called_);
  EXPECT_EQ(1, callbacks_.response_called_);
}

TEST_F(JsonRpcMessageFilterTest, ParseInvalidJson) {
  // Add invalid JSON
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add("{invalid json}\n");
  
  // Process through filter
  filter_->onData(*buffer, false);
  
  // Should trigger error callback
  EXPECT_EQ(1, callbacks_.error_called_);
  EXPECT_EQ(-32700, callbacks_.last_error_.code); // Parse error
}

TEST_F(JsonRpcMessageFilterTest, FramedMessages) {
  // Enable framing
  filter_->use_framing_ = true;
  
  // Create framed message
  nlohmann::json request = {
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"method", "test"}
  };
  std::string json_str = request.dump();
  
  // Add 4-byte length prefix
  auto buffer = std::make_unique<OwnedBuffer>();
  uint8_t len_bytes[4];
  uint32_t len = json_str.length();
  len_bytes[0] = (len >> 24) & 0xFF;
  len_bytes[1] = (len >> 16) & 0xFF;
  len_bytes[2] = (len >> 8) & 0xFF;
  len_bytes[3] = len & 0xFF;
  
  buffer->add(len_bytes, 4);
  buffer->add(json_str);
  
  // Process through filter
  filter_->onData(*buffer, false);
  
  // Verify parsed
  EXPECT_EQ(1, callbacks_.request_called_);
  EXPECT_EQ("test", callbacks_.last_request_.method);
}

TEST_F(JsonRpcMessageFilterTest, WriteFraming) {
  // Enable framing
  filter_->use_framing_ = true;
  
  // Create message
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add("{\"test\":\"data\"}");
  
  size_t original_len = buffer->length();
  
  // Process through write filter
  filter_->onWrite(*buffer, false);
  
  // Should have length prefix added
  EXPECT_EQ(original_len + 4, buffer->length());
}

// McpConnectionManager tests

class McpConnectionManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    dispatcher_ = event::createLibeventDispatcher("test");
    socket_interface_ = &network::socketInterface();
    
    // Create config for stdio transport
    config_.transport_type = TransportType::Stdio;
    config_.stdio_config = transport::StdioTransportSocketConfig{
        .stdin_fd = 0,
        .stdout_fd = 1,
        .non_blocking = true
    };
    config_.buffer_limit = 1024 * 1024;
    config_.connection_timeout = std::chrono::milliseconds(5000);
    config_.use_message_framing = false;
    
    manager_ = std::make_unique<McpConnectionManager>(
        *dispatcher_, *socket_interface_, config_);
    
    manager_->setMessageCallbacks(callbacks_);
  }
  
  void TearDown() override {
    manager_.reset();
    dispatcher_->exit();
  }
  
  std::unique_ptr<event::Dispatcher> dispatcher_;
  network::SocketInterface* socket_interface_;
  McpConnectionConfig config_;
  std::unique_ptr<McpConnectionManager> manager_;
  MockMcpMessageCallbacks callbacks_;
};

TEST_F(McpConnectionManagerTest, InitialState) {
  EXPECT_FALSE(manager_->isConnected());
}

TEST_F(McpConnectionManagerTest, ConnectStdio) {
  // Note: This test connects using stdio transport which doesn't do actual I/O
  auto result = manager_->connect();
  ASSERT_TRUE(result.ok()) << result.error();
  
  // Should be connected
  EXPECT_TRUE(manager_->isConnected());
  
  // Should receive connected event
  ASSERT_EQ(1, callbacks_.events_.size());
  EXPECT_EQ(network::ConnectionEvent::Connected, callbacks_.events_[0]);
}

TEST_F(McpConnectionManagerTest, SendRequest) {
  // Connect first
  auto result = manager_->connect();
  ASSERT_TRUE(result.ok());
  
  // Create request
  jsonrpc::Request request;
  request.id = 123;
  request.method = "initialize";
  request.params = nlohmann::json{{"version", "1.0"}};
  
  // Send request
  result = manager_->sendRequest(request);
  EXPECT_TRUE(result.ok());
}

TEST_F(McpConnectionManagerTest, SendNotification) {
  // Connect first
  auto result = manager_->connect();
  ASSERT_TRUE(result.ok());
  
  // Create notification
  jsonrpc::Notification notification;
  notification.method = "progress";
  notification.params = nlohmann::json{{"percent", 50}};
  
  // Send notification
  result = manager_->sendNotification(notification);
  EXPECT_TRUE(result.ok());
}

TEST_F(McpConnectionManagerTest, SendResponse) {
  // Connect first
  auto result = manager_->connect();
  ASSERT_TRUE(result.ok());
  
  // Create response
  jsonrpc::Response response;
  response.id = 456;
  response.result = nlohmann::json{{"status", "success"}};
  
  // Send response
  result = manager_->sendResponse(response);
  EXPECT_TRUE(result.ok());
}

TEST_F(McpConnectionManagerTest, SendErrorResponse) {
  // Connect first
  auto result = manager_->connect();
  ASSERT_TRUE(result.ok());
  
  // Create error response
  jsonrpc::Response response;
  response.id = 789;
  response.error = jsonrpc::Error{
      .code = -32601,
      .message = "Method not found",
      .data = nlohmann::json{"unknown_method"}
  };
  
  // Send response
  result = manager_->sendResponse(response);
  EXPECT_TRUE(result.ok());
}

TEST_F(McpConnectionManagerTest, CloseConnection) {
  // Connect first
  manager_->connect();
  EXPECT_TRUE(manager_->isConnected());
  
  // Close
  manager_->close();
  EXPECT_FALSE(manager_->isConnected());
}

TEST_F(McpConnectionManagerTest, MessageCallbackForwarding) {
  // Test that manager forwards messages to callbacks
  
  // Simulate request
  jsonrpc::Request request;
  request.id = 1;
  request.method = "test";
  manager_->onRequest(request);
  
  EXPECT_EQ(1, callbacks_.request_called_);
  EXPECT_EQ("test", callbacks_.last_request_.method);
  
  // Simulate notification
  jsonrpc::Notification notification;
  notification.method = "notify";
  manager_->onNotification(notification);
  
  EXPECT_EQ(1, callbacks_.notification_called_);
  EXPECT_EQ("notify", callbacks_.last_notification_.method);
  
  // Simulate response
  jsonrpc::Response response;
  response.id = 2;
  response.result = "ok";
  manager_->onResponse(response);
  
  EXPECT_EQ(1, callbacks_.response_called_);
  EXPECT_EQ(2, callbacks_.last_response_.id);
  
  // Simulate error
  Error error;
  error.code = -1;
  error.message = "test error";
  manager_->onError(error);
  
  EXPECT_EQ(1, callbacks_.error_called_);
  EXPECT_EQ("test error", callbacks_.last_error_.message);
}

TEST_F(McpConnectionManagerTest, HttpSseConfig) {
  // Create manager with HTTP/SSE transport
  McpConnectionConfig http_config;
  http_config.transport_type = TransportType::HttpSse;
  http_config.http_sse_config = transport::HttpSseTransportSocketConfig{
      .endpoint_url = "http://localhost:8080/mcp",
      .headers = {{"Authorization", "Bearer token"}},
      .connect_timeout = std::chrono::milliseconds(10000)
  };
  
  auto http_manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, *socket_interface_, http_config);
  
  // Connect would fail in test environment
  auto result = http_manager->connect();
  EXPECT_FALSE(result.ok());
}

TEST_F(McpConnectionManagerTest, FactoryFunction) {
  // Test factory function
  auto manager = createMcpConnectionManager(*dispatcher_);
  ASSERT_NE(nullptr, manager);
  
  // Should use default stdio config
  EXPECT_FALSE(manager->isConnected());
}

// Integration test demonstrating usage
TEST_F(McpConnectionManagerTest, UsageExample) {
  // Connect
  auto result = manager_->connect();
  ASSERT_TRUE(result.ok());
  
  // Send initialize request
  jsonrpc::Request init_request;
  init_request.id = 1;
  init_request.method = "initialize";
  init_request.params = nlohmann::json{
      {"protocol_version", "2024-11-05"},
      {"capabilities", nlohmann::json::object()},
      {"client_info", {
          {"name", "test_client"},
          {"version", "1.0.0"}
      }}
  };
  
  result = manager_->sendRequest(init_request);
  ASSERT_TRUE(result.ok());
  
  // In real usage, would run event loop and wait for response
  // dispatcher_->run(event::RunType::Block);
}

} // namespace
} // namespace mcp