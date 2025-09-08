#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "mcp/json/json_bridge.h"
#include "mcp/logging/log_macros.h"
#include "mcp/logging/logger_registry.h"
#include "mcp/logging/log_sink.h"
#include "mcp/config/config_merger.h"

// Forward declarations for merger
namespace mcp {
namespace config {

class ConfigMerger;
std::unique_ptr<ConfigMerger> createConfigMerger();

}  // namespace config
}  // namespace mcp

// Test logging sink
namespace mcp {
namespace logging {

class TestLogSink : public LogSink {
 public:
  void log(const LogMessage& msg) override {
    messages_.push_back(msg);
  }

  void flush() override {}

  SinkType type() const override { return SinkType::External; }

  bool hasMessage(LogLevel level, const std::string& substr) {
    for (const auto& msg : messages_) {
      if (msg.level == level && msg.message.find(substr) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

  std::vector<std::string> getDebugMessages() {
    std::vector<std::string> debug_msgs;
    for (const auto& msg : messages_) {
      if (msg.level == LogLevel::Debug) {
        debug_msgs.push_back(msg.message);
      }
    }
    return debug_msgs;
  }

  void clear() { messages_.clear(); }

 private:
  std::vector<LogMessage> messages_;
};

}  // namespace logging
}  // namespace mcp

namespace mcp {
namespace config {
namespace test {

using json::JsonValue;

class MergeSemanticsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    merger_ = createConfigMerger();
    
    // Set up test logging
    test_sink_ = std::make_shared<logging::TestLogSink>();
    auto& registry = logging::LoggerRegistry::instance();
    auto logger = registry.getOrCreateLogger("config.merge");
    logger->setSink(test_sink_);
    logger->setLevel(logging::LogLevel::Debug);
  }
  
  // Helper to create a pretty-printed representation for before/after comparison
  std::string jsonToString(const JsonValue& value, int indent = 2);
  
  std::unique_ptr<ConfigMerger> merger_;
  std::shared_ptr<logging::TestLogSink> test_sink_;
};

std::string MergeSemanticsTest::jsonToString(const JsonValue& value, int indent) {
  // Simple JSON pretty printer for test output
  // In production, would use a proper JSON serializer
  return "<json representation>";
}

// ============================================================================
// Object Merge Semantics
// ============================================================================

TEST_F(MergeSemanticsTest, ObjectDeepMerge_BeforeAfter) {
  // BEFORE: Two configuration objects to merge
  JsonValue base = JsonValue::object();
  base["server"]["host"] = "localhost";
  base["server"]["port"] = 8080;
  base["server"]["ssl"]["enabled"] = false;
  base["database"]["host"] = "db.local";
  base["database"]["port"] = 5432;
  base["database"]["pool"]["min"] = 5;
  base["database"]["pool"]["max"] = 20;
  
  JsonValue overlay = JsonValue::object();
  overlay["server"]["port"] = 9090;  // Override scalar
  overlay["server"]["ssl"]["enabled"] = true;  // Override nested scalar
  overlay["server"]["ssl"]["cert"] = "/path/to/cert";  // Add new field
  overlay["database"]["pool"]["max"] = 50;  // Override in nested object
  overlay["monitoring"]["enabled"] = true;  // Add new top-level object
  
  test_sink_->clear();
  
  std::vector<std::pair<std::string, JsonValue>> sources = {
    {"base", base},
    {"overlay", overlay}
  };
  
  // AFTER: Perform merge
  auto result = merger_->merge(sources);
  
  // Verify deep merge behavior
  EXPECT_EQ(result["server"]["host"].getString(), "localhost");  // Preserved
  EXPECT_EQ(result["server"]["port"].getInt(), 9090);  // Overridden
  EXPECT_EQ(result["server"]["ssl"]["enabled"].getBool(), true);  // Overridden
  EXPECT_EQ(result["server"]["ssl"]["cert"].getString(), "/path/to/cert");  // Added
  EXPECT_EQ(result["database"]["host"].getString(), "db.local");  // Preserved
  EXPECT_EQ(result["database"]["port"].getInt(), 5432);  // Preserved
  EXPECT_EQ(result["database"]["pool"]["min"].getInt(), 5);  // Preserved
  EXPECT_EQ(result["database"]["pool"]["max"].getInt(), 50);  // Overridden
  EXPECT_EQ(result["monitoring"]["enabled"].getBool(), true);  // Added
  
  // Verify logging shows deep merge decisions
  EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Debug, "DEEP-MERGE"));
  EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Debug, "OVERRIDE"));
}

TEST_F(MergeSemanticsTest, ScalarOverride_BeforeAfter) {
  // BEFORE: Scalar values at various levels
  JsonValue base = JsonValue::object();
  base["string_val"] = "original";
  base["int_val"] = 42;
  base["bool_val"] = false;
  base["nested"]["scalar"] = "nested_original";
  
  JsonValue overlay = JsonValue::object();
  overlay["string_val"] = "updated";
  overlay["int_val"] = 100;
  overlay["bool_val"] = true;
  overlay["nested"]["scalar"] = "nested_updated";
  overlay["new_scalar"] = "added";
  
  test_sink_->clear();
  
  std::vector<std::pair<std::string, JsonValue>> sources = {
    {"base", base},
    {"overlay", overlay}
  };
  
  // AFTER: All scalars should be overridden
  auto result = merger_->merge(sources);
  
  EXPECT_EQ(result["string_val"].getString(), "updated");
  EXPECT_EQ(result["int_val"].getInt(), 100);
  EXPECT_EQ(result["bool_val"].getBool(), true);
  EXPECT_EQ(result["nested"]["scalar"].getString(), "nested_updated");
  EXPECT_EQ(result["new_scalar"].getString(), "added");
  
  // Verify conflict detection
  auto debug_msgs = test_sink_->getDebugMessages();
  int conflict_count = 0;
  for (const auto& msg : debug_msgs) {
    if (msg.find("Conflict detected") != std::string::npos) {
      conflict_count++;
    }
  }
  EXPECT_GT(conflict_count, 0);
}

// ============================================================================
// Array Merge Semantics
// ============================================================================

TEST_F(MergeSemanticsTest, FilterListReplace_BeforeAfter) {
  // BEFORE: Filter lists that should be replaced entirely
  JsonValue base = JsonValue::object();
  JsonValue base_filters = JsonValue::array();
  base_filters.push_back("auth");
  base_filters.push_back("rate_limit");
  base_filters.push_back("logging");
  base["filters"] = base_filters;
  
  JsonValue base_http_filters = JsonValue::array();
  base_http_filters.push_back("cors");
  base_http_filters.push_back("compression");
  base["http_filters"] = base_http_filters;
  
  JsonValue overlay = JsonValue::object();
  JsonValue overlay_filters = JsonValue::array();
  overlay_filters.push_back("jwt_auth");  // Completely different
  overlay_filters.push_back("circuit_breaker");
  overlay["filters"] = overlay_filters;
  
  JsonValue overlay_http_filters = JsonValue::array();
  overlay_http_filters.push_back("cors");  // Same first element
  overlay_http_filters.push_back("gzip");  // Different second
  overlay_http_filters.push_back("cache");  // Additional third
  overlay["http_filters"] = overlay_http_filters;
  
  test_sink_->clear();
  
  std::vector<std::pair<std::string, JsonValue>> sources = {
    {"base", base},
    {"overlay", overlay}
  };
  
  // AFTER: Filter lists completely replaced
  auto result = merger_->merge(sources);
  
  // Verify complete replacement
  ASSERT_EQ(result["filters"].size(), 2);
  EXPECT_EQ(result["filters"][0].getString(), "jwt_auth");
  EXPECT_EQ(result["filters"][1].getString(), "circuit_breaker");
  
  ASSERT_EQ(result["http_filters"].size(), 3);
  EXPECT_EQ(result["http_filters"][0].getString(), "cors");
  EXPECT_EQ(result["http_filters"][1].getString(), "gzip");
  EXPECT_EQ(result["http_filters"][2].getString(), "cache");
  
  // Verify logging shows REPLACE category
  EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Debug, "Category chosen"));
  EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Debug, "REPLACE (filter list)"));
}

TEST_F(MergeSemanticsTest, NamedResourceMergeByName_BeforeAfter) {
  // BEFORE: Named resources that should merge by name field
  JsonValue base = JsonValue::object();
  JsonValue base_listeners = JsonValue::array();
  
  JsonValue http_listener = JsonValue::object();
  http_listener["name"] = "http";
  http_listener["port"] = 80;
  http_listener["protocol"] = "HTTP/1.1";
  http_listener["timeout"] = 30;
  base_listeners.push_back(http_listener);
  
  JsonValue https_listener = JsonValue::object();
  https_listener["name"] = "https";
  https_listener["port"] = 443;
  https_listener["protocol"] = "HTTP/2";
  https_listener["ssl"]["enabled"] = true;
  base_listeners.push_back(https_listener);
  
  JsonValue admin_listener = JsonValue::object();
  admin_listener["name"] = "admin";
  admin_listener["port"] = 9000;
  admin_listener["protocol"] = "HTTP/1.1";
  base_listeners.push_back(admin_listener);
  
  base["listeners"] = base_listeners;
  
  JsonValue overlay = JsonValue::object();
  JsonValue overlay_listeners = JsonValue::array();
  
  // Modify existing http listener
  JsonValue http_update = JsonValue::object();
  http_update["name"] = "http";
  http_update["port"] = 8080;  // Changed
  http_update["timeout"] = 60;  // Changed
  http_update["max_connections"] = 1000;  // Added
  overlay_listeners.push_back(http_update);
  
  // Add new grpc listener
  JsonValue grpc_listener = JsonValue::object();
  grpc_listener["name"] = "grpc";
  grpc_listener["port"] = 50051;
  grpc_listener["protocol"] = "gRPC";
  overlay_listeners.push_back(grpc_listener);
  
  // Modify https listener
  JsonValue https_update = JsonValue::object();
  https_update["name"] = "https";
  https_update["ssl"]["cert"] = "/path/to/cert.pem";  // Add nested field
  https_update["ssl"]["key"] = "/path/to/key.pem";    // Add nested field
  overlay_listeners.push_back(https_update);
  
  overlay["listeners"] = overlay_listeners;
  
  test_sink_->clear();
  
  std::vector<std::pair<std::string, JsonValue>> sources = {
    {"base", base},
    {"overlay", overlay}
  };
  
  // AFTER: Named resources merged by name
  auto result = merger_->merge(sources);
  
  // Should have 4 listeners: http (merged), https (merged), admin (unchanged), grpc (new)
  ASSERT_EQ(result["listeners"].size(), 4);
  
  // Find and verify each listener
  bool found_http = false, found_https = false, found_admin = false, found_grpc = false;
  
  for (size_t i = 0; i < result["listeners"].size(); ++i) {
    const auto& listener = result["listeners"][i];
    std::string name = listener["name"].getString();
    
    if (name == "http") {
      found_http = true;
      EXPECT_EQ(listener["port"].getInt(), 8080);  // Updated
      EXPECT_EQ(listener["protocol"].getString(), "HTTP/1.1");  // Preserved
      EXPECT_EQ(listener["timeout"].getInt(), 60);  // Updated
      EXPECT_EQ(listener["max_connections"].getInt(), 1000);  // Added
    } else if (name == "https") {
      found_https = true;
      EXPECT_EQ(listener["port"].getInt(), 443);  // Preserved
      EXPECT_EQ(listener["protocol"].getString(), "HTTP/2");  // Preserved
      EXPECT_EQ(listener["ssl"]["enabled"].getBool(), true);  // Preserved
      EXPECT_EQ(listener["ssl"]["cert"].getString(), "/path/to/cert.pem");  // Added
      EXPECT_EQ(listener["ssl"]["key"].getString(), "/path/to/key.pem");  // Added
    } else if (name == "admin") {
      found_admin = true;
      EXPECT_EQ(listener["port"].getInt(), 9000);  // Unchanged
      EXPECT_EQ(listener["protocol"].getString(), "HTTP/1.1");  // Unchanged
    } else if (name == "grpc") {
      found_grpc = true;
      EXPECT_EQ(listener["port"].getInt(), 50051);  // New
      EXPECT_EQ(listener["protocol"].getString(), "gRPC");  // New
    }
  }
  
  EXPECT_TRUE(found_http);
  EXPECT_TRUE(found_https);
  EXPECT_TRUE(found_admin);
  EXPECT_TRUE(found_grpc);
  
  // Verify logging shows MERGE-BY-NAME category
  EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Debug, "MERGE-BY-NAME"));
  EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Debug, "Merging named resource"));
  EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Debug, "Adding new named resource"));
}

TEST_F(MergeSemanticsTest, DefaultArrayReplace_BeforeAfter) {
  // BEFORE: Regular arrays (not filters or named resources) should replace
  JsonValue base = JsonValue::object();
  JsonValue base_tags = JsonValue::array();
  base_tags.push_back("production");
  base_tags.push_back("v1.0");
  base_tags.push_back("stable");
  base["tags"] = base_tags;
  
  JsonValue base_ports = JsonValue::array();
  base_ports.push_back(80);
  base_ports.push_back(443);
  base["allowed_ports"] = base_ports;
  
  JsonValue overlay = JsonValue::object();
  JsonValue overlay_tags = JsonValue::array();
  overlay_tags.push_back("production");  // Same first
  overlay_tags.push_back("v2.0");  // Different
  overlay["tags"] = overlay_tags;
  
  JsonValue overlay_ports = JsonValue::array();
  overlay_ports.push_back(8080);
  overlay_ports.push_back(8443);
  overlay_ports.push_back(9000);
  overlay["allowed_ports"] = overlay_ports;
  
  test_sink_->clear();
  
  std::vector<std::pair<std::string, JsonValue>> sources = {
    {"base", base},
    {"overlay", overlay}
  };
  
  // AFTER: Arrays completely replaced
  auto result = merger_->merge(sources);
  
  ASSERT_EQ(result["tags"].size(), 2);
  EXPECT_EQ(result["tags"][0].getString(), "production");
  EXPECT_EQ(result["tags"][1].getString(), "v2.0");
  
  ASSERT_EQ(result["allowed_ports"].size(), 3);
  EXPECT_EQ(result["allowed_ports"][0].getInt(), 8080);
  EXPECT_EQ(result["allowed_ports"][1].getInt(), 8443);
  EXPECT_EQ(result["allowed_ports"][2].getInt(), 9000);
  
  // Verify logging shows default REPLACE for arrays
  EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Debug, 
                                     "REPLACE (default array behavior)"));
}

// ============================================================================
// Complex Scenarios
// ============================================================================

TEST_F(MergeSemanticsTest, ComplexNestedMerge_BeforeAfter) {
  // BEFORE: Complex nested structure with mixed types
  JsonValue base = JsonValue::object();
  
  // Server configuration with nested objects and arrays
  base["server"]["http"]["listeners"] = JsonValue::array();
  JsonValue http_listener = JsonValue::object();
  http_listener["name"] = "main";
  http_listener["port"] = 8080;
  base["server"]["http"]["listeners"].push_back(http_listener);
  
  base["server"]["http"]["filters"] = JsonValue::array();
  base["server"]["http"]["filters"].push_back("auth");
  base["server"]["http"]["filters"].push_back("cors");
  
  base["server"]["settings"]["timeout"] = 30;
  base["server"]["settings"]["max_connections"] = 1000;
  
  // Database configuration
  base["database"]["clusters"] = JsonValue::array();
  JsonValue primary_cluster = JsonValue::object();
  primary_cluster["name"] = "primary";
  primary_cluster["replicas"] = 3;
  base["database"]["clusters"].push_back(primary_cluster);
  
  JsonValue overlay = JsonValue::object();
  
  // Partial updates to server configuration
  overlay["server"]["http"]["listeners"] = JsonValue::array();
  JsonValue http_update = JsonValue::object();
  http_update["name"] = "main";
  http_update["port"] = 9090;  // Update port
  http_update["ssl"] = true;   // Add SSL
  overlay["server"]["http"]["listeners"].push_back(http_update);
  
  JsonValue metrics_listener = JsonValue::object();
  metrics_listener["name"] = "metrics";
  metrics_listener["port"] = 9001;
  overlay["server"]["http"]["listeners"].push_back(metrics_listener);
  
  // Replace filters entirely
  overlay["server"]["http"]["filters"] = JsonValue::array();
  overlay["server"]["http"]["filters"].push_back("jwt");
  
  // Update nested settings
  overlay["server"]["settings"]["timeout"] = 60;
  overlay["server"]["settings"]["keepalive"] = true;  // Add new field
  
  // Update database clusters
  overlay["database"]["clusters"] = JsonValue::array();
  JsonValue primary_update = JsonValue::object();
  primary_update["name"] = "primary";
  primary_update["replicas"] = 5;  // Increase replicas
  primary_update["backup"] = true;  // Add backup flag
  overlay["database"]["clusters"].push_back(primary_update);
  
  test_sink_->clear();
  
  std::vector<std::pair<std::string, JsonValue>> sources = {
    {"base", base},
    {"overlay", overlay}
  };
  
  // AFTER: Complex merge with different strategies
  auto result = merger_->merge(sources);
  
  // Verify listeners (named resources) merged by name
  ASSERT_EQ(result["server"]["http"]["listeners"].size(), 2);
  bool found_main = false, found_metrics = false;
  for (size_t i = 0; i < result["server"]["http"]["listeners"].size(); ++i) {
    const auto& l = result["server"]["http"]["listeners"][i];
    if (l["name"].getString() == "main") {
      found_main = true;
      EXPECT_EQ(l["port"].getInt(), 9090);  // Updated
      EXPECT_EQ(l["ssl"].getBool(), true);   // Added
    } else if (l["name"].getString() == "metrics") {
      found_metrics = true;
      EXPECT_EQ(l["port"].getInt(), 9001);
    }
  }
  EXPECT_TRUE(found_main);
  EXPECT_TRUE(found_metrics);
  
  // Verify filters (filter list) completely replaced
  ASSERT_EQ(result["server"]["http"]["filters"].size(), 1);
  EXPECT_EQ(result["server"]["http"]["filters"][0].getString(), "jwt");
  
  // Verify settings (object) deep merged
  EXPECT_EQ(result["server"]["settings"]["timeout"].getInt(), 60);  // Updated
  EXPECT_EQ(result["server"]["settings"]["max_connections"].getInt(), 1000);  // Preserved
  EXPECT_EQ(result["server"]["settings"]["keepalive"].getBool(), true);  // Added
  
  // Verify database clusters merged by name
  ASSERT_EQ(result["database"]["clusters"].size(), 1);
  EXPECT_EQ(result["database"]["clusters"][0]["name"].getString(), "primary");
  EXPECT_EQ(result["database"]["clusters"][0]["replicas"].getInt(), 5);  // Updated
  EXPECT_EQ(result["database"]["clusters"][0]["backup"].getBool(), true);  // Added
  
  // Verify all merge strategies were logged
  EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Debug, "MERGE-BY-NAME"));
  EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Debug, "REPLACE (filter list)"));
  EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Debug, "DEEP-MERGE"));
}

// ============================================================================
// Platform Determinism Tests
// ============================================================================

TEST_F(MergeSemanticsTest, DeterministicMergeOrder) {
  // Test that merge order is deterministic regardless of key insertion order
  JsonValue base1 = JsonValue::object();
  base1["z_key"] = "z_value";
  base1["a_key"] = "a_value";
  base1["m_key"] = "m_value";
  
  JsonValue base2 = JsonValue::object();
  base2["a_key"] = "a_value";
  base2["m_key"] = "m_value";
  base2["z_key"] = "z_value";
  
  JsonValue overlay = JsonValue::object();
  overlay["a_key"] = "a_updated";
  overlay["b_key"] = "b_new";
  
  // Merge with different base key orders
  std::vector<std::pair<std::string, JsonValue>> sources1 = {
    {"base", base1},
    {"overlay", overlay}
  };
  
  std::vector<std::pair<std::string, JsonValue>> sources2 = {
    {"base", base2},
    {"overlay", overlay}
  };
  
  auto result1 = merger_->merge(sources1);
  auto result2 = merger_->merge(sources2);
  
  // Results should be identical
  EXPECT_EQ(result1["a_key"].getString(), result2["a_key"].getString());
  EXPECT_EQ(result1["b_key"].getString(), result2["b_key"].getString());
  EXPECT_EQ(result1["m_key"].getString(), result2["m_key"].getString());
  EXPECT_EQ(result1["z_key"].getString(), result2["z_key"].getString());
}

// ============================================================================
// Conflict Logging Tests
// ============================================================================

TEST_F(MergeSemanticsTest, ConflictSummaryLogging) {
  JsonValue base = JsonValue::object();
  base["a"] = 1;
  base["b"] = 2;
  base["c"] = 3;
  base["nested"]["x"] = 10;
  base["nested"]["y"] = 20;
  
  JsonValue overlay = JsonValue::object();
  overlay["a"] = 100;  // Conflict
  overlay["b"] = 200;  // Conflict
  overlay["c"] = 3;    // No conflict (same value)
  overlay["nested"]["x"] = 1000;  // Conflict
  overlay["nested"]["z"] = 30;    // No conflict (new key)
  
  test_sink_->clear();
  
  std::vector<std::pair<std::string, JsonValue>> sources = {
    {"base", base},
    {"overlay", overlay}
  };
  
  auto result = merger_->merge(sources);
  
  // Check that conflicts were logged (keys only, no values)
  auto debug_msgs = test_sink_->getDebugMessages();
  
  // Should log conflict detection
  EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Debug, "Conflict detected"));
  
  // Should NOT log actual values in conflict messages
  bool found_value_in_logs = false;
  for (const auto& msg : debug_msgs) {
    if (msg.find("100") != std::string::npos || 
        msg.find("200") != std::string::npos ||
        msg.find("1000") != std::string::npos) {
      found_value_in_logs = true;
      break;
    }
  }
  EXPECT_FALSE(found_value_in_logs) << "Conflict logs should not contain values";
  
  // Verify final INFO log shows conflict count
  EXPECT_TRUE(test_sink_->hasMessage(logging::LogLevel::Info, "conflicts_resolved"));
}

}  // namespace test
}  // namespace config
}  // namespace mcp
