/**
 * @file test_filter_chain_assembler.cc
 * @brief Tests for FilterChainAssembler
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/config/types.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/filter_chain_assembler.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/json/json_bridge.h"
#include "mcp/mcp_connection_manager.h"

#define GOPHER_LOG_COMPONENT "test.assembler"

namespace mcp {
namespace filter {
namespace test {

class MockProtocolCallbacks : public McpProtocolCallbacks {
 public:
  void onRequest(const jsonrpc::Request& request) override {}
  void onResponse(const jsonrpc::Response& response) override {}
  void onNotification(const jsonrpc::Notification& notification) override {}
  void onError(const Error& error) override {}
  void onConnectionEvent(network::ConnectionEvent event) override {}
};

class MockFilter : public network::NetworkFilterBase {
 public:
  MOCK_METHOD(network::FilterStatus,
              onData,
              (Buffer & data, bool end_stream),
              (override));
  MOCK_METHOD(network::FilterStatus,
              onWrite,
              (Buffer & data, bool end_stream),
              (override));
  MOCK_METHOD(network::FilterStatus, onNewConnection, (), (override));
};

class MockFilterFactory : public FilterFactory {
 public:
  explicit MockFilterFactory(const std::string& name, bool return_null = false)
      : name_(name), return_null_(return_null) {
    metadata_.name = name;
    metadata_.version = "1.0.0";
    metadata_.description = "Mock filter for testing";
  }

  network::FilterSharedPtr createFilter(
      const json::JsonValue& config) const override {
    create_count_++;
    last_config_ = config;

    if (return_null_) {
      return nullptr;
    }

    return std::make_shared<MockFilter>();
  }

  const FilterFactoryMetadata& getMetadata() const override {
    return metadata_;
  }

  json::JsonValue getDefaultConfig() const override {
    return json::JsonValue::object();
  }

  bool validateConfig(const json::JsonValue& config) const override {
    return true;
  }

  mutable int create_count_ = 0;
  mutable json::JsonValue last_config_;

 private:
  std::string name_;
  bool return_null_;
  FilterFactoryMetadata metadata_;
};

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
    filter_count_++;
  }

  void removeReadFilter(network::ReadFilterSharedPtr filter) override {}
  bool initializeReadFilters() override { return true; }
  void onRead() override {}
  network::FilterStatus onWrite() override {
    return network::FilterStatus::Continue;
  }
  void onConnectionEvent(network::ConnectionEvent event) override {}

  int filter_count_ = 0;
  std::vector<network::ReadFilterSharedPtr> read_filters_;
  std::vector<network::WriteFilterSharedPtr> write_filters_;
  std::vector<network::FilterSharedPtr> filters_;
};

class FilterChainAssemblerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    callbacks_ = std::make_unique<MockProtocolCallbacks>();

    // Clear registry to start fresh
    FilterRegistry::instance().clearFactories();

    // Register mock filters
    registerMockFilter("http.codec");
    registerMockFilter("sse.codec");
    registerMockFilter("json_rpc.dispatcher");
    registerMockFilter("rate_limit");
    registerMockFilter("circuit_breaker");
    registerMockFilter("metrics");
    registerMockFilter("null_filter", true);  // Returns nullptr
  }

  void TearDown() override { FilterRegistry::instance().clearFactories(); }

  void registerMockFilter(const std::string& name, bool return_null = false) {
    auto factory = std::make_shared<MockFilterFactory>(name, return_null);

    // Register both old and new style
    FilterRegistry::instance().registerFactory(name, factory);

    // Register context-aware factory
    BasicFilterMetadata metadata;
    metadata.name = name;
    metadata.version = "1.0.0";
    metadata.description = "Mock filter for testing";

    FilterRegistry::instance().registerContextFactory(
        name,
        [factory](const FilterCreationContext& ctx,
                  const json::JsonValue& config) {
          return factory->createFilter(config);
        },
        metadata);
  }

  FilterCreationContext createContext() {
    TransportMetadata transport("127.0.0.1", 8080);
    return FilterCreationContext(*dispatcher_, *callbacks_,
                                 ConnectionMode::Server, transport);
  }

  config::FilterConfig createFilterConfig(const std::string& name) {
    config::FilterConfig fc;
    fc.type = name;
    fc.name = name;
    return fc;
  }

  config::FilterConfig createFilterConfig(const std::string& name,
                                          const json::JsonValue& config) {
    config::FilterConfig fc;
    fc.type = name;
    fc.name = name;
    fc.config = config;
    return fc;
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::unique_ptr<MockProtocolCallbacks> callbacks_;
};

// Basic Assembly Tests

TEST_F(FilterChainAssemblerTest, BasicFilterChainAssembly) {
  config::FilterChainConfig config;
  config.filters = {createFilterConfig("http.codec"),
                    createFilterConfig("sse.codec"),
                    createFilterConfig("json_rpc.dispatcher")};

  FilterChainAssembler assembler(FilterRegistry::instance());
  MockFilterManager manager;
  auto context = createContext();

  auto result = assembler.assembleFilterChain(config, context, manager);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(manager.filter_count_, 3);
  EXPECT_EQ(result.created_filters.size(), 3);
  EXPECT_EQ(result.created_filters[0], "http.codec");
  EXPECT_EQ(result.created_filters[1], "sse.codec");
  EXPECT_EQ(result.created_filters[2], "json_rpc.dispatcher");
}

TEST_F(FilterChainAssemblerTest, SingleFilterAssembly) {
  config::FilterChainConfig config;
  config.filters = {createFilterConfig("http.codec")};

  FilterChainAssembler assembler(FilterRegistry::instance());
  MockFilterManager manager;
  auto context = createContext();

  auto result = assembler.assembleFilterChain(config, context, manager);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(manager.filter_count_, 1);
  EXPECT_EQ(result.created_filters.size(), 1);
  EXPECT_EQ(result.created_filters[0], "http.codec");
}

TEST_F(FilterChainAssemblerTest, EmptyFilterChain) {
  config::FilterChainConfig config;
  // Empty filters

  FilterChainAssembler assembler(FilterRegistry::instance());
  MockFilterManager manager;
  auto context = createContext();

  auto result = assembler.assembleFilterChain(config, context, manager);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(manager.filter_count_, 0);
}

// Validation Tests

TEST_F(FilterChainAssemblerTest, MissingFilterFactory) {
  config::FilterChainConfig config;
  config.filters = {createFilterConfig("unknown_filter")};

  FilterChainAssembler assembler(FilterRegistry::instance());

  auto result = assembler.validateFilterChain(config);

  EXPECT_FALSE(result.valid);
  EXPECT_FALSE(result.errors.empty());
  EXPECT_THAT(result.errors[0],
              testing::HasSubstr("Unknown filter type: unknown_filter"));
}

TEST_F(FilterChainAssemblerTest, EmptyFilterChainValidation) {
  config::FilterChainConfig config;
  // Empty filters

  FilterChainAssembler assembler(FilterRegistry::instance());

  auto result = assembler.validateFilterChain(config);

  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.errors.empty());
}

TEST_F(FilterChainAssemblerTest, ValidFilterChainValidation) {
  config::FilterChainConfig config;
  config.filters = {createFilterConfig("http.codec"),
                    createFilterConfig("sse.codec"),
                    createFilterConfig("json_rpc.dispatcher")};

  FilterChainAssembler assembler(FilterRegistry::instance());

  auto result = assembler.validateFilterChain(config);

  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.errors.empty());
}

// Filter Ordering Validation Tests

TEST_F(FilterChainAssemblerTest, FilterOrderingValidation) {
  config::FilterChainConfig config;
  config.filters = {createFilterConfig("json_rpc.dispatcher"),  // Wrong order
                    createFilterConfig("http.codec"),
                    createFilterConfig("sse.codec")};

  FilterChainAssembler assembler(FilterRegistry::instance());

  auto result = assembler.validateFilterChain(config);

  EXPECT_TRUE(result.valid);  // Still valid, just warnings
  EXPECT_FALSE(result.warnings.empty());
  EXPECT_THAT(result.warnings[0],
              testing::HasSubstr("Filter ordering may be incorrect"));
}

TEST_F(FilterChainAssemblerTest, CorrectFilterOrdering) {
  config::FilterChainConfig config;
  config.filters = {createFilterConfig("http.codec"),
                    createFilterConfig("sse.codec"),
                    createFilterConfig("json_rpc.dispatcher")};

  FilterChainAssembler assembler(FilterRegistry::instance());

  auto result = assembler.validateFilterChain(config);

  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.warnings.empty());  // No ordering warnings
}

TEST_F(FilterChainAssemblerTest, QoSFilterOrdering) {
  config::FilterChainConfig config;
  config.filters = {
      createFilterConfig("rate_limit"), createFilterConfig("circuit_breaker"),
      createFilterConfig("http.codec"), createFilterConfig("sse.codec"),
      createFilterConfig("json_rpc.dispatcher")};

  FilterChainAssembler assembler(FilterRegistry::instance());
  MockFilterManager manager;
  auto context = createContext();

  auto result = assembler.assembleFilterChain(config, context, manager);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(manager.filter_count_, 5);
  EXPECT_TRUE(result.warnings.empty());  // Should be correct ordering
}

// Error Handling Tests

TEST_F(FilterChainAssemblerTest, FilterCreationFailure) {
  config::FilterChainConfig config;
  config.filters = {createFilterConfig("http.codec"),
                    createFilterConfig("null_filter"),  // Returns nullptr
                    createFilterConfig("sse.codec")};

  FilterChainAssembler assembler(FilterRegistry::instance());
  MockFilterManager manager;
  auto context = createContext();

  auto result = assembler.assembleFilterChain(config, context, manager);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(manager.filter_count_, 0);  // No filters added on failure
  EXPECT_THAT(result.error_message,
              testing::HasSubstr("Failed to create filter 'null_filter'"));
}

TEST_F(FilterChainAssemblerTest, PartialFilterCreation) {
  config::FilterChainConfig config;
  config.filters = {
      createFilterConfig("http.codec"),
      createFilterConfig("unknown_filter")  // Will fail validation
  };

  FilterChainAssembler assembler(FilterRegistry::instance());
  MockFilterManager manager;
  auto context = createContext();

  auto result = assembler.assembleFilterChain(config, context, manager);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(manager.filter_count_,
            0);  // No filters added due to validation failure
  EXPECT_EQ(result.error_message, "Filter chain validation failed");
}

// ConfigurableFilterChainFactory Tests

TEST_F(FilterChainAssemblerTest, ConfigurableFactoryBasic) {
  config::FilterChainConfig config;
  config.filters = {createFilterConfig("http.codec"),
                    createFilterConfig("sse.codec"),
                    createFilterConfig("json_rpc.dispatcher")};

  ConfigurableFilterChainFactory factory(config);
  MockFilterManager manager;
  auto context = createContext();

  EXPECT_TRUE(factory.createFilterChain(context, manager));
  EXPECT_EQ(manager.filter_count_, 3);
}

TEST_F(FilterChainAssemblerTest, ConfigurableFactoryValidation) {
  config::FilterChainConfig valid_config;
  valid_config.filters = {createFilterConfig("http.codec")};

  config::FilterChainConfig invalid_config;
  invalid_config.filters = {createFilterConfig("unknown_filter")};

  ConfigurableFilterChainFactory valid_factory(valid_config);
  ConfigurableFilterChainFactory invalid_factory(invalid_config);

  EXPECT_TRUE(valid_factory.validateConfiguration());
  EXPECT_FALSE(invalid_factory.validateConfiguration());

  auto errors = invalid_factory.getValidationErrors();
  EXPECT_FALSE(errors.empty());
  EXPECT_THAT(errors[0], testing::HasSubstr("Unknown filter type"));
}

TEST_F(FilterChainAssemblerTest, ConfigurableFactoryFailure) {
  config::FilterChainConfig config;
  config.filters = {
      createFilterConfig("null_filter")  // Will fail to create
  };

  ConfigurableFilterChainFactory factory(config);
  MockFilterManager manager;
  auto context = createContext();

  EXPECT_FALSE(factory.createFilterChain(context, manager));
  EXPECT_EQ(manager.filter_count_, 0);
}

// Configuration Integration Tests

TEST_F(FilterChainAssemblerTest, FilterConfigurationPassing) {
  config::FilterChainConfig config;

  // Create filter with custom configuration
  json::JsonValue custom_config = json::JsonValue::object();
  custom_config["test_param"] = json::JsonValue("test_value");

  config.filters = {createFilterConfig("http.codec", custom_config)};

  FilterChainAssembler assembler(FilterRegistry::instance());
  MockFilterManager manager;
  auto context = createContext();

  auto result = assembler.assembleFilterChain(config, context, manager);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(manager.filter_count_, 1);

  // Verify configuration was passed (would need access to mock factory to
  // check)
}

TEST_F(FilterChainAssemblerTest, ComplexFilterChain) {
  config::FilterChainConfig config;
  config.filters = {createFilterConfig("rate_limit"),
                    createFilterConfig("circuit_breaker"),
                    createFilterConfig("metrics"),
                    createFilterConfig("http.codec"),
                    createFilterConfig("sse.codec"),
                    createFilterConfig("json_rpc.dispatcher")};

  FilterChainAssembler assembler(FilterRegistry::instance());
  MockFilterManager manager;
  auto context = createContext();

  auto result = assembler.assembleFilterChain(config, context, manager);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(manager.filter_count_, 6);
  EXPECT_EQ(result.created_filters.size(), 6);

  // Verify all filters were created in order
  EXPECT_EQ(result.created_filters[0], "rate_limit");
  EXPECT_EQ(result.created_filters[1], "circuit_breaker");
  EXPECT_EQ(result.created_filters[2], "metrics");
  EXPECT_EQ(result.created_filters[3], "http.codec");
  EXPECT_EQ(result.created_filters[4], "sse.codec");
  EXPECT_EQ(result.created_filters[5], "json_rpc.dispatcher");
}

}  // namespace test
}  // namespace filter
}  // namespace mcp