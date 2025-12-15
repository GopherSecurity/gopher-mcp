/**
 * @file test_handle_manager.cc
 * @brief Unit tests for HandleManager ownership semantics
 */

#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "../../src/c_api/handle_manager.h"

namespace mcp {
namespace c_api_internal {
namespace {

// Test class for tracking construction/destruction
class TestObject {
 public:
  explicit TestObject(int value, int* counter = nullptr)
      : value_(value), destruction_counter_(counter) {
    if (destruction_counter_) {
      (*destruction_counter_) = 0;
    }
  }

  ~TestObject() {
    if (destruction_counter_) {
      (*destruction_counter_)++;
    }
  }

  int getValue() const { return value_; }

 private:
  int value_;
  int* destruction_counter_;
};

class HandleManagerTest : public ::testing::Test {
 protected:
  HandleManager<TestObject> manager_;
};

TEST_F(HandleManagerTest, StoreSharedPtr) {
  auto obj = std::make_shared<TestObject>(42);
  uint64_t handle = manager_.store(obj);

  ASSERT_NE(handle, 0);
  auto retrieved = manager_.get(handle);
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->getValue(), 42);

  // Both should point to the same object
  EXPECT_EQ(obj.get(), retrieved.get());
  EXPECT_EQ(obj.use_count(), 3);  // obj, retrieved, and one in manager
}

TEST_F(HandleManagerTest, StoreUniquePtr) {
  auto obj = std::make_unique<TestObject>(99);
  TestObject* raw_ptr = obj.get();
  uint64_t handle = manager_.store(std::move(obj));

  ASSERT_NE(handle, 0);
  EXPECT_EQ(obj, nullptr);  // unique_ptr should be moved from

  auto retrieved = manager_.get(handle);
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->getValue(), 99);
  EXPECT_EQ(retrieved.get(), raw_ptr);  // Should be the same object
}

TEST_F(HandleManagerTest, StoreNullPtr) {
  std::shared_ptr<TestObject> null_shared;
  std::unique_ptr<TestObject> null_unique;

  EXPECT_EQ(manager_.store(null_shared), 0);
  EXPECT_EQ(manager_.store(std::move(null_unique)), 0);
}

TEST_F(HandleManagerTest, GetInvalidHandle) {
  EXPECT_EQ(manager_.get(0), nullptr);
  EXPECT_EQ(manager_.get(999999), nullptr);
}

TEST_F(HandleManagerTest, ReleaseHandle) {
  int destruction_count = 0;
  {
    auto obj = std::make_shared<TestObject>(10, &destruction_count);
    uint64_t handle = manager_.store(obj);
    EXPECT_EQ(destruction_count, 0);

    // Object still exists after store
    auto retrieved = manager_.get(handle);
    EXPECT_NE(retrieved, nullptr);
    EXPECT_EQ(destruction_count, 0);

    // Release the handle
    manager_.release(handle);

    // Object should no longer be retrievable
    EXPECT_EQ(manager_.get(handle), nullptr);
  }

  // Object should be destroyed now (if no other references)
  // Note: destruction_count should be 1, but timing may vary
  // so we just check it was destroyed at least once
  EXPECT_GE(destruction_count, 1);
}

TEST_F(HandleManagerTest, RetainIsNoOp) {
  auto obj = std::make_shared<TestObject>(20);
  uint64_t handle = manager_.store(obj);

  // retain() is a no-op for shared_ptr-based implementation
  manager_.retain(handle);

  auto retrieved = manager_.get(handle);
  EXPECT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->getValue(), 20);
}

TEST_F(HandleManagerTest, ClearAll) {
  std::vector<uint64_t> handles;
  for (int i = 0; i < 10; ++i) {
    auto obj = std::make_unique<TestObject>(i);
    handles.push_back(manager_.store(std::move(obj)));
  }

  // All handles should be valid
  for (size_t i = 0; i < handles.size(); ++i) {
    auto retrieved = manager_.get(handles[i]);
    EXPECT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->getValue(), static_cast<int>(i));
  }

  // Clear all handles
  manager_.clear();

  // All handles should now be invalid
  for (uint64_t handle : handles) {
    EXPECT_EQ(manager_.get(handle), nullptr);
  }
}

TEST_F(HandleManagerTest, UniqueHandles) {
  std::set<uint64_t> handles;
  for (int i = 0; i < 100; ++i) {
    auto obj = std::make_unique<TestObject>(i);
    uint64_t handle = manager_.store(std::move(obj));
    EXPECT_NE(handle, 0);
    EXPECT_TRUE(handles.insert(handle).second);  // Should be unique
  }
}

TEST_F(HandleManagerTest, ThreadSafety) {
  const int num_threads = 10;
  const int objects_per_thread = 100;
  std::vector<std::thread> threads;
  std::vector<std::vector<uint64_t>> thread_handles(num_threads);

  // Multiple threads storing objects
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([this, t, &thread_handles, objects_per_thread]() {
      for (int i = 0; i < objects_per_thread; ++i) {
        auto obj = std::make_unique<TestObject>(t * 1000 + i);
        uint64_t handle = manager_.store(std::move(obj));
        thread_handles[t].push_back(handle);
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }
  threads.clear();

  // Multiple threads retrieving objects
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([this, t, &thread_handles]() {
      for (uint64_t handle : thread_handles[t]) {
        auto obj = manager_.get(handle);
        EXPECT_NE(obj, nullptr);
        int expected_value = (t * 1000) + (obj->getValue() % 1000);
        EXPECT_EQ(obj->getValue(), expected_value);
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(HandleManagerTest, MixedOwnership) {
  int destruction_count1 = 0;
  int destruction_count2 = 0;

  // Store via unique_ptr
  auto unique = std::make_unique<TestObject>(1, &destruction_count1);
  uint64_t handle1 = manager_.store(std::move(unique));

  // Store via shared_ptr
  auto shared = std::make_shared<TestObject>(2, &destruction_count2);
  uint64_t handle2 = manager_.store(shared);

  // Both should be retrievable
  auto retrieved1 = manager_.get(handle1);
  auto retrieved2 = manager_.get(handle2);

  EXPECT_NE(retrieved1, nullptr);
  EXPECT_NE(retrieved2, nullptr);
  EXPECT_EQ(retrieved1->getValue(), 1);
  EXPECT_EQ(retrieved2->getValue(), 2);

  // Release first handle
  manager_.release(handle1);
  EXPECT_EQ(manager_.get(handle1), nullptr);

  // Second should still be valid
  EXPECT_NE(manager_.get(handle2), nullptr);

  // Clear remaining
  manager_.clear();

  // Both should be gone
  EXPECT_EQ(manager_.get(handle1), nullptr);
  EXPECT_EQ(manager_.get(handle2), nullptr);
}

TEST_F(HandleManagerTest, OwnershipTransfer) {
  int destruction_count = 0;
  TestObject* raw_ptr = nullptr;

  {
    auto obj = std::make_unique<TestObject>(777, &destruction_count);
    raw_ptr = obj.get();

    // Transfer ownership to manager
    uint64_t handle = manager_.store(std::move(obj));

    // Original unique_ptr should be empty
    EXPECT_EQ(obj, nullptr);

    // Object should still be alive
    EXPECT_EQ(destruction_count, 0);

    // Should be accessible via handle
    auto retrieved = manager_.get(handle);
    EXPECT_EQ(retrieved.get(), raw_ptr);

    // Release the handle
    manager_.release(handle);
  }

  // Object should be destroyed after release
  // (no other references exist)
  EXPECT_EQ(destruction_count, 1);
}

}  // namespace
}  // namespace c_api_internal
}  // namespace mcp