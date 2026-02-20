/**
 * @file test_http_codec_sse_get.cc
 * @brief Unit tests for HTTP codec SSE GET request functionality
 *
 * Tests for Section 1b implementation (commit cca768c5):
 * - SSE GET request generation with client endpoint configuration
 * - Message endpoint path extraction from full URL
 * - SSE GET state tracking (has_sent_sse_get_request_)
 * - POST request routing to message endpoint
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/http_codec_filter.h"

namespace mcp {
namespace filter {
namespace {

using ::testing::_;
using ::testing::NiceMock;

/**
 * Mock HTTP message callbacks
 */
class MockMessageCallbacks : public HttpCodecFilter::MessageCallbacks {
 public:
  MOCK_METHOD(void,
              onHeaders,
              ((const std::map<std::string, std::string>&), bool),
              (override));
  MOCK_METHOD(void, onBody, (const std::string&, bool), (override));
  MOCK_METHOD(void, onMessageComplete, (), (override));
  MOCK_METHOD(void, onError, (const std::string&), (override));
};

/**
 * Test fixture for HTTP codec SSE GET functionality
 */
class HttpCodecSseGetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create dispatcher
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");

    // Create mock callbacks
    callbacks_ = std::make_unique<NiceMock<MockMessageCallbacks>>();

    // Create HTTP codec filter in client mode
    filter_ =
        std::make_unique<HttpCodecFilter>(*callbacks_, *dispatcher_, false);

    // Initialize filter
    filter_->onNewConnection();
  }

  void TearDown() override {
    filter_.reset();
    callbacks_.reset();
    dispatcher_.reset();
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::unique_ptr<MockMessageCallbacks> callbacks_;
  std::unique_ptr<HttpCodecFilter> filter_;
};

// =============================================================================
// SSE GET Request Generation Tests
// =============================================================================

/**
 * Test: setClientEndpoint() configures path and host
 */
TEST_F(HttpCodecSseGetTest, SetClientEndpointConfiguresPathAndHost) {
  filter_->setClientEndpoint("/sse", "localhost:8080");

  // Configuration is internal, but we can verify by triggering SSE GET
  filter_->setUseSseGet(true);

  // Trigger SSE GET with empty buffer
  OwnedBuffer buffer;
  auto status = filter_->onWrite(buffer, false);

  EXPECT_EQ(status, network::FilterStatus::Continue);
  EXPECT_GT(buffer.length(), 0);

  // Extract the HTTP request
  std::string request_str(
      static_cast<const char*>(buffer.linearize(buffer.length())),
      buffer.length());

  // Verify it's a GET request to the configured path
  EXPECT_NE(request_str.find("GET /sse HTTP/1.1"), std::string::npos);
  EXPECT_NE(request_str.find("Host: localhost:8080"), std::string::npos);
}

/**
 * Test: SSE GET request is generated on first write with empty buffer
 */
TEST_F(HttpCodecSseGetTest, SseGetGeneratedOnFirstWriteWithEmptyBuffer) {
  filter_->setClientEndpoint("/api/sse", "example.com");
  filter_->setUseSseGet(true);

  EXPECT_FALSE(filter_->hasSentSseGetRequest());

  // First write with empty buffer triggers SSE GET
  OwnedBuffer buffer;
  auto status = filter_->onWrite(buffer, false);

  EXPECT_EQ(status, network::FilterStatus::Continue);
  EXPECT_TRUE(filter_->hasSentSseGetRequest());

  // Verify GET request was generated
  EXPECT_GT(buffer.length(), 0);
  std::string request_str(
      static_cast<const char*>(buffer.linearize(buffer.length())),
      buffer.length());

  EXPECT_NE(request_str.find("GET /api/sse HTTP/1.1"), std::string::npos);
  EXPECT_NE(request_str.find("Accept: text/event-stream"), std::string::npos);
  EXPECT_NE(request_str.find("Cache-Control: no-cache"), std::string::npos);
  EXPECT_NE(request_str.find("Connection: keep-alive"), std::string::npos);
}

/**
 * Test: SSE GET is only sent once
 */
TEST_F(HttpCodecSseGetTest, SseGetSentOnlyOnce) {
  filter_->setClientEndpoint("/sse", "localhost");
  filter_->setUseSseGet(true);

  // First write triggers GET
  OwnedBuffer buffer1;
  filter_->onWrite(buffer1, false);
  EXPECT_TRUE(filter_->hasSentSseGetRequest());
  size_t first_len = buffer1.length();
  EXPECT_GT(first_len, 0);

  // Second write with empty buffer should NOT generate another GET
  OwnedBuffer buffer2;
  filter_->onWrite(buffer2, false);
  EXPECT_EQ(buffer2.length(), 0);  // Should be empty, no GET generated

  // Third write with empty buffer should also be empty
  OwnedBuffer buffer3;
  filter_->onWrite(buffer3, false);
  EXPECT_EQ(buffer3.length(), 0);
}

/**
 * Test: Without setUseSseGet(), no GET is generated
 */
TEST_F(HttpCodecSseGetTest, NoSseGetWithoutUseSseGetFlag) {
  filter_->setClientEndpoint("/sse", "localhost");
  // DON'T call setUseSseGet(true)

  // Write with empty buffer
  OwnedBuffer buffer;
  auto status = filter_->onWrite(buffer, false);

  // Should return early, no GET generated
  EXPECT_EQ(status, network::FilterStatus::Continue);
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_FALSE(filter_->hasSentSseGetRequest());
}

// =============================================================================
// Message Endpoint Configuration Tests
// =============================================================================

/**
 * Test: setMessageEndpoint() stores endpoint URL
 */
TEST_F(HttpCodecSseGetTest, SetMessageEndpointStoresUrl) {
  EXPECT_FALSE(filter_->hasMessageEndpoint());

  filter_->setMessageEndpoint("http://localhost:8080/api/message");

  EXPECT_TRUE(filter_->hasMessageEndpoint());
  EXPECT_EQ(filter_->getMessageEndpoint(), "http://localhost:8080/api/message");
}

/**
 * Test: POST request uses message endpoint path if available
 */
TEST_F(HttpCodecSseGetTest, PostUsesMessageEndpointPathIfAvailable) {
  filter_->setClientEndpoint("/sse", "localhost:8080");
  filter_->setUseSseGet(true);

  // First, send SSE GET
  OwnedBuffer get_buffer;
  filter_->onWrite(get_buffer, false);
  EXPECT_TRUE(filter_->hasSentSseGetRequest());

  // Now set message endpoint (simulating endpoint event received)
  filter_->setMessageEndpoint("http://localhost:8080/api/message");

  // Send POST request
  std::string json_body = R"({"jsonrpc":"2.0","method":"test","id":1})";
  OwnedBuffer post_buffer;
  post_buffer.add(json_body);

  filter_->onWrite(post_buffer, false);

  // Extract the HTTP request
  std::string request_str(
      static_cast<const char*>(post_buffer.linearize(post_buffer.length())),
      post_buffer.length());

  // Should use message endpoint path, not default client path
  EXPECT_NE(request_str.find("POST /api/message HTTP/1.1"), std::string::npos);
  EXPECT_EQ(request_str.find("POST /sse HTTP/1.1"), std::string::npos);
}

/**
 * Test: POST extracts path from full URL with protocol
 */
TEST_F(HttpCodecSseGetTest, PostExtractsPathFromFullUrl) {
  filter_->setClientEndpoint("/default", "localhost");
  filter_->setUseSseGet(true);

  // Send SSE GET first
  OwnedBuffer get_buffer;
  filter_->onWrite(get_buffer, false);

  // Set message endpoint with full URL
  filter_->setMessageEndpoint("https://example.com:8080/custom/endpoint/path");

  // Send POST
  std::string json_body = R"({"test":"data"})";
  OwnedBuffer post_buffer;
  post_buffer.add(json_body);
  filter_->onWrite(post_buffer, false);

  std::string request_str(
      static_cast<const char*>(post_buffer.linearize(post_buffer.length())),
      post_buffer.length());

  // Should extract /custom/endpoint/path from the full URL
  EXPECT_NE(request_str.find("POST /custom/endpoint/path HTTP/1.1"),
            std::string::npos);
}

/**
 * Test: POST uses endpoint path if it's already just a path
 */
TEST_F(HttpCodecSseGetTest, PostUsesEndpointPathDirectly) {
  filter_->setClientEndpoint("/default", "localhost");
  filter_->setUseSseGet(true);

  // Send SSE GET
  OwnedBuffer get_buffer;
  filter_->onWrite(get_buffer, false);

  // Set message endpoint as just a path (no protocol)
  filter_->setMessageEndpoint("/message");

  // Send POST
  std::string json_body = R"({"data":"value"})";
  OwnedBuffer post_buffer;
  post_buffer.add(json_body);
  filter_->onWrite(post_buffer, false);

  std::string request_str(
      static_cast<const char*>(post_buffer.linearize(post_buffer.length())),
      post_buffer.length());

  EXPECT_NE(request_str.find("POST /message HTTP/1.1"), std::string::npos);
}

/**
 * Test: POST uses default path if no message endpoint set
 */
TEST_F(HttpCodecSseGetTest, PostUsesDefaultPathWithoutMessageEndpoint) {
  filter_->setClientEndpoint("/default-path", "localhost");
  filter_->setUseSseGet(true);

  // Send SSE GET
  OwnedBuffer get_buffer;
  filter_->onWrite(get_buffer, false);

  // DON'T set message endpoint

  // Send POST
  std::string json_body = R"({"test":"request"})";
  OwnedBuffer post_buffer;
  post_buffer.add(json_body);
  filter_->onWrite(post_buffer, false);

  std::string request_str(
      static_cast<const char*>(post_buffer.linearize(post_buffer.length())),
      post_buffer.length());

  // Should use default client path
  EXPECT_NE(request_str.find("POST /default-path HTTP/1.1"), std::string::npos);
}

// =============================================================================
// Integration Tests
// =============================================================================

/**
 * Test: Full SSE GET → Endpoint → POST flow
 */
TEST_F(HttpCodecSseGetTest, FullSseGetEndpointPostFlow) {
  // Configure for SSE mode
  filter_->setClientEndpoint("/sse", "server.example.com");
  filter_->setUseSseGet(true);

  // Step 1: Generate SSE GET request
  OwnedBuffer get_buffer;
  filter_->onWrite(get_buffer, false);

  EXPECT_TRUE(filter_->hasSentSseGetRequest());
  std::string get_request(
      static_cast<const char*>(get_buffer.linearize(get_buffer.length())),
      get_buffer.length());
  EXPECT_NE(get_request.find("GET /sse HTTP/1.1"), std::string::npos);

  // Step 2: Simulate receiving endpoint event
  filter_->setMessageEndpoint("http://server.example.com/api/rpc");
  EXPECT_TRUE(filter_->hasMessageEndpoint());

  // Step 3: Generate POST request
  std::string json_rpc = R"({"jsonrpc":"2.0","method":"initialize","id":1})";
  OwnedBuffer post_buffer;
  post_buffer.add(json_rpc);
  filter_->onWrite(post_buffer, false);

  std::string post_request(
      static_cast<const char*>(post_buffer.linearize(post_buffer.length())),
      post_buffer.length());

  // Verify POST uses the message endpoint path
  EXPECT_NE(post_request.find("POST /api/rpc HTTP/1.1"), std::string::npos);
  EXPECT_NE(post_request.find("Host: server.example.com"), std::string::npos);
  EXPECT_NE(post_request.find("Content-Type: application/json"),
            std::string::npos);
  EXPECT_NE(post_request.find(json_rpc), std::string::npos);
}

}  // namespace
}  // namespace filter
}  // namespace mcp
