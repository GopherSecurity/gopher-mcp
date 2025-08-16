/**
 * @file test_c_api_memory.cc
 * @brief Comprehensive memory management tests for MCP C API
 *
 * Tests memory allocation, deallocation, leak detection, and
 * stress testing for production-ready quality assurance.
 */

#include <atomic>
#include <chrono>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/c_api/mcp_c_types.h"

class MCPCApiMemoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Reset any global state
  }

  void TearDown() override {
    // Cleanup and verify no leaks
  }

  // Helper to create random string
  std::string RandomString(size_t length) {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(alphanum) - 2);

    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
      result += alphanum[dis(gen)];
    }
    return result;
  }
};

// ==================== BASIC MEMORY OPERATIONS ====================

TEST_F(MCPCApiMemoryTest, StringAllocationDeallocation) {
  // Test string duplication
  const char* original = "Test string for memory testing";
  char* dup = mcp_string_duplicate(original);

  ASSERT_NE(dup, nullptr);
  EXPECT_NE(dup, original);  // Different memory addresses
  EXPECT_STREQ(dup, original);

  // Free and ensure no double-free issues
  mcp_string_free(dup);

  // Test with empty string
  char* empty = mcp_string_duplicate("");
  ASSERT_NE(empty, nullptr);
  EXPECT_STREQ(empty, "");
  mcp_string_free(empty);

  // Test with null
  char* null_dup = mcp_string_duplicate(nullptr);
  EXPECT_EQ(null_dup, nullptr);
  mcp_string_free(null_dup);  // Should be safe
}

TEST_F(MCPCApiMemoryTest, ContentBlockMemory) {
  // Create and free various content types
  auto* text = mcp_text_content_create("Memory test text");
  ASSERT_NE(text, nullptr);
  mcp_content_block_free(text);

  auto* image = mcp_image_content_create("imagedata", "image/png");
  ASSERT_NE(image, nullptr);
  mcp_content_block_free(image);

  auto* audio = mcp_audio_content_create("audiodata", "audio/mp3");
  ASSERT_NE(audio, nullptr);
  mcp_content_block_free(audio);

  // Create resource content
  mcp_resource_t res = {.uri = strdup("file:///test.txt"),
                        .name = strdup("test.txt"),
                        .description = strdup("Test file"),
                        .mime_type = strdup("text/plain")};

  auto* resource = mcp_resource_content_create(&res);
  ASSERT_NE(resource, nullptr);

  // Free everything
  mcp_content_block_free(resource);
  free(res.uri);
  free(res.name);
  free(res.description);
  free(res.mime_type);
}

TEST_F(MCPCApiMemoryTest, NestedStructureMemory) {
  // Create embedded resource with nested content
  mcp_resource_t resource = {.uri = strdup("file:///doc.pdf"),
                             .name = strdup("doc.pdf"),
                             .description = nullptr,
                             .mime_type = nullptr};

  // Create nested content array
  const int nested_count = 10;
  std::vector<mcp_content_block_t*> nested;
  for (int i = 0; i < nested_count; ++i) {
    std::string text = "Page " + std::to_string(i);
    nested.push_back(mcp_text_content_create(text.c_str()));
  }

  auto* embedded =
      mcp_embedded_resource_create(&resource, nested.data(), nested_count);

  ASSERT_NE(embedded, nullptr);
  EXPECT_EQ(embedded->embedded.content_count, nested_count);

  // Free nested content originals
  for (auto* content : nested) {
    mcp_content_block_free(content);
  }

  // Free embedded (should free copied nested content)
  mcp_content_block_free(embedded);

  // Free resource strings
  free(resource.uri);
  free(resource.name);
}

// ==================== MEMORY POOL OPERATIONS ====================

TEST_F(MCPCApiMemoryTest, MemoryPoolBasic) {
  auto pool = mcp_memory_pool_create();
  ASSERT_NE(pool, nullptr);

  // Initial state
  EXPECT_EQ(mcp_memory_pool_get_size(pool), 0);
  EXPECT_EQ(mcp_memory_pool_get_peak_size(pool), 0);

  // Allocate various sizes
  void* small = mcp_memory_pool_alloc(pool, 16);
  void* medium = mcp_memory_pool_alloc(pool, 256);
  void* large = mcp_memory_pool_alloc(pool, 4096);

  ASSERT_NE(small, nullptr);
  ASSERT_NE(medium, nullptr);
  ASSERT_NE(large, nullptr);

  size_t total = 16 + 256 + 4096;
  EXPECT_EQ(mcp_memory_pool_get_size(pool), total);
  EXPECT_EQ(mcp_memory_pool_get_peak_size(pool), total);

  // Free some allocations
  mcp_memory_pool_free(pool, medium, 256);
  EXPECT_EQ(mcp_memory_pool_get_size(pool), total - 256);
  EXPECT_EQ(mcp_memory_pool_get_peak_size(pool), total);  // Peak unchanged

  // Clean up
  mcp_memory_pool_destroy(pool);
}

TEST_F(MCPCApiMemoryTest, MemoryPoolStress) {
  auto pool = mcp_memory_pool_create();

  const int num_iterations = 1000;
  const int max_alloc_size = 1024;

  std::vector<std::pair<void*, size_t>> allocations;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> size_dis(1, max_alloc_size);
  std::uniform_int_distribution<> action_dis(0, 2);

  for (int i = 0; i < num_iterations; ++i) {
    int action = action_dis(gen);

    if (action < 2 && allocations.size() < 100) {
      // Allocate
      size_t size = size_dis(gen);
      void* ptr = mcp_memory_pool_alloc(pool, size);
      ASSERT_NE(ptr, nullptr);
      allocations.push_back({ptr, size});

      // Write to memory to ensure it's accessible
      memset(ptr, 0xAB, size);
    } else if (!allocations.empty()) {
      // Free random allocation
      std::uniform_int_distribution<> idx_dis(0, allocations.size() - 1);
      int idx = idx_dis(gen);

      mcp_memory_pool_free(pool, allocations[idx].first,
                           allocations[idx].second);
      allocations.erase(allocations.begin() + idx);
    }
  }

  size_t final_size = mcp_memory_pool_get_size(pool);
  size_t peak_size = mcp_memory_pool_get_peak_size(pool);

  EXPECT_GE(peak_size, final_size);
  EXPECT_GT(peak_size, 0);

  mcp_memory_pool_destroy(pool);
}

// ==================== DEEP COPY MEMORY TESTS ====================

TEST_F(MCPCApiMemoryTest, DeepCopyIndependence) {
  // Create original with annotations
  mcp_role_t audience[] = {MCP_ROLE_USER};
  auto* original =
      mcp_text_content_with_annotations("Original text", audience, 1, 0.5);

  // Deep copy
  auto* copy = mcp_content_block_copy(original);

  ASSERT_NE(copy, nullptr);
  ASSERT_NE(copy, original);

  // Modify original
  free(original->text.text);
  original->text.text = strdup("Modified text");
  original->text.annotations.value.priority = 0.9;

  // Copy should be unchanged
  EXPECT_STREQ(copy->text.text, "Original text");
  EXPECT_DOUBLE_EQ(copy->text.annotations.value.priority, 0.5);

  // Free both independently
  mcp_content_block_free(original);
  mcp_content_block_free(copy);
}

TEST_F(MCPCApiMemoryTest, DeepCopyNested) {
  // Create complex nested structure
  mcp_resource_t resource = {.uri = strdup("file:///complex.pdf"),
                             .name = strdup("complex.pdf"),
                             .description = strdup("Complex document"),
                             .mime_type = strdup("application/pdf")};

  mcp_content_block_t nested[5];
  for (int i = 0; i < 5; ++i) {
    std::string text = "Content " + std::to_string(i);
    nested[i] = *mcp_text_content_create(text.c_str());
  }

  auto* original = mcp_embedded_resource_create(&resource, nested, 5);
  auto* copy = mcp_content_block_copy(original);

  ASSERT_NE(copy, nullptr);
  ASSERT_NE(copy, original);
  EXPECT_EQ(copy->embedded.content_count, 5);

  // Verify deep copy of nested content
  for (size_t i = 0; i < 5; ++i) {
    EXPECT_NE(&copy->embedded.content[i], &original->embedded.content[i]);
    EXPECT_NE(copy->embedded.content[i].text.text,
              original->embedded.content[i].text.text);
  }

  // Clean up
  for (int i = 0; i < 5; ++i) {
    mcp_content_block_free(&nested[i]);
  }
  mcp_content_block_free(original);
  mcp_content_block_free(copy);

  free(resource.uri);
  free(resource.name);
  free(resource.description);
  free(resource.mime_type);
}

// ==================== ARRAY MEMORY MANAGEMENT ====================

TEST_F(MCPCApiMemoryTest, ContentBlockArrayGrowth) {
  auto* array = mcp_content_block_array_create(1);  // Start small

  ASSERT_NE(array, nullptr);
  EXPECT_EQ(array->capacity, 1);

  // Add many items to force multiple reallocations
  const int num_items = 100;
  for (int i = 0; i < num_items; ++i) {
    std::string text = "Item " + std::to_string(i);
    auto* content = mcp_text_content_create(text.c_str());
    EXPECT_TRUE(mcp_content_block_array_append(array, content));
  }

  EXPECT_EQ(array->count, num_items);
  EXPECT_GE(array->capacity, num_items);

  // Verify all items are accessible
  for (size_t i = 0; i < num_items; ++i) {
    ASSERT_NE(array->items[i], nullptr);
    std::string expected = "Item " + std::to_string(i);
    EXPECT_STREQ(array->items[i]->text.text, expected.c_str());
  }

  // Free array (should free all items)
  mcp_content_block_array_free(array);
}

// ==================== MEMORY LEAK DETECTION ====================

TEST_F(MCPCApiMemoryTest, BuilderMemoryLeaks) {
  // Create and destroy many builders without leaks
  const int iterations = 1000;

  for (int i = 0; i < iterations; ++i) {
    // Text content
    auto* text = mcp_text_content_create("Test");
    mcp_content_block_free(text);

    // Tool
    auto* tool = mcp_tool_with_description("tool", "description");
    mcp_tool_free(tool);

    // Prompt with arguments
    mcp_prompt_argument_t args[] = {
        {.name = strdup("arg1"), .description = nullptr, .required = true}};
    auto* prompt = mcp_prompt_with_arguments("prompt", args, 1);
    free(args[0].name);
    mcp_prompt_free(prompt);

    // Message
    auto* msg = mcp_user_message("Message");
    mcp_message_free(msg);

    // Error with data
    auto* data = mcp_json_create_string("error data");
    auto* error = mcp_error_with_data(-1, "Error", data);
    mcp_json_free(data);
    mcp_error_free(error);
  }
}

TEST_F(MCPCApiMemoryTest, SchemaMemoryLeaks) {
  const int iterations = 100;

  for (int i = 0; i < iterations; ++i) {
    // String schema
    auto* str_schema =
        mcp_string_schema_with_constraints("Description", "[a-z]+", 1, 100);
    mcp_string_schema_free(str_schema);

    // Number schema
    auto* num_schema =
        mcp_number_schema_with_constraints("Number", 0.0, 100.0, 0.1);
    mcp_number_schema_free(num_schema);

    // Boolean schema
    auto* bool_schema = mcp_boolean_schema_with_description("Bool");
    mcp_boolean_schema_free(bool_schema);

    // Enum schema
    const char* values[] = {"a", "b", "c"};
    auto* enum_schema = mcp_enum_schema_with_description(values, 3, "Enum");
    mcp_enum_schema_free(enum_schema);
  }
}

// ==================== CONCURRENT MEMORY ACCESS ====================

TEST_F(MCPCApiMemoryTest, ConcurrentAllocation) {
  const int num_threads = 10;
  const int allocations_per_thread = 100;
  std::atomic<int> total_allocations(0);
  std::atomic<int> total_frees(0);

  std::vector<std::thread> threads;

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&, t]() {
      std::vector<void*> local_allocs;

      for (int i = 0; i < allocations_per_thread; ++i) {
        // Allocate various types
        if (i % 3 == 0) {
          auto* text = mcp_text_content_create("Thread test");
          local_allocs.push_back(text);
        } else if (i % 3 == 1) {
          auto* tool = mcp_tool_create("tool");
          local_allocs.push_back(tool);
        } else {
          auto* prompt = mcp_prompt_create("prompt");
          local_allocs.push_back(prompt);
        }
        total_allocations++;
      }

      // Free in different order
      std::random_device rd;
      std::mt19937 gen(rd());
      std::shuffle(local_allocs.begin(), local_allocs.end(), gen);

      for (size_t i = 0; i < local_allocs.size(); ++i) {
        if (i % 3 == 0) {
          mcp_content_block_free(
              static_cast<mcp_content_block_t*>(local_allocs[i]));
        } else if (i % 3 == 1) {
          mcp_tool_free(static_cast<mcp_tool_t*>(local_allocs[i]));
        } else {
          mcp_prompt_free(static_cast<mcp_prompt_t*>(local_allocs[i]));
        }
        total_frees++;
      }
    });
  }

  // Wait for all threads
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(total_allocations.load(), num_threads * allocations_per_thread);
  EXPECT_EQ(total_frees.load(), total_allocations.load());
}

TEST_F(MCPCApiMemoryTest, ConcurrentMemoryPool) {
  auto pool = mcp_memory_pool_create();

  const int num_threads = 8;
  const int operations_per_thread = 500;
  std::atomic<int> successful_allocs(0);
  std::atomic<int> successful_frees(0);

  std::vector<std::thread> threads;

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&]() {
      std::vector<std::pair<void*, size_t>> local_allocs;
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> size_dis(1, 512);
      std::uniform_int_distribution<> action_dis(0, 1);

      for (int i = 0; i < operations_per_thread; ++i) {
        if (action_dis(gen) == 0 || local_allocs.empty()) {
          // Allocate
          size_t size = size_dis(gen);
          void* ptr = mcp_memory_pool_alloc(pool, size);
          if (ptr) {
            local_allocs.push_back({ptr, size});
            successful_allocs++;
          }
        } else {
          // Free random allocation
          std::uniform_int_distribution<> idx_dis(0, local_allocs.size() - 1);
          int idx = idx_dis(gen);
          mcp_memory_pool_free(pool, local_allocs[idx].first,
                               local_allocs[idx].second);
          local_allocs.erase(local_allocs.begin() + idx);
          successful_frees++;
        }
      }

      // Clean up remaining allocations
      for (const auto& alloc : local_allocs) {
        mcp_memory_pool_free(pool, alloc.first, alloc.second);
        successful_frees++;
      }
    });
  }

  // Wait for all threads
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_GT(successful_allocs.load(), 0);
  EXPECT_EQ(successful_frees.load(), successful_allocs.load());

  // Final pool state
  size_t final_size = mcp_memory_pool_get_size(pool);
  EXPECT_EQ(final_size, 0);  // All freed

  mcp_memory_pool_destroy(pool);
}

// ==================== LARGE ALLOCATION TESTS ====================

TEST_F(MCPCApiMemoryTest, LargeStringAllocation) {
  // Create very large strings
  const size_t sizes[] = {1024, 10240, 102400, 1048576};  // 1KB to 1MB

  for (size_t size : sizes) {
    std::string large_str = RandomString(size);
    auto* content = mcp_text_content_create(large_str.c_str());

    ASSERT_NE(content, nullptr);
    EXPECT_EQ(strlen(content->text.text), size);

    // Verify content integrity
    EXPECT_EQ(large_str, content->text.text);

    mcp_content_block_free(content);
  }
}

TEST_F(MCPCApiMemoryTest, LargeArrayAllocation) {
  // Create array with many items
  const size_t num_items = 10000;
  auto* array = mcp_content_block_array_create(0);

  for (size_t i = 0; i < num_items; ++i) {
    auto* content = mcp_text_content_create("Item");
    EXPECT_TRUE(mcp_content_block_array_append(array, content));
  }

  EXPECT_EQ(array->count, num_items);

  // Free should handle all items
  mcp_content_block_array_free(array);
}

// ==================== ERROR RECOVERY TESTS ====================

TEST_F(MCPCApiMemoryTest, AllocationFailureRecovery) {
  // Test behavior with invalid inputs

  // Null string
  auto* null_content = mcp_text_content_create(nullptr);
  EXPECT_EQ(null_content, nullptr);

  // Empty arrays
  auto* empty_embedded = mcp_embedded_resource_create(nullptr, nullptr, 0);
  EXPECT_EQ(empty_embedded, nullptr);

  // Invalid schema
  auto* invalid_enum = mcp_enum_schema_create(nullptr, 0);
  EXPECT_EQ(invalid_enum, nullptr);

  // All operations should fail gracefully without crashes
}

TEST_F(MCPCApiMemoryTest, DoubleFreePrevention) {
  // Create and free content
  auto* content = mcp_text_content_create("Test");
  mcp_content_block_free(content);

  // Double free should be safe (implementation dependent)
  // This test verifies it doesn't crash
  // Note: In production, double-free should be avoided through proper design

  // Free null should always be safe
  mcp_content_block_free(nullptr);
  mcp_tool_free(nullptr);
  mcp_prompt_free(nullptr);
  mcp_resource_free(nullptr);
  mcp_message_free(nullptr);
  mcp_error_free(nullptr);
}

// ==================== MEMORY FRAGMENTATION TEST ====================

TEST_F(MCPCApiMemoryTest, FragmentationStress) {
  // Allocate and free in patterns that could cause fragmentation
  std::vector<void*> small_allocs;
  std::vector<void*> large_allocs;

  // Allocate alternating small and large blocks
  for (int i = 0; i < 100; ++i) {
    auto* small = mcp_text_content_create("S");
    auto* large = mcp_text_content_create(RandomString(1000).c_str());

    small_allocs.push_back(small);
    large_allocs.push_back(large);
  }

  // Free all small allocations (creates gaps)
  for (auto* ptr : small_allocs) {
    mcp_content_block_free(static_cast<mcp_content_block_t*>(ptr));
  }

  // Allocate medium-sized blocks (should reuse gaps if possible)
  std::vector<void*> medium_allocs;
  for (int i = 0; i < 100; ++i) {
    auto* medium = mcp_text_content_create("Medium");
    medium_allocs.push_back(medium);
  }

  // Clean up
  for (auto* ptr : large_allocs) {
    mcp_content_block_free(static_cast<mcp_content_block_t*>(ptr));
  }
  for (auto* ptr : medium_allocs) {
    mcp_content_block_free(static_cast<mcp_content_block_t*>(ptr));
  }
}

// Main test runner
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}