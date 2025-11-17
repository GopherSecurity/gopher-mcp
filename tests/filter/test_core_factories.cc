/**
 * @file test_core_factories.cc
 * @brief Unit tests for core filter factories
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "mcp/filter/filter_registry.h"
#include "mcp/filter/core_filter_factories.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"

using namespace mcp;
using namespace mcp::filter;
using namespace testing;

namespace {

enum class ScenarioSelection {
  kNativeOnly,
  kHybridOnly,
  kBoth
};

ScenarioSelection DetectScenarioSelection() {
  const char* env = std::getenv("MCP_TEST_SCENARIO");
  if (!env || *env == '\0') {
    return ScenarioSelection::kBoth;
  }
  std::string value(env);
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (value == "1" || value == "scenario1" || value == "native") {
    return ScenarioSelection::kNativeOnly;
  }
  if (value == "2" || value == "hybrid") {
    return ScenarioSelection::kHybridOnly;
  }
  if (value == "both" || value == "all") {
    return ScenarioSelection::kBoth;
  }
  return ScenarioSelection::kBoth;
}

ScenarioSelection ActiveScenarioSelection() {
  static ScenarioSelection selection = DetectScenarioSelection();
  return selection;
}

bool NativeStackEnabled() {
  return ActiveScenarioSelection() != ScenarioSelection::kHybridOnly;
}

bool HybridEnabled() {
  return ActiveScenarioSelection() != ScenarioSelection::kNativeOnly;
}

std::string ScenarioEnvValue() {
  const char* env = std::getenv("MCP_TEST_SCENARIO");
  return env && *env ? std::string(env) : std::string("<unset>");
}

std::string NormalizeRoot(std::string root) {
  if (root.empty()) {
    return root;
  }
  char last = root.back();
  if (last == '/' || last == '\\') {
    return root;
  }
#ifdef _WIN32
  return root + "\\";
#else
  return root + "/";
#endif
}

std::string SourceRoot() {
  const char* override_root = std::getenv("MCP_SOURCE_ROOT");
  if (override_root && *override_root) {
    return NormalizeRoot(std::string(override_root));
  }

  std::string file_path = __FILE__;
  const std::string suffix = "tests/filter/test_core_factories.cc";
  auto pos = file_path.rfind(suffix);
  if (pos == std::string::npos) {
    return std::string("../");  // best-effort fallback
  }
  return NormalizeRoot(file_path.substr(0, pos));
}

std::string HybridConfigPath() {
  const char* override_path = std::getenv("MCP_HYBRID_CONFIG");
  if (override_path && *override_path) {
    return std::string(override_path);
  }
  return SourceRoot() +
         "examples/typescript/calculator-hybrid/config-hybrid.json";
}

json::JsonValue LoadHybridConfig() {
  std::ifstream file(HybridConfigPath());
  std::stringstream buffer;
  buffer << file.rdbuf();
  if (!file.good()) {
    throw std::runtime_error("Unable to read hybrid config at " +
                             HybridConfigPath());
  }
  return json::JsonValue::parse(buffer.str());
}

class NativeCoreFactoriesTest : public Test {
 protected:
  void SetUp() override {
    if (!NativeStackEnabled()) {
      GTEST_SKIP() << "Native filter tests skipped (MCP_TEST_SCENARIO="
                   << ScenarioEnvValue() << ")";
    }
    FilterRegistry::instance().clearFactories();
    registerHttpCodecFilterFactory();
    registerSseCodecFilterFactory();
    registerJsonRpcDispatcherFilterFactory();
    registerRateLimitFilterFactory();
    registerCircuitBreakerFilterFactory();
    registerMetricsFilterFactory();
  }
};

class HybridFilterSuiteTest : public Test {
 protected:
  void SetUp() override {
    if (!HybridEnabled()) {
      GTEST_SKIP() << "Hybrid filter tests skipped (MCP_TEST_SCENARIO="
                   << ScenarioEnvValue() << ")";
    }
  }
};

// Test HttpCodecFilter factory registration and configuration
TEST_F(NativeCoreFactoriesTest, HttpCodecFactoryRegistration) {
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

TEST_F(NativeCoreFactoriesTest, HttpCodecDefaultConfig) {
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

TEST_F(NativeCoreFactoriesTest, HttpCodecValidation) {
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
TEST_F(NativeCoreFactoriesTest, SseCodecFactoryRegistration) {
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

TEST_F(NativeCoreFactoriesTest, SseCodecDefaultConfig) {
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

TEST_F(NativeCoreFactoriesTest, SseCodecValidation) {
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
TEST_F(NativeCoreFactoriesTest, JsonRpcFactoryRegistration) {
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

TEST_F(NativeCoreFactoriesTest, JsonRpcDefaultConfig) {
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

TEST_F(NativeCoreFactoriesTest, JsonRpcValidation) {
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
TEST_F(NativeCoreFactoriesTest, ValidateConfigThroughRegistry) {
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
TEST_F(NativeCoreFactoriesTest, InvalidConfigRejection) {
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
TEST_F(NativeCoreFactoriesTest, ConfigurationSchema) {
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

// Hybrid focused tests -------------------------------------------------------

TEST_F(HybridFilterSuiteTest, HybridConfigFiltersAreRegistered) {
  auto config = LoadHybridConfig();
  ASSERT_TRUE(config.contains("listeners"));
  const auto& listeners = config["listeners"];
  ASSERT_TRUE(listeners.isArray());
  ASSERT_GT(listeners.size(), 0);

  ASSERT_TRUE(listeners[0].contains("filter_chains"));
  const auto& chains = listeners[0]["filter_chains"];
  ASSERT_TRUE(chains.isArray());
  ASSERT_GT(chains.size(), 0);

  ASSERT_TRUE(chains[0].contains("filters"));
  const auto& filters = chains[0]["filters"];
  ASSERT_TRUE(filters.isArray());
  ASSERT_GT(filters.size(), 0);

  auto& registry = FilterRegistry::instance();

  for (size_t i = 0; i < filters.size(); ++i) {
    const auto& filter = filters[i];
    ASSERT_TRUE(filter.contains("type"));
    const std::string type = filter["type"].getString();

    bool has_context = registry.hasContextFactory(type);
    bool has_factory = registry.hasFactory(type);
    EXPECT_TRUE(has_context || has_factory)
        << "Filter type '" << type << "' missing registry entry";

    if (has_factory) {
      auto factory = registry.getFactory(type);
      ASSERT_NE(nullptr, factory);
      ASSERT_TRUE(filter.contains("config"));
      EXPECT_TRUE(factory->validateConfig(filter["config"]))
          << "Hybrid config rejected by factory '" << type << "'";
    }
  }
}

TEST_F(HybridFilterSuiteTest, RateLimitDefaultsFollowTokenBucketExample) {
  const auto* metadata =
      FilterRegistry::instance().getBasicMetadata("rate_limit");
  ASSERT_NE(nullptr, metadata);
  auto defaults = metadata->default_config;
  ASSERT_TRUE(defaults.isObject());
  EXPECT_EQ(std::string("token_bucket"),
            defaults["strategy"].getString("token_bucket"));
  EXPECT_EQ(100, defaults["bucket_capacity"].getInt());
  EXPECT_EQ(10, defaults["refill_rate"].getInt());
  EXPECT_TRUE(defaults["allow_burst"].getBool(true));
  EXPECT_EQ(20, defaults["burst_size"].getInt());
}

TEST_F(HybridFilterSuiteTest, CircuitBreakerDefaultsMatchHybridGuidance) {
  const auto* metadata =
      FilterRegistry::instance().getBasicMetadata("circuit_breaker");
  ASSERT_NE(nullptr, metadata);
  auto defaults = metadata->default_config;
  ASSERT_TRUE(defaults.isObject());
  EXPECT_EQ(5, defaults["failure_threshold"].getInt());
  EXPECT_NEAR(0.5, defaults["error_rate_threshold"].getFloat(0.5), 1e-6);
  EXPECT_EQ(10, defaults["min_requests"].getInt());
  EXPECT_EQ(30000, defaults["timeout_ms"].getInt());
  EXPECT_EQ(60000, defaults["window_size_ms"].getInt());
  EXPECT_EQ(3, defaults["half_open_max_requests"].getInt());
  EXPECT_EQ(2, defaults["half_open_success_threshold"].getInt());
}

TEST_F(HybridFilterSuiteTest, MetricsDefaultsExposeInternalTelemetry) {
  auto factory = FilterRegistry::instance().getFactory("metrics");
  ASSERT_NE(nullptr, factory);
  auto defaults = factory->getDefaultConfig();
  ASSERT_TRUE(defaults.isObject());
  EXPECT_EQ(std::string("internal"),
            defaults["provider"].getString("internal"));
  EXPECT_EQ(1, defaults["rate_update_interval_seconds"].getInt());
  EXPECT_EQ(10, defaults["report_interval_seconds"].getInt());
  EXPECT_EQ(5000, defaults["max_latency_threshold_ms"].getInt());
  EXPECT_EQ(10, defaults["error_rate_threshold"].getInt());
  EXPECT_TRUE(defaults["track_methods"].getBool());
  EXPECT_FALSE(defaults["enable_histograms"].getBool());
}

TEST_F(HybridFilterSuiteTest, RequestLoggerDefaultsMatchDocumentation) {
  const auto* metadata =
      FilterRegistry::instance().getBasicMetadata("request_logger");
  ASSERT_NE(nullptr, metadata);
  auto defaults = metadata->default_config;
  ASSERT_TRUE(defaults.isObject());
  EXPECT_EQ(std::string("debug"),
            defaults["log_level"].getString("debug"));
  EXPECT_EQ(std::string("pretty"),
            defaults["log_format"].getString("pretty"));
  EXPECT_TRUE(defaults["include_timestamps"].getBool());
  EXPECT_TRUE(defaults["include_payload"].getBool());
  EXPECT_EQ(1000, defaults["max_payload_length"].getInt());
  EXPECT_EQ(std::string("stdout"),
            defaults["output"].getString("stdout"));
}

}  // namespace
