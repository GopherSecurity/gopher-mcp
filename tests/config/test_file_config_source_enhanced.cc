/**
 * @file test_file_config_source_enhanced.cc
 * @brief Comprehensive tests for FileConfigSource with YAML, env substitution,
 * includes, and overlays
 */

#include <cstdlib>
#include <fstream>
#include <regex>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "mcp/config/config_manager.h"
#include "test_filesystem_utils.h"

namespace mcp {
namespace config {
namespace testing {

using namespace fs_utils;

class FileSourceEnhancedTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create temporary test directory
    test_dir_ = createUniqueTempDirectory("mcp_test_enhanced");

    // Clear environment
    unsetenv("MCP_CONFIG");
  }

  void TearDown() override {
    // Clean up test directory
    if (pathExists(test_dir_)) {
      removeDirectoryRecursive(test_dir_);
    }

    // Clean up environment
    unsetenv("MCP_CONFIG");
  }

  void createJsonFile(const std::string& path, const nlohmann::json& content) {
    createDirectoryRecursive(getParentDirectory(path));
    std::ofstream file(path);
    file << content.dump(2);
    file.close();
  }

  void createYamlFile(const std::string& path, const std::string& content) {
    createDirectoryRecursive(getParentDirectory(path));
    std::ofstream file(path);
    file << content;
    file.close();
  }

  void createLargeFile(const std::string& path, size_t size_mb) {
    createDirectoryRecursive(getParentDirectory(path));
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
  std::string test_dir_;
};

// Test YAML parsing - valid and invalid
TEST_F(FileSourceEnhancedTest, YamlParsing) {
  // Valid YAML
  std::string valid_yaml = joinPath(test_dir_, "valid.yaml");
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

  auto source = createFileConfigSource("yaml_test", 1, valid_yaml);
  auto config = source->loadConfiguration();

  EXPECT_EQ(std::string("test-node"), config["node"]["id"].getString());
  EXPECT_EQ(std::string("prod"), config["node"]["cluster"].getString());
  EXPECT_EQ(std::string("us-west"),
            config["node"]["metadata"]["region"].getString());
  EXPECT_EQ(9001, config["admin"]["port"].getInt());
  EXPECT_TRUE(config["admin"]["enable_debug"].getBool());
  EXPECT_EQ(std::string("tcp_main"),
            config["server"]["transports"][0]["name"].getString());
  EXPECT_EQ(8443, config["server"]["transports"][1]["port"].getInt());

  // Invalid YAML
  std::string invalid_yaml = joinPath(test_dir_, "invalid.yaml");
  createYamlFile(invalid_yaml, "key: value\n  bad indentation: here");

  auto bad_source =
      createFileConfigSource("bad_yaml", 1, invalid_yaml);
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
  std::string base_json = joinPath(test_dir_, "config.json");
  nlohmann::json base = {
      {"node", {{"id", "base-node"}, {"cluster", "default"}}},
      {"admin", {{"port", 9000}}}};
  createJsonFile(base_json, base);

  // Create config.d with mixed formats
  std::string config_d = joinPath(test_dir_, "config.d");
  createDirectoryRecursive(config_d);

  // Overlay 1 - YAML
  createYamlFile(joinPath(config_d, "01-override.yaml"), R"(
node:
  cluster: production
admin:
  bind_address: 127.0.0.1
)");

  // Overlay 2 - JSON
  nlohmann::json overlay2 = {{"server", {{"port", 8080}}},
                             {"admin", {{"enable_metrics", true}}}};
  createJsonFile(joinPath(config_d, "02-server.json"), overlay2);

  // Overlay 3 - YAML with new fields
  createYamlFile(joinPath(config_d, "03-features.yml"), R"(
features:
  auth: enabled
  logging: verbose
)");

  auto source = createFileConfigSource("mixed", 1, base_json);
  auto config = source->loadConfiguration();

  // Check merged result
  EXPECT_EQ(std::string("base-node"), config["node"]["id"].getString());
  EXPECT_EQ(std::string("production"), config["node"]["cluster"].getString());
  EXPECT_EQ(9000, config["admin"]["port"].getInt());
  EXPECT_EQ(std::string("127.0.0.1"),
            config["admin"]["bind_address"].getString());
  EXPECT_TRUE(config["admin"]["enable_metrics"].getBool());
  EXPECT_EQ(8080, config["server"]["port"].getInt());
  EXPECT_EQ(std::string("enabled"), config["features"]["auth"].getString());
  EXPECT_EQ(std::string("verbose"), config["features"]["logging"].getString());
}

// Test environment variable substitution
TEST_F(FileSourceEnhancedTest, EnvironmentSubstitution) {
  // Set test environment variables
  setenv("TEST_HOST", "localhost", 1);
  setenv("TEST_PORT", "3000", 1);
  setenv("TEST_USER", "admin", 1);

  std::string config_file = joinPath(test_dir_, "env_config.yaml");
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
    std::string valid_config = joinPath(test_dir_, "env_valid.json");
    nlohmann::json valid = {
        {"server", {{"host", "${TEST_HOST}"}, {"port", "${TEST_PORT}"}}}};
    createJsonFile(valid_config, valid);

    auto source = createFileConfigSource("env_test", 1, valid_config);
    auto config = source->loadConfiguration();

    EXPECT_EQ(std::string("localhost"), config["server"]["host"].getString());
    EXPECT_EQ(std::string("3000"), config["server"]["port"].getString());
  }

  // Test undefined with default
  {
    std::string default_config = joinPath(test_dir_, "env_default.json");
    nlohmann::json with_default = {{"database",
                                    {{"host", "${DB_HOST:-db.example.com}"},
                                     {"port", "${DB_PORT:-5432}"}}}};
    createJsonFile(default_config, with_default);

    auto source =
        createFileConfigSource("default_test", 1, default_config);
    auto config = source->loadConfiguration();

    EXPECT_EQ(std::string("db.example.com"),
              config["database"]["host"].getString());
    EXPECT_EQ(std::string("5432"), config["database"]["port"].getString());
  }

  // Test undefined without default (should throw)
  {
    std::string no_default = joinPath(test_dir_, "env_no_default.json");
    nlohmann::json without_default = {
        {"required", "${UNDEFINED_REQUIRED_VAR}"}};
    createJsonFile(no_default, without_default);

    auto source = createFileConfigSource("no_default", 1, no_default);
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
  std::string base_dir = joinPath(test_dir_, "configs");
  std::string includes_dir = joinPath(base_dir, "includes");
  std::string common_dir = joinPath(includes_dir, "common");

  createDirectoryRecursive(common_dir);

  // Common config
  createYamlFile(joinPath(common_dir, "logging.yaml"), R"(
logging:
  level: info
  format: json
)");

  // Database config
  nlohmann::json db_config = {
      {"database", {{"host", "localhost"}, {"port", 5432}, {"pool_size", 10}}}};
  createJsonFile(joinPath(includes_dir, "database.json"), db_config);

  // Server config with nested include
  nlohmann::json server_config = {{"server", {{"port", 8080}}},
                                  {"include", "common/logging.yaml"}};
  createJsonFile(joinPath(includes_dir, "server.json"), server_config);

  // Main config with multiple includes
  nlohmann::json main_config = {
      {"app", "test"},
      {"include", {"includes/database.json", "includes/server.json"}}};
  createJsonFile(joinPath(base_dir, "main.json"), main_config);

  auto source = createFileConfigSource("include_test", 1,
                                       joinPath(base_dir, "main.json"));
  auto config = source->loadConfiguration();

  // Verify all includes were processed
  EXPECT_EQ(std::string("test"), config["app"].getString());
  EXPECT_EQ(std::string("localhost"), config["database"]["host"].getString());
  EXPECT_EQ(10, config["database"]["pool_size"].getInt());
  EXPECT_EQ(8080, config["server"]["port"].getInt());
  EXPECT_EQ(std::string("info"), config["logging"]["level"].getString());
  EXPECT_EQ(std::string("json"), config["logging"]["format"].getString());

  // Include directive should be removed
  EXPECT_FALSE(config.contains("include"));
}

// Test circular include detection
TEST_F(FileSourceEnhancedTest, CircularIncludeDetection) {
  std::string config_a = joinPath(test_dir_, "config_a.json");
  std::string config_b = joinPath(test_dir_, "config_b.json");
  std::string config_c = joinPath(test_dir_, "config_c.json");

  // Create circular dependency: A -> B -> C -> A
  nlohmann::json a = {{"data_a", "value_a"}, {"include", "config_b.json"}};
  nlohmann::json b = {{"data_b", "value_b"}, {"include", "config_c.json"}};
  nlohmann::json c = {{"data_c", "value_c"}, {"include", "config_a.json"}};

  createJsonFile(config_a, a);
  createJsonFile(config_b, b);
  createJsonFile(config_c, c);

  auto source = createFileConfigSource("circular", 1, config_a);
  auto config = source->loadConfiguration();

  // Should handle circular includes gracefully
  // Each file should be included only once
  EXPECT_EQ(std::string("value_a"), config["data_a"].getString());
  EXPECT_EQ(std::string("value_b"), config["data_b"].getString());
  EXPECT_EQ(std::string("value_c"), config["data_c"].getString());
}

// Test include depth limit
TEST_F(FileSourceEnhancedTest, IncludeDepthLimit) {
  std::string base = test_dir_;

  // Create a deep chain of includes (exceeds limit of 8)
  for (int i = 0; i <= 10; i++) {
    std::string config = joinPath(base, "level" + std::to_string(i) + ".json");
    nlohmann::json content = {{"level", i}};

    if (i < 10) {
      content["include"] = "level" + std::to_string(i + 1) + ".json";
    }

    createJsonFile(config, content);
  }

  auto source =
      createFileConfigSource("depth", 1, joinPath(base, "level0.json"));

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
  std::string base_config = joinPath(test_dir_, "base.json");
  nlohmann::json base = {{"value", 0}, {"name", "base"}};
  createJsonFile(base_config, base);

  // Create config.d with files that will be sorted lexicographically
  std::string config_d = joinPath(test_dir_, "config.d");
  createDirectoryRecursive(config_d);

  // Create files in non-alphabetical order
  createJsonFile(joinPath(config_d, "30-third.json"), {{"value", 30}, {"third", true}});
  createJsonFile(joinPath(config_d, "10-first.json"), {{"value", 10}, {"first", true}});
  createJsonFile(joinPath(config_d, "20-second.json"),
                 {{"value", 20}, {"second", true}});
  createJsonFile(joinPath(config_d, "40-fourth.yaml"),
                 {{"value", 40}, {"fourth", true}});

  auto source = createFileConfigSource("order", 1, base_config);
  auto config = source->loadConfiguration();

  // Value should be from the last overlay (40-fourth.yaml)
  EXPECT_EQ(40, config["value"].getInt());

  // All overlay fields should be present
  EXPECT_TRUE(config["first"].getBool());
  EXPECT_TRUE(config["second"].getBool());
  EXPECT_TRUE(config["third"].getBool());
  EXPECT_TRUE(config["fourth"].getBool());

  // Base field should remain
  EXPECT_EQ(std::string("base"), config["name"].getString());
}

// Test search/precedence order
TEST_F(FileSourceEnhancedTest, SearchPrecedenceOrder) {
  // Create configs in different locations
  std::string cli_config = joinPath(test_dir_, "cli.json");
  std::string env_config = joinPath(test_dir_, "env.json");
  std::string local_config = joinPath(joinPath(test_dir_, "config"), "config.json");

  createJsonFile(cli_config, {{"source", "cli"}, {"priority", 1}});
  createJsonFile(env_config, {{"source", "env"}, {"priority", 2}});
  createJsonFile(local_config, {{"source", "local"}, {"priority", 3}});

  // Test CLI precedence (highest)
  {
    auto source = createFileConfigSource("cli", 1, cli_config);
    auto config = source->loadConfiguration();
    EXPECT_EQ(std::string("cli"), config["source"].getString());
    EXPECT_EQ(1, config["priority"].getInt());
  }

  // Test ENV precedence
  {
    setenv("MCP_CONFIG", env_config.c_str(), 1);
    auto source = createFileConfigSource("env", 1, "");
    auto config = source->loadConfiguration();
    EXPECT_EQ(std::string("env"), config["source"].getString());
    EXPECT_EQ(2, config["priority"].getInt());
    unsetenv("MCP_CONFIG");
  }

  // Test local config fallback
  {
    WorkingDirectoryGuard dir_guard(test_dir_);

    auto source = createFileConfigSource("local", 1, "");
    auto config = source->loadConfiguration();
    EXPECT_EQ(std::string("local"), config["source"].getString());
    EXPECT_EQ(3, config["priority"].getInt());
  }
}

// Test oversized file rejection
TEST_F(FileSourceEnhancedTest, OversizedFileRejection) {
  std::string large_file = joinPath(test_dir_, "large.json");

  // Create a file larger than 20MB limit
  createLargeFile(large_file, 25);

  auto source = createFileConfigSource("large", 1, large_file);

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

  std::string abs_include = joinPath(test_dir_, "absolute_include.json");
  std::string abs_target = joinPath(test_dir_, "target.json");

  createJsonFile(abs_target, {{"data", "target_value"}});

  // Use absolute path in include
  nlohmann::json with_abs = {{"base", "value"},
                             {"include", abs_target}};
  createJsonFile(abs_include, with_abs);

  auto source = createFileConfigSource("abs", 1, abs_include);
  auto config = source->loadConfiguration();

  EXPECT_EQ(std::string("value"), config["base"].getString());
  EXPECT_EQ(std::string("target_value"), config["data"].getString());
}

// Test robust error messages without content dump
TEST_F(FileSourceEnhancedTest, ErrorMessagesWithoutContentDump) {
  // Test parse error
  {
    std::string bad_json = joinPath(test_dir_, "bad.json");
    std::string bad_content = R"({
            "key": "value",
            "bad": // missing value
            "secret": "password123"
        })";

    std::ofstream file(bad_json);
    file << bad_content;
    file.close();

    auto source = createFileConfigSource("bad", 1, bad_json);

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
    std::string env_error = joinPath(test_dir_, "env_error.json");
    nlohmann::json with_undefined = {{"api_key", "${SECRET_API_KEY}"},
                                     {"other", "value"}};
    createJsonFile(env_error, with_undefined);

    auto source = createFileConfigSource("env_err", 1, env_error);

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
