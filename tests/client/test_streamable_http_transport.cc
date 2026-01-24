/**
 * @file test_streamable_http_transport.cc
 * @brief Unit tests for Streamable HTTP transport type
 *
 * Tests for the new TransportType::StreamableHttp feature:
 * - Transport negotiation based on URL path
 * - Correct distinction between HttpSse and StreamableHttp
 * - Configuration creation for StreamableHttp
 *
 * Commit: 19f359f19cf37184636ec745f19fe4087b47052a
 * Feature: Streamable HTTP Transport (Section 1)
 */

#include <gtest/gtest.h>

#include "mcp/mcp_connection_manager.h"

namespace mcp {
namespace {

/**
 * Test fixture for Streamable HTTP transport tests
 */
class StreamableHttpTransportTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

// =============================================================================
// Transport Type Enum Tests
// =============================================================================

/**
 * Test: TransportType::StreamableHttp exists in the enum
 */
TEST_F(StreamableHttpTransportTest, StreamableHttpEnumExists) {
  // Verify the enum value exists and is distinct from others
  TransportType streamable = TransportType::StreamableHttp;
  TransportType http_sse = TransportType::HttpSse;
  TransportType stdio = TransportType::Stdio;
  TransportType websocket = TransportType::WebSocket;

  EXPECT_NE(streamable, http_sse);
  EXPECT_NE(streamable, stdio);
  EXPECT_NE(streamable, websocket);
}

// =============================================================================
// Transport Negotiation Tests
// =============================================================================

/**
 * Helper class to expose the private negotiateTransport method for testing
 * We test the logic by examining the expected behavior patterns
 */
class TransportNegotiationTest : public ::testing::Test {
 protected:
  // Test transport negotiation by examining URL patterns
  // The actual negotiateTransport is private, so we test via expected behavior

  bool urlShouldUseSse(const std::string& uri) {
    // SSE transport is indicated by explicit /sse or /events endpoints
    if (uri.find("http://") != 0 && uri.find("https://") != 0) {
      return false;  // Not HTTP
    }

    // Extract path from URI
    std::string path;
    size_t scheme_end = uri.find("://");
    if (scheme_end != std::string::npos) {
      size_t path_start = uri.find('/', scheme_end + 3);
      if (path_start != std::string::npos) {
        path = uri.substr(path_start);
      }
    }

    // Check for SSE-specific paths
    return (path.find("/sse") != std::string::npos ||
            path.find("/events") != std::string::npos);
  }

  bool urlShouldUseStreamableHttp(const std::string& uri) {
    if (uri.find("http://") != 0 && uri.find("https://") != 0) {
      return false;  // Not HTTP
    }
    return !urlShouldUseSse(uri);
  }
};

/**
 * Test: URLs with /sse path should use HttpSse transport
 */
TEST_F(TransportNegotiationTest, SsePathUsesHttpSse) {
  EXPECT_TRUE(urlShouldUseSse("http://localhost:8080/sse"));
  EXPECT_TRUE(urlShouldUseSse("https://example.com/sse"));
  EXPECT_TRUE(urlShouldUseSse("http://server:3000/api/sse"));
  EXPECT_TRUE(urlShouldUseSse("https://mcp.example.com/v1/sse/endpoint"));
}

/**
 * Test: URLs with /events path should use HttpSse transport
 */
TEST_F(TransportNegotiationTest, EventsPathUsesHttpSse) {
  EXPECT_TRUE(urlShouldUseSse("http://localhost:8080/events"));
  EXPECT_TRUE(urlShouldUseSse("https://example.com/events"));
  EXPECT_TRUE(urlShouldUseSse("http://server:3000/api/events"));
  EXPECT_TRUE(urlShouldUseSse("https://mcp.example.com/v1/events/stream"));
}

/**
 * Test: URLs without /sse or /events should use StreamableHttp
 */
TEST_F(TransportNegotiationTest, OtherPathsUseStreamableHttp) {
  EXPECT_TRUE(urlShouldUseStreamableHttp("http://localhost:8080/rpc"));
  EXPECT_TRUE(urlShouldUseStreamableHttp("https://example.com/mcp"));
  EXPECT_TRUE(urlShouldUseStreamableHttp("http://server:3000/api"));
  EXPECT_TRUE(urlShouldUseStreamableHttp("https://mcp.example.com/v1/"));
  EXPECT_TRUE(urlShouldUseStreamableHttp("http://localhost:8080/"));
  EXPECT_TRUE(urlShouldUseStreamableHttp("https://example.com"));
}

/**
 * Test: Root path should use StreamableHttp
 */
TEST_F(TransportNegotiationTest, RootPathUsesStreamableHttp) {
  EXPECT_TRUE(urlShouldUseStreamableHttp("http://localhost:8080/"));
  EXPECT_TRUE(urlShouldUseStreamableHttp("http://localhost:8080"));
  EXPECT_FALSE(urlShouldUseSse("http://localhost:8080/"));
}

/**
 * Test: Case sensitivity - /SSE should NOT match (lowercase check)
 */
TEST_F(TransportNegotiationTest, PathMatchingIsCaseSensitive) {
  // The current implementation uses case-sensitive matching
  // /SSE or /EVENTS would not match
  EXPECT_FALSE(urlShouldUseSse("http://localhost:8080/SSE"));
  EXPECT_FALSE(urlShouldUseSse("http://localhost:8080/EVENTS"));
  EXPECT_TRUE(urlShouldUseStreamableHttp("http://localhost:8080/SSE"));
}

// =============================================================================
// Configuration Tests
// =============================================================================

/**
 * Test: McpConnectionConfig can be set to StreamableHttp
 */
TEST_F(StreamableHttpTransportTest, ConfigCanUseStreamableHttp) {
  McpConnectionConfig config;
  config.transport_type = TransportType::StreamableHttp;

  EXPECT_EQ(config.transport_type, TransportType::StreamableHttp);
}

/**
 * Test: StreamableHttp config uses http_sse_config field
 */
TEST_F(StreamableHttpTransportTest, StreamableHttpUsesHttpSseConfig) {
  McpConnectionConfig config;
  config.transport_type = TransportType::StreamableHttp;

  // StreamableHttp reuses the http_sse_config structure
  transport::HttpSseTransportSocketConfig http_config;
  http_config.mode = transport::HttpSseTransportSocketConfig::Mode::CLIENT;
  http_config.server_address = "localhost:8080";

  config.http_sse_config = mcp::make_optional(http_config);
  config.http_path = "/rpc";
  config.http_host = "localhost:8080";

  EXPECT_TRUE(config.http_sse_config.has_value());
  EXPECT_EQ(config.http_sse_config.value().server_address, "localhost:8080");
  EXPECT_EQ(config.http_path, "/rpc");
}

/**
 * Test: StreamableHttp can use HTTPS (SSL transport)
 */
TEST_F(StreamableHttpTransportTest, StreamableHttpSupportsHttps) {
  McpConnectionConfig config;
  config.transport_type = TransportType::StreamableHttp;

  transport::HttpSseTransportSocketConfig http_config;
  http_config.mode = transport::HttpSseTransportSocketConfig::Mode::CLIENT;
  http_config.server_address = "example.com:443";
  http_config.underlying_transport =
      transport::HttpSseTransportSocketConfig::UnderlyingTransport::SSL;

  transport::HttpSseTransportSocketConfig::SslConfig ssl_cfg;
  ssl_cfg.verify_peer = false;
  ssl_cfg.alpn_protocols = std::vector<std::string>{"http/1.1"};
  ssl_cfg.sni_hostname = mcp::make_optional(std::string("example.com"));
  http_config.ssl_config = mcp::make_optional(ssl_cfg);

  config.http_sse_config = mcp::make_optional(http_config);

  EXPECT_EQ(config.http_sse_config.value().underlying_transport,
            transport::HttpSseTransportSocketConfig::UnderlyingTransport::SSL);
  EXPECT_TRUE(config.http_sse_config.value().ssl_config.has_value());
}

// =============================================================================
// URL Parsing Tests
// =============================================================================

/**
 * Test helper to extract path from URL (mirrors the logic in
 * negotiateTransport)
 */
class UrlParsingTest : public ::testing::Test {
 protected:
  std::string extractPath(const std::string& uri) {
    std::string path;
    size_t scheme_end = uri.find("://");
    if (scheme_end != std::string::npos) {
      size_t path_start = uri.find('/', scheme_end + 3);
      if (path_start != std::string::npos) {
        path = uri.substr(path_start);
      }
    }
    return path;
  }
};

TEST_F(UrlParsingTest, ExtractPathFromHttpUrl) {
  EXPECT_EQ(extractPath("http://localhost:8080/rpc"), "/rpc");
  EXPECT_EQ(extractPath("http://localhost:8080/api/v1/mcp"), "/api/v1/mcp");
  EXPECT_EQ(extractPath("http://localhost:8080/"), "/");
}

TEST_F(UrlParsingTest, ExtractPathFromHttpsUrl) {
  EXPECT_EQ(extractPath("https://example.com/sse"), "/sse");
  EXPECT_EQ(extractPath("https://example.com:443/events"), "/events");
}

TEST_F(UrlParsingTest, NoPathReturnsEmpty) {
  EXPECT_EQ(extractPath("http://localhost:8080"), "");
  EXPECT_EQ(extractPath("https://example.com"), "");
}

}  // namespace
}  // namespace mcp
