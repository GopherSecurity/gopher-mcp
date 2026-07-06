#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "mcp/buffer.h"
#include "mcp/event/event_loop.h"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define private public
#include "mcp/mcp_connection_manager.h"
#undef private
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include "mcp/network/connection.h"
#include "mcp/network/socket_impl.h"

namespace mcp {
namespace {

class LoopbackHttpCapture {
 public:
  LoopbackHttpCapture() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GE(listen_fd_, 0);

    int opt = 1;
    EXPECT_EQ(
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)),
        0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    EXPECT_EQ(
        ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)),
        0);
    EXPECT_EQ(::listen(listen_fd_, 1), 0);

    socklen_t len = sizeof(addr);
    EXPECT_EQ(
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len), 0);
    port_ = ntohs(addr.sin_port);

    request_future_ = request_promise_.get_future();
    server_thread_ = std::thread([this]() { acceptOne(); });
  }

  ~LoopbackHttpCapture() {
    if (listen_fd_ >= 0) {
      ::close(listen_fd_);
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
  }

  uint16_t port() const { return port_; }

  std::future<std::string>& requestFuture() { return request_future_; }

 private:
  void acceptOne() {
    int fd = ::accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) {
      request_promise_.set_value("");
      return;
    }

    std::string request;
    char buf[512];
    while (request.find("\r\n\r\n") == std::string::npos) {
      ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
      if (n <= 0) {
        break;
      }
      request.append(buf, static_cast<size_t>(n));
    }

    const char response[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    (void)::send(fd, response, sizeof(response) - 1, 0);
    ::close(fd);
    request_promise_.set_value(request);
  }

  int listen_fd_{-1};
  uint16_t port_{0};
  std::promise<std::string> request_promise_;
  std::future<std::string> request_future_;
  std::thread server_thread_;
};

// Mock MCP message callbacks
class MockMcpProtocolCallbacks : public McpProtocolCallbacks {
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

template <class T>
T& unreachableRef() {
  std::abort();
}

template <class T>
const T& unreachableConstRef() {
  std::abort();
}

class CloseTrackingConnection : public network::Connection {
 public:
  CloseTrackingConnection(event::Dispatcher& dispatcher,
                          std::atomic<bool>& destroyed)
      : dispatcher_(dispatcher), destroyed_(destroyed) {}

  ~CloseTrackingConnection() override { destroyed_ = true; }

  void addConnectionCallbacks(network::ConnectionCallbacks& cb) override {
    callbacks_.push_back(&cb);
  }
  void removeConnectionCallbacks(network::ConnectionCallbacks& cb) override {
    callbacks_.erase(std::remove(callbacks_.begin(), callbacks_.end(), &cb),
                     callbacks_.end());
  }
  void addBytesSentCallback(network::BytesSentCb) override {}
  void enableHalfClose(bool enabled) override { half_close_enabled_ = enabled; }
  bool isHalfCloseEnabled() const override { return half_close_enabled_; }
  void close(network::ConnectionCloseType type) override {
    ++close_count_;
    last_close_type_ = type;
    state_ = network::ConnectionState::Closing;
    if (raise_local_close_on_close_) {
      for (network::ConnectionCallbacks* cb : callbacks_) {
        cb->onEvent(network::ConnectionEvent::LocalClose);
      }
    }
  }
  void close(network::ConnectionCloseType type,
             const std::string& details) override {
    close(type);
    local_close_reason_ = details;
  }
  network::DetectedCloseType detectedCloseType() const override {
    return network::DetectedCloseType::Normal;
  }
  event::Dispatcher& dispatcher() const override { return dispatcher_; }
  uint64_t id() const override { return 1; }
  void hashKey(std::vector<uint8_t>&) const override {}
  std::string nextProtocol() const override { return ""; }
  void noDelay(bool) override {}
  network::ReadDisableStatus readDisableWithStatus(bool disable) override {
    read_disabled_ = disable;
    return disable ? network::ReadDisableStatus::TransitionedToReadDisabled
                   : network::ReadDisableStatus::TransitionedToReadEnabled;
  }
  void detectEarlyCloseWhenReadDisabled(bool) override {}
  bool readEnabled() const override { return !read_disabled_; }
  network::ConnectionInfoSetter& connectionInfoSetter() override {
    return unreachableRef<network::ConnectionInfoSetter>();
  }
  const network::ConnectionInfoProvider& connectionInfoProvider()
      const override {
    return unreachableConstRef<network::ConnectionInfoProvider>();
  }
  network::ConnectionInfoProviderSharedPtr connectionInfoProviderSharedPtr()
      const override {
    return nullptr;
  }
  optional<network::Connection::UnixDomainSocketPeerCredentials>
  unixSocketPeerCredentials() const override {
    return nullopt;
  }
  void setConnectionStats(const network::ConnectionStats&) override {}
  network::SslConnectionInfoConstSharedPtr ssl() const override {
    return nullptr;
  }
  std::string requestedServerName() const override { return ""; }
  network::ConnectionState state() const override { return state_; }
  bool connecting() const override { return false; }
  void write(Buffer&, bool) override {}
  void setBufferLimits(uint32_t limit) override { buffer_limit_ = limit; }
  uint32_t bufferLimit() const override { return buffer_limit_; }
  bool aboveHighWatermark() const override { return false; }
  const network::SocketOptionsSharedPtr& socketOptions() const override {
    return socket_options_;
  }
  stream_info::StreamInfo& streamInfo() override { return stream_info_; }
  const stream_info::StreamInfo& streamInfo() const override {
    return stream_info_;
  }
  void setDelayedCloseTimeout(std::chrono::milliseconds) override {}
  void setIdleReadTimeout(std::chrono::milliseconds) override {}
  std::string transportFailureReason() const override { return ""; }
  std::string localCloseReason() const override { return local_close_reason_; }
  bool startSecureTransport() override { return false; }
  optional<std::chrono::milliseconds> lastRoundTripTime() const override {
    return nullopt;
  }
  void configureInitialCongestionWindow(uint64_t,
                                        std::chrono::microseconds) override {}
  optional<uint64_t> congestionWindowInBytes() const override {
    return nullopt;
  }
  network::Socket& socket() override {
    return unreachableRef<network::Socket>();
  }
  const network::Socket& socket() const override {
    return unreachableConstRef<network::Socket>();
  }
  network::TransportSocket& transportSocket() override {
    return unreachableRef<network::TransportSocket>();
  }
  const network::TransportSocket& transportSocket() const override {
    return unreachableConstRef<network::TransportSocket>();
  }

  Buffer& readBuffer() override { return read_buffer_; }
  Buffer& writeBuffer() override { return write_buffer_; }
  Buffer* currentWriteBuffer() override { return nullptr; }
  bool currentWriteEndStream() const override { return false; }
  bool readHalfClosed() const override { return false; }
  bool isClosed() const override {
    return state_ == network::ConnectionState::Closed;
  }
  void readDisable(bool disable) override { read_disabled_ = disable; }
  bool readDisabled() const override { return read_disabled_; }

  network::IoHandle& ioHandle() override {
    return unreachableRef<network::IoHandle>();
  }
  const network::IoHandle& ioHandle() const override {
    return unreachableConstRef<network::IoHandle>();
  }
  network::Connection& connection() override {
    return static_cast<network::Connection&>(*this);
  }
  bool shouldDrainReadBuffer() override { return false; }
  void setTransportSocketIsReadable() override {}
  void raiseEvent(network::ConnectionEvent) override {}
  void flushWriteBuffer() override {}

  void setState(network::ConnectionState state) { state_ = state; }

  int close_count_{0};
  network::ConnectionCloseType last_close_type_{
      network::ConnectionCloseType::FlushWrite};
  bool raise_local_close_on_close_{false};

 private:
  event::Dispatcher& dispatcher_;
  std::atomic<bool>& destroyed_;
  std::vector<network::ConnectionCallbacks*> callbacks_;
  OwnedBuffer read_buffer_;
  OwnedBuffer write_buffer_;
  stream_info::StreamInfoImpl stream_info_;
  network::SocketOptionsSharedPtr socket_options_{
      std::make_shared<std::vector<network::SocketOptionConstSharedPtr>>()};
  network::ConnectionState state_{network::ConnectionState::Open};
  bool half_close_enabled_{false};
  bool read_disabled_{false};
  uint32_t buffer_limit_{0};
  std::string local_close_reason_;
};

// JsonRpcMessageFilter tests
// NOTE: JsonRpcMessageFilter has been removed in favor of JsonRpcProtocolFilter
// These tests are temporarily disabled and should be rewritten for
// JsonRpcProtocolFilter

/*
class JsonRpcMessageFilterTest : public ::testing::Test {
protected:
  void SetUp() override {
    filter_ = std::make_unique<JsonRpcMessageFilter>(callbacks_);
    // Disable framing for tests (use newline-delimited JSON)
    filter_->setUseFraming(false);
  }

  MockMcpProtocolCallbacks callbacks_;
  std::unique_ptr<JsonRpcMessageFilter> filter_;
};
*/

/* Disabled - JsonRpcMessageFilter removed in favor of JsonRpcProtocolFilter
TEST_F(JsonRpcMessageFilterTest, ParseRequest) {
  // Create JSON-RPC request
  std::string request_json =
R"({"jsonrpc":"2.0","id":123,"method":"test_method","params":{"key":"value"}})";

  // Add to buffer
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add(request_json);
  buffer->add("\n");

  // Process through filter
  auto status = filter_->onData(*buffer, false);
  EXPECT_EQ(network::FilterStatus::Continue, status);

  // Verify callback
  EXPECT_EQ(1, callbacks_.request_called_);
  EXPECT_TRUE(mcp::holds_alternative<int64_t>(callbacks_.last_request_.id));
  EXPECT_EQ(123, mcp::get<int64_t>(callbacks_.last_request_.id));
  EXPECT_EQ("test_method", callbacks_.last_request_.method);
  EXPECT_TRUE(callbacks_.last_request_.params.has_value());
}

TEST_F(JsonRpcMessageFilterTest, ParseNotification) {
  // Create JSON-RPC notification
  std::string notification_json =
R"({"jsonrpc":"2.0","method":"notification_method","params":{"value1":1,"value2":2,"value3":3}})";

  // Add to buffer
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add(notification_json);
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
  std::string response_json =
R"({"jsonrpc":"2.0","id":456,"result":{"status":"ok"}})";

  // Add to buffer
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add(response_json);
  buffer->add("\n");

  // Process through filter
  filter_->onData(*buffer, false);

  // Verify callback
  EXPECT_EQ(1, callbacks_.response_called_);
  EXPECT_TRUE(mcp::holds_alternative<int64_t>(callbacks_.last_response_.id));
  EXPECT_EQ(456, mcp::get<int64_t>(callbacks_.last_response_.id));
  EXPECT_TRUE(callbacks_.last_response_.result.has_value());
  EXPECT_FALSE(callbacks_.last_response_.error.has_value());
}

TEST_F(JsonRpcMessageFilterTest, ParseErrorResponse) {
  // Create JSON-RPC error response
  std::string response_json =
R"({"jsonrpc":"2.0","id":789,"error":{"code":-32601,"message":"Method not
found","data":"test_method"}})";

  // Add to buffer
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add(response_json);
  buffer->add("\n");

  // Process through filter
  filter_->onData(*buffer, false);

  // Verify callback
  EXPECT_EQ(1, callbacks_.response_called_);
  EXPECT_TRUE(mcp::holds_alternative<int64_t>(callbacks_.last_response_.id));
  EXPECT_EQ(789, mcp::get<int64_t>(callbacks_.last_response_.id));
  EXPECT_FALSE(callbacks_.last_response_.result.has_value());
  EXPECT_TRUE(callbacks_.last_response_.error.has_value());
  EXPECT_EQ(-32601, callbacks_.last_response_.error->code);
  EXPECT_EQ("Method not found", callbacks_.last_response_.error->message);
}

TEST_F(JsonRpcMessageFilterTest, ParseMultipleMessages) {
  // Add multiple messages
  auto buffer = std::make_unique<OwnedBuffer>();

  buffer->add(R"({"jsonrpc":"2.0","id":1,"method":"method1"})" "\n");
  buffer->add(R"({"jsonrpc":"2.0","method":"notification1"})" "\n");
  buffer->add(R"({"jsonrpc":"2.0","id":2,"result":"ok"})" "\n");

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
  filter_->setUseFraming(true);

  // Create framed message
  std::string json_str = R"({"jsonrpc":"2.0","id":1,"method":"test"})";

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
  filter_->setUseFraming(true);

  // Create message
  auto buffer = std::make_unique<OwnedBuffer>();
  std::string test_data = "{\"test\":\"data\"}";
  buffer->add(test_data);

  size_t original_len = test_data.length();

  // Process through write filter
  filter_->onWrite(*buffer, false);

  // Should have length prefix added
  EXPECT_EQ(original_len + 4, buffer->length());

  // Verify the framing is correct
  std::string framed_data = buffer->toString();
  EXPECT_EQ(original_len + 4, framed_data.length());

  // Check length prefix (big-endian)
  uint32_t len = 0;
  len |= (static_cast<uint8_t>(framed_data[0]) << 24);
  len |= (static_cast<uint8_t>(framed_data[1]) << 16);
  len |= (static_cast<uint8_t>(framed_data[2]) << 8);
  len |= static_cast<uint8_t>(framed_data[3]);
  EXPECT_EQ(original_len, len);

  // Check message content
  EXPECT_EQ(test_data, framed_data.substr(4));
}
*/ // End of disabled JsonRpcMessageFilter tests

// McpConnectionManager tests

class McpConnectionManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto factory = event::createPlatformDefaultDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    socket_interface_ = &network::socketInterface();

    // Create config for stdio transport
    config_.transport_type = TransportType::Stdio;
    config_.stdio_config = transport::StdioTransportSocketConfig{
        .stdin_fd = 0, .stdout_fd = 1, .non_blocking = true};
    config_.buffer_limit = 1024 * 1024;
    config_.connection_timeout = std::chrono::milliseconds(5000);
    config_.use_message_framing = false;

    manager_ = std::make_unique<McpConnectionManager>(
        *dispatcher_, *socket_interface_, config_);

    manager_->setProtocolCallbacks(callbacks_);
  }

  void TearDown() override {
    stopDispatcherThread();
    manager_.reset();
    dispatcher_->exit();
  }

  void startDispatcherThread() {
    if (loop_thread_.joinable()) {
      return;
    }

    loop_thread_ =
        std::thread([this]() { dispatcher_->run(event::RunType::Block); });
  }

  void runOnDispatcher(std::function<void()> fn) {
    std::atomic<bool> done{false};
    dispatcher_->post([&done, fn = std::move(fn)]() {
      fn();
      done = true;
    });

    for (int i = 0; i < 400 && !done.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(done.load()) << "dispatcher never ran the posted callback";
  }

  void stopDispatcherThread() {
    if (loop_thread_.joinable()) {
      dispatcher_->exit();
      loop_thread_.join();
    }
  }

  event::DispatcherPtr dispatcher_;
  network::SocketInterface* socket_interface_;
  McpConnectionConfig config_;
  std::unique_ptr<McpConnectionManager> manager_;
  MockMcpProtocolCallbacks callbacks_;
  std::thread loop_thread_;
};

TEST_F(McpConnectionManagerTest, InitialState) {
  EXPECT_FALSE(manager_->isConnected());
}

TEST_F(McpConnectionManagerTest, DISABLED_ConnectStdio) {
  // TODO: This test is disabled because it tries to use actual stdin/stdout
  // which hangs in unit tests. Need to implement mock stdio transport.

  // Note: This test connects using stdio transport which doesn't do actual I/O
  auto result = manager_->connect();
  ASSERT_FALSE(mcp::holds_alternative<Error>(result));

  // Should be connected
  EXPECT_TRUE(manager_->isConnected());

  // Should receive connected event
  ASSERT_EQ(1, callbacks_.events_.size());
  EXPECT_EQ(network::ConnectionEvent::Connected, callbacks_.events_[0]);
}

TEST_F(McpConnectionManagerTest, DISABLED_SendRequest) {
  // Connect first
  auto result = manager_->connect();
  ASSERT_FALSE(mcp::holds_alternative<Error>(result));

  // Create request
  jsonrpc::Request request;
  request.id = 123;
  request.method = "initialize";
  Metadata params;
  params["version"] = MetadataValue(std::string("1.0"));
  request.params = params;

  // Send request
  result = manager_->sendRequest(request);
  EXPECT_FALSE(mcp::holds_alternative<Error>(result));
}

TEST_F(McpConnectionManagerTest, DISABLED_SendNotification) {
  // Connect first
  auto result = manager_->connect();
  ASSERT_FALSE(mcp::holds_alternative<Error>(result));

  // Create notification
  jsonrpc::Notification notification;
  notification.method = "progress";
  Metadata params;
  params["percent"] = MetadataValue(int64_t(50));
  notification.params = params;

  // Send notification
  result = manager_->sendNotification(notification);
  EXPECT_FALSE(mcp::holds_alternative<Error>(result));
}

TEST_F(McpConnectionManagerTest, DISABLED_SendResponse) {
  // Connect first
  auto result = manager_->connect();
  ASSERT_FALSE(mcp::holds_alternative<Error>(result));

  // Create response
  jsonrpc::Response response;
  response.id = 456;
  response.result = std::string("success");

  // Send response
  result = manager_->sendResponse(response);
  EXPECT_FALSE(mcp::holds_alternative<Error>(result));
}

TEST_F(McpConnectionManagerTest, DISABLED_SendErrorResponse) {
  // Connect first
  auto result = manager_->connect();
  ASSERT_FALSE(mcp::holds_alternative<Error>(result));

  // Create error response
  jsonrpc::Response response;
  response.id = 789;
  Error err;
  err.code = -32601;
  err.message = "Method not found";
  response.error = err;

  // Send response
  result = manager_->sendResponse(response);
  EXPECT_FALSE(mcp::holds_alternative<Error>(result));
}

TEST_F(McpConnectionManagerTest, DISABLED_CloseConnection) {
  // Connect first
  manager_->connect();
  EXPECT_TRUE(manager_->isConnected());

  // Close
  manager_->close();
  EXPECT_FALSE(manager_->isConnected());
}

TEST_F(McpConnectionManagerTest, CloseDefersActiveConnectionDestruction) {
  std::atomic<bool> destroyed{false};
  auto connection =
      std::make_unique<CloseTrackingConnection>(*dispatcher_, destroyed);
  auto* connection_ptr = connection.get();

  manager_->active_connection_ = std::move(connection);
  manager_->connected_ = true;

  manager_->close();

  EXPECT_EQ(1, connection_ptr->close_count_);
  EXPECT_EQ(network::ConnectionCloseType::NoFlush,
            connection_ptr->last_close_type_);
  EXPECT_EQ(connection_ptr, manager_->active_connection_.get());
  EXPECT_FALSE(destroyed.load());
  EXPECT_FALSE(manager_->isConnected());

  startDispatcherThread();
  runOnDispatcher([this]() {
    manager_->onConnectionEvent(network::ConnectionEvent::LocalClose);
  });

  EXPECT_EQ(nullptr, manager_->active_connection_.get());
}

TEST_F(McpConnectionManagerTest, CloseReleasesAlreadyClosingConnection) {
  std::atomic<bool> destroyed{false};
  auto connection =
      std::make_unique<CloseTrackingConnection>(*dispatcher_, destroyed);
  auto* connection_ptr = connection.get();
  connection->setState(network::ConnectionState::Closing);

  manager_->active_connection_ = std::move(connection);
  manager_->connected_ = true;

  startDispatcherThread();
  runOnDispatcher([this]() { manager_->close(); });

  EXPECT_EQ(0, connection_ptr->close_count_);
  EXPECT_EQ(nullptr, manager_->active_connection_.get());
  EXPECT_FALSE(manager_->isConnected());
}

TEST_F(McpConnectionManagerTest, CloseDoesNotForwardAfterCallbacksCleared) {
  std::atomic<bool> destroyed{false};
  auto connection =
      std::make_unique<CloseTrackingConnection>(*dispatcher_, destroyed);
  connection->raise_local_close_on_close_ = true;
  connection->addConnectionCallbacks(*manager_);

  manager_->active_connection_ = std::move(connection);
  manager_->connected_ = true;
  manager_->clearProtocolCallbacks();

  startDispatcherThread();
  runOnDispatcher([this]() { manager_->close(); });

  EXPECT_TRUE(callbacks_.events_.empty());
  EXPECT_EQ(nullptr, manager_->active_connection_.get());
}

TEST_F(McpConnectionManagerTest, ServerCloseDoesNotForwardLifecycleEvent) {
  std::atomic<bool> destroyed{false};
  auto connection =
      std::make_unique<CloseTrackingConnection>(*dispatcher_, destroyed);
  connection->raise_local_close_on_close_ = true;
  connection->addConnectionCallbacks(*manager_);

  manager_->is_server_ = true;
  manager_->active_connection_ = std::move(connection);
  manager_->connected_ = true;

  startDispatcherThread();
  runOnDispatcher([this]() { manager_->close(); });

  EXPECT_TRUE(callbacks_.events_.empty());
  EXPECT_EQ(nullptr, manager_->active_connection_.get());
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
  EXPECT_TRUE(mcp::holds_alternative<int64_t>(callbacks_.last_response_.id));
  EXPECT_EQ(2, mcp::get<int64_t>(callbacks_.last_response_.id));

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
  transport::HttpSseTransportSocketConfig http_sse_config;
  http_sse_config.server_address = "localhost:8080";
  http_sse_config.mode = transport::HttpSseTransportSocketConfig::Mode::CLIENT;
  http_sse_config.underlying_transport =
      transport::HttpSseTransportSocketConfig::UnderlyingTransport::TCP;
  http_sse_config.connect_timeout = std::chrono::milliseconds(10000);
  // Note: Headers are now handled by the filter chain
  http_config.http_sse_config = http_sse_config;

  auto http_manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, *socket_interface_, http_config);

  // Just verify the manager was created with HTTP/SSE config
  // Don't try to connect in unit test as it requires real dispatcher running
  EXPECT_FALSE(http_manager->isConnected());

  // TODO: Add integration test with real dispatcher for HTTP/SSE connections
}

TEST_F(McpConnectionManagerTest, HttpPostFiltersUnsafeAndGeneratedHeaders) {
  LoopbackHttpCapture capture;

  McpConnectionConfig http_config;
  http_config.transport_type = TransportType::HttpSse;
  http_config.http_headers = {{"Authorization", "Bearer base-token"},
                              {"Transfer-Encoding", "chunked"},
                              {"X-Bad-Base", "ok\r\nX-Smuggled: yes"}};

  McpConnectionManager http_manager(*dispatcher_, *socket_interface_,
                                    http_config);
  http_manager.onMessageEndpoint(
      "http://127.0.0.1:" + std::to_string(capture.port()) + "/mcp");

  std::string nul_value = "bad";
  nul_value.push_back('\0');
  nul_value += "value";

  ASSERT_TRUE(
      http_manager.sendHttpPost("{\"jsonrpc\":\"2.0\",\"method\":\"ping\"}",
                                {{"X-Request-ID", "req-1"},
                                 {"Content-Length", "9999"},
                                 {"X-Injected", "ok\r\nX-Injected-Header: yes"},
                                 {"X-Nul", nul_value}}));

  auto& request_future = capture.requestFuture();
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
  while (request_future.wait_for(std::chrono::milliseconds(0)) !=
             std::future_status::ready &&
         std::chrono::steady_clock::now() < deadline) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  ASSERT_EQ(request_future.wait_for(std::chrono::milliseconds(0)),
            std::future_status::ready);
  const std::string request = request_future.get();

  EXPECT_NE(request.find("Authorization: Bearer base-token\r\n"),
            std::string::npos)
      << request;
  EXPECT_NE(request.find("X-Request-ID: req-1\r\n"), std::string::npos)
      << request;
  EXPECT_EQ(request.find("Transfer-Encoding: chunked"), std::string::npos)
      << request;
  EXPECT_EQ(request.find("Content-Length: 9999"), std::string::npos) << request;
  EXPECT_EQ(request.find("X-Smuggled: yes"), std::string::npos) << request;
  EXPECT_EQ(request.find("X-Injected-Header: yes"), std::string::npos)
      << request;
  EXPECT_EQ(request.find("X-Nul:"), std::string::npos) << request;
}

TEST_F(McpConnectionManagerTest, FactoryFunction) {
  // Test factory function
  auto manager = createMcpConnectionManager(*dispatcher_);
  ASSERT_NE(nullptr, manager);

  // Should use default stdio config
  EXPECT_FALSE(manager->isConnected());
}

// Integration test demonstrating usage
TEST_F(McpConnectionManagerTest, DISABLED_UsageExample) {
  // Connect
  auto result = manager_->connect();
  ASSERT_FALSE(mcp::holds_alternative<Error>(result));

  // Send initialize request
  jsonrpc::Request init_request;
  init_request.id = 1;
  init_request.method = "initialize";
  Metadata init_params;
  init_params["protocol_version"] = MetadataValue(std::string("2024-11-05"));

  // Note: Metadata doesn't support nested objects, so we'll just use simple
  // values
  init_params["client_name"] = MetadataValue(std::string("test_client"));
  init_params["client_version"] = MetadataValue(std::string("1.0.0"));

  init_request.params = init_params;

  result = manager_->sendRequest(init_request);
  ASSERT_FALSE(mcp::holds_alternative<Error>(result));

  // In real usage, would run event loop and wait for response
  // dispatcher_->run(event::RunType::Block);
}

}  // namespace
}  // namespace mcp
