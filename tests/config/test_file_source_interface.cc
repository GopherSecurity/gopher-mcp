/**
 * @file test_file_source_interface.cc
 * @brief Tests for FileConfigSource interface compliance with ConfigSource
 */

#include <chrono>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <thread>
#include <unistd.h>

#include <gtest/gtest.h>
#include <sys/stat.h>

#include "mcp/config/config_manager.h"
#include "mcp/json/json_bridge.h"

#include "test_json_helpers.h"

namespace mcp {
namespace config {
namespace testing {

using mcp::json::JsonValue;
using test::boolean;
using test::makeJsonArray;
using test::makeJsonObject;
using test::num;
using test::str;

class FileSourceInterfaceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create temporary test directory using POSIX
    std::string base_name =
        "mcp_interface_test_" + std::to_string(getpid()) + "_" +
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());

    const char* tmp_dir = getenv("TMPDIR");
    if (!tmp_dir)
      tmp_dir = "/tmp";

    test_dir_ = std::string(tmp_dir) + "/" + base_name;
    createDirectoryRecursive(test_dir_);

    // Clear environment
    unsetenv("MCP_CONFIG");
  }

  void TearDown() override {
    // Clean up test directory
    if (directoryExists(test_dir_)) {
      removeDirectoryRecursive(test_dir_);
    }
    unsetenv("MCP_CONFIG");
  }

  void createJsonFile(const std::string& path, const JsonValue& content) {
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

  void touchFile(const std::string& path) {
    createDirectoryRecursive(getParentDirectory(path));
    std::ofstream file(path);
    file << "";
    file.close();
  }

 private:
  bool directoryExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
  }

  bool createDirectoryRecursive(const std::string& path) {
    if (path.empty() || directoryExists(path)) {
      return true;
    }

    std::string parent = getParentDirectory(path);
    if (!parent.empty() && parent != path) {
      if (!createDirectoryRecursive(parent)) {
        return false;
      }
    }

    return mkdir(path.c_str(), 0755) == 0 || directoryExists(path);
  }

  std::string getParentDirectory(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
      return ".";
    }
    if (pos == 0) {
      return "/";
    }
    return path.substr(0, pos);
  }

  void removeDirectoryRecursive(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (!dir)
      return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      std::string full_path = path + "/" + entry->d_name;
      struct stat st;
      if (stat(full_path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
          removeDirectoryRecursive(full_path);
        } else {
          unlink(full_path.c_str());
        }
      }
    }
    closedir(dir);
    rmdir(path.c_str());
  }

 protected:
  std::string test_dir_;
};

TEST_F(FileSourceInterfaceTest, ImplementsConfigSourceInterface) {
  // Test that FileConfigSource properly implements ConfigSource interface
  auto source =
      createFileConfigSource("test", ConfigSource::Priority::FILE, "");

  EXPECT_EQ("test", source->getName());
  EXPECT_EQ(ConfigSource::Priority::FILE, source->getPriority());

  // Initially may or may not have configuration (depends on discovery)
  // hasConfiguration() uses file discovery which may find system configs

  // hasChanged() should not crash
  EXPECT_FALSE(source->hasChanged());

  // getLastModified() returns epoch (0) when no config has been loaded yet
  auto last_modified = source->getLastModified();
  EXPECT_EQ(last_modified.time_since_epoch().count(), 0);

  // loadConfiguration() may return empty or found config depending on discovery
  auto config = source->loadConfiguration();
  // Just verify it doesn't crash - result depends on environment
}

TEST_F(FileSourceInterfaceTest, HasConfigurationWithDiscovery) {
  // Test configuration file discovery
  {
    // When explicit path is set to a nonexistent file, hasConfiguration() may
    // return true (path is set) but loadConfiguration will fail
    auto source = createFileConfigSource("test", ConfigSource::Priority::FILE,
                                         test_dir_ + "/nonexistent.json");
    // Loading will fail or return empty for nonexistent file
    try {
      auto config = source->loadConfiguration();
      EXPECT_TRUE(config.empty());
    } catch (const std::exception&) {
      // Exception is acceptable for nonexistent file
      SUCCEED();
    }
  }

  {
    // Test with explicit file that exists
    std::string config_file = test_dir_ + "/exists.json";
    createJsonFile(config_file, makeJsonObject({{"version", str("1.0")}}));

    auto source = createFileConfigSource("test", ConfigSource::Priority::FILE,
                                         config_file);
    EXPECT_TRUE(source->hasConfiguration());
  }

  {
    // Test with environment variable
    std::string env_config = test_dir_ + "/env.json";
    createJsonFile(env_config, makeJsonObject({{"version", str("1.0")}}));

    setenv("MCP_CONFIG", env_config.c_str(), 1);
    auto source =
        createFileConfigSource("test", ConfigSource::Priority::FILE, "");
    EXPECT_TRUE(source->hasConfiguration());
    unsetenv("MCP_CONFIG");
  }

  {
    // Test with current directory discovery
    char* original_cwd = getcwd(nullptr, 0);
    chdir(test_dir_.c_str());

    std::string local_config = test_dir_ + "/config/config.json";
    createJsonFile(local_config, makeJsonObject({{"version", str("1.0")}}));

    auto source =
        createFileConfigSource("test", ConfigSource::Priority::FILE, "");
    EXPECT_TRUE(source->hasConfiguration());

    chdir(original_cwd);
    free(original_cwd);
  }
}

TEST_F(FileSourceInterfaceTest, LoadConfigurationWithIncludes) {
  // Test configuration loading with include support
  std::string base_config = test_dir_ + "/config.json";
  std::string include_config = test_dir_ + "/include.json";

  // Create include file
  JsonValue include_data = makeJsonObject(
      {{"node", makeJsonObject({{"id", str("included-node")}})}});
  createJsonFile(include_config, include_data);

  // Create base config with include
  JsonValue base_data = makeJsonObject(
      {{"version", str("1.0")}, {"include", str(include_config)}});
  createJsonFile(base_config, base_data);

  auto source =
      createFileConfigSource("test", ConfigSource::Priority::FILE, base_config);
  EXPECT_TRUE(source->hasConfiguration());

  auto config = source->loadConfiguration();
  EXPECT_FALSE(config.empty());
  ASSERT_TRUE(config.isObject());
  ASSERT_TRUE(config.contains("version"));
  EXPECT_EQ(std::string("1.0"), config["version"].getString());
  ASSERT_TRUE(config.contains("node"));
  ASSERT_TRUE(config["node"].isObject());
  ASSERT_TRUE(config["node"].contains("id"));
  EXPECT_EQ(std::string("included-node"), config["node"]["id"].getString());
}

TEST_F(FileSourceInterfaceTest, ChangeDetection) {
  std::string config_file = test_dir_ + "/config.json";
  createJsonFile(config_file, makeJsonObject({{"version", str("1.0")}}));

  auto source =
      createFileConfigSource("test", ConfigSource::Priority::FILE, config_file);

  // Initially no changes (no config loaded yet)
  EXPECT_FALSE(source->hasChanged());

  // Load configuration (establishes baseline)
  source->loadConfiguration();
  EXPECT_FALSE(source->hasChanged());

  // Get last modified time after loading
  auto initial_time = source->getLastModified();

  // Wait longer to ensure filesystem mtime granularity is satisfied
  // Some filesystems have 1-second mtime granularity
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  createJsonFile(config_file, makeJsonObject({{"version", str("2.0")}}));

  // Should detect change (file mtime is now newer than last_modified_)
  EXPECT_TRUE(source->hasChanged());

  // Reload to update last_modified_
  source->loadConfiguration();
  auto new_time = source->getLastModified();
  EXPECT_GT(new_time, initial_time);
}

TEST_F(FileSourceInterfaceTest, YamlSupport) {
  std::string yaml_config = test_dir_ + "/config.yaml";
  std::string yaml_content = R"(
version: 1.0
node:
  id: "yaml-node"
  cluster: "yaml-cluster"
admin:
  enabled: true
  port: 9902
)";

  createYamlFile(yaml_config, yaml_content);

  auto source =
      createFileConfigSource("test", ConfigSource::Priority::FILE, yaml_config);
  EXPECT_TRUE(source->hasConfiguration());

  auto config = source->loadConfiguration();
  EXPECT_FALSE(config.empty());
  ASSERT_TRUE(config.isObject());
  ASSERT_TRUE(config.contains("version"));
  EXPECT_EQ(1.0, config["version"].getFloat());
  ASSERT_TRUE(config.contains("node"));
  ASSERT_TRUE(config["node"].isObject());
  EXPECT_EQ(std::string("yaml-node"), config["node"]["id"].getString());
  EXPECT_EQ(std::string("yaml-cluster"), config["node"]["cluster"].getString());
  ASSERT_TRUE(config.contains("admin"));
  ASSERT_TRUE(config["admin"].isObject());
  EXPECT_TRUE(config["admin"]["enabled"].getBool());
  EXPECT_EQ(9902, config["admin"]["port"].getInt());
}

}  // namespace testing
}  // namespace config
}  // namespace mcp
