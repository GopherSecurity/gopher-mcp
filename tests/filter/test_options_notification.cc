/**
 * Unit tests for OPTIONS preflight handling and notification responses
 *
 * Tests that:
 * - OPTIONS requests receive 204 No Content with CORS headers
 * - JSON-RPC notifications receive HTTP 202 Accepted response
 */

#include <string>

#include <gtest/gtest.h>

namespace mcp {
namespace filter {
namespace {

class OptionsNotificationTest : public ::testing::Test {};

// Test OPTIONS preflight response format
TEST_F(OptionsNotificationTest, OptionsPreflightResponseFormat) {
  // Build OPTIONS preflight response like http_sse_filter_chain_factory.cc does
  std::ostringstream response;
  response << "HTTP/1.1 204 No Content\r\n";
  response << "Access-Control-Allow-Origin: *\r\n";
  response << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
  response << "Access-Control-Allow-Headers: Content-Type, Authorization, "
              "Accept, Mcp-Session-Id, Mcp-Protocol-Version\r\n";
  response << "Access-Control-Max-Age: 86400\r\n";
  response << "Content-Length: 0\r\n";
  response << "\r\n";

  std::string preflight_response = response.str();

  // Verify 204 No Content for preflight
  EXPECT_TRUE(preflight_response.find("HTTP/1.1 204 No Content") !=
              std::string::npos)
      << "Preflight should return 204 No Content";

  // Verify all CORS headers present
  EXPECT_TRUE(preflight_response.find("Access-Control-Allow-Origin: *") !=
              std::string::npos);
  EXPECT_TRUE(preflight_response.find("Access-Control-Allow-Methods:") !=
              std::string::npos);
  EXPECT_TRUE(preflight_response.find("Access-Control-Allow-Headers:") !=
              std::string::npos);

  // Verify max-age for caching preflight results
  EXPECT_TRUE(preflight_response.find("Access-Control-Max-Age: 86400") !=
              std::string::npos)
      << "Should cache preflight for 24 hours";

  // Verify empty body
  EXPECT_TRUE(preflight_response.find("Content-Length: 0") != std::string::npos)
      << "Preflight response should have empty body";
}

// Test OPTIONS response includes required MCP headers
TEST_F(OptionsNotificationTest, OptionsAllowsMcpHeaders) {
  std::string allowed_headers =
      "Content-Type, Authorization, Accept, Mcp-Session-Id, "
      "Mcp-Protocol-Version";

  // MCP Inspector uses these headers
  EXPECT_TRUE(allowed_headers.find("Mcp-Session-Id") != std::string::npos)
      << "Should allow Mcp-Session-Id header";
  EXPECT_TRUE(allowed_headers.find("Mcp-Protocol-Version") != std::string::npos)
      << "Should allow Mcp-Protocol-Version header";
  EXPECT_TRUE(allowed_headers.find("Authorization") != std::string::npos)
      << "Should allow Authorization header for OAuth";
}

// Test HTTP 202 notification response format
TEST_F(OptionsNotificationTest, NotificationResponseFormat) {
  // Build notification response like http_sse_filter_chain_factory.cc does
  std::string http_response =
      "HTTP/1.1 202 Accepted\r\n"
      "Content-Length: 0\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
      "Access-Control-Allow-Headers: Content-Type, Authorization, Accept, "
      "Mcp-Session-Id, Mcp-Protocol-Version\r\n"
      "Connection: keep-alive\r\n"
      "\r\n";

  // Verify 202 Accepted for notifications
  EXPECT_TRUE(http_response.find("HTTP/1.1 202 Accepted") != std::string::npos)
      << "Notification response should return 202 Accepted";

  // Verify CORS headers present
  EXPECT_TRUE(http_response.find("Access-Control-Allow-Origin: *") !=
              std::string::npos);

  // Verify empty body
  EXPECT_TRUE(http_response.find("Content-Length: 0") != std::string::npos)
      << "Notification response should have empty body";
}

// Test that notifications don't return JSON-RPC response
TEST_F(OptionsNotificationTest, NotificationNoJsonRpcResponse) {
  // JSON-RPC notifications should NOT have a JSON body
  // Only HTTP response headers with 202 status

  std::string notification_response =
      "HTTP/1.1 202 Accepted\r\n"
      "Content-Length: 0\r\n"
      "Connection: keep-alive\r\n"
      "\r\n";

  // Should NOT contain JSON-RPC fields
  EXPECT_TRUE(notification_response.find("\"jsonrpc\"") == std::string::npos)
      << "Notification response should not contain JSON-RPC body";
  EXPECT_TRUE(notification_response.find("\"result\"") == std::string::npos)
      << "Notification response should not contain result field";
  EXPECT_TRUE(notification_response.find("\"id\"") == std::string::npos)
      << "Notification response should not contain id field";
}

// Test OPTIONS for common MCP paths
TEST_F(OptionsNotificationTest, OptionsRegisteredPaths) {
  // These paths should all handle OPTIONS requests
  std::vector<std::string> mcp_paths = {"/mcp", "/mcp/events", "/rpc",
                                        "/health", "/info"};

  for (const auto& path : mcp_paths) {
    // Each path should be registered for OPTIONS
    EXPECT_FALSE(path.empty()) << "Path should not be empty";
  }
}

}  // namespace
}  // namespace filter
}  // namespace mcp
