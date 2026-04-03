/**
 * Unit tests for resources/read response format and ResourceManager read
 * handlers.
 *
 * Covers:
 * - ReadResourceResult serialization matches MCP schema ({contents: [...]})
 * - ResourceManager dispatches to registered read handlers
 * - ResourceManager returns empty result for unknown URIs
 * - ResourceManager throws when a resource has no read handler
 * - TextResourceContents and BlobResourceContents both serialize correctly
 */

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

using namespace mcp;
using namespace mcp::json;
using namespace mcp::server;

// ---------------------------------------------------------------------------
// Serialization: verify the wire format matches the MCP schema
// ---------------------------------------------------------------------------

class ReadResourceResponseTest : public ::testing::Test {};

// resources/read must return {"contents": [{uri, mimeType, text}]}
TEST_F(ReadResourceResponseTest, TextResourceContentsFormat) {
  ReadResourceResult result;
  TextResourceContents content;
  content.uri = mcp::make_optional(std::string("metrics://server/stats"));
  content.mimeType = mcp::make_optional(std::string("application/json"));
  content.text = R"({"uptime":3600})";
  result.contents.push_back(content);

  auto result_json = to_json(result);
  auto response = jsonrpc::Response::success(
      make_request_id(1), jsonrpc::ResponseResult(result_json));
  JsonValue serialized = to_json(response);
  std::string json_str = serialized.toString();

  // Must have "contents" array wrapper
  EXPECT_NE(json_str.find("\"contents\":["), std::string::npos)
      << "result must contain contents array, got: " << json_str;

  // Must contain the text content fields
  EXPECT_NE(json_str.find("\"uri\":\"metrics://server/stats\""),
            std::string::npos)
      << "contents entry must have uri, got: " << json_str;
  EXPECT_NE(json_str.find("\"mimeType\":\"application/json\""),
            std::string::npos)
      << "contents entry must have mimeType, got: " << json_str;
  EXPECT_NE(json_str.find("\"text\":\"{\\\"uptime\\\":3600}\""),
            std::string::npos)
      << "contents entry must have text, got: " << json_str;

  // Must NOT have the old broken fields
  EXPECT_EQ(json_str.find("\"contentCount\""), std::string::npos)
      << "must not contain legacy contentCount field";
}

// Blob resource contents serialize with blob field instead of text
TEST_F(ReadResourceResponseTest, BlobResourceContentsFormat) {
  ReadResourceResult result;
  BlobResourceContents content;
  content.uri = mcp::make_optional(std::string("file:///image.png"));
  content.mimeType = mcp::make_optional(std::string("image/png"));
  content.blob = "iVBORw0KGgo=";  // truncated base64 for test
  result.contents.push_back(content);

  auto result_json = to_json(result);
  std::string json_str = result_json.toString();

  EXPECT_NE(json_str.find("\"contents\":["), std::string::npos);
  EXPECT_NE(json_str.find("\"blob\":\"iVBORw0KGgo=\""), std::string::npos);
  // Must not have a text field
  EXPECT_EQ(json_str.find("\"text\""), std::string::npos);
}

// Multiple contents entries are serialized as an array
TEST_F(ReadResourceResponseTest, MultipleContentsEntries) {
  ReadResourceResult result;

  TextResourceContents text_content;
  text_content.uri = mcp::make_optional(std::string("multi://a"));
  text_content.text = "hello";
  result.contents.push_back(text_content);

  BlobResourceContents blob_content;
  blob_content.uri = mcp::make_optional(std::string("multi://b"));
  blob_content.blob = "AAAA";
  result.contents.push_back(blob_content);

  auto result_json = to_json(result);
  std::string json_str = result_json.toString();

  // Both entries must appear
  EXPECT_NE(json_str.find("\"text\":\"hello\""), std::string::npos);
  EXPECT_NE(json_str.find("\"blob\":\"AAAA\""), std::string::npos);
}

// Empty contents array is valid (e.g. resource exists but has no data yet)
TEST_F(ReadResourceResponseTest, EmptyContentsArray) {
  ReadResourceResult result;

  auto result_json = to_json(result);
  std::string json_str = result_json.toString();

  EXPECT_NE(json_str.find("\"contents\":[]"), std::string::npos)
      << "empty result must serialize to {\"contents\":[]}, got: " << json_str;
}

// ---------------------------------------------------------------------------
// ResourceManager: handler registration and dispatch
// ---------------------------------------------------------------------------

class ResourceManagerTest : public ::testing::Test {
 protected:
  McpServerStats stats_;
  // SessionContext needs a Connection*; nullptr is safe here because our
  // handlers never touch the connection.
  SessionContext session_{"test-session", nullptr};
};

// Handler is invoked and its result returned on readResource
TEST_F(ResourceManagerTest, ReadInvokesRegisteredHandler) {
  ResourceManager mgr(stats_);

  Resource res;
  res.uri = "test://data";
  res.name = "Test Data";

  bool handler_called = false;
  mgr.registerResource(
      res,
      [&handler_called](const std::string& uri,
                        SessionContext& /*session*/) -> ReadResourceResult {
        handler_called = true;
        ReadResourceResult result;
        TextResourceContents c;
        c.uri = mcp::make_optional(uri);
        c.text = "payload";
        result.contents.push_back(c);
        return result;
      });

  auto result = mgr.readResource("test://data", session_);
  EXPECT_TRUE(handler_called);
  ASSERT_EQ(result.contents.size(), 1u);

  // Verify the contents are TextResourceContents with expected text
  const auto* text = mcp::get_if<TextResourceContents>(&result.contents[0]);
  ASSERT_NE(text, nullptr);
  EXPECT_EQ(text->text, "payload");
}

// readResource returns empty result for an unknown URI
TEST_F(ResourceManagerTest, ReadUnknownUriReturnsEmpty) {
  ResourceManager mgr(stats_);

  auto result = mgr.readResource("unknown://nowhere", session_);
  EXPECT_TRUE(result.contents.empty());
}

// readResource throws when resource was registered without a handler
TEST_F(ResourceManagerTest, ReadWithoutHandlerThrows) {
  ResourceManager mgr(stats_);

  Resource res;
  res.uri = "test://no-handler";
  res.name = "No Handler";
  mgr.registerResource(res);  // metadata-only, no handler

  EXPECT_THROW(mgr.readResource("test://no-handler", session_),
               std::runtime_error);
}

// Handler receives the correct URI when multiple resources share a handler
TEST_F(ResourceManagerTest, HandlerReceivesCorrectUri) {
  ResourceManager mgr(stats_);

  std::string received_uri;
  auto shared_handler = [&received_uri](
                            const std::string& uri,
                            SessionContext& /*session*/) -> ReadResourceResult {
    received_uri = uri;
    ReadResourceResult result;
    TextResourceContents c;
    c.uri = mcp::make_optional(uri);
    c.text = "content for " + uri;
    result.contents.push_back(c);
    return result;
  };

  Resource r1;
  r1.uri = "res://alpha";
  r1.name = "Alpha";
  mgr.registerResource(r1, shared_handler);

  Resource r2;
  r2.uri = "res://beta";
  r2.name = "Beta";
  mgr.registerResource(r2, shared_handler);

  mgr.readResource("res://alpha", session_);
  EXPECT_EQ(received_uri, "res://alpha");

  mgr.readResource("res://beta", session_);
  EXPECT_EQ(received_uri, "res://beta");
}

// resources_served stat is incremented on successful read
TEST_F(ResourceManagerTest, ReadIncrementsStats) {
  ResourceManager mgr(stats_);

  Resource res;
  res.uri = "test://stats";
  res.name = "Stats";
  mgr.registerResource(
      res, [](const std::string&, SessionContext&) -> ReadResourceResult {
        return ReadResourceResult{};
      });

  EXPECT_EQ(stats_.resources_served.load(), 0u);
  mgr.readResource("test://stats", session_);
  EXPECT_EQ(stats_.resources_served.load(), 1u);
  mgr.readResource("test://stats", session_);
  EXPECT_EQ(stats_.resources_served.load(), 2u);
}
