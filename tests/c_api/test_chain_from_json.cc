/**
 * @file test_chain_from_json.cc
 * @brief Comprehensive tests for C API JSON-based filter chain creation
 */

#include <gtest/gtest.h>

#include <string>
#include <memory>

#include "mcp/c_api/mcp_c_filter_chain.h"
#include "mcp/c_api/mcp_c_api_json.h"
#include "mcp/c_api/mcp_c_api.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/network/filter.h"
#include "mcp/json/json_bridge.h"

namespace mcp {
namespace c_api {
namespace {

// Mock filter factory for testing
class MockFilterFactory : public filter::FilterFactory {
 public:
  explicit MockFilterFactory(const std::string& name) : name_(name) {
    // Initialize metadata
    metadata_.name = name_;
    metadata_.version = "1.0.0";
    metadata_.dependencies = {};
    metadata_.config_schema = json::JsonValue::object();
    metadata_.description = "Mock filter for testing";
  }

  const filter::FilterFactoryMetadata& getMetadata() const override {
    return metadata_;
  }

  network::FilterSharedPtr createFilter(const json::JsonValue& config) const override {
    // Return nullptr to indicate filter needs runtime dependencies
    // This is acceptable for chain creation tests
    return nullptr;
  }
  
 private:
  std::string name_;
  filter::FilterFactoryMetadata metadata_;
};

class ChainFromJsonTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create dispatcher
    dispatcher_ = mcp_dispatcher_create();
    ASSERT_NE(dispatcher_, nullptr);
    
    // Register mock filter factories for testing
    registerMockFilters();
  }
  
  void TearDown() override {
    if (dispatcher_) {
      mcp_dispatcher_destroy(dispatcher_);
      dispatcher_ = nullptr;
    }
  }
  
  void registerMockFilters() {
    // Register common filter types used in tests
    auto& registry = filter::FilterRegistry::instance();
    
    if (!registry.hasFactory("json_rpc")) {
      registry.registerFactory("json_rpc", 
        std::make_shared<MockFilterFactory>("json_rpc"));
    }
    
    if (!registry.hasFactory("http_codec")) {
      registry.registerFactory("http_codec",
        std::make_shared<MockFilterFactory>("http_codec"));
    }
    
    if (!registry.hasFactory("sse_codec")) {
      registry.registerFactory("sse_codec",
        std::make_shared<MockFilterFactory>("sse_codec"));
    }
    
    if (!registry.hasFactory("rate_limit")) {
      registry.registerFactory("rate_limit",
        std::make_shared<MockFilterFactory>("rate_limit"));
    }
  }
  
  mcp_json_value_t parseJson(const std::string& json_str) {
    return mcp_json_parse(json_str.c_str());
  }
  
  mcp_dispatcher_t dispatcher_ = nullptr;
};

// Test 1: Happy path - minimal chain
TEST_F(ChainFromJsonTest, MinimalChain) {
  const char* json_str = R"({
    "filters": [
      { "type": "json_rpc", "config": { "strict_mode": true } }
    ]
  })";
  
  auto json_config = parseJson(json_str);
  ASSERT_NE(json_config, nullptr);
  
  auto chain = mcp_chain_create_from_json(dispatcher_, json_config);
  EXPECT_NE(chain, 0) << "Chain creation should succeed with minimal config";
  
  mcp_json_free(json_config);
}

// Test 2: Unknown filter type
TEST_F(ChainFromJsonTest, UnknownFilterType) {
  const char* json_str = R"({
    "filters": [
      { "type": "no_such_filter" }
    ]
  })";
  
  auto json_config = parseJson(json_str);
  ASSERT_NE(json_config, nullptr);
  
  auto chain = mcp_chain_create_from_json(dispatcher_, json_config);
  EXPECT_EQ(chain, 0) << "Chain creation should fail with unknown filter type";
  
  mcp_json_free(json_config);
}

// Test 3: Missing type field
TEST_F(ChainFromJsonTest, MissingTypeField) {
  const char* json_str = R"({
    "filters": [
      { "config": { "some_option": true } }
    ]
  })";
  
  auto json_config = parseJson(json_str);
  ASSERT_NE(json_config, nullptr);
  
  auto chain = mcp_chain_create_from_json(dispatcher_, json_config);
  EXPECT_EQ(chain, 0) << "Chain creation should fail without type field";
  
  mcp_json_free(json_config);
}

// Test 4: typed_config normalization
TEST_F(ChainFromJsonTest, TypedConfigNormalization) {
  const char* json_str = R"({
    "filters": [
      {
        "type": "json_rpc",
        "typed_config": {
          "@type": "type.googleapis.com/mcp.filters.jsonrpc.v1.JsonRpcConfig",
          "strict_mode": true,
          "max_batch": 10
        }
      }
    ]
  })";
  
  auto json_config = parseJson(json_str);
  ASSERT_NE(json_config, nullptr);
  
  auto chain = mcp_chain_create_from_json(dispatcher_, json_config);
  EXPECT_NE(chain, 0) << "Chain creation should succeed with typed_config";
  
  mcp_json_free(json_config);
}

// Test 5: Multiple filters in chain
TEST_F(ChainFromJsonTest, MultipleFilters) {
  const char* json_str = R"({
    "filters": [
      { "type": "rate_limit", "config": { "rps": 1000 } },
      { "type": "http_codec", "config": { "max_header_size": 8192 } },
      { "type": "sse_codec", "config": { "heartbeat": 30 } },
      { "type": "json_rpc", "config": { "strict_mode": false } }
    ]
  })";
  
  auto json_config = parseJson(json_str);
  ASSERT_NE(json_config, nullptr);
  
  auto chain = mcp_chain_create_from_json(dispatcher_, json_config);
  EXPECT_NE(chain, 0) << "Chain creation should succeed with multiple filters";
  
  mcp_json_free(json_config);
}

// Test 6: Using name as type fallback
TEST_F(ChainFromJsonTest, NameAsTypeFallback) {
  const char* json_str = R"({
    "filters": [
      { "name": "json_rpc", "config": { "strict_mode": true } }
    ]
  })";
  
  auto json_config = parseJson(json_str);
  ASSERT_NE(json_config, nullptr);
  
  auto chain = mcp_chain_create_from_json(dispatcher_, json_config);
  EXPECT_NE(chain, 0) << "Chain creation should succeed using name as type";
  
  mcp_json_free(json_config);
}

// Test 7: Invalid JSON structure - missing filters array
TEST_F(ChainFromJsonTest, MissingFiltersArray) {
  const char* json_str = R"({
    "name": "test_chain"
  })";
  
  auto json_config = parseJson(json_str);
  ASSERT_NE(json_config, nullptr);
  
  auto chain = mcp_chain_create_from_json(dispatcher_, json_config);
  EXPECT_EQ(chain, 0) << "Chain creation should fail without filters array";
  
  mcp_json_free(json_config);
}

// Test 8: Empty filters array
TEST_F(ChainFromJsonTest, EmptyFiltersArray) {
  const char* json_str = R"({
    "filters": []
  })";
  
  auto json_config = parseJson(json_str);
  ASSERT_NE(json_config, nullptr);
  
  auto chain = mcp_chain_create_from_json(dispatcher_, json_config);
  // Empty chain might be valid depending on implementation
  // For now, we'll expect it to succeed
  EXPECT_NE(chain, 0) << "Chain creation should succeed with empty filters";
  
  mcp_json_free(json_config);
}

// Test 9: Null dispatcher
TEST_F(ChainFromJsonTest, NullDispatcher) {
  const char* json_str = R"({
    "filters": [
      { "type": "json_rpc", "config": {} }
    ]
  })";
  
  auto json_config = parseJson(json_str);
  ASSERT_NE(json_config, nullptr);
  
  auto chain = mcp_chain_create_from_json(nullptr, json_config);
  EXPECT_EQ(chain, 0) << "Chain creation should fail with null dispatcher";
  
  mcp_json_free(json_config);
}

// Test 10: Null JSON config
TEST_F(ChainFromJsonTest, NullJsonConfig) {
  auto chain = mcp_chain_create_from_json(dispatcher_, nullptr);
  EXPECT_EQ(chain, 0) << "Chain creation should fail with null JSON config";
}

// Test 11: Complex configuration with optional fields
TEST_F(ChainFromJsonTest, ComplexConfiguration) {
  const char* json_str = R"({
    "name": "complex_chain",
    "options": {
      "ordering_strategy": "auto",
      "validate_dependencies": true
    },
    "filters": [
      {
        "type": "rate_limit",
        "name": "rate_limiter",
        "config": {
          "requests_per_second": 100,
          "burst": 50
        },
        "enabled": true,
        "priority": 10
      },
      {
        "type": "json_rpc",
        "name": "json_rpc_handler",
        "config": {
          "strict_mode": true,
          "use_framing": false
        }
      }
    ]
  })";
  
  auto json_config = parseJson(json_str);
  ASSERT_NE(json_config, nullptr);
  
  auto chain = mcp_chain_create_from_json(dispatcher_, json_config);
  EXPECT_NE(chain, 0) << "Chain creation should succeed with complex config";
  
  mcp_json_free(json_config);
}

// Test 12: Chain export to JSON
TEST_F(ChainFromJsonTest, ExportToJson) {
  const char* json_str = R"({
    "filters": [
      { "type": "json_rpc", "config": { "strict_mode": true } }
    ]
  })";
  
  auto json_config = parseJson(json_str);
  ASSERT_NE(json_config, nullptr);
  
  auto chain = mcp_chain_create_from_json(dispatcher_, json_config);
  ASSERT_NE(chain, 0);
  
  auto exported = mcp_chain_export_to_json(chain);
  EXPECT_NE(exported, nullptr) << "Chain export should produce valid JSON";
  
  // Verify the exported JSON is valid
  EXPECT_NE(mcp_json_get_type(exported), MCP_JSON_TYPE_NULL);
  
  mcp_json_free(json_config);
  mcp_json_free(exported);
}

// Test 13: Chain cloning
TEST_F(ChainFromJsonTest, ChainClone) {
  const char* json_str = R"({
    "filters": [
      { "type": "json_rpc", "config": { "strict_mode": true } }
    ]
  })";
  
  auto json_config = parseJson(json_str);
  ASSERT_NE(json_config, nullptr);
  
  auto original = mcp_chain_create_from_json(dispatcher_, json_config);
  ASSERT_NE(original, 0);
  
  // Clone should now work with dispatcher tracking
  auto cloned = mcp_chain_clone(original);
  EXPECT_NE(cloned, 0) << "Clone should succeed with dispatcher tracking";
  
  mcp_json_free(json_config);
}

// Test 14: Mixed typed_config and regular config
TEST_F(ChainFromJsonTest, MixedConfigStyles) {
  const char* json_str = R"({
    "filters": [
      {
        "type": "http_codec",
        "config": { "max_header_size": 8192 }
      },
      {
        "type": "json_rpc",
        "typed_config": {
          "@type": "type.googleapis.com/mcp.filters.jsonrpc.v1.JsonRpcConfig",
          "strict_mode": true
        }
      }
    ]
  })";
  
  auto json_config = parseJson(json_str);
  ASSERT_NE(json_config, nullptr);
  
  auto chain = mcp_chain_create_from_json(dispatcher_, json_config);
  EXPECT_NE(chain, 0) << "Chain creation should succeed with mixed config styles";
  
  mcp_json_free(json_config);
}

// Test 15: Invalid filter in array
TEST_F(ChainFromJsonTest, InvalidFilterInArray) {
  const char* json_str = R"({
    "filters": [
      { "type": "json_rpc", "config": {} },
      "invalid_filter_not_object",
      { "type": "http_codec", "config": {} }
    ]
  })";
  
  auto json_config = parseJson(json_str);
  ASSERT_NE(json_config, nullptr);
  
  auto chain = mcp_chain_create_from_json(dispatcher_, json_config);
  EXPECT_EQ(chain, 0) << "Chain creation should fail with non-object filter";
  
  mcp_json_free(json_config);
}

}  // namespace
}  // namespace c_api
}  // namespace mcp