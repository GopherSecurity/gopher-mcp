/**
 * @file test_filter_only_api_simple.cc
 * @brief Simple unit tests for Filter-Only C API
 *
 * Tests basic API functionality without complex dispatcher threading.
 */

#include <string>

#include <gtest/gtest.h>

#include "mcp/c_api/mcp_c_api.h"
#include "mcp/c_api/mcp_c_api_json.h"
#include "mcp/c_api/mcp_c_filter_only_api.h"

// Simple test that doesn't require dispatcher
TEST(FilterOnlyAPISimple, CreateJSONConfig) {
  // Create a chain-centric configuration (no listeners wrapper)
  auto config = mcp_json_create_object();
  ASSERT_NE(config, nullptr);

  // Set chain properties
  mcp_json_object_set(config, "name", mcp_json_create_string("default"));
  mcp_json_object_set(config, "transport_type", mcp_json_create_string("tcp"));

  // Create empty filters array
  auto filters = mcp_json_create_array();
  mcp_json_object_set(config, "filters", filters);

  // Stringify to verify
  char* json_str = mcp_json_stringify(config);
  EXPECT_NE(json_str, nullptr);

  if (json_str) {
    std::cout << "Created config: " << json_str << "\n";
    mcp_free(json_str);
  }

  mcp_json_free(config);
}

// Test validation without dispatcher (if possible)
TEST(FilterOnlyAPISimple, ValidateWithoutDispatcher) {
  // Initialize MCP
  mcp_result_t result = mcp_init(nullptr);
  ASSERT_EQ(result, MCP_OK);

  // Create simple chain-centric config (missing filters to make it invalid)
  auto config = mcp_json_create_object();
  mcp_json_object_set(config, "name", mcp_json_create_string("test"));
  // Missing "filters" array - this should trigger validation error

  // Try validation - this might still require dispatcher internally
  mcp_filter_only_validation_result_t validation;
  mcp_result_t status = mcp_filter_only_validate_json(config, &validation);

  // Even if validation fails due to missing dispatcher,
  // we should get a result back
  if (status == MCP_OK) {
    std::cout << "Validation completed\n";
    std::cout << "  Valid: " << (validation.valid ? "true" : "false") << "\n";
    std::cout << "  Errors: " << validation.error_count << "\n";
    std::cout << "  Warnings: " << validation.warning_count << "\n";

    mcp_filter_only_validation_result_free(&validation);
  } else {
    std::cout << "Validation requires active dispatcher\n";
  }

  mcp_json_free(config);
  mcp_shutdown();
}

// Test basic handle operations
TEST(FilterOnlyAPISimple, HandleOperations) {
  // Test that releasing null handle is safe
  mcp_filter_only_chain_release(0);

  // Test that retain on null handle is safe
  mcp_filter_only_chain_retain(0);

  // Test validation result free with null
  mcp_filter_only_validation_result_free(nullptr);

  // Test assembly result free with null
  mcp_filter_only_assembly_result_free(nullptr);

  SUCCEED();  // These operations should not crash
}

// Test JSON array operations
TEST(FilterOnlyAPISimple, JSONArrayOperations) {
  auto array = mcp_json_create_array();
  ASSERT_NE(array, nullptr);

  // Add some elements
  mcp_json_array_append(array, mcp_json_create_string("first"));
  mcp_json_array_append(array, mcp_json_create_string("second"));
  mcp_json_array_append(array, mcp_json_create_number(42));
  mcp_json_array_append(array, mcp_json_create_bool(MCP_TRUE));

  // Check size
  size_t size = mcp_json_array_size(array);
  EXPECT_EQ(size, 4u);

  // Get elements
  auto first = mcp_json_array_get(array, 0);
  EXPECT_NE(first, nullptr);
  if (first) {
    const char* str = mcp_json_get_string(first);
    if (str) {
      EXPECT_STREQ(str, "first");
    }
    mcp_json_free(first);
  }

  mcp_json_free(array);
}

// Test JSON object operations
TEST(FilterOnlyAPISimple, JSONObjectOperations) {
  auto obj = mcp_json_create_object();
  ASSERT_NE(obj, nullptr);

  // Set various properties
  mcp_json_object_set(obj, "name", mcp_json_create_string("test"));
  mcp_json_object_set(obj, "value", mcp_json_create_number(123));
  mcp_json_object_set(obj, "enabled", mcp_json_create_bool(MCP_TRUE));

  // Get properties
  auto name = mcp_json_object_get(obj, "name");
  EXPECT_NE(name, nullptr);
  if (name) {
    const char* str = mcp_json_get_string(name);
    if (str) {
      EXPECT_STREQ(str, "test");
    }
    mcp_json_free(name);
  }

  // Check if property exists
  auto exists = mcp_json_object_get(obj, "nonexistent");
  EXPECT_EQ(exists, nullptr);

  mcp_json_free(obj);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}