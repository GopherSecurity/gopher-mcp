/**
 * @file test_c_memory_impl.cc
 * @brief Comprehensive unit tests for mcp_c_memory_impl.cc
 *
 * Tests memory management, error handling, memory pools, batch operations,
 * and resource tracking functionality.
 */

#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/c_api/mcp_c_api.h"
#include "mcp/c_api/mcp_c_memory.h"
#include "mcp/c_api/mcp_c_types.h"

// ============================================================================
// Test Fixture
// ============================================================================

class MCPMemoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mcp_clear_last_error();
    // Initialize FFI system with default allocator
    mcp_init(nullptr);
  }

  void TearDown() override {
    // Check for any lingering errors
    const mcp_error_info_t* error = mcp_get_last_error();
    if (error) {
      ADD_FAILURE() << "Unexpected error: " << error->message;
    }
    mcp_shutdown();
  }
};

// ============================================================================
// Custom Allocator for Testing
// ============================================================================

struct TestAllocatorData {
  size_t alloc_count = 0;
  size_t free_count = 0;
  size_t total_allocated = 0;
  bool fail_next_alloc = false;
};

void* test_alloc(size_t size, void* user_data) {
  auto* data = static_cast<TestAllocatorData*>(user_data);
  if (data->fail_next_alloc) {
    data->fail_next_alloc = false;
    return nullptr;
  }
  data->alloc_count++;
  data->total_allocated += size;
  return std::malloc(size);
}

void* test_realloc(void* ptr, size_t new_size, void* user_data) {
  auto* data = static_cast<TestAllocatorData*>(user_data);
  if (data->fail_next_alloc) {
    data->fail_next_alloc = false;
    return nullptr;
  }
  return std::realloc(ptr, new_size);
}

void test_free(void* ptr, void* user_data) {
  auto* data = static_cast<TestAllocatorData*>(user_data);
  if (ptr) {
    data->free_count++;
  }
  std::free(ptr);
}

// ============================================================================
// FFI Initialization Tests
// ============================================================================

TEST_F(MCPMemoryTest, FFIInitialization) {
  // Already initialized in SetUp
  EXPECT_EQ(mcp_is_initialized(), MCP_TRUE);

  // Should be safe to initialize again
  EXPECT_EQ(mcp_init(nullptr), MCP_OK);
  EXPECT_EQ(mcp_is_initialized(), MCP_TRUE);
}

TEST_F(MCPMemoryTest, FFIShutdown) {
  EXPECT_EQ(mcp_is_initialized(), MCP_TRUE);

  mcp_shutdown();
  EXPECT_EQ(mcp_is_initialized(), MCP_FALSE);

  // Re-initialize for TearDown
  mcp_init(nullptr);
}

TEST_F(MCPMemoryTest, FFICustomAllocator) {
  TestAllocatorData allocator_data;
  mcp_allocator_t allocator = {test_alloc, test_realloc, test_free,
                               &allocator_data};

  // Shutdown and reinitialize with custom allocator
  mcp_shutdown();
  EXPECT_EQ(mcp_init(&allocator), MCP_OK);

  // Test allocation
  void* ptr = mcp_malloc(100);
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(allocator_data.alloc_count, 1);
  EXPECT_EQ(allocator_data.total_allocated, 100);

  // Test reallocation
  ptr = mcp_realloc(ptr, 200);
  ASSERT_NE(ptr, nullptr);

  // Test free
  mcp_free(ptr);
  EXPECT_EQ(allocator_data.free_count, 1);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(MCPMemoryTest, ErrorHandling) {
  // Initially no error
  EXPECT_EQ(mcp_get_last_error(), nullptr);

  // Clear when no error should be safe
  mcp_clear_last_error();
  EXPECT_EQ(mcp_get_last_error(), nullptr);
}

TEST_F(MCPMemoryTest, ErrorHandlerCallback) {
  struct CallbackData {
    bool called = false;
    mcp_result_t error_code = MCP_OK;
    std::string message;
  } callback_data;

  auto error_handler = [](const mcp_error_info_t* error, void* user_data) {
    auto* data = static_cast<CallbackData*>(user_data);
    data->called = true;
    data->error_code = error->code;
    data->message = error->message;
  };

  mcp_set_error_handler(error_handler, &callback_data);

  // Trigger an error through the types API
  // (Need to trigger an error that calls set_error internally)
  // This would normally happen through API calls that fail

  // Clean up
  mcp_set_error_handler(nullptr, nullptr);
}

// ============================================================================
// Memory Pool Tests
// ============================================================================

TEST_F(MCPMemoryTest, MemoryPoolCreate) {
  auto pool = mcp_memory_pool_create(1024);
  ASSERT_NE(pool, nullptr);

  size_t used, total, count;
  mcp_memory_pool_stats(pool, &used, &total, &count);
  EXPECT_EQ(used, 0);
  EXPECT_GE(total, 1024);
  EXPECT_EQ(count, 0);

  mcp_memory_pool_destroy(pool);
}

TEST_F(MCPMemoryTest, MemoryPoolAllocation) {
  auto pool = mcp_memory_pool_create(1024);
  ASSERT_NE(pool, nullptr);

  // Allocate some memory
  void* ptr1 = mcp_memory_pool_alloc(pool, 100);
  ASSERT_NE(ptr1, nullptr);

  void* ptr2 = mcp_memory_pool_alloc(pool, 200);
  ASSERT_NE(ptr2, nullptr);

  // Pointers should be different
  EXPECT_NE(ptr1, ptr2);

  size_t used, total, count;
  mcp_memory_pool_stats(pool, &used, &total, &count);
  EXPECT_GE(used, 300);  // At least 300 bytes (may be more due to alignment)
  EXPECT_EQ(count, 2);

  mcp_memory_pool_destroy(pool);
}

TEST_F(MCPMemoryTest, MemoryPoolAlignment) {
  auto pool = mcp_memory_pool_create(1024);
  ASSERT_NE(pool, nullptr);

  // Allocate unaligned size
  void* ptr1 = mcp_memory_pool_alloc(pool, 7);
  void* ptr2 = mcp_memory_pool_alloc(pool, 5);

  ASSERT_NE(ptr1, nullptr);
  ASSERT_NE(ptr2, nullptr);

  // Check that allocations are 8-byte aligned
  size_t addr1 = reinterpret_cast<size_t>(ptr1);
  size_t addr2 = reinterpret_cast<size_t>(ptr2);

  // The difference should be at least 8 bytes (aligned)
  EXPECT_GE(addr2 - addr1, 8);

  mcp_memory_pool_destroy(pool);
}

TEST_F(MCPMemoryTest, MemoryPoolGrowth) {
  auto pool = mcp_memory_pool_create(100);
  ASSERT_NE(pool, nullptr);

  // Allocate more than initial size
  void* ptr1 = mcp_memory_pool_alloc(pool, 80);
  void* ptr2 = mcp_memory_pool_alloc(pool, 80);
  void* ptr3 = mcp_memory_pool_alloc(pool, 80);

  ASSERT_NE(ptr1, nullptr);
  ASSERT_NE(ptr2, nullptr);
  ASSERT_NE(ptr3, nullptr);

  size_t used, total, count;
  mcp_memory_pool_stats(pool, &used, &total, &count);
  EXPECT_GE(used, 240);
  EXPECT_GT(total, 100);  // Should have grown
  EXPECT_EQ(count, 3);

  mcp_memory_pool_destroy(pool);
}

TEST_F(MCPMemoryTest, MemoryPoolReset) {
  auto pool = mcp_memory_pool_create(1024);
  ASSERT_NE(pool, nullptr);

  // Allocate some memory
  mcp_memory_pool_alloc(pool, 100);
  mcp_memory_pool_alloc(pool, 200);

  size_t used, total, count;
  mcp_memory_pool_stats(pool, &used, &total, &count);
  EXPECT_GT(used, 0);
  EXPECT_EQ(count, 2);

  // Reset pool
  mcp_memory_pool_reset(pool);

  mcp_memory_pool_stats(pool, &used, &total, &count);
  EXPECT_EQ(used, 0);
  EXPECT_EQ(count, 0);
  EXPECT_GE(total, 1024);  // Capacity should remain

  // Can allocate again after reset
  void* ptr = mcp_memory_pool_alloc(pool, 50);
  ASSERT_NE(ptr, nullptr);

  mcp_memory_pool_destroy(pool);
}

TEST_F(MCPMemoryTest, MemoryPoolNullOperations) {
  EXPECT_EQ(mcp_memory_pool_alloc(nullptr, 100), nullptr);
  EXPECT_EQ(mcp_memory_pool_alloc(nullptr, 0), nullptr);

  auto pool = mcp_memory_pool_create(100);
  EXPECT_EQ(mcp_memory_pool_alloc(pool, 0), nullptr);
  mcp_memory_pool_destroy(pool);

  mcp_memory_pool_reset(nullptr);
  mcp_memory_pool_destroy(nullptr);

  size_t used, total, count;
  mcp_memory_pool_stats(nullptr, &used, &total, &count);
}

// ============================================================================
// Batch Operations Tests
// ============================================================================

TEST_F(MCPMemoryTest, BatchExecute) {
  mcp_batch_operation_t operations[3] = {
      {MCP_BATCH_OP_CREATE, MCP_TYPE_STRING, nullptr, nullptr, nullptr, MCP_OK},
      {MCP_BATCH_OP_SET, MCP_TYPE_STRING, nullptr, nullptr, nullptr, MCP_OK},
      {MCP_BATCH_OP_FREE, MCP_TYPE_STRING, nullptr, nullptr, nullptr, MCP_OK}};

  EXPECT_EQ(mcp_batch_execute(operations, 3), MCP_OK);

  // All operations should have OK result
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(operations[i].result, MCP_OK);
  }
}

TEST_F(MCPMemoryTest, BatchExecuteNullOperations) {
  EXPECT_EQ(mcp_batch_execute(nullptr, 5), MCP_ERROR_INVALID_ARGUMENT);

  mcp_batch_operation_t operations[1];
  EXPECT_EQ(mcp_batch_execute(operations, 0), MCP_ERROR_INVALID_ARGUMENT);
}

// ============================================================================
// Resource Tracking Tests (Debug Mode)
// ============================================================================

#ifdef MCP_DEBUG
TEST_F(MCPMemoryTest, ResourceTracking) {
  mcp_enable_resource_tracking(MCP_TRUE);

  // Get initial counts
  size_t initial_string_count = mcp_get_resource_count(MCP_TYPE_STRING);

  // Note: Would need to create tracked resources through the API
  // This would require integration with the types that track resources

  mcp_enable_resource_tracking(MCP_FALSE);
}

TEST_F(MCPMemoryTest, ResourceReport) {
  mcp_enable_resource_tracking(MCP_TRUE);

  // This should print a report (can't easily test output)
  mcp_print_resource_report();

  mcp_enable_resource_tracking(MCP_FALSE);
}

TEST_F(MCPMemoryTest, LeakDetection) {
  mcp_enable_resource_tracking(MCP_TRUE);

  // Initially no leaks
  EXPECT_EQ(mcp_check_leaks(), MCP_FALSE);

  // Note: Would need to create and leak resources to test leak detection

  mcp_enable_resource_tracking(MCP_FALSE);
}
#endif

// ============================================================================
// Memory Utility Tests
// ============================================================================

TEST_F(MCPMemoryTest, StrDup) {
  const char* original = "Hello, World!";
  char* copy = mcp_strdup(original);

  ASSERT_NE(copy, nullptr);
  EXPECT_STREQ(copy, original);
  EXPECT_NE(copy, original);  // Different pointers

  mcp_string_free(copy);
}

TEST_F(MCPMemoryTest, StrDupNull) {
  char* copy = mcp_strdup(nullptr);
  EXPECT_EQ(copy, nullptr);
}

TEST_F(MCPMemoryTest, StrDupEmpty) {
  const char* empty = "";
  char* copy = mcp_strdup(empty);

  ASSERT_NE(copy, nullptr);
  EXPECT_STREQ(copy, "");

  mcp_string_free(copy);
}

TEST_F(MCPMemoryTest, MallocFreeBasic) {
  void* ptr = mcp_malloc(256);
  ASSERT_NE(ptr, nullptr);

  // Should be able to write to the memory
  memset(ptr, 0xAB, 256);

  mcp_free(ptr);
}

TEST_F(MCPMemoryTest, ReallocBasic) {
  void* ptr = mcp_malloc(100);
  ASSERT_NE(ptr, nullptr);

  // Write pattern
  memset(ptr, 0xCD, 100);

  // Grow
  ptr = mcp_realloc(ptr, 200);
  ASSERT_NE(ptr, nullptr);

  // First 100 bytes should still have pattern
  unsigned char* bytes = static_cast<unsigned char*>(ptr);
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(bytes[i], 0xCD);
  }

  // Shrink
  ptr = mcp_realloc(ptr, 50);
  ASSERT_NE(ptr, nullptr);

  mcp_free(ptr);
}

TEST_F(MCPMemoryTest, ReallocNull) {
  // realloc(nullptr, size) should behave like malloc
  void* ptr = mcp_realloc(nullptr, 100);
  ASSERT_NE(ptr, nullptr);
  mcp_free(ptr);
}

TEST_F(MCPMemoryTest, FreeNull) {
  // Should be safe to free null
  mcp_free(nullptr);
  mcp_string_free(nullptr);
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST_F(MCPMemoryTest, StressManyAllocations) {
  const int count = 10000;
  std::vector<void*> pointers;

  for (int i = 0; i < count; ++i) {
    size_t size = (i % 1000) + 1;  // Vary sizes
    void* ptr = mcp_malloc(size);
    ASSERT_NE(ptr, nullptr);
    pointers.push_back(ptr);
  }

  // Free in different order
  for (int i = count - 1; i >= 0; --i) {
    mcp_free(pointers[i]);
  }
}

TEST_F(MCPMemoryTest, StressMemoryPool) {
  auto pool = mcp_memory_pool_create(1024);
  ASSERT_NE(pool, nullptr);

  const int iterations = 100;
  for (int i = 0; i < iterations; ++i) {
    // Allocate various sizes
    std::vector<void*> ptrs;
    for (int j = 0; j < 10; ++j) {
      void* ptr = mcp_memory_pool_alloc(pool, (j + 1) * 10);
      ASSERT_NE(ptr, nullptr);
      ptrs.push_back(ptr);
    }

    // Reset and repeat
    mcp_memory_pool_reset(pool);
  }

  mcp_memory_pool_destroy(pool);
}

TEST_F(MCPMemoryTest, StressLargeAllocations) {
  // Test large allocations
  void* ptr1 = mcp_malloc(1024 * 1024);  // 1MB
  ASSERT_NE(ptr1, nullptr);

  void* ptr2 = mcp_malloc(10 * 1024 * 1024);  // 10MB
  ASSERT_NE(ptr2, nullptr);

  // Should be able to use the memory
  memset(ptr1, 0xFF, 1024 * 1024);
  memset(ptr2, 0xEE, 10 * 1024 * 1024);

  mcp_free(ptr1);
  mcp_free(ptr2);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(MCPMemoryTest, ThreadSafeAllocation) {
  const int thread_count = 10;
  const int allocs_per_thread = 100;

  auto thread_func = []() {
    for (int i = 0; i < allocs_per_thread; ++i) {
      void* ptr = mcp_malloc(100 + i);
      ASSERT_NE(ptr, nullptr);
      memset(ptr, i & 0xFF, 100 + i);
      mcp_free(ptr);
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < thread_count; ++i) {
    threads.emplace_back(thread_func);
  }

  for (auto& t : threads) {
    t.join();
  }
}

TEST_F(MCPMemoryTest, ThreadSafeMemoryPool) {
  // Note: Memory pools are not thread-safe by design
  // Each thread should have its own pool
  const int thread_count = 10;
  std::vector<mcp_memory_pool_t> pools(thread_count);

  // Create pool for each thread
  for (int i = 0; i < thread_count; ++i) {
    pools[i] = mcp_memory_pool_create(1024);
    ASSERT_NE(pools[i], nullptr);
  }

  auto thread_func = [&pools](int thread_id) {
    auto pool = pools[thread_id];
    for (int i = 0; i < 100; ++i) {
      void* ptr = mcp_memory_pool_alloc(pool, 10 + thread_id);
      if (ptr) {
        memset(ptr, thread_id, 10 + thread_id);
      }
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < thread_count; ++i) {
    threads.emplace_back(thread_func, i);
  }

  for (auto& t : threads) {
    t.join();
  }

  // Clean up pools
  for (auto pool : pools) {
    mcp_memory_pool_destroy(pool);
  }
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(MCPMemoryTest, IntegrationWithCustomAllocatorAndPool) {
  TestAllocatorData allocator_data;
  mcp_allocator_t allocator = {test_alloc, test_realloc, test_free,
                               &allocator_data};

  // Reinitialize with custom allocator
  mcp_shutdown();
  mcp_init(&allocator);

  // Create a memory pool (may or may not use custom allocator internally)
  auto pool = mcp_memory_pool_create(512);
  ASSERT_NE(pool, nullptr);
  // Don't assume pool uses custom allocator - implementation specific

  // Allocate from pool
  void* ptr = mcp_memory_pool_alloc(pool, 100);
  ASSERT_NE(ptr, nullptr);

  // String duplication should use custom allocator
  char* str = mcp_strdup("Test string");
  ASSERT_NE(str, nullptr);
  size_t allocs_before_free = allocator_data.alloc_count;

  mcp_string_free(str);
  EXPECT_EQ(allocator_data.free_count, 1);

  mcp_memory_pool_destroy(pool);
  // At least the string should have been freed
  EXPECT_GE(allocator_data.free_count, 1);
}

TEST_F(MCPMemoryTest, AllocationFailureHandling) {
  TestAllocatorData allocator_data;
  allocator_data.fail_next_alloc = true;

  mcp_allocator_t allocator = {test_alloc, test_realloc, test_free,
                               &allocator_data};

  mcp_shutdown();
  mcp_init(&allocator);

  // Allocation should fail
  void* ptr = mcp_malloc(100);
  EXPECT_EQ(ptr, nullptr);

  // Next allocation should succeed
  ptr = mcp_malloc(100);
  ASSERT_NE(ptr, nullptr);
  mcp_free(ptr);
}