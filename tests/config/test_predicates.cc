/**
 * @file test_predicates.cc
 * @brief Unit tests for predicate evaluation system
 */

#include <gtest/gtest.h>
#include <cstdlib>

#include "mcp/config/predicate_evaluator.h"
#include "mcp/json/json_bridge.h"

using namespace mcp::config;
using mcp::json::JsonValue;

class PredicateEvaluatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    evaluator_ = std::make_unique<PredicateEvaluator>();
    
    // Set up test environment variables
    setenv("TEST_ENV_VAR", "test_value", 1);
    setenv("DEBUG", "true", 1);
    setenv("ENABLE_FEATURE", "1", 1);
    
    // Set up test context
    context_.node_metadata["env"] = "production";
    context_.node_metadata["region"] = "us-west";
    context_.node_metadata["tier"] = "frontend";
    context_.transport_type = "http";
    context_.transport_name = "web-frontend";
    context_.route_path = "/api/v1/health";
  }
  
  void TearDown() override {
    // Clean up environment variables
    unsetenv("TEST_ENV_VAR");
    unsetenv("DEBUG");
    unsetenv("ENABLE_FEATURE");
  }
  
  std::unique_ptr<PredicateEvaluator> evaluator_;
  PredicateContext context_;
};

// ============================================================================
// Environment Variable Predicate Tests
// ============================================================================

TEST_F(PredicateEvaluatorTest, EnvVarExistence_Exists) {
  auto result = evaluator_->evaluateSingle("env:TEST_ENV_VAR", context_);
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.matched);
  EXPECT_TRUE(result.error_message.empty());
}

TEST_F(PredicateEvaluatorTest, EnvVarExistence_NotExists) {
  auto result = evaluator_->evaluateSingle("env:NONEXISTENT_VAR", context_);
  EXPECT_TRUE(result.success);
  EXPECT_FALSE(result.matched);
  EXPECT_TRUE(result.error_message.empty());
}

TEST_F(PredicateEvaluatorTest, EnvVarValue_Match) {
  auto result = evaluator_->evaluateSingle("env:TEST_ENV_VAR=test_value", context_);
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.matched);
}

TEST_F(PredicateEvaluatorTest, EnvVarValue_NoMatch) {
  auto result = evaluator_->evaluateSingle("env:TEST_ENV_VAR=wrong_value", context_);
  EXPECT_TRUE(result.success);
  EXPECT_FALSE(result.matched);
}

TEST_F(PredicateEvaluatorTest, EnvVarValue_BooleanTrue) {
  auto result = evaluator_->evaluateSingle("env:DEBUG=true", context_);
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.matched);
}

// ============================================================================
// Node Metadata Predicate Tests
// ============================================================================

TEST_F(PredicateEvaluatorTest, NodeMetadata_Exists) {
  auto result = evaluator_->evaluateSingle("node.metadata.env:", context_);
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.matched);
}

TEST_F(PredicateEvaluatorTest, NodeMetadata_NotExists) {
  auto result = evaluator_->evaluateSingle("node.metadata.nonexistent:", context_);
  EXPECT_TRUE(result.success);
  EXPECT_FALSE(result.matched);
}

TEST_F(PredicateEvaluatorTest, NodeMetadata_ValueMatch) {
  auto result = evaluator_->evaluateSingle("node.metadata.env=production", context_);
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.matched);
}

TEST_F(PredicateEvaluatorTest, NodeMetadata_ValueNoMatch) {
  auto result = evaluator_->evaluateSingle("node.metadata.env=development", context_);
  EXPECT_TRUE(result.success);
  EXPECT_FALSE(result.matched);
}

TEST_F(PredicateEvaluatorTest, NodeMetadata_MultipleKeys) {
  auto result1 = evaluator_->evaluateSingle("node.metadata.region=us-west", context_);
  auto result2 = evaluator_->evaluateSingle("node.metadata.tier=frontend", context_);
  
  EXPECT_TRUE(result1.success && result1.matched);
  EXPECT_TRUE(result2.success && result2.matched);
}

// ============================================================================
// Transport Predicate Tests
// ============================================================================

TEST_F(PredicateEvaluatorTest, TransportType_Match) {
  auto result = evaluator_->evaluateSingle("transport.type=http", context_);
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.matched);
}

TEST_F(PredicateEvaluatorTest, TransportType_NoMatch) {
  auto result = evaluator_->evaluateSingle("transport.type=tcp", context_);
  EXPECT_TRUE(result.success);
  EXPECT_FALSE(result.matched);
}

TEST_F(PredicateEvaluatorTest, TransportName_Match) {
  auto result = evaluator_->evaluateSingle("transport.name=web-frontend", context_);
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.matched);
}

TEST_F(PredicateEvaluatorTest, TransportName_NoMatch) {
  auto result = evaluator_->evaluateSingle("transport.name=api-backend", context_);
  EXPECT_TRUE(result.success);
  EXPECT_FALSE(result.matched);
}

// ============================================================================
// Route Predicate Tests
// ============================================================================

TEST_F(PredicateEvaluatorTest, RoutePath_ExactMatch) {
  auto result = evaluator_->evaluateSingle("route.path=/api/v1/health", context_);
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.matched);
}

TEST_F(PredicateEvaluatorTest, RoutePath_NoMatch) {
  auto result = evaluator_->evaluateSingle("route.path=/api/v2/health", context_);
  EXPECT_TRUE(result.success);
  EXPECT_FALSE(result.matched);
}

TEST_F(PredicateEvaluatorTest, RoutePathPrefix_Match) {
  auto result = evaluator_->evaluateSingle("route.path.prefix=/api", context_);
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.matched);
}

TEST_F(PredicateEvaluatorTest, RoutePathPrefix_NoMatch) {
  auto result = evaluator_->evaluateSingle("route.path.prefix=/admin", context_);
  EXPECT_TRUE(result.success);
  EXPECT_FALSE(result.matched);
}

// ============================================================================
// Complex Predicate Evaluation Tests
// ============================================================================

TEST_F(PredicateEvaluatorTest, MultiplePredicates_AllMatch) {
  auto predicates = JsonValue::object();
  predicates["env"] = JsonValue("DEBUG=true");
  predicates["node.metadata.env"] = JsonValue("production");
  predicates["transport.type"] = JsonValue("http");
  
  auto result = evaluator_->evaluate(predicates, context_);
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.matched);
}

TEST_F(PredicateEvaluatorTest, MultiplePredicates_SomeMatch) {
  auto predicates = JsonValue::object();
  predicates["env"] = JsonValue("DEBUG=true");
  predicates["node.metadata.env"] = JsonValue("development");  // Won't match
  predicates["transport.type"] = JsonValue("http");
  
  auto result = evaluator_->evaluate(predicates, context_);
  EXPECT_TRUE(result.success);
  EXPECT_FALSE(result.matched);  // All must match
}

TEST_F(PredicateEvaluatorTest, EmptyPredicates_AlwaysMatch) {
  auto predicates = JsonValue::object();
  
  auto result = evaluator_->evaluate(predicates, context_);
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.matched);
}

// ============================================================================
// Filter Enable/Disable Tests
// ============================================================================

TEST_F(PredicateEvaluatorTest, FilterEnabled_ExplicitTrue) {
  auto filter_config = JsonValue::object();
  filter_config["enabled"] = JsonValue(true);
  
  EXPECT_TRUE(evaluator_->isFilterEnabled(filter_config, context_));
}

TEST_F(PredicateEvaluatorTest, FilterEnabled_ExplicitFalse) {
  auto filter_config = JsonValue::object();
  filter_config["enabled"] = JsonValue(false);
  
  EXPECT_FALSE(evaluator_->isFilterEnabled(filter_config, context_));
}

TEST_F(PredicateEvaluatorTest, FilterEnabled_ConditionalMatch) {
  auto filter_config = JsonValue::object();
  auto enabled_when = JsonValue::object();
  enabled_when["env"] = JsonValue("DEBUG=true");
  filter_config["enabled_when"] = enabled_when;
  
  EXPECT_TRUE(evaluator_->isFilterEnabled(filter_config, context_));
}

TEST_F(PredicateEvaluatorTest, FilterEnabled_ConditionalNoMatch) {
  auto filter_config = JsonValue::object();
  auto enabled_when = JsonValue::object();
  enabled_when["env"] = JsonValue("DEBUG=false");
  filter_config["enabled_when"] = enabled_when;
  
  EXPECT_FALSE(evaluator_->isFilterEnabled(filter_config, context_));
}

TEST_F(PredicateEvaluatorTest, FilterEnabled_DefaultWhenNoConditions) {
  auto filter_config = JsonValue::object();
  
  EXPECT_TRUE(evaluator_->isFilterEnabled(filter_config, context_));
}

// ============================================================================
// Route Bypass Tests
// ============================================================================

TEST_F(PredicateEvaluatorTest, RouteBypass_ExplicitTrue) {
  auto route_config = JsonValue::object();
  route_config["bypass_filters"] = JsonValue(true);
  
  EXPECT_TRUE(evaluator_->shouldBypassFilters(route_config, context_));
}

TEST_F(PredicateEvaluatorTest, RouteBypass_ExplicitFalse) {
  auto route_config = JsonValue::object();
  route_config["bypass_filters"] = JsonValue(false);
  
  EXPECT_FALSE(evaluator_->shouldBypassFilters(route_config, context_));
}

TEST_F(PredicateEvaluatorTest, RouteBypass_ConditionalMatch) {
  auto route_config = JsonValue::object();
  auto bypass_predicates = JsonValue::object();
  bypass_predicates["route.path"] = JsonValue("/api/v1/health");
  route_config["bypass_filters"] = bypass_predicates;
  
  EXPECT_TRUE(evaluator_->shouldBypassFilters(route_config, context_));
}

TEST_F(PredicateEvaluatorTest, RouteBypass_ConditionalNoMatch) {
  auto route_config = JsonValue::object();
  auto bypass_predicates = JsonValue::object();
  bypass_predicates["route.path"] = JsonValue("/admin/status");
  route_config["bypass_filters"] = bypass_predicates;
  
  EXPECT_FALSE(evaluator_->shouldBypassFilters(route_config, context_));
}

TEST_F(PredicateEvaluatorTest, RouteBypass_DefaultWhenNotSpecified) {
  auto route_config = JsonValue::object();
  
  EXPECT_FALSE(evaluator_->shouldBypassFilters(route_config, context_));
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(PredicateEvaluatorTest, InvalidPredicateFormat) {
  auto result = evaluator_->evaluateSingle("invalid::predicate::format", context_);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.matched);
  EXPECT_FALSE(result.error_message.empty());
}

TEST_F(PredicateEvaluatorTest, UnknownPredicateType) {
  auto result = evaluator_->evaluateSingle("unknown.type=value", context_);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.matched);
  EXPECT_FALSE(result.error_message.empty());
}

TEST_F(PredicateEvaluatorTest, SafeDefaults_Enabled) {
  evaluator_->setSafeDefaults(true);
  
  auto filter_config = JsonValue::object();
  auto enabled_when = JsonValue::object();
  enabled_when["invalid.predicate"] = JsonValue("value");
  filter_config["enabled_when"] = enabled_when;
  
  // With safe defaults, invalid predicates result in disabled filter
  EXPECT_FALSE(evaluator_->isFilterEnabled(filter_config, context_));
}

TEST_F(PredicateEvaluatorTest, SafeDefaults_Disabled) {
  evaluator_->setSafeDefaults(false);
  
  auto filter_config = JsonValue::object();
  auto enabled_when = JsonValue::object();
  enabled_when["env"] = JsonValue("DEBUG=true");
  filter_config["enabled_when"] = enabled_when;
  
  // Without safe defaults, valid predicates still work
  EXPECT_TRUE(evaluator_->isFilterEnabled(filter_config, context_));
}

// ============================================================================
// Validation Tests
// ============================================================================

TEST_F(PredicateEvaluatorTest, ValidatePredicates_Valid) {
  auto predicates = JsonValue::object();
  predicates["env"] = JsonValue("DEBUG=true");
  predicates["node.metadata.env"] = JsonValue("production");
  predicates["transport.type"] = JsonValue("http");
  predicates["route.path"] = JsonValue("/api/v1/health");
  
  auto errors = evaluator_->validatePredicates(predicates);
  EXPECT_TRUE(errors.empty());
}

TEST_F(PredicateEvaluatorTest, ValidatePredicates_InvalidType) {
  auto predicates = JsonValue::object();
  predicates["invalid.type"] = JsonValue("value");
  
  auto errors = evaluator_->validatePredicates(predicates);
  EXPECT_FALSE(errors.empty());
  EXPECT_EQ(errors[0], "Unknown predicate type: invalid.type");
}

TEST_F(PredicateEvaluatorTest, ValidatePredicates_EmptyKey) {
  auto predicates = JsonValue::object();
  predicates[""] = JsonValue("value");
  
  auto errors = evaluator_->validatePredicates(predicates);
  EXPECT_FALSE(errors.empty());
  EXPECT_EQ(errors[0], "Empty predicate key");
}

TEST_F(PredicateEvaluatorTest, ValidatePredicates_InvalidValueType) {
  auto predicates = JsonValue::object();
  predicates["env"] = JsonValue::array();  // Arrays not allowed
  
  auto errors = evaluator_->validatePredicates(predicates);
  EXPECT_FALSE(errors.empty());
}

// ============================================================================
// PredicateContext Tests
// ============================================================================

TEST_F(PredicateEvaluatorTest, PredicateContext_FromJson) {
  auto json = JsonValue::object();
  auto metadata = JsonValue::object();
  metadata["env"] = JsonValue("test");
  metadata["region"] = JsonValue("us-east");
  
  json["node_metadata"] = metadata;
  json["transport_type"] = JsonValue("tcp");
  json["transport_name"] = JsonValue("backend");
  json["route_path"] = JsonValue("/health");
  
  auto ctx = PredicateContext::fromJson(json);
  
  EXPECT_EQ(ctx.node_metadata["env"], "test");
  EXPECT_EQ(ctx.node_metadata["region"], "us-east");
  EXPECT_EQ(ctx.transport_type, "tcp");
  EXPECT_EQ(ctx.transport_name, "backend");
  EXPECT_EQ(ctx.route_path, "/health");
}

TEST_F(PredicateEvaluatorTest, PredicateContext_ToJson) {
  PredicateContext ctx;
  ctx.node_metadata["env"] = "staging";
  ctx.transport_type = "https";
  ctx.transport_name = "secure-api";
  ctx.route_path = "/api/secure";
  
  auto json = ctx.toJson();
  
  EXPECT_TRUE(json.contains("node_metadata"));
  EXPECT_EQ(json["node_metadata"]["env"].getString(), "staging");
  EXPECT_EQ(json["transport_type"].getString(), "https");
  EXPECT_EQ(json["transport_name"].getString(), "secure-api");
  EXPECT_EQ(json["route_path"].getString(), "/api/secure");
}

// ============================================================================
// Global Instance Test
// ============================================================================

TEST_F(PredicateEvaluatorTest, GlobalInstance_Singleton) {
  auto& instance1 = GlobalPredicateEvaluator::instance();
  auto& instance2 = GlobalPredicateEvaluator::instance();
  
  // Both references should point to the same instance
  EXPECT_EQ(&instance1, &instance2);
}

// ============================================================================
// Edge Cases and Complex Scenarios
// ============================================================================

TEST_F(PredicateEvaluatorTest, ComplexScenario_ProductionFiltering) {
  // Scenario: Enable debug filter only in production with DEBUG env var
  auto filter_config = JsonValue::object();
  auto enabled_when = JsonValue::object();
  enabled_when["env"] = JsonValue("DEBUG=true");
  enabled_when["node.metadata.env"] = JsonValue("production");
  filter_config["enabled_when"] = enabled_when;
  
  // Should be enabled in our test context (production + DEBUG=true)
  EXPECT_TRUE(evaluator_->isFilterEnabled(filter_config, context_));
  
  // Change context to development
  context_.node_metadata["env"] = "development";
  EXPECT_FALSE(evaluator_->isFilterEnabled(filter_config, context_));
}

TEST_F(PredicateEvaluatorTest, ComplexScenario_HealthCheckBypass) {
  // Scenario: Bypass filters for health check endpoints
  auto route_config = JsonValue::object();
  auto bypass_predicates = JsonValue::object();
  bypass_predicates["route.path.prefix"] = JsonValue("/api/v1/health");
  route_config["bypass_filters"] = bypass_predicates;
  
  // Should bypass for health check
  EXPECT_TRUE(evaluator_->shouldBypassFilters(route_config, context_));
  
  // Should not bypass for other endpoints
  context_.route_path = "/api/v1/users";
  EXPECT_FALSE(evaluator_->shouldBypassFilters(route_config, context_));
}

TEST_F(PredicateEvaluatorTest, ComplexScenario_TransportSpecificFilters) {
  // Scenario: Enable SSL validation only for HTTPS transport
  auto filter_config = JsonValue::object();
  auto enabled_when = JsonValue::object();
  enabled_when["transport.type"] = JsonValue("https");
  filter_config["enabled_when"] = enabled_when;
  
  // Should not be enabled for HTTP
  context_.transport_type = "http";
  EXPECT_FALSE(evaluator_->isFilterEnabled(filter_config, context_));
  
  // Should be enabled for HTTPS
  context_.transport_type = "https";
  EXPECT_TRUE(evaluator_->isFilterEnabled(filter_config, context_));
}