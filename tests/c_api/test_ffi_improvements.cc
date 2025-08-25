/**
 * @file test_ffi_improvements.cc
 * @brief Comprehensive tests for improved FFI-safe C API with RAII integration
 *
 * This test suite validates:
 * - FFI safety of all types
 * - RAII integration with ResourceGuard and AllocationTransaction
 * - Reference counting functionality
 * - Memory pool operations
 * - Error context handling
 * - Thread safety of operations
 */

#include <gtest/gtest.h>
#include "mcp/c_api/mcp_ffi_core.h"
#include "mcp/c_api/mcp_c_types_improved.h"
#include "mcp/c_api/mcp_raii_integration.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

using namespace mcp::raii;

class FFIImprovementsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize FFI library
        ASSERT_EQ(mcp_ffi_initialize(nullptr), MCP_OK);
        ASSERT_TRUE(mcp_ffi_is_initialized());
    }
    
    void TearDown() override {
        // Shutdown FFI library
        ASSERT_EQ(mcp_ffi_shutdown(), MCP_OK);
    }
};

/* ============================================================================
 * FFI Safety Tests
 * ============================================================================ */

TEST_F(FFIImprovementsTest, FFISafePrimitiveTypes) {
    // Verify fixed sizes for cross-platform compatibility
    EXPECT_EQ(sizeof(mcp_bool_t), 1);
    EXPECT_EQ(sizeof(mcp_result_t), sizeof(int32_t));
    EXPECT_EQ(sizeof(mcp_type_id_t), sizeof(int32_t));
    
    // Verify boolean values
    mcp_bool_t true_val = MCP_TRUE;
    mcp_bool_t false_val = MCP_FALSE;
    EXPECT_EQ(static_cast<uint8_t>(true_val), 1);
    EXPECT_EQ(static_cast<uint8_t>(false_val), 0);
}

TEST_F(FFIImprovementsTest, StringViewVsOwned) {
    // Test string view (borrowed)
    const char* test_data = "Hello, FFI!";
    mcp_string_view_t view = {test_data, strlen(test_data)};
    EXPECT_EQ(view.data, test_data);
    EXPECT_EQ(view.length, strlen(test_data));
    
    // Test owned string
    auto owned = mcp_string_create(test_data, strlen(test_data));
    ASSERT_NE(owned, nullptr);
    EXPECT_NE(owned->data, test_data); // Different memory
    EXPECT_EQ(owned->length, strlen(test_data));
    EXPECT_GE(owned->capacity, owned->length + 1); // Space for null terminator
    EXPECT_EQ(strcmp(owned->data, test_data), 0);
    
    mcp_string_free(owned);
}

TEST_F(FFIImprovementsTest, NoUnionsInAPI) {
    // Test request ID without unions
    auto string_id = mcp_request_id_create_string("request-123");
    ASSERT_NE(string_id, nullptr);
    EXPECT_EQ(mcp_request_id_get_type(string_id), MCP_REQUEST_ID_TYPE_STRING);
    EXPECT_STREQ(mcp_request_id_get_string(string_id), "request-123");
    EXPECT_EQ(mcp_request_id_get_number(string_id), 0); // Safe default
    
    auto number_id = mcp_request_id_create_number(42);
    ASSERT_NE(number_id, nullptr);
    EXPECT_EQ(mcp_request_id_get_type(number_id), MCP_REQUEST_ID_TYPE_NUMBER);
    EXPECT_EQ(mcp_request_id_get_number(number_id), 42);
    EXPECT_EQ(mcp_request_id_get_string(number_id), nullptr); // Safe default
    
    mcp_request_id_free(string_id);
    mcp_request_id_free(number_id);
}

/* ============================================================================
 * RAII Integration Tests
 * ============================================================================ */

TEST_F(FFIImprovementsTest, ResourceGuardIntegration) {
    // Test automatic cleanup with ResourceGuard
    {
        auto guard = make_string_guard("RAII String", 11);
        ASSERT_NE(guard.get(), nullptr);
        EXPECT_EQ(guard.get()->length, 11);
        // Automatic cleanup on scope exit
    }
    
    {
        auto list_guard = make_list_guard(MCP_TYPE_STRING);
        ASSERT_NE(list_guard.get(), nullptr);
        EXPECT_EQ(mcp_list_size(list_guard.get()), 0);
        // Automatic cleanup on scope exit
    }
    
    {
        auto map_guard = make_map_guard(MCP_TYPE_JSON);
        ASSERT_NE(map_guard.get(), nullptr);
        EXPECT_EQ(mcp_map_size(map_guard.get()), 0);
        // Automatic cleanup on scope exit
    }
}

TEST_F(FFIImprovementsTest, AllocationTransactionIntegration) {
    MCPAllocationTransaction txn;
    
    // Track multiple resources
    auto str = mcp_string_create("Transaction Test", 16);
    txn.track_string(str);
    
    auto list = mcp_list_create(MCP_TYPE_STRING);
    txn.track_list(list);
    
    auto map = mcp_map_create(MCP_TYPE_JSON);
    txn.track_map(map);
    
    // Simulate success - commit transaction
    txn.commit();
    
    // Manual cleanup needed after commit
    mcp_string_free(str);
    mcp_list_free(list);
    mcp_map_free(map);
}

TEST_F(FFIImprovementsTest, AllocationTransactionRollback) {
    MCPAllocationTransaction txn;
    
    // Track resources
    auto str = mcp_string_create("Rollback Test", 13);
    txn.track_string(str);
    
    auto json = mcp_json_create_string("test");
    txn.track_json(json);
    
    // Don't commit - automatic rollback on destruction
    // Resources will be freed automatically
}

/* ============================================================================
 * Reference Counting Tests
 * ============================================================================ */

TEST_F(FFIImprovementsTest, RefCountedBasic) {
    auto str = mcp_string_create("RefCounted", 10);
    
    {
        RefCounted<mcp_string_owned_t> ref1(str);
        EXPECT_EQ(ref1.use_count(), 1);
        
        {
            RefCounted<mcp_string_owned_t> ref2(ref1);
            EXPECT_EQ(ref1.use_count(), 2);
            EXPECT_EQ(ref2.use_count(), 2);
        }
        
        EXPECT_EQ(ref1.use_count(), 1);
    }
    
    // String is automatically freed when last reference is destroyed
}

TEST_F(FFIImprovementsTest, RefCountedThreadSafety) {
    auto list = mcp_list_create(MCP_TYPE_STRING);
    RefCounted<std::remove_pointer_t<mcp_list_t>> shared_list(list);
    
    std::atomic<int> operations(0);
    std::vector<std::thread> threads;
    
    // Create multiple threads sharing the same ref-counted list
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&shared_list, &operations]() {
            RefCounted<std::remove_pointer_t<mcp_list_t>> local_ref(shared_list);
            
            // Simulate some work
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            if (local_ref) {
                operations.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(operations.load(), 10);
    EXPECT_EQ(shared_list.use_count(), 1);
}

/* ============================================================================
 * Memory Pool Tests
 * ============================================================================ */

TEST_F(FFIImprovementsTest, MemoryPoolGuard) {
    MemoryPoolGuard pool(1024);
    
    // Allocate from pool
    void* mem1 = pool.allocate(100);
    ASSERT_NE(mem1, nullptr);
    
    void* mem2 = pool.allocate(200);
    ASSERT_NE(mem2, nullptr);
    
    // Check stats
    auto stats = pool.get_stats();
    EXPECT_GE(stats.current_usage, 300);
    
    // Reset pool
    pool.reset_pool();
    
    // Stats should reflect reset
    stats = pool.get_stats();
    EXPECT_EQ(stats.current_usage, 0);
    
    // Pool automatically destroyed on scope exit
}

TEST_F(FFIImprovementsTest, PoolAllocatedResources) {
    auto pool = mcp_mempool_create(4096);
    ASSERT_NE(pool, nullptr);
    
    // Create resources from pool
    auto str = mcp_string_create_from_pool(pool, "Pool String", 11);
    ASSERT_NE(str, nullptr);
    
    auto list = mcp_list_create_from_pool(pool, MCP_TYPE_STRING, 10);
    ASSERT_NE(list, nullptr);
    
    auto map = mcp_map_create_from_pool(pool, MCP_TYPE_JSON, 10);
    ASSERT_NE(map, nullptr);
    
    // Reset pool - frees all resources at once
    mcp_mempool_reset(pool);
    
    mcp_mempool_destroy(pool);
}

/* ============================================================================
 * Error Context Tests
 * ============================================================================ */

TEST_F(FFIImprovementsTest, ErrorContextGuard) {
    {
        ErrorContextGuard error_guard;
        
        // Simulate an error
        mcp_error_info_t error = {
            MCP_ERROR_INVALID_ARGUMENT,
            "Test error message",
            __FILE__,
            __LINE__,
            0, // timestamp
            0  // thread_id
        };
        mcp_tls_set_error(&error);
        
        EXPECT_TRUE(error_guard.has_error());
        EXPECT_EQ(error_guard.get()->code, MCP_ERROR_INVALID_ARGUMENT);
        EXPECT_EQ(error_guard.get_message(), "Test error message");
        
        // Error automatically cleared on scope exit
    }
    
    // Verify error is cleared
    EXPECT_EQ(mcp_get_last_error(), nullptr);
}

/* ============================================================================
 * Validation Tests
 * ============================================================================ */

TEST_F(FFIImprovementsTest, HandleValidation) {
    auto str = mcp_string_create("Validation Test", 15);
    ASSERT_NE(str, nullptr);
    
    // Validate as correct type
    EXPECT_TRUE(mcp_validate_handle(
        reinterpret_cast<mcp_handle_t>(str), 
        MCP_TYPE_STRING
    ));
    
    // Validate as wrong type should fail
    EXPECT_FALSE(mcp_validate_handle(
        reinterpret_cast<mcp_handle_t>(str), 
        MCP_TYPE_LIST
    ));
    
    mcp_string_free(str);
}

TEST_F(FFIImprovementsTest, StringValidation) {
    mcp_string_view_t valid_str = {"Valid", 5};
    EXPECT_TRUE(mcp_validate_string(&valid_str));
    
    mcp_string_view_t null_data = {nullptr, 5};
    EXPECT_FALSE(mcp_validate_string(&null_data));
    
    mcp_string_view_t zero_length = {"Test", 0};
    EXPECT_TRUE(mcp_validate_string(&zero_length)); // Empty string is valid
}

/* ============================================================================
 * Batch Operations Tests
 * ============================================================================ */

TEST_F(FFIImprovementsTest, BatchOperationGuard) {
    BatchOperationGuard batch;
    
    // Add multiple resources to batch
    batch.add(mcp_string_create("Batch1", 6));
    batch.add(mcp_string_create("Batch2", 6));
    batch.add(mcp_string_create("Batch3", 6));
    
    // Add custom resource
    void* custom = malloc(100);
    batch.add_custom(custom, free);
    
    // All resources freed on destruction
    // Or explicitly:
    batch.execute_and_clear();
}

/* ============================================================================
 * Move Semantics Tests
 * ============================================================================ */

TEST_F(FFIImprovementsTest, MoveSemantics) {
    auto str1 = mcp_string_create("Move Source", 11);
    auto str2 = mcp_string_create("Move Target", 11);
    
    // Move str1 to str2
    auto result = mcp_string_move(str1, str2);
    ASSERT_EQ(result, str2);
    
    // str1 should be empty/reset
    EXPECT_EQ(str1->data, nullptr);
    EXPECT_EQ(str1->length, 0);
    EXPECT_EQ(str1->capacity, 0);
    
    // str2 should have the moved data
    EXPECT_STREQ(str2->data, "Move Source");
    EXPECT_EQ(str2->length, 11);
    
    mcp_string_free(str1);
    mcp_string_free(str2);
}

/* ============================================================================
 * Scoped Lock Tests
 * ============================================================================ */

TEST_F(FFIImprovementsTest, ScopedLockGuard) {
    std::atomic<int> counter(0);
    std::vector<std::thread> threads;
    
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&counter]() {
            ScopedLockGuard lock("test_resource");
            
            // Critical section
            int current = counter.load();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            counter.store(current + 1);
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(counter.load(), 10);
}

/* ============================================================================
 * Integration Tests
 * ============================================================================ */

TEST_F(FFIImprovementsTest, CompleteWorkflow) {
    // Demonstrate complete workflow with all improvements
    MCPAllocationTransaction txn;
    
    // Create request with RAII
    auto request = mcp_request_create("test.method");
    txn.track_request(request);
    
    // Set request ID (no union)
    auto id = mcp_request_id_create_string("req-123");
    mcp_request_set_id(request, id);
    mcp_request_id_free(id);
    
    // Create params as JSON
    auto params = mcp_json_create_object();
    txn.track_json(params);
    
    auto key_value = mcp_json_create_string("test_value");
    mcp_json_object_set(params, "key", key_value);
    mcp_json_free(key_value);
    
    mcp_request_set_params(request, params);
    
    // Validate request
    EXPECT_TRUE(mcp_request_is_valid(request));
    EXPECT_STREQ(mcp_request_get_method(request), "test.method");
    
    // Create response
    auto result = mcp_json_create_string("Success");
    auto req_id = mcp_request_get_id(request);
    auto response = mcp_response_create_success(req_id, result);
    txn.track_response(response);
    mcp_json_free(result);
    
    // Validate response
    EXPECT_TRUE(mcp_response_is_success(response));
    EXPECT_FALSE(mcp_response_is_error(response));
    
    // Commit transaction - resources need manual cleanup
    txn.commit();
    
    mcp_request_free(request);
    mcp_json_free(params);
    mcp_response_free(response);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}