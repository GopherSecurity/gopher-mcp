/**
 * @file test_file_config_source.cc
 * @brief Comprehensive tests for FileConfigSource implementation
 */

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/config/config_manager.h"
#include "mcp/logging/log_sink.h"
#include "mcp/logging/logger_registry.h"
#include "mcp/json/json_bridge.h"
#include "test_filesystem_utils.h"
#include "test_json_helpers.h"

namespace mcp {
namespace config {
namespace testing {

using namespace fs_utils;
using mcp::json::JsonValue;
using test::makeJsonObject;
using test::makeJsonArray;
using test::str;
using test::num;
using test::boolean;

// Test fixture for FileConfigSource tests
class FileConfigSourceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create temporary test directory
    test_dir_ = createUniqueTempDirectory("mcp_test");

    // Set up test environment variable
    setenv("TEST_VAR", "test_value", 1);
    setenv("TEST_PORT", "8080", 1);

    // Clear MCP_CONFIG if set
    unsetenv("MCP_CONFIG");
  }

  void TearDown() override {
    // Clean up test directory
    if (pathExists(test_dir_)) {
      removeDirectoryRecursive(test_dir_);
    }

    // Clean up environment variables
    unsetenv("TEST_VAR");
    unsetenv("TEST_PORT");
    unsetenv("MCP_CONFIG");
  }

  // Helper to create a test config file
  void createConfigFile(const std::string& path, const JsonValue& content) {
    createDirectoryRecursive(getParentDirectory(path));
    std::ofstream file(path);
    file << content.toString(true);  // pretty=true for formatting
    file.close();
  }

  void createYamlFile(const std::string& path, const std::string& content) {
    createDirectoryRecursive(getParentDirectory(path));
    std::ofstream file(path);
    file << content;
    file.close();
  }

 protected:
  std::string test_dir_;
};

// Test search order resolution
TEST_F(FileConfigSourceTest, SearchOrderResolution) {
  // Create config files in different locations
  std::string cli_config = joinPath(test_dir_, "cli_config.json");
  std::string env_config = joinPath(test_dir_, "env_config.json");
  std::string local_config = joinPath(joinPath(test_dir_, "config"), "config.json");

  JsonValue cli_json = makeJsonObject({{"source", str("cli")}});
  JsonValue env_json = makeJsonObject({{"source", str("env")}});
  JsonValue local_json = makeJsonObject({{"source", str("local")}});

  createConfigFile(cli_config, cli_json);
  createConfigFile(env_config, env_json);
  createConfigFile(local_config, local_json);

  // Test 1: CLI config takes precedence
  {
    auto source = createFileConfigSource("test", 1, cli_config);
    auto config = source->loadConfiguration();
    EXPECT_EQ(std::string("cli"), config["source"].getString());
  }

  // Test 2: ENV config used when no CLI config
  {
    setenv("MCP_CONFIG", env_config.c_str(), 1);
    auto source = createFileConfigSource("test", 1, "");
    auto config = source->loadConfiguration();
    EXPECT_EQ(std::string("env"), config["source"].getString());
    unsetenv("MCP_CONFIG");
  }

  // Test 3: Local config used as fallback
  {
    // Change to test directory so ./config/config.json is found
    WorkingDirectoryGuard dir_guard(test_dir_);

    auto source = createFileConfigSource("test", 1, "");
    auto config = source->loadConfiguration();
    EXPECT_EQ(std::string("local"), config["source"].getString());
  }
}

// Test environment variable substitution
TEST_F(FileConfigSourceTest, EnvironmentVariableSubstitution) {
  std::string config_file = joinPath(test_dir_, "config.json");

  JsonValue config = makeJsonObject({
    {"host", str("${TEST_HOST:-localhost}")},
    {"port", str("${TEST_PORT}")},
    {"path", str("/api/${TEST_VAR}/endpoint")},
    {"timeout", str("${UNDEFINED_VAR:-30}")},
    {"nested", makeJsonObject({{"value", str("${TEST_VAR}")}})}
  });

  createConfigFile(config_file, config);

  auto source = createFileConfigSource("test", 1, config_file);
  auto result = source->loadConfiguration();

  // TEST_HOST not set, should use default
  EXPECT_EQ(std::string("localhost"), result["host"].getString());

  // TEST_PORT is set to 8080
  EXPECT_EQ(std::string("8080"), result["port"].getString());

  // TEST_VAR is set to test_value
  EXPECT_EQ(std::string("/api/test_value/endpoint"), result["path"].getString());

  // UNDEFINED_VAR not set, should use default
  EXPECT_EQ(std::string("30"), result["timeout"].getString());

  // Nested substitution
  EXPECT_EQ(std::string("test_value"), result["nested"]["value"].getString());
}

// Test include file resolution
TEST_F(FileConfigSourceTest, IncludeFileResolution) {
  std::string main_config = joinPath(test_dir_, "main.json");
  std::string include1 = joinPath(joinPath(test_dir_, "includes"), "db.json");
  std::string include2 = joinPath(joinPath(test_dir_, "includes"), "server.json");

  JsonValue db_config = makeJsonObject({
    {"database", makeJsonObject({{"host", str("localhost")}, {"port", num(5432)}})}
  });

  JsonValue server_config = makeJsonObject({
    {"server", makeJsonObject({{"port", num(8080)}, {"workers", num(4)}})}
  });

  JsonValue main = makeJsonObject({
    {"app", str("test")},
    {"include", makeJsonArray({str("includes/db.json"), str("includes/server.json")})}
  });

  createConfigFile(include1, db_config);
  createConfigFile(include2, server_config);
  createConfigFile(main_config, main);

  auto source = createFileConfigSource("test", 1, main_config);
  auto result = source->loadConfiguration();

  // Check that includes were processed and merged
  EXPECT_EQ(std::string("test"), result["app"].getString());
  EXPECT_EQ(std::string("localhost"), result["database"]["host"].getString());
  EXPECT_EQ(5432, result["database"]["port"].getInt());
  EXPECT_EQ(8080, result["server"]["port"].getInt());
  EXPECT_EQ(4, result["server"]["workers"].getInt());

  // Include directive should be removed
  EXPECT_FALSE(result.contains("include"));
}

// Test directory scanning (config.d pattern)
TEST_F(FileConfigSourceTest, DirectoryScanning) {
  std::string main_config = joinPath(test_dir_, "main.json");
  std::string config_dir = joinPath(test_dir_, "conf.d");

  // Create multiple config files in directory
  JsonValue config1 = makeJsonObject({{"module1", makeJsonObject({{"enabled", boolean(true)}})}});
  JsonValue config2 = makeJsonObject({{"module2", makeJsonObject({{"timeout", num(30)}})}});
  JsonValue config3 = makeJsonObject({{"module3", makeJsonObject({{"max_connections", num(100)}})}});

  createConfigFile(joinPath(config_dir, "01-module1.json"), config1);
  createConfigFile(joinPath(config_dir, "02-module2.json"), config2);
  createConfigFile(joinPath(config_dir, "03-module3.yaml"), config3);  // Mixed formats

  JsonValue main = makeJsonObject({{"app", str("test")}, {"include_dir", str("conf.d")}});

  createConfigFile(main_config, main);

  auto source = createFileConfigSource("test", 1, main_config);
  auto result = source->loadConfiguration();

  // Check that all files were included (sorted order)
  EXPECT_EQ(std::string("test"), result["app"].getString());
  EXPECT_TRUE(result["module1"]["enabled"].getBool());
  EXPECT_EQ(30, result["module2"]["timeout"].getInt());
  EXPECT_EQ(100, result["module3"]["max_connections"].getInt());

  // Include_dir directive should be removed
  EXPECT_FALSE(result.contains("include_dir"));
}

// Test YAML parsing
TEST_F(FileConfigSourceTest, YamlParsing) {
  std::string yaml_file = joinPath(test_dir_, "config.yaml");

  std::string yaml_content = R"(
node:
  id: test-node
  cluster: production
admin:
  bind_address: 127.0.0.1
  port: 9001
server:
  listeners:
    - name: main
      address: 0.0.0.0:8080
      transports:
        - type: tcp
)";

  createYamlFile(yaml_file, yaml_content);

  auto source = createFileConfigSource("test", 1, yaml_file);
  auto result = source->loadConfiguration();

  EXPECT_EQ(std::string("test-node"), result["node"]["id"].getString());
  EXPECT_EQ(std::string("production"), result["node"]["cluster"].getString());
  EXPECT_EQ(std::string("127.0.0.1"), result["admin"]["address"].getString());
  EXPECT_EQ(9001, result["admin"]["port"].getInt());
  EXPECT_EQ(std::string("main"), result["server"]["listeners"][0]["name"].getString());
}

// Test parse error handling with line/column info
TEST_F(FileConfigSourceTest, ParseErrorHandling) {
  std::string bad_json = joinPath(test_dir_, "bad.json");
  std::string bad_yaml = joinPath(test_dir_, "bad.yaml");

  // Invalid JSON
  std::ofstream json_file(bad_json);
  json_file << R"({
  "valid": "field",
  "invalid": // missing value
})";
  json_file.close();

  auto json_source = createFileConfigSource("test", 1, bad_json);
  EXPECT_THROW(
      {
        try {
          json_source->loadConfiguration();
        } catch (const std::exception& e) {
          std::string error = e.what();
          // Should contain parse error location info
          EXPECT_TRUE(error.find("parse error") != std::string::npos ||
                      error.find("byte") != std::string::npos);
          throw;
        }
      },
      std::exception);

  // Invalid YAML
  createYamlFile(bad_yaml, "key: value\n  invalid indentation");

  auto yaml_source = createFileConfigSource("test", 1, bad_yaml);
  EXPECT_THROW(
      {
        try {
          yaml_source->loadConfiguration();
        } catch (const std::exception& e) {
          std::string error = e.what();
          // Should contain line/column info
          EXPECT_TRUE(error.find("line") != std::string::npos ||
                      error.find("column") != std::string::npos);
          throw;
        }
      },
      std::exception);
}

// Test circular include detection
TEST_F(FileConfigSourceTest, CircularIncludeDetection) {
  std::string config1 = joinPath(test_dir_, "config1.json");
  std::string config2 = joinPath(test_dir_, "config2.json");

  // Create circular includes
  JsonValue json1 = makeJsonObject({{"data1", str("value1")}, {"include", str("config2.json")}});

  JsonValue json2 = makeJsonObject({{"data2", str("value2")}, {"include", str("config1.json")}});

  createConfigFile(config1, json1);
  createConfigFile(config2, json2);

  auto source = createFileConfigSource("test", 1, config1);
  auto result = source->loadConfiguration();

  // Should handle circular includes gracefully
  // Each file should be included only once
  EXPECT_EQ(std::string("value1"), result["data1"].getString());
  EXPECT_EQ(std::string("value2"), result["data2"].getString());
}

// Test max include depth
TEST_F(FileConfigSourceTest, MaxIncludeDepth) {
  // Create a chain of includes exceeding max depth
  std::string base = test_dir_;

  for (int i = 0; i <= 12; i++) {  // Max depth is typically 10
    std::string config = joinPath(base, "config" + std::to_string(i) + ".json");
    JsonValue content = makeJsonObject({{"level", num(i)}});

    if (i < 12) {
      content["include"] = JsonValue("config" + std::to_string(i + 1) + ".json");
    }

    createConfigFile(config, content);
  }

  auto source =
      createFileConfigSource("test", 1, joinPath(base, "config0.json"));

  // Should throw when max depth exceeded
  EXPECT_THROW({ source->loadConfiguration(); }, std::runtime_error);
}

// Test missing file handling
TEST_F(FileConfigSourceTest, MissingFileHandling) {
  std::string nonexistent = joinPath(test_dir_, "nonexistent.json");

  auto source = createFileConfigSource("test", 1, nonexistent);

  // Should return empty config or throw
  try {
    auto result = source->loadConfiguration();
    // If it doesn't throw, should return empty or an object
    EXPECT_TRUE(result.empty() || result.isObject());
  } catch (const std::exception& e) {
    // Throwing is also acceptable
    EXPECT_TRUE(std::string(e.what()).find("open") != std::string::npos ||
                std::string(e.what()).find("exist") != std::string::npos);
  }
}

// Test configuration merge semantics
TEST_F(FileConfigSourceTest, MergeSemantics) {
  std::string base_config = joinPath(test_dir_, "base.json");
  std::string override_config = joinPath(test_dir_, "override.json");

  JsonValue base = makeJsonObject({
    {"server", makeJsonObject({{"port", num(8080)}, {"workers", num(4)}, {"timeout", num(30)}})},
    {"database", makeJsonObject({{"host", str("localhost")}})}
  });

  JsonValue override = makeJsonObject({
    {"server", makeJsonObject({
      {"port", num(9090)},  // Override
      {"workers", num(8)}   // Override
                            // timeout not specified, should keep base value
    })},
    {"cache", makeJsonObject({// New section
      {"enabled", boolean(true)}
    })}
  });

  createConfigFile(base_config, base);
  createConfigFile(override_config, override);

  JsonValue main = makeJsonObject({
    {"include", makeJsonArray({str(base_config), str(override_config)})}
  });

  std::string main_config = joinPath(test_dir_, "main.json");
  createConfigFile(main_config, main);

  auto source = createFileConfigSource("test", 1, main_config);
  auto result = source->loadConfiguration();

  // Check merge results
  EXPECT_EQ(9090, result["server"]["port"].getInt());           // Overridden
  EXPECT_EQ(8, result["server"]["workers"].getInt());           // Overridden
  EXPECT_EQ(30, result["server"]["timeout"].getInt());          // Kept from base
  EXPECT_EQ(std::string("localhost"), result["database"]["host"].getString());  // Kept from base
  EXPECT_TRUE(result["cache"]["enabled"].getBool());             // New from override
}

// Test logging output
TEST_F(FileConfigSourceTest, LoggingOutput) {
  // Create a test log sink to capture log messages
  class TestLogSink : public mcp::logging::LogSink {
   public:
    void log(const mcp::logging::LogMessage& message) override {
      messages_.push_back(message);
    }
    
    void flush() override {}
    
    mcp::logging::SinkType type() const override { return mcp::logging::SinkType::Null; }

    bool hasMessage(const std::string& substr, mcp::logging::LogLevel level) {
      for (const auto& msg : messages_) {
        if (msg.level == level && msg.message.find(substr) != std::string::npos) {
          return true;
        }
      }
      return false;
    }

    void clear() { messages_.clear(); }

   private:
    std::vector<mcp::logging::LogMessage> messages_;
  };

  auto test_sink = std::make_shared<TestLogSink>();
  auto& registry = mcp::logging::LoggerRegistry::instance();
  auto logger = registry.getOrCreateLogger("config.file");
  logger->setSink(test_sink);

  // Test successful parse
  {
    test_sink->clear();
    std::string config_file = joinPath(test_dir_, "valid.json");
    createConfigFile(config_file, {{"test", "value"}});

    auto source = createFileConfigSource("test", 1, config_file);
    source->loadConfiguration();

    // Should have INFO logs for discovery start/end
    EXPECT_TRUE(test_sink->hasMessage("Starting configuration discovery",
                                      mcp::logging::LogLevel::Info));
    EXPECT_TRUE(test_sink->hasMessage("Configuration discovery completed",
                                      mcp::logging::LogLevel::Info));
  }

  // Test parse error
  {
    test_sink->clear();
    std::string bad_file = joinPath(test_dir_, "bad.json");
    std::ofstream file(bad_file);
    file << "{ invalid json }";
    file.close();

    auto source = createFileConfigSource("test", 1, bad_file);

    EXPECT_THROW(source->loadConfiguration(), std::exception);

    // Should have ERROR log
    EXPECT_TRUE(test_sink->hasMessage("Failed to parse",
                                      mcp::logging::LogLevel::Error));
  }
}

// Test ConfigurationManager integration
TEST_F(FileConfigSourceTest, ConfigurationManagerIntegration) {
  std::string config_file = joinPath(test_dir_, "manager_test.json");

  JsonValue config = makeJsonObject({
    {"node", makeJsonObject({{"id", str("test-node-${TEST_VAR}")}, {"cluster", str("test-cluster")}})},
    {"admin", makeJsonObject({{"address", str("127.0.0.1")}, {"port", str("${TEST_PORT}")}})}
  });

  createConfigFile(config_file, config);

  auto& manager = ConfigurationManager::getInstance();

  // Add file source
  std::vector<std::shared_ptr<ConfigSource>> sources;
  sources.push_back(createFileConfigSource("file", 1, config_file));

  EXPECT_TRUE(manager.initialize(sources, UnknownFieldPolicy::WARN));
  manager.loadConfiguration();

  auto bootstrap = manager.getBootstrapConfig();
  ASSERT_NE(bootstrap, nullptr);

  // Check environment substitution worked
  EXPECT_EQ(bootstrap->node.id, "test-node-test_value");
  EXPECT_EQ(bootstrap->node.cluster, "test-cluster");
  EXPECT_EQ(bootstrap->admin.address, "127.0.0.1");
  EXPECT_EQ(bootstrap->admin.port, 8080);  // Converted from string "8080"
}

// Test thread-safe version ID generation
TEST_F(FileConfigSourceTest, ThreadSafeVersionId) {
  auto& manager = ConfigurationManager::getInstance();
  manager.initialize({}, UnknownFieldPolicy::WARN);

  std::set<std::string> version_ids;
  std::mutex mutex;
  std::atomic<int> counter{0};
  const int num_threads = 10;
  const int ids_per_thread = 100;

  std::vector<std::thread> threads;

  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back([&]() {
      for (int j = 0; j < ids_per_thread; j++) {
        // Call internal generateVersionId through reload
        manager.loadConfiguration();
        auto version = manager.getCurrentVersion();

        std::lock_guard<std::mutex> lock(mutex);
        auto result = version_ids.insert(version);
        EXPECT_TRUE(result.second) << "Duplicate version ID: " << version;
        counter++;
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // All version IDs should be unique
  EXPECT_EQ(version_ids.size(), num_threads * ids_per_thread);

  // Version IDs should follow expected format
  for (const auto& id : version_ids) {
    // Format: YYYYMMDD-HHMMSS-NNNN (date-time-counter)
    EXPECT_TRUE(
        std::regex_match(id, std::regex(R"(\d{8}-\d{6}-\d{4}|\d+-\d{4})")));
  }
}

// Test listener invocation outside lock
TEST_F(FileConfigSourceTest, ListenerInvocationThreadSafety) {
  auto& manager = ConfigurationManager::getInstance();
  manager.initialize({}, UnknownFieldPolicy::WARN);

  std::atomic<int> callback_count{0};
  std::atomic<bool> deadlock_detected{false};

  // Add a listener that tries to add another listener (would deadlock if lock
  // held)
  auto listener_id =
      manager.addChangeListener([&](const ConfigChangeEvent& event) {
        callback_count++;

        // Try to add another listener from within callback
        // This would deadlock if the lock was still held
        std::thread detector([&]() {
          auto start = std::chrono::steady_clock::now();

          // This should not block
          auto id = manager.addChangeListener([](const ConfigChangeEvent&) {});

          auto duration = std::chrono::steady_clock::now() - start;
          if (duration > std::chrono::milliseconds(100)) {
            deadlock_detected = true;
          }

          manager.removeChangeListener(id);
        });

        detector.join();
      });

  // Trigger configuration change
  manager.loadConfiguration();

  // Wait a bit for callbacks
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  EXPECT_GT(callback_count, 0);
  EXPECT_FALSE(deadlock_detected) << "Deadlock detected in listener invocation";

  manager.removeChangeListener(listener_id);
}

}  // namespace testing
}  // namespace config
}  // namespace mcp

// Main test runner
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
