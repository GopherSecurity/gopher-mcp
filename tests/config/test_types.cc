/**
 * @file test_types.cc
 * @brief Unit tests for configuration data models
 */

#include <gtest/gtest.h>

#include "mcp/config/types.h"
#include "mcp/json/json_bridge.h"

using namespace mcp::config;
using mcp::json::JsonValue;

class ConfigTypesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Setup code if needed
  }

  void TearDown() override {
    // Cleanup code if needed
  }
};

// ============================================================================
// NodeConfig Tests
// ============================================================================

TEST_F(ConfigTypesTest, NodeConfigDefaults) {
  NodeConfig config;

  EXPECT_EQ(config.id, "gopher-mcp-node-1");
  EXPECT_EQ(config.cluster, "default");
  EXPECT_TRUE(config.metadata.empty());
  EXPECT_TRUE(config.region.empty());
  EXPECT_TRUE(config.zone.empty());
}

TEST_F(ConfigTypesTest, NodeConfigValidation_ValidConfig) {
  NodeConfig config;
  config.id = "test-node-123";
  config.cluster = "production";
  config.metadata["env"] = "prod";
  config.metadata["version"] = "1.2.3";

  EXPECT_NO_THROW(config.validate());
}

TEST_F(ConfigTypesTest, NodeConfigValidation_EmptyId) {
  NodeConfig config;
  config.id = "";

  try {
    config.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    EXPECT_EQ(e.field(), "node.id");
    EXPECT_NE(std::string(e.what()).find("cannot be empty"), std::string::npos);
  }
}

TEST_F(ConfigTypesTest, NodeConfigValidation_InvalidIdCharacters) {
  NodeConfig config;
  config.id = "node@#$%";

  try {
    config.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    EXPECT_EQ(e.field(), "node.id");
    EXPECT_NE(std::string(e.what()).find("alphanumeric"), std::string::npos);
  }
}

TEST_F(ConfigTypesTest, NodeConfigValidation_IdTooLong) {
  NodeConfig config;
  config.id = std::string(257, 'a');

  try {
    config.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    EXPECT_EQ(e.field(), "node.id");
    EXPECT_NE(std::string(e.what()).find("exceed 256"), std::string::npos);
  }
}

TEST_F(ConfigTypesTest, NodeConfigValidation_EmptyCluster) {
  NodeConfig config;
  config.cluster = "";

  try {
    config.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    EXPECT_EQ(e.field(), "node.cluster");
    EXPECT_NE(std::string(e.what()).find("cannot be empty"), std::string::npos);
  }
}

TEST_F(ConfigTypesTest, NodeConfigValidation_MetadataKeyTooLong) {
  NodeConfig config;
  config.metadata[std::string(129, 'k')] = "value";

  try {
    config.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    EXPECT_EQ(e.field(), "node.metadata");
    EXPECT_NE(std::string(e.what()).find("exceeds maximum length"),
              std::string::npos);
  }
}

TEST_F(ConfigTypesTest, NodeConfigJsonRoundTrip) {
  NodeConfig original;
  original.id = "test-node";
  original.cluster = "test-cluster";
  original.metadata["key1"] = "value1";
  original.metadata["key2"] = "value2";
  original.region = "us-west-2";
  original.zone = "us-west-2a";

  // Serialize to JSON
  JsonValue j = original.toJson();

  // Deserialize from JSON
  NodeConfig deserialized = NodeConfig::fromJson(j);

  // Verify round-trip
  EXPECT_EQ(original.id, deserialized.id);
  EXPECT_EQ(original.cluster, deserialized.cluster);
  EXPECT_EQ(original.metadata, deserialized.metadata);
  EXPECT_EQ(original.region, deserialized.region);
  EXPECT_EQ(original.zone, deserialized.zone);
}

// ============================================================================
// AdminConfig Tests
// ============================================================================

TEST_F(ConfigTypesTest, AdminConfigDefaults) {
  AdminConfig config;

  EXPECT_EQ(config.address, "127.0.0.1");
  EXPECT_EQ(config.port, 9901);
  EXPECT_EQ(config.allowed_ips, (std::vector<std::string>{"127.0.0.1", "::1"}));
  EXPECT_TRUE(config.enabled);
  EXPECT_EQ(config.path_prefix, "/admin");
  EXPECT_FALSE(config.enable_cors);
  EXPECT_EQ(config.cors_origins, (std::vector<std::string>{"*"}));
}

TEST_F(ConfigTypesTest, AdminConfigValidation_ValidConfig) {
  AdminConfig config;
  config.address = "0.0.0.0";
  config.port = 8080;
  config.allowed_ips = {"10.0.0.0/8", "192.168.0.0/16"};

  EXPECT_NO_THROW(config.validate());
}

TEST_F(ConfigTypesTest, AdminConfigValidation_DisabledSkipsValidation) {
  AdminConfig config;
  config.enabled = false;
  config.address = "";  // Would normally fail validation
  config.port = 0;      // Would normally fail validation

  EXPECT_NO_THROW(config.validate());
}

TEST_F(ConfigTypesTest, AdminConfigValidation_EmptyAddress) {
  AdminConfig config;
  config.address = "";

  try {
    config.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    EXPECT_EQ(e.field(), "admin.address");
    EXPECT_NE(std::string(e.what()).find("cannot be empty"), std::string::npos);
  }
}

TEST_F(ConfigTypesTest, AdminConfigValidation_InvalidPort) {
  AdminConfig config;
  config.port = 0;

  try {
    config.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    EXPECT_EQ(e.field(), "admin.port");
    EXPECT_NE(std::string(e.what()).find("cannot be 0"), std::string::npos);
  }
}

TEST_F(ConfigTypesTest, AdminConfigValidation_EmptyAllowedIps) {
  AdminConfig config;
  config.allowed_ips.clear();

  try {
    config.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    EXPECT_EQ(e.field(), "admin.allowed_ips");
    EXPECT_NE(std::string(e.what()).find("At least one"), std::string::npos);
  }
}

TEST_F(ConfigTypesTest, AdminConfigValidation_InvalidCidr) {
  AdminConfig config;
  config.allowed_ips = {"10.0.0.0/999"};

  try {
    config.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    EXPECT_EQ(e.field(), "admin.allowed_ips");
    EXPECT_NE(std::string(e.what()).find("Invalid CIDR"), std::string::npos);
  }
}

TEST_F(ConfigTypesTest, AdminConfigValidation_InvalidPathPrefix) {
  AdminConfig config;
  config.path_prefix = "admin";  // Missing leading slash

  try {
    config.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    EXPECT_EQ(e.field(), "admin.path_prefix");
    EXPECT_NE(std::string(e.what()).find("must start with '/'"),
              std::string::npos);
  }
}

TEST_F(ConfigTypesTest, AdminConfigValidation_CorsWithoutOrigins) {
  AdminConfig config;
  config.enable_cors = true;
  config.cors_origins.clear();

  try {
    config.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    EXPECT_EQ(e.field(), "admin.cors_origins");
    EXPECT_NE(std::string(e.what()).find("must be specified"),
              std::string::npos);
  }
}

TEST_F(ConfigTypesTest, AdminConfigJsonRoundTrip) {
  AdminConfig original;
  original.address = "0.0.0.0";
  original.port = 8888;
  original.allowed_ips = {"10.0.0.0/8", "192.168.0.0/16", "172.16.0.0/12"};
  original.enabled = true;
  original.path_prefix = "/api/admin";
  original.enable_cors = true;
  original.cors_origins = {"http://localhost:3000",
                           "https://admin.example.com"};

  // Serialize to JSON
  JsonValue j = original.toJson();

  // Deserialize from JSON
  AdminConfig deserialized = AdminConfig::fromJson(j);

  // Verify round-trip
  EXPECT_EQ(original.address, deserialized.address);
  EXPECT_EQ(original.port, deserialized.port);
  EXPECT_EQ(original.allowed_ips, deserialized.allowed_ips);
  EXPECT_EQ(original.enabled, deserialized.enabled);
  EXPECT_EQ(original.path_prefix, deserialized.path_prefix);
  EXPECT_EQ(original.enable_cors, deserialized.enable_cors);
  EXPECT_EQ(original.cors_origins, deserialized.cors_origins);
}

// ============================================================================
// BootstrapConfig Tests
// ============================================================================

TEST_F(ConfigTypesTest, BootstrapConfigDefaults) {
  BootstrapConfig config;

  EXPECT_EQ(config.version, "1.0");
  EXPECT_TRUE(config.config_path.empty());
  EXPECT_EQ(config.node.id, "gopher-mcp-node-1");
  EXPECT_EQ(config.admin.address, "127.0.0.1");
}

TEST_F(ConfigTypesTest, BootstrapConfigValidation_ValidConfig) {
  BootstrapConfig config;
  config.version = "1.2.3";
  config.node.id = "my-node";
  config.node.cluster = "production";
  config.admin.port = 9000;

  EXPECT_NO_THROW(config.validate());
}

TEST_F(ConfigTypesTest, BootstrapConfigValidation_InvalidVersion) {
  BootstrapConfig config;

  // Test empty version
  config.version = "";
  try {
    config.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    EXPECT_EQ(e.field(), "version");
  }

  // Test invalid version format
  config.version = "1.2.3.4";  // Too many dots
  try {
    config.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    EXPECT_EQ(e.field(), "version");
    EXPECT_NE(std::string(e.what()).find("Invalid version format"),
              std::string::npos);
  }

  // Test version with non-numeric
  config.version = "1.2a";
  try {
    config.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    EXPECT_EQ(e.field(), "version");
  }
}

TEST_F(ConfigTypesTest, BootstrapConfigValidation_NestedValidation) {
  BootstrapConfig config;
  config.node.id = "";  // Invalid node config

  try {
    config.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    EXPECT_EQ(e.field(), "node.id");
  }
}

TEST_F(ConfigTypesTest, BootstrapConfigJsonRoundTrip) {
  BootstrapConfig original = BootstrapConfig::createDefault();
  original.version = "2.0";
  original.config_path = "/etc/mcp/config.yaml";
  original.node.id = "prod-node-1";
  original.node.cluster = "production";
  original.node.metadata["environment"] = "production";
  original.node.region = "us-east-1";
  original.admin.address = "0.0.0.0";
  original.admin.port = 9999;
  original.admin.enable_cors = true;

  // Serialize to JSON
  JsonValue j = original.toJson();

  // Deserialize from JSON
  BootstrapConfig deserialized = BootstrapConfig::fromJson(j);

  // Verify round-trip (excluding loaded_at timestamp)
  EXPECT_EQ(original.version, deserialized.version);
  EXPECT_EQ(original.node.id, deserialized.node.id);
  EXPECT_EQ(original.node.cluster, deserialized.node.cluster);
  EXPECT_EQ(original.node.metadata, deserialized.node.metadata);
  EXPECT_EQ(original.node.region, deserialized.node.region);
  EXPECT_EQ(original.admin.address, deserialized.admin.address);
  EXPECT_EQ(original.admin.port, deserialized.admin.port);
  EXPECT_EQ(original.admin.enable_cors, deserialized.admin.enable_cors);
}

TEST_F(ConfigTypesTest, BootstrapConfigMerge) {
  BootstrapConfig base;
  base.node.id = "base-node";
  base.node.cluster = "base-cluster";
  base.node.metadata["key1"] = "value1";
  base.admin.port = 8000;

  BootstrapConfig overlay;
  overlay.node.id = "overlay-node";
  overlay.node.metadata["key2"] = "value2";
  overlay.node.region = "us-west-2";
  overlay.admin.port = 9000;
  overlay.admin.allowed_ips = {"192.168.0.0/16"};

  base.merge(overlay);

  // Check merged values
  EXPECT_EQ(base.node.id, "overlay-node");
  EXPECT_EQ(base.node.cluster, "base-cluster");  // Not overridden
  EXPECT_EQ(base.node.metadata.size(), 2);
  EXPECT_EQ(base.node.metadata["key1"], "value1");
  EXPECT_EQ(base.node.metadata["key2"], "value2");
  EXPECT_EQ(base.node.region, "us-west-2");
  EXPECT_EQ(base.admin.port, 9000);
  EXPECT_EQ(base.admin.allowed_ips,
            (std::vector<std::string>{"192.168.0.0/16"}));
}

TEST_F(ConfigTypesTest, BootstrapConfigEquality) {
  BootstrapConfig config1;
  config1.node.id = "test-node";
  config1.admin.port = 8080;

  BootstrapConfig config2;
  config2.node.id = "test-node";
  config2.admin.port = 8080;

  BootstrapConfig config3;
  config3.node.id = "different-node";
  config3.admin.port = 8080;

  EXPECT_EQ(config1, config2);
  EXPECT_NE(config1, config3);
}

TEST_F(ConfigTypesTest, JsonParsingWithMissingFields) {
  // Test that missing fields use defaults
  JsonValue minimal = JsonValue::parse("{\"node\": {\"id\": \"minimal-node\"}}");

  BootstrapConfig config = BootstrapConfig::fromJson(minimal);

  EXPECT_EQ(config.node.id, "minimal-node");
  EXPECT_EQ(config.node.cluster, "default");     // Default value
  EXPECT_EQ(config.admin.address, "127.0.0.1");  // Default value
  EXPECT_EQ(config.admin.port, 9901);            // Default value
}

TEST_F(ConfigTypesTest, JsonParsingWithNullValues) {
  // Test that null values are handled correctly
  JsonValue with_nulls = JsonValue::parse(R"({
    "version": "1.0",
    "node": {
      "id": "test-node",
      "cluster": null,
      "region": null
    },
    "admin": {
      "address": null,
      "port": 8080
    }
  })");

  BootstrapConfig config = BootstrapConfig::fromJson(with_nulls);

  EXPECT_EQ(config.node.id, "test-node");
  EXPECT_EQ(config.node.cluster, "default");     // Default used for null
  EXPECT_TRUE(config.node.region.empty());       // Empty for null
  EXPECT_EQ(config.admin.address, "127.0.0.1");  // Default used for null
  EXPECT_EQ(config.admin.port, 8080);
}

TEST_F(ConfigTypesTest, ComplexValidationScenario) {
  // Test a complex real-world configuration
  JsonValue complex_config = JsonValue::parse(R"({
    "version": "2.1",
    "node": {
      "id": "prod-mcp-node-001",
      "cluster": "production-us-west",
      "metadata": {
        "environment": "production",
        "datacenter": "dc1",
        "rack": "r12",
        "service_version": "2.1.0"
      },
      "region": "us-west-2",
      "zone": "us-west-2a"
    },
    "admin": {
      "address": "0.0.0.0",
      "port": 9901,
      "allowed_ips": ["10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "127.0.0.1"],
      "enabled": true,
      "path_prefix": "/internal/admin",
      "enable_cors": true,
      "cors_origins": ["https://admin.example.com", "https://monitoring.example.com"]
    }
  })");

  BootstrapConfig config = BootstrapConfig::fromJson(complex_config);

  // Should validate successfully
  EXPECT_NO_THROW(config.validate());

  // Verify all fields were parsed correctly
  EXPECT_EQ(config.version, "2.1");
  EXPECT_EQ(config.node.id, "prod-mcp-node-001");
  EXPECT_EQ(config.node.metadata.size(), 4);
  EXPECT_EQ(config.node.metadata["environment"], "production");
  EXPECT_EQ(config.admin.allowed_ips.size(), 4);
  EXPECT_TRUE(config.admin.enable_cors);
  EXPECT_EQ(config.admin.cors_origins.size(), 2);
}

// ============================================================================
// Error Message Quality Tests
// ============================================================================

TEST_F(ConfigTypesTest, DetailedErrorMessages) {
  NodeConfig node;
  node.id = "node with spaces";

  try {
    node.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    std::string error_msg = e.what();
    // Check that error message contains useful information
    EXPECT_NE(error_msg.find("node.id"), std::string::npos);
    EXPECT_NE(error_msg.find("alphanumeric"), std::string::npos);
    EXPECT_NE(error_msg.find("dashes"), std::string::npos);
    EXPECT_NE(error_msg.find("underscores"), std::string::npos);
  }

  AdminConfig admin;
  admin.allowed_ips = {"10.0.0.0/abc"};

  try {
    admin.validate();
    FAIL() << "Expected ConfigValidationError";
  } catch (const ConfigValidationError& e) {
    std::string error_msg = e.what();
    // Check that error message identifies the problematic value
    EXPECT_NE(error_msg.find("10.0.0.0/abc"), std::string::npos);
    EXPECT_NE(error_msg.find("CIDR"), std::string::npos);
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}