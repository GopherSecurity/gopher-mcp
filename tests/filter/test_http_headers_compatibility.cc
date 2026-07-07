/**
 * @file test_http_headers_compatibility.cc
 * @brief Unit tests for HTTP Headers Compatibility
 *
 * Tests for HTTP header fixes:
 * - Accept header includes both application/json and text/event-stream
 * - User-Agent header is present in requests
 *
 * Commit: 19f359f19cf37184636ec745f19fe4087b47052a
 * Feature: HTTP Headers Compatibility (Section 5)
 */

#include <map>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/http_codec_filter.h"

namespace mcp {
namespace filter {
namespace {

/**
 * Mock message callbacks for testing
 */
class TestMessageCallbacks : public HttpCodecFilter::MessageCallbacks {
 public:
  void onHeaders(const std::map<std::string, std::string>& headers,
                 bool keep_alive) override {
    (void)headers;
    (void)keep_alive;
  }

  void onBody(const std::string& data, bool end_stream) override {
    (void)data;
    (void)end_stream;
  }

  void onMessageComplete() override {}

  void onError(const std::string& error) override { (void)error; }
};

/**
 * Test fixture for HTTP Headers Compatibility tests
 */
class HttpHeadersCompatibilityTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    dispatcher_->run(event::RunType::NonBlock);
  }

  void TearDown() override { dispatcher_.reset(); }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  TestMessageCallbacks callbacks_;
};

// =============================================================================
// Accept Header Tests
// =============================================================================

/**
 * Test: POST request Accept header includes application/json
 *
 * The Accept header should include both application/json and text/event-stream
 * to maximize compatibility with different MCP server implementations.
 */
TEST_F(HttpHeadersCompatibilityTest, PostRequestAcceptHeaderIncludesJson) {
  // Create client-mode HTTP codec filter
  HttpCodecFilter filter(callbacks_, *dispatcher_, false /* is_server */);
  filter.setClientEndpoint("/rpc", "localhost:8080");

  // Create a buffer with JSON-RPC data
  OwnedBuffer write_buffer;
  std::string json_data = "{\"jsonrpc\":\"2.0\",\"method\":\"test\",\"id\":1}";
  write_buffer.add(json_data.c_str(), json_data.length());

  // Trigger onWrite to generate HTTP request
  filter.onWrite(write_buffer, false);

  // Get captured HTTP request
  std::string request = write_buffer.toString();

  // Verify Accept header includes application/json
  EXPECT_NE(request.find("Accept: application/json"), std::string::npos)
      << "Accept header should include application/json\nRequest:\n"
      << request;

  // Verify Accept header also includes text/event-stream
  EXPECT_NE(request.find("text/event-stream"), std::string::npos)
      << "Accept header should include text/event-stream";

  // Verify the combined format
  EXPECT_NE(request.find("Accept: application/json, text/event-stream"),
            std::string::npos)
      << "Accept header should have both types in correct format";
}

// =============================================================================
// User-Agent Header Tests
// =============================================================================

/**
 * Test: POST request includes User-Agent header
 */
TEST_F(HttpHeadersCompatibilityTest, PostRequestIncludesUserAgent) {
  HttpCodecFilter filter(callbacks_, *dispatcher_, false /* is_server */);
  filter.setClientEndpoint("/rpc", "localhost:8080");

  OwnedBuffer write_buffer;
  std::string json_data = "{\"jsonrpc\":\"2.0\",\"method\":\"test\",\"id\":1}";
  write_buffer.add(json_data.c_str(), json_data.length());

  filter.onWrite(write_buffer, false);

  std::string request = write_buffer.toString();

  // Verify User-Agent header is present
  EXPECT_NE(request.find("User-Agent: gopher-mcp/1.0"), std::string::npos)
      << "User-Agent header should be present with correct value\nRequest:\n"
      << request;
}

/**
 * Test: SSE GET request includes User-Agent header
 */
TEST_F(HttpHeadersCompatibilityTest, SseGetRequestIncludesUserAgent) {
  HttpCodecFilter filter(callbacks_, *dispatcher_, false /* is_server */);
  filter.setClientEndpoint("/sse", "localhost:8080");
  filter.setUseSseGet(true);  // Enable SSE GET mode

  OwnedBuffer write_buffer;
  // Empty data triggers SSE GET request

  filter.onWrite(write_buffer, false);

  std::string request = write_buffer.toString();

  // Verify it's a GET request
  EXPECT_NE(request.find("GET /sse HTTP/1.1"), std::string::npos)
      << "Should be a GET request for SSE\nRequest:\n"
      << request;

  // Verify User-Agent header is present
  EXPECT_NE(request.find("User-Agent: gopher-mcp/1.0"), std::string::npos)
      << "SSE GET request should include User-Agent header";
}

// =============================================================================
// Combined Header Tests
// =============================================================================

/**
 * Test: POST request has all required headers
 */
TEST_F(HttpHeadersCompatibilityTest, PostRequestHasAllRequiredHeaders) {
  HttpCodecFilter filter(callbacks_, *dispatcher_, false /* is_server */);
  filter.setClientEndpoint("/mcp", "example.com");

  OwnedBuffer write_buffer;
  std::string json_data =
      "{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"id\":1}";
  write_buffer.add(json_data.c_str(), json_data.length());

  filter.onWrite(write_buffer, false);

  std::string request = write_buffer.toString();

  // Verify all required headers
  EXPECT_NE(request.find("POST /mcp HTTP/1.1"), std::string::npos)
      << "Should have POST request line";
  EXPECT_NE(request.find("Host: example.com"), std::string::npos)
      << "Should have Host header";
  EXPECT_NE(request.find("Content-Type: application/json"), std::string::npos)
      << "Should have Content-Type header";
  EXPECT_NE(request.find("Content-Length:"), std::string::npos)
      << "Should have Content-Length header";
  EXPECT_NE(request.find("Accept: application/json, text/event-stream"),
            std::string::npos)
      << "Should have Accept header with both types";
  EXPECT_NE(request.find("Connection: keep-alive"), std::string::npos)
      << "Should have Connection header";
  EXPECT_NE(request.find("User-Agent: gopher-mcp/1.0"), std::string::npos)
      << "Should have User-Agent header";
}

/**
 * Test: SSE GET request has all required headers
 */
TEST_F(HttpHeadersCompatibilityTest, SseGetRequestHasAllRequiredHeaders) {
  HttpCodecFilter filter(callbacks_, *dispatcher_, false /* is_server */);
  filter.setClientEndpoint("/events", "mcp.example.com:3000");
  filter.setUseSseGet(true);

  OwnedBuffer write_buffer;

  filter.onWrite(write_buffer, false);

  std::string request = write_buffer.toString();

  // Verify all required headers for SSE GET
  EXPECT_NE(request.find("GET /events HTTP/1.1"), std::string::npos)
      << "Should have GET request line";
  EXPECT_NE(request.find("Host: mcp.example.com:3000"), std::string::npos)
      << "Should have Host header";
  EXPECT_NE(request.find("Accept: text/event-stream"), std::string::npos)
      << "Should have Accept header for SSE";
  EXPECT_NE(request.find("Cache-Control: no-cache"), std::string::npos)
      << "Should have Cache-Control header";
  EXPECT_NE(request.find("Connection: keep-alive"), std::string::npos)
      << "Should have Connection header";
  EXPECT_NE(request.find("User-Agent: gopher-mcp/1.0"), std::string::npos)
      << "Should have User-Agent header";
}

// =============================================================================
// Header Passthrough Tests
// =============================================================================

TEST_F(HttpHeadersCompatibilityTest, PostRequestIncludesClientHeaders) {
  HttpCodecFilter filter(callbacks_, *dispatcher_, false /* is_server */);
  filter.setClientEndpoint("/mcp", "backend.example.com");
  filter.setClientHeaders(
      {{"Authorization", "Bearer caller-token"}, {"X-Request-ID", "req-123"}});

  OwnedBuffer write_buffer;
  std::string json_data =
      "{\"jsonrpc\":\"2.0\",\"method\":\"tools/list\",\"id\":1}";
  write_buffer.add(json_data.c_str(), json_data.length());

  filter.onWrite(write_buffer, false);

  std::string request = write_buffer.toString();

  EXPECT_NE(request.find("Authorization: Bearer caller-token\r\n"),
            std::string::npos)
      << request;
  EXPECT_NE(request.find("X-Request-ID: req-123\r\n"), std::string::npos)
      << request;
}

TEST_F(HttpHeadersCompatibilityTest, ClientHeadersCannotOverrideGenerated) {
  HttpCodecFilter filter(callbacks_, *dispatcher_, false /* is_server */);
  filter.setClientEndpoint("/mcp", "backend.example.com");
  filter.setClientHeaders({{"Host", "attacker.example.com"},
                           {"Content-Type", "text/plain"},
                           {"Content-Length", "999999"},
                           {"Accept", "text/plain"},
                           {"Connection", "close"},
                           {"User-Agent", "custom-agent"},
                           {"Cache-Control", "max-age=3600"},
                           {"Authorization", "Bearer caller-token"}});

  OwnedBuffer write_buffer;
  std::string json_data =
      "{\"jsonrpc\":\"2.0\",\"method\":\"tools/list\",\"id\":1}";
  write_buffer.add(json_data.c_str(), json_data.length());

  filter.onWrite(write_buffer, false);

  std::string request = write_buffer.toString();

  EXPECT_NE(request.find("Host: backend.example.com\r\n"), std::string::npos)
      << request;
  EXPECT_NE(request.find("Content-Type: application/json\r\n"),
            std::string::npos)
      << request;
  EXPECT_NE(request.find("Content-Length: " + std::to_string(json_data.size()) +
                         "\r\n"),
            std::string::npos)
      << request;
  EXPECT_NE(request.find("Accept: application/json, text/event-stream\r\n"),
            std::string::npos)
      << request;
  EXPECT_NE(request.find("Connection: keep-alive\r\n"), std::string::npos)
      << request;
  EXPECT_NE(request.find("User-Agent: gopher-mcp/1.0\r\n"), std::string::npos)
      << request;
  EXPECT_EQ(request.find("Host: attacker.example.com"), std::string::npos)
      << request;
  EXPECT_EQ(request.find("Content-Type: text/plain"), std::string::npos)
      << request;
  EXPECT_EQ(request.find("Content-Length: 999999"), std::string::npos)
      << request;
  EXPECT_EQ(request.find("Accept: text/plain"), std::string::npos) << request;
  EXPECT_EQ(request.find("Connection: close"), std::string::npos) << request;
  EXPECT_EQ(request.find("User-Agent: custom-agent"), std::string::npos)
      << request;
  EXPECT_EQ(request.find("Cache-Control: max-age=3600"), std::string::npos)
      << request;
  EXPECT_NE(request.find("Authorization: Bearer caller-token\r\n"),
            std::string::npos)
      << request;
}

TEST_F(HttpHeadersCompatibilityTest, ClientHeadersRejectLineInjection) {
  HttpCodecFilter filter(callbacks_, *dispatcher_, false /* is_server */);
  filter.setClientEndpoint("/mcp", "backend.example.com");

  std::string nul_name = "X-Bad";
  nul_name.push_back('\0');
  nul_name += "Name";
  std::string nul_value = "bad";
  nul_value.push_back('\0');
  nul_value += "value";

  filter.setClientHeaders({{"Authorization", "Bearer caller-token"},
                           {"X-Injected-Value", "ok\r\nX-Smuggled: yes"},
                           {"X-Bad\nName", "value"},
                           {nul_name, "value"},
                           {"X-Nul-Value", nul_value}});

  OwnedBuffer write_buffer;
  std::string json_data =
      "{\"jsonrpc\":\"2.0\",\"method\":\"tools/list\",\"id\":1}";
  write_buffer.add(json_data.c_str(), json_data.length());

  filter.onWrite(write_buffer, false);

  std::string request = write_buffer.toString();

  EXPECT_NE(request.find("Authorization: Bearer caller-token\r\n"),
            std::string::npos)
      << request;
  EXPECT_EQ(request.find("X-Smuggled: yes"), std::string::npos) << request;
  EXPECT_EQ(request.find("X-Injected-Value:"), std::string::npos) << request;
  EXPECT_EQ(request.find("X-Bad\nName"), std::string::npos) << request;
  EXPECT_EQ(request.find("X-Nul-Value:"), std::string::npos) << request;
}

TEST_F(HttpHeadersCompatibilityTest, ClientHeaderSourceOverridesStaticHeaders) {
  HttpCodecFilter filter(callbacks_, *dispatcher_, false /* is_server */);
  filter.setClientEndpoint("/mcp", "backend.example.com");
  filter.setClientHeaders(
      {{"Authorization", "Bearer static-token"}, {"X-Static", "yes"}});

  auto current_headers = std::make_shared<std::map<std::string, std::string>>();
  (*current_headers)["Authorization"] = "Bearer per-request-token";
  (*current_headers)["X-Request-ID"] = "req-456";
  filter.setClientHeaderSource(current_headers);

  OwnedBuffer write_buffer;
  std::string json_data =
      "{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"id\":2}";
  write_buffer.add(json_data.c_str(), json_data.length());

  filter.onWrite(write_buffer, false);

  std::string request = write_buffer.toString();

  EXPECT_NE(request.find("Authorization: Bearer per-request-token\r\n"),
            std::string::npos)
      << request;
  EXPECT_NE(request.find("X-Request-ID: req-456\r\n"), std::string::npos)
      << request;
  EXPECT_EQ(request.find("Authorization: Bearer static-token"),
            std::string::npos)
      << request;
  EXPECT_EQ(request.find("X-Static: yes"), std::string::npos) << request;
}

// =============================================================================
// Edge Cases
// =============================================================================

/**
 * Test: Headers are properly terminated with CRLF
 */
TEST_F(HttpHeadersCompatibilityTest, HeadersProperlyTerminated) {
  HttpCodecFilter filter(callbacks_, *dispatcher_, false /* is_server */);
  filter.setClientEndpoint("/rpc", "localhost");

  OwnedBuffer write_buffer;
  std::string json_data = "{\"test\":true}";
  write_buffer.add(json_data.c_str(), json_data.length());

  filter.onWrite(write_buffer, false);

  std::string request = write_buffer.toString();

  // Verify headers end with double CRLF before body
  EXPECT_NE(request.find("\r\n\r\n"), std::string::npos)
      << "Headers should be terminated with double CRLF";

  // Verify User-Agent line ends with CRLF
  EXPECT_NE(request.find("User-Agent: gopher-mcp/1.0\r\n"), std::string::npos)
      << "User-Agent header should end with CRLF";
}

/**
 * Test: Accept header format is correct (no trailing spaces)
 */
TEST_F(HttpHeadersCompatibilityTest, AcceptHeaderFormatCorrect) {
  HttpCodecFilter filter(callbacks_, *dispatcher_, false /* is_server */);
  filter.setClientEndpoint("/rpc", "localhost");

  OwnedBuffer write_buffer;
  std::string json_data = "{\"test\":true}";
  write_buffer.add(json_data.c_str(), json_data.length());

  filter.onWrite(write_buffer, false);

  std::string request = write_buffer.toString();

  // Verify exact Accept header format
  EXPECT_NE(request.find("Accept: application/json, text/event-stream\r\n"),
            std::string::npos)
      << "Accept header should have exact format with CRLF termination";
}

}  // namespace
}  // namespace filter
}  // namespace mcp
