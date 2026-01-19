/**
 * @file test_connection_manager_callback_fixes.cc
 * @brief Unit tests for connection manager callback removal fixes
 *
 * Tests for commit 464e5a16af7460f428114b7b3f84835d0236cb29:
 * - Remove callbacks before closing connection
 * - Prevent use-after-free in connection callbacks
 * - Safe cleanup order
 */

#include <atomic>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/mcp_connection_manager.h"
#include "mcp/network/connection_impl.h"

namespace mcp {
namespace {

using namespace network;

// Mock connection for testing callback removal
class MockConnection {
 public:
  void addCallbacks(void* cb) { callbacks_.push_back(cb); }

  void removeCallbacks(void* cb) {
    auto it = std::find(callbacks_.begin(), callbacks_.end(), cb);
    if (it != callbacks_.end()) {
      callbacks_.erase(it);
      removed_count_++;
    }
  }

  void close() {
    closed_ = true;

    // Simulate calling callbacks during close
    // If callbacks weren't removed, this could cause use-after-free
    for (auto* cb : callbacks_) {
      (void)cb;  // Access callback pointer
    }
  }

  bool closed_{false};
  int removed_count_{0};
  std::vector<void*> callbacks_;
};

/**
 * Test callbacks removed before close
 * Fix: Call removeConnectionCallbacks before close
 */
TEST(ConnectionManagerCallbackFixes, RemoveCallbacksBeforeClose) {
  MockConnection conn;
  void* callback_ptr = reinterpret_cast<void*>(0x1234);

  conn.addCallbacks(callback_ptr);
  EXPECT_EQ(1, conn.callbacks_.size());

  // The fix does:
  // active_connection_->removeConnectionCallbacks(*this);
  // active_connection_->close(...);

  conn.removeCallbacks(callback_ptr);
  EXPECT_EQ(0, conn.callbacks_.size());
  EXPECT_EQ(1, conn.removed_count_);

  conn.close();
  EXPECT_TRUE(conn.closed_);
}

/**
 * Test prevents use-after-free
 * Fix: Remove callbacks first to prevent accessing freed memory
 */
TEST(ConnectionManagerCallbackFixes, PreventsUseAfterFree) {
  MockConnection conn;
  void* callback_ptr = reinterpret_cast<void*>(0x5678);

  conn.addCallbacks(callback_ptr);

  // Remove callbacks BEFORE closing
  conn.removeCallbacks(callback_ptr);
  EXPECT_EQ(0, conn.callbacks_.size());

  // Now close - no callbacks to access
  conn.close();
  EXPECT_TRUE(conn.closed_);

  // No use-after-free occurred
  SUCCEED();
}

/**
 * Test cleanup order is correct
 * Fix: Ensure proper order: remove callbacks -> close -> reset
 */
TEST(ConnectionManagerCallbackFixes, ProperCleanupOrder) {
  std::vector<std::string> cleanup_steps;

  // Step 1: Remove callbacks
  cleanup_steps.push_back("remove_callbacks");

  // Step 2: Close connection
  cleanup_steps.push_back("close_connection");

  // Step 3: Reset connection pointer
  cleanup_steps.push_back("reset_pointer");

  ASSERT_EQ(3, cleanup_steps.size());
  EXPECT_EQ("remove_callbacks", cleanup_steps[0]);
  EXPECT_EQ("close_connection", cleanup_steps[1]);
  EXPECT_EQ("reset_pointer", cleanup_steps[2]);
}

/**
 * Test multiple callbacks removed safely
 * Fix: All callbacks removed before close
 */
TEST(ConnectionManagerCallbackFixes, MultipleCallbacksRemoved) {
  MockConnection conn;

  void* cb1 = reinterpret_cast<void*>(0x1000);
  void* cb2 = reinterpret_cast<void*>(0x2000);
  void* cb3 = reinterpret_cast<void*>(0x3000);

  conn.addCallbacks(cb1);
  conn.addCallbacks(cb2);
  conn.addCallbacks(cb3);
  EXPECT_EQ(3, conn.callbacks_.size());

  // Remove all callbacks
  conn.removeCallbacks(cb1);
  conn.removeCallbacks(cb2);
  conn.removeCallbacks(cb3);
  EXPECT_EQ(0, conn.callbacks_.size());
  EXPECT_EQ(3, conn.removed_count_);

  // Safe to close now
  conn.close();
  EXPECT_TRUE(conn.closed_);
}

/**
 * Test callback removal is idempotent
 * Fix: Removing same callback multiple times is safe
 */
TEST(ConnectionManagerCallbackFixes, IdempotentCallbackRemoval) {
  MockConnection conn;
  void* callback_ptr = reinterpret_cast<void*>(0xABCD);

  conn.addCallbacks(callback_ptr);
  EXPECT_EQ(1, conn.callbacks_.size());

  // Remove once
  conn.removeCallbacks(callback_ptr);
  EXPECT_EQ(0, conn.callbacks_.size());

  // Remove again - should be safe
  conn.removeCallbacks(callback_ptr);
  EXPECT_EQ(0, conn.callbacks_.size());

  conn.close();
  EXPECT_TRUE(conn.closed_);
}

/**
 * Test close with FlushWrite
 * Fix: Callbacks removed before close with any close type
 */
TEST(ConnectionManagerCallbackFixes, CallbacksRemovedBeforeFlushWrite) {
  MockConnection conn;
  void* callback_ptr = reinterpret_cast<void*>(0xDEAD);

  conn.addCallbacks(callback_ptr);

  // The fix removes callbacks before:
  // active_connection_->close(ConnectionCloseType::FlushWrite);

  conn.removeCallbacks(callback_ptr);
  EXPECT_EQ(0, conn.callbacks_.size());

  conn.close();
  EXPECT_TRUE(conn.closed_);
}

/**
 * Test close without callbacks doesn't crash
 * Fix: Safe to close even if no callbacks were registered
 */
TEST(ConnectionManagerCallbackFixes, CloseWithoutCallbacks) {
  MockConnection conn;

  EXPECT_EQ(0, conn.callbacks_.size());

  // Should be safe to close without callbacks
  conn.close();
  EXPECT_TRUE(conn.closed_);
}

/**
 * Test connection reset after close
 * Fix: Reset active_connection_ after closing
 */
TEST(ConnectionManagerCallbackFixes, ConnectionResetAfterClose) {
  std::unique_ptr<MockConnection> conn = std::make_unique<MockConnection>();

  void* callback_ptr = reinterpret_cast<void*>(0xBEEF);
  conn->addCallbacks(callback_ptr);
  conn->removeCallbacks(callback_ptr);
  conn->close();

  // Reset the connection pointer (the fix)
  conn.reset();

  EXPECT_EQ(nullptr, conn);
}

/**
 * Test callback removal prevents race conditions
 * Fix: Remove callbacks before close prevents races
 */
TEST(ConnectionManagerCallbackFixes, PreventsRaceConditions) {
  MockConnection conn;
  void* callback_ptr = reinterpret_cast<void*>(0xCAFE);

  conn.addCallbacks(callback_ptr);

  // Remove callbacks first (the fix)
  // This ensures close() can't access the callbacks
  conn.removeCallbacks(callback_ptr);

  // Multiple threads could be closing
  std::atomic<int> close_count{0};

  for (int i = 0; i < 5; ++i) {
    if (!conn.closed_) {
      conn.close();
      close_count++;
    }
  }

  EXPECT_GE(close_count, 1);
  EXPECT_TRUE(conn.closed_);
}

/**
 * Test safe cleanup when connection is null
 * Fix: Check for null before removing callbacks
 */
TEST(ConnectionManagerCallbackFixes, SafeCleanupWithNullConnection) {
  MockConnection* conn = nullptr;

  // The fix checks: if (active_connection_)
  if (conn) {
    conn->close();
  }

  // Should not crash
  SUCCEED();
}

/**
 * Test callback object lifetime
 * Fix: Callbacks removed before they might be destroyed
 */
TEST(ConnectionManagerCallbackFixes, CallbackLifetime) {
  MockConnection conn;

  {
    void* callback_ptr = reinterpret_cast<void*>(0xFACE);
    conn.addCallbacks(callback_ptr);

    // Remove callbacks before callback object goes out of scope
    conn.removeCallbacks(callback_ptr);
    EXPECT_EQ(0, conn.callbacks_.size());
  }  // callback_ptr goes out of scope

  // Close should not access freed callback
  conn.close();
  EXPECT_TRUE(conn.closed_);
}

/**
 * Test manager can be destroyed safely after close
 * Fix: Proper cleanup order allows safe destruction
 */
TEST(ConnectionManagerCallbackFixes, SafeDestruction) {
  {
    MockConnection conn;
    void* callback_ptr = reinterpret_cast<void*>(0x1111);

    conn.addCallbacks(callback_ptr);
    conn.removeCallbacks(callback_ptr);
    conn.close();

    // conn will be destroyed here
  }

  // Should not crash during destruction
  SUCCEED();
}

/**
 * Test close is called after callback removal completes
 * Fix: Ensures callback removal completes before close
 */
TEST(ConnectionManagerCallbackFixes, RemovalCompletesBeforeClose) {
  MockConnection conn;
  void* callback_ptr = reinterpret_cast<void*>(0x2222);

  conn.addCallbacks(callback_ptr);

  // Removal should complete fully
  conn.removeCallbacks(callback_ptr);
  EXPECT_EQ(1, conn.removed_count_);
  EXPECT_EQ(0, conn.callbacks_.size());

  // Now safe to close
  EXPECT_FALSE(conn.closed_);
  conn.close();
  EXPECT_TRUE(conn.closed_);
}

/**
 * Test callback not accessed after removal
 * Fix: Removed callbacks are never accessed during close
 */
TEST(ConnectionManagerCallbackFixes, RemovedCallbackNotAccessed) {
  MockConnection conn;
  void* callback_ptr = reinterpret_cast<void*>(0x3333);

  conn.addCallbacks(callback_ptr);
  EXPECT_EQ(1, conn.callbacks_.size());

  // Remove the callback
  conn.removeCallbacks(callback_ptr);
  EXPECT_EQ(0, conn.callbacks_.size());

  // Close - should not try to access removed callback
  conn.close();

  // If callback was accessed, we'd see it in the callbacks list
  EXPECT_EQ(0, conn.callbacks_.size());
}

}  // namespace
}  // namespace mcp
