/**
 * @file test_file_config_source.cc
 * @brief Comprehensive tests for FileConfigSource implementation
 */

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "mcp/config/config_manager.h"
#include "mcp/logging/log_sink.h"
#include "mcp/logging/logger_registry.h"

namespace mcp {
namespace config {
namespace testing {

namespace fs = std::filesystem;

// Test fixture for FileConfigSource tests
class FileConfigSourceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create temporary test directory
    test_dir_ =
        fs::temp_directory_path() / "mcp_test" /
        ("test_" + std::to_string(getpid()) + "_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(test_dir_);

    // Set up test environment variable
    setenv("TEST_VAR", "test_value", 1);
    setenv("TEST_PORT", "8080", 1);

    // Clear MCP_CONFIG if set
    unsetenv("MCP_CONFIG");
  }

  void TearDown() override {
    // Clean up test directory
    if (fs::exists(test_dir_)) {
      fs::remove_all(test_dir_);
    }

    // Clean up environment variables
    unsetenv("TEST_VAR");
    unsetenv("TEST_PORT");
    unsetenv("MCP_CONFIG");
  }

  // Helper to create a test config file
  void createConfigFile(const fs::path& path, const nlohmann::json& content) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path);
    file << content.dump(2);
    file.close();
  }

  void createYamlFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path);
    file << content;
    file.close();
  }

 protected:
  fs::path test_dir_;
};

// Test search order resolution
TEST_F(FileConfigSourceTest, SearchOrderResolution) {
  // Create config files in different locations
  fs::path cli_config = test_dir_ / "cli_config.json";
  fs::path env_config = test_dir_ / "env_config.json";
  fs::path local_config = test_dir_ / "config" / "config.json";

  nlohmann::json cli_json = {{"source", "cli"}};
  nlohmann::json env_json = {{"source", "env"}};
  nlohmann::json local_json = {{"source", "local"}};

  createConfigFile(cli_config, cli_json);
  createConfigFile(env_config, env_json);
  createConfigFile(local_config, local_json);

  // Test 1: CLI config takes precedence
  {
    auto source = createFileConfigSource("test", 1, cli_config.string());
    auto config = source->load();
    EXPECT_EQ(config["source"], "cli");
  }

  // Test 2: ENV config used when no CLI config
  {
    setenv("MCP_CONFIG", env_config.string().c_str(), 1);
    auto source = createFileConfigSource("test", 1, "");
    auto config = source->load();
    EXPECT_EQ(config["source"], "env");
    unsetenv("MCP_CONFIG");
  }

  // Test 3: Local config used as fallback
  {
    // Change to test directory so ./config/config.json is found
    auto original_dir = fs::current_path();
    fs::current_path(test_dir_);

    auto source = createFileConfigSource("test", 1, "");
    auto config = source->load();
    EXPECT_EQ(config["source"], "local");

    fs::current_path(original_dir);
  }
}

// Test environment variable substitution
TEST_F(FileConfigSourceTest, EnvironmentVariableSubstitution) {
  fs::path config_file = test_dir_ / "config.json";

  nlohmann::json config = {{"host", "${TEST_HOST:-localhost}"},
                           {"port", "${TEST_PORT}"},
                           {"path", "/api/${TEST_VAR}/endpoint"},
                           {"timeout", "${UNDEFINED_VAR:-30}"},
                           {"nested", {{"value", "${TEST_VAR}"}}}};

  createConfigFile(config_file, config);

  auto source = createFileConfigSource("test", 1, config_file.string());
  auto result = source->load();

  // TEST_HOST not set, should use default
  EXPECT_EQ(result["host"], "localhost");

  // TEST_PORT is set to 8080
  EXPECT_EQ(result["port"], "8080");

  // TEST_VAR is set to test_value
  EXPECT_EQ(result["path"], "/api/test_value/endpoint");

  // UNDEFINED_VAR not set, should use default
  EXPECT_EQ(result["timeout"], "30");

  // Nested substitution
  EXPECT_EQ(result["nested"]["value"], "test_value");
}

// Test include file resolution
TEST_F(FileConfigSourceTest, IncludeFileResolution) {
  fs::path main_config = test_dir_ / "main.json";
  fs::path include1 = test_dir_ / "includes" / "db.json";
  fs::path include2 = test_dir_ / "includes" / "server.json";

  nlohmann::json db_config = {
      {"database", {{"host", "localhost"}, {"port", 5432}}}};

  nlohmann::json server_config = {{"server", {{"port", 8080}, {"workers", 4}}}};

  nlohmann::json main = {
      {"app", "test"},
      {"include", {"includes/db.json", "includes/server.json"}}};

  createConfigFile(include1, db_config);
  createConfigFile(include2, server_config);
  createConfigFile(main_config, main);

  auto source = createFileConfigSource("test", 1, main_config.string());
  auto result = source->load();

  // Check that includes were processed and merged
  EXPECT_EQ(result["app"], "test");
  EXPECT_EQ(result["database"]["host"], "localhost");
  EXPECT_EQ(result["database"]["port"], 5432);
  EXPECT_EQ(result["server"]["port"], 8080);
  EXPECT_EQ(result["server"]["workers"], 4);

  // Include directive should be removed
  EXPECT_FALSE(result.contains("include"));
}

// Test directory scanning (config.d pattern)
TEST_F(FileConfigSourceTest, DirectoryScanning) {
  fs::path main_config = test_dir_ / "main.json";
  fs::path config_dir = test_dir_ / "conf.d";

  // Create multiple config files in directory
  nlohmann::json config1 = {{"module1", {{"enabled", true}}}};
  nlohmann::json config2 = {{"module2", {{"timeout", 30}}}};
  nlohmann::json config3 = {{"module3", {{"max_connections", 100}}}};

  createConfigFile(config_dir / "01-module1.json", config1);
  createConfigFile(config_dir / "02-module2.json", config2);
  createConfigFile(config_dir / "03-module3.yaml", config3);  // Mixed formats

  nlohmann::json main = {{"app", "test"}, {"include_dir", "conf.d"}};

  createConfigFile(main_config, main);

  auto source = createFileConfigSource("test", 1, main_config.string());
  auto result = source->load();

  // Check that all files were included (sorted order)
  EXPECT_EQ(result["app"], "test");
  EXPECT_TRUE(result["module1"]["enabled"]);
  EXPECT_EQ(result["module2"]["timeout"], 30);
  EXPECT_EQ(result["module3"]["max_connections"], 100);

  // Include_dir directive should be removed
  EXPECT_FALSE(result.contains("include_dir"));
}

// Test YAML parsing
TEST_F(FileConfigSourceTest, YamlParsing) {
  fs::path yaml_file = test_dir_ / "config.yaml";

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

  auto source = createFileConfigSource("test", 1, yaml_file.string());
  auto result = source->load();

  EXPECT_EQ(result["node"]["id"], "test-node");
  EXPECT_EQ(result["node"]["cluster"], "production");
  EXPECT_EQ(result["admin"]["bind_address"], "127.0.0.1");
  EXPECT_EQ(result["admin"]["port"], 9001);
  EXPECT_EQ(result["server"]["listeners"][0]["name"], "main");
}

// Test parse error handling with line/column info
TEST_F(FileConfigSourceTest, ParseErrorHandling) {
  fs::path bad_json = test_dir_ / "bad.json";
  fs::path bad_yaml = test_dir_ / "bad.yaml";

  // Invalid JSON
  std::ofstream json_file(bad_json);
  json_file << R"({
  "valid": "field",
  "invalid": // missing value
})";
  json_file.close();

  auto json_source = createFileConfigSource("test", 1, bad_json.string());
  EXPECT_THROW(
      {
        try {
          json_source->load();
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

  auto yaml_source = createFileConfigSource("test", 1, bad_yaml.string());
  EXPECT_THROW(
      {
        try {
          yaml_source->load();
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
  fs::path config1 = test_dir_ / "config1.json";
  fs::path config2 = test_dir_ / "config2.json";

  // Create circular includes
  nlohmann::json json1 = {{"data1", "value1"}, {"include", "config2.json"}};

  nlohmann::json json2 = {{"data2", "value2"}, {"include", "config1.json"}};

  createConfigFile(config1, json1);
  createConfigFile(config2, json2);

  auto source = createFileConfigSource("test", 1, config1.string());
  auto result = source->load();

  // Should handle circular includes gracefully
  // Each file should be included only once
  EXPECT_EQ(result["data1"], "value1");
  EXPECT_EQ(result["data2"], "value2");
}

// Test max include depth
TEST_F(FileConfigSourceTest, MaxIncludeDepth) {
  // Create a chain of includes exceeding max depth
  fs::path base = test_dir_;

  for (int i = 0; i <= 12; i++) {  // Max depth is typically 10
    fs::path config = base / ("config" + std::to_string(i) + ".json");
    nlohmann::json content = {{"level", i}};

    if (i < 12) {
      content["include"] = "config" + std::to_string(i + 1) + ".json";
    }

    createConfigFile(config, content);
  }

  auto source =
      createFileConfigSource("test", 1, (base / "config0.json").string());

  // Should throw when max depth exceeded
  EXPECT_THROW({ source->load(); }, std::runtime_error);
}

// Test missing file handling
TEST_F(FileConfigSourceTest, MissingFileHandling) {
  fs::path nonexistent = test_dir_ / "nonexistent.json";

  auto source = createFileConfigSource("test", 1, nonexistent.string());

  // Should return empty config or throw
  try {
    auto result = source->load();
    // If it doesn't throw, should return empty
    EXPECT_TRUE(result.empty() || result.is_object());
  } catch (const std::exception& e) {
    // Throwing is also acceptable
    EXPECT_TRUE(std::string(e.what()).find("open") != std::string::npos ||
                std::string(e.what()).find("exist") != std::string::npos);
  }
}

// Test configuration merge semantics
TEST_F(FileConfigSourceTest, MergeSemantics) {
  fs::path base_config = test_dir_ / "base.json";
  fs::path override_config = test_dir_ / "override.json";

  nlohmann::json base = {
      {"server", {{"port", 8080}, {"workers", 4}, {"timeout", 30}}},
      {"database", {{"host", "localhost"}}}};

  nlohmann::json override = {
      {"server",
       {
           {"port", 9090},  // Override
           {"workers", 8}   // Override
                            // timeout not specified, should keep base value
       }},
      {"cache",
       {// New section
        {"enabled", true}}}};

  createConfigFile(base_config, base);
  createConfigFile(override_config, override);

  nlohmann::json main = {
      {"include", {base_config.string(), override_config.string()}}};

  fs::path main_config = test_dir_ / "main.json";
  createConfigFile(main_config, main);

  auto source = createFileConfigSource("test", 1, main_config.string());
  auto result = source->load();

  // Check merge results
  EXPECT_EQ(result["server"]["port"], 9090);           // Overridden
  EXPECT_EQ(result["server"]["workers"], 8);           // Overridden
  EXPECT_EQ(result["server"]["timeout"], 30);          // Kept from base
  EXPECT_EQ(result["database"]["host"], "localhost");  // Kept from base
  EXPECT_TRUE(result["cache"]["enabled"]);             // New from override
}

// Test logging output
TEST_F(FileConfigSourceTest, LoggingOutput) {
  // Create a test log sink to capture log messages
  class TestLogSink : public mcp::logging::LogSink {
   public:
    void log(const mcp::logging::LogMessage& message) override {
      messages_.push_back(message);
    }

    bool hasMessage(const std::string& substr, mcp::logging::LogLevel level) {
      for (const auto& msg : messages_) {
        if (msg.level == level) {
          std::stringstream ss;
          ss << msg;
          if (ss.str().find(substr) != std::string::npos) {
            return true;
          }
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
  logger->addSink(test_sink);

  // Test successful parse
  {
    test_sink->clear();
    fs::path config_file = test_dir_ / "valid.json";
    createConfigFile(config_file, {{"test", "value"}});

    auto source = createFileConfigSource("test", 1, config_file.string());
    source->load();

    // Should have INFO logs for discovery start/end
    EXPECT_TRUE(test_sink->hasMessage("Starting configuration discovery",
                                      mcp::logging::LogLevel::Info));
    EXPECT_TRUE(test_sink->hasMessage("Configuration discovery completed",
                                      mcp::logging::LogLevel::Info));
  }

  // Test parse error
  {
    test_sink->clear();
    fs::path bad_file = test_dir_ / "bad.json";
    std::ofstream file(bad_file);
    file << "{ invalid json }";
    file.close();

    auto source = createFileConfigSource("test", 1, bad_file.string());

    EXPECT_THROW(source->load(), std::exception);

    // Should have ERROR log
    EXPECT_TRUE(test_sink->hasMessage("Failed to parse",
                                      mcp::logging::LogLevel::Error));
  }
}

// Test ConfigurationManager integration
TEST_F(FileConfigSourceTest, ConfigurationManagerIntegration) {
  fs::path config_file = test_dir_ / "manager_test.json";

  nlohmann::json config = {
      {"node", {{"id", "test-node-${TEST_VAR}"}, {"cluster", "test-cluster"}}},
      {"admin", {{"bind_address", "127.0.0.1"}, {"port", "${TEST_PORT}"}}}};

  createConfigFile(config_file, config);

  auto& manager = ConfigurationManager::getInstance();

  // Add file source
  std::vector<std::shared_ptr<ConfigSource>> sources;
  sources.push_back(createFileConfigSource("file", 1, config_file.string()));

  EXPECT_TRUE(manager.initialize(sources, UnknownFieldPolicy::WARN));
  manager.loadConfiguration();

  auto bootstrap = manager.getBootstrapConfig();
  ASSERT_NE(bootstrap, nullptr);

  // Check environment substitution worked
  EXPECT_EQ(bootstrap->node.id, "test-node-test_value");
  EXPECT_EQ(bootstrap->node.cluster, "test-cluster");
  EXPECT_EQ(bootstrap->admin.bind_address, "127.0.0.1");
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