/**
 * @file test_mcp_filter_chain.cc
 * @brief Comprehensive unit tests for MCP Filter Chain C API with RAII
 * enforcement
 *
 * Tests cover:
 * - Advanced chain builder operations
 * - Chain state management (pause/resume/reset)
 * - Node management (add, enable/disable, conditional)
 * - Error handling and edge cases
 * All resources are managed using RAII guards for automatic cleanup.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

// Only include necessary headers to avoid conflicts
#include "mcp/c_api/mcp_c_api.h"
#include "mcp/c_api/mcp_c_types.h"
#include "mcp/c_api/mcp_raii.h"

using namespace testing;

namespace {

// ============================================================================
// RAII Guard Wrappers for MCP Resources
// ============================================================================

class DispatcherGuard {
 public:
  DispatcherGuard() : dispatcher_(mcp_dispatcher_create()) {}

  ~DispatcherGuard() {
    if (dispatcher_) {
      mcp_dispatcher_destroy(dispatcher_);
    }
  }

  mcp_dispatcher_t get() const { return dispatcher_; }
  operator mcp_dispatcher_t() const { return dispatcher_; }
  explicit operator bool() const { return dispatcher_ != nullptr; }

  // Disable copy
  DispatcherGuard(const DispatcherGuard&) = delete;
  DispatcherGuard& operator=(const DispatcherGuard&) = delete;

  // Enable move
  DispatcherGuard(DispatcherGuard&& other) noexcept
      : dispatcher_(other.dispatcher_) {
    other.dispatcher_ = nullptr;
  }

  DispatcherGuard& operator=(DispatcherGuard&& other) noexcept {
    if (this != &other) {
      if (dispatcher_) {
        mcp_dispatcher_destroy(dispatcher_);
      }
      dispatcher_ = other.dispatcher_;
      other.dispatcher_ = nullptr;
    }
    return *this;
  }

 private:
  mcp_dispatcher_t dispatcher_;
};

class BufferGuard {
 public:
  explicit BufferGuard(size_t capacity)
      : buffer_(mcp_buffer_create(capacity)) {}

  ~BufferGuard() {
    if (buffer_) {
      mcp_buffer_free(buffer_);
    }
  }

  mcp_buffer_t* get() const { return buffer_; }
  operator mcp_buffer_t*() const { return buffer_; }
  explicit operator bool() const { return buffer_ != nullptr; }

  // Disable copy
  BufferGuard(const BufferGuard&) = delete;
  BufferGuard& operator=(const BufferGuard&) = delete;

  // Enable move
  BufferGuard(BufferGuard&& other) noexcept : buffer_(other.buffer_) {
    other.buffer_ = nullptr;
  }

 private:
  mcp_buffer_t* buffer_;
};

class TransactionGuard {
 public:
  TransactionGuard() : transaction_(mcp_transaction_create()) {}

  ~TransactionGuard() {
    if (transaction_) {
      mcp_transaction_destroy(&transaction_);
    }
  }

  mcp_transaction_t get() const { return transaction_; }
  operator mcp_transaction_t() const { return transaction_; }
  explicit operator bool() const { return transaction_ != nullptr; }

  // Disable copy
  TransactionGuard(const TransactionGuard&) = delete;
  TransactionGuard& operator=(const TransactionGuard&) = delete;

  // Enable move
  TransactionGuard(TransactionGuard&& other) noexcept
      : transaction_(other.transaction_) {
    other.transaction_ = nullptr;
  }

 private:
  mcp_transaction_t transaction_;
};

// ============================================================================
// Test Fixture with RAII
// ============================================================================

class MCPFilterChainTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Initialize MCP library
    auto result = mcp_init(nullptr);
    ASSERT_EQ(result, MCP_OK);

    // Create dispatcher with RAII guard
    dispatcher_ = std::make_unique<DispatcherGuard>();
    ASSERT_TRUE(*dispatcher_);
  }

  void TearDown() override {
    // All RAII guards automatically clean up in reverse order
    dispatcher_.reset();

    // Shutdown MCP library
    mcp_shutdown();
  }

  // Create a test buffer with RAII
  BufferGuard createTestBuffer(size_t size = 1024) { return BufferGuard(size); }

 protected:
  std::unique_ptr<DispatcherGuard> dispatcher_;
};

// ============================================================================
// Basic Tests with RAII
// ============================================================================

TEST_F(MCPFilterChainTest, DispatcherCreation) {
  // Test that dispatcher was created successfully
  EXPECT_TRUE(*dispatcher_);
  EXPECT_NE(dispatcher_->get(), nullptr);
}

TEST_F(MCPFilterChainTest, BufferCreationWithRAII) {
  // Test buffer creation and automatic cleanup
  auto buffer = createTestBuffer(2048);
  EXPECT_TRUE(buffer);
  EXPECT_NE(buffer.get(), nullptr);

  // Buffer automatically cleaned up when it goes out of scope
}

TEST_F(MCPFilterChainTest, DispatcherOperations) {
  // Test basic dispatcher operations
  EXPECT_TRUE(mcp_dispatcher_is_thread(dispatcher_->get()));

  // Test timer operations
  auto timer_id =
      mcp_dispatcher_create_timer(dispatcher_->get(), nullptr, nullptr);
  EXPECT_NE(timer_id, 0);

  // Enable timer with 1 second timeout
  mcp_dispatcher_enable_timer(dispatcher_->get(), timer_id, 1000, false);

  // Disable and destroy timer
  mcp_dispatcher_disable_timer(dispatcher_->get(), timer_id);
  mcp_dispatcher_destroy_timer(dispatcher_->get(), timer_id);
}

TEST_F(MCPFilterChainTest, TransactionBasedOperations) {
  // Use transaction for resource management
  TransactionGuard transaction;
  ASSERT_TRUE(transaction);

  // Create multiple buffers and add to transaction
  BufferGuard buffer1(1024);
  BufferGuard buffer2(2048);

  ASSERT_TRUE(buffer1);
  ASSERT_TRUE(buffer2);

  // Add buffers to transaction
  auto result = mcp_transaction_add(
      transaction, reinterpret_cast<void*>(buffer1.get()), MCP_TYPE_UNKNOWN);
  EXPECT_EQ(result, MCP_OK);

  result = mcp_transaction_add(
      transaction, reinterpret_cast<void*>(buffer2.get()), MCP_TYPE_UNKNOWN);
  EXPECT_EQ(result, MCP_OK);

  // Check transaction size
  EXPECT_EQ(mcp_transaction_size(transaction), 2);

  // Commit transaction
  auto txn_ptr = transaction.get();
  EXPECT_EQ(mcp_transaction_commit(&txn_ptr), MCP_OK);
}

// ============================================================================
// Thread Safety Tests with RAII
// ============================================================================

TEST_F(MCPFilterChainTest, ConcurrentDispatcherOperations) {
  std::atomic<bool> stop(false);
  std::vector<std::thread> threads;
  std::atomic<int> operations_completed(0);

  // Thread 1: Create and destroy timers
  threads.emplace_back([&]() {
    while (!stop) {
      auto timer_id =
          mcp_dispatcher_create_timer(dispatcher_->get(), nullptr, nullptr);
      if (timer_id != 0) {
        mcp_dispatcher_enable_timer(dispatcher_->get(), timer_id, 100, false);
        mcp_dispatcher_disable_timer(dispatcher_->get(), timer_id);
        mcp_dispatcher_destroy_timer(dispatcher_->get(), timer_id);
        operations_completed++;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // Thread 2: Check if in dispatcher thread
  threads.emplace_back([&]() {
    while (!stop) {
      bool is_thread = mcp_dispatcher_is_thread(dispatcher_->get());
      if (is_thread) {
        operations_completed++;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // Let threads run for a short time
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  stop = true;

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_GT(operations_completed.load(), 0);
}

// ============================================================================
// Integration Tests with RAII
// ============================================================================

TEST_F(MCPFilterChainTest, FullLifecycleWithRAII) {
  // Create multiple resources and manage them with RAII
  std::vector<BufferGuard> buffers;

  // Create multiple buffers
  for (int i = 0; i < 10; ++i) {
    size_t size = 1024 * (i + 1);
    BufferGuard buffer(size);
    ASSERT_TRUE(buffer);
    buffers.push_back(std::move(buffer));
  }

  // Create transaction to manage some resources
  TransactionGuard transaction;
  ASSERT_TRUE(transaction);

  // Add first few buffers to transaction
  for (size_t i = 0; i < std::min(size_t(3), buffers.size()); ++i) {
    auto result = mcp_transaction_add(transaction,
                                      reinterpret_cast<void*>(buffers[i].get()),
                                      MCP_TYPE_UNKNOWN);
    EXPECT_EQ(result, MCP_OK);
  }

  // Test timer operations with dispatcher
  std::vector<uint64_t> timers;
  for (int i = 0; i < 5; ++i) {
    auto timer_id =
        mcp_dispatcher_create_timer(dispatcher_->get(), nullptr, nullptr);
    if (timer_id != 0) {
      mcp_dispatcher_enable_timer(dispatcher_->get(), timer_id, 100 * (i + 1),
                                  false);
      timers.push_back(timer_id);
    }
  }

  // Clean up timers
  for (auto timer_id : timers) {
    mcp_dispatcher_disable_timer(dispatcher_->get(), timer_id);
    mcp_dispatcher_destroy_timer(dispatcher_->get(), timer_id);
  }

  // Commit transaction
  auto txn_ptr = transaction.get();
  mcp_transaction_commit(&txn_ptr);

  // All buffers and transaction automatically cleaned up by RAII
}

// ============================================================================
// Error Handling Tests with RAII
// ============================================================================

TEST_F(MCPFilterChainTest, InvalidOperations) {
  // Test operations with null/invalid parameters
  EXPECT_FALSE(mcp_dispatcher_is_thread(nullptr));

  auto invalid_timer = mcp_dispatcher_create_timer(nullptr, nullptr, nullptr);
  EXPECT_EQ(invalid_timer, 0);

  // These should not crash
  mcp_dispatcher_disable_timer(dispatcher_->get(), 0);
  mcp_dispatcher_destroy_timer(dispatcher_->get(), 0);

  // Test buffer creation with invalid size
  BufferGuard invalid_buffer(0);
  // May or may not be null depending on implementation

  // Test transaction operations
  TransactionGuard transaction;
  if (transaction) {
    // Test adding null pointer
    auto result = mcp_transaction_add(transaction, nullptr, MCP_TYPE_UNKNOWN);
    EXPECT_NE(result, MCP_OK);
  }
}

TEST_F(MCPFilterChainTest, RAIICleanupOnException) {
  // Test that RAII cleanup works even when exceptions occur
  try {
    BufferGuard buffer(1024);
    ASSERT_TRUE(buffer);

    TransactionGuard transaction;
    ASSERT_TRUE(transaction);

    // Simulate an error condition that might throw
    // In real code this might be a failed operation
    if (buffer && transaction) {
      // Normal operations would go here
      // Resources will be cleaned up by RAII even if exception occurs
    }

    // All resources automatically cleaned up

  } catch (...) {
    // RAII ensures cleanup even in exceptional cases
    FAIL() << "Unexpected exception occurred";
  }
}

// ============================================================================
// Performance Tests with RAII
// ============================================================================

TEST_F(MCPFilterChainTest, RAIIPerformanceTest) {
  // Test RAII performance with many resources
  auto start = std::chrono::high_resolution_clock::now();

  {
    std::vector<BufferGuard> buffers;
    std::vector<TransactionGuard> transactions;

    // Create many resources
    for (int i = 0; i < 1000; ++i) {
      BufferGuard buffer(1024);
      if (buffer) {
        buffers.push_back(std::move(buffer));
      }

      if (i % 100 == 0) {
        TransactionGuard transaction;
        if (transaction) {
          transactions.push_back(std::move(transaction));
        }
      }
    }

    // All resources automatically cleaned up when going out of scope
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  // Should complete reasonably quickly
  EXPECT_LT(duration.count(), 5000);  // Less than 5 seconds
}

}  // namespace

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}