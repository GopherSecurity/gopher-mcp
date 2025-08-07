#include <gtest/gtest.h>
#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/buffer.h"
#include "mcp/json_bridge.h"

namespace mcp {
namespace transport {
namespace {

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
  auto json = mcp::json::JsonValue::parse(json_str);
  EXPECT_EQ("2.0", json["jsonrpc"].getString());
  EXPECT_EQ(1, json["id"].getInt());
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
  
  auto json1 = mcp::json::JsonValue::parse(events[0]);
  EXPECT_EQ("notification1", json1["method"].getString());
  
  auto json2 = mcp::json::JsonValue::parse(events[1]);
  EXPECT_EQ("notification2", json2["method"].getString());
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