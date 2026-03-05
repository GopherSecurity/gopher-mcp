/**
 * Unit tests for MCP server response formats
 *
 * Tests that server responses conform to MCP protocol specification:
 * - initialize: nested JSON with serverInfo, capabilities objects
 * - tools/list: {"tools": [...]} wrapper
 * - prompts/list: {"prompts": [...]} wrapper
 */

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/types.h"

using namespace mcp;
using namespace mcp::json;

class McpServerResponseTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

// Test that initialize response has proper nested structure
TEST_F(McpServerResponseTest, InitializeResponseFormat) {
  // Build initialize response the same way mcp_server.cc does
  JsonValue result_json;
  result_json["protocolVersion"] = "2024-11-05";

  JsonValue server_info;
  server_info["name"] = "test-server";
  server_info["version"] = "1.0.0";
  result_json["serverInfo"] = std::move(server_info);

  JsonValue capabilities = JsonValue::object();
  capabilities["tools"] = JsonValue::object();
  capabilities["prompts"] = JsonValue::object();
  result_json["capabilities"] = std::move(capabilities);

  result_json["instructions"] = "Test instructions";

  // Create response
  auto response = jsonrpc::Response::success(
      make_request_id(1), jsonrpc::ResponseResult(result_json));

  // Serialize and verify
  JsonValue serialized = to_json(response);
  std::string json_str = serialized.toString();

  // Verify nested structure (not flattened dot notation)
  EXPECT_TRUE(json_str.find("\"serverInfo\":{\"name\"") != std::string::npos)
      << "serverInfo should be nested object, got: " << json_str;
  EXPECT_TRUE(json_str.find("\"capabilities\":{") != std::string::npos)
      << "capabilities should be nested object";
  EXPECT_TRUE(json_str.find("\"tools\":{}") != std::string::npos)
      << "tools should be empty object";
  EXPECT_TRUE(json_str.find("\"prompts\":{}") != std::string::npos)
      << "prompts should be empty object";

  // Verify NOT flattened
  EXPECT_TRUE(json_str.find("serverInfo.name") == std::string::npos)
      << "Should not use dot notation";
  EXPECT_TRUE(json_str.find("capabilities.tools") == std::string::npos)
      << "Should not use dot notation";
}

// Test that tools/list response wraps array in object
TEST_F(McpServerResponseTest, ListToolsResponseFormat) {
  // Build tools/list response the same way mcp_server.cc does
  JsonValue tools_array = JsonValue::array();

  JsonValue tool1 = JsonValue::object();
  tool1["name"] = "get-weather";
  tool1["description"] = "Get weather for a city";

  JsonValue input_schema = JsonValue::object();
  input_schema["type"] = "object";
  JsonValue properties = JsonValue::object();
  JsonValue city_prop = JsonValue::object();
  city_prop["type"] = "string";
  city_prop["description"] = "City name";
  properties["city"] = std::move(city_prop);
  input_schema["properties"] = std::move(properties);

  JsonValue required = JsonValue::array();
  required.push_back("city");
  input_schema["required"] = std::move(required);

  tool1["inputSchema"] = std::move(input_schema);
  tools_array.push_back(std::move(tool1));

  JsonValue response_obj = JsonValue::object();
  response_obj["tools"] = std::move(tools_array);

  auto response = jsonrpc::Response::success(
      make_request_id(1), jsonrpc::ResponseResult(response_obj));

  // Serialize and verify
  JsonValue serialized = to_json(response);
  std::string json_str = serialized.toString();

  // Verify wrapped in {"tools": [...]}
  EXPECT_TRUE(json_str.find("\"result\":{\"tools\":[") != std::string::npos)
      << "tools should be wrapped in object, got: " << json_str;

  // Verify NOT bare array
  EXPECT_TRUE(json_str.find("\"result\":[{\"name\"") == std::string::npos)
      << "result should not be bare array";
}

// Test that prompts/list response wraps array in object
TEST_F(McpServerResponseTest, ListPromptsResponseFormat) {
  // Build prompts/list response the same way mcp_server.cc does
  JsonValue prompts_array = JsonValue::array();

  JsonValue prompt1 = JsonValue::object();
  prompt1["name"] = "greeting";
  prompt1["description"] = "A greeting prompt";
  prompts_array.push_back(std::move(prompt1));

  JsonValue response_obj = JsonValue::object();
  response_obj["prompts"] = std::move(prompts_array);

  auto response = jsonrpc::Response::success(
      make_request_id(1), jsonrpc::ResponseResult(response_obj));

  // Serialize and verify
  JsonValue serialized = to_json(response);
  std::string json_str = serialized.toString();

  // Verify wrapped in {"prompts": [...]}
  EXPECT_TRUE(json_str.find("\"result\":{\"prompts\":[") != std::string::npos)
      << "prompts should be wrapped in object, got: " << json_str;
}

// Test empty lists are properly formatted
TEST_F(McpServerResponseTest, EmptyListsFormat) {
  // Empty tools list
  JsonValue tools_obj = JsonValue::object();
  tools_obj["tools"] = JsonValue::array();

  auto tools_response = jsonrpc::Response::success(
      make_request_id(1), jsonrpc::ResponseResult(tools_obj));

  JsonValue tools_serialized = to_json(tools_response);
  std::string tools_json = tools_serialized.toString();

  EXPECT_TRUE(tools_json.find("\"result\":{\"tools\":[]}") != std::string::npos)
      << "Empty tools should be {\"tools\":[]}, got: " << tools_json;

  // Empty prompts list
  JsonValue prompts_obj = JsonValue::object();
  prompts_obj["prompts"] = JsonValue::array();

  auto prompts_response = jsonrpc::Response::success(
      make_request_id(2), jsonrpc::ResponseResult(prompts_obj));

  JsonValue prompts_serialized = to_json(prompts_response);
  std::string prompts_json = prompts_serialized.toString();

  EXPECT_TRUE(prompts_json.find("\"result\":{\"prompts\":[]}") !=
              std::string::npos)
      << "Empty prompts should be {\"prompts\":[]}, got: " << prompts_json;
}
