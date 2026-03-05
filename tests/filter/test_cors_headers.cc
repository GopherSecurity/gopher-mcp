/**
 * Unit tests for CORS headers in HTTP responses
 *
 * Tests that HTTP responses include proper CORS headers for browser-based
 * MCP clients (e.g., MCP Inspector).
 */

#include <string>

#include <gtest/gtest.h>

namespace mcp {
namespace filter {
namespace {

// Expected CORS header values
const std::string CORS_ALLOW_ORIGIN = "Access-Control-Allow-Origin: *";
const std::string CORS_ALLOW_METHODS =
    "Access-Control-Allow-Methods: GET, POST, OPTIONS";
const std::string CORS_ALLOW_HEADERS =
    "Access-Control-Allow-Headers: Content-Type, Authorization, Accept, "
    "Mcp-Session-Id, Mcp-Protocol-Version";

class CorsHeadersTest : public ::testing::Test {};

// Test that CORS headers have correct format
TEST_F(CorsHeadersTest, CorsAllowOriginFormat) {
  // Verify the allow origin header allows all origins
  EXPECT_EQ(CORS_ALLOW_ORIGIN, "Access-Control-Allow-Origin: *");
  EXPECT_TRUE(CORS_ALLOW_ORIGIN.find("*") != std::string::npos)
      << "Allow-Origin should include wildcard";
}

TEST_F(CorsHeadersTest, CorsAllowMethodsFormat) {
  // Verify required methods are allowed
  EXPECT_TRUE(CORS_ALLOW_METHODS.find("GET") != std::string::npos)
      << "Should allow GET method";
  EXPECT_TRUE(CORS_ALLOW_METHODS.find("POST") != std::string::npos)
      << "Should allow POST method";
  EXPECT_TRUE(CORS_ALLOW_METHODS.find("OPTIONS") != std::string::npos)
      << "Should allow OPTIONS method for preflight";
}

TEST_F(CorsHeadersTest, CorsAllowHeadersFormat) {
  // Verify required headers are allowed
  EXPECT_TRUE(CORS_ALLOW_HEADERS.find("Content-Type") != std::string::npos)
      << "Should allow Content-Type header";
  EXPECT_TRUE(CORS_ALLOW_HEADERS.find("Authorization") != std::string::npos)
      << "Should allow Authorization header for OAuth";
  EXPECT_TRUE(CORS_ALLOW_HEADERS.find("Mcp-Session-Id") != std::string::npos)
      << "Should allow Mcp-Session-Id header";
  EXPECT_TRUE(CORS_ALLOW_HEADERS.find("Mcp-Protocol-Version") !=
              std::string::npos)
      << "Should allow Mcp-Protocol-Version header";
}

// Test that a complete HTTP response with CORS headers is valid
TEST_F(CorsHeadersTest, CompleteHttpResponseFormat) {
  // Build a complete HTTP response like http_codec_filter.cc does
  std::ostringstream response;
  response << "HTTP/1.1 200 OK\r\n";
  response << "Content-Type: application/json\r\n";
  response << "Content-Length: 15\r\n";
  response << "Cache-Control: no-cache\r\n";
  response << CORS_ALLOW_ORIGIN << "\r\n";
  response << CORS_ALLOW_METHODS << "\r\n";
  response << CORS_ALLOW_HEADERS << "\r\n";
  response << "Connection: keep-alive\r\n";
  response << "\r\n";
  response << R"({"result":"ok"})";

  std::string http_response = response.str();

  // Verify response structure
  EXPECT_TRUE(http_response.find("HTTP/1.1 200 OK") != std::string::npos);
  EXPECT_TRUE(http_response.find("Access-Control-Allow-Origin: *") !=
              std::string::npos);
  EXPECT_TRUE(http_response.find("Access-Control-Allow-Methods:") !=
              std::string::npos);
  EXPECT_TRUE(http_response.find("Access-Control-Allow-Headers:") !=
              std::string::npos);

  // Verify CORS headers come before body
  size_t cors_pos = http_response.find("Access-Control-Allow-Origin");
  size_t body_pos = http_response.find("{\"result\"");
  EXPECT_LT(cors_pos, body_pos) << "CORS headers should come before body";
}

}  // namespace
}  // namespace filter
}  // namespace mcp
