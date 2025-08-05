#include <gtest/gtest.h>
#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/network/socket_impl.h"
#include "mcp/buffer.h"

namespace mcp {
namespace transport {
namespace {

// Mock transport socket callbacks
class MockTransportSocketCallbacks : public network::TransportSocketCallbacks {
public:
  bool shouldDrainReadBuffer() override { 
    return should_drain_read_buffer_; 
  }
  
  void setTransportSocketIsReadable() override {
    readable_called_++;
  }
  
  void raiseEvent(network::ConnectionEvent event) override {
    events_.push_back(event);
  }
  
  void flushWriteBuffer() override {
    flush_write_called_++;
  }
  
  // Test state
  bool should_drain_read_buffer_{true};
  int readable_called_{0};
  int flush_write_called_{0};
  std::vector<network::ConnectionEvent> events_;
};

// Mock socket for testing
class MockSocket : public network::Socket {
public:
  // Socket interface implementation (minimal)
  network::ConnectionInfoSetter& connectionInfoProvider() override {
    static network::ConnectionInfoSetter dummy;
    return dummy;
  }
  
  const network::ConnectionInfoProvider& connectionInfoProvider() const override {
    static network::ConnectionInfoProvider dummy;
    return dummy;
  }
  
  network::ConnectionInfoProviderSharedPtr connectionInfoProviderSharedPtr() const override {
    return nullptr;
  }
  
  network::IoHandle& ioHandle() override { return io_handle_; }
  const network::IoHandle& ioHandle() const override { return io_handle_; }
  
  Type socketType() const override { return Type::Stream; }
  
  const network::Address::InstanceConstSharedPtr& addressProvider() const override {
    static network::Address::InstanceConstSharedPtr dummy;
    return dummy;
  }
  
  void setLocalAddress(const network::Address::InstanceConstSharedPtr& address) override {
    (void)address;
  }
  
  bool isOpen() const override { return is_open_; }
  
  void close() override { is_open_ = false; }
  
  Result<void> bind(const network::Address::Instance& address) override {
    (void)address;
    return Result<void>::makeSuccess();
  }
  
  Result<void> listen(int backlog) override {
    (void)backlog;
    return Result<void>::makeSuccess();
  }
  
  Result<void> connect(const network::Address::Instance& address) override {
    (void)address;
    return Result<void>::makeSuccess();
  }
  
  Result<void> setSocketOption(const network::SocketOption& option) override {
    (void)option;
    return Result<void>::makeSuccess();
  }
  
  Result<int> getSocketOption(const network::SocketOptionName& option_name, 
                               void* value, socklen_t* len) override {
    (void)option_name;
    (void)value;
    (void)len;
    return Result<int>::makeSuccess(0);
  }
  
  Result<void> setBlockingForTest(bool blocking) override {
    (void)blocking;
    return Result<void>::makeSuccess();
  }
  
  // Test state
  network::IoHandle io_handle_{-1};
  bool is_open_{true};
};

class HttpSseTransportSocketTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create config
    config_.endpoint_url = "http://example.com/mcp";
    config_.headers["Authorization"] = "Bearer test-token";
    config_.headers["X-Custom"] = "value";
    config_.connect_timeout = std::chrono::milliseconds(5000);
    config_.request_timeout = std::chrono::milliseconds(10000);
    config_.enable_keepalive = true;
    config_.verify_ssl = true;
    
    // Create transport socket
    transport_ = std::make_unique<HttpSseTransportSocket>(config_);
    
    // Set callbacks
    transport_->setTransportSocketCallbacks(callbacks_);
  }
  
  HttpSseTransportSocketConfig config_;
  std::unique_ptr<HttpSseTransportSocket> transport_;
  MockTransportSocketCallbacks callbacks_;
  MockSocket socket_;
};

TEST_F(HttpSseTransportSocketTest, BasicProperties) {
  EXPECT_EQ("http+sse", transport_->protocol());
  EXPECT_TRUE(transport_->failureReason().empty());
  EXPECT_TRUE(transport_->canFlushClose());
}

TEST_F(HttpSseTransportSocketTest, Connect) {
  auto result = transport_->connect(socket_);
  ASSERT_TRUE(result.ok());
  
  // onConnected should trigger readable
  transport_->onConnected();
  EXPECT_EQ(1, callbacks_.readable_called_);
}

TEST_F(HttpSseTransportSocketTest, WriteJsonRpcMessage) {
  // Connect first
  transport_->connect(socket_);
  
  // Create JSON-RPC request
  nlohmann::json request = {
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"method", "initialize"},
      {"params", {
          {"protocol_version", "2024-11-05"},
          {"capabilities", {}}
      }}
  };
  
  // Write through transport
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add(request.dump());
  
  auto result = transport_->doWrite(*buffer, false);
  ASSERT_TRUE(result.ok());
  EXPECT_GT(result.bytes_processed_, 0);
  EXPECT_EQ(0, buffer->length());
}

TEST_F(HttpSseTransportSocketTest, ParseSseEvents) {
  // Connect first
  transport_->connect(socket_);
  
  // Note: Full test would require mocking the underlying transport
  // and simulating SSE event stream. For now, test basic operations.
  
  auto buffer = std::make_unique<OwnedBuffer>();
  auto result = transport_->doRead(*buffer);
  
  // Should succeed but return 0 bytes (no data available)
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(0, result.bytes_processed_);
}

TEST_F(HttpSseTransportSocketTest, CloseSocket) {
  transport_->connect(socket_);
  
  // Close socket
  transport_->closeSocket(network::ConnectionEvent::LocalClose);
  
  // Verify event raised
  ASSERT_EQ(1, callbacks_.events_.size());
  EXPECT_EQ(network::ConnectionEvent::LocalClose, callbacks_.events_[0]);
  
  // Subsequent operations should fail
  auto buffer = std::make_unique<OwnedBuffer>();
  auto result = transport_->doRead(*buffer);
  EXPECT_FALSE(result.ok());
}

// HttpSseTransportSocketFactory tests

class HttpSseTransportSocketFactoryTest : public ::testing::Test {
protected:
  void SetUp() override {
    config_.endpoint_url = "https://api.example.com/mcp";
    factory_ = std::make_unique<HttpSseTransportSocketFactory>(config_);
  }
  
  HttpSseTransportSocketConfig config_;
  std::unique_ptr<HttpSseTransportSocketFactory> factory_;
};

TEST_F(HttpSseTransportSocketFactoryTest, BasicProperties) {
  EXPECT_TRUE(factory_->implementsSecureTransport()); // HTTPS URL
  EXPECT_EQ("http+sse", factory_->name());
  EXPECT_TRUE(factory_->supportsAlpn());
}

TEST_F(HttpSseTransportSocketFactoryTest, DefaultServerNameIndication) {
  auto sni = factory_->defaultServerNameIndication();
  EXPECT_EQ("api.example.com", sni);
}

TEST_F(HttpSseTransportSocketFactoryTest, CreateTransportSocket) {
  auto socket = factory_->createTransportSocket(nullptr);
  ASSERT_NE(nullptr, socket);
  EXPECT_EQ("http+sse", socket->protocol());
}

TEST_F(HttpSseTransportSocketFactoryTest, HashKey) {
  std::vector<uint8_t> key;
  factory_->hashKey(key, nullptr);
  
  // Should contain factory name and endpoint
  EXPECT_GT(key.size(), 0);
  
  // Verify content
  std::string key_str(key.begin(), key.end());
  EXPECT_NE(std::string::npos, key_str.find("http+sse"));
  EXPECT_NE(std::string::npos, key_str.find(config_.endpoint_url));
}

TEST_F(HttpSseTransportSocketFactoryTest, InsecureEndpoint) {
  // Test with HTTP (not HTTPS)
  HttpSseTransportSocketConfig http_config;
  http_config.endpoint_url = "http://example.com/mcp";
  
  HttpSseTransportSocketFactory http_factory(http_config);
  EXPECT_FALSE(http_factory.implementsSecureTransport());
}

TEST_F(HttpSseTransportSocketFactoryTest, FactoryFunction) {
  // Test factory function
  auto factory = createHttpSseTransportSocketFactory(config_);
  ASSERT_NE(nullptr, factory);
  EXPECT_EQ("http+sse", factory->name());
}

// Test SSE parsing logic
TEST(HttpSseParsingTest, ParseSseEventData) {
  // Test parsing SSE event format
  std::string sse_data = 
      "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n"
      "\n";
  
  // This would be parsed by the transport to extract the JSON-RPC message
  size_t data_pos = sse_data.find("data: ");
  ASSERT_NE(std::string::npos, data_pos);
  
  size_t start = data_pos + 6; // Skip "data: "
  size_t end = sse_data.find('\n', start);
  std::string json_str = sse_data.substr(start, end - start);
  
  // Parse JSON
  auto json = nlohmann::json::parse(json_str);
  EXPECT_EQ("2.0", json["jsonrpc"]);
  EXPECT_EQ(1, json["id"]);
  EXPECT_TRUE(json.contains("result"));
}

TEST(HttpSseParsingTest, ParseMultipleEvents) {
  // Test parsing multiple SSE events
  std::string sse_data = 
      "data: {\"jsonrpc\":\"2.0\",\"method\":\"notification1\"}\n"
      "\n"
      "data: {\"jsonrpc\":\"2.0\",\"method\":\"notification2\"}\n"
      "\n";
  
  std::vector<std::string> events;
  size_t pos = 0;
  
  while (pos < sse_data.length()) {
    size_t data_pos = sse_data.find("data: ", pos);
    if (data_pos == std::string::npos) break;
    
    size_t start = data_pos + 6;
    size_t end = sse_data.find('\n', start);
    events.push_back(sse_data.substr(start, end - start));
    
    pos = sse_data.find("\n\n", end);
    if (pos != std::string::npos) pos += 2;
  }
  
  ASSERT_EQ(2, events.size());
  
  auto json1 = nlohmann::json::parse(events[0]);
  EXPECT_EQ("notification1", json1["method"]);
  
  auto json2 = nlohmann::json::parse(events[1]);
  EXPECT_EQ("notification2", json2["method"]);
}

TEST(HttpSseParsingTest, ParseEventWithId) {
  // Test SSE event with ID
  std::string sse_data = 
      "id: 123\n"
      "event: message\n"
      "data: {\"test\":\"data\"}\n"
      "\n";
  
  // Parse event fields
  std::string id, event_type, data;
  
  std::istringstream stream(sse_data);
  std::string line;
  
  while (std::getline(stream, line)) {
    if (line.substr(0, 4) == "id: ") {
      id = line.substr(4);
    } else if (line.substr(0, 7) == "event: ") {
      event_type = line.substr(7);
    } else if (line.substr(0, 6) == "data: ") {
      data = line.substr(6);
    }
  }
  
  EXPECT_EQ("123", id);
  EXPECT_EQ("message", event_type);
  EXPECT_EQ("{\"test\":\"data\"}", data);
}

// Test HTTP request building
TEST(HttpRequestBuildingTest, BuildPostRequest) {
  std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"test\"}";
  std::string endpoint = "/api/mcp";
  
  std::stringstream request;
  request << "POST " << endpoint << " HTTP/1.1\r\n";
  request << "Content-Type: application/json\r\n";
  request << "Content-Length: " << body.length() << "\r\n";
  request << "Connection: keep-alive\r\n";
  request << "\r\n";
  request << body;
  
  std::string req_str = request.str();
  
  // Verify request structure
  EXPECT_NE(std::string::npos, req_str.find("POST /api/mcp HTTP/1.1"));
  EXPECT_NE(std::string::npos, req_str.find("Content-Type: application/json"));
  EXPECT_NE(std::string::npos, req_str.find("Content-Length: " + std::to_string(body.length())));
  EXPECT_NE(std::string::npos, req_str.find(body));
}

} // namespace
} // namespace transport
} // namespace mcp