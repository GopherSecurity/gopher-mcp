#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

// Platform-specific includes for filesystem operations
#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <direct.h>
#include <windows.h>
#endif

#include "mcp/config/config_manager.h"

namespace mcp {
namespace config {
namespace test {

class FileSourceCompleteTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create test directory
    test_dir_ = std::string("test_config_") + std::to_string(getpid());
    mkdir(test_dir_.c_str(), 0755);
    
    // Save original working directory
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
      original_dir_ = cwd;
    }
  }

  void TearDown() override {
    // Restore working directory
    if (!original_dir_.empty()) {
      chdir(original_dir_.c_str());
    }
    
    // Clean up test directory
    removeDirectory(test_dir_);
    
    // Clean up environment variables
    unsetenv("MCP_CONFIG");
    unsetenv("TEST_HOST");
    unsetenv("TEST_PORT");
    unsetenv("TEST_MODE");
  }

  void createFile(const std::string& path, const std::string& content) {
    // Create parent directories if needed
    size_t last_slash = path.find_last_of('/');
    if (last_slash != std::string::npos) {
      std::string dir = path.substr(0, last_slash);
      createDirectory(dir);
    }
    
    std::ofstream file(path);
    file << content;
    file.close();
  }

  void createDirectory(const std::string& path) {
    size_t pos = 0;
    while ((pos = path.find('/', pos)) != std::string::npos) {
      std::string subdir = path.substr(0, pos);
      if (!subdir.empty()) {
        mkdir(subdir.c_str(), 0755);
      }
      pos++;
    }
    mkdir(path.c_str(), 0755);
  }

  void removeDirectory(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (dir) {
      struct dirent* entry;
      while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name != "." && name != "..") {
          std::string full_path = path + "/" + name;
          struct stat st;
          if (stat(full_path.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
              removeDirectory(full_path);
            } else {
              unlink(full_path.c_str());
            }
          }
        }
      }
      closedir(dir);
      rmdir(path.c_str());
    }
  }

  std::string test_dir_;
  std::string original_dir_;
};

// Test YAML parsing with valid and invalid files
TEST_F(FileSourceCompleteTest, YamlParsing) {
  // Valid YAML
  std::string valid_yaml = R"(
server:
  host: localhost
  port: 8080
  ssl:
    enabled: true
    cert: /path/to/cert.pem
database:
  connections: 10
  timeout: 30
features:
  - auth
  - logging
  - metrics
)";

  createFile(test_dir_ + "/config.yaml", valid_yaml);
  
  auto source = createFileConfigSource("test", 1, test_dir_ + "/config.yaml");
  auto config = source->loadConfiguration();
  
  EXPECT_EQ(config["server"]["host"], "localhost");
  EXPECT_EQ(config["server"]["port"], 8080);
  EXPECT_EQ(config["server"]["ssl"]["enabled"], true);
  EXPECT_EQ(config["database"]["connections"], 10);
  EXPECT_EQ(config["features"][0], "auth");
  EXPECT_EQ(config["features"][2], "metrics");
}

TEST_F(FileSourceCompleteTest, InvalidYamlParsing) {
  // Invalid YAML with syntax error
  std::string invalid_yaml = R"(
server:
  host: localhost
  port: 8080
  ssl:
    enabled: true
    cert: /path/to/cert.pem
  invalid_indent
database:
  connections: 10
)";

  createFile(test_dir_ + "/bad.yaml", invalid_yaml);
  
  auto source = createFileConfigSource("test", 1, test_dir_ + "/bad.yaml");
  
  EXPECT_THROW({
    try {
      source->loadConfiguration();
    } catch (const std::exception& e) {
      std::string error = e.what();
      // Should contain line/column info
      EXPECT_TRUE(error.find("line") != std::string::npos ||
                  error.find("parse") != std::string::npos);
      throw;
    }
  }, std::exception);
}

// Test environment variable substitution
TEST_F(FileSourceCompleteTest, EnvironmentSubstitution) {
  std::string config_with_env = R"({
  "server": {
    "host": "${TEST_HOST:-localhost}",
    "port": "${TEST_PORT:-8080}",
    "mode": "${TEST_MODE}"
  },
  "database": {
    "url": "postgres://${DB_USER:-admin}:${DB_PASS:-secret}@${DB_HOST:-localhost}/mydb"
  }
})";

  createFile(test_dir_ + "/config.json", config_with_env);
  
  // Test with defaults (vars not set)
  {
    auto source = createFileConfigSource("test", 1, test_dir_ + "/config.json");
    
    // Should fail because TEST_MODE has no default
    EXPECT_THROW({
      source->loadConfiguration();
    }, std::runtime_error);
  }
  
  // Test with environment variables set
  {
    setenv("TEST_HOST", "production.example.com", 1);
    setenv("TEST_PORT", "9090", 1);
    setenv("TEST_MODE", "production", 1);
    setenv("DB_USER", "dbadmin", 1);
    
    auto source = createFileConfigSource("test", 1, test_dir_ + "/config.json");
    auto config = source->loadConfiguration();
    
    EXPECT_EQ(config["server"]["host"], "production.example.com");
    EXPECT_EQ(config["server"]["port"], "9090");
    EXPECT_EQ(config["server"]["mode"], "production");
    EXPECT_EQ(config["database"]["url"], 
              "postgres://dbadmin:secret@localhost/mydb");
  }
}

// Test include file resolution
TEST_F(FileSourceCompleteTest, IncludeResolution) {
  // Main config with includes
  std::string main_config = R"({
  "app": "myapp",
  "version": "1.0",
  "include": ["base.json", "overrides.yaml"]
})";

  std::string base_config = R"({
  "server": {
    "host": "localhost",
    "port": 8080
  },
  "database": {
    "pool_size": 10
  }
})";

  std::string overrides_yaml = R"(
server:
  port: 9090
  ssl_enabled: true
database:
  pool_size: 20
features:
  new_feature: enabled
)";

  createFile(test_dir_ + "/config.json", main_config);
  createFile(test_dir_ + "/base.json", base_config);
  createFile(test_dir_ + "/overrides.yaml", overrides_yaml);
  
  auto source = createFileConfigSource("test", 1, test_dir_ + "/config.json");
  auto config = source->loadConfiguration();
  
  // Check merged configuration
  EXPECT_EQ(config["app"], "myapp");
  EXPECT_EQ(config["server"]["host"], "localhost");
  EXPECT_EQ(config["server"]["port"], 9090);  // Overridden
  EXPECT_EQ(config["server"]["ssl_enabled"], true);  // Added
  EXPECT_EQ(config["database"]["pool_size"], 20);  // Overridden
  EXPECT_EQ(config["features"]["new_feature"], "enabled");
}

// Test circular include detection
TEST_F(FileSourceCompleteTest, CircularIncludeDetection) {
  std::string config1 = R"({
  "name": "config1",
  "include": "config2.json"
})";

  std::string config2 = R"({
  "name": "config2",
  "include": "config3.json"
})";

  std::string config3 = R"({
  "name": "config3",
  "include": "config1.json"
})";

  createFile(test_dir_ + "/config1.json", config1);
  createFile(test_dir_ + "/config2.json", config2);
  createFile(test_dir_ + "/config3.json", config3);
  
  auto source = createFileConfigSource("test", 1, test_dir_ + "/config1.json");
  auto config = source->loadConfiguration();
  
  // Should handle circular includes gracefully
  // Each file should be included only once
  EXPECT_TRUE(config.contains("name"));
}

// Test maximum include depth
TEST_F(FileSourceCompleteTest, MaxIncludeDepth) {
  // Create a chain of includes exceeding max depth
  for (int i = 0; i < 10; i++) {
    std::string config = R"({
  "level": )" + std::to_string(i) + R"(,
  "include": "config)" + std::to_string(i + 1) + R"(.json"
})";
    createFile(test_dir_ + "/config" + std::to_string(i) + ".json", config);
  }
  
  auto source = createFileConfigSource("test", 1, test_dir_ + "/config0.json");
  
  // Should throw when max depth exceeded
  EXPECT_THROW({
    source->loadConfiguration();
  }, std::runtime_error);
}

// Test config.d overlay processing
TEST_F(FileSourceCompleteTest, ConfigDOverlays) {
  std::string main_config = R"({
  "app": "myapp",
  "server": {
    "host": "localhost",
    "port": 8080
  }
})";

  // Create config.d directory with overlay files
  createDirectory(test_dir_ + "/config.d");
  
  // Files will be processed in lexicographic order
  std::string overlay1 = R"({
  "server": {
    "port": 9090,
    "ssl": true
  },
  "feature1": "enabled"
})";

  std::string overlay2_yaml = R"(
server:
  port: 9091
  workers: 4
feature2: enabled
)";

  std::string overlay3 = R"({
  "server": {
    "port": 9092
  },
  "feature3": "enabled"
})";

  createFile(test_dir_ + "/config.json", main_config);
  createFile(test_dir_ + "/config.d/01-base.json", overlay1);
  createFile(test_dir_ + "/config.d/02-workers.yaml", overlay2_yaml);
  createFile(test_dir_ + "/config.d/03-final.json", overlay3);
  
  auto source = createFileConfigSource("test", 1, test_dir_ + "/config.json");
  auto config = source->loadConfiguration();
  
  // Check that overlays were applied in order
  EXPECT_EQ(config["app"], "myapp");
  EXPECT_EQ(config["server"]["host"], "localhost");
  EXPECT_EQ(config["server"]["port"], 9092);  // Last overlay wins
  EXPECT_EQ(config["server"]["ssl"], true);
  EXPECT_EQ(config["server"]["workers"], 4);
  EXPECT_EQ(config["feature1"], "enabled");
  EXPECT_EQ(config["feature2"], "enabled");
  EXPECT_EQ(config["feature3"], "enabled");
}

// Test search/precedence policy
TEST_F(FileSourceCompleteTest, SearchPrecedence) {
  // Create configs in different locations
  std::string cli_config = R"({"source": "cli", "priority": 1})";
  std::string env_config = R"({"source": "env", "priority": 2})";
  std::string local_config = R"({"source": "local", "priority": 3})";
  
  createFile(test_dir_ + "/cli.json", cli_config);
  createFile(test_dir_ + "/env.json", env_config);
  createFile(test_dir_ + "/config.json", local_config);
  
  // Test 1: CLI config takes precedence
  {
    auto source = createFileConfigSource("test", 1, test_dir_ + "/cli.json");
    auto config = source->loadConfiguration();
    EXPECT_EQ(config["source"], "cli");
  }
  
  // Test 2: ENV config takes precedence when no CLI
  {
    setenv("MCP_CONFIG", (test_dir_ + "/env.json").c_str(), 1);
    auto source = createFileConfigSource("test", 1, "");
    auto config = source->loadConfiguration();
    EXPECT_EQ(config["source"], "env");
    unsetenv("MCP_CONFIG");
  }
  
  // Test 3: Local config when no CLI or ENV
  {
    chdir(test_dir_.c_str());
    auto source = createFileConfigSource("test", 1, "");
    auto config = source->loadConfiguration();
    EXPECT_EQ(config["source"], "local");
  }
}

// Test oversized file rejection
TEST_F(FileSourceCompleteTest, OversizedFileRejection) {
  // Create a file larger than max size
  std::string large_content = "{\"data\": \"";
  for (size_t i = 0; i < 21 * 1024 * 1024; i++) {  // 21 MB
    large_content += "x";
  }
  large_content += "\"}";
  
  createFile(test_dir_ + "/large.json", large_content);
  
  auto source = createFileConfigSource("test", 1, test_dir_ + "/large.json");
  
  EXPECT_THROW({
    try {
      source->loadConfiguration();
    } catch (const std::exception& e) {
      std::string error = e.what();
      // Should mention file size
      EXPECT_TRUE(error.find("size") != std::string::npos ||
                  error.find("large") != std::string::npos);
      throw;
    }
  }, std::runtime_error);
}

// Test mixed JSON/YAML overlays
TEST_F(FileSourceCompleteTest, MixedJsonYamlOverlays) {
  std::string base_json = R"({
  "app": "myapp",
  "server": {
    "host": "localhost",
    "port": 8080
  },
  "features": ["feature1", "feature2"]
})";

  std::string overlay_yaml = R"(
server:
  port: 9090
  ssl:
    enabled: true
    cert: /path/to/cert
features:
  - feature1
  - feature2
  - feature3
database:
  host: dbhost
  port: 5432
)";

  createDirectory(test_dir_ + "/config.d");
  createFile(test_dir_ + "/config.json", base_json);
  createFile(test_dir_ + "/config.d/database.yaml", overlay_yaml);
  
  auto source = createFileConfigSource("test", 1, test_dir_ + "/config.json");
  auto config = source->loadConfiguration();
  
  // Check merged result
  EXPECT_EQ(config["app"], "myapp");
  EXPECT_EQ(config["server"]["host"], "localhost");
  EXPECT_EQ(config["server"]["port"], 9090);
  EXPECT_EQ(config["server"]["ssl"]["enabled"], true);
  EXPECT_EQ(config["database"]["host"], "dbhost");
  EXPECT_EQ(config["features"].size(), 3);
}

// Test environment substitution in YAML files
TEST_F(FileSourceCompleteTest, YamlEnvironmentSubstitution) {
  std::string yaml_with_env = R"(
server:
  host: ${SERVER_HOST:-localhost}
  port: ${SERVER_PORT:-8080}
  mode: ${SERVER_MODE:-development}
database:
  url: postgres://${DB_USER:-user}@${DB_HOST:-localhost}:${DB_PORT:-5432}/db
)";

  createFile(test_dir_ + "/config.yaml", yaml_with_env);
  
  setenv("SERVER_HOST", "prod.example.com", 1);
  setenv("DB_PORT", "5433", 1);
  
  auto source = createFileConfigSource("test", 1, test_dir_ + "/config.yaml");
  auto config = source->loadConfiguration();
  
  EXPECT_EQ(config["server"]["host"], "prod.example.com");
  EXPECT_EQ(config["server"]["port"], 8080);  // Uses default
  EXPECT_EQ(config["server"]["mode"], "development");  // Uses default
  EXPECT_EQ(config["database"]["url"], 
            "postgres://user@localhost:5433/db");
}

// Test include with environment substitution
TEST_F(FileSourceCompleteTest, IncludeWithEnvironmentSubstitution) {
  std::string main_config = R"({
  "app": "${APP_NAME:-myapp}",
  "include": "base.json"
})";

  std::string base_config = R"({
  "server": {
    "host": "${SERVER_HOST:-localhost}",
    "port": "${SERVER_PORT:-8080}"
  }
})";

  createFile(test_dir_ + "/config.json", main_config);
  createFile(test_dir_ + "/base.json", base_config);
  
  setenv("APP_NAME", "production-app", 1);
  setenv("SERVER_PORT", "9090", 1);
  
  auto source = createFileConfigSource("test", 1, test_dir_ + "/config.json");
  auto config = source->loadConfiguration();
  
  EXPECT_EQ(config["app"], "production-app");
  EXPECT_EQ(config["server"]["host"], "localhost");
  EXPECT_EQ(config["server"]["port"], "9090");
}

}  // namespace test
}  // namespace config
}  // namespace mcp