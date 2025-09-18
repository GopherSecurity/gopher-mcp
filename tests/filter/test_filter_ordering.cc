/**
 * @file test_filter_ordering.cc
 * @brief Comprehensive tests for filter ordering validator
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <iostream>

#include "mcp/config/filter_order_validator.h"
#include "mcp/filter/filter_chain_builder.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"

// Must undefine before redefining
#ifdef GOPHER_LOG_COMPONENT
#undef GOPHER_LOG_COMPONENT
#endif

#include "mcp/logging/log_macros.h"
#include "mcp/logging/logger_registry.h"

#define GOPHER_LOG_COMPONENT "test.filter.ordering"

using namespace mcp;
using namespace mcp::config;
using namespace mcp::filter;
using namespace mcp::json;

namespace {

// Helper to create FilterConfig
FilterConfig createFilter(const std::string& name, bool enabled = true) {
  FilterConfig config;
  config.name = name;
  config.enabled = enabled;
  config.config = JsonValue::object();
  return config;
}

FilterConfig createFilterWithDeps(const std::string& name, 
                                  const std::vector<std::string>& deps) {
  FilterConfig config = createFilter(name);
  config.dependencies = deps;
  return config;
}

// Helper to create JSON chain config
JsonValue createChainJson(const std::string& name,
                         const std::vector<std::string>& filter_names) {
  JsonObjectBuilder chain;
  chain.add("name", name);
  
  JsonArrayBuilder filters;
  for (const auto& filter_name : filter_names) {
    filters.add(JsonObjectBuilder()
               .add("name", filter_name)
               .add("enabled", true)
               .build());
  }
  
  chain.add("filters", filters.build());
  return chain.build();
}

}  // namespace

// ============================================================================
// Test Fixture
// ============================================================================

class FilterOrderValidatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    validator_ = std::make_shared<FilterOrderValidator>();
    validator_->initializeDefaults();
  }

  void TearDown() override {
    validator_.reset();
  }

  std::shared_ptr<FilterOrderValidator> validator_;
};

// ============================================================================
// Basic Functionality Tests
// ============================================================================

TEST_F(FilterOrderValidatorTest, EmptyChainValidation) {
  std::vector<FilterConfig> filters;
  EXPECT_TRUE(validator_->validate(filters));
  EXPECT_TRUE(validator_->getErrors().empty());
}

TEST_F(FilterOrderValidatorTest, SingleFilterValidation) {
  std::vector<FilterConfig> filters = {
    createFilter("json_rpc")
  };
  
  EXPECT_TRUE(validator_->validate(filters));
  if (!validator_->getErrors().empty()) {
    for (const auto& error : validator_->getErrors()) {
      std::cout << "Error: " << error << std::endl;
    }
  }
  EXPECT_TRUE(validator_->getErrors().empty());
}

TEST_F(FilterOrderValidatorTest, RegisterCustomFilter) {
  FilterOrderMetadata custom_meta("my_custom_filter", FilterStage::Application);
  validator_->registerFilter(custom_meta);
  
  EXPECT_TRUE(validator_->hasFilter("my_custom_filter"));
  EXPECT_EQ(FilterStage::Application, validator_->getFilterStage("my_custom_filter"));
}

// ============================================================================
// Stage Ordering Tests
// ============================================================================

TEST_F(FilterOrderValidatorTest, ValidStageOrdering) {
  std::vector<FilterConfig> filters = {
    createFilter("rate_limit"),       // Security
    createFilter("circuit_breaker"),  // QoS
    createFilter("metrics"),          // Observability
    createFilter("http_codec"),       // Protocol
    createFilter("json_rpc")          // Application
  };
  
  EXPECT_TRUE(validator_->validate(filters));
  EXPECT_TRUE(validator_->getErrors().empty());
}

TEST_F(FilterOrderValidatorTest, InvalidStageOrdering) {
  std::vector<FilterConfig> filters = {
    createFilter("json_rpc"),          // Application
    createFilter("rate_limit")         // Security (should be first)
  };
  
  EXPECT_FALSE(validator_->validate(filters));
  
  auto errors = validator_->getErrors();
  EXPECT_FALSE(errors.empty());
  EXPECT_THAT(errors[0], testing::HasSubstr("Stage violation"));
}

TEST_F(FilterOrderValidatorTest, MixedStageOrdering) {
  std::vector<FilterConfig> filters = {
    createFilter("metrics"),           // Observability
    createFilter("rate_limit"),        // Security (wrong order)
    createFilter("json_rpc")           // Application
  };
  
  EXPECT_FALSE(validator_->validate(filters));
  
  auto errors = validator_->getErrors();
  EXPECT_FALSE(errors.empty());
}

// ============================================================================
// Dependency Tests
// ============================================================================

TEST_F(FilterOrderValidatorTest, ValidDependencies) {
  std::vector<FilterConfig> filters = {
    createFilter("http_codec"),
    createFilterWithDeps("sse_codec", {"http_codec"})
  };
  
  EXPECT_TRUE(validator_->validate(filters));
  EXPECT_TRUE(validator_->getErrors().empty());
}

TEST_F(FilterOrderValidatorTest, MissingDependency) {
  std::vector<FilterConfig> filters = {
    createFilterWithDeps("sse_codec", {"http_codec"})  // Missing http_codec
  };
  
  EXPECT_FALSE(validator_->validate(filters));
  
  auto errors = validator_->getErrors();
  EXPECT_FALSE(errors.empty());
  EXPECT_THAT(errors[0], testing::HasSubstr("Missing required"));
}

TEST_F(FilterOrderValidatorTest, DisabledDependency) {
  FilterConfig http = createFilter("http_codec");
  http.enabled = false;
  
  std::vector<FilterConfig> filters = {
    http,  // Disabled
    createFilterWithDeps("sse_codec", {"http_codec"})
  };
  
  EXPECT_FALSE(validator_->validate(filters));
  EXPECT_THAT(validator_->getErrors()[0], testing::HasSubstr("Missing required"));
}

// ============================================================================
// Constraint Tests (before/after)
// ============================================================================

TEST_F(FilterOrderValidatorTest, BeforeConstraintValid) {
  // http_codec has "before: sse_codec" constraint
  std::vector<FilterConfig> filters = {
    createFilter("http_codec"),
    createFilter("sse_codec")
  };
  
  EXPECT_TRUE(validator_->validate(filters));
  EXPECT_TRUE(validator_->getErrors().empty());
}

TEST_F(FilterOrderValidatorTest, BeforeConstraintViolation) {
  // http_codec must come before sse_codec
  std::vector<FilterConfig> filters = {
    createFilter("sse_codec"),
    createFilter("http_codec")
  };
  
  EXPECT_FALSE(validator_->validate(filters));
  EXPECT_THAT(validator_->getErrors()[0], testing::HasSubstr("Constraint violation"));
}

TEST_F(FilterOrderValidatorTest, AfterConstraintValid) {
  // json_rpc has "after: http_codec" constraint
  std::vector<FilterConfig> filters = {
    createFilter("http_codec"),
    createFilter("json_rpc")
  };
  
  EXPECT_TRUE(validator_->validate(filters));
  EXPECT_TRUE(validator_->getErrors().empty());
}

// ============================================================================
// Transport-Specific Tests
// ============================================================================

TEST_F(FilterOrderValidatorTest, HTTPTransportValid) {
  validator_->setTransportType(TransportType::HTTP);
  
  std::vector<FilterConfig> filters = {
    createFilter("rate_limit"),
    createFilter("metrics"),
    createFilter("http_codec"),
    createFilter("json_rpc")
  };
  
  EXPECT_TRUE(validator_->validate(filters));
  EXPECT_TRUE(validator_->getErrors().empty());
}

TEST_F(FilterOrderValidatorTest, HTTPWithSSETransportValid) {
  validator_->setTransportType(TransportType::HTTPWithSSE);
  
  std::vector<FilterConfig> filters = {
    createFilter("rate_limit"),
    createFilter("http_codec"),
    createFilter("sse_codec"),
    createFilter("json_rpc")
  };
  
  EXPECT_TRUE(validator_->validate(filters));
  EXPECT_TRUE(validator_->getErrors().empty());
}

TEST_F(FilterOrderValidatorTest, HTTPWithSSEMissingCodec) {
  validator_->setTransportType(TransportType::HTTPWithSSE);
  
  std::vector<FilterConfig> filters = {
    createFilter("http_codec"),
    // Missing sse_codec
    createFilter("json_rpc")
  };
  
  EXPECT_FALSE(validator_->validate(filters));
  EXPECT_THAT(validator_->getErrors()[0], testing::HasSubstr("requires 'sse_codec'"));
}

TEST_F(FilterOrderValidatorTest, HTTPWithSSEWrongOrder) {
  validator_->setTransportType(TransportType::HTTPWithSSE);
  
  std::vector<FilterConfig> filters = {
    createFilter("sse_codec"),  // Wrong: should be after http_codec
    createFilter("http_codec"),
    createFilter("json_rpc")
  };
  
  EXPECT_FALSE(validator_->validate(filters));
  EXPECT_THAT(validator_->getErrors()[0], 
              testing::HasSubstr("'http_codec' must come before 'sse_codec'"));
}

TEST_F(FilterOrderValidatorTest, StdioTransportValid) {
  validator_->setTransportType(TransportType::Stdio);
  
  std::vector<FilterConfig> filters = {
    createFilter("rate_limit"),
    createFilter("json_rpc")
  };
  
  EXPECT_TRUE(validator_->validate(filters));
  // Should have warning about typically including json_rpc (which we do)
  EXPECT_TRUE(validator_->getErrors().empty());
}

// ============================================================================
// Reordering Tests
// ============================================================================

TEST_F(FilterOrderValidatorTest, ReorderByStage) {
  std::vector<FilterConfig> filters = {
    createFilter("json_rpc"),          // Application
    createFilter("metrics"),           // Observability
    createFilter("rate_limit"),        // Security
    createFilter("http_codec"),        // Protocol
    createFilter("circuit_breaker")    // QoS
  };
  
  auto reordered = validator_->reorder(filters);
  
  ASSERT_EQ(5u, reordered.size());
  EXPECT_EQ("rate_limit", reordered[0].name);       // Security
  EXPECT_EQ("circuit_breaker", reordered[1].name);  // QoS
  EXPECT_EQ("metrics", reordered[2].name);          // Observability
  EXPECT_EQ("http_codec", reordered[3].name);       // Protocol
  EXPECT_EQ("json_rpc", reordered[4].name);         // Application
}

TEST_F(FilterOrderValidatorTest, ReorderWithDependencies) {
  std::vector<FilterConfig> filters = {
    createFilterWithDeps("sse_codec", {"http_codec"}),
    createFilter("json_rpc"),
    createFilter("http_codec")
  };
  
  auto reordered = validator_->reorder(filters);
  
  ASSERT_EQ(3u, reordered.size());
  // http_codec must come before sse_codec due to dependency
  auto http_pos = std::find_if(reordered.begin(), reordered.end(),
                               [](const FilterConfig& f) { return f.name == "http_codec"; });
  auto sse_pos = std::find_if(reordered.begin(), reordered.end(),
                              [](const FilterConfig& f) { return f.name == "sse_codec"; });
  
  EXPECT_LT(std::distance(reordered.begin(), http_pos),
            std::distance(reordered.begin(), sse_pos));
}

// ============================================================================
// JSON Chain Validation Tests
// ============================================================================

TEST_F(FilterOrderValidatorTest, ValidateChainFromJSON) {
  auto chain_json = createChainJson("test_chain", 
                                   {"rate_limit", "metrics", "http_codec", "json_rpc"});
  
  auto result = validator_->validateChain(chain_json);
  
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.errors.empty());
}

TEST_F(FilterOrderValidatorTest, ValidateChainWithErrors) {
  auto chain_json = createChainJson("bad_chain", 
                                   {"json_rpc", "rate_limit"});  // Wrong order
  
  auto result = validator_->validateChain(chain_json);
  
  EXPECT_FALSE(result.valid);
  EXPECT_FALSE(result.errors.empty());
  EXPECT_THAT(result.errors[0], testing::HasSubstr("bad_chain"));
  EXPECT_THAT(result.errors[0], testing::HasSubstr("Stage violation"));
}

TEST_F(FilterOrderValidatorTest, ValidateChainMissingFilters) {
  JsonObjectBuilder chain;
  chain.add("name", "empty_chain");
  // Missing "filters" field
  
  auto result = validator_->validateChain(chain.build());
  
  EXPECT_FALSE(result.valid);
  EXPECT_THAT(result.errors[0], testing::HasSubstr("must have 'filters' array"));
}

// ============================================================================
// Recommended Ordering Tests
// ============================================================================

TEST_F(FilterOrderValidatorTest, GetRecommendedOrderingHTTP) {
  auto ordering = validator_->getRecommendedOrdering(TransportType::HTTP);
  
  EXPECT_FALSE(ordering.empty());
  // Should start with rate_limit (Security stage)
  EXPECT_EQ("rate_limit", ordering[0]);
  // Should end with json_rpc (Application stage)
  EXPECT_EQ("json_rpc", ordering.back());
}

TEST_F(FilterOrderValidatorTest, GetRecommendedOrderingStdio) {
  auto ordering = validator_->getRecommendedOrdering(TransportType::Stdio);
  
  EXPECT_FALSE(ordering.empty());
  // Should not include http_codec for stdio
  auto it = std::find(ordering.begin(), ordering.end(), "http_codec");
  EXPECT_EQ(ordering.end(), it);
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST_F(FilterOrderValidatorTest, UnknownFilterType) {
  std::vector<FilterConfig> filters = {
    createFilter("unknown_filter_1"),
    createFilter("unknown_filter_2")
  };
  
  // Unknown filters default to Application stage, so should validate
  EXPECT_TRUE(validator_->validate(filters));
  EXPECT_TRUE(validator_->getErrors().empty());
}

TEST_F(FilterOrderValidatorTest, MixedEnabledDisabled) {
  std::vector<FilterConfig> filters = {
    createFilter("json_rpc"),          // Enabled
    createFilter("rate_limit", false), // Disabled (should be ignored)
    createFilter("metrics")            // Enabled
  };
  
  // Should validate because disabled filter is ignored
  EXPECT_TRUE(validator_->validate(filters));
  EXPECT_TRUE(validator_->getErrors().empty());
}

TEST_F(FilterOrderValidatorTest, CyclicDependency) {
  std::vector<FilterConfig> filters = {
    createFilterWithDeps("filter_a", {"filter_b"}),
    createFilterWithDeps("filter_b", {"filter_c"}),
    createFilterWithDeps("filter_c", {"filter_a"})  // Cycle!
  };
  
  auto reordered = validator_->reorder(filters);
  
  // Reorder should still return something (fallback to stage ordering)
  EXPECT_EQ(3u, reordered.size());
}

TEST_F(FilterOrderValidatorTest, ComplexChainValidation) {
  std::vector<FilterConfig> filters = {
    createFilter("rate_limit"),
    createFilter("auth"),
    createFilter("circuit_breaker"),
    createFilter("backpressure"),
    createFilter("metrics"),
    createFilter("tracing"),
    createFilter("http_codec"),
    createFilter("sse_codec"),
    createFilter("json_rpc")
  };
  
  EXPECT_TRUE(validator_->validate(filters));
  EXPECT_TRUE(validator_->getErrors().empty());
}

// ============================================================================
// Helper Function Tests
// ============================================================================

TEST_F(FilterOrderValidatorTest, CreateDefaultValidator) {
  auto default_validator = createDefaultValidator();
  
  EXPECT_TRUE(default_validator->hasFilter("rate_limit"));
  EXPECT_TRUE(default_validator->hasFilter("http_codec"));
  EXPECT_TRUE(default_validator->hasFilter("json_rpc"));
}

TEST_F(FilterOrderValidatorTest, GetFilterStageName) {
  EXPECT_STREQ("Security", getFilterStageName(FilterStage::Security));
  EXPECT_STREQ("QoS", getFilterStageName(FilterStage::QoS));
  EXPECT_STREQ("Observability", getFilterStageName(FilterStage::Observability));
  EXPECT_STREQ("Protocol", getFilterStageName(FilterStage::Protocol));
  EXPECT_STREQ("Application", getFilterStageName(FilterStage::Application));
}

TEST_F(FilterOrderValidatorTest, ClearMetadata) {
  validator_->clear();
  
  EXPECT_FALSE(validator_->hasFilter("rate_limit"));
  EXPECT_FALSE(validator_->hasFilter("http_codec"));
  
  // After clear, unknown filters should still work (default to Application)
  std::vector<FilterConfig> filters = {
    createFilter("any_filter")
  };
  
  EXPECT_TRUE(validator_->validate(filters));
}