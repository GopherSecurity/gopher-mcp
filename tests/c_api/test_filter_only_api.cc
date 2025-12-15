/**
 * @file test_filter_only_api.cc
 * @brief Unit tests for Filter-Only C API
 *
 * Tests the minimal filter-only API designed for hybrid SDK integration
 * where the official MCP SDK handles protocol operations and Gopher filters
 * provide enterprise-grade filtering capabilities.
 */

#include <chrono>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/c_api/mcp_c_api.h"
#include "mcp/c_api/mcp_c_api_json.h"
#include "mcp/c_api/mcp_c_filter_only_api.h"
#include "mcp/filter/filter_registry.h"

// No need for extern declarations - they're in mcp_c_api.h

// Test fixture for filter-only API tests
class FilterOnlyAPITest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::cerr << "[SetUp] ENTRY POINT\n" << std::flush;
    std::cout << "[SetUp] Starting test setup...\n" << std::flush;

    // Initialize MCP library
    std::cerr << "[SetUp] About to call mcp_init\n" << std::flush;
    std::cout << "[SetUp] Calling mcp_init...\n" << std::flush;
    mcp_result_t result = mcp_init(nullptr);
    std::cerr << "[SetUp] mcp_init returned, result=" << result << "\n"
              << std::flush;
    std::cerr << "[SetUp] About to ASSERT_EQ\n" << std::flush;
    ASSERT_EQ(result, MCP_OK);
    std::cerr << "[SetUp] ASSERT_EQ passed\n" << std::flush;
    std::cout << "[SetUp] mcp_init completed successfully\n" << std::flush;

    // Create dispatcher for tests
    std::cerr << "[SetUp] About to create dispatcher\n" << std::flush;
    std::cout << "[SetUp] Calling mcp_dispatcher_create...\n" << std::flush;
    dispatcher_ = mcp_dispatcher_create();
    std::cerr << "[SetUp] Dispatcher created\n" << std::flush;
    std::cerr << "[SetUp] About to ASSERT dispatcher != nullptr\n"
              << std::flush;
    ASSERT_NE(dispatcher_, nullptr) << "Failed to create dispatcher";
    std::cerr << "[SetUp] ASSERT passed\n" << std::flush;
    std::cout << "[SetUp] Dispatcher created: " << dispatcher_ << "\n"
              << std::flush;

    // Check thread status before starting
    std::cerr << "[SetUp] Calling mcp_dispatcher_is_thread\n" << std::flush;
    mcp_bool_t is_disp_thread = mcp_dispatcher_is_thread(dispatcher_);
    std::cerr << "[SetUp] mcp_dispatcher_is_thread returned: "
              << (int)is_disp_thread << "\n"
              << std::flush;
    std::cerr << "[SetUp] About to print result\n" << std::flush;
    std::cout << "[SetUp] Before thread start - is dispatcher thread: "
              << (is_disp_thread ? "YES" : "NO") << "\n"
              << std::flush;
    std::cerr << "[SetUp] Printed result\n" << std::flush;

    // Run dispatcher in background thread
    std::cerr << "[SetUp] About to start dispatcher thread\n" << std::flush;
    std::cout << "[SetUp] Starting dispatcher thread...\n" << std::flush;
    std::cerr << "[SetUp] Creating std::thread object\n" << std::flush;
    dispatcher_thread_ = std::thread([this]() {
      std::cerr << "[Dispatcher Thread] Lambda entry\n" << std::flush;
      std::cout
          << "[Dispatcher Thread] Entry - about to call mcp_dispatcher_run\n"
          << std::flush;
      std::cout
          << "[Dispatcher Thread] Checking if this is dispatcher thread...\n"
          << std::flush;
      mcp_bool_t is_disp = mcp_dispatcher_is_thread(dispatcher_);
      std::cout << "[Dispatcher Thread] Is dispatcher thread BEFORE run: "
                << (is_disp ? "YES" : "NO") << "\n"
                << std::flush;

      std::cerr << "[Dispatcher Thread] About to call mcp_dispatcher_run\n"
                << std::flush;
      mcp_dispatcher_run(dispatcher_);
      std::cerr << "[Dispatcher Thread] mcp_dispatcher_run returned\n"
                << std::flush;

      std::cout << "[Dispatcher Thread] Exit - mcp_dispatcher_run returned\n"
                << std::flush;
    });
    std::cerr << "[SetUp] std::thread created\n" << std::flush;
    std::cout << "[SetUp] Dispatcher thread launched\n" << std::flush;

    // Give dispatcher time to start
    std::cerr << "[SetUp] About to sleep\n" << std::flush;
    std::cout << "[SetUp] Sleeping 50ms to let dispatcher start...\n"
              << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cerr << "[SetUp] Sleep complete\n" << std::flush;

    // Check thread status after starting
    std::cerr << "[SetUp] Calling mcp_dispatcher_is_thread again\n"
              << std::flush;
    is_disp_thread = mcp_dispatcher_is_thread(dispatcher_);
    std::cout
        << "[SetUp] After thread start - main thread is dispatcher thread: "
        << (is_disp_thread ? "YES" : "NO") << "\n"
        << std::flush;

    std::cout << "[SetUp] Setup complete!\n" << std::flush;
  }

  void TearDown() override {
    std::cout << "[TearDown] Starting test cleanup...\n";

    // Mark dispatcher as shutting down - prevent new posts
    dispatcher_shutdown_requested_ = true;

    // Stop and clean up dispatcher
    if (dispatcher_) {
      std::cout << "[TearDown] Calling mcp_dispatcher_stop...\n";
      mcp_dispatcher_stop(dispatcher_);
      std::cout << "[TearDown] Dispatcher stopped\n";

      if (dispatcher_thread_.joinable()) {
        std::cout << "[TearDown] Joining dispatcher thread...\n";
        dispatcher_thread_.join();
        std::cout << "[TearDown] Dispatcher thread joined\n";
      }

      std::cout << "[TearDown] Calling mcp_dispatcher_destroy...\n";
      mcp_dispatcher_destroy(dispatcher_);
      std::cout << "[TearDown] Dispatcher destroyed\n";
      dispatcher_ = nullptr;
    }

    // Shutdown MCP library
    std::cout << "[TearDown] Calling mcp_shutdown...\n";
    mcp_shutdown();
    std::cout << "[TearDown] Cleanup complete!\n";
  }

  /**
   * Helper: Create chain-centric filter chain JSON configuration
   *
   * This creates a single filter chain object that matches the assembler
   * contract:
   * {
   *   "name": "default",
   *   "transport_type": "tcp",
   *   "filters": [...]
   * }
   */
  mcp_json_value_t createCanonicalConfig(bool with_warnings = false) {
    // Create chain-centric configuration (no listeners wrapper)
    auto config = mcp_json_create_object();

    // Set chain properties
    mcp_json_object_set(config, "name", mcp_json_create_string("default"));
    mcp_json_object_set(config, "transport_type",
                        mcp_json_create_string("tcp"));

    // Create filters array with some basic filters
    auto filters = mcp_json_create_array();

    // Add http.codec filter (context factory registered)
    auto http_filter = mcp_json_create_object();
    mcp_json_object_set(http_filter, "name",
                        mcp_json_create_string("http_codec"));
    mcp_json_object_set(http_filter, "type",
                        mcp_json_create_string("http.codec"));
    mcp_json_object_set(http_filter, "config", mcp_json_create_object());
    mcp_json_array_append(filters, http_filter);

    // Add sse.codec filter (context factory registered)
    auto sse_filter = mcp_json_create_object();
    mcp_json_object_set(sse_filter, "name",
                        mcp_json_create_string("sse_codec"));
    mcp_json_object_set(sse_filter, "type",
                        mcp_json_create_string("sse.codec"));
    mcp_json_object_set(sse_filter, "config", mcp_json_create_object());
    mcp_json_array_append(filters, sse_filter);

    // Optionally add filters that might generate warnings
    if (with_warnings) {
      // Could add filters with edge-case configurations here
    }

    mcp_json_object_set(config, "filters", filters);

    return config;
  }

  /**
   * Helper: Create invalid configuration (for negative tests)
   */
  mcp_json_value_t createInvalidConfig() {
    auto config = mcp_json_create_object();

    // Create chain with missing required fields
    mcp_json_object_set(config, "name",
                        mcp_json_create_string("invalid_chain"));
    // Missing "filters" array - this makes it invalid

    return config;
  }

  /**
   * Helper: Execute a function on dispatcher thread with result synchronization
   *
   * This ensures dispatcher-affine APIs are called from the correct thread.
   * Exceptions are captured and re-thrown on the calling thread.
   */
  template <typename Result>
  Result executeOnDispatcher(std::function<Result()> func) {
    // Check if dispatcher is still running
    if (dispatcher_shutdown_requested_) {
      throw std::runtime_error(
          "Cannot post to dispatcher after shutdown requested");
    }

    std::promise<Result> promise;
    auto future = promise.get_future();

    struct CallbackData {
      std::function<Result()> func;
      std::promise<Result>* promise;
    };

    auto* data = new CallbackData{std::move(func), &promise};

    mcp_result_t post_result = mcp_dispatcher_post(
        dispatcher_,
        [](void* user_data) {
          auto* cb_data = static_cast<CallbackData*>(user_data);
          try {
            // Execute function and set result
            cb_data->promise->set_value(cb_data->func());
          } catch (...) {
            // Capture exception to re-throw on calling thread
            cb_data->promise->set_exception(std::current_exception());
          }
          delete cb_data;
        },
        data);

    if (post_result != MCP_OK) {
      delete data;
      throw std::runtime_error("Failed to post to dispatcher");
    }

    // Wait for result and potentially re-throw exception
    return future.get();
  }

  mcp_dispatcher_t dispatcher_ = nullptr;
  std::thread dispatcher_thread_;
  std::atomic<bool> dispatcher_shutdown_requested_{false};
};

/* ============================================================================
 * Core Filter Registration Tests (NEW)
 * ============================================================================
 */

TEST_F(FilterOnlyAPITest, CoreFiltersAreRegistered) {
  // This test verifies that all core filters are successfully registered
  // in the filter registry when using static linking

  auto& registry = mcp::filter::FilterRegistry::instance();

  // Check that all 6 core filters are registered as context factories
  std::cout << "[TEST] Checking core filter registration...\n";

  // Check http.codec
  bool has_http = registry.hasContextFactory("http.codec");
  std::cout << "  http.codec: " << (has_http ? "REGISTERED" : "MISSING")
            << "\n";
  EXPECT_TRUE(has_http) << "http.codec filter not registered";

  // Check sse.codec
  bool has_sse = registry.hasContextFactory("sse.codec");
  std::cout << "  sse.codec: " << (has_sse ? "REGISTERED" : "MISSING") << "\n";
  EXPECT_TRUE(has_sse) << "sse.codec filter not registered";

  // Check json_rpc.dispatcher
  bool has_json_rpc = registry.hasContextFactory("json_rpc.dispatcher");
  std::cout << "  json_rpc.dispatcher: "
            << (has_json_rpc ? "REGISTERED" : "MISSING") << "\n";
  EXPECT_TRUE(has_json_rpc) << "json_rpc.dispatcher filter not registered";

  // Check rate_limit
  bool has_rate = registry.hasFactory("rate_limit");  // Traditional factory
  std::cout << "  rate_limit: " << (has_rate ? "REGISTERED" : "MISSING")
            << "\n";
  EXPECT_TRUE(has_rate) << "rate_limit filter not registered";

  // Check circuit_breaker
  bool has_breaker =
      registry.hasFactory("circuit_breaker");  // Traditional factory
  std::cout << "  circuit_breaker: " << (has_breaker ? "REGISTERED" : "MISSING")
            << "\n";
  EXPECT_TRUE(has_breaker) << "circuit_breaker filter not registered";

  // Check metrics
  bool has_metrics = registry.hasFactory("metrics");  // Traditional factory
  std::cout << "  metrics: " << (has_metrics ? "REGISTERED" : "MISSING")
            << "\n";
  EXPECT_TRUE(has_metrics) << "metrics filter not registered";

  // Print all registered factories for debugging
  std::cout << "\n[TEST] All registered context factories:\n";
  auto context_factories = registry.listContextFactories();
  for (const auto& name : context_factories) {
    std::cout << "  - " << name << "\n";
  }

  std::cout << "\n[TEST] All registered traditional factories:\n";
  auto trad_factories = registry.listFactories();
  for (const auto& name : trad_factories) {
    std::cout << "  - " << name << "\n";
  }

  // We should have at least 3 context factories and 3 traditional factories
  EXPECT_GE(context_factories.size(), 3u)
      << "Expected at least 3 context factories";
  EXPECT_GE(trad_factories.size(), 3u)
      << "Expected at least 3 traditional factories";
}

TEST_F(FilterOnlyAPITest, ValidateFilterConfiguration) {
  // Test validation with actual filter configurations
  auto config = mcp_json_create_object();
  mcp_json_object_set(config, "name", mcp_json_create_string("test_chain"));
  mcp_json_object_set(config, "transport_type", mcp_json_create_string("tcp"));

  auto filters = mcp_json_create_array();

  // Add http.codec filter
  auto http_filter = mcp_json_create_object();
  mcp_json_object_set(http_filter, "name",
                      mcp_json_create_string("http_codec"));
  mcp_json_object_set(http_filter, "type",
                      mcp_json_create_string("http.codec"));
  mcp_json_object_set(http_filter, "config", mcp_json_create_object());
  mcp_json_array_append(filters, http_filter);

  // Add sse.codec filter
  auto sse_filter = mcp_json_create_object();
  mcp_json_object_set(sse_filter, "name", mcp_json_create_string("sse_codec"));
  mcp_json_object_set(sse_filter, "type", mcp_json_create_string("sse.codec"));
  mcp_json_object_set(sse_filter, "config", mcp_json_create_object());
  mcp_json_array_append(filters, sse_filter);

  // Add json_rpc.dispatcher filter with configuration
  auto dispatcher_filter = mcp_json_create_object();
  mcp_json_object_set(dispatcher_filter, "name",
                      mcp_json_create_string("dispatcher"));
  mcp_json_object_set(dispatcher_filter, "type",
                      mcp_json_create_string("json_rpc.dispatcher"));
  mcp_json_object_set(dispatcher_filter, "config", mcp_json_create_object());
  mcp_json_array_append(filters, dispatcher_filter);

  mcp_json_object_set(config, "filters", filters);

  // Validate configuration
  mcp_filter_only_validation_result_t result;
  mcp_result_t status = mcp_filter_only_validate_json(config, &result);

  EXPECT_EQ(status, MCP_OK);

  // Print validation results
  if (!result.valid) {
    std::cout << "Validation errors:\n";
    for (size_t i = 0; i < result.error_count; i++) {
      std::cout << "  - " << result.errors[i] << "\n";
    }
  }

  // With proper filter registration, this should be valid
  EXPECT_TRUE(result.valid)
      << "Configuration with registered filters should be valid";

  mcp_filter_only_validation_result_free(&result);
  mcp_json_free(config);
}

/* ============================================================================
 * Validation Tests
 * ============================================================================
 */

TEST_F(FilterOnlyAPITest, ValidateValidConfiguration) {
  std::cerr << "[TEST] Entry to test body\n" << std::flush;
  std::cout << "[TEST] ValidateValidConfiguration starting...\n" << std::flush;

  // Check what thread we're on
  std::cerr << "[TEST] Calling mcp_dispatcher_is_thread\n" << std::flush;
  mcp_bool_t is_disp_thread = mcp_dispatcher_is_thread(dispatcher_);
  std::cout << "[TEST] Test thread is dispatcher thread: "
            << (is_disp_thread ? "YES" : "NO") << "\n"
            << std::flush;

  std::cout << "[TEST] Creating config...\n";
  mcp_json_value_t config = createCanonicalConfig();
  ASSERT_NE(config, nullptr);
  std::cout << "[TEST] Config created\n";

  // Debug: Check what filter types are registered
  std::cout << "[TEST] Checking registered filter types in registry...\n";
  // The registry should have been initialized by now through static init

  std::cout << "[TEST] Calling mcp_filter_only_validate_json...\n";
  mcp_filter_only_validation_result_t result;
  mcp_result_t status = mcp_filter_only_validate_json(config, &result);
  std::cout << "[TEST] Validation returned with status: " << status << "\n";

  // Print any errors for debugging
  if (result.error_count > 0) {
    std::cout << "Validation errors:\n";
    for (size_t i = 0; i < result.error_count; i++) {
      std::cout << "  - " << result.errors[i] << "\n";
    }
  }

  // We expect validation to succeed with proper filters
  EXPECT_EQ(status, MCP_OK);

  // With registered filters, the chain should be valid
  EXPECT_EQ(result.valid, MCP_TRUE);
  EXPECT_EQ(result.error_count, 0u);

  // Warnings are okay - just informational
  if (result.warning_count > 0) {
    std::cout << "Validation warnings:\n";
    for (size_t i = 0; i < result.warning_count; i++) {
      std::cout << "  - " << result.warnings[i] << "\n";
    }
  }

  mcp_filter_only_validation_result_free(&result);
  mcp_json_free(config);
}

TEST_F(FilterOnlyAPITest, ValidateInvalidConfiguration) {
  mcp_json_value_t config = createInvalidConfig();
  ASSERT_NE(config, nullptr);

  mcp_filter_only_validation_result_t result;
  mcp_result_t status = mcp_filter_only_validate_json(config, &result);

  // Validation should succeed (we got a result), but config should be invalid
  EXPECT_EQ(status, MCP_OK);
  EXPECT_EQ(result.valid, MCP_FALSE);
  EXPECT_GT(result.error_count, 0u);

  if (result.error_count > 0) {
    std::cout << "Expected validation errors:\n";
    for (size_t i = 0; i < result.error_count; i++) {
      std::cout << "  - " << result.errors[i] << "\n";
    }
  }

  mcp_filter_only_validation_result_free(&result);
  mcp_json_free(config);
}

TEST_F(FilterOnlyAPITest, ValidationResultFreeIsIdempotent) {
  mcp_json_value_t config = createCanonicalConfig();

  mcp_filter_only_validation_result_t result;
  mcp_filter_only_validate_json(config, &result);

  // Free once
  mcp_filter_only_validation_result_free(&result);

  // Free again - should be safe
  mcp_filter_only_validation_result_free(&result);

  // Free with NULL - should be safe
  mcp_filter_only_validation_result_free(nullptr);

  mcp_json_free(config);
}

/* ============================================================================
 * Chain Creation and Assembly Tests
 * ============================================================================
 */

TEST_F(FilterOnlyAPITest, AssembleChainFromValidConfig) {
  mcp_json_value_t config = createCanonicalConfig();
  ASSERT_NE(config, nullptr);

  // Execute assembly on dispatcher thread
  auto result = executeOnDispatcher<
      std::pair<mcp_result_t, mcp_filter_only_assembly_result_t>>(
      [this, config]() {
        mcp_filter_only_assembly_result_t result;
        mcp_result_t status =
            mcp_filter_only_assemble_from_json(dispatcher_, config, &result);
        return std::make_pair(status, result);
      });

  mcp_result_t status = result.first;
  mcp_filter_only_assembly_result_t assembly_result = result.second;

  EXPECT_EQ(status, MCP_OK);
  EXPECT_EQ(assembly_result.success, MCP_TRUE);
  EXPECT_NE(assembly_result.chain, 0u);
  EXPECT_GE(assembly_result.created_filter_count, 2u);  // At least 2 filters

  if (assembly_result.success) {
    std::cout << "Created chain with " << assembly_result.created_filter_count
              << " filters:\n";
    for (size_t i = 0; i < assembly_result.created_filter_count; i++) {
      std::cout << "  - " << assembly_result.created_filters[i] << "\n";
    }

    // Take ownership of chain and clean it up on dispatcher thread
    mcp_filter_only_chain_t chain = assembly_result.chain;
    assembly_result.chain = 0;

    executeOnDispatcher<int>([chain]() {
      mcp_filter_only_chain_release(chain);
      return 0;
    });
  }

  mcp_filter_only_assembly_result_free(&assembly_result);
  mcp_json_free(config);
}

TEST_F(FilterOnlyAPITest, AssembleChainFromInvalidConfig) {
  mcp_json_value_t config = createInvalidConfig();
  ASSERT_NE(config, nullptr);

  // Execute assembly on dispatcher thread
  auto result = executeOnDispatcher<
      std::pair<mcp_result_t, mcp_filter_only_assembly_result_t>>(
      [this, config]() {
        mcp_filter_only_assembly_result_t result;
        mcp_result_t status =
            mcp_filter_only_assemble_from_json(dispatcher_, config, &result);
        return std::make_pair(status, result);
      });

  mcp_result_t status = result.first;
  mcp_filter_only_assembly_result_t assembly_result = result.second;

  // Assembly should return a result, but success should be false
  EXPECT_EQ(status, MCP_OK);
  EXPECT_EQ(assembly_result.success, MCP_FALSE);
  EXPECT_EQ(assembly_result.chain, 0u);
  EXPECT_NE(assembly_result.error_message, nullptr);

  if (assembly_result.error_message) {
    std::cout << "Expected assembly error: " << assembly_result.error_message
              << "\n";
  }

  mcp_filter_only_assembly_result_free(&assembly_result);
  mcp_json_free(config);
}

TEST_F(FilterOnlyAPITest, CreateChainSimpleAPI) {
  mcp_json_value_t config = createCanonicalConfig();
  ASSERT_NE(config, nullptr);

  // Create chain on dispatcher thread
  mcp_filter_only_chain_t chain =
      executeOnDispatcher<mcp_filter_only_chain_t>([this, config]() {
        return mcp_filter_only_chain_create_from_json(dispatcher_, config);
      });

  EXPECT_NE(chain, 0u);

  if (chain) {
    executeOnDispatcher<int>([chain]() {
      mcp_filter_only_chain_release(chain);
      return 0;
    });
  }

  mcp_json_free(config);
}

TEST_F(FilterOnlyAPITest, CreateChainRequiresDispatcher) {
  mcp_json_value_t config = createCanonicalConfig();

  // Try to create without dispatcher - should fail
  mcp_filter_only_chain_t chain =
      mcp_filter_only_chain_create_from_json(nullptr, config);
  EXPECT_EQ(chain, 0u);

  mcp_json_free(config);
}

/* ============================================================================
 * Chain Lifecycle Tests
 * ============================================================================
 */

TEST_F(FilterOnlyAPITest, ChainReleaseIsIdempotent) {
  // Release with NULL handle on dispatcher - should be safe
  executeOnDispatcher<int>([]() {
    mcp_filter_only_chain_release(0);
    return 0;
  });

  // Create and release chain on dispatcher thread
  mcp_json_value_t config = createCanonicalConfig();

  mcp_filter_only_chain_t chain =
      executeOnDispatcher<mcp_filter_only_chain_t>([this, config]() {
        return mcp_filter_only_chain_create_from_json(dispatcher_, config);
      });

  ASSERT_NE(chain, 0u);

  executeOnDispatcher<int>([chain]() {
    mcp_filter_only_chain_release(chain);
    return 0;
  });

  mcp_json_free(config);
}

TEST_F(FilterOnlyAPITest, ChainRetainReleaseCycle) {
  mcp_json_value_t config = createCanonicalConfig();

  mcp_filter_only_chain_t chain =
      executeOnDispatcher<mcp_filter_only_chain_t>([this, config]() {
        return mcp_filter_only_chain_create_from_json(dispatcher_, config);
      });

  ASSERT_NE(chain, 0u);

  // Retain and release on dispatcher thread
  executeOnDispatcher<int>([chain]() {
    // Retain twice
    mcp_filter_only_chain_retain(chain);
    mcp_filter_only_chain_retain(chain);

    // Release three times (initial + 2 retains)
    mcp_filter_only_chain_release(chain);
    mcp_filter_only_chain_release(chain);
    mcp_filter_only_chain_release(chain);
    return 0;
  });

  mcp_json_free(config);
}

TEST_F(FilterOnlyAPITest, CloneChain) {
  mcp_json_value_t config = createCanonicalConfig();

  auto chains = executeOnDispatcher<
      std::pair<mcp_filter_only_chain_t, mcp_filter_only_chain_t>>(
      [this, config]() {
        mcp_filter_only_chain_t chain =
            mcp_filter_only_chain_create_from_json(dispatcher_, config);
        mcp_filter_only_chain_t cloned = 0;

        if (chain) {
          // Clone the chain
          cloned = mcp_filter_only_chain_clone(chain);
        }

        return std::make_pair(chain, cloned);
      });

  mcp_filter_only_chain_t chain = chains.first;
  mcp_filter_only_chain_t cloned = chains.second;

  ASSERT_NE(chain, 0u);
  EXPECT_NE(cloned, 0u);
  EXPECT_NE(cloned, chain);  // Should be different handles

  // Release both chains on dispatcher thread
  executeOnDispatcher<int>([chain, cloned]() {
    mcp_filter_only_chain_release(chain);
    if (cloned) {
      mcp_filter_only_chain_release(cloned);
    }
    return 0;
  });

  mcp_json_free(config);
}

/* ============================================================================
 * Runtime Filter Control Tests
 * ============================================================================
 */

TEST_F(FilterOnlyAPITest, EnableDisableFilter) {
  mcp_json_value_t config = createCanonicalConfig();

  auto result = executeOnDispatcher<
      std::tuple<mcp_filter_only_chain_t, mcp_result_t, mcp_result_t>>(
      [this, config]() {
        mcp_filter_only_chain_t chain =
            mcp_filter_only_chain_create_from_json(dispatcher_, config);
        if (!chain) {
          return std::make_tuple((mcp_filter_only_chain_t)0, MCP_ERROR_UNKNOWN,
                                 MCP_ERROR_UNKNOWN);
        }

        // Disable a filter
        mcp_result_t status1 =
            mcp_filter_only_set_filter_enabled(chain, "http_codec", MCP_FALSE);

        // Re-enable the filter
        mcp_result_t status2 =
            mcp_filter_only_set_filter_enabled(chain, "http_codec", MCP_TRUE);

        return std::make_tuple(chain, status1, status2);
      });

  mcp_filter_only_chain_t chain = std::get<0>(result);
  mcp_result_t status1 = std::get<1>(result);
  mcp_result_t status2 = std::get<2>(result);

  ASSERT_NE(chain, 0u);
  EXPECT_EQ(status1, MCP_OK);
  EXPECT_EQ(status2, MCP_OK);

  executeOnDispatcher<int>([chain]() {
    mcp_filter_only_chain_release(chain);
    return 0;
  });

  mcp_json_free(config);
}

TEST_F(FilterOnlyAPITest, SetFilterEnabledNonExistentFilter) {
  mcp_json_value_t config = createCanonicalConfig();

  auto result =
      executeOnDispatcher<std::pair<mcp_filter_only_chain_t, mcp_result_t>>(
          [this, config]() {
            mcp_filter_only_chain_t chain =
                mcp_filter_only_chain_create_from_json(dispatcher_, config);
            if (!chain) {
              return std::make_pair((mcp_filter_only_chain_t)0,
                                    MCP_ERROR_UNKNOWN);
            }

            // Try to disable a filter that doesn't exist - should succeed
            // (no-op)
            mcp_result_t status = mcp_filter_only_set_filter_enabled(
                chain, "nonexistent_filter", MCP_FALSE);

            return std::make_pair(chain, status);
          });

  mcp_filter_only_chain_t chain = result.first;
  mcp_result_t status = result.second;

  ASSERT_NE(chain, 0u);
  EXPECT_EQ(status, MCP_OK);  // Should not fail, just no-op

  executeOnDispatcher<int>([chain]() {
    mcp_filter_only_chain_release(chain);
    return 0;
  });

  mcp_json_free(config);
}

/* ============================================================================
 * Export/Import Tests
 * ============================================================================
 */

TEST_F(FilterOnlyAPITest, ExportChainConfiguration) {
  mcp_json_value_t config = createCanonicalConfig();

  auto result =
      executeOnDispatcher<std::pair<mcp_filter_only_chain_t, mcp_json_value_t>>(
          [this, config]() {
            mcp_filter_only_chain_t chain =
                mcp_filter_only_chain_create_from_json(dispatcher_, config);
            mcp_json_value_t exported = nullptr;

            if (chain) {
              // Export chain configuration
              exported = mcp_filter_only_chain_export_to_json(chain);
            }

            return std::make_pair(chain, exported);
          });

  mcp_filter_only_chain_t chain = result.first;
  mcp_json_value_t exported = result.second;

  ASSERT_NE(chain, 0u);
  EXPECT_NE(exported, nullptr);

  if (exported) {
    // Exported config should be valid JSON
    char* json_str = mcp_json_stringify(exported);
    EXPECT_NE(json_str, nullptr);

    if (json_str) {
      std::cout << "Exported chain config:\n" << json_str << "\n";
      mcp_free(json_str);
    }

    mcp_json_free(exported);
  }

  executeOnDispatcher<int>([chain]() {
    mcp_filter_only_chain_release(chain);
    return 0;
  });

  mcp_json_free(config);
}

TEST_F(FilterOnlyAPITest, ExportImportRoundTrip) {
  // Create original chain
  mcp_json_value_t config = createCanonicalConfig();

  auto result = executeOnDispatcher<std::tuple<
      mcp_filter_only_chain_t, mcp_filter_only_chain_t, mcp_json_value_t>>(
      [this, config]() {
        mcp_filter_only_chain_t chain1 =
            mcp_filter_only_chain_create_from_json(dispatcher_, config);
        mcp_json_value_t exported = nullptr;
        mcp_filter_only_chain_t chain2 = 0;

        if (chain1) {
          // Export configuration
          exported = mcp_filter_only_chain_export_to_json(chain1);

          if (exported) {
            // Create new chain from exported config
            chain2 =
                mcp_filter_only_chain_create_from_json(dispatcher_, exported);
          }
        }

        return std::make_tuple(chain1, chain2, exported);
      });

  mcp_filter_only_chain_t chain1 = std::get<0>(result);
  mcp_filter_only_chain_t chain2 = std::get<1>(result);
  mcp_json_value_t exported = std::get<2>(result);

  ASSERT_NE(chain1, 0u);
  ASSERT_NE(exported, nullptr);
  EXPECT_NE(chain2, 0u);

  // Release both chains on dispatcher thread
  executeOnDispatcher<int>([chain1, chain2]() {
    if (chain2) {
      mcp_filter_only_chain_release(chain2);
    }
    mcp_filter_only_chain_release(chain1);
    return 0;
  });

  mcp_json_free(exported);
  mcp_json_free(config);
}

/* ============================================================================
 * Statistics Tests
 * ============================================================================
 */

TEST_F(FilterOnlyAPITest, GetChainStatistics) {
  mcp_json_value_t config = createCanonicalConfig();

  auto result =
      executeOnDispatcher<std::tuple<mcp_filter_only_chain_t, mcp_result_t,
                                     mcp_filter_only_stats_t>>(
          [this, config]() {
            mcp_filter_only_chain_t chain =
                mcp_filter_only_chain_create_from_json(dispatcher_, config);
            mcp_filter_only_stats_t stats{};
            mcp_result_t status = MCP_ERROR_UNKNOWN;

            if (chain) {
              status = mcp_filter_only_get_stats(chain, &stats);
            }

            return std::make_tuple(chain, status, stats);
          });

  mcp_filter_only_chain_t chain = std::get<0>(result);
  mcp_result_t status = std::get<1>(result);
  mcp_filter_only_stats_t stats = std::get<2>(result);

  ASSERT_NE(chain, 0u);
  EXPECT_EQ(status, MCP_OK);

  // Stats should be initialized
  std::cout << "Chain Statistics:\n";
  std::cout << "  Total processed: " << stats.total_processed << "\n";
  std::cout << "  Errors: " << stats.total_errors << "\n";
  std::cout << "  Bypassed: " << stats.total_bypassed << "\n";
  std::cout << "  Avg latency: " << stats.avg_latency_ms << " ms\n";
  std::cout << "  Max latency: " << stats.max_latency_ms << " ms\n";
  std::cout << "  Throughput: " << stats.throughput_mbps << " Mbps\n";
  std::cout << "  Active filters: " << stats.active_filters << "\n";

  executeOnDispatcher<int>([chain]() {
    mcp_filter_only_chain_release(chain);
    return 0;
  });

  mcp_json_free(config);
}

TEST_F(FilterOnlyAPITest, GetStatsRequiresValidHandle) {
  mcp_result_t status = executeOnDispatcher<mcp_result_t>([]() {
    mcp_filter_only_stats_t stats;
    // Get stats with invalid handle - should fail gracefully
    return mcp_filter_only_get_stats(0, &stats);
  });

  EXPECT_NE(status, MCP_OK);
}

/* ============================================================================
 * Advanced Operations Tests
 * ============================================================================
 */

TEST_F(FilterOnlyAPITest, PauseResumeChain) {
  mcp_json_value_t config = createCanonicalConfig();

  auto result =
      executeOnDispatcher<std::tuple<mcp_filter_only_chain_t, mcp_chain_state_t,
                                     mcp_result_t, mcp_result_t>>(
          [this, config]() {
            mcp_filter_only_chain_t chain =
                mcp_filter_only_chain_create_from_json(dispatcher_, config);
            mcp_chain_state_t state = MCP_CHAIN_STATE_IDLE;
            mcp_result_t pause_status = MCP_ERROR_UNKNOWN;
            mcp_result_t resume_status = MCP_ERROR_UNKNOWN;

            if (chain) {
              // Get initial state
              state = mcp_filter_only_chain_get_state(chain);

              // Pause chain
              pause_status = mcp_filter_only_chain_pause(chain);

              // Resume chain
              resume_status = mcp_filter_only_chain_resume(chain);
            }

            return std::make_tuple(chain, state, pause_status, resume_status);
          });

  mcp_filter_only_chain_t chain = std::get<0>(result);
  mcp_chain_state_t state = std::get<1>(result);
  mcp_result_t pause_status = std::get<2>(result);
  mcp_result_t resume_status = std::get<3>(result);

  ASSERT_NE(chain, 0u);
  EXPECT_TRUE(state == MCP_CHAIN_STATE_IDLE ||
              state == MCP_CHAIN_STATE_COMPLETED);
  EXPECT_EQ(pause_status, MCP_OK);
  EXPECT_EQ(resume_status, MCP_OK);

  executeOnDispatcher<int>([chain]() {
    mcp_filter_only_chain_release(chain);
    return 0;
  });

  mcp_json_free(config);
}

TEST_F(FilterOnlyAPITest, ResetChain) {
  mcp_json_value_t config = createCanonicalConfig();

  auto result =
      executeOnDispatcher<std::pair<mcp_filter_only_chain_t, mcp_result_t>>(
          [this, config]() {
            mcp_filter_only_chain_t chain =
                mcp_filter_only_chain_create_from_json(dispatcher_, config);
            mcp_result_t status = MCP_ERROR_UNKNOWN;

            if (chain) {
              // Reset chain - should return to initial state
              status = mcp_filter_only_chain_reset(chain);

              // After reset, stats should be cleared
              mcp_filter_only_stats_t stats;
              mcp_filter_only_get_stats(chain, &stats);
              // Stats might not be zero depending on implementation, but test
              // should not crash
            }

            return std::make_pair(chain, status);
          });

  mcp_filter_only_chain_t chain = result.first;
  mcp_result_t status = result.second;

  ASSERT_NE(chain, 0u);
  EXPECT_EQ(status, MCP_OK);

  executeOnDispatcher<int>([chain]() {
    mcp_filter_only_chain_release(chain);
    return 0;
  });

  mcp_json_free(config);
}

/* ============================================================================
 * Memory Leak Detection Tests
 * ============================================================================
 *
 * These tests create and destroy many chains to help detect memory leaks
 * when running under valgrind or similar tools.
 */

TEST_F(FilterOnlyAPITest, NoLeaksMultipleChainCreation) {
  mcp_json_value_t config = createCanonicalConfig();

  // Create and destroy 100 chains on dispatcher thread
  executeOnDispatcher<int>([this, config]() {
    for (int i = 0; i < 100; i++) {
      mcp_filter_only_chain_t chain =
          mcp_filter_only_chain_create_from_json(dispatcher_, config);
      if (chain) {
        mcp_filter_only_chain_release(chain);
      }
    }
    return 0;
  });

  mcp_json_free(config);
}

TEST_F(FilterOnlyAPITest, NoLeaksValidationCycles) {
  // Validate 100 times - validation doesn't need dispatcher thread
  for (int i = 0; i < 100; i++) {
    mcp_json_value_t config = createCanonicalConfig();

    mcp_filter_only_validation_result_t result;
    mcp_filter_only_validate_json(config, &result);
    mcp_filter_only_validation_result_free(&result);

    mcp_json_free(config);
  }
}

TEST_F(FilterOnlyAPITest, NoLeaksAssemblyCycles) {
  mcp_json_value_t config = createCanonicalConfig();

  // Assemble and destroy 50 chains on dispatcher thread
  executeOnDispatcher<int>([this, config]() {
    for (int i = 0; i < 50; i++) {
      mcp_filter_only_assembly_result_t result;
      mcp_filter_only_assemble_from_json(dispatcher_, config, &result);

      if (result.success && result.chain) {
        mcp_filter_only_chain_t chain = result.chain;
        result.chain = 0;
        mcp_filter_only_chain_release(chain);
      }

      mcp_filter_only_assembly_result_free(&result);
    }
    return 0;
  });

  mcp_json_free(config);
}

TEST_F(FilterOnlyAPITest, NoLeaksExportImportCycles) {
  mcp_json_value_t config = createCanonicalConfig();

  mcp_filter_only_chain_t chain =
      executeOnDispatcher<mcp_filter_only_chain_t>([this, config]() {
        return mcp_filter_only_chain_create_from_json(dispatcher_, config);
      });

  ASSERT_NE(chain, 0u);

  // Export and re-import 50 times on dispatcher thread
  executeOnDispatcher<int>([chain]() {
    for (int i = 0; i < 50; i++) {
      mcp_json_value_t exported = mcp_filter_only_chain_export_to_json(chain);
      if (exported) {
        mcp_json_free(exported);
      }
    }
    return 0;
  });

  executeOnDispatcher<int>([chain]() {
    mcp_filter_only_chain_release(chain);
    return 0;
  });

  mcp_json_free(config);
}

TEST_F(FilterOnlyAPITest, NoLeaksCloneCycles) {
  mcp_json_value_t config = createCanonicalConfig();

  mcp_filter_only_chain_t chain =
      executeOnDispatcher<mcp_filter_only_chain_t>([this, config]() {
        return mcp_filter_only_chain_create_from_json(dispatcher_, config);
      });

  ASSERT_NE(chain, 0u);

  // Clone and destroy 50 times on dispatcher thread
  executeOnDispatcher<int>([chain]() {
    for (int i = 0; i < 50; i++) {
      mcp_filter_only_chain_t cloned = mcp_filter_only_chain_clone(chain);
      if (cloned) {
        mcp_filter_only_chain_release(cloned);
      }
    }
    return 0;
  });

  executeOnDispatcher<int>([chain]() {
    mcp_filter_only_chain_release(chain);
    return 0;
  });

  mcp_json_free(config);
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================
 */

int main(int argc, char** argv) {
  std::cout << "=== MAIN ENTRY ===\n" << std::flush;
  std::cout << "Initializing GoogleTest...\n" << std::flush;
  ::testing::InitGoogleTest(&argc, argv);
  std::cout << "GoogleTest initialized\n" << std::flush;
  std::cout << "Running all tests...\n" << std::flush;
  int result = RUN_ALL_TESTS();
  std::cout << "Tests completed with result: " << result << "\n" << std::flush;
  return result;
}
