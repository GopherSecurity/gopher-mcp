#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "mcp/json/json_bridge.h"
#include "mcp/logging/log_macros.h"

// Forward declarations for merger and validator
namespace mcp {
namespace config {

class ConfigMerger;
std::unique_ptr<ConfigMerger> createConfigMerger();

class Validator;
class RangeValidator;
class CompositeValidator;

std::unique_ptr<Validator> createSchemaValidator(
    const std::string& name,
    const mcp::json::JsonValue& schema);
std::unique_ptr<RangeValidator> createRangeValidator(const std::string& name);
std::unique_ptr<CompositeValidator> createCompositeValidator(
    const std::string& name);

}  // namespace config
}  // namespace mcp

namespace mcp {
namespace config {
namespace test {

using json::JsonValue;

class ConfigMergerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    merger_ = createConfigMerger();
  }
  
  std::unique_ptr<ConfigMerger> merger_;
};

// Test deep merge for nested objects
TEST_F(ConfigMergerTest, DeepMergeNestedObjects) {
  JsonValue base = JsonValue::object();
  base["server"]["host"] = "localhost";
  base["server"]["port"] = 8080;
  base["database"]["host"] = "db.local";
  base["database"]["port"] = 5432;
  
  JsonValue overlay = JsonValue::object();
  overlay["server"]["port"] = 9090;
  overlay["server"]["ssl"] = true;
  overlay["database"]["pool_size"] = 10;
  
  std::vector<std::pair<std::string, JsonValue>> sources = {
    {"base", base},
    {"overlay", overlay}
  };
  
  auto result = merger_->merge(sources, "test-snapshot", "v1.0");
  
  // Check merged values
  EXPECT_EQ(result["server"]["host"].getString(), "localhost");  // Preserved
  EXPECT_EQ(result["server"]["port"].getInt(), 9090);  // Overridden
  EXPECT_EQ(result["server"]["ssl"].getBool(), true);  // Added
  EXPECT_EQ(result["database"]["host"].getString(), "db.local");  // Preserved
  EXPECT_EQ(result["database"]["port"].getInt(), 5432);  // Preserved
  EXPECT_EQ(result["database"]["pool_size"].getInt(), 10);  // Added
}

// Test filter list replacement
TEST_F(ConfigMergerTest, FilterListReplacement) {
  JsonValue base = JsonValue::object();
  JsonValue base_filters = JsonValue::array();
  base_filters.push_back("filter1");
  base_filters.push_back("filter2");
  base["filters"] = base_filters;
  
  JsonValue overlay = JsonValue::object();
  JsonValue overlay_filters = JsonValue::array();
  overlay_filters.push_back("filter3");
  overlay_filters.push_back("filter4");
  overlay_filters.push_back("filter5");
  overlay["filters"] = overlay_filters;
  
  std::vector<std::pair<std::string, JsonValue>> sources = {
    {"base", base},
    {"overlay", overlay}
  };
  
  auto result = merger_->merge(sources);
  
  // Filter list should be completely replaced
  ASSERT_EQ(result["filters"].size(), 3);
  EXPECT_EQ(result["filters"][0].getString(), "filter3");
  EXPECT_EQ(result["filters"][1].getString(), "filter4");
  EXPECT_EQ(result["filters"][2].getString(), "filter5");
}

// Test named resource merging by name
TEST_F(ConfigMergerTest, NamedResourceMergeByName) {
  JsonValue base = JsonValue::object();
  JsonValue base_listeners = JsonValue::array();
  
  JsonValue listener1 = JsonValue::object();
  listener1["name"] = "http";
  listener1["port"] = 80;
  listener1["protocol"] = "HTTP";
  base_listeners.push_back(listener1);
  
  JsonValue listener2 = JsonValue::object();
  listener2["name"] = "https";
  listener2["port"] = 443;
  listener2["protocol"] = "HTTPS";
  base_listeners.push_back(listener2);
  
  base["listeners"] = base_listeners;
  
  JsonValue overlay = JsonValue::object();
  JsonValue overlay_listeners = JsonValue::array();
  
  // Override existing listener
  JsonValue listener1_override = JsonValue::object();
  listener1_override["name"] = "http";
  listener1_override["port"] = 8080;  // Changed port
  listener1_override["timeout"] = 30;  // New field
  overlay_listeners.push_back(listener1_override);
  
  // Add new listener
  JsonValue listener3 = JsonValue::object();
  listener3["name"] = "grpc";
  listener3["port"] = 9000;
  listener3["protocol"] = "GRPC";
  overlay_listeners.push_back(listener3);
  
  overlay["listeners"] = overlay_listeners;
  
  std::vector<std::pair<std::string, JsonValue>> sources = {
    {"base", base},
    {"overlay", overlay}
  };
  
  auto result = merger_->merge(sources);
  
  // Should have all three listeners
  ASSERT_EQ(result["listeners"].size(), 3);
  
  // Check merged http listener
  bool found_http = false;
  for (size_t i = 0; i < result["listeners"].size(); ++i) {
    if (result["listeners"][i]["name"].getString() == "http") {
      EXPECT_EQ(result["listeners"][i]["port"].getInt(), 8080);  // Overridden
      EXPECT_EQ(result["listeners"][i]["protocol"].getString(), "HTTP");  // Preserved
      EXPECT_EQ(result["listeners"][i]["timeout"].getInt(), 30);  // Added
      found_http = true;
    }
  }
  EXPECT_TRUE(found_http);
  
  // Check unchanged https listener
  bool found_https = false;
  for (size_t i = 0; i < result["listeners"].size(); ++i) {
    if (result["listeners"][i]["name"].getString() == "https") {
      EXPECT_EQ(result["listeners"][i]["port"].getInt(), 443);
      EXPECT_EQ(result["listeners"][i]["protocol"].getString(), "HTTPS");
      found_https = true;
    }
  }
  EXPECT_TRUE(found_https);
  
  // Check new grpc listener
  bool found_grpc = false;
  for (size_t i = 0; i < result["listeners"].size(); ++i) {
    if (result["listeners"][i]["name"].getString() == "grpc") {
      EXPECT_EQ(result["listeners"][i]["port"].getInt(), 9000);
      EXPECT_EQ(result["listeners"][i]["protocol"].getString(), "GRPC");
      found_grpc = true;
    }
  }
  EXPECT_TRUE(found_grpc);
}

// Test precedence layering
TEST_F(ConfigMergerTest, PrecedenceLayering) {
  JsonValue bootstrap = JsonValue::object();
  bootstrap["setting"] = "bootstrap";
  bootstrap["level"] = 1;
  
  JsonValue config_d = JsonValue::object();
  config_d["setting"] = "config.d";
  config_d["level"] = 2;
  config_d["extra"] = "from_overlay";
  
  JsonValue cli_env = JsonValue::object();
  cli_env["setting"] = "cli";
  cli_env["level"] = 3;
  
  // Order: bootstrap < config.d < CLI/env
  std::vector<std::pair<std::string, JsonValue>> sources = {
    {"bootstrap", bootstrap},
    {"config.d", config_d},
    {"cli", cli_env}
  };
  
  auto result = merger_->merge(sources);
  
  // CLI should win for overlapping fields
  EXPECT_EQ(result["setting"].getString(), "cli");
  EXPECT_EQ(result["level"].getInt(), 3);
  // Extra field from config.d should be preserved
  EXPECT_EQ(result["extra"].getString(), "from_overlay");
}

class ConfigValidatorTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

// Test schema validation
TEST_F(ConfigValidatorTest, SchemaValidation) {
  // Create a simple schema
  JsonValue schema = JsonValue::object();
  schema["type"] = "object";
  
  JsonValue properties = JsonValue::object();
  properties["host"]["type"] = "string";
  properties["port"]["type"] = "number";
  properties["port"]["minimum"] = 1;
  properties["port"]["maximum"] = 65535;
  
  schema["properties"] = properties;
  schema["required"] = JsonValue::array();
  schema["required"].push_back("host");
  schema["required"].push_back("port");
  
  auto validator = createSchemaValidator("test-schema", schema);
  
  // Valid config
  {
    JsonValue config = JsonValue::object();
    config["host"] = "localhost";
    config["port"] = 8080;
    
    auto result = validator->validate(config, Validator::ValidationMode::STRICT);
    EXPECT_TRUE(result.is_valid);
    EXPECT_EQ(result.getErrorCount(), 0);
  }
  
  // Invalid config - missing required field
  {
    JsonValue config = JsonValue::object();
    config["host"] = "localhost";
    
    auto result = validator->validate(config, Validator::ValidationMode::STRICT);
    EXPECT_FALSE(result.is_valid);
    EXPECT_GT(result.getErrorCount(), 0);
  }
  
  // Invalid config - wrong type
  {
    JsonValue config = JsonValue::object();
    config["host"] = "localhost";
    config["port"] = "not-a-number";
    
    auto result = validator->validate(config, Validator::ValidationMode::STRICT);
    EXPECT_FALSE(result.is_valid);
    EXPECT_GT(result.getErrorCount(), 0);
  }
}

// Test range validator
TEST_F(ConfigValidatorTest, RangeValidation) {
  auto validator = createRangeValidator("port-validator");
  
  RangeValidator::RangeRule port_rule;
  port_rule.path = "server.port";
  port_rule.min = 1;
  port_rule.max = 65535;
  port_rule.min_inclusive = true;
  port_rule.max_inclusive = true;
  validator->addRule(port_rule);
  
  RangeValidator::RangeRule timeout_rule;
  timeout_rule.path = "timeout";
  timeout_rule.min = 0;
  timeout_rule.max = 3600;
  validator->addRule(timeout_rule);
  
  // Valid config
  {
    JsonValue config = JsonValue::object();
    config["server"]["port"] = 8080;
    config["timeout"] = 30;
    
    auto result = validator->validate(config, Validator::ValidationMode::STRICT);
    EXPECT_TRUE(result.is_valid);
    EXPECT_EQ(result.getErrorCount(), 0);
  }
  
  // Invalid - port out of range
  {
    JsonValue config = JsonValue::object();
    config["server"]["port"] = 70000;
    config["timeout"] = 30;
    
    auto result = validator->validate(config, Validator::ValidationMode::STRICT);
    EXPECT_FALSE(result.is_valid);
    EXPECT_GT(result.getErrorCount(), 0);
  }
  
  // Invalid - negative timeout
  {
    JsonValue config = JsonValue::object();
    config["server"]["port"] = 8080;
    config["timeout"] = -5;
    
    auto result = validator->validate(config, Validator::ValidationMode::STRICT);
    EXPECT_FALSE(result.is_valid);
    EXPECT_GT(result.getErrorCount(), 0);
  }
}

// Test validation modes
TEST_F(ConfigValidatorTest, ValidationModes) {
  // Create schema that doesn't include "extra" field
  JsonValue schema = JsonValue::object();
  schema["type"] = "object";
  
  JsonValue properties = JsonValue::object();
  properties["known"]["type"] = "string";
  schema["properties"] = properties;
  
  auto validator = createSchemaValidator("mode-test", schema);
  
  JsonValue config = JsonValue::object();
  config["known"] = "value";
  config["unknown"] = "extra";
  
  // Strict mode - unknown fields are errors
  {
    auto result = validator->validate(config, Validator::ValidationMode::STRICT);
    EXPECT_FALSE(result.is_valid);
    EXPECT_GT(result.getErrorCount(), 0);
    EXPECT_TRUE(result.unknown_fields.find("unknown") != result.unknown_fields.end());
  }
  
  // Warn mode - unknown fields are warnings
  {
    auto result = validator->validate(config, Validator::ValidationMode::WARN);
    EXPECT_TRUE(result.is_valid);  // Still valid, just warnings
    EXPECT_EQ(result.getErrorCount(), 0);
    EXPECT_GT(result.getWarningCount(), 0);
    EXPECT_TRUE(result.unknown_fields.find("unknown") != result.unknown_fields.end());
  }
  
  // Permissive mode - unknown fields are ignored
  {
    auto result = validator->validate(config, Validator::ValidationMode::PERMISSIVE);
    EXPECT_TRUE(result.is_valid);
    EXPECT_EQ(result.getErrorCount(), 0);
    EXPECT_EQ(result.getWarningCount(), 0);
    EXPECT_TRUE(result.unknown_fields.empty());
  }
}

// Test composite validator
TEST_F(ConfigValidatorTest, CompositeValidation) {
  auto composite = createCompositeValidator("full-validation");
  
  // Add schema validator
  JsonValue schema = JsonValue::object();
  schema["type"] = "object";
  JsonValue properties = JsonValue::object();
  properties["server"]["type"] = "object";
  schema["properties"] = properties;
  composite->addValidator(createSchemaValidator("schema", schema));
  
  // Add range validator
  auto range = createRangeValidator("ranges");
  RangeValidator::RangeRule rule;
  rule.path = "server.port";
  rule.min = 1024;
  rule.max = 65535;
  range->addRule(rule);
  composite->addValidator(std::move(range));
  
  // Valid config
  {
    JsonValue config = JsonValue::object();
    config["server"]["port"] = 8080;
    
    auto result = composite->validate(config, Validator::ValidationMode::STRICT);
    EXPECT_TRUE(result.is_valid);
    EXPECT_EQ(result.getErrorCount(), 0);
  }
  
  // Invalid config - fails range validation
  {
    JsonValue config = JsonValue::object();
    config["server"]["port"] = 80;  // Below minimum
    
    auto result = composite->validate(config, Validator::ValidationMode::STRICT);
    EXPECT_FALSE(result.is_valid);
    EXPECT_GT(result.getErrorCount(), 0);
    EXPECT_TRUE(result.failing_categories.find("server") != 
                result.failing_categories.end());
  }
}

}  // namespace test
}  // namespace config
}  // namespace mcp