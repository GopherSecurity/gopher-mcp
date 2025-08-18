/**
 * @file test_http_server_codec_filter_simple.cc
 * @brief Simple integration tests for HTTP server codec filter with state machine
 */

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <map>

#include "mcp/filter/http_codec_filter.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/buffer.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;

// Simple request callbacks implementation
class TestRequestCallbacks : public HttpCodecFilter::MessageCallbacks {
public:
  void onHeaders(const std::map<std::string, std::string>& headers, bool keep_alive) override {
    headers_received_ = true;
    headers_ = headers;
    keep_alive_ = keep_alive;
  }
  
  void onBody(const std::string& data, bool end_stream) override {
    body_received_ = true;
    body_ = data;
    end_stream_ = end_stream;
  }
  
  void onMessageComplete() override {
    message_complete_ = true;
  }
  
  void onError(const std::string& error) override {
    error_received_ = true;
    error_message_ = error;
  }

  // Test state
  bool headers_received_{false};
  bool body_received_{false};
  bool message_complete_{false};
  bool error_received_{false};
  std::map<std::string, std::string> headers_;
  std::string body_;
  std::string error_message_;
  bool keep_alive_{false};
  bool end_stream_{false};
};

class HttpCodecFilterIntegrationTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create dispatcher
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    dispatcher_->run(event::RunType::NonBlock);
    
    // Create filter (server mode)
    filter_ = std::make_unique<HttpCodecFilter>(callbacks_, *dispatcher_, true);
  }

  void TearDown() override {
    filter_.reset();
    dispatcher_.reset();
  }

  // Helper to create HTTP request data
  OwnedBuffer createGetRequest(const std::string& path) {
    OwnedBuffer buffer;
    std::string request = "GET " + path + " HTTP/1.1\r\n";
    request += "Host: example.com\r\n";
    request += "User-Agent: test-client\r\n";
    request += "\r\n";
    
    buffer.add(request.c_str(), request.length());
    return buffer;
  }

  OwnedBuffer createPostRequest(const std::string& path, const std::string& body) {
    OwnedBuffer buffer;
    std::string request = "POST " + path + " HTTP/1.1\r\n";
    request += "Host: example.com\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + std::to_string(body.length()) + "\r\n";
    request += "\r\n";
    request += body;
    
    buffer.add(request.c_str(), request.length());
    return buffer;
  }

  // Helper to run dispatcher briefly
  void runFor(std::chrono::milliseconds duration) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < duration) {
      dispatcher_->run(event::RunType::NonBlock);
      std::this_thread::sleep_for(1ms);
    }
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  TestRequestCallbacks callbacks_;
  std::unique_ptr<HttpCodecFilter> filter_;
};

// ===== Basic Integration Tests =====

TEST_F(HttpCodecFilterIntegrationTest, FilterCreation) {
  // Test that filter can be created successfully
  EXPECT_NE(filter_, nullptr);
  EXPECT_EQ(filter_->onNewConnection(), network::FilterStatus::Continue);
}

TEST_F(HttpCodecFilterIntegrationTest, SimpleGetRequest) {
  filter_->onNewConnection();
  
  auto request = createGetRequest("/test");
  EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);
  
  runFor(10ms);
  
  // Verify request was processed
  EXPECT_TRUE(callbacks_.headers_received_);
  EXPECT_TRUE(callbacks_.message_complete_);
  EXPECT_FALSE(callbacks_.error_received_);
  
  // Check headers
  auto url_it = callbacks_.headers_.find("url");
  EXPECT_NE(url_it, callbacks_.headers_.end());
  EXPECT_EQ(url_it->second, "/test");
  
  auto host_it = callbacks_.headers_.find("host");
  EXPECT_NE(host_it, callbacks_.headers_.end());
  EXPECT_EQ(host_it->second, "example.com");
}

TEST_F(HttpCodecFilterIntegrationTest, PostRequestWithBody) {
  filter_->onNewConnection();
  
  std::string body = R"({"message": "hello world"})";
  auto request = createPostRequest("/api/data", body);
  
  EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);
  
  runFor(10ms);
  
  // Verify request was processed
  EXPECT_TRUE(callbacks_.headers_received_);
  EXPECT_TRUE(callbacks_.body_received_);
  EXPECT_TRUE(callbacks_.message_complete_);
  EXPECT_FALSE(callbacks_.error_received_);
  
  // Check body
  EXPECT_EQ(callbacks_.body_, body);
  EXPECT_TRUE(callbacks_.end_stream_);
  
  // Check content type header
  auto content_type_it = callbacks_.headers_.find("content-type");
  EXPECT_NE(content_type_it, callbacks_.headers_.end());
  EXPECT_EQ(content_type_it->second, "application/json");
}

TEST_F(HttpCodecFilterIntegrationTest, KeepAliveConnection) {
  filter_->onNewConnection();
  
  auto request = createGetRequest("/test1");
  EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);
  
  runFor(10ms);
  
  EXPECT_TRUE(callbacks_.headers_received_);
  EXPECT_TRUE(callbacks_.keep_alive_);
  
  // Reset callbacks for second request
  callbacks_ = TestRequestCallbacks{};
  
  // Send second request
  auto request2 = createGetRequest("/test2");
  EXPECT_EQ(filter_->onData(request2, false), network::FilterStatus::Continue);
  
  runFor(10ms);
  
  EXPECT_TRUE(callbacks_.headers_received_);
  auto url_it = callbacks_.headers_.find("url");
  EXPECT_NE(url_it, callbacks_.headers_.end());
  EXPECT_EQ(url_it->second, "/test2");
}

TEST_F(HttpCodecFilterIntegrationTest, MalformedRequest) {
  filter_->onNewConnection();
  
  OwnedBuffer malformed;
  malformed.add("INVALID HTTP REQUEST\r\n\r\n", 24);
  
  EXPECT_EQ(filter_->onData(malformed, false), network::FilterStatus::Continue);
  
  runFor(10ms);
  
  // Should receive error
  EXPECT_TRUE(callbacks_.error_received_);
  EXPECT_FALSE(callbacks_.error_message_.empty());
}

// ===== State Machine Integration Tests =====

TEST_F(HttpCodecFilterIntegrationTest, StateMachineIntegration) {
  filter_->onNewConnection();
  
  // The state machine should be properly integrated and handle the request lifecycle
  auto request = createGetRequest("/state-test");
  EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);
  
  runFor(10ms);
  
  // Should complete successfully with state machine managing the flow
  EXPECT_TRUE(callbacks_.headers_received_);
  EXPECT_TRUE(callbacks_.message_complete_);
  EXPECT_FALSE(callbacks_.error_received_);
}

} // namespace
} // namespace filter
} // namespace mcp