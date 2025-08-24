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

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <memory>

#define MCP_RAII_IMPLEMENTATION
#include "mcp/c_api/mcp_raii.h"
// Note: Avoiding mcp_c_types.h to prevent header conflicts

using namespace mcp::raii;

namespace {

/* ============================================================================
 * Test Fixtures and Utilities
 * ============================================================================ */

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
            << ", deallocations=" 
            << MockResource::deallocation_count.load();
    }
};

/* ============================================================================
 * ResourceGuard Tests
 * ============================================================================ */

class ResourceGuardTest : public RAIITest {};

TEST_F(ResourceGuardTest, DefaultConstruction) {
    ResourceGuard<MockResource> guard;
    
    EXPECT_FALSE(guard);
    EXPECT_EQ(nullptr, guard.get());
}

TEST_F(ResourceGuardTest, BasicResourceManagement) {
    auto* resource = new MockResource(123);
    
    {
        ResourceGuard<MockResource> guard(resource, [](MockResource* p) { delete p; });
        
        EXPECT_TRUE(guard);
        EXPECT_EQ(resource, guard.get());
        EXPECT_EQ(123, guard.get()->value);
    } // guard destructor should delete resource
    
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
    
    ResourceGuard<MockResource> guard1(resource, [](MockResource* p) { delete p; });
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
    } // No cleanup should occur
    
    EXPECT_EQ(0, MockResource::deallocation_count.load());
    
    // Manual cleanup
    delete released;
    EXPECT_EQ(1, MockResource::deallocation_count.load());
}

TEST_F(ResourceGuardTest, Reset) {
    auto* resource1 = new MockResource(111);
    auto* resource2 = new MockResource(222);
    
    ResourceGuard<MockResource> guard(resource1, [](MockResource* p) { delete p; });
    EXPECT_EQ(resource1, guard.get());
    
    // Reset with new resource
    guard.reset(resource2);
    EXPECT_EQ(resource2, guard.get());
    EXPECT_EQ(1, MockResource::deallocation_count.load()); // resource1 deleted
    
    // Reset to null
    guard.reset();
    EXPECT_FALSE(guard);
    EXPECT_EQ(nullptr, guard.get());
    EXPECT_EQ(2, MockResource::deallocation_count.load()); // resource2 deleted
}

TEST_F(ResourceGuardTest, Swap) {
    auto* resource1 = new MockResource(333);
    auto* resource2 = new MockResource(444);
    
    ResourceGuard<MockResource> guard1(resource1, [](MockResource* p) { delete p; });
    ResourceGuard<MockResource> guard2(resource2, [](MockResource* p) { delete p; });
    
    guard1.swap(guard2);
    
    EXPECT_EQ(resource2, guard1.get());
    EXPECT_EQ(resource1, guard2.get());
}

TEST_F(ResourceGuardTest, MakeResourceGuard) {
    auto* resource = new MockResource(555);
    
    {
        auto guard = make_resource_guard(resource, [](MockResource* p) { delete p; });
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
 * ============================================================================ */

class AllocationTransactionTest : public RAIITest {};

TEST_F(AllocationTransactionTest, CommitTransaction) {
    auto* resource1 = new MockResource(777);
    auto* resource2 = new MockResource(888);
    
    {
        AllocationTransaction txn;
        txn.track(resource1, [](void* ptr) { delete static_cast<MockResource*>(ptr); });
        txn.track(resource2, [](void* ptr) { delete static_cast<MockResource*>(ptr); });
        
        EXPECT_EQ(2u, txn.resource_count());
        EXPECT_FALSE(txn.is_committed());
        
        txn.commit();
        EXPECT_TRUE(txn.is_committed());
        EXPECT_EQ(0u, txn.resource_count());
    } // No cleanup should occur on destruction
    
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
        txn.track(resource1, [](void* ptr) { delete static_cast<MockResource*>(ptr); });
        txn.track(resource2, [](void* ptr) { delete static_cast<MockResource*>(ptr); });
        
        EXPECT_EQ(2u, txn.resource_count());
        EXPECT_FALSE(txn.is_committed());
        
        // Don't commit - should rollback automatically
    }
    
    EXPECT_EQ(2, MockResource::deallocation_count.load());
}

TEST_F(AllocationTransactionTest, ManualRollback) {
    auto* resource = new MockResource(1111);
    
    AllocationTransaction txn;
    txn.track(resource, [](void* ptr) { delete static_cast<MockResource*>(ptr); });
    
    EXPECT_FALSE(txn.is_committed());
    
    txn.rollback();
    EXPECT_TRUE(txn.is_committed());
    EXPECT_EQ(1, MockResource::deallocation_count.load());
}

TEST_F(AllocationTransactionTest, TypedTracking) {
    auto* resource = new MockResource(1212);
    
    {
        AllocationTransaction txn;
        txn.track(resource, [](void* p) { delete static_cast<MockResource*>(p); }); // Uses proper deleter
        
        EXPECT_EQ(1u, txn.resource_count());
        // Don't commit - should auto-rollback
    }
    
    EXPECT_EQ(1, MockResource::deallocation_count.load());
}

TEST_F(AllocationTransactionTest, MoveSemantics) {
    auto* resource = new MockResource(1313);
    
    AllocationTransaction txn1;
    txn1.track(resource, [](void* ptr) { delete static_cast<MockResource*>(ptr); });
    
    // Move construction
    AllocationTransaction txn2 = std::move(txn1);
    EXPECT_EQ(1u, txn2.resource_count());
    EXPECT_EQ(0u, txn1.resource_count());
    EXPECT_TRUE(txn1.is_committed()); // Moved-from should be committed
    
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
 * ============================================================================ */

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
                    auto guard = make_resource_guard(resource, [](MockResource* p) { delete p; });
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
                txn.track(resource, [](void* p) { delete static_cast<MockResource*>(p); });
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
    int expected_deallocations = rolled_back_transactions.load() * resources_per_thread;
    EXPECT_EQ(expected_deallocations, MockResource::deallocation_count.load());
}

/* ============================================================================
 * ScopedCleanup Tests
 * ============================================================================ */

class ScopedCleanupTest : public RAIITest {};

TEST_F(ScopedCleanupTest, BasicCleanup) {
    bool cleanup_called = false;
    
    {
        auto cleanup = make_scoped_cleanup([&]() {
            cleanup_called = true;
        });
        
        EXPECT_TRUE(cleanup.is_active());
        EXPECT_FALSE(cleanup_called);
    }
    
    EXPECT_TRUE(cleanup_called);
}

TEST_F(ScopedCleanupTest, ReleaseCleanup) {
    bool cleanup_called = false;
    
    {
        auto cleanup = make_scoped_cleanup([&]() {
            cleanup_called = true;
        });
        
        cleanup.release();
        EXPECT_FALSE(cleanup.is_active());
    }
    
    EXPECT_FALSE(cleanup_called);
}

TEST_F(ScopedCleanupTest, MoveSemantics) {
    bool cleanup_called = false;
    
    {
        auto cleanup1 = make_scoped_cleanup([&]() {
            cleanup_called = true;
        });
        
        auto cleanup2 = std::move(cleanup1);
        EXPECT_FALSE(cleanup1.is_active());
        EXPECT_TRUE(cleanup2.is_active());
    }
    
    EXPECT_TRUE(cleanup_called);
}

/* ============================================================================
 * Performance Tests
 * ============================================================================ */

class PerformanceTest : public RAIITest {};

TEST_F(PerformanceTest, ResourceGuardPerformance) {
    constexpr int iterations = 100000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        auto* resource = new MockResource(i);
        auto guard = make_resource_guard(resource, [](MockResource* p) { delete p; });
        // Guard destructor cleans up automatically
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Performance should be reasonable (less than 1 microsecond per operation)
    EXPECT_LT(duration.count(), iterations);
    
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
            txn.track(resource, [](void* p) { delete static_cast<MockResource*>(p); });
        }
        
        // All transactions auto-rollback for consistent memory behavior
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Performance should be reasonable (less than 100ms total)
    EXPECT_LT(duration.count(), 100);
}

/* ============================================================================
 * Integration Tests with Generic C Types
 * ============================================================================ */

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
 * ============================================================================ */

class EdgeCaseTest : public RAIITest {};

TEST_F(EdgeCaseTest, ExceptionDuringCleanup) {
    // Test that exceptions during cleanup don't propagate
    bool exception_thrown = false;
    
    {
        auto cleanup = make_scoped_cleanup([&]() {
            exception_thrown = true;
            throw std::runtime_error("Cleanup exception");
        });
    } // Should not throw
    
    EXPECT_TRUE(exception_thrown);
    // Test should complete without throwing
}

TEST_F(EdgeCaseTest, SelfAssignment) {
    auto* resource = new MockResource(1414);
    
    ResourceGuard<MockResource> guard(resource, [](MockResource* p) { delete p; });
    
    // Self-assignment should be safe
    guard = std::move(guard);
    
    EXPECT_TRUE(guard);
    EXPECT_EQ(resource, guard.get());
}

TEST_F(EdgeCaseTest, DoubleFreeProtection) {
    auto* resource = new MockResource(1515);
    
    ResourceGuard<MockResource> guard(resource, [](MockResource* p) { delete p; });
    
    // Manual reset should prevent double-free
    guard.reset();
    EXPECT_FALSE(guard);
    
    guard.reset(); // Should be safe to call again
    
    EXPECT_EQ(1, MockResource::deallocation_count.load());
}

} // anonymous namespace

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}