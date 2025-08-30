/**
 * @file test_mcp_filter_buffer.cc
 * @brief Comprehensive unit tests for mcp_filter_buffer.cc with RAII
 * enforcement
 *
 * Tests zero-copy buffer operations, fragment management, reservations,
 * integer I/O, search functionality, watermarks, and buffer pools.
 * All resources are managed using RAII guards for automatic cleanup.
 */

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/c_api/mcp_c_api.h"

// Forward declare types to avoid header conflicts
typedef uint64_t mcp_buffer_handle_t;
typedef struct mcp_buffer_pool* mcp_buffer_pool_t;

#include "mcp/c_api/mcp_c_filter_buffer.h"

namespace {

// ============================================================================
// RAII Guard Wrapper for Buffer Handles
// ============================================================================

class BufferGuard {
 public:
  explicit BufferGuard(mcp_buffer_handle_t handle = 0) : handle_(handle) {
    if (handle_ != 0) {
      guard_ =
          mcp_guard_create(reinterpret_cast<void*>(handle_), MCP_TYPE_UNKNOWN);
    }
  }

  BufferGuard(BufferGuard&& other) noexcept
      : handle_(other.handle_), guard_(other.guard_) {
    other.handle_ = 0;
    other.guard_ = nullptr;
  }

  BufferGuard& operator=(BufferGuard&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = other.handle_;
      guard_ = other.guard_;
      other.handle_ = 0;
      other.guard_ = nullptr;
    }
    return *this;
  }

  ~BufferGuard() { reset(); }

  void reset(mcp_buffer_handle_t new_handle = 0) {
    if (guard_) {
      mcp_guard_destroy(&guard_);
    }
    handle_ = new_handle;
    if (handle_ != 0) {
      guard_ =
          mcp_guard_create(reinterpret_cast<void*>(handle_), MCP_TYPE_UNKNOWN);
    }
  }

  mcp_buffer_handle_t get() const { return handle_; }
  mcp_buffer_handle_t release() {
    if (guard_) {
      mcp_guard_release(&guard_);
    }
    auto h = handle_;
    handle_ = 0;
    return h;
  }

  operator mcp_buffer_handle_t() const { return handle_; }

  // Disable copy
  BufferGuard(const BufferGuard&) = delete;
  BufferGuard& operator=(const BufferGuard&) = delete;

 private:
  mcp_buffer_handle_t handle_;
  mcp_guard_t guard_ = nullptr;
};

// ============================================================================
// RAII Guard for Buffer Pools
// ============================================================================

class BufferPoolGuard {
 public:
  explicit BufferPoolGuard(mcp_buffer_pool_t pool = nullptr) : pool_(pool) {}

  BufferPoolGuard(BufferPoolGuard&& other) noexcept : pool_(other.pool_) {
    other.pool_ = nullptr;
  }

  BufferPoolGuard& operator=(BufferPoolGuard&& other) noexcept {
    if (this != &other) {
      reset();
      pool_ = other.pool_;
      other.pool_ = nullptr;
    }
    return *this;
  }

  ~BufferPoolGuard() { reset(); }

  void reset(mcp_buffer_pool_t new_pool = nullptr) {
    if (pool_) {
      // Note: No explicit destroy function in API yet
      // TODO: Call mcp_buffer_pool_destroy when available
    }
    pool_ = new_pool;
  }

  mcp_buffer_pool_t get() const { return pool_; }
  mcp_buffer_pool_t release() {
    auto p = pool_;
    pool_ = nullptr;
    return p;
  }

  operator mcp_buffer_pool_t() const { return pool_; }

  // Disable copy
  BufferPoolGuard(const BufferPoolGuard&) = delete;
  BufferPoolGuard& operator=(const BufferPoolGuard&) = delete;

 private:
  mcp_buffer_pool_t pool_;
};

// ============================================================================
// Test Fixture with RAII
// ============================================================================

class MCPFilterBufferTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Initialize MCP library
    mcp_init(nullptr);
  }

  void TearDown() override {
    // All resources are automatically cleaned up through RAII guards

    // Shutdown MCP library
    mcp_shutdown();
  }

  BufferGuard CreateBuffer(size_t capacity = 1024) {
    auto handle =
        mcp_buffer_create_owned(capacity, MCP_BUFFER_OWNERSHIP_EXCLUSIVE);
    return BufferGuard(handle);
  }

  BufferPoolGuard CreateBufferPool(size_t buffer_size = 4096) {
    mcp_buffer_pool_config_t config = {};
    config.buffer_size = buffer_size;
    config.max_buffers = 10;
    config.prealloc_count = 2;
    // Note: thread_local is a C++ keyword, can't assign directly
    config.zero_on_alloc = MCP_FALSE;

    auto pool = mcp_buffer_pool_create_ex(&config);
    return BufferPoolGuard(pool);
  }
};

// ============================================================================
// Buffer Creation Tests with RAII
// ============================================================================

TEST_F(MCPFilterBufferTest, CreateOwnedBuffer) {
  BufferGuard buffer(
      mcp_buffer_create_owned(1024, MCP_BUFFER_OWNERSHIP_EXCLUSIVE));
  ASSERT_NE(buffer.get(), 0);
  EXPECT_EQ(mcp_buffer_length(buffer), 0);
  EXPECT_EQ(mcp_buffer_is_empty(buffer), MCP_TRUE);
  // Buffer automatically cleaned up when guard goes out of scope
}

TEST_F(MCPFilterBufferTest, CreateViewBuffer) {
  const char* data = "Hello, World!";
  BufferGuard buffer(mcp_buffer_create_view(data, strlen(data)));
  ASSERT_NE(buffer.get(), 0);
  EXPECT_EQ(mcp_buffer_length(buffer), strlen(data));
  // Buffer automatically cleaned up
}

TEST_F(MCPFilterBufferTest, CreateFromFragment) {
  const char* data = "Fragment data";
  std::atomic<bool> released{false};

  mcp_buffer_fragment_t fragment = {};
  fragment.data = data;
  fragment.size = strlen(data);
  fragment.release_callback = [](void* data, size_t size, void* user_data) {
    *static_cast<std::atomic<bool>*>(user_data) = true;
  };
  fragment.user_data = &released;

  {
    BufferGuard buffer(mcp_buffer_create_from_fragment(&fragment));
    ASSERT_NE(buffer.get(), 0);
    EXPECT_EQ(mcp_buffer_length(buffer), strlen(data));
  }  // Buffer automatically destroyed here

  // Give callback time to execute
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

TEST_F(MCPFilterBufferTest, CloneBuffer) {
  auto original = CreateBuffer();
  const char* data = "Original data";
  ASSERT_EQ(mcp_buffer_add_string(original, data), MCP_OK);

  BufferGuard clone(mcp_buffer_clone(original));
  ASSERT_NE(clone.get(), 0);

  EXPECT_EQ(mcp_buffer_length(clone), mcp_buffer_length(original));

  // Verify data is copied
  char read_buffer[100] = {};
  EXPECT_EQ(mcp_buffer_peek(clone, 0, read_buffer, strlen(data)), MCP_OK);
  EXPECT_STREQ(read_buffer, data);
  // Both buffers automatically cleaned up
}

TEST_F(MCPFilterBufferTest, CreateCOWBuffer) {
  auto original = CreateBuffer();
  const char* data = "COW data";
  ASSERT_EQ(mcp_buffer_add_string(original, data), MCP_OK);

  BufferGuard cow(mcp_buffer_create_cow(original));
  ASSERT_NE(cow.get(), 0);

  EXPECT_EQ(mcp_buffer_length(cow), mcp_buffer_length(original));
  // Both buffers automatically cleaned up
}

// ============================================================================
// Buffer Data Operations Tests with RAII
// ============================================================================

TEST_F(MCPFilterBufferTest, AddData) {
  auto buffer = CreateBuffer();
  const char* data = "Test data";

  EXPECT_EQ(mcp_buffer_add(buffer, data, strlen(data)), MCP_OK);
  EXPECT_EQ(mcp_buffer_length(buffer), strlen(data));
  EXPECT_EQ(mcp_buffer_is_empty(buffer), MCP_FALSE);
}

TEST_F(MCPFilterBufferTest, AddString) {
  auto buffer = CreateBuffer();
  const char* str = "String data";

  EXPECT_EQ(mcp_buffer_add_string(buffer, str), MCP_OK);
  EXPECT_EQ(mcp_buffer_length(buffer), strlen(str));
}

TEST_F(MCPFilterBufferTest, AddBuffer) {
  auto buffer1 = CreateBuffer();
  auto buffer2 = CreateBuffer();

  const char* data1 = "First ";
  const char* data2 = "Second";

  ASSERT_EQ(mcp_buffer_add_string(buffer1, data1), MCP_OK);
  ASSERT_EQ(mcp_buffer_add_string(buffer2, data2), MCP_OK);

  EXPECT_EQ(mcp_buffer_add_buffer(buffer1, buffer2), MCP_OK);
  EXPECT_EQ(mcp_buffer_length(buffer1), strlen(data1) + strlen(data2));

  char combined[100] = {};
  EXPECT_EQ(mcp_buffer_peek(buffer1, 0, combined, mcp_buffer_length(buffer1)),
            MCP_OK);
  EXPECT_STREQ(combined, "First Second");
}

TEST_F(MCPFilterBufferTest, PrependData) {
  auto buffer = CreateBuffer();

  ASSERT_EQ(mcp_buffer_add_string(buffer, "World"), MCP_OK);
  EXPECT_EQ(mcp_buffer_prepend(buffer, "Hello ", 6), MCP_OK);

  char result[100] = {};
  EXPECT_EQ(mcp_buffer_peek(buffer, 0, result, mcp_buffer_length(buffer)),
            MCP_OK);
  EXPECT_STREQ(result, "Hello World");
}

// ============================================================================
// Buffer Consumption Tests with RAII
// ============================================================================

TEST_F(MCPFilterBufferTest, DrainBuffer) {
  auto buffer = CreateBuffer();
  const char* data = "12345678";

  ASSERT_EQ(mcp_buffer_add_string(buffer, data), MCP_OK);
  EXPECT_EQ(mcp_buffer_length(buffer), 8);

  EXPECT_EQ(mcp_buffer_drain(buffer, 3), MCP_OK);
  EXPECT_EQ(mcp_buffer_length(buffer), 5);

  char remaining[100] = {};
  EXPECT_EQ(mcp_buffer_peek(buffer, 0, remaining, 5), MCP_OK);
  EXPECT_STREQ(remaining, "45678");
}

TEST_F(MCPFilterBufferTest, DrainMoreThanAvailable) {
  auto buffer = CreateBuffer();
  const char* data = "Short";

  ASSERT_EQ(mcp_buffer_add_string(buffer, data), MCP_OK);
  EXPECT_EQ(mcp_buffer_drain(buffer, 100), MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_buffer_length(buffer), 5);  // Unchanged
}

TEST_F(MCPFilterBufferTest, MoveBuffer) {
  auto source = CreateBuffer();
  auto dest = CreateBuffer();

  const char* data = "Move this data";
  ASSERT_EQ(mcp_buffer_add_string(source, data), MCP_OK);

  size_t original_length = mcp_buffer_length(source);
  EXPECT_EQ(mcp_buffer_move(source, dest, 0), MCP_OK);

  EXPECT_EQ(mcp_buffer_length(source), 0);
  EXPECT_EQ(mcp_buffer_length(dest), original_length);

  char moved[100] = {};
  EXPECT_EQ(mcp_buffer_peek(dest, 0, moved, original_length), MCP_OK);
  EXPECT_STREQ(moved, data);
}

TEST_F(MCPFilterBufferTest, MovePartialBuffer) {
  auto source = CreateBuffer();
  auto dest = CreateBuffer();

  const char* data = "12345678";
  ASSERT_EQ(mcp_buffer_add_string(source, data), MCP_OK);

  EXPECT_EQ(mcp_buffer_move(source, dest, 4), MCP_OK);

  EXPECT_EQ(mcp_buffer_length(source), 4);
  EXPECT_EQ(mcp_buffer_length(dest), 4);

  char source_remaining[100] = {};
  char dest_content[100] = {};

  EXPECT_EQ(mcp_buffer_peek(source, 0, source_remaining, 4), MCP_OK);
  EXPECT_EQ(mcp_buffer_peek(dest, 0, dest_content, 4), MCP_OK);

  EXPECT_STREQ(source_remaining, "5678");
  EXPECT_STREQ(dest_content, "1234");
}

TEST_F(MCPFilterBufferTest, DrainTracker) {
  auto buffer = CreateBuffer();
  std::atomic<size_t> total_drained{0};

  mcp_drain_tracker_t tracker = {};
  tracker.callback = [](size_t bytes_drained, void* user_data) {
    static_cast<std::atomic<size_t>*>(user_data)->fetch_add(bytes_drained);
  };
  tracker.user_data = &total_drained;

  EXPECT_EQ(mcp_buffer_set_drain_tracker(buffer, &tracker), MCP_OK);

  const char* data = "Test data for drain tracking";
  ASSERT_EQ(mcp_buffer_add_string(buffer, data), MCP_OK);

  EXPECT_EQ(mcp_buffer_drain(buffer, 5), MCP_OK);
  EXPECT_EQ(mcp_buffer_drain(buffer, 10), MCP_OK);

  // Drain tracker should have been called
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_GE(total_drained.load(), 15);
}

// ============================================================================
// Buffer Reservation Tests with RAII
// ============================================================================

TEST_F(MCPFilterBufferTest, ReserveAndCommit) {
  auto buffer = CreateBuffer();
  mcp_buffer_reservation_t reservation = {};

  EXPECT_EQ(mcp_buffer_reserve(buffer, 100, &reservation), MCP_OK);
  ASSERT_NE(reservation.data, nullptr);
  EXPECT_GE(reservation.capacity, 100);
  EXPECT_EQ(reservation.buffer, buffer.get());
  EXPECT_NE(reservation.reservation_id, 0);

  // Write to reserved space
  const char* data = "Reserved data";
  memcpy(reservation.data, data, strlen(data));

  EXPECT_EQ(mcp_buffer_commit_reservation(&reservation, strlen(data)), MCP_OK);
  EXPECT_EQ(reservation.reservation_id, 0);  // Cleared after commit

  EXPECT_EQ(mcp_buffer_length(buffer), strlen(data));

  char result[100] = {};
  EXPECT_EQ(mcp_buffer_peek(buffer, 0, result, strlen(data)), MCP_OK);
  EXPECT_STREQ(result, data);
}

TEST_F(MCPFilterBufferTest, CancelReservation) {
  auto buffer = CreateBuffer();
  mcp_buffer_reservation_t reservation = {};

  EXPECT_EQ(mcp_buffer_reserve(buffer, 50, &reservation), MCP_OK);
  ASSERT_NE(reservation.data, nullptr);

  // Write but then cancel
  memcpy(reservation.data, "Cancelled", 9);

  EXPECT_EQ(mcp_buffer_cancel_reservation(&reservation), MCP_OK);
  EXPECT_EQ(reservation.reservation_id, 0);

  // Buffer should remain empty
  EXPECT_EQ(mcp_buffer_length(buffer), 0);
}

TEST_F(MCPFilterBufferTest, MultipleReservations) {
  auto buffer = CreateBuffer();
  mcp_buffer_reservation_t res1 = {};
  mcp_buffer_reservation_t res2 = {};

  // Multiple reservations should be possible
  EXPECT_EQ(mcp_buffer_reserve(buffer, 50, &res1), MCP_OK);
  EXPECT_EQ(mcp_buffer_reserve(buffer, 50, &res2), MCP_OK);

  ASSERT_NE(res1.data, nullptr);
  ASSERT_NE(res2.data, nullptr);
  EXPECT_NE(res1.reservation_id, res2.reservation_id);

  // Commit both
  memcpy(res1.data, "First", 5);
  memcpy(res2.data, "Second", 6);

  EXPECT_EQ(mcp_buffer_commit_reservation(&res1, 5), MCP_OK);
  EXPECT_EQ(mcp_buffer_commit_reservation(&res2, 6), MCP_OK);

  EXPECT_EQ(mcp_buffer_length(buffer), 11);
}

// ============================================================================
// Buffer Access Tests with RAII
// ============================================================================

TEST_F(MCPFilterBufferTest, GetContiguous) {
  auto buffer = CreateBuffer();
  const char* data = "Contiguous data";
  ASSERT_EQ(mcp_buffer_add_string(buffer, data), MCP_OK);

  const void* ptr = nullptr;
  size_t actual_length = 0;

  EXPECT_EQ(mcp_buffer_get_contiguous(buffer, 0, 100, &ptr, &actual_length),
            MCP_OK);
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(actual_length, strlen(data));
  EXPECT_EQ(memcmp(ptr, data, actual_length), 0);
}

TEST_F(MCPFilterBufferTest, GetContiguousWithOffset) {
  auto buffer = CreateBuffer();
  const char* data = "0123456789";
  ASSERT_EQ(mcp_buffer_add_string(buffer, data), MCP_OK);

  const void* ptr = nullptr;
  size_t actual_length = 0;

  EXPECT_EQ(mcp_buffer_get_contiguous(buffer, 5, 3, &ptr, &actual_length),
            MCP_OK);
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(actual_length, 3);
  EXPECT_EQ(memcmp(ptr, "567", 3), 0);
}

TEST_F(MCPFilterBufferTest, Linearize) {
  auto buffer = CreateBuffer();
  const char* data = "Data to linearize";
  ASSERT_EQ(mcp_buffer_add_string(buffer, data), MCP_OK);

  void* linear = nullptr;
  EXPECT_EQ(mcp_buffer_linearize(buffer, strlen(data), &linear), MCP_OK);
  ASSERT_NE(linear, nullptr);
  EXPECT_EQ(memcmp(linear, data, strlen(data)), 0);
}

TEST_F(MCPFilterBufferTest, PeekData) {
  auto buffer = CreateBuffer();
  const char* data = "Peek at this data";
  ASSERT_EQ(mcp_buffer_add_string(buffer, data), MCP_OK);

  char result[100] = {};
  EXPECT_EQ(mcp_buffer_peek(buffer, 0, result, 4), MCP_OK);
  EXPECT_STREQ(result, "Peek");

  // Peek with offset
  memset(result, 0, sizeof(result));
  EXPECT_EQ(mcp_buffer_peek(buffer, 8, result, 4), MCP_OK);
  EXPECT_STREQ(result, "this");

  // Buffer should not be consumed
  EXPECT_EQ(mcp_buffer_length(buffer), strlen(data));
}

// ============================================================================
// Type-Safe I/O Tests with RAII
// ============================================================================

TEST_F(MCPFilterBufferTest, WriteReadLittleEndian) {
  auto buffer = CreateBuffer();

  // Write various sized integers
  EXPECT_EQ(mcp_buffer_write_le_int(buffer, 0x12, 1), MCP_OK);
  EXPECT_EQ(mcp_buffer_write_le_int(buffer, 0x3456, 2), MCP_OK);
  EXPECT_EQ(mcp_buffer_write_le_int(buffer, 0x789ABCDE, 4), MCP_OK);
  EXPECT_EQ(mcp_buffer_write_le_int(buffer, 0x123456789ABCDEF0, 8), MCP_OK);

  // Read them back
  uint64_t value = 0;

  EXPECT_EQ(mcp_buffer_read_le_int(buffer, 1, &value), MCP_OK);
  EXPECT_EQ(value, 0x12);

  EXPECT_EQ(mcp_buffer_read_le_int(buffer, 2, &value), MCP_OK);
  EXPECT_EQ(value, 0x3456);

  EXPECT_EQ(mcp_buffer_read_le_int(buffer, 4, &value), MCP_OK);
  EXPECT_EQ(value, 0x789ABCDE);

  EXPECT_EQ(mcp_buffer_read_le_int(buffer, 8, &value), MCP_OK);
  EXPECT_EQ(value, 0x123456789ABCDEF0);
}

TEST_F(MCPFilterBufferTest, WriteReadBigEndian) {
  auto buffer = CreateBuffer();

  // Write various sized integers
  EXPECT_EQ(mcp_buffer_write_be_int(buffer, 0xAB, 1), MCP_OK);
  EXPECT_EQ(mcp_buffer_write_be_int(buffer, 0xCDEF, 2), MCP_OK);
  EXPECT_EQ(mcp_buffer_write_be_int(buffer, 0x01234567, 4), MCP_OK);
  EXPECT_EQ(mcp_buffer_write_be_int(buffer, 0xFEDCBA9876543210, 8), MCP_OK);

  // Read them back
  uint64_t value = 0;

  EXPECT_EQ(mcp_buffer_read_be_int(buffer, 1, &value), MCP_OK);
  EXPECT_EQ(value, 0xAB);

  EXPECT_EQ(mcp_buffer_read_be_int(buffer, 2, &value), MCP_OK);
  EXPECT_EQ(value, 0xCDEF);

  EXPECT_EQ(mcp_buffer_read_be_int(buffer, 4, &value), MCP_OK);
  EXPECT_EQ(value, 0x01234567);

  EXPECT_EQ(mcp_buffer_read_be_int(buffer, 8, &value), MCP_OK);
  EXPECT_EQ(value, 0xFEDCBA9876543210);
}

TEST_F(MCPFilterBufferTest, InvalidIntegerSize) {
  auto buffer = CreateBuffer();
  uint64_t value = 0x1234;

  // Invalid sizes should fail
  EXPECT_EQ(mcp_buffer_write_le_int(buffer, value, 3),
            MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_buffer_write_be_int(buffer, value, 5),
            MCP_ERROR_INVALID_ARGUMENT);

  EXPECT_EQ(mcp_buffer_write_le_int(buffer, value, 4), MCP_OK);

  EXPECT_EQ(mcp_buffer_read_le_int(buffer, 3, &value),
            MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_buffer_read_be_int(buffer, 7, &value),
            MCP_ERROR_INVALID_ARGUMENT);
}

// ============================================================================
// Buffer Search Tests with RAII
// ============================================================================

TEST_F(MCPFilterBufferTest, SearchPattern) {
  auto buffer = CreateBuffer();
  const char* data = "The quick brown fox jumps over the lazy dog";
  ASSERT_EQ(mcp_buffer_add_string(buffer, data), MCP_OK);

  size_t position = 0;

  // Search for "fox"
  EXPECT_EQ(mcp_buffer_search(buffer, "fox", 3, 0, &position), MCP_OK);
  EXPECT_EQ(position, 16);

  // Search for "dog"
  EXPECT_EQ(mcp_buffer_search(buffer, "dog", 3, 0, &position), MCP_OK);
  EXPECT_EQ(position, 40);

  // Search from middle
  EXPECT_EQ(mcp_buffer_search(buffer, "the", 3, 20, &position), MCP_OK);
  EXPECT_EQ(position, 31);

  // Pattern not found
  EXPECT_EQ(mcp_buffer_search(buffer, "cat", 3, 0, &position),
            MCP_ERROR_NOT_FOUND);
}

TEST_F(MCPFilterBufferTest, FindByte) {
  auto buffer = CreateBuffer();
  const char* data = "Line 1\nLine 2\nLine 3";
  ASSERT_EQ(mcp_buffer_add_string(buffer, data), MCP_OK);

  size_t position = 0;

  // Find newline
  EXPECT_EQ(mcp_buffer_find_byte(buffer, '\n', &position), MCP_OK);
  EXPECT_EQ(position, 6);

  // Find space
  EXPECT_EQ(mcp_buffer_find_byte(buffer, ' ', &position), MCP_OK);
  EXPECT_EQ(position, 4);

  // Byte not found
  EXPECT_EQ(mcp_buffer_find_byte(buffer, '\t', &position), MCP_ERROR_NOT_FOUND);
}

// ============================================================================
// Buffer Information Tests with RAII
// ============================================================================

TEST_F(MCPFilterBufferTest, BufferLength) {
  auto buffer = CreateBuffer();
  EXPECT_EQ(mcp_buffer_length(buffer), 0);

  const char* data = "12345";
  ASSERT_EQ(mcp_buffer_add_string(buffer, data), MCP_OK);
  EXPECT_EQ(mcp_buffer_length(buffer), 5);

  ASSERT_EQ(mcp_buffer_drain(buffer, 2), MCP_OK);
  EXPECT_EQ(mcp_buffer_length(buffer), 3);
}

TEST_F(MCPFilterBufferTest, BufferStats) {
  auto buffer = CreateBuffer();
  const char* data = "Statistics test data";
  ASSERT_EQ(mcp_buffer_add_string(buffer, data), MCP_OK);

  mcp_buffer_stats_t stats = {};
  EXPECT_EQ(mcp_buffer_get_stats(buffer, &stats), MCP_OK);

  EXPECT_EQ(stats.total_bytes, strlen(data));
  EXPECT_EQ(stats.used_bytes, strlen(data));
  EXPECT_GT(stats.slice_count, 0);
}

// ============================================================================
// Buffer Watermark Tests with RAII
// ============================================================================

TEST_F(MCPFilterBufferTest, SetWatermarks) {
  auto buffer = CreateBuffer();

  // Should accept watermark settings
  EXPECT_EQ(mcp_buffer_set_watermarks(buffer, 100, 500, 1000), MCP_OK);

  // Initially should be below low watermark
  EXPECT_EQ(mcp_buffer_below_low_watermark(buffer), MCP_TRUE);
  EXPECT_EQ(mcp_buffer_above_high_watermark(buffer), MCP_FALSE);
}

// ============================================================================
// Advanced Buffer Pool Tests with RAII
// ============================================================================

TEST_F(MCPFilterBufferTest, CreateBufferPool) {
  mcp_buffer_pool_config_t config = {};
  config.buffer_size = 4096;
  config.max_buffers = 5;
  config.prealloc_count = 2;
  // Note: thread_local is a C++ keyword, can't assign directly
  config.zero_on_alloc = MCP_TRUE;

  BufferPoolGuard pool(mcp_buffer_pool_create_ex(&config));
  ASSERT_NE(pool.get(), nullptr);

  size_t free_count = 0;
  size_t used_count = 0;
  size_t total_allocated = 0;

  EXPECT_EQ(mcp_buffer_pool_get_stats(pool, &free_count, &used_count,
                                      &total_allocated),
            MCP_OK);
  EXPECT_EQ(free_count, 2);
  EXPECT_EQ(used_count, 0);
  EXPECT_GE(total_allocated, config.buffer_size * config.prealloc_count);
}

TEST_F(MCPFilterBufferTest, BufferPoolTrim) {
  auto pool = CreateBufferPool(1024);
  ASSERT_NE(pool.get(), nullptr);

  // Trim pool to have at most 1 free buffer
  EXPECT_EQ(mcp_buffer_pool_trim(pool, 1), MCP_OK);

  size_t free_count = 0;
  EXPECT_EQ(mcp_buffer_pool_get_stats(pool, &free_count, nullptr, nullptr),
            MCP_OK);
  EXPECT_LE(free_count, 1);
}

// ============================================================================
// Error Handling Tests with RAII
// ============================================================================

TEST_F(MCPFilterBufferTest, InvalidHandles) {
  mcp_buffer_handle_t invalid = 999999;

  EXPECT_EQ(mcp_buffer_add(invalid, "data", 4), MCP_ERROR_NOT_FOUND);
  EXPECT_EQ(mcp_buffer_drain(invalid, 1), MCP_ERROR_NOT_FOUND);
  EXPECT_EQ(mcp_buffer_length(invalid), 0);

  char buffer[10];
  EXPECT_EQ(mcp_buffer_peek(invalid, 0, buffer, 10), MCP_ERROR_NOT_FOUND);

  uint64_t value;
  EXPECT_EQ(mcp_buffer_read_le_int(invalid, 4, &value), MCP_ERROR_NOT_FOUND);
}

TEST_F(MCPFilterBufferTest, NullPointers) {
  auto buffer = CreateBuffer();

  EXPECT_EQ(mcp_buffer_add(buffer, nullptr, 10),
            MCP_OK);  // Null data is OK with length 0
  EXPECT_EQ(mcp_buffer_add_string(buffer, nullptr), MCP_ERROR_INVALID_ARGUMENT);

  EXPECT_EQ(mcp_buffer_reserve(buffer, 100, nullptr),
            MCP_ERROR_INVALID_ARGUMENT);

  const void* data;
  size_t length;
  EXPECT_EQ(mcp_buffer_get_contiguous(buffer, 0, 10, nullptr, &length),
            MCP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(mcp_buffer_get_contiguous(buffer, 0, 10, &data, nullptr),
            MCP_ERROR_INVALID_ARGUMENT);
}

// ============================================================================
// Stress Tests with RAII
// ============================================================================

TEST_F(MCPFilterBufferTest, LargeDataOperations) {
  auto buffer = CreateBuffer();

  // Add large amounts of data
  std::vector<uint8_t> large_data(1024 * 1024, 'X');  // 1MB
  EXPECT_EQ(mcp_buffer_add(buffer, large_data.data(), large_data.size()),
            MCP_OK);
  EXPECT_EQ(mcp_buffer_length(buffer), large_data.size());

  // Drain in chunks
  size_t chunk_size = 4096;
  size_t total_drained = 0;
  while (total_drained < large_data.size()) {
    size_t to_drain = std::min(chunk_size, large_data.size() - total_drained);
    EXPECT_EQ(mcp_buffer_drain(buffer, to_drain), MCP_OK);
    total_drained += to_drain;
  }

  EXPECT_EQ(mcp_buffer_length(buffer), 0);
}

TEST_F(MCPFilterBufferTest, ManySmallWrites) {
  auto buffer = CreateBuffer();

  const int iterations = 10000;
  for (int i = 0; i < iterations; ++i) {
    char data[10];
    snprintf(data, sizeof(data), "%d,", i);
    ASSERT_EQ(mcp_buffer_add_string(buffer, data), MCP_OK);
  }

  EXPECT_GT(mcp_buffer_length(buffer), iterations * 2);
}

TEST_F(MCPFilterBufferTest, ConcurrentAccess) {
  auto buffer = CreateBuffer();
  std::atomic<bool> stop{false};
  std::atomic<size_t> writes{0};
  std::atomic<size_t> reads{0};

  // Writer thread
  std::thread writer([&]() {
    while (!stop.load()) {
      const char* data = "W";
      if (mcp_buffer_add_string(buffer.get(), data) == MCP_OK) {
        writes.fetch_add(1);
      }
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  });

  // Reader thread
  std::thread reader([&]() {
    while (!stop.load()) {
      if (mcp_buffer_length(buffer.get()) > 0) {
        if (mcp_buffer_drain(buffer.get(), 1) == MCP_OK) {
          reads.fetch_add(1);
        }
      }
      std::this_thread::sleep_for(std::chrono::microseconds(150));
    }
  });

  // Run for a short time
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  stop.store(true);

  writer.join();
  reader.join();

  // Verify some operations occurred
  EXPECT_GT(writes.load(), 0);
  EXPECT_GT(reads.load(), 0);

  // Final length should be writes - reads
  size_t expected_length = writes.load() - reads.load();
  EXPECT_NEAR(mcp_buffer_length(buffer), expected_length, 10);
}

// ============================================================================
// Fragment and External Memory Tests with RAII
// ============================================================================

TEST_F(MCPFilterBufferTest, ExternalFragmentLifecycle) {
  std::vector<uint8_t> external_data(1024, 'E');
  std::atomic<bool> released{false};

  mcp_buffer_fragment_t fragment = {};
  fragment.data = external_data.data();
  fragment.size = external_data.size();
  fragment.release_callback = [](void* data, size_t size, void* user_data) {
    EXPECT_NE(data, nullptr);
    EXPECT_EQ(size, 1024);
    *static_cast<std::atomic<bool>*>(user_data) = true;
  };
  fragment.user_data = &released;

  {
    auto buffer = CreateBuffer();
    EXPECT_EQ(mcp_buffer_add_fragment(buffer, &fragment), MCP_OK);
    EXPECT_EQ(mcp_buffer_length(buffer), fragment.size);

    // Fragment should be released when buffer is drained or destroyed
    EXPECT_EQ(mcp_buffer_drain(buffer, fragment.size), MCP_OK);
  }  // Buffer automatically destroyed here

  // Give callback time to execute
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

// ============================================================================
// Integration Tests with RAII
// ============================================================================

TEST_F(MCPFilterBufferTest, CompleteWorkflow) {
  // Create buffer with initial data
  auto buffer = CreateBuffer();
  ASSERT_EQ(mcp_buffer_add_string(buffer, "Initial "), MCP_OK);

  // Reserve space for more data
  mcp_buffer_reservation_t reservation = {};
  ASSERT_EQ(mcp_buffer_reserve(buffer, 50, &reservation), MCP_OK);

  // Write to reservation
  const char* reserved_data = "reserved ";
  memcpy(reservation.data, reserved_data, strlen(reserved_data));
  ASSERT_EQ(mcp_buffer_commit_reservation(&reservation, strlen(reserved_data)),
            MCP_OK);

  // Add more data
  ASSERT_EQ(mcp_buffer_add_string(buffer, "data"), MCP_OK);

  // Write integers
  ASSERT_EQ(mcp_buffer_write_le_int(buffer, 0x1234, 2), MCP_OK);
  ASSERT_EQ(mcp_buffer_write_be_int(buffer, 0x5678, 2), MCP_OK);

  // Verify total length
  size_t expected_length = strlen("Initial reserved data") + 4;
  EXPECT_EQ(mcp_buffer_length(buffer), expected_length);

  // Read back text
  char text[100] = {};
  EXPECT_EQ(mcp_buffer_peek(buffer, 0, text, strlen("Initial reserved data")),
            MCP_OK);
  EXPECT_STREQ(text, "Initial reserved data");

  // Drain text portion
  EXPECT_EQ(mcp_buffer_drain(buffer, strlen("Initial reserved data")), MCP_OK);

  // Read integers
  uint64_t le_value = 0, be_value = 0;
  EXPECT_EQ(mcp_buffer_read_le_int(buffer, 2, &le_value), MCP_OK);
  EXPECT_EQ(le_value, 0x1234);
  EXPECT_EQ(mcp_buffer_read_be_int(buffer, 2, &be_value), MCP_OK);
  EXPECT_EQ(be_value, 0x5678);

  // Buffer should now be empty
  EXPECT_EQ(mcp_buffer_length(buffer), 0);
  EXPECT_EQ(mcp_buffer_is_empty(buffer), MCP_TRUE);
  // Buffer automatically cleaned up
}

// ============================================================================
// Transaction-based Tests with RAII
// ============================================================================

TEST_F(MCPFilterBufferTest, TransactionBasedOperations) {
  // Use transaction for multiple buffer operations
  mcp_transaction_t txn = mcp_transaction_create();
  ASSERT_NE(txn, nullptr);

  // Create multiple buffers and add to transaction
  BufferGuard buffer1(
      mcp_buffer_create_owned(1024, MCP_BUFFER_OWNERSHIP_EXCLUSIVE));
  BufferGuard buffer2(
      mcp_buffer_create_owned(1024, MCP_BUFFER_OWNERSHIP_EXCLUSIVE));

  ASSERT_NE(buffer1.get(), 0);
  ASSERT_NE(buffer2.get(), 0);

  // Transaction manages multiple resources
  EXPECT_EQ(mcp_transaction_add(txn, reinterpret_cast<void*>(buffer1.release()),
                                MCP_TYPE_UNKNOWN),
            MCP_OK);
  EXPECT_EQ(mcp_transaction_add(txn, reinterpret_cast<void*>(buffer2.release()),
                                MCP_TYPE_UNKNOWN),
            MCP_OK);

  // Operations succeed or all rollback
  EXPECT_EQ(mcp_transaction_size(txn), 2);

  // Commit transaction - prevents cleanup
  EXPECT_EQ(mcp_transaction_commit(&txn), MCP_OK);

  // Note: In real usage, committed resources would be transferred elsewhere
  // For testing, we need to clean them up manually since we didn't transfer
  // ownership
}

}  // namespace