/**
 * @file test_c_api_json_simple.cc
 * @brief Simple unit tests for JSON functionality in MCP C API
 *
 * Tests JSON value creation, manipulation, and serialization
 * using the functions available in mcp_c_collections.h
 */

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

extern "C" {
#include "mcp/c_api/mcp_c_collections.h"
#include "mcp/c_api/mcp_c_memory.h"
}

namespace {

class MCPJsonSimpleTest : public ::testing::Test {
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
 * JSON Value Creation Tests
 * ============================================================================
 */

TEST_F(MCPJsonSimpleTest, CreateNull) {
  mcp_json_value_t json = mcp_json_create_null();
  ASSERT_NE(json, nullptr);
  EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_NULL);
  mcp_json_free(json);
}

TEST_F(MCPJsonSimpleTest, CreateBool) {
  // Test true
  mcp_json_value_t json_true = mcp_json_create_bool(MCP_TRUE);
  ASSERT_NE(json_true, nullptr);
  EXPECT_EQ(mcp_json_get_type(json_true), MCP_JSON_TYPE_BOOL);
  EXPECT_TRUE(mcp_json_get_bool(json_true));
  mcp_json_free(json_true);

  // Test false
  mcp_json_value_t json_false = mcp_json_create_bool(MCP_FALSE);
  ASSERT_NE(json_false, nullptr);
  EXPECT_EQ(mcp_json_get_type(json_false), MCP_JSON_TYPE_BOOL);
  EXPECT_FALSE(mcp_json_get_bool(json_false));
  mcp_json_free(json_false);
}

TEST_F(MCPJsonSimpleTest, CreateNumber) {
  // Test positive number
  mcp_json_value_t json = mcp_json_create_number(42.5);
  ASSERT_NE(json, nullptr);
  EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_NUMBER);
  EXPECT_DOUBLE_EQ(mcp_json_get_number(json), 42.5);
  mcp_json_free(json);

  // Test zero
  json = mcp_json_create_number(0.0);
  ASSERT_NE(json, nullptr);
  EXPECT_DOUBLE_EQ(mcp_json_get_number(json), 0.0);
  mcp_json_free(json);

  // Test negative number
  json = mcp_json_create_number(-123.456);
  ASSERT_NE(json, nullptr);
  EXPECT_DOUBLE_EQ(mcp_json_get_number(json), -123.456);
  mcp_json_free(json);
}

TEST_F(MCPJsonSimpleTest, CreateString) {
  // Test normal string
  const char* test_str = "Hello, World!";
  mcp_json_value_t json = mcp_json_create_string(test_str);
  ASSERT_NE(json, nullptr);
  EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_STRING);
  EXPECT_STREQ(mcp_json_get_string(json), test_str);
  mcp_json_free(json);

  // Test empty string
  json = mcp_json_create_string("");
  ASSERT_NE(json, nullptr);
  EXPECT_STREQ(mcp_json_get_string(json), "");
  mcp_json_free(json);

  // Test null string
  json = mcp_json_create_string(nullptr);
  EXPECT_EQ(json, nullptr);
}

TEST_F(MCPJsonSimpleTest, CreateArray) {
  mcp_json_value_t array = mcp_json_create_array();
  ASSERT_NE(array, nullptr);
  EXPECT_EQ(mcp_json_get_type(array), MCP_JSON_TYPE_ARRAY);
  EXPECT_EQ(mcp_json_array_size(array), 0);

  // Add elements
  EXPECT_EQ(mcp_json_array_append(array, mcp_json_create_number(1)), MCP_OK);
  EXPECT_EQ(mcp_json_array_append(array, mcp_json_create_string("two")),
            MCP_OK);
  EXPECT_EQ(mcp_json_array_append(array, mcp_json_create_bool(MCP_TRUE)),
            MCP_OK);

  EXPECT_EQ(mcp_json_array_size(array), 3);

  // Get elements
  auto elem0 = mcp_json_array_get(array, 0);
  ASSERT_NE(elem0, nullptr);
  EXPECT_DOUBLE_EQ(mcp_json_get_number(elem0), 1);

  auto elem1 = mcp_json_array_get(array, 1);
  ASSERT_NE(elem1, nullptr);
  EXPECT_STREQ(mcp_json_get_string(elem1), "two");

  auto elem2 = mcp_json_array_get(array, 2);
  ASSERT_NE(elem2, nullptr);
  EXPECT_TRUE(mcp_json_get_bool(elem2));

  // Out of bounds
  EXPECT_EQ(mcp_json_array_get(array, 3), nullptr);

  mcp_json_free(array);
}

TEST_F(MCPJsonSimpleTest, CreateObject) {
  mcp_json_value_t obj = mcp_json_create_object();
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(mcp_json_get_type(obj), MCP_JSON_TYPE_OBJECT);

  // Add properties
  EXPECT_EQ(mcp_json_object_set(obj, "name", mcp_json_create_string("test")),
            MCP_OK);
  EXPECT_EQ(mcp_json_object_set(obj, "value", mcp_json_create_number(42)),
            MCP_OK);
  EXPECT_EQ(mcp_json_object_set(obj, "active", mcp_json_create_bool(MCP_TRUE)),
            MCP_OK);

  // Check has
  EXPECT_TRUE(mcp_json_object_has(obj, "name"));
  EXPECT_TRUE(mcp_json_object_has(obj, "value"));
  EXPECT_TRUE(mcp_json_object_has(obj, "active"));
  EXPECT_FALSE(mcp_json_object_has(obj, "nonexistent"));

  // Get properties
  auto name = mcp_json_object_get(obj, "name");
  ASSERT_NE(name, nullptr);
  EXPECT_STREQ(mcp_json_get_string(name), "test");

  auto value = mcp_json_object_get(obj, "value");
  ASSERT_NE(value, nullptr);
  EXPECT_DOUBLE_EQ(mcp_json_get_number(value), 42);

  auto active = mcp_json_object_get(obj, "active");
  ASSERT_NE(active, nullptr);
  EXPECT_TRUE(mcp_json_get_bool(active));

  // Get nonexistent
  EXPECT_EQ(mcp_json_object_get(obj, "nonexistent"), nullptr);

  // Update property
  EXPECT_EQ(mcp_json_object_set(obj, "value", mcp_json_create_number(100)),
            MCP_OK);
  value = mcp_json_object_get(obj, "value");
  ASSERT_NE(value, nullptr);
  EXPECT_DOUBLE_EQ(mcp_json_get_number(value), 100);

  mcp_json_free(obj);
}

/* ============================================================================
 * Type Checking Tests
 * ============================================================================
 */

TEST_F(MCPJsonSimpleTest, TypeChecking) {
  // Create values of each type
  mcp_json_value_t null_val = mcp_json_create_null();
  mcp_json_value_t bool_val = mcp_json_create_bool(MCP_TRUE);
  mcp_json_value_t num_val = mcp_json_create_number(42);
  mcp_json_value_t str_val = mcp_json_create_string("test");
  mcp_json_value_t arr_val = mcp_json_create_array();
  mcp_json_value_t obj_val = mcp_json_create_object();

  // Check types
  EXPECT_EQ(mcp_json_get_type(null_val), MCP_JSON_TYPE_NULL);
  EXPECT_EQ(mcp_json_get_type(bool_val), MCP_JSON_TYPE_BOOL);
  EXPECT_EQ(mcp_json_get_type(num_val), MCP_JSON_TYPE_NUMBER);
  EXPECT_EQ(mcp_json_get_type(str_val), MCP_JSON_TYPE_STRING);
  EXPECT_EQ(mcp_json_get_type(arr_val), MCP_JSON_TYPE_ARRAY);
  EXPECT_EQ(mcp_json_get_type(obj_val), MCP_JSON_TYPE_OBJECT);

  // Check null pointer
  EXPECT_EQ(mcp_json_get_type(nullptr), MCP_JSON_TYPE_NULL);

  // Clean up
  mcp_json_free(null_val);
  mcp_json_free(bool_val);
  mcp_json_free(num_val);
  mcp_json_free(str_val);
  mcp_json_free(arr_val);
  mcp_json_free(obj_val);
}

/* ============================================================================
 * Type Mismatch Tests
 * ============================================================================
 */

TEST_F(MCPJsonSimpleTest, TypeMismatchHandling) {
  // Create a string value
  mcp_json_value_t str = mcp_json_create_string("test");
  ASSERT_NE(str, nullptr);

  // Try to get as wrong types
  EXPECT_FALSE(mcp_json_get_bool(str));
  EXPECT_EQ(mcp_json_get_number(str), 0.0);
  EXPECT_EQ(mcp_json_array_size(str), 0);

  mcp_json_free(str);

  // Create a number value
  mcp_json_value_t num = mcp_json_create_number(42);
  ASSERT_NE(num, nullptr);

  // Try to get as wrong types
  EXPECT_FALSE(mcp_json_get_bool(num));
  EXPECT_EQ(mcp_json_get_string(num), nullptr);
  EXPECT_EQ(mcp_json_array_size(num), 0);

  mcp_json_free(num);
}

/* ============================================================================
 * Nested Structure Tests
 * ============================================================================
 */

TEST_F(MCPJsonSimpleTest, NestedArrays) {
  // Create array of arrays
  mcp_json_value_t outer = mcp_json_create_array();
  ASSERT_NE(outer, nullptr);

  for (int i = 0; i < 3; ++i) {
    mcp_json_value_t inner = mcp_json_create_array();
    for (int j = 0; j < 3; ++j) {
      mcp_json_array_append(inner, mcp_json_create_number(i * 10 + j));
    }
    mcp_json_array_append(outer, inner);
  }

  EXPECT_EQ(mcp_json_array_size(outer), 3);

  // Verify structure
  for (int i = 0; i < 3; ++i) {
    auto inner = mcp_json_array_get(outer, i);
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(mcp_json_get_type(inner), MCP_JSON_TYPE_ARRAY);
    EXPECT_EQ(mcp_json_array_size(inner), 3);

    for (int j = 0; j < 3; ++j) {
      auto val = mcp_json_array_get(inner, j);
      ASSERT_NE(val, nullptr);
      EXPECT_DOUBLE_EQ(mcp_json_get_number(val), i * 10 + j);
    }
  }

  mcp_json_free(outer);
}

TEST_F(MCPJsonSimpleTest, NestedObjects) {
  // Create nested object structure
  mcp_json_value_t root = mcp_json_create_object();
  ASSERT_NE(root, nullptr);

  // Add user object
  mcp_json_value_t user = mcp_json_create_object();
  mcp_json_object_set(user, "name", mcp_json_create_string("John"));
  mcp_json_object_set(user, "age", mcp_json_create_number(30));

  // Add address object
  mcp_json_value_t address = mcp_json_create_object();
  mcp_json_object_set(address, "street", mcp_json_create_string("123 Main St"));
  mcp_json_object_set(address, "city", mcp_json_create_string("New York"));
  mcp_json_object_set(address, "zip", mcp_json_create_string("10001"));

  mcp_json_object_set(user, "address", address);
  mcp_json_object_set(root, "user", user);

  // Add metadata
  mcp_json_value_t meta = mcp_json_create_object();
  mcp_json_object_set(meta, "version", mcp_json_create_string("1.0"));
  mcp_json_object_set(meta, "timestamp", mcp_json_create_number(1234567890));
  mcp_json_object_set(root, "metadata", meta);

  // Verify structure
  auto retrieved_user = mcp_json_object_get(root, "user");
  ASSERT_NE(retrieved_user, nullptr);

  auto name = mcp_json_object_get(retrieved_user, "name");
  ASSERT_NE(name, nullptr);
  EXPECT_STREQ(mcp_json_get_string(name), "John");

  auto retrieved_address = mcp_json_object_get(retrieved_user, "address");
  ASSERT_NE(retrieved_address, nullptr);

  auto city = mcp_json_object_get(retrieved_address, "city");
  ASSERT_NE(city, nullptr);
  EXPECT_STREQ(mcp_json_get_string(city), "New York");

  auto retrieved_meta = mcp_json_object_get(root, "metadata");
  ASSERT_NE(retrieved_meta, nullptr);

  auto version = mcp_json_object_get(retrieved_meta, "version");
  ASSERT_NE(version, nullptr);
  EXPECT_STREQ(mcp_json_get_string(version), "1.0");

  mcp_json_free(root);
}

TEST_F(MCPJsonSimpleTest, MixedNesting) {
  // Create object with array of objects
  mcp_json_value_t root = mcp_json_create_object();
  mcp_json_value_t items = mcp_json_create_array();

  for (int i = 0; i < 3; ++i) {
    mcp_json_value_t item = mcp_json_create_object();
    std::string id = "item" + std::to_string(i);
    mcp_json_object_set(item, "id", mcp_json_create_string(id.c_str()));
    mcp_json_object_set(item, "value", mcp_json_create_number(i * 10));
    mcp_json_object_set(
        item, "active",
        mcp_json_create_bool(i % 2 == 0 ? MCP_TRUE : MCP_FALSE));
    mcp_json_array_append(items, item);
  }

  mcp_json_object_set(root, "items", items);
  mcp_json_object_set(root, "count", mcp_json_create_number(3));

  // Verify
  auto retrieved_items = mcp_json_object_get(root, "items");
  ASSERT_NE(retrieved_items, nullptr);
  EXPECT_EQ(mcp_json_array_size(retrieved_items), 3);

  auto count = mcp_json_object_get(root, "count");
  ASSERT_NE(count, nullptr);
  EXPECT_DOUBLE_EQ(mcp_json_get_number(count), 3);

  // Check first item
  auto first = mcp_json_array_get(retrieved_items, 0);
  ASSERT_NE(first, nullptr);

  auto id = mcp_json_object_get(first, "id");
  ASSERT_NE(id, nullptr);
  EXPECT_STREQ(mcp_json_get_string(id), "item0");

  auto active = mcp_json_object_get(first, "active");
  ASSERT_NE(active, nullptr);
  EXPECT_TRUE(mcp_json_get_bool(active));

  mcp_json_free(root);
}

/* ============================================================================
 * Error Handling Tests
 * ============================================================================
 */

TEST_F(MCPJsonSimpleTest, NullParameterHandling) {
  // Array operations with null
  EXPECT_EQ(mcp_json_array_append(nullptr, mcp_json_create_null()),
            MCP_ERROR_INVALID_ARGUMENT);

  mcp_json_value_t array = mcp_json_create_array();
  EXPECT_EQ(mcp_json_array_append(array, nullptr), MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_json_array_size(nullptr), 0);
  EXPECT_EQ(mcp_json_array_get(nullptr, 0), nullptr);
  mcp_json_free(array);

  // Object operations with null
  EXPECT_EQ(mcp_json_object_set(nullptr, "key", mcp_json_create_null()),
            MCP_ERROR_INVALID_ARGUMENT);

  mcp_json_value_t obj = mcp_json_create_object();
  EXPECT_EQ(mcp_json_object_set(obj, nullptr, mcp_json_create_null()),
            MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_json_object_set(obj, "key", nullptr),
            MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_json_object_get(nullptr, "key"), nullptr);
  EXPECT_EQ(mcp_json_object_get(obj, nullptr), nullptr);
  EXPECT_FALSE(mcp_json_object_has(nullptr, "key"));
  EXPECT_FALSE(mcp_json_object_has(obj, nullptr));
  mcp_json_free(obj);

  // Free null is safe
  mcp_json_free(nullptr);
}

/* ============================================================================
 * Memory Management Tests
 * ============================================================================
 */

TEST_F(MCPJsonSimpleTest, MemoryManagement) {
  // Test that freeing complex structure doesn't leak
  mcp_json_value_t root = mcp_json_create_object();

  // Build complex structure
  for (int i = 0; i < 10; ++i) {
    mcp_json_value_t arr = mcp_json_create_array();
    for (int j = 0; j < 10; ++j) {
      mcp_json_value_t obj = mcp_json_create_object();
      mcp_json_object_set(obj, "index", mcp_json_create_number(j));
      mcp_json_object_set(obj, "data", mcp_json_create_string("test"));
      mcp_json_array_append(arr, obj);
    }
    std::string key = "array_" + std::to_string(i);
    mcp_json_object_set(root, key.c_str(), arr);
  }

  // Single free should recursively free everything
  mcp_json_free(root);
}

/* ============================================================================
 * Stress Tests
 * ============================================================================
 */

TEST_F(MCPJsonSimpleTest, StressLargeArray) {
  mcp_json_value_t array = mcp_json_create_array();
  ASSERT_NE(array, nullptr);

  const int count = 10000;
  for (int i = 0; i < count; ++i) {
    EXPECT_EQ(mcp_json_array_append(array, mcp_json_create_number(i)), MCP_OK);
  }

  EXPECT_EQ(mcp_json_array_size(array), count);

  // Verify some random elements
  for (int i = 0; i < 100; ++i) {
    int idx = (i * 97) % count;  // Pseudo-random access
    auto elem = mcp_json_array_get(array, idx);
    ASSERT_NE(elem, nullptr);
    EXPECT_DOUBLE_EQ(mcp_json_get_number(elem), idx);
  }

  mcp_json_free(array);
}

TEST_F(MCPJsonSimpleTest, StressLargeObject) {
  mcp_json_value_t obj = mcp_json_create_object();
  ASSERT_NE(obj, nullptr);

  const int count = 1000;
  for (int i = 0; i < count; ++i) {
    std::string key = "key_" + std::to_string(i);
    EXPECT_EQ(mcp_json_object_set(obj, key.c_str(), mcp_json_create_number(i)),
              MCP_OK);
  }

  // Verify some entries
  for (int i = 0; i < 100; ++i) {
    int idx = (i * 97) % count;
    std::string key = "key_" + std::to_string(idx);
    EXPECT_TRUE(mcp_json_object_has(obj, key.c_str()));

    auto val = mcp_json_object_get(obj, key.c_str());
    ASSERT_NE(val, nullptr);
    EXPECT_DOUBLE_EQ(mcp_json_get_number(val), idx);
  }

  mcp_json_free(obj);
}

TEST_F(MCPJsonSimpleTest, StressDeepNesting) {
  mcp_json_value_t root = mcp_json_create_object();
  mcp_json_value_t current = root;

  const int depth = 100;
  for (int i = 0; i < depth; ++i) {
    mcp_json_value_t nested = mcp_json_create_object();
    mcp_json_object_set(nested, "depth", mcp_json_create_number(i));
    std::string key = "level_" + std::to_string(i);
    mcp_json_object_set(current, key.c_str(), nested);
    current = nested;
  }

  // Add final value
  mcp_json_object_set(current, "final", mcp_json_create_string("deepest"));

  // Verify we can traverse to the deepest level
  current = root;
  for (int i = 0; i < depth; ++i) {
    std::string key = "level_" + std::to_string(i);
    current = mcp_json_object_get(current, key.c_str());
    ASSERT_NE(current, nullptr);

    auto depth_val = mcp_json_object_get(current, "depth");
    if (depth_val) {
      EXPECT_DOUBLE_EQ(mcp_json_get_number(depth_val), i);
    }
  }

  auto final_val = mcp_json_object_get(current, "final");
  ASSERT_NE(final_val, nullptr);
  EXPECT_STREQ(mcp_json_get_string(final_val), "deepest");

  mcp_json_free(root);
}

/* ============================================================================
 * Thread Safety Tests
 * ============================================================================
 */

TEST_F(MCPJsonSimpleTest, ThreadSafeCreation) {
  const int num_threads = 10;
  const int ops_per_thread = 100;
  std::vector<std::thread> threads;
  std::vector<mcp_json_value_t> results(num_threads);

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&results, t, ops_per_thread]() {
      mcp_json_value_t arr = mcp_json_create_array();
      for (int i = 0; i < ops_per_thread; ++i) {
        mcp_json_value_t obj = mcp_json_create_object();
        std::string id =
            "thread_" + std::to_string(t) + "_item_" + std::to_string(i);
        mcp_json_object_set(obj, "id", mcp_json_create_string(id.c_str()));
        mcp_json_object_set(obj, "thread", mcp_json_create_number(t));
        mcp_json_object_set(obj, "index", mcp_json_create_number(i));
        mcp_json_array_append(arr, obj);
      }
      results[t] = arr;
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // Verify results
  for (int t = 0; t < num_threads; ++t) {
    ASSERT_NE(results[t], nullptr);
    EXPECT_EQ(mcp_json_array_size(results[t]), ops_per_thread);

    // Check first item
    auto first = mcp_json_array_get(results[t], 0);
    ASSERT_NE(first, nullptr);

    auto thread_num = mcp_json_object_get(first, "thread");
    ASSERT_NE(thread_num, nullptr);
    EXPECT_DOUBLE_EQ(mcp_json_get_number(thread_num), t);

    mcp_json_free(results[t]);
  }
}

}  // namespace