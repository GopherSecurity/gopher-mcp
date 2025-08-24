/**
 * @file test_mcp_raii_simple.cc
 * @brief Simplified unit tests for MCP RAII utilities
 *
 * Tests the core RAII functionality without C API type conflicts.
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
    
    int value;
    
    MockResource(int val = 42) : value(val) {
        allocation_count.fetch_add(1, std::memory_order_relaxed);
    }
    
    ~MockResource() {
        deallocation_count.fetch_add(1, std::memory_order_relaxed);
    }
    
    static void reset_counters() {
        allocation_count.store(0, std::memory_order_relaxed);
        deallocation_count.store(0, std::memory_order_relaxed);
    }
    
    static bool is_balanced() {
        return allocation_count.load(std::memory_order_relaxed) == 
               deallocation_count.load(std::memory_order_relaxed);
    }
};

// Static member definitions
std::atomic<int> MockResource::allocation_count{0};
std::atomic<int> MockResource::deallocation_count{0};

/**
 * Test fixture for RAII tests
 */
class RAIISimpleTest : public ::testing::Test {
protected:
    void SetUp() override {
        MockResource::reset_counters();
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

TEST_F(RAIISimpleTest, BasicResourceManagement) {
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

TEST_F(RAIISimpleTest, MoveSemantics) {
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
    
    // Only one deallocation should occur when guard2 is destroyed
}

TEST_F(RAIISimpleTest, CustomDeleter) {
    auto* resource = new MockResource(456);
    bool deleter_called = false;
    
    {
        ResourceGuard<MockResource> guard(resource, [&deleter_called](MockResource* ptr) {
            deleter_called = true;
            delete ptr;
        });
        EXPECT_TRUE(guard);
        EXPECT_EQ(resource, guard.get());
    }
    
    EXPECT_TRUE(deleter_called);
    EXPECT_EQ(1, MockResource::deallocation_count.load());
}

TEST_F(RAIISimpleTest, MakeResourceGuard) {
    auto* resource = new MockResource(555);
    
    {
        auto guard = make_resource_guard(resource, [](MockResource* p) { delete p; });
        EXPECT_TRUE(guard);
        EXPECT_EQ(resource, guard.get());
    }
    
    EXPECT_EQ(1, MockResource::deallocation_count.load());
}

/* ============================================================================
 * AllocationTransaction Tests
 * ============================================================================ */

TEST_F(RAIISimpleTest, CommitTransaction) {
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

TEST_F(RAIISimpleTest, RollbackTransaction) {
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

TEST_F(RAIISimpleTest, TypedTracking) {
    auto* resource = new MockResource(1212);
    
    {
        AllocationTransaction txn;
        txn.track(resource, [](void* ptr) { delete static_cast<MockResource*>(ptr); });
        
        EXPECT_EQ(1u, txn.resource_count());
        // Don't commit - should auto-rollback
    }
    
    EXPECT_EQ(1, MockResource::deallocation_count.load());
}

/* ============================================================================
 * ScopedCleanup Tests
 * ============================================================================ */

TEST_F(RAIISimpleTest, BasicCleanup) {
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

TEST_F(RAIISimpleTest, ReleaseCleanup) {
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

/* ============================================================================
 * Thread Safety Tests
 * ============================================================================ */

TEST_F(RAIISimpleTest, ConcurrentResourceGuardOperations) {
    constexpr int num_threads = 5;
    constexpr int operations_per_thread = 50;
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([i]() {
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
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // All resources should be properly cleaned up
    EXPECT_EQ(num_threads * operations_per_thread, 
              MockResource::allocation_count.load());
    EXPECT_EQ(num_threads * operations_per_thread, 
              MockResource::deallocation_count.load());
}

/* ============================================================================
 * Performance Tests
 * ============================================================================ */

TEST_F(RAIISimpleTest, ResourceGuardPerformance) {
    constexpr int iterations = 10000;
    
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

} // anonymous namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}