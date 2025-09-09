/**
 * @file test_core_factories.cc
 * @brief Unit tests for core filter factories
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/filter/filter_registry.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"

using namespace mcp;
using namespace mcp::filter;
using namespace testing;

namespace {

class CoreFactoriesTest : public Test {
 protected:
  void SetUp() override {
    // Registry should already have factories registered via static initialization
  }

  void TearDown() override {
    // Don't clear registry as it's a singleton used across tests
  }
};

// Test HttpCodecFilter factory registration and configuration
TEST_F(CoreFactoriesTest, HttpCodecFactoryRegistration) {
  // Check factory is registered
  EXPECT_TRUE(FilterRegistry::instance().hasFactory("http_codec"));
  
  auto factory = FilterRegistry::instance().getFactory("http_codec");
  ASSERT_NE(nullptr, factory);
  
  // Check metadata
  const auto& metadata = factory->getMetadata();
  EXPECT_EQ("http_codec", metadata.name);
  EXPECT_EQ("1.0.0", metadata.version);
  EXPECT_FALSE(metadata.description.empty());
  EXPECT_FALSE(metadata.dependencies.empty());
}

TEST_F(CoreFactoriesTest, HttpCodecDefaultConfig) {
  auto factory = FilterRegistry::instance().getFactory("http_codec");
  ASSERT_NE(nullptr, factory);
  
  auto defaults = factory->getDefaultConfig();
  EXPECT_TRUE(defaults.isObject());
  EXPECT_EQ("server", defaults["mode"].getString());
  EXPECT_EQ(8192, defaults["max_header_size"].getInt());
  EXPECT_EQ(1048576, defaults["max_body_size"].getInt());
  EXPECT_TRUE(defaults["keep_alive"].getBool());
  EXPECT_EQ(30000, defaults["timeout_ms"].getInt());
  EXPECT_FALSE(defaults["strict_mode"].getBool());
}

TEST_F(CoreFactoriesTest, HttpCodecValidation) {
  auto factory = FilterRegistry::instance().getFactory("http_codec");
  ASSERT_NE(nullptr, factory);
  
  // Valid config
  auto valid_config = json::JsonObjectBuilder()
      .add("mode", "client")
      .add("max_header_size", 16384)
      .add("keep_alive", false)
      .build();
  EXPECT_TRUE(factory->validateConfig(valid_config));
  
  // Invalid mode
  auto invalid_mode = json::JsonObjectBuilder()
      .add("mode", "invalid")
      .build();
  EXPECT_FALSE(factory->validateConfig(invalid_mode));
  
  // Out of range header size
  auto invalid_header_size = json::JsonObjectBuilder()
      .add("max_header_size", 100000)
      .build();
  EXPECT_FALSE(factory->validateConfig(invalid_header_size));
  
  // Wrong type for boolean
  auto invalid_type = json::JsonObjectBuilder()
      .add("keep_alive", "yes")
      .build();
  EXPECT_FALSE(factory->validateConfig(invalid_type));
}

// Test SseCodecFilter factory registration and configuration
TEST_F(CoreFactoriesTest, SseCodecFactoryRegistration) {
  // Check factory is registered
  EXPECT_TRUE(FilterRegistry::instance().hasFactory("sse_codec"));
  
  auto factory = FilterRegistry::instance().getFactory("sse_codec");
  ASSERT_NE(nullptr, factory);
  
  // Check metadata
  const auto& metadata = factory->getMetadata();
  EXPECT_EQ("sse_codec", metadata.name);
  EXPECT_EQ("1.0.0", metadata.version);
  EXPECT_FALSE(metadata.description.empty());
  EXPECT_FALSE(metadata.dependencies.empty());
  // Should depend on http_codec
  auto deps = metadata.dependencies;
  EXPECT_TRUE(std::find(deps.begin(), deps.end(), "http_codec") != deps.end());
}

TEST_F(CoreFactoriesTest, SseCodecDefaultConfig) {
  auto factory = FilterRegistry::instance().getFactory("sse_codec");
  ASSERT_NE(nullptr, factory);
  
  auto defaults = factory->getDefaultConfig();
  EXPECT_TRUE(defaults.isObject());
  EXPECT_EQ("server", defaults["mode"].getString());
  EXPECT_EQ(65536, defaults["max_event_size"].getInt());
  EXPECT_EQ(3000, defaults["retry_ms"].getInt());
  EXPECT_EQ(30000, defaults["keep_alive_ms"].getInt());
  EXPECT_FALSE(defaults["enable_compression"].getBool());
  EXPECT_EQ(100, defaults["event_buffer_limit"].getInt());
}

TEST_F(CoreFactoriesTest, SseCodecValidation) {
  auto factory = FilterRegistry::instance().getFactory("sse_codec");
  ASSERT_NE(nullptr, factory);
  
  // Valid config
  auto valid_config = json::JsonObjectBuilder()
      .add("mode", "client")
      .add("max_event_size", 32768)
      .add("retry_ms", 5000)
      .add("enable_compression", true)
      .build();
  EXPECT_TRUE(factory->validateConfig(valid_config));
  
  // Invalid mode
  auto invalid_mode = json::JsonObjectBuilder()
      .add("mode", "proxy")
      .build();
  EXPECT_FALSE(factory->validateConfig(invalid_mode));
  
  // Out of range event size
  auto invalid_event_size = json::JsonObjectBuilder()
      .add("max_event_size", 2097152)  // 2MB, exceeds max
      .build();
  EXPECT_FALSE(factory->validateConfig(invalid_event_size));
  
  // Out of range retry
  auto invalid_retry = json::JsonObjectBuilder()
      .add("retry_ms", 50)  // Too small
      .build();
  EXPECT_FALSE(factory->validateConfig(invalid_retry));
}

// Test JsonRpcProtocolFilter factory registration and configuration
TEST_F(CoreFactoriesTest, JsonRpcFactoryRegistration) {
  // Check factory is registered
  EXPECT_TRUE(FilterRegistry::instance().hasFactory("json_rpc"));
  
  auto factory = FilterRegistry::instance().getFactory("json_rpc");
  ASSERT_NE(nullptr, factory);
  
  // Check metadata
  const auto& metadata = factory->getMetadata();
  EXPECT_EQ("json_rpc", metadata.name);
  EXPECT_EQ("1.0.0", metadata.version);
  EXPECT_FALSE(metadata.description.empty());
  EXPECT_FALSE(metadata.dependencies.empty());
}

TEST_F(CoreFactoriesTest, JsonRpcDefaultConfig) {
  auto factory = FilterRegistry::instance().getFactory("json_rpc");
  ASSERT_NE(nullptr, factory);
  
  auto defaults = factory->getDefaultConfig();
  EXPECT_TRUE(defaults.isObject());
  EXPECT_EQ("server", defaults["mode"].getString());
  EXPECT_TRUE(defaults["use_framing"].getBool());
  EXPECT_EQ(1048576, defaults["max_message_size"].getInt());
  EXPECT_TRUE(defaults["batch_enabled"].getBool());
  EXPECT_EQ(100, defaults["batch_limit"].getInt());
  EXPECT_TRUE(defaults["strict_mode"].getBool());
  EXPECT_EQ(30000, defaults["timeout_ms"].getInt());
  EXPECT_TRUE(defaults["validate_params"].getBool());
}

TEST_F(CoreFactoriesTest, JsonRpcValidation) {
  auto factory = FilterRegistry::instance().getFactory("json_rpc");
  ASSERT_NE(nullptr, factory);
  
  // Valid config
  auto valid_config = json::JsonObjectBuilder()
      .add("mode", "client")
      .add("use_framing", false)
      .add("batch_limit", 50)
      .add("strict_mode", false)
      .build();
  EXPECT_TRUE(factory->validateConfig(valid_config));
  
  // Invalid mode
  auto invalid_mode = json::JsonObjectBuilder()
      .add("mode", "bidirectional")
      .build();
  EXPECT_FALSE(factory->validateConfig(invalid_mode));
  
  // Out of range message size
  auto invalid_msg_size = json::JsonObjectBuilder()
      .add("max_message_size", 20971520)  // 20MB, exceeds max
      .build();
  EXPECT_FALSE(factory->validateConfig(invalid_msg_size));
  
  // Out of range batch limit
  auto invalid_batch = json::JsonObjectBuilder()
      .add("batch_limit", 2000)  // Exceeds max
      .build();
  EXPECT_FALSE(factory->validateConfig(invalid_batch));
  
  // Wrong type for boolean
  auto invalid_type = json::JsonObjectBuilder()
      .add("use_framing", 1)  // Should be boolean, not int
      .build();
  EXPECT_FALSE(factory->validateConfig(invalid_type));
}

// Test that all factories validate configurations correctly through the registry
TEST_F(CoreFactoriesTest, ValidateConfigThroughRegistry) {
  // Note: We can't actually create filters since they need runtime dependencies,
  // but we can test that the factories validate configurations correctly
  
  // HTTP Codec - valid config should validate successfully
  {
    auto factory = FilterRegistry::instance().getFactory("http_codec");
    ASSERT_NE(nullptr, factory);
    
    auto config = json::JsonObjectBuilder()
        .add("mode", "server")
        .add("max_header_size", 16384)
        .build();
    
    EXPECT_TRUE(factory->validateConfig(config));
  }
  
  // SSE Codec - valid config should validate successfully
  {
    auto factory = FilterRegistry::instance().getFactory("sse_codec");
    ASSERT_NE(nullptr, factory);
    
    auto config = json::JsonObjectBuilder()
        .add("mode", "server")
        .add("max_event_size", 32768)
        .build();
    
    EXPECT_TRUE(factory->validateConfig(config));
  }
  
  // JSON-RPC - valid config should validate successfully
  {
    auto factory = FilterRegistry::instance().getFactory("json_rpc");
    ASSERT_NE(nullptr, factory);
    
    auto config = json::JsonObjectBuilder()
        .add("mode", "server")
        .add("use_framing", true)
        .build();
    
    EXPECT_TRUE(factory->validateConfig(config));
  }
}

// Test invalid configurations are rejected
TEST_F(CoreFactoriesTest, InvalidConfigRejection) {
  // HTTP Codec with invalid config
  {
    auto factory = FilterRegistry::instance().getFactory("http_codec");
    ASSERT_NE(nullptr, factory);
    
    auto config = json::JsonObjectBuilder()
        .add("mode", "invalid_mode")
        .build();
    
    EXPECT_FALSE(factory->validateConfig(config));
  }
  
  // SSE Codec with out-of-range value
  {
    auto factory = FilterRegistry::instance().getFactory("sse_codec");
    ASSERT_NE(nullptr, factory);
    
    auto config = json::JsonObjectBuilder()
        .add("retry_ms", 100000)  // Exceeds max
        .build();
    
    EXPECT_FALSE(factory->validateConfig(config));
  }
  
  // JSON-RPC with wrong type
  {
    auto factory = FilterRegistry::instance().getFactory("json_rpc");
    ASSERT_NE(nullptr, factory);
    
    auto config = json::JsonObjectBuilder()
        .add("batch_enabled", "yes")  // Should be boolean
        .build();
    
    EXPECT_FALSE(factory->validateConfig(config));
  }
}

// Test configuration schema is properly defined
TEST_F(CoreFactoriesTest, ConfigurationSchema) {
  // Check HTTP codec schema
  {
    auto factory = FilterRegistry::instance().getFactory("http_codec");
    ASSERT_NE(nullptr, factory);
    
    const auto& metadata = factory->getMetadata();
    const auto& schema = metadata.config_schema;
    
    EXPECT_TRUE(schema.isObject());
    EXPECT_EQ("object", schema["type"].getString());
    EXPECT_TRUE(schema.contains("properties"));
    EXPECT_TRUE(schema["properties"].isObject());
    EXPECT_TRUE(schema["properties"].contains("mode"));
    EXPECT_TRUE(schema["properties"].contains("max_header_size"));
  }
  
  // Check SSE codec schema
  {
    auto factory = FilterRegistry::instance().getFactory("sse_codec");
    ASSERT_NE(nullptr, factory);
    
    const auto& metadata = factory->getMetadata();
    const auto& schema = metadata.config_schema;
    
    EXPECT_TRUE(schema.isObject());
    EXPECT_EQ("object", schema["type"].getString());
    EXPECT_TRUE(schema.contains("properties"));
    EXPECT_TRUE(schema["properties"].contains("max_event_size"));
    EXPECT_TRUE(schema["properties"].contains("retry_ms"));
  }
  
  // Check JSON-RPC schema
  {
    auto factory = FilterRegistry::instance().getFactory("json_rpc");
    ASSERT_NE(nullptr, factory);
    
    const auto& metadata = factory->getMetadata();
    const auto& schema = metadata.config_schema;
    
    EXPECT_TRUE(schema.isObject());
    EXPECT_EQ("object", schema["type"].getString());
    EXPECT_TRUE(schema.contains("properties"));
    EXPECT_TRUE(schema["properties"].contains("use_framing"));
    EXPECT_TRUE(schema["properties"].contains("batch_limit"));
  }
}

}  // namespace