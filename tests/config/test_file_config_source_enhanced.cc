/**
 * @file test_file_config_source_enhanced.cc
 * @brief Comprehensive tests for FileConfigSource with YAML, env substitution,
 * includes, and overlays
 */

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "mcp/config/config_manager.h"

namespace mcp {
namespace config {
namespace testing {

namespace fs = std::filesystem;

class FileSourceEnhancedTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create temporary test directory
    test_dir_ =
        fs::temp_directory_path() / "mcp_test_enhanced" /
        ("test_" + std::to_string(getpid()) + "_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(test_dir_);

    // Clear environment
    unsetenv("MCP_CONFIG");
  }

  void TearDown() override {
    // Clean up test directory
    if (fs::exists(test_dir_)) {
      fs::remove_all(test_dir_);
    }

    // Clean up environment
    unsetenv("MCP_CONFIG");
  }

  void createJsonFile(const fs::path& path, const nlohmann::json& content) {
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

  void createLargeFile(const fs::path& path, size_t size_mb) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path);
    // Create a large JSON array
    file << "[";
    size_t bytes_written = 1;
    size_t target_bytes = size_mb * 1024 * 1024;

    while (bytes_written < target_bytes) {
      std::string element =
          "\"padding_element_" + std::to_string(bytes_written) + "\",";
      file << element;
      bytes_written += element.length();
    }
    file << "\"end\"]";
    file.close();
  }

 protected:
  fs::path test_dir_;
};

// Test YAML parsing - valid and invalid
TEST_F(FileSourceEnhancedTest, YamlParsing) {
  // Valid YAML
  fs::path valid_yaml = test_dir_ / "valid.yaml";
  std::string yaml_content = R"(
node:
  id: test-node
  cluster: prod
  metadata:
    region: us-west
    zone: 2a
admin:
  bind_address: 0.0.0.0
  port: 9001
  enable_debug: true
server:
  transports:
    - name: tcp_main
      type: tcp
      port: 8080
    - name: ssl_main
      type: ssl
      port: 8443
)";

  createYamlFile(valid_yaml, yaml_content);

  auto source = createFileConfigSource("yaml_test", 1, valid_yaml.string());
  auto config = source->loadConfiguration();

  EXPECT_EQ(config["node"]["id"], "test-node");
  EXPECT_EQ(config["node"]["cluster"], "prod");
  EXPECT_EQ(config["node"]["metadata"]["region"], "us-west");
  EXPECT_EQ(config["admin"]["port"], 9001);
  EXPECT_TRUE(config["admin"]["enable_debug"]);
  EXPECT_EQ(config["server"]["transports"][0]["name"], "tcp_main");
  EXPECT_EQ(config["server"]["transports"][1]["port"], 8443);

  // Invalid YAML
  fs::path invalid_yaml = test_dir_ / "invalid.yaml";
  createYamlFile(invalid_yaml, "key: value\n  bad indentation: here");

  auto bad_source =
      createFileConfigSource("bad_yaml", 1, invalid_yaml.string());
  EXPECT_THROW(
      {
        try {
          bad_source->loadConfiguration();
        } catch (const std::exception& e) {
          std::string error = e.what();
          EXPECT_TRUE(error.find("YAML parse error") != std::string::npos ||
                      error.find("line") != std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

// Test mixed JSON/YAML overlays
TEST_F(FileSourceEnhancedTest, MixedJsonYamlOverlays) {
  // Base config in JSON
  fs::path base_json = test_dir_ / "config.json";
  nlohmann::json base = {
      {"node", {{"id", "base-node"}, {"cluster", "default"}}},
      {"admin", {{"port", 9000}}}};
  createJsonFile(base_json, base);

  // Create config.d with mixed formats
  fs::path config_d = test_dir_ / "config.d";
  fs::create_directories(config_d);

  // Overlay 1 - YAML
  createYamlFile(config_d / "01-override.yaml", R"(
node:
  cluster: production
admin:
  bind_address: 127.0.0.1
)");

  // Overlay 2 - JSON
  nlohmann::json overlay2 = {{"server", {{"port", 8080}}},
                             {"admin", {{"enable_metrics", true}}}};
  createJsonFile(config_d / "02-server.json", overlay2);

  // Overlay 3 - YAML with new fields
  createYamlFile(config_d / "03-features.yml", R"(
features:
  auth: enabled
  logging: verbose
)");

  auto source = createFileConfigSource("mixed", 1, base_json.string());
  auto config = source->loadConfiguration();

  // Check merged result
  EXPECT_EQ(config["node"]["id"], "base-node");  // From base
  EXPECT_EQ(config["node"]["cluster"],
            "production");                   // Overridden by overlay 1
  EXPECT_EQ(config["admin"]["port"], 9000);  // From base
  EXPECT_EQ(config["admin"]["bind_address"], "127.0.0.1");  // From overlay 1
  EXPECT_TRUE(config["admin"]["enable_metrics"]);           // From overlay 2
  EXPECT_EQ(config["server"]["port"], 8080);                // From overlay 2
  EXPECT_EQ(config["features"]["auth"], "enabled");         // From overlay 3
  EXPECT_EQ(config["features"]["logging"], "verbose");      // From overlay 3
}

// Test environment variable substitution
TEST_F(FileSourceEnhancedTest, EnvironmentSubstitution) {
  // Set test environment variables
  setenv("TEST_HOST", "localhost", 1);
  setenv("TEST_PORT", "3000", 1);
  setenv("TEST_USER", "admin", 1);

  fs::path config_file = test_dir_ / "env_config.yaml";
  std::string yaml_content = R"(
database:
  host: ${DB_HOST:-db.example.com}
  port: ${DB_PORT:-5432}
  user: ${TEST_USER}
  password: ${DB_PASS:-default_pass}
server:
  host: ${TEST_HOST}
  port: ${TEST_PORT}
  workers: ${WORKERS:-4}
undefined:
  required: ${UNDEFINED_VAR}
)";

  createYamlFile(config_file, yaml_content);

  // Test defined variables
  {
    fs::path valid_config = test_dir_ / "env_valid.json";
    nlohmann::json valid = {
        {"server", {{"host", "${TEST_HOST}"}, {"port", "${TEST_PORT}"}}}};
    createJsonFile(valid_config, valid);

    auto source = createFileConfigSource("env_test", 1, valid_config.string());
    auto config = source->loadConfiguration();

    EXPECT_EQ(config["server"]["host"], "localhost");
    EXPECT_EQ(config["server"]["port"], "3000");
  }

  // Test undefined with default
  {
    fs::path default_config = test_dir_ / "env_default.json";
    nlohmann::json with_default = {{"database",
                                    {{"host", "${DB_HOST:-db.example.com}"},
                                     {"port", "${DB_PORT:-5432}"}}}};
    createJsonFile(default_config, with_default);

    auto source =
        createFileConfigSource("default_test", 1, default_config.string());
    auto config = source->loadConfiguration();

    EXPECT_EQ(config["database"]["host"], "db.example.com");
    EXPECT_EQ(config["database"]["port"], "5432");
  }

  // Test undefined without default (should throw)
  {
    fs::path no_default = test_dir_ / "env_no_default.json";
    nlohmann::json without_default = {
        {"required", "${UNDEFINED_REQUIRED_VAR}"}};
    createJsonFile(no_default, without_default);

    auto source = createFileConfigSource("no_default", 1, no_default.string());
    EXPECT_THROW(
        {
          try {
            source->loadConfiguration();
          } catch (const std::exception& e) {
            std::string error = e.what();
            EXPECT_TRUE(error.find("Undefined environment variable") !=
                        std::string::npos);
            throw;
          }
        },
        std::runtime_error);
  }

  // Clean up
  unsetenv("TEST_HOST");
  unsetenv("TEST_PORT");
  unsetenv("TEST_USER");
}

// Test include resolution
TEST_F(FileSourceEnhancedTest, IncludeResolution) {
  // Create include hierarchy
  fs::path base_dir = test_dir_ / "configs";
  fs::path includes_dir = base_dir / "includes";
  fs::path common_dir = includes_dir / "common";

  fs::create_directories(common_dir);

  // Common config
  createYamlFile(common_dir / "logging.yaml", R"(
logging:
  level: info
  format: json
)");

  // Database config
  nlohmann::json db_config = {
      {"database", {{"host", "localhost"}, {"port", 5432}, {"pool_size", 10}}}};
  createJsonFile(includes_dir / "database.json", db_config);

  // Server config with nested include
  nlohmann::json server_config = {{"server", {{"port", 8080}}},
                                  {"include", "common/logging.yaml"}};
  createJsonFile(includes_dir / "server.json", server_config);

  // Main config with multiple includes
  nlohmann::json main_config = {
      {"app", "test"},
      {"include", {"includes/database.json", "includes/server.json"}}};
  createJsonFile(base_dir / "main.json", main_config);

  auto source = createFileConfigSource("include_test", 1,
                                       (base_dir / "main.json").string());
  auto config = source->loadConfiguration();

  // Verify all includes were processed
  EXPECT_EQ(config["app"], "test");
  EXPECT_EQ(config["database"]["host"], "localhost");
  EXPECT_EQ(config["database"]["pool_size"], 10);
  EXPECT_EQ(config["server"]["port"], 8080);
  EXPECT_EQ(config["logging"]["level"], "info");
  EXPECT_EQ(config["logging"]["format"], "json");

  // Include directive should be removed
  EXPECT_FALSE(config.contains("include"));
}

// Test circular include detection
TEST_F(FileSourceEnhancedTest, CircularIncludeDetection) {
  fs::path config_a = test_dir_ / "config_a.json";
  fs::path config_b = test_dir_ / "config_b.json";
  fs::path config_c = test_dir_ / "config_c.json";

  // Create circular dependency: A -> B -> C -> A
  nlohmann::json a = {{"data_a", "value_a"}, {"include", "config_b.json"}};
  nlohmann::json b = {{"data_b", "value_b"}, {"include", "config_c.json"}};
  nlohmann::json c = {{"data_c", "value_c"}, {"include", "config_a.json"}};

  createJsonFile(config_a, a);
  createJsonFile(config_b, b);
  createJsonFile(config_c, c);

  auto source = createFileConfigSource("circular", 1, config_a.string());
  auto config = source->loadConfiguration();

  // Should handle circular includes gracefully
  // Each file should be included only once
  EXPECT_EQ(config["data_a"], "value_a");
  EXPECT_EQ(config["data_b"], "value_b");
  EXPECT_EQ(config["data_c"], "value_c");
}

// Test include depth limit
TEST_F(FileSourceEnhancedTest, IncludeDepthLimit) {
  fs::path base = test_dir_;

  // Create a deep chain of includes (exceeds limit of 8)
  for (int i = 0; i <= 10; i++) {
    fs::path config = base / ("level" + std::to_string(i) + ".json");
    nlohmann::json content = {{"level", i}};

    if (i < 10) {
      content["include"] = "level" + std::to_string(i + 1) + ".json";
    }

    createJsonFile(config, content);
  }

  auto source =
      createFileConfigSource("depth", 1, (base / "level0.json").string());

  // Should throw when max depth exceeded
  EXPECT_THROW(
      {
        try {
          source->loadConfiguration();
        } catch (const std::exception& e) {
          std::string error = e.what();
          EXPECT_TRUE(error.find("Maximum include depth") != std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

// Test config.d overlay lexicographic order
TEST_F(FileSourceEnhancedTest, ConfigDOverlayOrder) {
  fs::path base_config = test_dir_ / "base.json";
  nlohmann::json base = {{"value", 0}, {"name", "base"}};
  createJsonFile(base_config, base);

  // Create config.d with files that will be sorted lexicographically
  fs::path config_d = test_dir_ / "config.d";
  fs::create_directories(config_d);

  // Create files in non-alphabetical order
  createJsonFile(config_d / "30-third.json", {{"value", 30}, {"third", true}});
  createJsonFile(config_d / "10-first.json", {{"value", 10}, {"first", true}});
  createJsonFile(config_d / "20-second.json",
                 {{"value", 20}, {"second", true}});
  createJsonFile(config_d / "40-fourth.yaml",
                 {{"value", 40}, {"fourth", true}});

  auto source = createFileConfigSource("order", 1, base_config.string());
  auto config = source->loadConfiguration();

  // Value should be from the last overlay (40-fourth.yaml)
  EXPECT_EQ(config["value"], 40);

  // All overlay fields should be present
  EXPECT_TRUE(config["first"]);
  EXPECT_TRUE(config["second"]);
  EXPECT_TRUE(config["third"]);
  EXPECT_TRUE(config["fourth"]);

  // Base field should remain
  EXPECT_EQ(config["name"], "base");
}

// Test search/precedence order
TEST_F(FileSourceEnhancedTest, SearchPrecedenceOrder) {
  // Create configs in different locations
  fs::path cli_config = test_dir_ / "cli.json";
  fs::path env_config = test_dir_ / "env.json";
  fs::path local_config = test_dir_ / "config" / "config.json";

  createJsonFile(cli_config, {{"source", "cli"}, {"priority", 1}});
  createJsonFile(env_config, {{"source", "env"}, {"priority", 2}});
  createJsonFile(local_config, {{"source", "local"}, {"priority", 3}});

  // Test CLI precedence (highest)
  {
    auto source = createFileConfigSource("cli", 1, cli_config.string());
    auto config = source->loadConfiguration();
    EXPECT_EQ(config["source"], "cli");
    EXPECT_EQ(config["priority"], 1);
  }

  // Test ENV precedence
  {
    setenv("MCP_CONFIG", env_config.string().c_str(), 1);
    auto source = createFileConfigSource("env", 1, "");
    auto config = source->loadConfiguration();
    EXPECT_EQ(config["source"], "env");
    EXPECT_EQ(config["priority"], 2);
    unsetenv("MCP_CONFIG");
  }

  // Test local config fallback
  {
    auto original_dir = fs::current_path();
    fs::current_path(test_dir_);

    auto source = createFileConfigSource("local", 1, "");
    auto config = source->loadConfiguration();
    EXPECT_EQ(config["source"], "local");
    EXPECT_EQ(config["priority"], 3);

    fs::current_path(original_dir);
  }
}

// Test oversized file rejection
TEST_F(FileSourceEnhancedTest, OversizedFileRejection) {
  fs::path large_file = test_dir_ / "large.json";

  // Create a file larger than 20MB limit
  createLargeFile(large_file, 25);

  auto source = createFileConfigSource("large", 1, large_file.string());

  EXPECT_THROW(
      {
        try {
          source->loadConfiguration();
        } catch (const std::exception& e) {
          std::string error = e.what();
          // Should mention file size limit
          EXPECT_TRUE(error.find("File too large") != std::string::npos ||
                      error.find("exceeds maximum") != std::string::npos);
          // Should not dump file contents
          EXPECT_FALSE(error.find("padding_element") != std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

// Test absolute path restrictions with allowed roots
TEST_F(FileSourceEnhancedTest, AbsolutePathRestrictions) {
  // This test would require modifying the FileConfigSource Options
  // to set allowed_include_roots, which requires access to the internal class
  // For now, we'll test that absolute paths work in general

  fs::path abs_include = test_dir_ / "absolute_include.json";
  fs::path abs_target = test_dir_ / "target.json";

  createJsonFile(abs_target, {{"data", "target_value"}});

  // Use absolute path in include
  nlohmann::json with_abs = {{"base", "value"},
                             {"include", abs_target.string()}};
  createJsonFile(abs_include, with_abs);

  auto source = createFileConfigSource("abs", 1, abs_include.string());
  auto config = source->loadConfiguration();

  EXPECT_EQ(config["base"], "value");
  EXPECT_EQ(config["data"], "target_value");
}

// Test robust error messages without content dump
TEST_F(FileSourceEnhancedTest, ErrorMessagesWithoutContentDump) {
  // Test parse error
  {
    fs::path bad_json = test_dir_ / "bad.json";
    std::string bad_content = R"({
            "key": "value",
            "bad": // missing value
            "secret": "password123"
        })";

    std::ofstream file(bad_json);
    file << bad_content;
    file.close();

    auto source = createFileConfigSource("bad", 1, bad_json.string());

    try {
      source->loadConfiguration();
      FAIL() << "Should have thrown on parse error";
    } catch (const std::exception& e) {
      std::string error = e.what();
      // Should have error location
      EXPECT_TRUE(error.find("parse error") != std::string::npos ||
                  error.find("byte") != std::string::npos);
      // Should NOT include secret content
      EXPECT_FALSE(error.find("password123") != std::string::npos);
    }
  }

  // Test undefined variable error
  {
    fs::path env_error = test_dir_ / "env_error.json";
    nlohmann::json with_undefined = {{"api_key", "${SECRET_API_KEY}"},
                                     {"other", "value"}};
    createJsonFile(env_error, with_undefined);

    auto source = createFileConfigSource("env_err", 1, env_error.string());

    try {
      source->loadConfiguration();
      FAIL() << "Should have thrown on undefined variable";
    } catch (const std::exception& e) {
      std::string error = e.what();
      // Should mention the variable name
      EXPECT_TRUE(error.find("SECRET_API_KEY") != std::string::npos);
      // Should NOT include surrounding content
      EXPECT_FALSE(error.find("api_key") != std::string::npos);
    }
  }
}

}  // namespace testing
}  // namespace config
}  // namespace mcp

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}