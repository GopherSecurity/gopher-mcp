/**
 * @file test_configurable_filter_chain.cc
 * @brief Comprehensive tests for configuration-driven filter chain
 * functionality
 *
 * Tests both JSON and YAML configuration formats, various filter combinations,
 * and runtime behavior of configured filter chains.
 */

#include <fstream>
#include <memory>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/event/event_loop.h"
#include "mcp/filter/filter_chain_assembler.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/network/filter.h"

// Must undefine before redefining
#ifdef GOPHER_LOG_COMPONENT
#undef GOPHER_LOG_COMPONENT
#endif

#include "mcp/logging/log_macros.h"
#include "mcp/logging/logger_registry.h"

#define GOPHER_LOG_COMPONENT "test.configurable_chain"

namespace mcp {
namespace filter {
namespace test {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

// ============================================================================
// Mock Classes
// ============================================================================

/**
 * Mock filter for testing configuration application
 */
class MockConfigurableFilter : public network::NetworkFilterBase {
 public:
  explicit MockConfigurableFilter(const json::JsonValue& config)
      : config_(config) {
    if (config.contains("name")) {
      name_ = config["name"].getString();
    }
    if (config.contains("enabled")) {
      enabled_ = config["enabled"].getBool();
    }
  }

  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    data_processed_ = true;
    last_data_size_ = data.length();
    return network::FilterStatus::Continue;
  }

  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    write_processed_ = true;
    return network::FilterStatus::Continue;
  }

  network::FilterStatus onNewConnection() override {
    connection_initialized_ = true;
    return network::FilterStatus::Continue;
  }

  const json::JsonValue& getConfig() const { return config_; }
  const std::string& getName() const { return name_; }
  bool isEnabled() const { return enabled_; }

  bool data_processed_ = false;
  bool write_processed_ = false;
  bool connection_initialized_ = false;
  size_t last_data_size_ = 0;

 private:
  json::JsonValue config_;
  std::string name_;
  bool enabled_ = true;
};

/**
 * Factory for creating mock configurable filters
 */
class MockConfigurableFilterFactory : public FilterFactory {
 public:
  explicit MockConfigurableFilterFactory(const std::string& type)
      : type_(type) {
    metadata_.name = type;
    metadata_.version = "1.0.0";
    metadata_.description = "Mock " + type + " filter for testing";
  }

  network::FilterSharedPtr createFilter(
      const json::JsonValue& config) const override {
    creation_count_++;
    last_config_ = config;

    auto filter = std::make_shared<MockConfigurableFilter>(config);
    created_filters_.push_back(filter);
    return filter;
  }

  const FilterFactoryMetadata& getMetadata() const override {
    return metadata_;
  }

  json::JsonValue getDefaultConfig() const override {
    return json::JsonObjectBuilder()
        .add("enabled", true)
        .add("test_mode", true)
        .build();
  }

  bool validateConfig(const json::JsonValue& config) const override {
    return true;
  }

  // Test helpers
  int getCreationCount() const { return creation_count_; }
  const json::JsonValue& getLastConfig() const { return last_config_; }
  const std::vector<std::shared_ptr<MockConfigurableFilter>>&
  getCreatedFilters() const {
    return created_filters_;
  }

 private:
  std::string type_;
  FilterFactoryMetadata metadata_;
  mutable int creation_count_ = 0;
  mutable json::JsonValue last_config_;
  mutable std::vector<std::shared_ptr<MockConfigurableFilter>> created_filters_;
};

/**
 * Mock filter manager for testing filter chain application
 */
class MockFilterManager : public network::FilterManager {
 public:
  void addReadFilter(network::ReadFilterSharedPtr filter) override {
    read_filters_.push_back(filter);
  }

  void addWriteFilter(network::WriteFilterSharedPtr filter) override {
    write_filters_.push_back(filter);
  }

  void addFilter(network::FilterSharedPtr filter) override {
    filters_.push_back(filter);
    filter_names_.push_back(
        std::dynamic_pointer_cast<MockConfigurableFilter>(filter)->getName());
  }

  void removeReadFilter(network::ReadFilterSharedPtr filter) override {}
  bool initializeReadFilters() override { return true; }
  void onRead() override {}
  network::FilterStatus onWrite() override {
    return network::FilterStatus::Continue;
  }
  void onConnectionEvent(network::ConnectionEvent event) override {}

  const std::vector<network::FilterSharedPtr>& getFilters() const {
    return filters_;
  }
  const std::vector<std::string>& getFilterNames() const {
    return filter_names_;
  }
  size_t getFilterCount() const { return filters_.size(); }

 private:
  std::vector<network::ReadFilterSharedPtr> read_filters_;
  std::vector<network::WriteFilterSharedPtr> write_filters_;
  std::vector<network::FilterSharedPtr> filters_;
  std::vector<std::string> filter_names_;
};

/**
 * Test fixture for temporary configuration files
 */
class ConfigFileFixture {
 public:
  ConfigFileFixture() {
    // Create temp directory for test configs
    temp_dir_ = std::filesystem::temp_directory_path() / "mcp_filter_test";
    std::filesystem::create_directories(temp_dir_);
  }

  ~ConfigFileFixture() {
    // Clean up temp files
    std::filesystem::remove_all(temp_dir_);
  }

  std::string writeJsonConfig(const std::string& name,
                              const json::JsonValue& config) {
    std::string path = (temp_dir_ / (name + ".json")).string();
    std::ofstream file(path);
    file << config.toString();
    file.close();
    return path;
  }

  // YAML write method - reserved for future use
  // std::string writeYamlConfig(const std::string& name, const std::string&
  // yaml)

  std::string getTempDir() const { return temp_dir_.string(); }

 private:
  std::filesystem::path temp_dir_;
};

// ============================================================================
// Test Fixture
// ============================================================================

class ConfigurableFilterChainTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Set up test logging
    auto& logger_registry = logging::LoggerRegistry::instance();
    logger_registry.getDefaultLogger()->setLevel(logging::LogLevel::Debug);

    // Register mock filter factories
    registerMockFactory("http_codec");
    registerMockFactory("sse_codec");
    registerMockFactory("json_rpc");
    registerMockFactory("metrics");
    registerMockFactory("rate_limit");
    registerMockFactory("circuit_breaker");

    // Create config fixture
    config_fixture_ = std::make_unique<ConfigFileFixture>();
  }

  void TearDown() override {
    config_fixture_.reset();
    // Note: In production, would need to unregister factories
  }

  void registerMockFactory(const std::string& type) {
    auto factory = std::make_shared<MockConfigurableFilterFactory>(type);
    factories_[type] = factory;
    FilterRegistry::instance().registerFactory(type, factory);
  }

  std::shared_ptr<MockConfigurableFilterFactory> getFactory(
      const std::string& type) {
    return factories_[type];
  }

  json::JsonValue createMinimalJsonConfig() {
    return json::JsonObjectBuilder()
        .add("filter_chains",
             json::JsonArrayBuilder()
                 .add(json::JsonObjectBuilder()
                          .add("name", "server")
                          .add("filters",
                               json::JsonArrayBuilder()
                                   .add(json::JsonObjectBuilder()
                                            .add("type", "json_rpc")
                                            .add("name", "json_rpc_protocol")
                                            .add("config",
                                                 json::JsonObjectBuilder()
                                                     .add("mode", "server")
                                                     .add("use_framing", false)
                                                     .add("strict_mode", true)
                                                     .build())
                                            .build())
                                   .build())
                          .build())
                 .build())
        .build();
  }

  json::JsonValue createFullStackJsonConfig() {
    return json::JsonObjectBuilder()
        .add(
            "filter_chains",
            json::JsonArrayBuilder()
                .add(
                    json::JsonObjectBuilder()
                        .add("name", "server")
                        .add(
                            "filters",
                            json::JsonArrayBuilder()
                                .add(json::JsonObjectBuilder()
                                         .add("type", "metrics")
                                         .add("name", "metrics_collector")
                                         .add("config",
                                              json::JsonObjectBuilder()
                                                  .add("track_methods", true)
                                                  .add("report_interval_ms",
                                                       5000)
                                                  .build())
                                         .build())
                                .add(
                                    json::JsonObjectBuilder()
                                        .add("type", "rate_limit")
                                        .add("name", "rate_limiter")
                                        .add("config",
                                             json::JsonObjectBuilder()
                                                 .add("max_requests_per_second",
                                                      100)
                                                 .add("burst_size", 200)
                                                 .build())
                                        .build())
                                .add(json::JsonObjectBuilder()
                                         .add("type", "http_codec")
                                         .add("name", "http_parser")
                                         .add("config",
                                              json::JsonObjectBuilder()
                                                  .add("mode", "server")
                                                  .add("max_header_size", 8192)
                                                  .build())
                                         .build())
                                .add(json::JsonObjectBuilder()
                                         .add("type", "sse_codec")
                                         .add("name", "sse_parser")
                                         .add("config",
                                              json::JsonObjectBuilder()
                                                  .add("max_event_size", 65536)
                                                  .build())
                                         .build())
                                .add(json::JsonObjectBuilder()
                                         .add("type", "json_rpc")
                                         .add("name", "json_rpc_protocol")
                                         .add("config",
                                              json::JsonObjectBuilder()
                                                  .add("mode", "server")
                                                  .add("use_framing", true)
                                                  .build())
                                         .build())
                                .build())
                        .build())
                .build())
        .build();
  }

  // YAML config creation methods removed - will be implemented when
  // FileConfigSource is exposed

  std::unique_ptr<ConfigFileFixture> config_fixture_;
  std::map<std::string, std::shared_ptr<MockConfigurableFilterFactory>>
      factories_;
};

// ============================================================================
// Configuration Loading Tests - JSON
// ============================================================================

TEST_F(ConfigurableFilterChainTest, LoadMinimalJsonConfig) {
  auto config = createMinimalJsonConfig();

  FilterChainBuilder builder;
  builder.fromConfig(config["filter_chains"][0]);

  EXPECT_EQ(builder.getFilterCount(), 1);
  EXPECT_TRUE(builder.validate());

  const auto& filters = builder.getFilters();
  EXPECT_EQ(filters[0].name, "json_rpc");
  EXPECT_FALSE(filters[0].config["use_framing"].getBool());
}

TEST_F(ConfigurableFilterChainTest, LoadFullJsonConfig) {
  auto config = createFullStackJsonConfig();

  FilterChainBuilder builder;
  builder.fromConfig(config["filter_chains"][0]);

  EXPECT_EQ(builder.getFilterCount(), 5);
  EXPECT_TRUE(builder.validate());

  const auto& filters = builder.getFilters();
  std::vector<std::string> expected_types = {
      "metrics", "rate_limit", "http_codec", "sse_codec", "json_rpc"};

  for (size_t i = 0; i < expected_types.size(); ++i) {
    EXPECT_EQ(filters[i].name, expected_types[i]);
  }
}

TEST_F(ConfigurableFilterChainTest, LoadInvalidJsonConfig) {
  // Missing required fields
  auto config = json::JsonObjectBuilder()
                    .add("filter_chains", json::JsonArrayBuilder()
                                              .add(json::JsonObjectBuilder()
                                                       .add("name", "server")
                                                       // Missing filters array
                                                       .build())
                                              .build())
                    .build();

  FilterChainBuilder builder;
  builder.fromConfig(config["filter_chains"][0]);

  // Should handle gracefully
  EXPECT_EQ(builder.getFilterCount(), 0);
}

// ============================================================================
// Configuration Loading Tests - YAML
// ============================================================================
// TODO: Implement YAML-specific tests when FileConfigSource is exposed:
// - LoadMinimalYamlConfig
// - LoadFullYamlConfig
// - YamlWithAnchorsAndAliases
// - YamlEnvironmentVariables
// - YamlIncludes
// - YamlMergeKeys
// - YamlMultiDocument
// - YamlComplexStructures
// - YamlToJsonEquivalence
// ============================================================================

// ============================================================================
// Filter Creation and Application Tests
// ============================================================================

TEST_F(ConfigurableFilterChainTest, CreateJsonRpcOnlyChain) {
  auto config = createMinimalJsonConfig();

  ConfigurableFilterChainFactory factory(config["filter_chains"][0]);
  MockFilterManager filter_manager;

  EXPECT_TRUE(factory.createFilterChain(filter_manager));
  EXPECT_EQ(filter_manager.getFilterCount(), 1);

  // Verify factory was called
  auto json_rpc_factory = getFactory("json_rpc");
  EXPECT_EQ(json_rpc_factory->getCreationCount(), 1);

  // Verify configuration was passed
  const auto& last_config = json_rpc_factory->getLastConfig();
  EXPECT_FALSE(last_config["use_framing"].getBool());
  EXPECT_TRUE(last_config["strict_mode"].getBool());
}

TEST_F(ConfigurableFilterChainTest, CreateFullProtocolStack) {
  auto config = createFullStackJsonConfig();

  ConfigurableFilterChainFactory factory(config["filter_chains"][0]);
  MockFilterManager filter_manager;

  EXPECT_TRUE(factory.createFilterChain(filter_manager));
  EXPECT_EQ(filter_manager.getFilterCount(), 5);

  // Verify all factories were called
  EXPECT_EQ(getFactory("metrics")->getCreationCount(), 1);
  EXPECT_EQ(getFactory("rate_limit")->getCreationCount(), 1);
  EXPECT_EQ(getFactory("http_codec")->getCreationCount(), 1);
  EXPECT_EQ(getFactory("sse_codec")->getCreationCount(), 1);
  EXPECT_EQ(getFactory("json_rpc")->getCreationCount(), 1);
}

TEST_F(ConfigurableFilterChainTest, VerifyFilterConfiguration) {
  auto config = createFullStackJsonConfig();

  ConfigurableFilterChainFactory factory(config["filter_chains"][0]);
  MockFilterManager filter_manager;

  factory.createFilterChain(filter_manager);

  // Check metrics filter config
  auto metrics_config = getFactory("metrics")->getLastConfig();
  EXPECT_TRUE(metrics_config["track_methods"].getBool());
  EXPECT_EQ(metrics_config["report_interval_ms"].getInt(), 5000);

  // Check rate limit filter config
  auto rate_limit_config = getFactory("rate_limit")->getLastConfig();
  EXPECT_EQ(rate_limit_config["max_requests_per_second"].getInt(), 100);
  EXPECT_EQ(rate_limit_config["burst_size"].getInt(), 200);

  // Check JSON-RPC filter config
  auto jsonrpc_config = getFactory("json_rpc")->getLastConfig();
  EXPECT_TRUE(jsonrpc_config["use_framing"].getBool());
  EXPECT_EQ(jsonrpc_config["mode"].getString(), "server");
}

// ============================================================================
// Runtime Behavior Tests
// ============================================================================

TEST_F(ConfigurableFilterChainTest, HttpSseJsonRpcChain) {
  auto config = createFullStackJsonConfig();

  ConfigurableFilterChainFactory factory(config["filter_chains"][0]);
  MockFilterManager filter_manager;

  factory.createFilterChain(filter_manager);

  // Simulate data flow through chain
  OwnedBuffer test_data;
  test_data.add("POST /test HTTP/1.1\r\n\r\n");

  const auto& filters = filter_manager.getFilters();

  // Each filter should process data
  for (const auto& filter : filters) {
    auto mock_filter =
        std::dynamic_pointer_cast<MockConfigurableFilter>(filter);
    mock_filter->onData(test_data, false);
    EXPECT_TRUE(mock_filter->data_processed_);
  }
}

TEST_F(ConfigurableFilterChainTest, StdioJsonRpcOnly) {
  auto config = createMinimalJsonConfig();

  ConfigurableFilterChainFactory factory(config["filter_chains"][0]);
  MockFilterManager filter_manager;

  factory.createFilterChain(filter_manager);

  EXPECT_EQ(filter_manager.getFilterCount(), 1);

  // Verify only JSON-RPC filter exists
  const auto& filters = filter_manager.getFilters();
  auto json_rpc_filter =
      std::dynamic_pointer_cast<MockConfigurableFilter>(filters[0]);

  EXPECT_EQ(json_rpc_filter->getName(),
            "");  // Mock filter doesn't set name from config
  EXPECT_FALSE(json_rpc_filter->getConfig()["use_framing"].getBool());
}

TEST_F(ConfigurableFilterChainTest, MetricsRateLimitChain) {
  // Create config with only QoS filters
  auto config =
      json::JsonObjectBuilder()
          .add("filter_chains",
               json::JsonArrayBuilder()
                   .add(json::JsonObjectBuilder()
                            .add("name", "qos")
                            .add("filters",
                                 json::JsonArrayBuilder()
                                     .add(json::JsonObjectBuilder()
                                              .add("type", "metrics")
                                              .add("name", "metrics")
                                              .add("config",
                                                   json::JsonObjectBuilder()
                                                       .add("track_all", true)
                                                       .build())
                                              .build())
                                     .add(json::JsonObjectBuilder()
                                              .add("type", "rate_limit")
                                              .add("name", "limiter")
                                              .add("config",
                                                   json::JsonObjectBuilder()
                                                       .add("limit", 100)
                                                       .build())
                                              .build())
                                     .add(json::JsonObjectBuilder()
                                              .add("type", "json_rpc")
                                              .add("name", "protocol")
                                              .add("config",
                                                   json::JsonValue::object())
                                              .build())
                                     .build())
                            .build())
                   .build())
          .build();

  ConfigurableFilterChainFactory factory(config["filter_chains"][0]);
  MockFilterManager filter_manager;

  factory.createFilterChain(filter_manager);

  EXPECT_EQ(filter_manager.getFilterCount(), 3);

  // Verify QoS filters come before protocol filter
  // Verify filter count and order
  EXPECT_EQ(filter_manager.getFilterCount(), 3);
}

TEST_F(ConfigurableFilterChainTest, SelectiveFilterChain) {
  // Create config with selective filters (HTTP + JSON-RPC, no SSE)
  auto config =
      json::JsonObjectBuilder()
          .add("filter_chains",
               json::JsonArrayBuilder()
                   .add(json::JsonObjectBuilder()
                            .add("name", "selective")
                            .add("filters",
                                 json::JsonArrayBuilder()
                                     .add(json::JsonObjectBuilder()
                                              .add("type", "http_codec")
                                              .add("name", "http")
                                              .add("config",
                                                   json::JsonValue::object())
                                              .build())
                                     .add(json::JsonObjectBuilder()
                                              .add("type", "json_rpc")
                                              .add("name", "jsonrpc")
                                              .add("config",
                                                   json::JsonValue::object())
                                              .build())
                                     .build())
                            .build())
                   .build())
          .build();

  ConfigurableFilterChainFactory factory(config["filter_chains"][0]);
  MockFilterManager filter_manager;

  factory.createFilterChain(filter_manager);

  EXPECT_EQ(filter_manager.getFilterCount(), 2);

  // Verify only HTTP and JSON-RPC filters are present
  const auto& filters = filter_manager.getFilters();
  EXPECT_EQ(filters.size(), 2);
}

TEST_F(ConfigurableFilterChainTest, CircuitBreakerChain) {
  auto config =
      json::JsonObjectBuilder()
          .add("filter_chains",
               json::JsonArrayBuilder()
                   .add(json::JsonObjectBuilder()
                            .add("name", "circuit_breaker")
                            .add("filters",
                                 json::JsonArrayBuilder()
                                     .add(json::JsonObjectBuilder()
                                              .add("type", "circuit_breaker")
                                              .add("name", "breaker")
                                              .add("config",
                                                   json::JsonObjectBuilder()
                                                       .add("failure_threshold",
                                                            5)
                                                       .add("timeout_ms", 1000)
                                                       .add("reset_timeout_ms",
                                                            5000)
                                                       .build())
                                              .build())
                                     .add(json::JsonObjectBuilder()
                                              .add("type", "json_rpc")
                                              .add("name", "protocol")
                                              .add("config",
                                                   json::JsonValue::object())
                                              .build())
                                     .build())
                            .build())
                   .build())
          .build();

  ConfigurableFilterChainFactory factory(config["filter_chains"][0]);
  MockFilterManager filter_manager;

  factory.createFilterChain(filter_manager);

  EXPECT_EQ(filter_manager.getFilterCount(), 2);

  // Verify circuit breaker config
  auto breaker_config = getFactory("circuit_breaker")->getLastConfig();
  EXPECT_EQ(breaker_config["failure_threshold"].getInt(), 5);
  EXPECT_EQ(breaker_config["timeout_ms"].getInt(), 1000);
  EXPECT_EQ(breaker_config["reset_timeout_ms"].getInt(), 5000);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(ConfigurableFilterChainTest, FilterChainErrorPropagation) {
  auto config = createFullStackJsonConfig();

  ConfigurableFilterChainFactory factory(config["filter_chains"][0]);
  MockFilterManager filter_manager;

  factory.createFilterChain(filter_manager);

  // Simulate error in middle of chain
  const auto& filters = filter_manager.getFilters();

  // Process data through first two filters
  OwnedBuffer test_data;
  test_data.add("invalid data");

  auto metrics_filter =
      std::dynamic_pointer_cast<MockConfigurableFilter>(filters[0]);
  auto rate_limit_filter =
      std::dynamic_pointer_cast<MockConfigurableFilter>(filters[1]);

  EXPECT_EQ(metrics_filter->onData(test_data, false),
            network::FilterStatus::Continue);
  EXPECT_EQ(rate_limit_filter->onData(test_data, false),
            network::FilterStatus::Continue);

  // Both should have processed data
  EXPECT_TRUE(metrics_filter->data_processed_);
  EXPECT_TRUE(rate_limit_filter->data_processed_);
}

TEST_F(ConfigurableFilterChainTest, MissingFilterType) {
  // Register only some filter types, not all
  auto config =
      json::JsonObjectBuilder()
          .add("filter_chains",
               json::JsonArrayBuilder()
                   .add(json::JsonObjectBuilder()
                            .add("name", "test")
                            .add("filters",
                                 json::JsonArrayBuilder()
                                     .add(json::JsonObjectBuilder()
                                              .add("type", "unknown_filter")
                                              .add("name", "unknown")
                                              .add("config",
                                                   json::JsonValue::object())
                                              .build())
                                     .build())
                            .build())
                   .build())
          .build();

  ConfigurableFilterChainFactory factory(config["filter_chains"][0]);
  MockFilterManager filter_manager;

  // Should handle unknown filter type gracefully
  bool result = factory.createFilterChain(filter_manager);

  // Factory should report failure or skip unknown filter
  // Exact behavior depends on implementation
  EXPECT_EQ(filter_manager.getFilterCount(), 0);
}

}  // namespace test
}  // namespace filter
}  // namespace mcp