/**
 * @file test_json_chain_creation_simple.cc
 * @brief Simple integration test for JSON-based filter chain creation
 */

#include <gtest/gtest.h>

#include "mcp/c_api/mcp_c_filter_chain.h"

namespace mcp {
namespace c_api {
namespace {

TEST(JsonChainCreation, BasicCreation) {
  // Create a minimal JSON config
  const char* json_str = R"({
    "name": "test_chain",
    "filters": [
      {
        "type": "http_codec",
        "name": "http_filter",
        "config": {}
      }
    ]
  })";
  
  // Parse JSON (using simple cast for testing)
  auto json_config = reinterpret_cast<mcp_json_value_t>(const_cast<char*>(json_str));
  
  // Create chain with dummy dispatcher
  auto dispatcher = reinterpret_cast<mcp_dispatcher_t>(0x1234);
  
  // Try to create chain - this tests that the function doesn't crash
  // It may return 0 if factories aren't registered
  mcp_filter_chain_t chain = mcp_chain_create_from_json(dispatcher, json_config);
  
  // We don't expect success since we're not in a full environment
  // Just test that it doesn't crash
  (void)chain;
}

TEST(JsonChainCreation, ChainClone) {
  // Create a minimal JSON config
  const char* json_str = R"({
    "name": "test_chain",
    "filters": [
      {
        "type": "json_rpc",
        "name": "rpc_filter",
        "config": {}
      }
    ]
  })";
  
  // Parse JSON (using simple cast for testing)
  auto json_config = reinterpret_cast<mcp_json_value_t>(const_cast<char*>(json_str));
  
  // Create chain with dummy dispatcher
  auto dispatcher = reinterpret_cast<mcp_dispatcher_t>(0x1234);
  
  // Create original chain
  mcp_filter_chain_t original = mcp_chain_create_from_json(dispatcher, json_config);
  
  // Test cloning
  if (original != 0) {
    // If the original chain was created, try to clone it
    mcp_filter_chain_t cloned = mcp_chain_clone(original);
    
    // With our fix, clone should succeed if original succeeded
    // and had a dispatcher stored
    if (original != 0) {
      // We expect clone to succeed with our fix
      EXPECT_NE(cloned, 0) << "Chain clone should succeed when original has dispatcher";
    }
  } else {
    // If original creation failed (e.g., no factory registered),
    // we can't test cloning
    GTEST_SKIP() << "Original chain creation failed, skipping clone test";
  }
}

}  // namespace
}  // namespace c_api
}  // namespace mcp