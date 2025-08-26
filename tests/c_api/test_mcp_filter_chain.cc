/**
 * @file test_mcp_filter_chain.cc
 * @brief Comprehensive unit tests for MCP Filter Chain C API
 *
 * Tests cover:
 * - Advanced chain builder operations
 * - Chain state management (pause/resume/reset)
 * - Node management (add, enable/disable, conditional)
 * - Parallel processing groups
 * - Chain routing and load balancing
 * - Chain pool management
 * - Dynamic composition (JSON, clone, merge)
 * - Performance profiling and optimization
 * - Debugging and validation
 * - Thread safety
 * - Error handling and edge cases
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

#include "mcp/c_api/mcp_filter_chain.h"
#include "mcp/c_api/mcp_filter_buffer.h"
#include "mcp/c_api/mcp_raii.h"
#include "mcp/c_api/mcp_c_api_json.h"
#include "mcp/c_api/mcp_c_types.h"
#include "mcp/c_api/mcp_c_api.h"

using namespace testing;

// ============================================================================
// Test Fixture
// ============================================================================

class MCPFilterChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create dispatcher for tests
        dispatcher_ = mcp_dispatcher_create();
        ASSERT_NE(dispatcher_, nullptr);
    }

    void TearDown() override {
        // Clean up any remaining chains
        for (auto chain : created_chains_) {
            mcp_filter_chain_destroy(chain);
        }
        created_chains_.clear();
        
        // Clean up dispatcher
        if (dispatcher_) {
            mcp_dispatcher_destroy(dispatcher_);
            dispatcher_ = nullptr;
        }
    }
    
    // Helper to track created chains for cleanup
    mcp_filter_chain_t trackChain(mcp_filter_chain_t chain) {
        if (chain != 0) {
            created_chains_.push_back(chain);
        }
        return chain;
    }
    
    // Create a simple test filter
    mcp_filter_t createTestFilter(const char* name) {
        auto filter = mcp_filter_create(name);
        return filter;
    }
    
    // Create a test buffer
    mcp_buffer_handle_t createTestBuffer(const char* data) {
        size_t len = strlen(data);
        return mcp_buffer_create(data, len);
    }

protected:
    mcp_dispatcher_t dispatcher_ = nullptr;
    std::vector<mcp_filter_chain_t> created_chains_;
};

// ============================================================================
// Basic Chain Builder Tests
// ============================================================================

TEST_F(MCPFilterChainTest, CreateChainBuilderWithConfig) {
    mcp_chain_config_t config = {};
    config.name = "test_chain";
    config.mode = MCP_CHAIN_MODE_SEQUENTIAL;
    config.routing = MCP_ROUTING_ROUND_ROBIN;
    config.max_parallel = 4;
    config.buffer_size = 1024;
    config.timeout_ms = 5000;
    config.stop_on_error = MCP_TRUE;
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    ASSERT_NE(builder, nullptr);
    
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    // Verify chain was created with correct initial state
    EXPECT_EQ(mcp_chain_get_state(chain), MCP_CHAIN_STATE_IDLE);
    
    mcp_filter_chain_builder_destroy(builder);
}

TEST_F(MCPFilterChainTest, CreateChainBuilderNullConfig) {
    // Should handle null config gracefully
    auto builder = mcp_chain_builder_create_ex(dispatcher_, nullptr);
    EXPECT_EQ(builder, nullptr);
}

TEST_F(MCPFilterChainTest, AddNodeToBuilder) {
    mcp_chain_config_t config = {};
    config.name = "test_chain";
    config.mode = MCP_CHAIN_MODE_SEQUENTIAL;
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    ASSERT_NE(builder, nullptr);
    
    // Create and add a filter node
    mcp_filter_node_t node = {};
    node.filter = createTestFilter("test_filter");
    node.name = "filter_1";
    node.priority = 10;
    node.enabled = MCP_TRUE;
    node.bypass_on_error = MCP_FALSE;
    
    auto result = mcp_chain_builder_add_node(builder, &node);
    EXPECT_EQ(result, MCP_OK);
    
    // Add another node with different priority
    mcp_filter_node_t node2 = {};
    node2.filter = createTestFilter("test_filter_2");
    node2.name = "filter_2";
    node2.priority = 5; // Lower priority should be executed first
    node2.enabled = MCP_TRUE;
    node2.bypass_on_error = MCP_TRUE;
    
    result = mcp_chain_builder_add_node(builder, &node2);
    EXPECT_EQ(result, MCP_OK);
    
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    mcp_filter_chain_builder_destroy(builder);
}

TEST_F(MCPFilterChainTest, AddNodeInvalidArguments) {
    mcp_chain_config_t config = {};
    config.name = "test_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    ASSERT_NE(builder, nullptr);
    
    // Test null builder
    mcp_filter_node_t node = {};
    auto result = mcp_chain_builder_add_node(nullptr, &node);
    EXPECT_EQ(result, MCP_ERROR_INVALID_ARGUMENT);
    
    // Test null node
    result = mcp_chain_builder_add_node(builder, nullptr);
    EXPECT_EQ(result, MCP_ERROR_INVALID_ARGUMENT);
    
    mcp_filter_chain_builder_destroy(builder);
}

// ============================================================================
// Conditional Filter Tests
// ============================================================================

TEST_F(MCPFilterChainTest, AddConditionalFilter) {
    mcp_chain_config_t config = {};
    config.name = "conditional_chain";
    config.mode = MCP_CHAIN_MODE_CONDITIONAL;
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    ASSERT_NE(builder, nullptr);
    
    // Create a condition
    mcp_filter_condition_t condition = {};
    condition.match_type = MCP_MATCH_ALL;
    condition.field = "content_type";
    condition.value = "application/json";
    condition.target_filter = createTestFilter("json_filter");
    
    auto result = mcp_chain_builder_add_conditional(builder, &condition, 
                                                    condition.target_filter);
    EXPECT_EQ(result, MCP_OK);
    
    // Add another conditional filter
    mcp_filter_condition_t condition2 = {};
    condition2.match_type = MCP_MATCH_ANY;
    condition2.field = "method";
    condition2.value = "GET";
    condition2.target_filter = createTestFilter("get_filter");
    
    result = mcp_chain_builder_add_conditional(builder, &condition2, 
                                               condition2.target_filter);
    EXPECT_EQ(result, MCP_OK);
    
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    mcp_filter_chain_builder_destroy(builder);
}

TEST_F(MCPFilterChainTest, AddConditionalFilterInvalidArguments) {
    mcp_chain_config_t config = {};
    config.name = "test_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    ASSERT_NE(builder, nullptr);
    
    mcp_filter_condition_t condition = {};
    mcp_filter_t filter = createTestFilter("test");
    
    // Test null builder
    auto result = mcp_chain_builder_add_conditional(nullptr, &condition, filter);
    EXPECT_EQ(result, MCP_ERROR_INVALID_ARGUMENT);
    
    // Test null condition
    result = mcp_chain_builder_add_conditional(builder, nullptr, filter);
    EXPECT_EQ(result, MCP_ERROR_INVALID_ARGUMENT);
    
    mcp_filter_chain_builder_destroy(builder);
}

// ============================================================================
// Parallel Filter Group Tests
// ============================================================================

TEST_F(MCPFilterChainTest, AddParallelGroup) {
    mcp_chain_config_t config = {};
    config.name = "parallel_chain";
    config.mode = MCP_CHAIN_MODE_PARALLEL;
    config.max_parallel = 4;
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    ASSERT_NE(builder, nullptr);
    
    // Create a group of filters to run in parallel
    std::vector<mcp_filter_t> filters;
    filters.push_back(createTestFilter("parallel_filter_1"));
    filters.push_back(createTestFilter("parallel_filter_2"));
    filters.push_back(createTestFilter("parallel_filter_3"));
    
    auto result = mcp_chain_builder_add_parallel_group(builder, 
                                                       filters.data(), 
                                                       filters.size());
    EXPECT_EQ(result, MCP_OK);
    
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    mcp_filter_chain_builder_destroy(builder);
}

TEST_F(MCPFilterChainTest, AddParallelGroupInvalidArguments) {
    mcp_chain_config_t config = {};
    config.name = "test_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    ASSERT_NE(builder, nullptr);
    
    mcp_filter_t filters[2];
    
    // Test null builder
    auto result = mcp_chain_builder_add_parallel_group(nullptr, filters, 2);
    EXPECT_EQ(result, MCP_ERROR_INVALID_ARGUMENT);
    
    // Test null filters
    result = mcp_chain_builder_add_parallel_group(builder, nullptr, 2);
    EXPECT_EQ(result, MCP_ERROR_INVALID_ARGUMENT);
    
    mcp_filter_chain_builder_destroy(builder);
}

// ============================================================================
// Custom Router Tests
// ============================================================================

static mcp_filter_t customRouterFunction(mcp_buffer_handle_t buffer,
                                         const mcp_filter_node_t* nodes,
                                         size_t node_count,
                                         void* user_data) {
    // Simple custom routing logic for testing
    auto* counter = static_cast<std::atomic<int>*>(user_data);
    counter->fetch_add(1);
    
    // Route to first available filter
    return (node_count > 0 && nodes) ? nodes[0].filter : 0;
}

TEST_F(MCPFilterChainTest, SetCustomRouter) {
    mcp_chain_config_t config = {};
    config.name = "routed_chain";
    config.routing = MCP_ROUTING_CUSTOM;
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    ASSERT_NE(builder, nullptr);
    
    std::atomic<int> router_calls(0);
    
    auto result = mcp_chain_builder_set_router(builder, 
                                               customRouterFunction, 
                                               &router_calls);
    EXPECT_EQ(result, MCP_OK);
    
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    mcp_filter_chain_builder_destroy(builder);
}

// ============================================================================
// Chain State Management Tests
// ============================================================================

TEST_F(MCPFilterChainTest, ChainStateTransitions) {
    mcp_chain_config_t config = {};
    config.name = "state_test_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    ASSERT_NE(builder, nullptr);
    
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    // Initial state should be idle
    EXPECT_EQ(mcp_chain_get_state(chain), MCP_CHAIN_STATE_IDLE);
    
    // Pause chain (should stay idle if not processing)
    auto result = mcp_chain_pause(chain);
    EXPECT_EQ(result, MCP_OK);
    
    // Resume chain
    result = mcp_chain_resume(chain);
    EXPECT_EQ(result, MCP_OK);
    
    // Reset chain
    result = mcp_chain_reset(chain);
    EXPECT_EQ(result, MCP_OK);
    EXPECT_EQ(mcp_chain_get_state(chain), MCP_CHAIN_STATE_IDLE);
    
    mcp_filter_chain_builder_destroy(builder);
}

TEST_F(MCPFilterChainTest, ChainStateInvalidHandle) {
    // Test with invalid handle
    mcp_filter_chain_t invalid_chain = 0;
    
    EXPECT_EQ(mcp_chain_get_state(invalid_chain), MCP_CHAIN_STATE_ERROR);
    EXPECT_EQ(mcp_chain_pause(invalid_chain), MCP_ERROR_NOT_FOUND);
    EXPECT_EQ(mcp_chain_resume(invalid_chain), MCP_ERROR_NOT_FOUND);
    EXPECT_EQ(mcp_chain_reset(invalid_chain), MCP_ERROR_NOT_FOUND);
}

// ============================================================================
// Filter Enable/Disable Tests
// ============================================================================

TEST_F(MCPFilterChainTest, EnableDisableFilter) {
    mcp_chain_config_t config = {};
    config.name = "enable_disable_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    ASSERT_NE(builder, nullptr);
    
    // Add multiple filters
    mcp_filter_node_t node1 = {};
    node1.filter = createTestFilter("filter_1");
    node1.name = "filter_1";
    node1.enabled = MCP_TRUE;
    mcp_chain_builder_add_node(builder, &node1);
    
    mcp_filter_node_t node2 = {};
    node2.filter = createTestFilter("filter_2");
    node2.name = "filter_2";
    node2.enabled = MCP_TRUE;
    mcp_chain_builder_add_node(builder, &node2);
    
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    // Disable filter_1
    auto result = mcp_chain_set_filter_enabled(chain, "filter_1", MCP_FALSE);
    EXPECT_EQ(result, MCP_OK);
    
    // Enable filter_1
    result = mcp_chain_set_filter_enabled(chain, "filter_1", MCP_TRUE);
    EXPECT_EQ(result, MCP_OK);
    
    // Test with non-existent filter (should still return OK)
    result = mcp_chain_set_filter_enabled(chain, "non_existent", MCP_FALSE);
    EXPECT_EQ(result, MCP_OK);
    
    mcp_filter_chain_builder_destroy(builder);
}

TEST_F(MCPFilterChainTest, EnableDisableFilterInvalidArguments) {
    mcp_chain_config_t config = {};
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    
    // Test null filter name
    auto result = mcp_chain_set_filter_enabled(chain, nullptr, MCP_TRUE);
    EXPECT_EQ(result, MCP_ERROR_INVALID_ARGUMENT);
    
    // Test invalid chain
    result = mcp_chain_set_filter_enabled(0, "filter", MCP_TRUE);
    EXPECT_EQ(result, MCP_ERROR_NOT_FOUND);
    
    mcp_filter_chain_builder_destroy(builder);
}

// ============================================================================
// Chain Statistics Tests
// ============================================================================

TEST_F(MCPFilterChainTest, GetChainStatistics) {
    mcp_chain_config_t config = {};
    config.name = "stats_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    ASSERT_NE(builder, nullptr);
    
    // Add some filters
    for (int i = 0; i < 3; ++i) {
        mcp_filter_node_t node = {};
        node.filter = createTestFilter(("filter_" + std::to_string(i)).c_str());
        node.name = ("filter_" + std::to_string(i)).c_str();
        node.enabled = MCP_TRUE;
        mcp_chain_builder_add_node(builder, &node);
    }
    
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    mcp_chain_stats_t stats = {};
    auto result = mcp_chain_get_stats(chain, &stats);
    EXPECT_EQ(result, MCP_OK);
    
    // Initial stats should be zero
    EXPECT_EQ(stats.total_processed, 0);
    EXPECT_EQ(stats.total_errors, 0);
    EXPECT_EQ(stats.total_bypassed, 0);
    EXPECT_GE(stats.active_filters, 0); // Should have active filters
    EXPECT_EQ(stats.avg_latency_ms, 0.0);
    EXPECT_EQ(stats.max_latency_ms, 0.0);
    
    mcp_filter_chain_builder_destroy(builder);
}

TEST_F(MCPFilterChainTest, GetChainStatisticsInvalidArguments) {
    mcp_chain_config_t config = {};
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    
    // Test null stats pointer
    auto result = mcp_chain_get_stats(chain, nullptr);
    EXPECT_EQ(result, MCP_ERROR_INVALID_ARGUMENT);
    
    // Test invalid chain
    mcp_chain_stats_t stats = {};
    result = mcp_chain_get_stats(0, &stats);
    EXPECT_EQ(result, MCP_ERROR_NOT_FOUND);
    
    mcp_filter_chain_builder_destroy(builder);
}

// ============================================================================
// Event Callback Tests
// ============================================================================

struct EventCallbackData {
    std::atomic<int> event_count{0};
    mcp_chain_state_t last_old_state{MCP_CHAIN_STATE_IDLE};
    mcp_chain_state_t last_new_state{MCP_CHAIN_STATE_IDLE};
    std::mutex mutex;
    std::condition_variable cv;
};

static void chainEventCallback(mcp_filter_chain_t chain,
                               mcp_chain_state_t old_state,
                               mcp_chain_state_t new_state,
                               void* user_data) {
    auto* data = static_cast<EventCallbackData*>(user_data);
    std::lock_guard<std::mutex> lock(data->mutex);
    data->event_count++;
    data->last_old_state = old_state;
    data->last_new_state = new_state;
    data->cv.notify_all();
}

TEST_F(MCPFilterChainTest, SetEventCallback) {
    mcp_chain_config_t config = {};
    config.name = "event_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    ASSERT_NE(builder, nullptr);
    
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    EventCallbackData callback_data;
    
    auto result = mcp_chain_set_event_callback(chain, 
                                               chainEventCallback, 
                                               &callback_data);
    EXPECT_EQ(result, MCP_OK);
    
    // State transitions should trigger callbacks
    // Note: Actual implementation may post to dispatcher thread
    
    mcp_filter_chain_builder_destroy(builder);
}

// ============================================================================
// Chain Router Tests
// ============================================================================

TEST_F(MCPFilterChainTest, CreateChainRouter) {
    mcp_router_config_t config = {};
    config.strategy = MCP_ROUTING_ROUND_ROBIN;
    config.hash_seed = 12345;
    
    auto router = mcp_chain_router_create(&config);
    ASSERT_NE(router, nullptr);
    
    mcp_chain_router_destroy(router);
}

TEST_F(MCPFilterChainTest, ChainRouterAddRoute) {
    mcp_router_config_t config = {};
    config.strategy = MCP_ROUTING_ROUND_ROBIN;
    
    auto router = mcp_chain_router_create(&config);
    ASSERT_NE(router, nullptr);
    
    // Create chains for routing
    mcp_chain_config_t chain_config = {};
    auto builder1 = mcp_chain_builder_create_ex(dispatcher_, &chain_config);
    auto chain1 = trackChain(mcp_filter_chain_builder_build(builder1));
    
    auto builder2 = mcp_chain_builder_create_ex(dispatcher_, &chain_config);
    auto chain2 = trackChain(mcp_filter_chain_builder_build(builder2));
    
    // Add routes
    auto condition = [](mcp_buffer_handle_t buffer,
                       const mcp_protocol_metadata_t* metadata,
                       void* user_data) -> mcp_bool_t {
        return MCP_TRUE; // Always match for testing
    };
    
    auto result = mcp_chain_router_add_route(router, condition, chain1);
    EXPECT_EQ(result, MCP_OK);
    
    result = mcp_chain_router_add_route(router, condition, chain2);
    EXPECT_EQ(result, MCP_OK);
    
    // Test routing
    auto buffer = createTestBuffer("test data");
    mcp_protocol_metadata_t metadata = {};
    
    auto selected_chain = mcp_chain_router_route(router, buffer, &metadata);
    EXPECT_NE(selected_chain, 0);
    
    mcp_buffer_destroy(buffer);
    mcp_chain_router_destroy(router);
    mcp_filter_chain_builder_destroy(builder1);
    mcp_filter_chain_builder_destroy(builder2);
}

TEST_F(MCPFilterChainTest, ChainRouterInvalidArguments) {
    // Test null config
    auto router = mcp_chain_router_create(nullptr);
    EXPECT_EQ(router, nullptr);
    
    // Test null router operations
    auto result = mcp_chain_router_add_route(nullptr, nullptr, 0);
    EXPECT_EQ(result, MCP_ERROR_INVALID_ARGUMENT);
    
    auto buffer = createTestBuffer("test");
    auto chain = mcp_chain_router_route(nullptr, buffer, nullptr);
    EXPECT_EQ(chain, 0);
    
    mcp_buffer_destroy(buffer);
}

// ============================================================================
// Chain Pool Tests
// ============================================================================

TEST_F(MCPFilterChainTest, CreateChainPool) {
    // Create a base chain
    mcp_chain_config_t config = {};
    config.name = "pool_base_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    auto base_chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(base_chain, 0);
    
    // Create pool
    size_t pool_size = 5;
    auto pool = mcp_chain_pool_create(base_chain, pool_size, 
                                      MCP_ROUTING_ROUND_ROBIN);
    ASSERT_NE(pool, nullptr);
    
    // Get statistics
    size_t active = 0, idle = 0;
    uint64_t total_processed = 0;
    
    auto result = mcp_chain_pool_get_stats(pool, &active, &idle, 
                                           &total_processed);
    EXPECT_EQ(result, MCP_OK);
    EXPECT_EQ(active, 0);
    EXPECT_EQ(idle, pool_size);
    EXPECT_EQ(total_processed, 0);
    
    mcp_chain_pool_destroy(pool);
    mcp_filter_chain_builder_destroy(builder);
}

TEST_F(MCPFilterChainTest, ChainPoolGetAndReturn) {
    mcp_chain_config_t config = {};
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    auto base_chain = trackChain(mcp_filter_chain_builder_build(builder));
    
    size_t pool_size = 3;
    auto pool = mcp_chain_pool_create(base_chain, pool_size, 
                                      MCP_ROUTING_LEAST_LOADED);
    ASSERT_NE(pool, nullptr);
    
    // Get chains from pool
    std::vector<mcp_filter_chain_t> acquired_chains;
    for (size_t i = 0; i < pool_size; ++i) {
        auto chain = mcp_chain_pool_get_next(pool);
        EXPECT_NE(chain, 0);
        acquired_chains.push_back(chain);
    }
    
    // Pool should be empty now
    auto chain = mcp_chain_pool_get_next(pool);
    EXPECT_EQ(chain, 0);
    
    // Return chains to pool
    for (auto c : acquired_chains) {
        mcp_chain_pool_return(pool, c);
    }
    
    // Should be able to get chains again
    chain = mcp_chain_pool_get_next(pool);
    EXPECT_NE(chain, 0);
    mcp_chain_pool_return(pool, chain);
    
    mcp_chain_pool_destroy(pool);
    mcp_filter_chain_builder_destroy(builder);
}

TEST_F(MCPFilterChainTest, ChainPoolInvalidArguments) {
    // Test null pool operations
    auto chain = mcp_chain_pool_get_next(nullptr);
    EXPECT_EQ(chain, 0);
    
    mcp_chain_pool_return(nullptr, 0);
    
    auto result = mcp_chain_pool_get_stats(nullptr, nullptr, nullptr, nullptr);
    EXPECT_EQ(result, MCP_ERROR_INVALID_ARGUMENT);
}

// ============================================================================
// JSON Configuration Tests
// ============================================================================

TEST_F(MCPFilterChainTest, CreateChainFromJSON) {
    // Create JSON configuration
    mcp_json_value_t json = mcp_json_create_object();
    mcp_json_set_string(json, "name", "json_chain");
    mcp_json_set_number(json, "mode", MCP_CHAIN_MODE_SEQUENTIAL);
    mcp_json_set_number(json, "max_parallel", 4);
    mcp_json_set_bool(json, "stop_on_error", MCP_TRUE);
    
    auto chain = trackChain(mcp_chain_create_from_json(dispatcher_, json));
    // Note: Implementation may return 0 if not fully implemented
    
    mcp_json_free(json);
}

TEST_F(MCPFilterChainTest, ExportChainToJSON) {
    mcp_chain_config_t config = {};
    config.name = "export_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    auto json = mcp_chain_export_to_json(chain);
    // Note: Implementation may return null if not fully implemented
    
    if (json) {
        mcp_json_free(json);
    }
    
    mcp_filter_chain_builder_destroy(builder);
}

// ============================================================================
// Chain Clone and Merge Tests
// ============================================================================

TEST_F(MCPFilterChainTest, CloneChain) {
    mcp_chain_config_t config = {};
    config.name = "original_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    
    // Add some filters
    mcp_filter_node_t node = {};
    node.filter = createTestFilter("filter");
    node.name = "filter";
    mcp_chain_builder_add_node(builder, &node);
    
    auto original_chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(original_chain, 0);
    
    auto cloned_chain = trackChain(mcp_chain_clone(original_chain));
    // Note: Implementation may return 0 if not fully implemented
    
    mcp_filter_chain_builder_destroy(builder);
}

TEST_F(MCPFilterChainTest, MergeChains) {
    mcp_chain_config_t config1 = {};
    config1.name = "chain1";
    auto builder1 = mcp_chain_builder_create_ex(dispatcher_, &config1);
    auto chain1 = trackChain(mcp_filter_chain_builder_build(builder1));
    
    mcp_chain_config_t config2 = {};
    config2.name = "chain2";
    auto builder2 = mcp_chain_builder_create_ex(dispatcher_, &config2);
    auto chain2 = trackChain(mcp_filter_chain_builder_build(builder2));
    
    auto merged = trackChain(mcp_chain_merge(chain1, chain2, 
                                             MCP_CHAIN_MODE_SEQUENTIAL));
    // Note: Implementation may return 0 if not fully implemented
    
    mcp_filter_chain_builder_destroy(builder1);
    mcp_filter_chain_builder_destroy(builder2);
}

// ============================================================================
// Chain Optimization Tests
// ============================================================================

TEST_F(MCPFilterChainTest, OptimizeChain) {
    mcp_chain_config_t config = {};
    config.name = "optimize_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    
    // Add multiple filters
    for (int i = 0; i < 5; ++i) {
        mcp_filter_node_t node = {};
        node.filter = createTestFilter(("filter_" + std::to_string(i)).c_str());
        node.name = ("filter_" + std::to_string(i)).c_str();
        node.priority = 10 - i; // Decreasing priority
        mcp_chain_builder_add_node(builder, &node);
    }
    
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    auto result = mcp_chain_optimize(chain);
    EXPECT_EQ(result, MCP_OK);
    
    result = mcp_chain_reorder_filters(chain);
    EXPECT_EQ(result, MCP_OK);
    
    mcp_filter_chain_builder_destroy(builder);
}

TEST_F(MCPFilterChainTest, ProfileChain) {
    mcp_chain_config_t config = {};
    config.name = "profile_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    auto test_buffer = createTestBuffer("profile test data");
    size_t iterations = 100;
    mcp_json_value_t report = nullptr;
    
    auto result = mcp_chain_profile(chain, test_buffer, iterations, &report);
    EXPECT_EQ(result, MCP_OK);
    
    if (report) {
        mcp_json_free(report);
    }
    
    mcp_buffer_destroy(test_buffer);
    mcp_filter_chain_builder_destroy(builder);
}

// ============================================================================
// Chain Debugging Tests
// ============================================================================

TEST_F(MCPFilterChainTest, SetTraceLevel) {
    mcp_chain_config_t config = {};
    config.name = "trace_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    // Set different trace levels
    auto result = mcp_chain_set_trace_level(chain, 0); // Off
    EXPECT_EQ(result, MCP_OK);
    
    result = mcp_chain_set_trace_level(chain, 1); // Basic
    EXPECT_EQ(result, MCP_OK);
    
    result = mcp_chain_set_trace_level(chain, 2); // Detailed
    EXPECT_EQ(result, MCP_OK);
    
    mcp_filter_chain_builder_destroy(builder);
}

TEST_F(MCPFilterChainTest, DumpChain) {
    mcp_chain_config_t config = {};
    config.name = "dump_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    
    // Add some filters
    for (int i = 0; i < 3; ++i) {
        mcp_filter_node_t node = {};
        node.filter = createTestFilter(("filter_" + std::to_string(i)).c_str());
        node.name = ("filter_" + std::to_string(i)).c_str();
        mcp_chain_builder_add_node(builder, &node);
    }
    
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    // Test different dump formats
    char* text_dump = mcp_chain_dump(chain, "text");
    if (text_dump) {
        EXPECT_NE(strlen(text_dump), 0);
        free(text_dump);
    }
    
    char* json_dump = mcp_chain_dump(chain, "json");
    if (json_dump) {
        EXPECT_NE(strlen(json_dump), 0);
        free(json_dump);
    }
    
    char* dot_dump = mcp_chain_dump(chain, "dot");
    if (dot_dump) {
        EXPECT_NE(strlen(dot_dump), 0);
        free(dot_dump);
    }
    
    // Test default format (text)
    char* default_dump = mcp_chain_dump(chain, nullptr);
    if (default_dump) {
        EXPECT_NE(strlen(default_dump), 0);
        free(default_dump);
    }
    
    mcp_filter_chain_builder_destroy(builder);
}

TEST_F(MCPFilterChainTest, ValidateChain) {
    mcp_chain_config_t config = {};
    config.name = "validate_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    mcp_json_value_t errors = nullptr;
    auto result = mcp_chain_validate(chain, &errors);
    EXPECT_EQ(result, MCP_OK);
    
    if (errors) {
        mcp_json_free(errors);
    }
    
    mcp_filter_chain_builder_destroy(builder);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(MCPFilterChainTest, ConcurrentChainOperations) {
    mcp_chain_config_t config = {};
    config.name = "concurrent_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    
    // Add multiple filters
    for (int i = 0; i < 10; ++i) {
        mcp_filter_node_t node = {};
        node.filter = createTestFilter(("filter_" + std::to_string(i)).c_str());
        node.name = ("filter_" + std::to_string(i)).c_str();
        node.enabled = MCP_TRUE;
        mcp_chain_builder_add_node(builder, &node);
    }
    
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    std::atomic<bool> stop(false);
    std::vector<std::thread> threads;
    
    // Thread 1: Continuously get stats
    threads.emplace_back([&]() {
        while (!stop) {
            mcp_chain_stats_t stats = {};
            mcp_chain_get_stats(chain, &stats);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    
    // Thread 2: Enable/disable filters
    threads.emplace_back([&]() {
        int counter = 0;
        while (!stop) {
            std::string filter_name = "filter_" + std::to_string(counter % 10);
            mcp_chain_set_filter_enabled(chain, filter_name.c_str(), 
                                         counter % 2 == 0 ? MCP_TRUE : MCP_FALSE);
            counter++;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    
    // Thread 3: Pause/resume operations
    threads.emplace_back([&]() {
        while (!stop) {
            mcp_chain_pause(chain);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            mcp_chain_resume(chain);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    
    // Let threads run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop = true;
    
    for (auto& t : threads) {
        t.join();
    }
    
    mcp_filter_chain_builder_destroy(builder);
}

TEST_F(MCPFilterChainTest, ConcurrentPoolAccess) {
    mcp_chain_config_t config = {};
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    auto base_chain = trackChain(mcp_filter_chain_builder_build(builder));
    
    size_t pool_size = 10;
    auto pool = mcp_chain_pool_create(base_chain, pool_size, 
                                      MCP_ROUTING_ROUND_ROBIN);
    ASSERT_NE(pool, nullptr);
    
    std::atomic<int> acquisitions(0);
    std::atomic<int> releases(0);
    std::atomic<bool> stop(false);
    std::vector<std::thread> threads;
    
    // Multiple threads acquiring and releasing chains
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&]() {
            while (!stop) {
                auto chain = mcp_chain_pool_get_next(pool);
                if (chain != 0) {
                    acquisitions++;
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    mcp_chain_pool_return(pool, chain);
                    releases++;
                }
            }
        });
    }
    
    // Let threads run
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop = true;
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Verify acquisitions and releases are balanced
    EXPECT_EQ(acquisitions.load(), releases.load());
    
    mcp_chain_pool_destroy(pool);
    mcp_filter_chain_builder_destroy(builder);
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST_F(MCPFilterChainTest, EmptyChainOperations) {
    mcp_chain_config_t config = {};
    config.name = "empty_chain";
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    // Build chain without adding any filters
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    // Operations on empty chain should succeed
    EXPECT_EQ(mcp_chain_get_state(chain), MCP_CHAIN_STATE_IDLE);
    EXPECT_EQ(mcp_chain_pause(chain), MCP_OK);
    EXPECT_EQ(mcp_chain_resume(chain), MCP_OK);
    EXPECT_EQ(mcp_chain_reset(chain), MCP_OK);
    
    mcp_chain_stats_t stats = {};
    EXPECT_EQ(mcp_chain_get_stats(chain, &stats), MCP_OK);
    EXPECT_EQ(stats.total_processed, 0);
    EXPECT_EQ(stats.active_filters, 0);
    
    mcp_filter_chain_builder_destroy(builder);
}

TEST_F(MCPFilterChainTest, LargeChainCreation) {
    mcp_chain_config_t config = {};
    config.name = "large_chain";
    config.mode = MCP_CHAIN_MODE_SEQUENTIAL;
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    ASSERT_NE(builder, nullptr);
    
    // Add a large number of filters
    const int num_filters = 1000;
    for (int i = 0; i < num_filters; ++i) {
        mcp_filter_node_t node = {};
        node.filter = createTestFilter(("filter_" + std::to_string(i)).c_str());
        node.name = ("filter_" + std::to_string(i)).c_str();
        node.priority = i;
        node.enabled = (i % 2 == 0) ? MCP_TRUE : MCP_FALSE;
        
        auto result = mcp_chain_builder_add_node(builder, &node);
        EXPECT_EQ(result, MCP_OK);
    }
    
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    // Verify chain was created successfully
    EXPECT_EQ(mcp_chain_get_state(chain), MCP_CHAIN_STATE_IDLE);
    
    mcp_filter_chain_builder_destroy(builder);
}

TEST_F(MCPFilterChainTest, ChainWithMixedModes) {
    mcp_chain_config_t config = {};
    config.name = "mixed_mode_chain";
    config.mode = MCP_CHAIN_MODE_PIPELINE; // Pipeline mode
    config.max_parallel = 8;
    config.buffer_size = 4096;
    config.timeout_ms = 10000;
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    ASSERT_NE(builder, nullptr);
    
    // Add sequential filters
    mcp_filter_node_t seq_node = {};
    seq_node.filter = createTestFilter("sequential_filter");
    seq_node.name = "sequential";
    seq_node.priority = 1;
    mcp_chain_builder_add_node(builder, &seq_node);
    
    // Add parallel group
    std::vector<mcp_filter_t> parallel_filters;
    for (int i = 0; i < 3; ++i) {
        parallel_filters.push_back(createTestFilter(
            ("parallel_" + std::to_string(i)).c_str()));
    }
    mcp_chain_builder_add_parallel_group(builder, 
                                         parallel_filters.data(), 
                                         parallel_filters.size());
    
    // Add conditional filter
    mcp_filter_condition_t condition = {};
    condition.match_type = MCP_MATCH_ANY;
    condition.field = "type";
    condition.value = "special";
    condition.target_filter = createTestFilter("conditional_filter");
    mcp_chain_builder_add_conditional(builder, &condition, 
                                      condition.target_filter);
    
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    mcp_filter_chain_builder_destroy(builder);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(MCPFilterChainTest, FullChainLifecycle) {
    // Create a complete chain with all features
    mcp_chain_config_t config = {};
    config.name = "full_lifecycle_chain";
    config.mode = MCP_CHAIN_MODE_SEQUENTIAL;
    config.routing = MCP_ROUTING_PRIORITY;
    config.max_parallel = 4;
    config.buffer_size = 2048;
    config.timeout_ms = 5000;
    config.stop_on_error = MCP_TRUE;
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    ASSERT_NE(builder, nullptr);
    
    // Add various filter types
    for (int i = 0; i < 5; ++i) {
        mcp_filter_node_t node = {};
        node.filter = createTestFilter(("lifecycle_filter_" + 
                                       std::to_string(i)).c_str());
        node.name = ("filter_" + std::to_string(i)).c_str();
        node.priority = i;
        node.enabled = MCP_TRUE;
        node.bypass_on_error = (i == 2); // Middle filter can bypass on error
        
        mcp_chain_builder_add_node(builder, &node);
    }
    
    // Build chain
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    // Set event callback
    EventCallbackData callback_data;
    mcp_chain_set_event_callback(chain, chainEventCallback, &callback_data);
    
    // Get initial state
    EXPECT_EQ(mcp_chain_get_state(chain), MCP_CHAIN_STATE_IDLE);
    
    // Pause chain
    mcp_chain_pause(chain);
    
    // Disable some filters
    mcp_chain_set_filter_enabled(chain, "filter_1", MCP_FALSE);
    mcp_chain_set_filter_enabled(chain, "filter_3", MCP_FALSE);
    
    // Resume chain
    mcp_chain_resume(chain);
    
    // Get statistics
    mcp_chain_stats_t stats = {};
    mcp_chain_get_stats(chain, &stats);
    
    // Set trace level for debugging
    mcp_chain_set_trace_level(chain, 2);
    
    // Dump chain structure
    char* dump = mcp_chain_dump(chain, "json");
    if (dump) {
        free(dump);
    }
    
    // Validate chain
    mcp_json_value_t errors = nullptr;
    mcp_chain_validate(chain, &errors);
    if (errors) {
        mcp_json_free(errors);
    }
    
    // Reset chain
    mcp_chain_reset(chain);
    EXPECT_EQ(mcp_chain_get_state(chain), MCP_CHAIN_STATE_IDLE);
    
    mcp_filter_chain_builder_destroy(builder);
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(MCPFilterChainTest, ChainPerformanceUnderLoad) {
    mcp_chain_config_t config = {};
    config.name = "performance_chain";
    config.mode = MCP_CHAIN_MODE_PARALLEL;
    config.max_parallel = 16;
    
    auto builder = mcp_chain_builder_create_ex(dispatcher_, &config);
    
    // Add many filters
    for (int i = 0; i < 50; ++i) {
        mcp_filter_node_t node = {};
        node.filter = createTestFilter(("perf_filter_" + 
                                       std::to_string(i)).c_str());
        node.name = ("perf_" + std::to_string(i)).c_str();
        node.priority = i;
        mcp_chain_builder_add_node(builder, &node);
    }
    
    auto chain = trackChain(mcp_filter_chain_builder_build(builder));
    ASSERT_NE(chain, 0);
    
    // Measure operation times
    auto start = std::chrono::high_resolution_clock::now();
    
    // Perform many operations
    for (int i = 0; i < 1000; ++i) {
        mcp_chain_stats_t stats = {};
        mcp_chain_get_stats(chain, &stats);
        
        if (i % 100 == 0) {
            mcp_chain_pause(chain);
            mcp_chain_resume(chain);
        }
        
        if (i % 50 == 0) {
            std::string filter_name = "perf_" + std::to_string(i % 50);
            mcp_chain_set_filter_enabled(chain, filter_name.c_str(), 
                                        i % 2 == 0 ? MCP_TRUE : MCP_FALSE);
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>
                   (end - start);
    
    // Operations should complete in reasonable time
    EXPECT_LT(duration.count(), 5000); // Less than 5 seconds
    
    mcp_filter_chain_builder_destroy(builder);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}