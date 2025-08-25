/**
 * @file test_c_collections_impl.cc
 * @brief Comprehensive unit tests for MCP C API collections implementation
 *
 * Tests list, map, iterator, JSON value, and metadata functionality
 * including edge cases, stress tests, and thread safety.
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

extern "C" {
#include "mcp/c_api/mcp_c_collections.h"
#include "mcp/c_api/mcp_c_memory.h"
#include "mcp/c_api/mcp_c_types.h"
}

namespace {

class MCPCollectionsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Initialize FFI
    mcp_ffi_initialize(nullptr);
  }

  void TearDown() override {
    // Clean up FFI
    mcp_ffi_shutdown();
  }
};

/* ============================================================================
 * List Tests
 * ============================================================================
 */

TEST_F(MCPCollectionsTest, ListCreateDestroy) {
  // Test basic creation and destruction
  mcp_list_t list = mcp_list_create(MCP_TYPE_STRING);
  ASSERT_NE(list, nullptr);
  EXPECT_TRUE(mcp_list_is_valid(list));
  EXPECT_EQ(mcp_list_element_type(list), MCP_TYPE_STRING);
  EXPECT_EQ(mcp_list_size(list), 0);
  mcp_list_free(list);

  // Test with capacity
  list = mcp_list_create_with_capacity(MCP_TYPE_JSON, 100);
  ASSERT_NE(list, nullptr);
  EXPECT_GE(mcp_list_capacity(list), 100);
  EXPECT_EQ(mcp_list_size(list), 0);
  mcp_list_free(list);

  // Test null list
  EXPECT_FALSE(mcp_list_is_valid(nullptr));
  EXPECT_EQ(mcp_list_size(nullptr), 0);
  EXPECT_EQ(mcp_list_element_type(nullptr), MCP_TYPE_UNKNOWN);
}

TEST_F(MCPCollectionsTest, ListAppendGet) {
  mcp_list_t list = mcp_list_create(MCP_TYPE_STRING);
  ASSERT_NE(list, nullptr);

  // Test append
  const char* str1 = "first";
  const char* str2 = "second";
  const char* str3 = "third";

  EXPECT_EQ(mcp_list_append(list, (void*)str1), MCP_OK);
  EXPECT_EQ(mcp_list_size(list), 1);

  EXPECT_EQ(mcp_list_append(list, (void*)str2), MCP_OK);
  EXPECT_EQ(mcp_list_size(list), 2);

  EXPECT_EQ(mcp_list_append(list, (void*)str3), MCP_OK);
  EXPECT_EQ(mcp_list_size(list), 3);

  // Test get
  EXPECT_EQ(mcp_list_get(list, 0), str1);
  EXPECT_EQ(mcp_list_get(list, 1), str2);
  EXPECT_EQ(mcp_list_get(list, 2), str3);

  // Test out of bounds
  EXPECT_EQ(mcp_list_get(list, 3), nullptr);
  EXPECT_EQ(mcp_list_get(list, 100), nullptr);

  // Test null list
  EXPECT_EQ(mcp_list_append(nullptr, (void*)str1), MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_list_get(nullptr, 0), nullptr);

  mcp_list_free(list);
}

TEST_F(MCPCollectionsTest, ListInsertSetRemove) {
  mcp_list_t list = mcp_list_create(MCP_TYPE_STRING);
  ASSERT_NE(list, nullptr);

  const char* str1 = "one";
  const char* str2 = "two";
  const char* str3 = "three";
  const char* str4 = "four";

  // Initial append
  mcp_list_append(list, (void*)str1);
  mcp_list_append(list, (void*)str3);

  // Test insert
  EXPECT_EQ(mcp_list_insert(list, 1, (void*)str2), MCP_OK);
  EXPECT_EQ(mcp_list_size(list), 3);
  EXPECT_EQ(mcp_list_get(list, 0), str1);
  EXPECT_EQ(mcp_list_get(list, 1), str2);
  EXPECT_EQ(mcp_list_get(list, 2), str3);

  // Test set
  EXPECT_EQ(mcp_list_set(list, 1, (void*)str4), MCP_OK);
  EXPECT_EQ(mcp_list_get(list, 1), str4);
  EXPECT_EQ(mcp_list_size(list), 3);

  // Test remove
  EXPECT_EQ(mcp_list_remove(list, 1), MCP_OK);
  EXPECT_EQ(mcp_list_size(list), 2);
  EXPECT_EQ(mcp_list_get(list, 0), str1);
  EXPECT_EQ(mcp_list_get(list, 1), str3);

  // Test invalid operations
  EXPECT_EQ(mcp_list_insert(list, 10, (void*)str1), MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_list_set(list, 10, (void*)str1), MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_list_remove(list, 10), MCP_ERROR_INVALID_ARGUMENT);

  mcp_list_free(list);
}

TEST_F(MCPCollectionsTest, ListClear) {
  mcp_list_t list = mcp_list_create(MCP_TYPE_STRING);
  ASSERT_NE(list, nullptr);

  // Add items
  for (int i = 0; i < 10; ++i) {
    mcp_list_append(list, (void*)(intptr_t)i);
  }
  EXPECT_EQ(mcp_list_size(list), 10);

  // Clear
  EXPECT_EQ(mcp_list_clear(list), MCP_OK);
  EXPECT_EQ(mcp_list_size(list), 0);

  // Can still use after clear
  mcp_list_append(list, (void*)(intptr_t)42);
  EXPECT_EQ(mcp_list_size(list), 1);

  // Clear null list
  EXPECT_EQ(mcp_list_clear(nullptr), MCP_ERROR_INVALID_ARGUMENT);

  mcp_list_free(list);
}

/* ============================================================================
 * List Iterator Tests
 * ============================================================================
 */

TEST_F(MCPCollectionsTest, ListIterator) {
  mcp_list_t list = mcp_list_create(MCP_TYPE_STRING);
  ASSERT_NE(list, nullptr);

  // Add items
  const char* items[] = {"a", "b", "c", "d", "e"};
  for (const char* item : items) {
    mcp_list_append(list, (void*)item);
  }

  // Create iterator
  mcp_list_iterator_t iter = mcp_list_iterator_create(list);
  ASSERT_NE(iter, nullptr);

  // Iterate forward
  int count = 0;
  while (mcp_list_iterator_has_next(iter)) {
    void* item = mcp_list_iterator_next(iter);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item, items[count]);
    count++;
  }
  EXPECT_EQ(count, 5);

  // No more items
  EXPECT_FALSE(mcp_list_iterator_has_next(iter));
  EXPECT_EQ(mcp_list_iterator_next(iter), nullptr);

  // Reset and iterate again
  mcp_list_iterator_reset(iter);
  EXPECT_TRUE(mcp_list_iterator_has_next(iter));
  void* first = mcp_list_iterator_next(iter);
  EXPECT_EQ(first, items[0]);

  mcp_list_iterator_free(iter);

  // Null iterator
  EXPECT_EQ(mcp_list_iterator_create(nullptr), nullptr);
  EXPECT_FALSE(mcp_list_iterator_has_next(nullptr));
  EXPECT_EQ(mcp_list_iterator_next(nullptr), nullptr);

  mcp_list_free(list);
}

/* ============================================================================
 * Map Tests
 * ============================================================================
 */

TEST_F(MCPCollectionsTest, MapCreateDestroy) {
  // Test basic creation and destruction
  mcp_map_t map = mcp_map_create(MCP_TYPE_STRING);
  ASSERT_NE(map, nullptr);
  EXPECT_TRUE(mcp_map_is_valid(map));
  EXPECT_EQ(mcp_map_value_type(map), MCP_TYPE_STRING);
  EXPECT_EQ(mcp_map_size(map), 0);
  mcp_map_free(map);

  // Test with capacity
  map = mcp_map_create_with_capacity(MCP_TYPE_JSON, 100);
  ASSERT_NE(map, nullptr);
  EXPECT_EQ(mcp_map_size(map), 0);
  mcp_map_free(map);

  // Test null map
  EXPECT_FALSE(mcp_map_is_valid(nullptr));
  EXPECT_EQ(mcp_map_size(nullptr), 0);
  EXPECT_EQ(mcp_map_value_type(nullptr), MCP_TYPE_UNKNOWN);
}

TEST_F(MCPCollectionsTest, MapSetGetHas) {
  mcp_map_t map = mcp_map_create(MCP_TYPE_STRING);
  ASSERT_NE(map, nullptr);

  // Test set and get
  const char* value1 = "value1";
  const char* value2 = "value2";
  const char* value3 = "value3";

  EXPECT_EQ(mcp_map_set(map, "key1", (void*)value1), MCP_OK);
  EXPECT_EQ(mcp_map_size(map), 1);

  EXPECT_EQ(mcp_map_set(map, "key2", (void*)value2), MCP_OK);
  EXPECT_EQ(mcp_map_size(map), 2);

  EXPECT_EQ(mcp_map_set(map, "key3", (void*)value3), MCP_OK);
  EXPECT_EQ(mcp_map_size(map), 3);

  // Test get
  EXPECT_EQ(mcp_map_get(map, "key1"), value1);
  EXPECT_EQ(mcp_map_get(map, "key2"), value2);
  EXPECT_EQ(mcp_map_get(map, "key3"), value3);
  EXPECT_EQ(mcp_map_get(map, "nonexistent"), nullptr);

  // Test has
  EXPECT_TRUE(mcp_map_has(map, "key1"));
  EXPECT_TRUE(mcp_map_has(map, "key2"));
  EXPECT_TRUE(mcp_map_has(map, "key3"));
  EXPECT_FALSE(mcp_map_has(map, "nonexistent"));

  // Test overwrite
  const char* newValue = "newValue";
  EXPECT_EQ(mcp_map_set(map, "key2", (void*)newValue), MCP_OK);
  EXPECT_EQ(mcp_map_size(map), 3);  // Size unchanged
  EXPECT_EQ(mcp_map_get(map, "key2"), newValue);

  // Test null operations
  EXPECT_EQ(mcp_map_set(nullptr, "key", (void*)value1),
            MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_map_set(map, nullptr, (void*)value1),
            MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_map_get(nullptr, "key"), nullptr);
  EXPECT_EQ(mcp_map_get(map, nullptr), nullptr);
  EXPECT_FALSE(mcp_map_has(nullptr, "key"));
  EXPECT_FALSE(mcp_map_has(map, nullptr));

  mcp_map_free(map);
}

TEST_F(MCPCollectionsTest, MapRemoveClear) {
  mcp_map_t map = mcp_map_create(MCP_TYPE_STRING);
  ASSERT_NE(map, nullptr);

  // Add items
  for (int i = 0; i < 10; ++i) {
    std::string key = "key" + std::to_string(i);
    mcp_map_set(map, key.c_str(), (void*)(intptr_t)i);
  }
  EXPECT_EQ(mcp_map_size(map), 10);

  // Remove existing
  EXPECT_EQ(mcp_map_remove(map, "key5"), MCP_OK);
  EXPECT_EQ(mcp_map_size(map), 9);
  EXPECT_FALSE(mcp_map_has(map, "key5"));

  // Remove non-existent
  EXPECT_EQ(mcp_map_remove(map, "nonexistent"), MCP_ERROR_NOT_FOUND);
  EXPECT_EQ(mcp_map_size(map), 9);

  // Clear
  EXPECT_EQ(mcp_map_clear(map), MCP_OK);
  EXPECT_EQ(mcp_map_size(map), 0);

  // Can still use after clear
  mcp_map_set(map, "new", (void*)(intptr_t)42);
  EXPECT_EQ(mcp_map_size(map), 1);

  // Null operations
  EXPECT_EQ(mcp_map_remove(nullptr, "key"), MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_map_remove(map, nullptr), MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_map_clear(nullptr), MCP_ERROR_INVALID_ARGUMENT);

  mcp_map_free(map);
}

/* ============================================================================
 * Map Iterator Tests
 * ============================================================================
 */

TEST_F(MCPCollectionsTest, MapIterator) {
  mcp_map_t map = mcp_map_create(MCP_TYPE_STRING);
  ASSERT_NE(map, nullptr);

  // Add items
  std::vector<std::string> keys = {"alpha", "beta", "gamma", "delta",
                                   "epsilon"};
  for (size_t i = 0; i < keys.size(); ++i) {
    mcp_map_set(map, keys[i].c_str(), (void*)(intptr_t)i);
  }

  // Create iterator
  mcp_map_iterator_t iter = mcp_map_iterator_create(map);
  ASSERT_NE(iter, nullptr);

  // Iterate through all items
  std::vector<std::string> found_keys;
  while (mcp_map_iterator_has_next(iter)) {
    const char* key = mcp_map_iterator_next_key(iter);
    ASSERT_NE(key, nullptr);
    found_keys.push_back(key);
  }

  // Check we got all keys (order may differ)
  EXPECT_EQ(found_keys.size(), keys.size());
  std::sort(found_keys.begin(), found_keys.end());
  std::sort(keys.begin(), keys.end());
  EXPECT_EQ(found_keys, keys);

  // Create new iterator for values
  mcp_map_iterator_free(iter);
  iter = mcp_map_iterator_create(map);
  ASSERT_NE(iter, nullptr);

  int count = 0;
  while (mcp_map_iterator_has_next(iter)) {
    void* value = mcp_map_iterator_next_value(iter);
    // Value can be nullptr (0) since we store small integers as pointers
    EXPECT_GE((intptr_t)value, 0);
    EXPECT_LT((intptr_t)value, 5);
    count++;
  }
  EXPECT_EQ(count, 5);

  mcp_map_iterator_free(iter);

  // Null iterator
  EXPECT_EQ(mcp_map_iterator_create(nullptr), nullptr);
  EXPECT_FALSE(mcp_map_iterator_has_next(nullptr));
  EXPECT_EQ(mcp_map_iterator_next_key(nullptr), nullptr);
  EXPECT_EQ(mcp_map_iterator_next_value(nullptr), nullptr);

  mcp_map_free(map);
}

/* ============================================================================
 * JSON Value Tests
 * ============================================================================
 */

TEST_F(MCPCollectionsTest, JSONNull) {
  mcp_json_value_t json = mcp_json_create_null();
  ASSERT_NE(json, nullptr);

  EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_NULL);

  // Type mismatches return defaults
  EXPECT_FALSE(mcp_json_get_bool(json));
  EXPECT_EQ(mcp_json_get_number(json), 0.0);
  EXPECT_EQ(mcp_json_get_string(json), nullptr);
  EXPECT_EQ(mcp_json_array_size(json), 0);

  mcp_json_free(json);
}

TEST_F(MCPCollectionsTest, JSONBool) {
  mcp_json_value_t json_true = mcp_json_create_bool(MCP_TRUE);
  ASSERT_NE(json_true, nullptr);
  EXPECT_EQ(mcp_json_get_type(json_true), MCP_JSON_TYPE_BOOL);
  EXPECT_TRUE(mcp_json_get_bool(json_true));

  mcp_json_value_t json_false = mcp_json_create_bool(MCP_FALSE);
  ASSERT_NE(json_false, nullptr);
  EXPECT_EQ(mcp_json_get_type(json_false), MCP_JSON_TYPE_BOOL);
  EXPECT_FALSE(mcp_json_get_bool(json_false));

  mcp_json_free(json_true);
  mcp_json_free(json_false);
}

TEST_F(MCPCollectionsTest, JSONNumber) {
  mcp_json_value_t json = mcp_json_create_number(42.5);
  ASSERT_NE(json, nullptr);

  EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_NUMBER);
  EXPECT_DOUBLE_EQ(mcp_json_get_number(json), 42.5);

  // Type mismatch
  EXPECT_FALSE(mcp_json_get_bool(json));
  EXPECT_EQ(mcp_json_get_string(json), nullptr);

  mcp_json_free(json);

  // Test special values
  json = mcp_json_create_number(0.0);
  ASSERT_NE(json, nullptr);
  EXPECT_DOUBLE_EQ(mcp_json_get_number(json), 0.0);
  mcp_json_free(json);

  json = mcp_json_create_number(-123.456);
  ASSERT_NE(json, nullptr);
  EXPECT_DOUBLE_EQ(mcp_json_get_number(json), -123.456);
  mcp_json_free(json);
}

TEST_F(MCPCollectionsTest, JSONString) {
  const char* test_str = "Hello, World!";
  mcp_json_value_t json = mcp_json_create_string(test_str);
  ASSERT_NE(json, nullptr);

  EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_STRING);
  EXPECT_STREQ(mcp_json_get_string(json), test_str);

  // Type mismatch
  EXPECT_FALSE(mcp_json_get_bool(json));
  EXPECT_EQ(mcp_json_get_number(json), 0.0);

  mcp_json_free(json);

  // Test null string
  EXPECT_EQ(mcp_json_create_string(nullptr), nullptr);

  // Test empty string
  json = mcp_json_create_string("");
  ASSERT_NE(json, nullptr);
  EXPECT_STREQ(mcp_json_get_string(json), "");
  mcp_json_free(json);
}

TEST_F(MCPCollectionsTest, JSONArray) {
  mcp_json_value_t array = mcp_json_create_array();
  ASSERT_NE(array, nullptr);

  EXPECT_EQ(mcp_json_get_type(array), MCP_JSON_TYPE_ARRAY);
  EXPECT_EQ(mcp_json_array_size(array), 0);

  // Add elements
  mcp_json_value_t elem1 = mcp_json_create_number(1.0);
  mcp_json_value_t elem2 = mcp_json_create_string("two");
  mcp_json_value_t elem3 = mcp_json_create_bool(MCP_TRUE);

  EXPECT_EQ(mcp_json_array_append(array, elem1), MCP_OK);
  EXPECT_EQ(mcp_json_array_append(array, elem2), MCP_OK);
  EXPECT_EQ(mcp_json_array_append(array, elem3), MCP_OK);

  EXPECT_EQ(mcp_json_array_size(array), 3);

  // Get elements
  EXPECT_EQ(mcp_json_array_get(array, 0), elem1);
  EXPECT_EQ(mcp_json_array_get(array, 1), elem2);
  EXPECT_EQ(mcp_json_array_get(array, 2), elem3);
  EXPECT_EQ(mcp_json_array_get(array, 3), nullptr);  // Out of bounds

  // Verify element values
  EXPECT_DOUBLE_EQ(mcp_json_get_number(mcp_json_array_get(array, 0)), 1.0);
  EXPECT_STREQ(mcp_json_get_string(mcp_json_array_get(array, 1)), "two");
  EXPECT_TRUE(mcp_json_get_bool(mcp_json_array_get(array, 2)));

  // Invalid operations
  EXPECT_EQ(mcp_json_array_append(nullptr, elem1), MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_json_array_append(array, nullptr), MCP_ERROR_INVALID_ARGUMENT);

  // Cleanup (will recursively free elements)
  mcp_json_free(array);
}

TEST_F(MCPCollectionsTest, JSONObject) {
  mcp_json_value_t object = mcp_json_create_object();
  ASSERT_NE(object, nullptr);

  EXPECT_EQ(mcp_json_get_type(object), MCP_JSON_TYPE_OBJECT);

  // Add properties
  mcp_json_value_t prop1 = mcp_json_create_number(42.0);
  mcp_json_value_t prop2 = mcp_json_create_string("hello");
  mcp_json_value_t prop3 = mcp_json_create_bool(MCP_FALSE);

  EXPECT_EQ(mcp_json_object_set(object, "number", prop1), MCP_OK);
  EXPECT_EQ(mcp_json_object_set(object, "string", prop2), MCP_OK);
  EXPECT_EQ(mcp_json_object_set(object, "boolean", prop3), MCP_OK);

  // Check has
  EXPECT_TRUE(mcp_json_object_has(object, "number"));
  EXPECT_TRUE(mcp_json_object_has(object, "string"));
  EXPECT_TRUE(mcp_json_object_has(object, "boolean"));
  EXPECT_FALSE(mcp_json_object_has(object, "nonexistent"));

  // Get properties
  EXPECT_EQ(mcp_json_object_get(object, "number"), prop1);
  EXPECT_EQ(mcp_json_object_get(object, "string"), prop2);
  EXPECT_EQ(mcp_json_object_get(object, "boolean"), prop3);
  EXPECT_EQ(mcp_json_object_get(object, "nonexistent"), nullptr);

  // Verify values
  EXPECT_DOUBLE_EQ(mcp_json_get_number(mcp_json_object_get(object, "number")),
                   42.0);
  EXPECT_STREQ(mcp_json_get_string(mcp_json_object_get(object, "string")),
               "hello");
  EXPECT_FALSE(mcp_json_get_bool(mcp_json_object_get(object, "boolean")));

  // Replace property
  mcp_json_value_t new_prop = mcp_json_create_number(100.0);
  EXPECT_EQ(mcp_json_object_set(object, "number", new_prop), MCP_OK);
  EXPECT_DOUBLE_EQ(mcp_json_get_number(mcp_json_object_get(object, "number")),
                   100.0);

  // Invalid operations
  EXPECT_EQ(mcp_json_object_set(nullptr, "key", prop1),
            MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_json_object_set(object, nullptr, prop1),
            MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_json_object_set(object, "key", nullptr),
            MCP_ERROR_INVALID_ARGUMENT);

  // Cleanup (will recursively free properties)
  mcp_json_free(object);
}

TEST_F(MCPCollectionsTest, JSONNested) {
  // Create nested structure: { "array": [1, "two", { "nested": true }] }
  mcp_json_value_t root = mcp_json_create_object();
  mcp_json_value_t array = mcp_json_create_array();
  mcp_json_value_t nested = mcp_json_create_object();

  // Build nested object
  mcp_json_object_set(nested, "nested", mcp_json_create_bool(MCP_TRUE));

  // Build array
  mcp_json_array_append(array, mcp_json_create_number(1.0));
  mcp_json_array_append(array, mcp_json_create_string("two"));
  mcp_json_array_append(array, nested);

  // Add array to root
  mcp_json_object_set(root, "array", array);

  // Verify structure
  mcp_json_value_t retrieved_array = mcp_json_object_get(root, "array");
  ASSERT_NE(retrieved_array, nullptr);
  EXPECT_EQ(mcp_json_array_size(retrieved_array), 3);

  mcp_json_value_t retrieved_nested = mcp_json_array_get(retrieved_array, 2);
  ASSERT_NE(retrieved_nested, nullptr);
  EXPECT_EQ(mcp_json_get_type(retrieved_nested), MCP_JSON_TYPE_OBJECT);

  mcp_json_value_t nested_bool =
      mcp_json_object_get(retrieved_nested, "nested");
  ASSERT_NE(nested_bool, nullptr);
  EXPECT_TRUE(mcp_json_get_bool(nested_bool));

  // Cleanup (will recursively free everything)
  mcp_json_free(root);
}

/* ============================================================================
 * Metadata Tests
 * ============================================================================
 */

TEST_F(MCPCollectionsTest, Metadata) {
  mcp_metadata_t metadata = mcp_metadata_create();
  ASSERT_NE(metadata, nullptr);

  // Initially empty
  EXPECT_EQ(mcp_metadata_to_json(metadata), nullptr);

  // Set JSON data
  mcp_json_value_t data = mcp_json_create_object();
  mcp_json_object_set(data, "version", mcp_json_create_string("1.0"));
  mcp_json_object_set(data, "timestamp", mcp_json_create_number(1234567890));

  EXPECT_EQ(mcp_metadata_from_json(metadata, data), MCP_OK);

  // Retrieve JSON data
  mcp_json_value_t retrieved = mcp_metadata_to_json(metadata);
  EXPECT_EQ(retrieved, data);

  // Verify data
  EXPECT_STREQ(mcp_json_get_string(mcp_json_object_get(retrieved, "version")),
               "1.0");
  EXPECT_DOUBLE_EQ(
      mcp_json_get_number(mcp_json_object_get(retrieved, "timestamp")),
      1234567890);

  // Replace data
  mcp_json_value_t new_data = mcp_json_create_string("simple metadata");
  EXPECT_EQ(mcp_metadata_from_json(metadata, new_data), MCP_OK);
  EXPECT_EQ(mcp_metadata_to_json(metadata), new_data);

  // Invalid operations
  EXPECT_EQ(mcp_metadata_from_json(nullptr, data), MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_metadata_from_json(metadata, nullptr),
            MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_metadata_to_json(nullptr), nullptr);

  mcp_metadata_free(metadata);
}

/* ============================================================================
 * Stress Tests
 * ============================================================================
 */

TEST_F(MCPCollectionsTest, ListStressTest) {
  mcp_list_t list = mcp_list_create(MCP_TYPE_STRING);
  ASSERT_NE(list, nullptr);

  // Add many items
  const int count = 10000;
  for (int i = 0; i < count; ++i) {
    mcp_list_append(list, (void*)(intptr_t)i);
  }
  EXPECT_EQ(mcp_list_size(list), count);

  // Random access
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, count - 1);

  for (int i = 0; i < 1000; ++i) {
    int idx = dis(gen);
    EXPECT_EQ(mcp_list_get(list, idx), (void*)(intptr_t)idx);
  }

  // Random inserts and removes
  for (int i = 0; i < 100; ++i) {
    int idx = dis(gen) % mcp_list_size(list);
    if (i % 2 == 0) {
      mcp_list_insert(list, idx, (void*)(intptr_t)(count + i));
    } else {
      mcp_list_remove(list, idx);
    }
  }

  // Clear and refill
  mcp_list_clear(list);
  for (int i = 0; i < 100; ++i) {
    mcp_list_append(list, (void*)(intptr_t)i);
  }
  EXPECT_EQ(mcp_list_size(list), 100);

  mcp_list_free(list);
}

TEST_F(MCPCollectionsTest, MapStressTest) {
  mcp_map_t map = mcp_map_create(MCP_TYPE_STRING);
  ASSERT_NE(map, nullptr);

  // Add many items
  const int count = 10000;
  for (int i = 0; i < count; ++i) {
    std::string key = "key_" + std::to_string(i);
    mcp_map_set(map, key.c_str(), (void*)(intptr_t)i);
  }
  EXPECT_EQ(mcp_map_size(map), count);

  // Random access
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, count - 1);

  for (int i = 0; i < 1000; ++i) {
    int idx = dis(gen);
    std::string key = "key_" + std::to_string(idx);
    EXPECT_TRUE(mcp_map_has(map, key.c_str()));
    EXPECT_EQ(mcp_map_get(map, key.c_str()), (void*)(intptr_t)idx);
  }

  // Random removes
  for (int i = 0; i < 100; ++i) {
    int idx = dis(gen);
    std::string key = "key_" + std::to_string(idx);
    mcp_map_remove(map, key.c_str());
  }

  // Clear and refill
  mcp_map_clear(map);
  for (int i = 0; i < 100; ++i) {
    std::string key = "new_key_" + std::to_string(i);
    mcp_map_set(map, key.c_str(), (void*)(intptr_t)i);
  }
  EXPECT_EQ(mcp_map_size(map), 100);

  mcp_map_free(map);
}

TEST_F(MCPCollectionsTest, JSONStressTest) {
  // Create deeply nested JSON
  mcp_json_value_t root = mcp_json_create_object();
  mcp_json_value_t current = root;

  // Create nested objects
  for (int i = 0; i < 100; ++i) {
    mcp_json_value_t nested = mcp_json_create_object();
    std::string key = "level_" + std::to_string(i);
    mcp_json_object_set(current, key.c_str(), nested);

    // Add some data at each level
    mcp_json_object_set(nested, "index", mcp_json_create_number(i));
    mcp_json_object_set(nested, "name", mcp_json_create_string(key.c_str()));

    // Add array with items
    mcp_json_value_t array = mcp_json_create_array();
    for (int j = 0; j < 10; ++j) {
      mcp_json_array_append(array, mcp_json_create_number(j));
    }
    mcp_json_object_set(nested, "array", array);

    current = nested;
  }

  // Verify structure is intact
  current = root;
  for (int i = 0; i < 100; ++i) {
    std::string key = "level_" + std::to_string(i);
    mcp_json_value_t nested = mcp_json_object_get(current, key.c_str());
    ASSERT_NE(nested, nullptr);

    // Check data
    EXPECT_DOUBLE_EQ(mcp_json_get_number(mcp_json_object_get(nested, "index")),
                     i);
    EXPECT_STREQ(mcp_json_get_string(mcp_json_object_get(nested, "name")),
                 key.c_str());

    mcp_json_value_t array = mcp_json_object_get(nested, "array");
    ASSERT_NE(array, nullptr);
    EXPECT_EQ(mcp_json_array_size(array), 10);

    current = nested;
  }

  // Cleanup (should handle deep recursion)
  mcp_json_free(root);
}

/* ============================================================================
 * Thread Safety Tests
 * ============================================================================
 */

TEST_F(MCPCollectionsTest, ListThreadSafety) {
  const int num_threads = 10;
  const int ops_per_thread = 1000;
  std::vector<std::thread> threads;

  // Create separate lists for each thread
  std::vector<mcp_list_t> lists(num_threads);
  for (int i = 0; i < num_threads; ++i) {
    lists[i] = mcp_list_create(MCP_TYPE_STRING);
    ASSERT_NE(lists[i], nullptr);
  }

  // Launch threads
  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&lists, i]() {
      mcp_list_t list = lists[i];

      // Perform operations
      for (int j = 0; j < ops_per_thread; ++j) {
        int value = i * ops_per_thread + j;
        mcp_list_append(list, (void*)(intptr_t)value);

        if (j % 10 == 0) {
          size_t size = mcp_list_size(list);
          if (size > 0) {
            mcp_list_get(list, size / 2);
          }
        }

        if (j % 50 == 0 && mcp_list_size(list) > 5) {
          mcp_list_remove(list, 0);
        }
      }
    });
  }

  // Wait for completion
  for (auto& thread : threads) {
    thread.join();
  }

  // Verify each list
  for (int i = 0; i < num_threads; ++i) {
    size_t size = mcp_list_size(lists[i]);
    EXPECT_GT(size, 0);
    EXPECT_LE(size, ops_per_thread);
    mcp_list_free(lists[i]);
  }
}

TEST_F(MCPCollectionsTest, MapThreadSafety) {
  const int num_threads = 10;
  const int ops_per_thread = 1000;
  std::vector<std::thread> threads;

  // Create separate maps for each thread
  std::vector<mcp_map_t> maps(num_threads);
  for (int i = 0; i < num_threads; ++i) {
    maps[i] = mcp_map_create(MCP_TYPE_STRING);
    ASSERT_NE(maps[i], nullptr);
  }

  // Launch threads
  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&maps, i]() {
      mcp_map_t map = maps[i];

      // Perform operations
      for (int j = 0; j < ops_per_thread; ++j) {
        std::string key =
            "thread_" + std::to_string(i) + "_key_" + std::to_string(j);
        int value = i * ops_per_thread + j;
        mcp_map_set(map, key.c_str(), (void*)(intptr_t)value);

        if (j % 10 == 0) {
          mcp_map_has(map, key.c_str());
          mcp_map_get(map, key.c_str());
        }

        if (j % 50 == 0 && j > 0) {
          std::string old_key =
              "thread_" + std::to_string(i) + "_key_" + std::to_string(j - 50);
          mcp_map_remove(map, old_key.c_str());
        }
      }
    });
  }

  // Wait for completion
  for (auto& thread : threads) {
    thread.join();
  }

  // Verify each map
  for (int i = 0; i < num_threads; ++i) {
    size_t size = mcp_map_size(maps[i]);
    EXPECT_GT(size, 0);
    EXPECT_LE(size, ops_per_thread);
    mcp_map_free(maps[i]);
  }
}

TEST_F(MCPCollectionsTest, JSONThreadSafety) {
  const int num_threads = 10;
  const int ops_per_thread = 100;
  std::vector<std::thread> threads;
  std::vector<mcp_json_value_t> jsons(num_threads);

  // Launch threads
  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&jsons, i]() {
      // Each thread creates its own JSON structure
      mcp_json_value_t root = mcp_json_create_object();

      for (int j = 0; j < ops_per_thread; ++j) {
        std::string key = "key_" + std::to_string(j);

        // Create various types
        if (j % 4 == 0) {
          mcp_json_object_set(root, key.c_str(), mcp_json_create_number(j));
        } else if (j % 4 == 1) {
          mcp_json_object_set(root, key.c_str(),
                              mcp_json_create_string(key.c_str()));
        } else if (j % 4 == 2) {
          mcp_json_object_set(
              root, key.c_str(),
              mcp_json_create_bool(j % 2 == 0 ? MCP_TRUE : MCP_FALSE));
        } else {
          mcp_json_value_t array = mcp_json_create_array();
          for (int k = 0; k < 5; ++k) {
            mcp_json_array_append(array, mcp_json_create_number(k));
          }
          mcp_json_object_set(root, key.c_str(), array);
        }
      }

      jsons[i] = root;
    });
  }

  // Wait for completion
  for (auto& thread : threads) {
    thread.join();
  }

  // Verify and cleanup
  for (int i = 0; i < num_threads; ++i) {
    ASSERT_NE(jsons[i], nullptr);
    EXPECT_EQ(mcp_json_get_type(jsons[i]), MCP_JSON_TYPE_OBJECT);

    // Check some values
    mcp_json_value_t val = mcp_json_object_get(jsons[i], "key_0");
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(mcp_json_get_type(val), MCP_JSON_TYPE_NUMBER);

    mcp_json_free(jsons[i]);
  }
}

}  // namespace