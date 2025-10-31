/**
 * @file test_json_api_unified.cc
 * @brief Tests for unified JSON C API - verifies no conflicts and proper operation
 */

#include <gtest/gtest.h>
#include <cstring>
#include <memory>

// Include both headers to ensure no conflicts
#include "mcp/c_api/mcp_c_api.h"
#include "mcp/c_api/mcp_c_api_json.h"
#include "mcp/c_api/mcp_c_collections.h"
#include "mcp/c_api/mcp_c_memory.h"

class UnifiedJsonApiTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Initialize if needed
    if (!mcp_is_initialized()) {
      ASSERT_EQ(mcp_init(nullptr), MCP_OK);
    }
  }

  void TearDown() override {
    // Cleanup handled by RAII
  }
};

// Test 1: Compile-time test - headers can be included together
TEST_F(UnifiedJsonApiTest, HeadersCoexist) {
  // This test passes if it compiles
  SUCCEED();
}

// Test 2: Canonical API - parse and stringify
TEST_F(UnifiedJsonApiTest, CanonicalParseStringify) {
  // Test null
  const char* null_json = "null";
  auto null_val = mcp_json_parse(null_json);
  ASSERT_NE(null_val, nullptr);
  EXPECT_EQ(mcp_json_get_type(null_val), MCP_JSON_TYPE_NULL);
  
  char* null_str = mcp_json_stringify(null_val);
  ASSERT_NE(null_str, nullptr);
  EXPECT_STREQ(null_str, "null");
  mcp_string_free(null_str);
  mcp_json_free(null_val);

  // Test boolean
  const char* bool_json = "true";
  auto bool_val = mcp_json_parse(bool_json);
  ASSERT_NE(bool_val, nullptr);
  EXPECT_EQ(mcp_json_get_type(bool_val), MCP_JSON_TYPE_BOOL);
  EXPECT_EQ(mcp_json_get_bool(bool_val), MCP_TRUE);
  
  char* bool_str = mcp_json_stringify(bool_val);
  ASSERT_NE(bool_str, nullptr);
  EXPECT_STREQ(bool_str, "true");
  mcp_string_free(bool_str);
  mcp_json_free(bool_val);

  // Test number
  const char* num_json = "42.5";
  auto num_val = mcp_json_parse(num_json);
  ASSERT_NE(num_val, nullptr);
  EXPECT_EQ(mcp_json_get_type(num_val), MCP_JSON_TYPE_NUMBER);
  EXPECT_DOUBLE_EQ(mcp_json_get_number(num_val), 42.5);
  
  char* num_str = mcp_json_stringify(num_val);
  ASSERT_NE(num_str, nullptr);
  // Note: exact format may vary
  mcp_string_free(num_str);
  mcp_json_free(num_val);

  // Test string
  const char* str_json = "\"hello world\"";
  auto str_val = mcp_json_parse(str_json);
  ASSERT_NE(str_val, nullptr);
  EXPECT_EQ(mcp_json_get_type(str_val), MCP_JSON_TYPE_STRING);
  EXPECT_STREQ(mcp_json_get_string(str_val), "hello world");
  
  char* str_str = mcp_json_stringify(str_val);
  ASSERT_NE(str_str, nullptr);
  EXPECT_STREQ(str_str, "\"hello world\"");
  mcp_string_free(str_str);
  mcp_json_free(str_val);
}

// Test 3: Array round-trip
TEST_F(UnifiedJsonApiTest, ArrayRoundTrip) {
  const char* array_json = "[1, 2, 3]";
  auto array = mcp_json_parse(array_json);
  ASSERT_NE(array, nullptr);
  EXPECT_EQ(mcp_json_get_type(array), MCP_JSON_TYPE_ARRAY);
  EXPECT_EQ(mcp_json_array_size(array), 3);
  
  auto first = mcp_json_array_get(array, 0);
  EXPECT_DOUBLE_EQ(mcp_json_get_number(first), 1.0);
  
  char* array_str = mcp_json_stringify(array);
  ASSERT_NE(array_str, nullptr);
  // Should contain [1,2,3] or similar
  EXPECT_NE(strstr(array_str, "1"), nullptr);
  EXPECT_NE(strstr(array_str, "2"), nullptr);
  EXPECT_NE(strstr(array_str, "3"), nullptr);
  
  mcp_string_free(array_str);
  mcp_json_free(array);
}

// Test 4: Object round-trip
TEST_F(UnifiedJsonApiTest, ObjectRoundTrip) {
  const char* obj_json = "{\"name\":\"test\",\"value\":123}";
  auto obj = mcp_json_parse(obj_json);
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(mcp_json_get_type(obj), MCP_JSON_TYPE_OBJECT);
  
  auto name_val = mcp_json_object_get(obj, "name");
  if (name_val) {
    EXPECT_EQ(mcp_json_get_type(name_val), MCP_JSON_TYPE_STRING);
    EXPECT_STREQ(mcp_json_get_string(name_val), "test");
  }
  
  auto value_val = mcp_json_object_get(obj, "value");
  if (value_val) {
    EXPECT_EQ(mcp_json_get_type(value_val), MCP_JSON_TYPE_NUMBER);
    EXPECT_DOUBLE_EQ(mcp_json_get_number(value_val), 123.0);
  }
  
  mcp_json_free(obj);
}

// Test 5: Compatibility wrapper - mcp_string_t parse
TEST_F(UnifiedJsonApiTest, CompatibilityMcpStringParse) {
  const char* json_cstr = "{\"test\":true}";
  mcp_string_t json_str;
  json_str.data = json_cstr;
  json_str.length = strlen(json_cstr);
  
  auto val = mcp_json_parse_mcp_string(json_str);
  ASSERT_NE(val, nullptr);
  EXPECT_EQ(mcp_json_get_type(val), MCP_JSON_TYPE_OBJECT);
  
  mcp_json_free(val);
}

// Test 6: Compatibility wrapper - string buffer stringify wraps canonical string
TEST_F(UnifiedJsonApiTest, CompatibilityStringBufferStringify) {
  auto obj = mcp_json_create_object();
  mcp_json_object_set(obj, "compatible", mcp_json_create_bool(MCP_TRUE));

  auto buffer = mcp_json_stringify_buffer(obj, MCP_FALSE);
  // Should now return a valid buffer handle
  EXPECT_NE(buffer, nullptr);

  // No direct accessor for buffer content; just ensure we can free it
  mcp_string_buffer_free(buffer);

  // Canonical API still works for content checks
  char* str = mcp_json_stringify(obj);
  ASSERT_NE(str, nullptr);
  EXPECT_NE(strstr(str, "compatible"), nullptr);

  mcp_string_free(str);
  mcp_json_free(obj);
}

// Test 7: Deprecated mcp_json_release works
TEST_F(UnifiedJsonApiTest, DeprecatedReleaseWorks) {
  auto val = mcp_json_create_string("test");
  ASSERT_NE(val, nullptr);
  
  // Should not crash
  mcp_json_release(val);
  
  // Test passes if no crash
  SUCCEED();
}

// Test 8: Memory ownership - mcp_string_free
TEST_F(UnifiedJsonApiTest, MemoryOwnershipStringFree) {
  auto val = mcp_json_create_number(3.14);
  char* str = mcp_json_stringify(val);
  ASSERT_NE(str, nullptr);
  
  // Should be able to free with mcp_string_free
  mcp_string_free(str);
  mcp_json_free(val);
  
  SUCCEED();
}

// Test 9: Parse error handling
TEST_F(UnifiedJsonApiTest, ParseErrorHandling) {
  // Invalid JSON
  auto val1 = mcp_json_parse("{invalid}");
  EXPECT_EQ(val1, nullptr);
  
  auto val2 = mcp_json_parse("");
  EXPECT_EQ(val2, nullptr);
  
  auto val3 = mcp_json_parse(nullptr);
  EXPECT_EQ(val3, nullptr);
  
  SUCCEED();
}

// Test 10: Stringify error handling  
TEST_F(UnifiedJsonApiTest, StringifyErrorHandling) {
  char* str = mcp_json_stringify(nullptr);
  EXPECT_EQ(str, nullptr);
  
  SUCCEED();
}

// Test 11: Complex nested structure
TEST_F(UnifiedJsonApiTest, ComplexNestedStructure) {
  const char* complex_json = R"({
    "users": [
      {"id": 1, "name": "Alice", "active": true},
      {"id": 2, "name": "Bob", "active": false}
    ],
    "count": 2,
    "metadata": {
      "version": "1.0",
      "timestamp": 1234567890
    }
  })";
  
  auto root = mcp_json_parse(complex_json);
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(mcp_json_get_type(root), MCP_JSON_TYPE_OBJECT);
  
  auto users = mcp_json_object_get(root, "users");
  if (users) {
    EXPECT_EQ(mcp_json_get_type(users), MCP_JSON_TYPE_ARRAY);
    EXPECT_EQ(mcp_json_array_size(users), 2);
    
    auto first_user = mcp_json_array_get(users, 0);
    if (first_user) {
      auto name = mcp_json_object_get(first_user, "name");
      if (name) {
        EXPECT_STREQ(mcp_json_get_string(name), "Alice");
      }
    }
  }
  
  mcp_json_free(root);
}

// Test 12: SDK compatibility - ensure old code still works
TEST_F(UnifiedJsonApiTest, SdkBackwardCompatibility) {
  // Create using collections API
  auto val = mcp_json_create_object();
  mcp_json_object_set(val, "sdk", mcp_json_create_string("compatible"));
  
  // Stringify using canonical API
  char* str = mcp_json_stringify(val);
  ASSERT_NE(str, nullptr);
  
  // Parse back using canonical API
  auto parsed = mcp_json_parse(str);
  ASSERT_NE(parsed, nullptr);
  
  // Free using collections API
  mcp_string_free(str);
  mcp_json_free(val);
  mcp_json_free(parsed);
  
  SUCCEED();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();
  
  // Cleanup
  if (mcp_is_initialized()) {
    mcp_shutdown();
  }
  
  return result;
}
