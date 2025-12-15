/**
 * @file test_unified_chain_handles.cc
 * @brief Smoke test for unified filter chain handle system
 *
 * This test verifies that both simple chains (created via builder API) and
 * advanced chains (created via JSON API) share the same handle space and
 * can be operated on safely without cross-contamination.
 */

#include <gtest/gtest.h>

#include "mcp/c_api/mcp_c_api_json.h"
#include "mcp/c_api/mcp_c_filter_api.h"
#include "mcp/c_api/mcp_c_filter_chain.h"
#include "mcp/c_api/mcp_c_types.h"

// Forward declare the init/shutdown/dispatcher functions to avoid header
// conflicts
extern "C" {
MCP_API mcp_result_t mcp_init(const mcp_allocator_t* allocator) MCP_NOEXCEPT;
MCP_API void mcp_shutdown(void) MCP_NOEXCEPT;
MCP_API mcp_dispatcher_t mcp_dispatcher_create(void) MCP_NOEXCEPT;
MCP_API void mcp_dispatcher_destroy(mcp_dispatcher_t dispatcher) MCP_NOEXCEPT;
}

class UnifiedChainHandleTest : public ::testing::Test {
 protected:
  mcp_dispatcher_t dispatcher_;

  void SetUp() override {
    // Initialize library with default allocator
    ASSERT_EQ(MCP_OK, mcp_init(nullptr));

    // Create dispatcher
    dispatcher_ = mcp_dispatcher_create();
    ASSERT_NE(nullptr, dispatcher_);
  }

  void TearDown() override {
    // Cleanup
    if (dispatcher_) {
      mcp_dispatcher_destroy(dispatcher_);
    }
    mcp_shutdown();
  }
};

TEST_F(UnifiedChainHandleTest, SimpleChainViaBuilder) {
  // Create a simple chain using the builder API
  auto builder = mcp_filter_chain_builder_create(dispatcher_);
  ASSERT_NE(nullptr, builder);

  // Build the chain
  mcp_filter_chain_t chain_handle = mcp_filter_chain_build(builder);
  ASSERT_NE(0u, chain_handle);

  // Cleanup builder
  mcp_filter_chain_builder_destroy(builder);

  // Try operations that should work on simple chains
  mcp_filter_chain_retain(chain_handle);
  mcp_filter_chain_release(chain_handle);

  // Try to dump the chain (should work for both types)
  char* dump = mcp_chain_dump(chain_handle, "text");
  if (dump) {
    EXPECT_NE(nullptr, strstr(dump, "Chain"));  // Should contain "Chain"
    free(dump);
  }

  // Cleanup
  mcp_filter_chain_release(chain_handle);
}

TEST_F(UnifiedChainHandleTest, AdvancedChainViaJSON) {
  // Create a JSON configuration for an advanced chain
  auto json_config = mcp_json_create_object();
  ASSERT_NE(nullptr, json_config);

  // Add a simple filter configuration
  auto filters_array = mcp_json_create_array();
  auto filter_obj = mcp_json_create_object();
  auto type_str = mcp_json_create_string("http_codec");
  mcp_json_object_set(filter_obj, "type", type_str);
  mcp_json_array_append(filters_array, filter_obj);
  mcp_json_object_set(json_config, "filters", filters_array);

  // Create advanced chain from JSON
  mcp_filter_chain_t chain_handle =
      mcp_chain_create_from_json(dispatcher_, json_config);

  // Clean up JSON
  mcp_json_free(json_config);

  // If the chain creation failed (likely due to missing registry), that's ok
  // for this test We're mainly testing that the handle system doesn't crash
  if (chain_handle != 0) {
    // Try operations that should work on advanced chains
    EXPECT_EQ(MCP_OK, mcp_chain_pause(chain_handle));
    EXPECT_EQ(MCP_OK, mcp_chain_resume(chain_handle));

    // Get state (should work for advanced chains)
    mcp_chain_state_t state = mcp_chain_get_state(chain_handle);
    EXPECT_NE(MCP_CHAIN_STATE_ERROR, state);

    // Try to dump the chain
    char* dump = mcp_chain_dump(chain_handle, "json");
    if (dump) {
      EXPECT_NE(nullptr, strstr(dump, "chain"));  // Should contain "chain"
      free(dump);
    }

    // Cleanup
    mcp_filter_chain_release(chain_handle);
  }
}

TEST_F(UnifiedChainHandleTest, MixedChainHandles) {
  // Create both types of chains and verify they don't interfere

  // Create simple chain
  auto builder = mcp_filter_chain_builder_create(dispatcher_);
  ASSERT_NE(nullptr, builder);
  mcp_filter_chain_t simple_chain = mcp_filter_chain_build(builder);
  mcp_filter_chain_builder_destroy(builder);
  ASSERT_NE(0u, simple_chain);

  // Create JSON config for advanced chain
  auto json_config = mcp_json_create_object();
  auto filters_array = mcp_json_create_array();
  mcp_json_object_set(json_config, "filters", filters_array);

  // Try to create advanced chain (may fail if registry not set up)
  mcp_filter_chain_t advanced_chain =
      mcp_chain_create_from_json(dispatcher_, json_config);
  mcp_json_free(json_config);

  // Verify handles are different (if both were created)
  if (advanced_chain != 0) {
    EXPECT_NE(simple_chain, advanced_chain);

    // Operations on advanced chain should not affect simple chain
    mcp_chain_pause(advanced_chain);  // Should work on advanced

    // Dump both chains - they should produce different output
    char* simple_dump = mcp_chain_dump(simple_chain, "text");
    char* advanced_dump = mcp_chain_dump(advanced_chain, "text");

    if (simple_dump && advanced_dump) {
      // Basic check that they're different
      EXPECT_STRNE(simple_dump, advanced_dump);
    }

    if (simple_dump)
      free(simple_dump);
    if (advanced_dump)
      free(advanced_dump);

    // Cleanup advanced chain
    mcp_filter_chain_release(advanced_chain);
  }

  // Cleanup simple chain
  mcp_filter_chain_release(simple_chain);
}

TEST_F(UnifiedChainHandleTest, InvalidHandleOperations) {
  // Test that operations on invalid handles don't crash
  mcp_filter_chain_t invalid_handle = 999999;  // Unlikely to be valid

  // These should all handle invalid handles gracefully
  EXPECT_EQ(MCP_ERROR_NOT_FOUND, mcp_chain_pause(invalid_handle));
  EXPECT_EQ(MCP_ERROR_NOT_FOUND, mcp_chain_resume(invalid_handle));
  EXPECT_EQ(MCP_CHAIN_STATE_ERROR, mcp_chain_get_state(invalid_handle));

  char* dump = mcp_chain_dump(invalid_handle, "text");
  EXPECT_EQ(nullptr, dump);  // Should return nullptr for invalid handle

  // Release operations should be safe even on invalid handles
  mcp_filter_chain_release(invalid_handle);  // Should not crash
}

// Main function
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}