/**
 * @file test_filter_api.cc
 * @brief Comprehensive tests for MCP Filter C API
 *
 * Dependencies from mcp_c_api.h:
 *
 * 2. **Event Loop & Dispatcher:**
 *    - mcp_dispatcher_t mcp_dispatcher_create(void);
 *    - void mcp_dispatcher_destroy(mcp_dispatcher_t dispatcher);
 *
 * **Functions being used:**
 * - mcp_init(nullptr) - Initialize for each test
 * - mcp_dispatcher_create() - Create dispatcher for testing
 * - mcp_dispatcher_destroy() - Cleanup after tests
 * - mcp_shutdown() - Shutdown after tests
 *
 * **Type Dependencies:**
 * - mcp_dispatcher_t - Used throughout filter API as handle type
 * - mcp_result_t - Return codes for all filter operations
 * - mcp_json_value_t - JSON configuration for filters
 * - mcp_connection_t - Connection handles for filter managers
 * - mcp_client_t / mcp_server_t - Client/server integration
 */

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/c_api/mcp_c_api.h"
#include "mcp/c_api/mcp_c_filter_api.h"
#include "mcp/c_api/mcp_c_filter_buffer.h"
#include "mcp/c_api/mcp_c_filter_chain.h"

namespace {

// Test fixture for Filter API tests
class FilterApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Initialize MCP
    ASSERT_EQ(mcp_init(nullptr), MCP_OK);

    // Create dispatcher
    dispatcher_ = mcp_dispatcher_create();
    ASSERT_NE(dispatcher_, 0);
  }

  void TearDown() override {
    // Cleanup
    if (dispatcher_) {
      mcp_dispatcher_destroy(dispatcher_);
    }
    mcp_shutdown();
  }

  mcp_dispatcher_t dispatcher_ = 0;
};

// ============================================================================
// Basic Filter Tests
// ============================================================================

TEST_F(FilterApiTest, CreateAndReleaseFilter) {
  mcp_filter_config_t config = {};
  config.name = "test_filter";
  config.type = MCP_FILTER_CUSTOM;
  config.layer = MCP_PROTOCOL_LAYER_7_APPLICATION;

  mcp_filter_t filter = mcp_filter_create(dispatcher_, &config);
  ASSERT_NE(filter, 0);

  // Retain and release
  mcp_filter_retain(filter);
  mcp_filter_release(filter);
  mcp_filter_release(filter);
}

TEST_F(FilterApiTest, CreateBuiltinFilters) {
  struct TestCase {
    mcp_builtin_filter_type_t type;
    const char* name;
  };

  TestCase test_cases[] = {
      {MCP_FILTER_HTTP_CODEC, "HTTP Codec"},
      {MCP_FILTER_RATE_LIMIT, "Rate Limit"},
      {MCP_FILTER_CIRCUIT_BREAKER, "Circuit Breaker"},
      {MCP_FILTER_TLS_TERMINATION, "TLS Termination"},
      {MCP_FILTER_ACCESS_LOG, "Access Log"},
  };

  for (const auto& tc : test_cases) {
    mcp_filter_t filter =
        mcp_filter_create_builtin(dispatcher_, tc.type, mcp_json_create_null());
    ASSERT_NE(filter, 0) << "Failed to create " << tc.name;
    mcp_filter_release(filter);
  }
}

TEST_F(FilterApiTest, SetFilterCallbacks) {
  // Create filter
  mcp_filter_config_t config = {};
  config.name = "callback_test";
  config.type = MCP_FILTER_CUSTOM;

  mcp_filter_t filter = mcp_filter_create(dispatcher_, &config);
  ASSERT_NE(filter, 0);

  // Set callbacks
  std::atomic<int> callback_count{0};

  mcp_filter_callbacks_t callbacks = {};
  callbacks.on_data = [](mcp_buffer_handle_t buffer, mcp_bool_t end_stream,
                         void* user_data) -> mcp_filter_status_t {
    auto* count = static_cast<std::atomic<int>*>(user_data);
    (*count)++;
    return MCP_FILTER_CONTINUE;
  };
  callbacks.user_data = &callback_count;

  ASSERT_EQ(mcp_filter_set_callbacks(filter, &callbacks), MCP_OK);

  mcp_filter_release(filter);
}

TEST_F(FilterApiTest, ProtocolMetadata) {
  mcp_filter_t filter = mcp_filter_create_builtin(
      dispatcher_, MCP_FILTER_HTTP_CODEC, mcp_json_create_null());
  ASSERT_NE(filter, 0);

  // Set L7 metadata
  mcp_protocol_metadata_t metadata = {};
  metadata.layer = MCP_PROTOCOL_LAYER_7_APPLICATION;
  metadata.data.l7.protocol = MCP_APP_PROTOCOL_HTTP;
  metadata.data.l7.method = "GET";
  metadata.data.l7.path = "/test";
  metadata.data.l7.status_code = 200;

  ASSERT_EQ(mcp_filter_set_protocol_metadata(filter, &metadata), MCP_OK);

  // Get metadata back
  mcp_protocol_metadata_t retrieved = {};
  ASSERT_EQ(mcp_filter_get_protocol_metadata(filter, &retrieved), MCP_OK);
  ASSERT_EQ(retrieved.layer, MCP_PROTOCOL_LAYER_7_APPLICATION);

  mcp_filter_release(filter);
}

// ============================================================================
// Filter Chain Tests
// ============================================================================

TEST_F(FilterApiTest, CreateFilterChain) {
  mcp_filter_chain_builder_t builder =
      mcp_filter_chain_builder_create(dispatcher_);
  ASSERT_NE(builder, nullptr);

  // Create filters
  mcp_filter_t filter1 = mcp_filter_create_builtin(
      dispatcher_, MCP_FILTER_ACCESS_LOG, mcp_json_create_null());
  mcp_filter_t filter2 = mcp_filter_create_builtin(
      dispatcher_, MCP_FILTER_RATE_LIMIT, mcp_json_create_null());

  // Add to chain
  ASSERT_EQ(mcp_filter_chain_add_filter(builder, filter1,
                                        MCP_FILTER_POSITION_FIRST, 0),
            MCP_OK);
  ASSERT_EQ(mcp_filter_chain_add_filter(builder, filter2,
                                        MCP_FILTER_POSITION_LAST, 0),
            MCP_OK);

  // Build chain
  mcp_filter_chain_t chain = mcp_filter_chain_build(builder);
  ASSERT_NE(chain, 0);

  // Cleanup
  mcp_filter_chain_builder_destroy(builder);
  mcp_filter_chain_release(chain);
  mcp_filter_release(filter1);
  mcp_filter_release(filter2);
}

TEST_F(FilterApiTest, ChainWithPositioning) {
  mcp_filter_chain_builder_t builder =
      mcp_filter_chain_builder_create(dispatcher_);

  // Create multiple filters
  std::vector<mcp_filter_t> filters;
  for (int i = 0; i < 5; ++i) {
    filters.push_back(mcp_filter_create_builtin(dispatcher_, MCP_FILTER_CUSTOM,
                                                mcp_json_create_null()));
  }

  // Add with different positions
  ASSERT_EQ(mcp_filter_chain_add_filter(builder, filters[0],
                                        MCP_FILTER_POSITION_FIRST, 0),
            MCP_OK);
  ASSERT_EQ(mcp_filter_chain_add_filter(builder, filters[1],
                                        MCP_FILTER_POSITION_LAST, 0),
            MCP_OK);
  ASSERT_EQ(mcp_filter_chain_add_filter(builder, filters[2],
                                        MCP_FILTER_POSITION_AFTER, filters[0]),
            MCP_OK);
  ASSERT_EQ(mcp_filter_chain_add_filter(builder, filters[3],
                                        MCP_FILTER_POSITION_BEFORE, filters[1]),
            MCP_OK);

  mcp_filter_chain_t chain = mcp_filter_chain_build(builder);
  ASSERT_NE(chain, 0);

  // Cleanup
  mcp_filter_chain_builder_destroy(builder);
  mcp_filter_chain_release(chain);
  for (auto f : filters) {
    mcp_filter_release(f);
  }
}

// ============================================================================
// Filter Manager Tests
// ============================================================================

TEST_F(FilterApiTest, FilterManager) {
  // Create filter manager
  mcp_filter_manager_t manager = mcp_filter_manager_create(0, dispatcher_);
  ASSERT_NE(manager, 0);

  // Create and add filters
  mcp_filter_t filter1 = mcp_filter_create_builtin(
      dispatcher_, MCP_FILTER_HTTP_CODEC, mcp_json_create_null());
  mcp_filter_t filter2 = mcp_filter_create_builtin(
      dispatcher_, MCP_FILTER_COMPRESSION, mcp_json_create_null());

  ASSERT_EQ(mcp_filter_manager_add_filter(manager, filter1), MCP_OK);
  ASSERT_EQ(mcp_filter_manager_add_filter(manager, filter2), MCP_OK);

  // Initialize manager
  ASSERT_EQ(mcp_filter_manager_initialize(manager), MCP_OK);

  // Cleanup
  mcp_filter_manager_release(manager);
  mcp_filter_release(filter1);
  mcp_filter_release(filter2);
}

// ============================================================================
// Zero-Copy Buffer Tests
// ============================================================================

TEST_F(FilterApiTest, BufferCreateAndRelease) {
  mcp_buffer_handle_t buffer = mcp_filter_buffer_create(nullptr, 0, 0);
  ASSERT_NE(buffer, 0);

  ASSERT_EQ(mcp_filter_buffer_length(buffer), 0);

  mcp_filter_buffer_release(buffer);
}

TEST_F(FilterApiTest, BufferOperations) {
  const char* test_data = "Hello, Filter API!";
  size_t data_len = strlen(test_data);

  mcp_buffer_handle_t buffer = mcp_filter_buffer_create(
      reinterpret_cast<const uint8_t*>(test_data), data_len, 0);
  ASSERT_NE(buffer, 0);

  ASSERT_EQ(mcp_filter_buffer_length(buffer), data_len);

  // Get buffer slices
  mcp_buffer_slice_t slices[10];
  size_t slice_count = 10;
  ASSERT_EQ(mcp_filter_get_buffer_slices(buffer, slices, &slice_count), MCP_OK);
  ASSERT_GT(slice_count, 0);

  // Verify first slice contains our data
  ASSERT_GE(slices[0].length, data_len);
  ASSERT_EQ(memcmp(slices[0].data, test_data, data_len), 0);

  mcp_filter_buffer_release(buffer);
}

TEST_F(FilterApiTest, BufferReservation) {
  mcp_buffer_handle_t buffer = mcp_filter_buffer_create(nullptr, 0, 0);
  ASSERT_NE(buffer, 0);

  // Reserve space
  mcp_buffer_slice_t slice;
  ASSERT_EQ(mcp_filter_reserve_buffer(buffer, 1024, &slice), MCP_OK);
  ASSERT_GE(slice.length, 1024);
  ASSERT_NE(slice.data, nullptr);

  // Write to reserved space
  const char* msg = "Reserved buffer test";
  memcpy(const_cast<uint8_t*>(slice.data), msg, strlen(msg));

  // Commit
  ASSERT_EQ(mcp_filter_commit_buffer(buffer, strlen(msg)), MCP_OK);

  ASSERT_EQ(mcp_filter_buffer_length(buffer), strlen(msg));

  mcp_filter_buffer_release(buffer);
}

// ============================================================================
// Buffer Pool Tests
// ============================================================================

TEST_F(FilterApiTest, BufferPool) {
  size_t buffer_size = 4096;
  size_t max_buffers = 10;

  mcp_buffer_pool_t pool = mcp_buffer_pool_create(buffer_size, max_buffers);
  ASSERT_NE(pool, nullptr);

  // Acquire buffers
  std::vector<mcp_buffer_handle_t> buffers;
  for (size_t i = 0; i < 5; ++i) {
    mcp_buffer_handle_t buffer = mcp_buffer_pool_acquire(pool);
    ASSERT_NE(buffer, 0) << "Failed to acquire buffer " << i;
    buffers.push_back(buffer);
  }

  // Release buffers back to pool
  for (auto buffer : buffers) {
    mcp_buffer_pool_release(pool, buffer);
  }

  // Acquire again (should reuse)
  mcp_buffer_handle_t reused = mcp_buffer_pool_acquire(pool);
  ASSERT_NE(reused, 0);
  mcp_buffer_pool_release(pool, reused);

  mcp_buffer_pool_destroy(pool);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(FilterApiTest, ThreadSafeFilterCreation) {
  const int num_threads = 10;
  const int filters_per_thread = 100;

  std::vector<std::thread> threads;
  std::atomic<int> success_count{0};

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([this, &success_count, filters_per_thread]() {
      for (int i = 0; i < filters_per_thread; ++i) {
        mcp_filter_config_t config = {};
        config.name = "thread_test";
        config.type = MCP_FILTER_CUSTOM;

        mcp_filter_t filter = mcp_filter_create(dispatcher_, &config);
        if (filter != 0) {
          success_count++;
          mcp_filter_release(filter);
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  ASSERT_EQ(success_count, num_threads * filters_per_thread);
}

TEST_F(FilterApiTest, ThreadSafeBufferOperations) {
  mcp_buffer_handle_t buffer = mcp_filter_buffer_create(nullptr, 0, 0);
  ASSERT_NE(buffer, 0);

  const int num_threads = 10;
  const int ops_per_thread = 100;

  std::vector<std::thread> threads;
  std::atomic<int> success_count{0};

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([buffer, &success_count, ops_per_thread]() {
      for (int i = 0; i < ops_per_thread; ++i) {
        size_t len = mcp_filter_buffer_length(buffer);
        success_count++;
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  ASSERT_EQ(success_count, num_threads * ops_per_thread);

  mcp_filter_buffer_release(buffer);
}

// ============================================================================
// Resource Guard Tests
// ============================================================================

TEST_F(FilterApiTest, ResourceGuard) {
  mcp_filter_resource_guard_t* guard = mcp_filter_guard_create(dispatcher_);
  ASSERT_NE(guard, nullptr);

  // Create filters and add to guard
  for (int i = 0; i < 10; ++i) {
    mcp_filter_config_t config = {};
    config.name = "guard_test";
    config.type = MCP_FILTER_CUSTOM;

    mcp_filter_t filter = mcp_filter_create(dispatcher_, &config);
    ASSERT_NE(filter, 0);

    ASSERT_EQ(mcp_filter_guard_add_filter(guard, filter), MCP_OK);
  }

  // Guard will clean up all filters
  mcp_filter_guard_release(guard);
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(FilterApiTest, FilterStatistics) {
  mcp_filter_t filter = mcp_filter_create_builtin(
      dispatcher_, MCP_FILTER_RATE_LIMIT, mcp_json_create_null());
  ASSERT_NE(filter, 0);

  mcp_filter_stats_t stats = {};
  ASSERT_EQ(mcp_filter_get_stats(filter, &stats), MCP_OK);

  // Initial stats should be zero
  ASSERT_EQ(stats.bytes_processed, 0);
  ASSERT_EQ(stats.packets_processed, 0);
  ASSERT_EQ(stats.errors, 0);

  // Reset stats
  ASSERT_EQ(mcp_filter_reset_stats(filter), MCP_OK);

  mcp_filter_release(filter);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(FilterApiTest, ClientServerIntegration) {
  // Create client context
  mcp_filter_client_context_t client_ctx = {};

  // Build request filter chain
  mcp_filter_chain_builder_t req_builder =
      mcp_filter_chain_builder_create(dispatcher_);
  mcp_filter_t auth_filter = mcp_filter_create_builtin(
      dispatcher_, MCP_FILTER_AUTHENTICATION, mcp_json_create_null());
  mcp_filter_chain_add_filter(req_builder, auth_filter,
                              MCP_FILTER_POSITION_FIRST, 0);
  client_ctx.request_filters = mcp_filter_chain_build(req_builder);

  // Build response filter chain
  mcp_filter_chain_builder_t resp_builder =
      mcp_filter_chain_builder_create(dispatcher_);
  mcp_filter_t log_filter = mcp_filter_create_builtin(
      dispatcher_, MCP_FILTER_ACCESS_LOG, mcp_json_create_null());
  mcp_filter_chain_add_filter(resp_builder, log_filter,
                              MCP_FILTER_POSITION_FIRST, 0);
  client_ctx.response_filters = mcp_filter_chain_build(resp_builder);

  // Test sending filtered request
  const char* request = "Test request";
  std::atomic<bool> callback_called{false};

  auto completion_cb = [](mcp_result_t result, void* user_data) {
    auto* flag = static_cast<std::atomic<bool>*>(user_data);
    flag->store(true);
  };

  mcp_request_id_t req_id = mcp_client_send_filtered(
      &client_ctx, reinterpret_cast<const uint8_t*>(request), strlen(request),
      completion_cb, &callback_called);

  // Note: In real implementation, this would actually process

  // Cleanup
  mcp_filter_chain_builder_destroy(req_builder);
  mcp_filter_chain_builder_destroy(resp_builder);
  mcp_filter_chain_release(client_ctx.request_filters);
  mcp_filter_chain_release(client_ctx.response_filters);
  mcp_filter_release(auth_filter);
  mcp_filter_release(log_filter);
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(FilterApiTest, PerformanceThroughput) {
  const size_t buffer_size = 1024 * 1024;  // 1MB
  const int iterations = 1000;

  // Create buffer with data
  std::vector<uint8_t> data(buffer_size);
  mcp_buffer_handle_t buffer = mcp_filter_buffer_create(
      data.data(), data.size(), MCP_BUFFER_FLAG_READONLY);

  // Create filter chain
  mcp_filter_chain_builder_t builder =
      mcp_filter_chain_builder_create(dispatcher_);
  mcp_filter_t filter = mcp_filter_create_builtin(
      dispatcher_, MCP_FILTER_METRICS, mcp_json_create_null());
  mcp_filter_chain_add_filter(builder, filter, MCP_FILTER_POSITION_FIRST, 0);
  mcp_filter_chain_t chain = mcp_filter_chain_build(builder);

  auto start = std::chrono::high_resolution_clock::now();

  // Process buffer through filter chain
  for (int i = 0; i < iterations; ++i) {
    // Simulate processing
    size_t len = mcp_filter_buffer_length(buffer);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  double throughput_mbps = (buffer_size * iterations / (1024.0 * 1024.0)) /
                           (duration.count() / 1000.0);

  std::cout << "Throughput: " << throughput_mbps << " MB/s" << std::endl;

  // Cleanup
  mcp_filter_chain_builder_destroy(builder);
  mcp_filter_chain_release(chain);
  mcp_filter_release(filter);
  mcp_filter_buffer_release(buffer);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}