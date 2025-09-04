/**
 * @file test_file_source_interface.cc
 * @brief Tests for FileConfigSource interface compliance with ConfigSource
 */

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "mcp/config/config_manager.h"

namespace mcp {
namespace config {
namespace testing {

class FileSourceInterfaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary test directory using POSIX
        std::string base_name = "mcp_interface_test_" + 
                               std::to_string(getpid()) + "_" +
                               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        
        const char* tmp_dir = getenv("TMPDIR");
        if (!tmp_dir) tmp_dir = "/tmp";
        
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
        if (!dir) return;
        
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
    auto source = createFileConfigSource("test", ConfigSource::Priority::FILE, "");
    
    EXPECT_EQ("test", source->getName());
    EXPECT_EQ(ConfigSource::Priority::FILE, source->getPriority());
    
    // Initially should not have configuration (no file exists)
    EXPECT_FALSE(source->hasConfiguration());
    
    // hasChanged() should not crash
    EXPECT_FALSE(source->hasChanged());
    
    // getLastModified() should not crash
    auto last_modified = source->getLastModified();
    EXPECT_GT(last_modified.time_since_epoch().count(), 0);
    
    // loadConfiguration() should return empty when no file exists
    auto config = source->loadConfiguration();
    EXPECT_TRUE(config.empty());
}

TEST_F(FileSourceInterfaceTest, HasConfigurationWithDiscovery) {
    // Test configuration file discovery
    {
        // Test with explicit file that doesn't exist
        auto source = createFileConfigSource("test", ConfigSource::Priority::FILE, 
                                            test_dir_ + "/nonexistent.json");
        EXPECT_FALSE(source->hasConfiguration());
    }
    
    {
        // Test with explicit file that exists
        std::string config_file = test_dir_ + "/exists.json";
        createJsonFile(config_file, nlohmann::json{{"version", "1.0"}});
        
        auto source = createFileConfigSource("test", ConfigSource::Priority::FILE, config_file);
        EXPECT_TRUE(source->hasConfiguration());
    }
    
    {
        // Test with environment variable
        std::string env_config = test_dir_ + "/env.json";
        createJsonFile(env_config, nlohmann::json{{"version", "1.0"}});
        
        setenv("MCP_CONFIG", env_config.c_str(), 1);
        auto source = createFileConfigSource("test", ConfigSource::Priority::FILE, "");
        EXPECT_TRUE(source->hasConfiguration());
        unsetenv("MCP_CONFIG");
    }
    
    {
        // Test with current directory discovery
        char* original_cwd = getcwd(nullptr, 0);
        chdir(test_dir_.c_str());
        
        std::string local_config = test_dir_ + "/config/config.json";
        createJsonFile(local_config, nlohmann::json{{"version", "1.0"}});
        
        auto source = createFileConfigSource("test", ConfigSource::Priority::FILE, "");
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
    nlohmann::json include_data = {
        {"node", {
            {"id", "included-node"}
        }}
    };
    createJsonFile(include_config, include_data);
    
    // Create base config with include
    nlohmann::json base_data = {
        {"version", "1.0"},
        {"include", include_config}
    };
    createJsonFile(base_config, base_data);
    
    auto source = createFileConfigSource("test", ConfigSource::Priority::FILE, base_config);
    EXPECT_TRUE(source->hasConfiguration());
    
    auto config = source->loadConfiguration();
    EXPECT_FALSE(config.empty());
    EXPECT_EQ("1.0", config["version"]);
    EXPECT_EQ("included-node", config["node"]["id"]);
}

TEST_F(FileSourceInterfaceTest, ChangeDetection) {
    std::string config_file = test_dir_ + "/config.json";
    createJsonFile(config_file, nlohmann::json{{"version", "1.0"}});
    
    auto source = createFileConfigSource("test", ConfigSource::Priority::FILE, config_file);
    
    // Initially no changes
    EXPECT_FALSE(source->hasChanged());
    
    // Get initial last modified time
    auto initial_time = source->getLastModified();
    
    // Load configuration (establishes baseline)
    source->loadConfiguration();
    EXPECT_FALSE(source->hasChanged());
    
    // Wait a bit and modify file
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    createJsonFile(config_file, nlohmann::json{{"version", "2.0"}});
    
    // Should detect change
    EXPECT_TRUE(source->hasChanged());
    
    // Last modified time should be updated
    auto new_time = source->getLastModified();
    EXPECT_GT(new_time, initial_time);
}

TEST_F(FileSourceInterfaceTest, YamlSupport) {
    std::string yaml_config = test_dir_ + "/config.yaml";
    std::string yaml_content = R"(
version: "1.0"
node:
  id: "yaml-node"
  cluster: "yaml-cluster"
admin:
  enabled: true
  port: 9902
)";
    
    createYamlFile(yaml_config, yaml_content);
    
    auto source = createFileConfigSource("test", ConfigSource::Priority::FILE, yaml_config);
    EXPECT_TRUE(source->hasConfiguration());
    
    auto config = source->loadConfiguration();
    EXPECT_FALSE(config.empty());
    EXPECT_EQ("1.0", config["version"]);
    EXPECT_EQ("yaml-node", config["node"]["id"]);
    EXPECT_EQ("yaml-cluster", config["node"]["cluster"]);
    EXPECT_EQ(true, config["admin"]["enabled"]);
    EXPECT_EQ(9902, config["admin"]["port"]);
}

}  // namespace testing
}  // namespace config
}  // namespace mcp