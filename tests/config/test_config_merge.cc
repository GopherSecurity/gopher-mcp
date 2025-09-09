/**
 * @file test_config_merge.cc
 * @brief Unit tests for proper configuration merge semantics
 */

#include <gtest/gtest.h>

#include "mcp/config/config_types.h"

using namespace mcp::config;

class ConfigMergeTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(ConfigMergeTest, ConfigFieldBasics) {
  ConfigField<int> field(42);  // Default is 42

  // Initially not set, should return default
  EXPECT_FALSE(field.is_set());
  EXPECT_EQ(field.get(), 42);

  // Set a value
  field.set(100);
  EXPECT_TRUE(field.is_set());
  EXPECT_EQ(field.get(), 100);

  // Reset to unset
  field.reset();
  EXPECT_FALSE(field.is_set());
  EXPECT_EQ(field.get(), 42);

  // Assignment operator
  field = 200;
  EXPECT_TRUE(field.is_set());
  EXPECT_EQ(field.get(), 200);
}

TEST_F(ConfigMergeTest, ConfigFieldMerge) {
  ConfigField<std::string> base("default");
  ConfigField<std::string> overlay("default");

  // Neither field is set
  base.merge(overlay);
  EXPECT_FALSE(base.is_set());
  EXPECT_EQ(base.get(), "default");

  // Overlay is set to default value (should still override)
  overlay.set("default");
  base.merge(overlay);
  EXPECT_TRUE(base.is_set());
  EXPECT_EQ(base.get(), "default");

  // Reset for next test
  base.reset();
  overlay.reset();

  // Base has value, overlay doesn't
  base.set("base_value");
  base.merge(overlay);
  EXPECT_TRUE(base.is_set());
  EXPECT_EQ(base.get(), "base_value");

  // Both have values (overlay should win)
  overlay.set("overlay_value");
  base.merge(overlay);
  EXPECT_TRUE(base.is_set());
  EXPECT_EQ(base.get(), "overlay_value");
}

TEST_F(ConfigMergeTest, NodeConfigMergeDefaultValues) {
  NodeConfig base;
  NodeConfig overlay;

  // Overlay explicitly sets field to default value
  overlay.id.set("gopher-mcp-node-1");  // Same as default
  overlay.cluster.set("default");       // Same as default

  // Merge should apply these values even though they match defaults
  base.merge(overlay);

  EXPECT_TRUE(base.id.is_set());
  EXPECT_TRUE(base.cluster.is_set());
  EXPECT_EQ(base.id.get(), "gopher-mcp-node-1");
  EXPECT_EQ(base.cluster.get(), "default");
}

TEST_F(ConfigMergeTest, NodeConfigMergePartialOverlay) {
  NodeConfig base;
  base.id.set("node-1");
  base.cluster.set("prod");
  base.region.set("us-west");

  NodeConfig overlay;
  overlay.cluster.set("staging");  // Only override cluster
  overlay.zone.set("us-west-2a");  // Add new field

  base.merge(overlay);

  // Unchanged fields retain original values
  EXPECT_EQ(base.id.get(), "node-1");
  EXPECT_EQ(base.region.get(), "us-west");

  // Changed field gets overlay value
  EXPECT_EQ(base.cluster.get(), "staging");

  // New field from overlay
  EXPECT_EQ(base.zone.get(), "us-west-2a");
}

TEST_F(ConfigMergeTest, AdminConfigMergeDefaultAddress) {
  AdminConfig base;
  AdminConfig overlay;

  // Explicitly set admin to localhost (same as default)
  overlay.address.set("127.0.0.1");
  overlay.port.set(9901);

  base.merge(overlay);

  // Should apply even though these are default values
  EXPECT_TRUE(base.address.is_set());
  EXPECT_TRUE(base.port.is_set());
  EXPECT_EQ(base.address.get(), "127.0.0.1");
  EXPECT_EQ(base.port.get(), 9901);
}

TEST_F(ConfigMergeTest, AdminConfigMergeAllowedIPs) {
  AdminConfig base;
  base.allowed_ips.set({"192.168.1.0/24"});

  AdminConfig overlay;
  // Explicitly set to default localhost values
  overlay.allowed_ips.set({"127.0.0.1", "::1"});

  base.merge(overlay);

  // Should use overlay's values even though they're defaults
  EXPECT_EQ(base.allowed_ips.get().size(), 2);
  EXPECT_EQ(base.allowed_ips.get()[0], "127.0.0.1");
  EXPECT_EQ(base.allowed_ips.get()[1], "::1");
}

TEST_F(ConfigMergeTest, BootstrapConfigComplexMerge) {
  BootstrapConfig base;
  base.node.id.set("base-node");
  base.node.cluster.set("production");
  base.admin.port.set(8080);
  base.admin.enabled.set(true);

  BootstrapConfig overlay;
  // Set some fields to defaults explicitly
  overlay.node.cluster.set("default");  // Override with default value
  overlay.admin.port.set(9901);         // Override with default value
  overlay.admin.enabled.set(false);     // Override to false
  overlay.config_path.set("/etc/mcp/config.yaml");

  base.merge(overlay);

  // Node ID unchanged (not in overlay)
  EXPECT_EQ(base.node.id.get(), "base-node");

  // Cluster overridden to default
  EXPECT_EQ(base.node.cluster.get(), "default");

  // Admin port overridden to default
  EXPECT_EQ(base.admin.port.get(), 9901);

  // Admin enabled overridden
  EXPECT_EQ(base.admin.enabled.get(), false);

  // New config path from overlay
  EXPECT_EQ(base.config_path.get(), "/etc/mcp/config.yaml");
}

TEST_F(ConfigMergeTest, JsonRoundTripPreservesSetState) {
  NodeConfig original;
  original.id.set("test-node");
  // cluster is not set (will use default)
  original.region.set("us-east");
  // zone is not set

  auto json = original.toJson();

  // JSON should only contain explicitly set fields
  EXPECT_TRUE(json.contains("id"));
  EXPECT_TRUE(json.contains("cluster"));  // Always included as it's required
  EXPECT_TRUE(json.contains("region"));
  EXPECT_FALSE(json.contains("zone"));  // Not set, not included

  auto loaded = NodeConfig::fromJson(json);

  // Loaded config should have same set state
  EXPECT_TRUE(loaded.id.is_set());
  EXPECT_TRUE(loaded.cluster.is_set());  // Will use default
  EXPECT_TRUE(loaded.region.is_set());
  EXPECT_FALSE(loaded.zone.is_set());

  // Values should match
  EXPECT_EQ(loaded.id.get(), "test-node");
  EXPECT_EQ(loaded.cluster.get(), "default");  // Default value
  EXPECT_EQ(loaded.region.get(), "us-east");
}

TEST_F(ConfigMergeTest, MultiLevelMerge) {
  // Simulate loading configs from multiple sources
  // 1. Defaults
  BootstrapConfig defaults;

  // 2. System config file
  BootstrapConfig system_config;
  system_config.node.cluster.set("production");
  system_config.admin.port.set(9000);

  // 3. User config file
  BootstrapConfig user_config;
  user_config.node.id.set("my-node");
  user_config.admin.port.set(9901);  // Override back to default
  user_config.admin.enabled.set(false);

  // 4. Environment variables
  BootstrapConfig env_config;
  env_config.admin.address.set("0.0.0.0");

  // Apply merges in order
  BootstrapConfig final_config = defaults;
  final_config.merge(system_config);
  final_config.merge(user_config);
  final_config.merge(env_config);

  // Verify final values
  EXPECT_EQ(final_config.node.id.get(), "my-node");          // From user
  EXPECT_EQ(final_config.node.cluster.get(), "production");  // From system
  EXPECT_EQ(final_config.admin.port.get(), 9901);  // From user (default value)
  EXPECT_EQ(final_config.admin.address.get(), "0.0.0.0");  // From env
  EXPECT_EQ(final_config.admin.enabled.get(), false);      // From user
}

TEST_F(ConfigMergeTest, ValidateAfterMerge) {
  NodeConfig base;
  base.id.set("");  // Invalid empty ID

  NodeConfig overlay;
  overlay.id.set("valid-node-id");

  // Base config is invalid
  EXPECT_THROW(base.validate(), ConfigValidationError);

  // After merge, should be valid
  base.merge(overlay);
  EXPECT_NO_THROW(base.validate());

  // Test merge that results in invalid config
  NodeConfig bad_overlay;
  bad_overlay.cluster.set("");  // Invalid empty cluster

  base.merge(bad_overlay);
  EXPECT_THROW(base.validate(), ConfigValidationError);
}