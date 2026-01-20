/**
 * @file test_mcp_initialize_request.cc
 * @brief Unit tests for MCP Initialize Request structure
 *
 * Tests for the MCP-compliant initialize request format:
 * - protocolVersion as string
 * - clientInfo as nested object with name and version
 * - capabilities as object (can be empty)
 *
 * Commit: 19f359f19cf37184636ec745f19fe4087b47052a
 * Feature: MCP Initialize Request Fix (Section 3) and JSON Serialization (Section 4)
 */

#include <gtest/gtest.h>

#include "mcp/json/json_serialization.h"
#include "mcp/types.h"

namespace mcp {
namespace {

using json::JsonSerializeTraits;
using json::JsonValue;

/**
 * Test fixture for MCP Initialize Request tests
 */
class McpInitializeRequestTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

// =============================================================================
// JSON String Detection Tests (Section 4 - Required for Section 3)
// =============================================================================

/**
 * Test: JSON object strings are parsed and serialized as nested objects
 */
TEST_F(McpInitializeRequestTest, JsonObjectStringBecomesNestedObject) {
  Metadata metadata;
  metadata["clientInfo"] = "{\"name\":\"test-client\",\"version\":\"1.0.0\"}";

  JsonValue json = JsonSerializeTraits<Metadata>::serialize(metadata);

  // clientInfo should be an object, not a string
  ASSERT_TRUE(json.isObject());
  ASSERT_TRUE(json.contains("clientInfo"));
  EXPECT_TRUE(json["clientInfo"].isObject());

  // Verify nested properties
  EXPECT_TRUE(json["clientInfo"].contains("name"));
  EXPECT_TRUE(json["clientInfo"].contains("version"));
  EXPECT_EQ(json["clientInfo"]["name"].getString(), "test-client");
  EXPECT_EQ(json["clientInfo"]["version"].getString(), "1.0.0");
}

/**
 * Test: Empty JSON object string becomes empty nested object
 */
TEST_F(McpInitializeRequestTest, EmptyJsonObjectStringBecomesEmptyObject) {
  Metadata metadata;
  metadata["capabilities"] = "{}";

  JsonValue json = JsonSerializeTraits<Metadata>::serialize(metadata);

  ASSERT_TRUE(json.isObject());
  ASSERT_TRUE(json.contains("capabilities"));
  EXPECT_TRUE(json["capabilities"].isObject());
}

/**
 * Test: JSON array strings are parsed and serialized as arrays
 */
TEST_F(McpInitializeRequestTest, JsonArrayStringBecomesArray) {
  Metadata metadata;
  metadata["items"] = "[1, 2, 3]";

  JsonValue json = JsonSerializeTraits<Metadata>::serialize(metadata);

  ASSERT_TRUE(json.isObject());
  ASSERT_TRUE(json.contains("items"));
  EXPECT_TRUE(json["items"].isArray());
  EXPECT_EQ(json["items"].size(), 3u);
}

/**
 * Test: Regular strings are not parsed as JSON
 */
TEST_F(McpInitializeRequestTest, RegularStringsRemainStrings) {
  Metadata metadata;
  metadata["name"] = "hello world";
  metadata["path"] = "/api/v1/endpoint";
  metadata["version"] = "1.0.0";

  JsonValue json = JsonSerializeTraits<Metadata>::serialize(metadata);

  EXPECT_TRUE(json["name"].isString());
  EXPECT_TRUE(json["path"].isString());
  EXPECT_TRUE(json["version"].isString());
  EXPECT_EQ(json["name"].getString(), "hello world");
}

/**
 * Test: Strings that look like JSON but are invalid remain as strings
 */
TEST_F(McpInitializeRequestTest, InvalidJsonStringsRemainStrings) {
  Metadata metadata;
  metadata["bad1"] = "{not valid json}";
  metadata["bad2"] = "{missing: quotes}";

  JsonValue json = JsonSerializeTraits<Metadata>::serialize(metadata);

  // These should remain as strings because they're not valid JSON
  EXPECT_TRUE(json["bad1"].isString());
  EXPECT_TRUE(json["bad2"].isString());
}

/**
 * Test: Strings starting with { but not ending with } remain strings
 */
TEST_F(McpInitializeRequestTest, PartialBracesRemainStrings) {
  Metadata metadata;
  metadata["text1"] = "{hello";
  metadata["text2"] = "hello}";

  JsonValue json = JsonSerializeTraits<Metadata>::serialize(metadata);

  EXPECT_TRUE(json["text1"].isString());
  EXPECT_TRUE(json["text2"].isString());
}

// =============================================================================
// Initialize Request Structure Tests (Section 3)
// =============================================================================

/**
 * Test: Initialize params can be constructed with MCP-compliant structure
 */
TEST_F(McpInitializeRequestTest, InitializeParamsStructure) {
  // Simulate what initializeProtocol() does
  std::string client_name = "gopher-mcp";
  std::string client_version = "1.0.0";
  std::string protocol_version = "2024-11-05";

  Metadata init_params;
  init_params["protocolVersion"] = protocol_version;

  // clientInfo as JSON string (will be serialized as nested object)
  std::string client_info_json = "{\"name\":\"" + client_name +
                                 "\",\"version\":\"" + client_version + "\"}";
  init_params["clientInfo"] = client_info_json;

  // capabilities as empty object
  init_params["capabilities"] = "{}";

  // Serialize to JSON
  JsonValue json = JsonSerializeTraits<Metadata>::serialize(init_params);

  // Verify structure matches MCP spec
  ASSERT_TRUE(json.isObject());

  // protocolVersion should be a string
  ASSERT_TRUE(json.contains("protocolVersion"));
  EXPECT_TRUE(json["protocolVersion"].isString());
  EXPECT_EQ(json["protocolVersion"].getString(), "2024-11-05");

  // clientInfo should be a nested object
  ASSERT_TRUE(json.contains("clientInfo"));
  EXPECT_TRUE(json["clientInfo"].isObject());
  EXPECT_EQ(json["clientInfo"]["name"].getString(), "gopher-mcp");
  EXPECT_EQ(json["clientInfo"]["version"].getString(), "1.0.0");

  // capabilities should be an object
  ASSERT_TRUE(json.contains("capabilities"));
  EXPECT_TRUE(json["capabilities"].isObject());
}

/**
 * Test: Serialized initialize params produce valid JSON string
 */
TEST_F(McpInitializeRequestTest, InitializeParamsSerializesToValidJson) {
  Metadata init_params;
  init_params["protocolVersion"] = "2024-11-05";
  init_params["clientInfo"] = "{\"name\":\"test\",\"version\":\"1.0\"}";
  init_params["capabilities"] = "{}";

  JsonValue json = JsonSerializeTraits<Metadata>::serialize(init_params);
  std::string json_str = json.toString();

  // Should be valid JSON that can be parsed back
  JsonValue parsed = JsonValue::parse(json_str);
  EXPECT_TRUE(parsed.isObject());
  EXPECT_TRUE(parsed["clientInfo"].isObject());
}

/**
 * Test: Non-JSON metadata values serialize correctly alongside JSON strings
 */
TEST_F(McpInitializeRequestTest, MixedMetadataTypes) {
  Metadata metadata;
  metadata["string_val"] = "hello";
  metadata["int_val"] = int64_t(42);
  metadata["double_val"] = 3.14;
  metadata["bool_val"] = true;
  metadata["json_obj"] = "{\"nested\":true}";

  JsonValue json = JsonSerializeTraits<Metadata>::serialize(metadata);

  EXPECT_TRUE(json["string_val"].isString());
  EXPECT_TRUE(json["int_val"].isNumber());
  EXPECT_TRUE(json["double_val"].isNumber());
  EXPECT_TRUE(json["bool_val"].isBoolean());
  EXPECT_TRUE(json["json_obj"].isObject());
  EXPECT_TRUE(json["json_obj"]["nested"].isBoolean());
}

// =============================================================================
// Edge Cases
// =============================================================================

/**
 * Test: Empty string is not treated as JSON
 */
TEST_F(McpInitializeRequestTest, EmptyStringNotJson) {
  Metadata metadata;
  metadata["empty"] = "";

  JsonValue json = JsonSerializeTraits<Metadata>::serialize(metadata);

  EXPECT_TRUE(json["empty"].isString());
  EXPECT_EQ(json["empty"].getString(), "");
}

/**
 * Test: Whitespace-only strings are not treated as JSON
 */
TEST_F(McpInitializeRequestTest, WhitespaceStringNotJson) {
  Metadata metadata;
  metadata["spaces"] = "   ";

  JsonValue json = JsonSerializeTraits<Metadata>::serialize(metadata);

  EXPECT_TRUE(json["spaces"].isString());
}

/**
 * Test: Deeply nested JSON objects are handled correctly
 */
TEST_F(McpInitializeRequestTest, DeeplyNestedJsonObject) {
  Metadata metadata;
  metadata["deep"] = "{\"level1\":{\"level2\":{\"level3\":\"value\"}}}";

  JsonValue json = JsonSerializeTraits<Metadata>::serialize(metadata);

  ASSERT_TRUE(json["deep"].isObject());
  ASSERT_TRUE(json["deep"]["level1"].isObject());
  ASSERT_TRUE(json["deep"]["level1"]["level2"].isObject());
  EXPECT_EQ(json["deep"]["level1"]["level2"]["level3"].getString(), "value");
}

/**
 * Test: JSON with special characters in strings
 */
TEST_F(McpInitializeRequestTest, JsonWithSpecialCharacters) {
  Metadata metadata;
  // Note: The JSON string itself needs proper escaping
  metadata["special"] = "{\"path\":\"/api/v1\",\"query\":\"a=1&b=2\"}";

  JsonValue json = JsonSerializeTraits<Metadata>::serialize(metadata);

  ASSERT_TRUE(json["special"].isObject());
  EXPECT_EQ(json["special"]["path"].getString(), "/api/v1");
  EXPECT_EQ(json["special"]["query"].getString(), "a=1&b=2");
}

}  // namespace
}  // namespace mcp
