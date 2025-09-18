/**
 * @file test_filter_chain_builder.cc
 * @brief Tests for configuration-driven filter chain builder
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <chrono>

#include "mcp/filter/filter_chain_builder.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/network/filter.h"

// Must undefine before redefining
#ifdef GOPHER_LOG_COMPONENT
#undef GOPHER_LOG_COMPONENT
#endif

#include "mcp/logging/log_macros.h"
#include "mcp/logging/logger_registry.h"

#define GOPHER_LOG_COMPONENT "test.chainbuilder"

namespace mcp {
namespace filter {
namespace test {

// ============================================================================
// Mock Classes
// ============================================================================

class MockFilter : public network::NetworkFilterBase {
 public:
  MOCK_METHOD(network::FilterStatus, onData, (Buffer& data, bool end_stream), (override));
  MOCK_METHOD(network::FilterStatus, onWrite, (Buffer& data, bool end_stream), (override));
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

  network::FilterSharedPtr createFilter(const json::JsonValue& config) const override {
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
  network::FilterStatus onWrite() override { return network::FilterStatus::Continue; }
  void onConnectionEvent(network::ConnectionEvent event) override {}
  
  int filter_count_ = 0;
  std::vector<network::ReadFilterSharedPtr> read_filters_;
  std::vector<network::WriteFilterSharedPtr> write_filters_;
  std::vector<network::FilterSharedPtr> filters_;
};

class MockDependencyValidator : public DependencyValidator {
 public:
  bool validate(const std::vector<FilterConfig>& filters) const override {
    return validate_result_;
  }
  
  std::vector<std::string> getErrors() const override {
    return errors_;
  }
  
  std::vector<FilterConfig> reorder(
      const std::vector<FilterConfig>& filters) const override {
    if (use_custom_order_) {
      return custom_order_;
    }
    return filters;
  }
  
  bool validate_result_ = true;
  std::vector<std::string> errors_;
  bool use_custom_order_ = false;
  std::vector<FilterConfig> custom_order_;
};

// ============================================================================
// Test Fixture
// ============================================================================

class FilterChainBuilderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Set up test logging
    auto& logger_registry = logging::LoggerRegistry::instance();
    // Use setLevel for the default logger instead of setDefaultLevel
    logger_registry.getDefaultLogger()->setLevel(logging::LogLevel::Debug);
    
    // Register mock filters
    registerMockFilter("mock_filter_1");
    registerMockFilter("mock_filter_2");
    registerMockFilter("mock_filter_3");
    registerMockFilter("null_filter", true);  // Returns nullptr
  }
  
  void TearDown() override {
    // Clean up registry (if needed)
  }
  
  void registerMockFilter(const std::string& name, bool return_null = false) {
    auto factory = std::make_shared<MockFilterFactory>(name, return_null);
    FilterRegistry::instance().registerFactory(name, factory);
  }
  
  json::JsonValue createFilterConfig(const std::string& name,
                                     bool enabled = true,
                                     int priority = 0) {
    return json::JsonObjectBuilder()
        .add("name", name)
        .add("enabled", enabled)
        .add("priority", priority)
        .add("config", json::JsonObjectBuilder()
            .add("test_param", "test_value")
            .build())
        .build();
  }
};

// ============================================================================
// Basic Builder Tests
// ============================================================================

TEST_F(FilterChainBuilderTest, EmptyChain) {
  FilterChainBuilder builder;
  
  EXPECT_EQ(builder.getFilterCount(), 0);
  EXPECT_FALSE(builder.validate());
  
  auto errors = builder.getValidationErrors();
  EXPECT_FALSE(errors.empty());
  EXPECT_NE(errors[0].find("empty"), std::string::npos);
}

TEST_F(FilterChainBuilderTest, AddSingleFilter) {
  FilterChainBuilder builder;
  
  auto config = json::JsonObjectBuilder()
      .add("param1", "value1")
      .build();
  
  builder.addFilter("mock_filter_1", config);
  
  EXPECT_EQ(builder.getFilterCount(), 1);
  EXPECT_TRUE(builder.validate());
}

TEST_F(FilterChainBuilderTest, AddMultipleFilters) {
  FilterChainBuilder builder;
  
  builder.addFilter("mock_filter_1", json::JsonValue::object())
         .addFilter("mock_filter_2", json::JsonValue::object())
         .addFilter("mock_filter_3", json::JsonValue::object());
  
  EXPECT_EQ(builder.getFilterCount(), 3);
  EXPECT_TRUE(builder.validate());
}

TEST_F(FilterChainBuilderTest, ConditionalAddition) {
  FilterChainBuilder builder;
  
  builder.addFilterIf(true, "mock_filter_1", json::JsonValue::object())
         .addFilterIf(false, "mock_filter_2", json::JsonValue::object())
         .addFilterIf(true, "mock_filter_3", json::JsonValue::object());
  
  EXPECT_EQ(builder.getFilterCount(), 2);
  
  const auto& filters = builder.getFilters();
  EXPECT_EQ(filters[0].name, "mock_filter_1");
  EXPECT_EQ(filters[1].name, "mock_filter_3");
}

TEST_F(FilterChainBuilderTest, JsonConditionalAddition) {
  FilterChainBuilder builder;
  
  auto enabled_condition = json::JsonObjectBuilder()
      .add("enabled", true)
      .build();
  
  auto disabled_condition = json::JsonObjectBuilder()
      .add("enabled", false)
      .build();
  
  builder.addFilterIf(enabled_condition, "mock_filter_1", json::JsonValue::object())
         .addFilterIf(disabled_condition, "mock_filter_2", json::JsonValue::object());
  
  EXPECT_EQ(builder.getFilterCount(), 1);
  EXPECT_EQ(builder.getFilters()[0].name, "mock_filter_1");
}

// ============================================================================
// Configuration Loading Tests
// ============================================================================

TEST_F(FilterChainBuilderTest, LoadFromConfig) {
  auto config = json::JsonObjectBuilder()
      .add("filters", json::JsonArrayBuilder()
          .add(createFilterConfig("mock_filter_1"))
          .add(createFilterConfig("mock_filter_2"))
          .add(createFilterConfig("mock_filter_3", false))  // disabled
          .build())
      .build();
  
  FilterChainBuilder builder;
  builder.fromConfig(config);
  
  EXPECT_EQ(builder.getFilterCount(), 2);  // One disabled
  EXPECT_EQ(builder.getEnabledFilterCount(), 2);
}

TEST_F(FilterChainBuilderTest, LoadWithOptions) {
  auto config = json::JsonObjectBuilder()
      .add("options", json::JsonObjectBuilder()
          .add("validate_dependencies", false)
          .add("allow_missing_optional", false)
          .add("enable_metrics", true)
          .add("max_build_time_ms", 5)
          .add("ordering_strategy", "priority")
          .build())
      .add("filters", json::JsonArrayBuilder()
          .add(createFilterConfig("mock_filter_1"))
          .build())
      .build();
  
  FilterChainBuilder builder;
  builder.fromConfig(config);
  
  EXPECT_TRUE(builder.validate());
}

TEST_F(FilterChainBuilderTest, LoadWithDependencies) {
  auto config = json::JsonObjectBuilder()
      .add("filters", json::JsonArrayBuilder()
          .add(json::JsonObjectBuilder()
              .add("name", "mock_filter_1")
              .add("dependencies", json::JsonArrayBuilder()
                  .build())
              .build())
          .add(json::JsonObjectBuilder()
              .add("name", "mock_filter_2")
              .add("dependencies", json::JsonArrayBuilder()
                  .add("mock_filter_1")
                  .build())
              .build())
          .build())
      .build();
  
  FilterChainBuilder builder;
  builder.fromConfig(config);
  
  EXPECT_TRUE(builder.validate());
}

TEST_F(FilterChainBuilderTest, LoadWithConditions) {
  auto config = json::JsonObjectBuilder()
      .add("filters", json::JsonArrayBuilder()
          .add(json::JsonObjectBuilder()
              .add("name", "mock_filter_1")
              .add("conditions", json::JsonObjectBuilder()
                  .add("enabled", true)
                  .build())
              .build())
          .add(json::JsonObjectBuilder()
              .add("name", "mock_filter_2")
              .add("conditions", json::JsonObjectBuilder()
                  .add("enabled", false)
                  .build())
              .build())
          .build())
      .build();
  
  FilterChainBuilder builder;
  builder.fromConfig(config);
  
  EXPECT_EQ(builder.getFilterCount(), 1);
  EXPECT_EQ(builder.getFilters()[0].name, "mock_filter_1");
}

// ============================================================================
// Ordering Tests
// ============================================================================

TEST_F(FilterChainBuilderTest, ManualOrdering) {
  FilterChainBuilder builder;
  
  builder.withOrdering("manual")
         .addFilter("mock_filter_3", json::JsonValue::object())
         .addFilter("mock_filter_1", json::JsonValue::object())
         .addFilter("mock_filter_2", json::JsonValue::object());
  
  const auto& filters = builder.getFilters();
  EXPECT_EQ(filters[0].name, "mock_filter_3");
  EXPECT_EQ(filters[1].name, "mock_filter_1");
  EXPECT_EQ(filters[2].name, "mock_filter_2");
}

TEST_F(FilterChainBuilderTest, PriorityOrdering) {
  auto config = json::JsonObjectBuilder()
      .add("options", json::JsonObjectBuilder()
          .add("ordering_strategy", "priority")
          .build())
      .add("filters", json::JsonArrayBuilder()
          .add(json::JsonObjectBuilder()
              .add("name", "mock_filter_1")
              .add("priority", 30)
              .build())
          .add(json::JsonObjectBuilder()
              .add("name", "mock_filter_2")
              .add("priority", 10)
              .build())
          .add(json::JsonObjectBuilder()
              .add("name", "mock_filter_3")
              .add("priority", 20)
              .build())
          .build())
      .build();
  
  FilterChainBuilder builder;
  builder.fromConfig(config);
  
  auto filters = builder.buildFilters();
  // Note: buildFilters() applies ordering
  // Priority 10 should come first, then 20, then 30
}

// ============================================================================
// Dependency Validation Tests
// ============================================================================

TEST_F(FilterChainBuilderTest, ValidDependencies) {
  FilterChainBuilder builder;
  
  FilterConfig filter1("mock_filter_1", json::JsonValue::object());
  FilterConfig filter2("mock_filter_2", json::JsonValue::object());
  filter2.dependencies.push_back("mock_filter_1");
  
  builder.addFilter(filter1)
         .addFilter(filter2);
  
  EXPECT_TRUE(builder.validate());
}

TEST_F(FilterChainBuilderTest, InvalidDependencies) {
  FilterChainBuilder builder;
  
  FilterConfig filter1("mock_filter_1", json::JsonValue::object());
  filter1.dependencies.push_back("mock_filter_2");  // Depends on later filter
  FilterConfig filter2("mock_filter_2", json::JsonValue::object());
  
  builder.addFilter(filter1)
         .addFilter(filter2);
  
  EXPECT_FALSE(builder.validate());
  
  auto errors = builder.getValidationErrors();
  EXPECT_FALSE(errors.empty());
  EXPECT_NE(errors[0].find("depends on"), std::string::npos);
}

TEST_F(FilterChainBuilderTest, ExternalDependencyValidator) {
  auto validator = std::make_shared<MockDependencyValidator>();
  validator->validate_result_ = false;
  validator->errors_.push_back("Custom validation error");
  
  FilterChainBuilder builder;
  builder.withDependencyValidator(validator)
         .addFilter("mock_filter_1", json::JsonValue::object());
  
  EXPECT_FALSE(builder.validate());
  
  auto errors = builder.getValidationErrors();
  EXPECT_FALSE(errors.empty());
  EXPECT_NE(errors[0].find("Custom validation error"), std::string::npos);
}

// ============================================================================
// Build Tests
// ============================================================================

TEST_F(FilterChainBuilderTest, BuildValidChain) {
  MockFilterManager manager;
  
  FilterChainBuilder builder;
  builder.addFilter("mock_filter_1", json::JsonValue::object())
         .addFilter("mock_filter_2", json::JsonValue::object());
  
  EXPECT_TRUE(builder.build(manager));
  EXPECT_EQ(manager.filter_count_, 2);  // Mock factories create filters successfully
}

TEST_F(FilterChainBuilderTest, BuildInvalidChain) {
  MockFilterManager manager;
  
  FilterChainBuilder builder;
  // Empty chain is invalid
  
  EXPECT_FALSE(builder.build(manager));
  EXPECT_EQ(manager.filter_count_, 0);
}

TEST_F(FilterChainBuilderTest, BuildFiltersWithoutManager) {
  FilterChainBuilder builder;
  builder.addFilter("mock_filter_1", json::JsonValue::object())
         .addFilter("mock_filter_2", json::JsonValue::object());
  
  auto filters = builder.buildFilters();
  EXPECT_EQ(filters.size(), 2);  // Mock factories create filters successfully
}

TEST_F(FilterChainBuilderTest, BuildWithUnknownFilter) {
  MockFilterManager manager;
  
  FilterChainBuilder builder;
  builder.addFilter("unknown_filter", json::JsonValue::object());
  
  EXPECT_FALSE(builder.validate());
  EXPECT_FALSE(builder.build(manager));
}

// ============================================================================
// Clone Tests
// ============================================================================

TEST_F(FilterChainBuilderTest, CloneBuilder) {
  FilterChainBuilder builder;
  builder.addFilter("mock_filter_1", json::JsonValue::object())
         .addFilter("mock_filter_2", json::JsonValue::object());
  
  auto cloned = builder.clone();
  
  EXPECT_EQ(cloned->getFilterCount(), 2);
  EXPECT_EQ(cloned->getFilters()[0].name, "mock_filter_1");
  EXPECT_EQ(cloned->getFilters()[1].name, "mock_filter_2");
  
  // Modify original shouldn't affect clone
  builder.addFilter("mock_filter_3", json::JsonValue::object());
  EXPECT_EQ(builder.getFilterCount(), 3);
  EXPECT_EQ(cloned->getFilterCount(), 2);
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(FilterChainBuilderTest, BuildTimeConstraint) {
  MockFilterManager manager;
  
  FilterChainBuilder builder;
  
  // Add many filters
  for (int i = 0; i < 10; ++i) {
    builder.addFilter("mock_filter_1", json::JsonValue::object());
  }
  
  ChainBuildOptions options;
  options.enable_metrics = true;
  options.max_build_time = std::chrono::milliseconds(10);
  builder.withOptions(options);
  
  auto start = std::chrono::steady_clock::now();
  builder.build(manager);
  auto end = std::chrono::steady_clock::now();
  
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  
  // Should complete within reasonable time
  EXPECT_LT(duration.count(), 100);  // 100ms is generous for test
}

// ============================================================================
// ConfigurableFilterChainFactory Tests
// ============================================================================

TEST_F(FilterChainBuilderTest, ConfigurableFactoryBasic) {
  auto config = json::JsonObjectBuilder()
      .add("filters", json::JsonArrayBuilder()
          .add(createFilterConfig("mock_filter_1"))
          .add(createFilterConfig("mock_filter_2"))
          .build())
      .build();
  
  ConfigurableFilterChainFactory factory(config);
  
  MockFilterManager manager;
  EXPECT_TRUE(factory.createFilterChain(manager));
}

TEST_F(FilterChainBuilderTest, ConfigurableFactoryValidation) {
  auto valid_config = json::JsonObjectBuilder()
      .add("filters", json::JsonArrayBuilder()
          .add(createFilterConfig("mock_filter_1"))
          .build())
      .build();
  
  auto invalid_config = json::JsonObjectBuilder()
      .add("filters", json::JsonArrayBuilder()
          .add(createFilterConfig("unknown_filter"))
          .build())
      .build();
  
  EXPECT_TRUE(ConfigurableFilterChainFactory::validateConfiguration(valid_config));
  EXPECT_FALSE(ConfigurableFilterChainFactory::validateConfiguration(invalid_config));
}

TEST_F(FilterChainBuilderTest, ConfigurableFactoryClone) {
  auto config = json::JsonObjectBuilder()
      .add("filters", json::JsonArrayBuilder()
          .add(createFilterConfig("mock_filter_1"))
          .build())
      .build();
  
  ConfigurableFilterChainFactory factory(config);
  auto cloned = factory.clone();
  
  MockFilterManager manager1, manager2;
  EXPECT_TRUE(factory.createFilterChain(manager1));
  EXPECT_TRUE(cloned->createFilterChain(manager2));
}

TEST_F(FilterChainBuilderTest, ConfigurableFactoryWithAdditionalFactories) {
  auto config = json::JsonObjectBuilder()
      .add("filters", json::JsonArrayBuilder()
          .add(createFilterConfig("mock_filter_1"))
          .build())
      .build();
  
  ConfigurableFilterChainFactory factory(config);
  
  std::vector<network::FilterFactoryCb> additional_factories;
  additional_factories.push_back([]() {
    return std::make_shared<MockFilter>();
  });
  
  MockFilterManager manager;
  EXPECT_TRUE(factory.createNetworkFilterChain(manager, additional_factories));
  EXPECT_EQ(manager.filter_count_, 2);  // One from config + one from additional factory
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(FilterChainBuilderTest, ClearValidationErrors) {
  FilterChainBuilder builder;
  
  // Create invalid chain
  FilterConfig filter("mock_filter_1", json::JsonValue::object());
  filter.dependencies.push_back("nonexistent");
  builder.addFilter(filter);
  
  EXPECT_FALSE(builder.validate());
  EXPECT_FALSE(builder.getValidationErrors().empty());
  
  // Clear and create valid chain
  builder.clear();
  builder.addFilter("mock_filter_1", json::JsonValue::object());
  
  EXPECT_TRUE(builder.validate());
  EXPECT_TRUE(builder.getValidationErrors().empty());
}

TEST_F(FilterChainBuilderTest, InvalidConfigurationHandling) {
  FilterChainBuilder builder;
  
  // Invalid config type
  builder.fromConfig(json::JsonValue("not an object"));
  EXPECT_EQ(builder.getFilterCount(), 0);
  
  // Invalid filters array
  auto bad_config = json::JsonObjectBuilder()
      .add("filters", "not an array")
      .build();
  builder.fromConfig(bad_config);
  EXPECT_EQ(builder.getFilterCount(), 0);
  
  // Filter without name
  auto no_name_config = json::JsonObjectBuilder()
      .add("filters", json::JsonArrayBuilder()
          .add(json::JsonObjectBuilder()
              .add("enabled", true)
              .build())
          .build())
      .build();
  builder.fromConfig(no_name_config);
  EXPECT_EQ(builder.getFilterCount(), 0);
}

}  // namespace test
}  // namespace filter
}  // namespace mcp