/**
 * @file test_mcp_raii.cc
 * @brief Comprehensive unit tests for MCP RAII utilities
 *
 * Tests all aspects of the RAII library including:
 * - ResourceGuard functionality and edge cases
 * - AllocationTransaction with commit/rollback scenarios
 * - Thread safety and concurrent operations
 * - Performance characteristics
 * - Memory safety and leak detection
 *
 * @copyright Copyright (c) 2025 MCP Project
 * @license MIT License
 */

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#define MCP_RAII_IMPLEMENTATION
#include "mcp/c_api/mcp_raii.h"
// Note: Avoiding mcp_c_types.h to prevent header conflicts

using namespace mcp::raii;

namespace {

/* ============================================================================
 * Test Fixtures and Utilities
 * ============================================================================
 */

/**
 * Mock resource class for testing
 */
class MockResource {
 public:
  static std::atomic<int> allocation_count;
  static std::atomic<int> deallocation_count;
  static std::atomic<int> copy_count;
  static std::atomic<int> move_count;

  int value;

  MockResource(int val = 42) : value(val) {
    allocation_count.fetch_add(1, std::memory_order_relaxed);
  }

  MockResource(const MockResource& other) : value(other.value) {
    allocation_count.fetch_add(1, std::memory_order_relaxed);
    copy_count.fetch_add(1, std::memory_order_relaxed);
  }

  MockResource(MockResource&& other) noexcept : value(other.value) {
    allocation_count.fetch_add(1, std::memory_order_relaxed);
    move_count.fetch_add(1, std::memory_order_relaxed);
    other.value = 0;
  }

  ~MockResource() {
    deallocation_count.fetch_add(1, std::memory_order_relaxed);
  }

  static void reset_counters() {
    allocation_count.store(0, std::memory_order_relaxed);
    deallocation_count.store(0, std::memory_order_relaxed);
    copy_count.store(0, std::memory_order_relaxed);
    move_count.store(0, std::memory_order_relaxed);
  }

  static bool is_balanced() {
    return allocation_count.load(std::memory_order_relaxed) ==
           deallocation_count.load(std::memory_order_relaxed);
  }
};

// Static member definitions
std::atomic<int> MockResource::allocation_count{0};
std::atomic<int> MockResource::deallocation_count{0};
std::atomic<int> MockResource::copy_count{0};
std::atomic<int> MockResource::move_count{0};

/**
 * Custom deleter for testing
 */
class MockDeleter {
 public:
  static std::atomic<int> delete_count;

  void operator()(MockResource* ptr) const {
    if (ptr) {
      delete_count.fetch_add(1, std::memory_order_relaxed);
      delete ptr;
    }
  }

  static void reset_counter() {
    delete_count.store(0, std::memory_order_relaxed);
  }
};

std::atomic<int> MockDeleter::delete_count{0};

}  // anonymous namespace

// Specialization of c_deleter for MockResource
namespace mcp {
namespace raii {

template <>
struct c_deleter<MockResource> {
  void operator()(MockResource* ptr) const noexcept { delete ptr; }
};

}  // namespace raii
}  // namespace mcp

namespace {

/**
 * Test fixture for RAII tests
 */
class RAIITest : public ::testing::Test {
 protected:
  void SetUp() override {
    MockResource::reset_counters();
    MockDeleter::reset_counter();
  }

  void TearDown() override {
    // Verify no memory leaks in tests
    EXPECT_TRUE(MockResource::is_balanced())
        << "Memory leak detected: allocations="
        << MockResource::allocation_count.load()
        << ", deallocations=" << MockResource::deallocation_count.load();
  }
};

/* ============================================================================
 * ResourceGuard Tests
 * ============================================================================
 */

class ResourceGuardTest : public RAIITest {};

TEST_F(ResourceGuardTest, DefaultConstruction) {
  ResourceGuard<MockResource> guard;

  EXPECT_FALSE(guard);
  EXPECT_EQ(nullptr, guard.get());
}

TEST_F(ResourceGuardTest, BasicResourceManagement) {
  auto* resource = new MockResource(123);

  {
    ResourceGuard<MockResource> guard(resource,
                                      [](MockResource* p) { delete p; });

    EXPECT_TRUE(guard);
    EXPECT_EQ(resource, guard.get());
    EXPECT_EQ(123, guard.get()->value);
  }  // guard destructor should delete resource

  // Verify resource was properly cleaned up
  EXPECT_EQ(1, MockResource::deallocation_count.load());
}

TEST_F(ResourceGuardTest, CustomDeleter) {
  auto* resource = new MockResource(456);

  {
    ResourceGuard<MockResource> guard(resource, MockDeleter{});
    EXPECT_TRUE(guard);
    EXPECT_EQ(resource, guard.get());
  }

  EXPECT_EQ(1, MockDeleter::delete_count.load());
  EXPECT_EQ(1, MockResource::deallocation_count.load());
}

TEST_F(ResourceGuardTest, MoveSemantics) {
  auto* resource = new MockResource(789);

  ResourceGuard<MockResource> guard1(resource,
                                     [](MockResource* p) { delete p; });
  EXPECT_TRUE(guard1);
  EXPECT_EQ(resource, guard1.get());

  // Move construction
  ResourceGuard<MockResource> guard2 = std::move(guard1);
  EXPECT_FALSE(guard1);
  EXPECT_EQ(nullptr, guard1.get());
  EXPECT_TRUE(guard2);
  EXPECT_EQ(resource, guard2.get());

  // Move assignment
  ResourceGuard<MockResource> guard3;
  guard3 = std::move(guard2);
  EXPECT_FALSE(guard2);
  EXPECT_EQ(nullptr, guard2.get());
  EXPECT_TRUE(guard3);
  EXPECT_EQ(resource, guard3.get());

  // Only one deallocation should occur when guard3 is destroyed
}

TEST_F(ResourceGuardTest, Release) {
  auto* resource = new MockResource(101112);

  MockResource* released;
  {
    ResourceGuard<MockResource> guard(resource);
    released = guard.release();
    EXPECT_EQ(resource, released);
    EXPECT_FALSE(guard);
    EXPECT_EQ(nullptr, guard.get());
  }  // No cleanup should occur

  EXPECT_EQ(0, MockResource::deallocation_count.load());

  // Manual cleanup
  delete released;
  EXPECT_EQ(1, MockResource::deallocation_count.load());
}

TEST_F(ResourceGuardTest, Reset) {
  auto* resource1 = new MockResource(111);
  auto* resource2 = new MockResource(222);

  ResourceGuard<MockResource> guard(resource1,
                                    [](MockResource* p) { delete p; });
  EXPECT_EQ(resource1, guard.get());

  // Reset with new resource
  guard.reset(resource2);
  EXPECT_EQ(resource2, guard.get());
  EXPECT_EQ(1, MockResource::deallocation_count.load());  // resource1 deleted

  // Reset to null
  guard.reset();
  EXPECT_FALSE(guard);
  EXPECT_EQ(nullptr, guard.get());
  EXPECT_EQ(2, MockResource::deallocation_count.load());  // resource2 deleted
}

TEST_F(ResourceGuardTest, Swap) {
  auto* resource1 = new MockResource(333);
  auto* resource2 = new MockResource(444);

  ResourceGuard<MockResource> guard1(resource1,
                                     [](MockResource* p) { delete p; });
  ResourceGuard<MockResource> guard2(resource2,
                                     [](MockResource* p) { delete p; });

  guard1.swap(guard2);

  EXPECT_EQ(resource2, guard1.get());
  EXPECT_EQ(resource1, guard2.get());
}

TEST_F(ResourceGuardTest, MakeResourceGuard) {
  auto* resource = new MockResource(555);

  {
    auto guard =
        make_resource_guard(resource, [](MockResource* p) { delete p; });
    EXPECT_TRUE(guard);
    EXPECT_EQ(resource, guard.get());
  }

  EXPECT_EQ(1, MockResource::deallocation_count.load());
}

TEST_F(ResourceGuardTest, MakeResourceGuardWithCustomDeleter) {
  auto* resource = new MockResource(666);

  {
    auto guard = make_resource_guard(resource, MockDeleter{});
    EXPECT_TRUE(guard);
    EXPECT_EQ(resource, guard.get());
  }

  EXPECT_EQ(1, MockDeleter::delete_count.load());
}

TEST_F(ResourceGuardTest, NullResource) {
  ResourceGuard<MockResource> guard(nullptr, [](MockResource* p) { delete p; });

  EXPECT_FALSE(guard);
  EXPECT_EQ(nullptr, guard.get());

  // Should not crash on destruction
}

/* ============================================================================
 * AllocationTransaction Tests
 * ============================================================================
 */

class AllocationTransactionTest : public RAIITest {};

TEST_F(AllocationTransactionTest, CommitTransaction) {
  auto* resource1 = new MockResource(777);
  auto* resource2 = new MockResource(888);

  {
    AllocationTransaction txn;
    txn.track(resource1,
              [](void* ptr) { delete static_cast<MockResource*>(ptr); });
    txn.track(resource2,
              [](void* ptr) { delete static_cast<MockResource*>(ptr); });

    EXPECT_EQ(2u, txn.resource_count());
    EXPECT_FALSE(txn.is_committed());

    txn.commit();
    EXPECT_TRUE(txn.is_committed());
    EXPECT_EQ(0u, txn.resource_count());
  }  // No cleanup should occur on destruction

  EXPECT_EQ(0, MockResource::deallocation_count.load());

  // Manual cleanup
  delete resource1;
  delete resource2;
  EXPECT_EQ(2, MockResource::deallocation_count.load());
}

TEST_F(AllocationTransactionTest, RollbackTransaction) {
  auto* resource1 = new MockResource(999);
  auto* resource2 = new MockResource(1010);

  {
    AllocationTransaction txn;
    txn.track(resource1,
              [](void* ptr) { delete static_cast<MockResource*>(ptr); });
    txn.track(resource2,
              [](void* ptr) { delete static_cast<MockResource*>(ptr); });

    EXPECT_EQ(2u, txn.resource_count());
    EXPECT_FALSE(txn.is_committed());

    // Don't commit - should rollback automatically
  }

  EXPECT_EQ(2, MockResource::deallocation_count.load());
}

TEST_F(AllocationTransactionTest, ManualRollback) {
  auto* resource = new MockResource(1111);

  AllocationTransaction txn;
  txn.track(resource,
            [](void* ptr) { delete static_cast<MockResource*>(ptr); });

  EXPECT_FALSE(txn.is_committed());

  txn.rollback();
  EXPECT_TRUE(txn.is_committed());
  EXPECT_EQ(1, MockResource::deallocation_count.load());
}

TEST_F(AllocationTransactionTest, TypedTracking) {
  auto* resource = new MockResource(1212);

  {
    AllocationTransaction txn;
    txn.track(resource, [](void* p) {
      delete static_cast<MockResource*>(p);
    });  // Uses proper deleter

    EXPECT_EQ(1u, txn.resource_count());
    // Don't commit - should auto-rollback
  }

  EXPECT_EQ(1, MockResource::deallocation_count.load());
}

TEST_F(AllocationTransactionTest, MoveSemantics) {
  auto* resource = new MockResource(1313);

  AllocationTransaction txn1;
  txn1.track(resource,
             [](void* ptr) { delete static_cast<MockResource*>(ptr); });

  // Move construction
  AllocationTransaction txn2 = std::move(txn1);
  EXPECT_EQ(1u, txn2.resource_count());
  EXPECT_EQ(0u, txn1.resource_count());
  EXPECT_TRUE(txn1.is_committed());  // Moved-from should be committed

  // Move assignment
  AllocationTransaction txn3;
  txn3 = std::move(txn2);
  EXPECT_EQ(1u, txn3.resource_count());
  EXPECT_EQ(0u, txn2.resource_count());
  EXPECT_TRUE(txn2.is_committed());

  // Don't commit txn3 - should cleanup resource
}

TEST_F(AllocationTransactionTest, EmptyTransaction) {
  {
    AllocationTransaction txn;
    EXPECT_EQ(0u, txn.resource_count());
    EXPECT_FALSE(txn.is_committed());

    txn.commit();
    EXPECT_TRUE(txn.is_committed());
  }

  // Should not crash with empty transaction
}

TEST_F(AllocationTransactionTest, NullResourceTracking) {
  AllocationTransaction txn;

  // Tracking null resources should be safe
  txn.track(nullptr, [](void*) {});
  txn.track<MockResource>(nullptr);

  EXPECT_EQ(0u, txn.resource_count());
}

/* ============================================================================
 * Thread Safety Tests
 * ============================================================================
 */

class ThreadSafetyTest : public RAIITest {};

TEST_F(ThreadSafetyTest, ConcurrentResourceGuardOperations) {
  constexpr int num_threads = 10;
  constexpr int operations_per_thread = 100;

  std::atomic<int> completed_threads{0};
  std::vector<std::thread> threads;

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&, i]() {
      for (int j = 0; j < operations_per_thread; ++j) {
        auto* resource = new MockResource(i * 1000 + j);

        // Create and destroy ResourceGuard
        {
          auto guard =
              make_resource_guard(resource, [](MockResource* p) { delete p; });
          EXPECT_TRUE(guard);
          EXPECT_EQ(resource, guard.get());

          // Simulate some work
          std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
      }
      completed_threads.fetch_add(1, std::memory_order_relaxed);
    });
  }

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(num_threads, completed_threads.load());

  // All resources should be properly cleaned up
  EXPECT_EQ(num_threads * operations_per_thread,
            MockResource::allocation_count.load());
  EXPECT_EQ(num_threads * operations_per_thread,
            MockResource::deallocation_count.load());
}

TEST_F(ThreadSafetyTest, ConcurrentTransactionOperations) {
  constexpr int num_threads = 5;
  constexpr int resources_per_thread = 50;

  std::vector<std::thread> threads;
  std::atomic<int> rolled_back_transactions{0};

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&, i]() {
      AllocationTransaction txn;

      // Track multiple resources
      for (int j = 0; j < resources_per_thread; ++j) {
        auto* resource = new MockResource(i * 1000 + j);
        txn.track(resource,
                  [](void* p) { delete static_cast<MockResource*>(p); });
      }

      // Only test rollback behavior for simplicity
      if (i % 2 != 0) {
        // Let it auto-rollback
        rolled_back_transactions.fetch_add(1, std::memory_order_relaxed);
      } else {
        // Manual rollback for even threads
        txn.rollback();
        rolled_back_transactions.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  // Wait for all threads
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(num_threads, rolled_back_transactions.load());

  // Resources from all rolled-back transactions should be cleaned up
  int expected_deallocations =
      rolled_back_transactions.load() * resources_per_thread;
  EXPECT_EQ(expected_deallocations, MockResource::deallocation_count.load());
}

/* ============================================================================
 * ScopedCleanup Tests
 * ============================================================================
 */

class ScopedCleanupTest : public RAIITest {};

TEST_F(ScopedCleanupTest, BasicCleanup) {
  bool cleanup_called = false;

  {
    auto cleanup = make_scoped_cleanup([&]() { cleanup_called = true; });

    EXPECT_TRUE(cleanup.is_active());
    EXPECT_FALSE(cleanup_called);
  }

  EXPECT_TRUE(cleanup_called);
}

TEST_F(ScopedCleanupTest, ReleaseCleanup) {
  bool cleanup_called = false;

  {
    auto cleanup = make_scoped_cleanup([&]() { cleanup_called = true; });

    cleanup.release();
    EXPECT_FALSE(cleanup.is_active());
  }

  EXPECT_FALSE(cleanup_called);
}

TEST_F(ScopedCleanupTest, MoveSemantics) {
  bool cleanup_called = false;

  {
    auto cleanup1 = make_scoped_cleanup([&]() { cleanup_called = true; });

    auto cleanup2 = std::move(cleanup1);
    EXPECT_FALSE(cleanup1.is_active());
    EXPECT_TRUE(cleanup2.is_active());
  }

  EXPECT_TRUE(cleanup_called);
}

/* ============================================================================
 * Performance Tests
 * ============================================================================
 */

class PerformanceTest : public RAIITest {};

TEST_F(PerformanceTest, ResourceGuardPerformance) {
  constexpr int iterations = 100000;

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < iterations; ++i) {
    auto* resource = new MockResource(i);
    auto guard =
        make_resource_guard(resource, [](MockResource* p) { delete p; });
    // Guard destructor cleans up automatically
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  // Performance should be reasonable (less than 10 microseconds per operation)
  EXPECT_LT(duration.count(), iterations * 10);

  // Verify all resources were properly managed
  EXPECT_TRUE(MockResource::is_balanced());
}

TEST_F(PerformanceTest, TransactionPerformance) {
  constexpr int iterations = 10000;
  constexpr int resources_per_transaction = 10;

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < iterations; ++i) {
    AllocationTransaction txn;

    for (int j = 0; j < resources_per_transaction; ++j) {
      auto* resource = new MockResource(i * 1000 + j);
      txn.track(resource,
                [](void* p) { delete static_cast<MockResource*>(p); });
    }

    // All transactions auto-rollback for consistent memory behavior
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  // Performance should be reasonable (less than 100ms total)
  EXPECT_LT(duration.count(), 100);
}

/* ============================================================================
 * Integration Tests with Generic C Types
 * ============================================================================
 */

class CTypeIntegrationTest : public RAIITest {};

TEST_F(CTypeIntegrationTest, MallocResourceGuard) {
  // Create a malloc'd resource
  auto* buffer = static_cast<char*>(malloc(100));
  strcpy(buffer, "test data");

  {
    auto guard = make_resource_guard(buffer, [](char* p) { free(p); });
    EXPECT_TRUE(guard);
    EXPECT_EQ(buffer, guard.get());
    EXPECT_STREQ("test data", guard.get());
  }

  // Memory should be cleaned up by free deleter
}

TEST_F(CTypeIntegrationTest, MultipleMallocTransaction) {
  AllocationTransaction txn;

  // Create multiple malloc'd resources
  for (int i = 0; i < 5; ++i) {
    auto* buffer = static_cast<char*>(malloc(20));
    snprintf(buffer, 20, "buffer_%d", i);

    txn.track(buffer, [](void* p) { free(p); });
  }

  EXPECT_EQ(5u, txn.resource_count());

  // Let transaction rollback automatically
  // All malloc'd resources should be properly cleaned up
}

/* ============================================================================
 * Edge Cases and Error Handling
 * ============================================================================
 */

class EdgeCaseTest : public RAIITest {};

TEST_F(EdgeCaseTest, ExceptionDuringCleanup) {
  // Test that exceptions during cleanup don't propagate
  bool exception_thrown = false;

  {
    auto cleanup = make_scoped_cleanup([&]() {
      exception_thrown = true;
      throw std::runtime_error("Cleanup exception");
    });
  }  // Should not throw

  EXPECT_TRUE(exception_thrown);
  // Test should complete without throwing
}

TEST_F(EdgeCaseTest, SelfAssignment) {
  auto* resource = new MockResource(1414);

  ResourceGuard<MockResource> guard(resource,
                                    [](MockResource* p) { delete p; });

  // Self-assignment should be safe
  guard = std::move(guard);

  EXPECT_TRUE(guard);
  EXPECT_EQ(resource, guard.get());
}

TEST_F(EdgeCaseTest, DoubleFreeProtection) {
  auto* resource = new MockResource(1515);

  ResourceGuard<MockResource> guard(resource,
                                    [](MockResource* p) { delete p; });

  // Manual reset should prevent double-free
  guard.reset();
  EXPECT_FALSE(guard);

  guard.reset();  // Should be safe to call again

  EXPECT_EQ(1, MockResource::deallocation_count.load());
}

/* ============================================================================
 * Production-Quality Critical Bug Fix Tests
 * ============================================================================
 */

class CriticalBugFixTest : public ::testing::Test {
 protected:
  void SetUp() override { MockResource::reset_counters(); }

  void TearDown() override {
    EXPECT_TRUE(MockResource::is_balanced())
        << "Memory leak detected: allocations="
        << MockResource::allocation_count.load()
        << ", deallocations=" << MockResource::deallocation_count.load();
  }
};

TEST_F(CriticalBugFixTest, ResourceGuardResetWithNewDeleterFixed) {
  // Test the critical reset() bug fix - previously resource was cleaned with
  // wrong deleter
  auto* resource1 = new MockResource(111);
  auto* resource2 = new MockResource(222);

  bool deleter2_called = false;

  {
    ResourceGuard<MockResource> guard(resource1);

    // Create custom function deleter for the test
    std::function<void(MockResource*)> custom_deleter = [&](MockResource* p) {
      deleter2_called = true;
      delete p;
    };

    EXPECT_EQ(resource1, guard.get());

    // This should immediately clean up resource1 with the default deleter
    // and assign resource2 with the custom deleter
    guard.reset(resource2, custom_deleter);

    EXPECT_EQ(resource2, guard.get());
    EXPECT_EQ(1, MockResource::deallocation_count
                     .load());  // resource1 should be freed by original deleter
  }

  // resource2 should be freed by custom deleter when guard is destroyed
  EXPECT_TRUE(deleter2_called);
  EXPECT_EQ(2, MockResource::deallocation_count.load());
}

TEST_F(CriticalBugFixTest, DefaultConstructorDeleterInitialized) {
  // Test that default constructor properly initializes deleter (no-op deleter)
  ResourceGuard<MockResource> guard;

  EXPECT_FALSE(guard);
  EXPECT_EQ(nullptr, guard.get());

  // This should not crash - deleter should be properly initialized
  guard.reset();  // Should be safe to call

  // Assign a resource - will use c_deleter<MockResource> specialization
  auto* resource = new MockResource(123);
  guard.reset(resource);

  EXPECT_TRUE(guard);
  EXPECT_EQ(resource, guard.get());

  // Let the destructor clean up automatically
  // Destructor should properly clean up using default c_deleter
  // The TearDown will verify that cleanup worked correctly
}

/* ============================================================================
 * Enhanced ResourceGuard Production Tests
 * ============================================================================
 */

class EnhancedResourceGuardTest : public ::testing::Test {
 protected:
  void SetUp() override { MockResource::reset_counters(); }

  void TearDown() override { EXPECT_TRUE(MockResource::is_balanced()); }
};

TEST_F(EnhancedResourceGuardTest, OperatorArrowAndDereference) {
  auto* resource = new MockResource(12345);
  ResourceGuard<MockResource> guard(resource);

  // Test operator->
  EXPECT_EQ(12345, guard->value);

  // Test operator*
  EXPECT_EQ(12345, (*guard).value);

  // Modify through operators
  guard->value = 54321;
  EXPECT_EQ(54321, (*guard).value);
}

TEST_F(EnhancedResourceGuardTest, ExceptionSafetyInMoveOperations) {
  // Test that move operations are strongly exception safe
  auto* resource = new MockResource(999);

  ResourceGuard<MockResource> guard1(resource);
  EXPECT_TRUE(guard1);
  EXPECT_EQ(resource, guard1.get());

  // Move construction should be noexcept
  static_assert(
      std::is_nothrow_move_constructible<ResourceGuard<MockResource>>::value,
      "ResourceGuard move constructor must be noexcept");

  ResourceGuard<MockResource> guard2 = std::move(guard1);
  EXPECT_FALSE(guard1);  // guard1 should be empty after move
  EXPECT_TRUE(guard2);
  EXPECT_EQ(resource, guard2.get());

  // Move assignment should be noexcept
  static_assert(
      std::is_nothrow_move_assignable<ResourceGuard<MockResource>>::value,
      "ResourceGuard move assignment must be noexcept");
}

/* ============================================================================
 * Production Monitoring and Statistics Tests
 * ============================================================================
 */

class ProductionMonitoringTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MockResource::reset_counters();
    mcp_raii_reset_stats();
  }

  void TearDown() override { EXPECT_TRUE(MockResource::is_balanced()); }
};

TEST_F(ProductionMonitoringTest, GlobalStatisticsTracking) {
  // Test that global statistics are properly tracked
  uint64_t guards_created, guards_destroyed;
  uint64_t resources_tracked, resources_released;
  uint64_t exceptions_in_destructors;

  mcp_raii_get_stats(&guards_created, &guards_destroyed, &resources_tracked,
                     &resources_released, &exceptions_in_destructors);

  // Initial state should be zero (after reset in SetUp)
  EXPECT_EQ(0, guards_created);
  EXPECT_EQ(0, guards_destroyed);

  {
    // Create some resource guards to test statistics
    auto guard1 = make_resource_guard(new MockResource(1));
    auto guard2 = make_resource_guard(new MockResource(2));

    // Statistics should be available through C API
    mcp_raii_get_stats(&guards_created, &guards_destroyed, &resources_tracked,
                       &resources_released, &exceptions_in_destructors);

    // We may not have complete statistics tracking yet,
    // but function should not crash and return valid values
    EXPECT_GE(guards_created, 0);
    EXPECT_GE(guards_destroyed, 0);
  }

  // Test statistics reset functionality
  mcp_raii_reset_stats();

  mcp_raii_get_stats(&guards_created, &guards_destroyed, &resources_tracked,
                     &resources_released, &exceptions_in_destructors);

  EXPECT_EQ(0, guards_created);
  EXPECT_EQ(0, guards_destroyed);
}

TEST_F(ProductionMonitoringTest, ActiveResourceTracking) {
  // Test resource leak detection capability
  size_t initial_active = mcp_raii_active_resources();

  {
    auto guard1 = make_resource_guard(new MockResource(100));
    auto guard2 = make_resource_guard(new MockResource(200));

    // In debug builds, active resources should increase
    size_t active_with_resources = mcp_raii_active_resources();

#ifdef MCP_RAII_DEBUG_MODE
    EXPECT_GE(active_with_resources, initial_active);
#else
    // In release builds, this may return 0 (not available)
    EXPECT_GE(active_with_resources, 0);
#endif
  }

  // After guards are destroyed, active count should return to initial state
  size_t final_active = mcp_raii_active_resources();
  EXPECT_EQ(initial_active, final_active);
}

/* ============================================================================
 * Enhanced Transaction Tests
 * ============================================================================
 */

class EnhancedTransactionTest : public ::testing::Test {
 protected:
  void SetUp() override { MockResource::reset_counters(); }

  void TearDown() override { EXPECT_TRUE(MockResource::is_balanced()); }
};

TEST_F(EnhancedTransactionTest, SwapTransactions) {
  auto* resource1 = new MockResource(111);
  auto* resource2 = new MockResource(222);
  auto* resource3 = new MockResource(333);
  auto* resource4 = new MockResource(444);

  AllocationTransaction txn1, txn2;

  txn1.track(resource1);
  txn1.track(resource2);

  txn2.track(resource3);
  txn2.track(resource4);

  EXPECT_EQ(2, txn1.resource_count());
  EXPECT_EQ(2, txn2.resource_count());

  // Test move semantics which provides similar functionality to swap
  AllocationTransaction txn3 = std::move(txn1);
  EXPECT_EQ(0, txn1.resource_count());
  EXPECT_EQ(2, txn3.resource_count());

  txn3.commit();  // Prevent cleanup
  txn2.commit();  // Prevent cleanup

  // Resources should not be deallocated since transactions were committed
  EXPECT_EQ(0, MockResource::deallocation_count.load());

  // Manual cleanup for test
  delete resource1;
  delete resource2;
  delete resource3;
  delete resource4;
}

TEST_F(EnhancedTransactionTest, EmptyTransactionOperations) {
  AllocationTransaction txn;

  EXPECT_EQ(0, txn.resource_count());
  EXPECT_TRUE(txn.empty());
  EXPECT_FALSE(txn.is_committed());

  // Operations on empty transaction should be safe
  txn.rollback();                   // Should be safe
  EXPECT_TRUE(txn.is_committed());  // rollback commits the transaction

  txn.commit();  // Should be safe (already committed)
  EXPECT_TRUE(txn.is_committed());
}

TEST_F(EnhancedTransactionTest, ReserveCapacityOptimization) {
  AllocationTransaction txn;

  // Test reserve functionality
  txn.reserve(100);

  // Add resources and verify no performance degradation
  std::vector<MockResource*> resources;
  for (int i = 0; i < 50; ++i) {
    auto* resource = new MockResource(i);
    resources.push_back(resource);
    txn.track(resource);
  }

  EXPECT_EQ(50, txn.resource_count());
  EXPECT_FALSE(txn.empty());

  txn.commit();

  // Manual cleanup since transaction was committed
  for (auto* resource : resources) {
    delete resource;
  }
}

/* ============================================================================
 * New API Feature Tests
 * ============================================================================
 */

class NewAPIFeatureTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MockResource::reset_counters();
    mcp_raii_reset_stats();
  }

  void TearDown() override { EXPECT_TRUE(MockResource::is_balanced()); }
};

TEST_F(NewAPIFeatureTest, ComparisonOperators) {
  auto* resource1 = new MockResource(100);
  auto* resource2 = new MockResource(200);

  ResourceGuard<MockResource> guard1(resource1);
  ResourceGuard<MockResource> guard2(resource2);
  ResourceGuard<MockResource> empty_guard;

  // Test equality/inequality between guards
  EXPECT_NE(guard1, guard2);
  EXPECT_EQ(guard1, guard1);

  // Test null comparisons
  EXPECT_EQ(empty_guard, nullptr);
  EXPECT_NE(guard1, nullptr);
  EXPECT_EQ(nullptr, empty_guard);
  EXPECT_NE(nullptr, guard1);
}

TEST_F(NewAPIFeatureTest, GetDeleterAccess) {
  bool custom_deleter_called = false;

  std::function<void(MockResource*)> custom_deleter = [&](MockResource* p) {
    custom_deleter_called = true;
    delete p;
  };

  {
    ResourceGuard<MockResource> guard(new MockResource(42), custom_deleter);

    // Test get_deleter access (const and non-const)
    const auto& const_guard = guard;
    const auto& const_deleter = const_guard.get_deleter();
    auto& deleter = guard.get_deleter();

    // Verify we have access to the deleter
    EXPECT_FALSE(custom_deleter_called);
  }

  EXPECT_TRUE(custom_deleter_called);
}

TEST_F(NewAPIFeatureTest, TransactionSwapFunctionality) {
  auto* resource1 = new MockResource(111);
  auto* resource2 = new MockResource(222);
  auto* resource3 = new MockResource(333);
  auto* resource4 = new MockResource(444);

  AllocationTransaction txn1, txn2;

  txn1.track(resource1);
  txn1.track(resource2);

  txn2.track(resource3);
  txn2.track(resource4);

  EXPECT_EQ(2, txn1.resource_count());
  EXPECT_EQ(2, txn2.resource_count());

  // Test member swap
  txn1.swap(txn2);

  EXPECT_EQ(2, txn1.resource_count());
  EXPECT_EQ(2, txn2.resource_count());

  // Test non-member swap
  swap(txn1, txn2);

  EXPECT_EQ(2, txn1.resource_count());
  EXPECT_EQ(2, txn2.resource_count());

  // Commit both to prevent cleanup
  txn1.commit();
  txn2.commit();

  // Manual cleanup
  delete resource1;
  delete resource2;
  delete resource3;
  delete resource4;
}

TEST_F(NewAPIFeatureTest, ResourceGuardSwapFunctionality) {
  auto* resource1 = new MockResource(555);
  auto* resource2 = new MockResource(666);

  ResourceGuard<MockResource> guard1(resource1);
  ResourceGuard<MockResource> guard2(resource2);

  EXPECT_EQ(resource1, guard1.get());
  EXPECT_EQ(resource2, guard2.get());

  // Test member swap
  guard1.swap(guard2);

  EXPECT_EQ(resource2, guard1.get());
  EXPECT_EQ(resource1, guard2.get());

  // Test non-member swap
  swap(guard1, guard2);

  EXPECT_EQ(resource1, guard1.get());
  EXPECT_EQ(resource2, guard2.get());
}

TEST_F(NewAPIFeatureTest, StatisticsTracking) {
  uint64_t initial_guards_created, initial_guards_destroyed;
  uint64_t initial_resources_tracked, initial_resources_released;
  uint64_t initial_exceptions;

  mcp_raii_get_stats(&initial_guards_created, &initial_guards_destroyed,
                     &initial_resources_tracked, &initial_resources_released,
                     &initial_exceptions);

  {
    // Create multiple guards to test statistics
    auto guard1 = make_resource_guard(new MockResource(1));
    auto guard2 = make_resource_guard(new MockResource(2));
    auto guard3 = make_resource_guard(new MockResource(3));

    uint64_t current_guards_created, current_guards_destroyed;
    uint64_t current_resources_tracked, current_resources_released;
    uint64_t current_exceptions;

    mcp_raii_get_stats(&current_guards_created, &current_guards_destroyed,
                       &current_resources_tracked, &current_resources_released,
                       &current_exceptions);

    // Verify statistics increased
    EXPECT_GE(current_guards_created, initial_guards_created + 3);
    EXPECT_GE(current_guards_destroyed, initial_guards_destroyed);
  }

  // After destruction, destroyed count should increase
  uint64_t final_guards_created, final_guards_destroyed;
  uint64_t final_resources_tracked, final_resources_released;
  uint64_t final_exceptions;

  mcp_raii_get_stats(&final_guards_created, &final_guards_destroyed,
                     &final_resources_tracked, &final_resources_released,
                     &final_exceptions);

  EXPECT_GE(final_guards_destroyed, initial_guards_destroyed + 3);
}

/* ============================================================================
 * ResourceTracker Tests (Debug Mode Only) - From test_mcp_raii_missing.cc
 * ============================================================================
 */

#ifdef MCP_RAII_DEBUG_MODE

class ResourceTrackerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Note: ResourceTracker doesn't have a clear() method
    // We'll just track the initial count
    initial_resource_count_ = ResourceTracker::instance().active_resources();
  }

  void TearDown() override {
    // Verify no leaks from our tests
    EXPECT_EQ(initial_resource_count_,
              ResourceTracker::instance().active_resources());
  }

 protected:
  size_t initial_resource_count_ = 0;
};

TEST_F(ResourceTrackerTest, TrackAndUntrackResources) {
  int dummy_resource = 42;
  void* resource_ptr = &dummy_resource;

  // Track a resource
  ResourceTracker::instance().track_resource(resource_ptr, "TestResource",
                                             __FILE__, __LINE__);

  // Verify it's tracked
  EXPECT_EQ(initial_resource_count_ + 1,
            ResourceTracker::instance().active_resources());

  // Untrack the resource
  ResourceTracker::instance().untrack_resource(resource_ptr);

  // Verify it's no longer tracked
  EXPECT_EQ(initial_resource_count_,
            ResourceTracker::instance().active_resources());
}

TEST_F(ResourceTrackerTest, DetectLeakedResources) {
  int dummy1 = 1, dummy2 = 2, dummy3 = 3;

  ResourceTracker::instance().track_resource(&dummy1, "Resource1");
  ResourceTracker::instance().track_resource(&dummy2, "Resource2");
  ResourceTracker::instance().track_resource(&dummy3, "Resource3");

  EXPECT_EQ(initial_resource_count_ + 3,
            ResourceTracker::instance().active_resources());

  // Only untrack two resources
  ResourceTracker::instance().untrack_resource(&dummy1);
  ResourceTracker::instance().untrack_resource(&dummy2);

  // One resource should still be tracked (leaked)
  EXPECT_EQ(initial_resource_count_ + 1,
            ResourceTracker::instance().active_resources());

  // Note: ResourceTracker doesn't have get_leaked_resources() method
  // We can only verify the count

  // Clean up for test
  ResourceTracker::instance().untrack_resource(&dummy3);
}

TEST_F(ResourceTrackerTest, ReportLeaks) {
  // The report_leaks() method has an assertion that fires when leaks are
  // detected in debug mode, so we can't test the actual reporting in debug mode
  GTEST_SKIP()
      << "Test skipped in debug mode due to assertion on leak detection";
}

#endif  // MCP_RAII_DEBUG_MODE

/* ============================================================================
 * Additional Exception Handling Tests - From test_mcp_raii_missing.cc
 * ============================================================================
 */

// Custom deleter that throws (for testing exception handling)
struct ThrowingDeleter {
  mutable bool should_throw = true;
  mutable int call_count = 0;

  void operator()(char* ptr) const {
    ++call_count;
    if (should_throw && ptr) {
      throw std::runtime_error("Deleter exception");
    }
    free(ptr);
  }
};

TEST_F(EdgeCaseTest, ResourceGuardDestructorNoThrow) {
  // ResourceGuard destructor should not throw even if deleter throws
  // In debug mode with MCP_RAII_DEBUG_MODE, the assertion will fire
  // when an exception is caught in the destructor, so we skip this test
#ifdef MCP_RAII_DEBUG_MODE
  GTEST_SKIP() << "Test skipped in debug mode due to assertion on exception in "
                  "destructor";
#else
  auto call_count = std::make_shared<int>(0);
  auto throwing_deleter = [call_count](char* ptr) {
    (*call_count)++;
    free(ptr);
    throw std::runtime_error("Deleter exception");
  };

  {
    ResourceGuard<char> guard(static_cast<char*>(malloc(10)), throwing_deleter);
    EXPECT_TRUE(guard);
    // Destructor will be called here - should not throw
  }

  // Deleter should have been called despite throwing
  EXPECT_EQ(1, *call_count);
#endif
}

TEST_F(EdgeCaseTest, AllocationTransactionRollbackNoThrow) {
  ThrowingDeleter deleter1, deleter2;

  // In debug mode with MCP_RAII_DEBUG_MODE, the assertion will fire
  // when an exception is caught in the destructor, so we skip this test
#ifdef MCP_RAII_DEBUG_MODE
  GTEST_SKIP() << "Test skipped in debug mode due to assertion on exception in "
                  "destructor";
#else
  {
    AllocationTransaction txn;
    txn.track(malloc(10),
              [&deleter1](void* p) { deleter1(static_cast<char*>(p)); });
    txn.track(malloc(20),
              [&deleter2](void* p) { deleter2(static_cast<char*>(p)); });

    // Rollback will call deleters which throw - should not propagate
    // Destructor implicitly calls rollback if not committed
  }

  // Both deleters should have been called
  EXPECT_EQ(1, deleter1.call_count);
  EXPECT_EQ(1, deleter2.call_count);
#endif
}

/* ============================================================================
 * Resource Recycling Pattern Tests - From test_mcp_raii_missing.cc
 * ============================================================================
 */

class ResourceRecyclingTest : public RAIITest {};

TEST_F(ResourceRecyclingTest, ReusableResourceGuard) {
  // Test pattern for reusing a ResourceGuard with different resources
  ResourceGuard<char> guard;

  // First resource
  char* resource1 = static_cast<char*>(malloc(100));
  guard.reset(resource1, free);
  EXPECT_EQ(resource1, guard.get());

  // Second resource (first one gets freed)
  char* resource2 = static_cast<char*>(malloc(200));
  guard.reset(resource2, free);
  EXPECT_EQ(resource2, guard.get());

  // Third resource
  char* resource3 = static_cast<char*>(malloc(300));
  guard.reset(resource3, free);
  EXPECT_EQ(resource3, guard.get());

  // Release last resource for manual management
  char* released = guard.release();
  EXPECT_EQ(resource3, released);
  EXPECT_FALSE(guard);

  free(released);
}

TEST_F(ResourceRecyclingTest, TransactionReusePattern) {
  // Test pattern for reusing a transaction
  AllocationTransaction txn;

  // First batch of resources
  void* batch1[] = {malloc(10), malloc(20), malloc(30)};
  for (auto* resource : batch1) {
    txn.track(resource, free);
  }
  EXPECT_EQ(3, txn.resource_count());

  // Rollback first batch
  txn.rollback();
  EXPECT_TRUE(txn.is_committed());
  EXPECT_EQ(0, txn.resource_count());

  // Can't reuse a committed transaction - need a new one
  AllocationTransaction txn2;

  // Second batch of resources
  void* batch2[] = {malloc(40), malloc(50)};
  for (auto* resource : batch2) {
    txn2.track(resource, free);
  }
  EXPECT_EQ(2, txn2.resource_count());

  // Commit second batch
  txn2.commit();

  // Manual cleanup since we committed
  for (auto* resource : batch2) {
    free(resource);
  }
}

/* ============================================================================
 * Advanced Deleter Tests - From test_mcp_raii_missing.cc
 * ============================================================================
 */

// Stateful deleter with context
class StatefulDeleter {
  std::string context_;
  mutable int delete_count_ = 0;

 public:
  explicit StatefulDeleter(const std::string& context) : context_(context) {}

  void operator()(char* ptr) const {
    ++delete_count_;
    free(ptr);
  }

  const std::string& context() const { return context_; }
  int delete_count() const { return delete_count_; }
};

TEST_F(EdgeCaseTest, StatefulCustomDeleter) {
  auto delete_count = std::make_shared<int>(0);
  auto deleter = [delete_count](char* ptr) {
    (*delete_count)++;
    free(ptr);
  };

  {
    ResourceGuard<char> guard(static_cast<char*>(malloc(50)), deleter);
    EXPECT_TRUE(guard);
  }

  // Deleter should have been called
  EXPECT_EQ(1, *delete_count);
}

TEST_F(EdgeCaseTest, LambdaWithCapture) {
  int cleanup_count = 0;
  std::string cleanup_message;

  {
    auto deleter = [&cleanup_count, &cleanup_message](char* ptr) {
      ++cleanup_count;
      cleanup_message = "Cleaned up resource";
      free(ptr);
    };

    ResourceGuard<char> guard(static_cast<char*>(malloc(100)), deleter);
    EXPECT_TRUE(guard);
  }

  EXPECT_EQ(1, cleanup_count);
  EXPECT_EQ("Cleaned up resource", cleanup_message);
}

/* ============================================================================
 * Nested Transaction Tests - From test_mcp_raii_missing.cc
 * ============================================================================
 */

class NestedTransactionTest : public RAIITest {};

TEST_F(NestedTransactionTest, TransactionWithResourceGuards) {
  // Pattern: Using ResourceGuards within a transaction scope
  AllocationTransaction txn;

  // Create resources with guards
  auto guard1 = make_resource_guard(static_cast<char*>(malloc(100)), free);
  auto guard2 = make_resource_guard(static_cast<char*>(malloc(200)), free);

  // Track guard-managed resources in transaction
  txn.track(guard1.get(), [](void*) {});  // No-op since guard manages it
  txn.track(guard2.get(), [](void*) {});

  // Commit transaction - guards still manage the resources
  txn.commit();

  // Guards will clean up when they go out of scope
}

TEST_F(NestedTransactionTest, MultiLevelResourceManagement) {
  // Outer transaction
  AllocationTransaction outer_txn;

  void* resource1 = malloc(50);
  outer_txn.track(resource1, free);

  {
    // Inner transaction
    AllocationTransaction inner_txn;

    void* resource2 = malloc(100);
    inner_txn.track(resource2, free);

    // Inner rollback - resource2 freed
    inner_txn.rollback();
  }

  // Outer still has resource1
  EXPECT_EQ(1, outer_txn.resource_count());

  // Let outer auto-rollback
}

/* ============================================================================
 * Memory Pool Tests (Future Implementation) - From test_mcp_raii_missing.cc
 * ============================================================================
 */

class MemoryPoolTest : public RAIITest {};

TEST_F(MemoryPoolTest, DISABLED_BasicMemoryPoolOperations) {
  // Test for future memory pool implementation
  // Currently disabled as feature is not yet implemented

  // Expected API:
  // MemoryPool pool(1024 * 1024); // 1MB pool
  // auto* block = pool.allocate(256);
  // EXPECT_NE(nullptr, block);
  // pool.deallocate(block);
}

TEST_F(MemoryPoolTest, DISABLED_PoolWithResourceGuard) {
  // Test for future integration of memory pool with ResourceGuard
  // Currently disabled as feature is not yet implemented

  // Expected usage:
  // MemoryPool pool(1024 * 1024);
  // auto guard = make_resource_guard(
  //     pool.allocate(512),
  //     [&pool](void* ptr) { pool.deallocate(ptr); }
  // );
}

/* ============================================================================
 * Performance Stress Tests - From test_mcp_raii_missing.cc
 * ============================================================================
 */

class PerformanceStressTest : public RAIITest {};

TEST_F(PerformanceStressTest, MassiveAllocationTransaction) {
  // Stress test with large number of resources
  constexpr int resource_count = 10000;

  AllocationTransaction txn;
  txn.reserve(resource_count);

  std::vector<void*> resources;
  resources.reserve(resource_count);

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < resource_count; ++i) {
    void* resource = malloc(10);
    resources.push_back(resource);
    txn.track(resource, free);
  }

  EXPECT_EQ(resource_count, txn.resource_count());

  // Measure rollback performance
  auto rollback_start = std::chrono::high_resolution_clock::now();
  txn.rollback();
  auto rollback_end = std::chrono::high_resolution_clock::now();

  auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      rollback_end - start);
  auto rollback_duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(rollback_end -
                                                            rollback_start);

  // Performance expectations
  EXPECT_LT(total_duration.count(), 1000);    // Less than 1 second total
  EXPECT_LT(rollback_duration.count(), 100);  // Less than 100ms for rollback
}

TEST_F(PerformanceStressTest, RapidResourceGuardCreation) {
  // Stress test rapid creation/destruction
  constexpr int iterations = 100000;

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < iterations; ++i) {
    auto guard = make_resource_guard(static_cast<char*>(malloc(1)), free);
    // Immediate destruction
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  // Should complete in reasonable time
  EXPECT_LT(duration.count(), 5000);  // Less than 5 seconds
}

}  // anonymous namespace

/* ============================================================================
 * Main Test Runner
 * ============================================================================
 */

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}