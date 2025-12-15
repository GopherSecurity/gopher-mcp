#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

#include <gtest/gtest.h>

// Platform-specific includes for filesystem operations
#ifndef _WIN32
#include <dirent.h>
#include <unistd.h>

#include <sys/stat.h>
#else
#include <direct.h>
#include <windows.h>
#endif

#include "mcp/config/config_manager.h"
#include "mcp/logging/log_macros.h"
#include "mcp/logging/logger_registry.h"

// Test logging sink to capture log messages
namespace mcp {
namespace logging {

class TestLogSink : public LogSink {
 public:
  TestLogSink() = default;

  void log(const LogMessage& msg) override {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.push_back(msg);
  }

  void flush() override {}

  SinkType type() const override { return SinkType::External; }

  bool hasMessage(const std::string& content, LogLevel level = LogLevel::Info) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& msg : messages_) {
      if (msg.level == level &&
          msg.message.find(content) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.clear();
  }

  size_t getMessageCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_.size();
  }

 private:
  std::vector<LogMessage> messages_;
  mutable std::mutex mutex_;
};

}  // namespace logging
}  // namespace mcp

namespace mcp {
namespace config {
namespace test {

class SearchPrecedenceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create test directory
    test_dir_ = std::string("test_search_") + std::to_string(getpid());
    mkdir(test_dir_.c_str(), 0755);

    // Save original working directory
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
      original_dir_ = cwd;
    }

    // Set up test logging sink
    test_sink_ = std::make_shared<logging::TestLogSink>();
    auto& registry = logging::LoggerRegistry::instance();
    auto logger = registry.getOrCreateLogger("config.search");
    logger->setSink(test_sink_);
    logger->setLevel(logging::LogLevel::Debug);

    auto file_logger = registry.getOrCreateLogger("config.file");
    file_logger->setSink(test_sink_);
    file_logger->setLevel(logging::LogLevel::Debug);
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
  std::shared_ptr<logging::TestLogSink> test_sink_;
};

// Test precedence order: CLI > ENV > Local > System
TEST_F(SearchPrecedenceTest, PrecedenceOrderCLI) {
  // Create configs at different levels
  std::string cli_config = R"({"source": "cli", "level": 1})";
  std::string env_config = R"({"source": "env", "level": 2})";
  std::string local_config = R"({"source": "local", "level": 3})";
  std::string system_config = R"({"source": "system", "level": 4})";

  createFile(test_dir_ + "/cli.json", cli_config);
  createFile(test_dir_ + "/env.json", env_config);
  createFile(test_dir_ + "/config.json", local_config);
  createDirectory("/tmp/gopher-mcp-test");
  createFile("/tmp/gopher-mcp-test/config.json", system_config);

  // Test CLI precedence (highest)
  test_sink_->clear();
  auto source = createFileConfigSource("test", 1, test_dir_ + "/cli.json");
  auto config = source->loadConfiguration();

  EXPECT_EQ(std::string("cli"), config["source"].getString());
  EXPECT_TRUE(
      test_sink_->hasMessage("CLI override detected", logging::LogLevel::Info));
  EXPECT_TRUE(test_sink_->hasMessage("Configuration source won: CLI",
                                     logging::LogLevel::Info));
}

TEST_F(SearchPrecedenceTest, PrecedenceOrderENV) {
  // Create configs at different levels
  std::string env_config = R"({"source": "env", "level": 2})";
  std::string local_config = R"({"source": "local", "level": 3})";
  std::string system_config = R"({"source": "system", "level": 4})";

  createFile(test_dir_ + "/env.json", env_config);
  createFile(test_dir_ + "/config.json", local_config);
  createDirectory("/tmp/gopher-mcp-test");
  createFile("/tmp/gopher-mcp-test/config.json", system_config);

  // Test ENV precedence (no CLI)
  setenv("MCP_CONFIG", (test_dir_ + "/env.json").c_str(), 1);
  test_sink_->clear();

  auto source = createFileConfigSource("test", 1, "");
  auto config = source->loadConfiguration();

  EXPECT_EQ(std::string("env"), config["source"].getString());
  EXPECT_TRUE(test_sink_->hasMessage("Environment override detected",
                                     logging::LogLevel::Info));
  EXPECT_TRUE(test_sink_->hasMessage("Configuration source won: MCP_CONFIG",
                                     logging::LogLevel::Info));
}

TEST_F(SearchPrecedenceTest, PrecedenceOrderLocal) {
  // Create configs at local and system levels
  std::string local_config = R"({"source": "local", "level": 3})";
  std::string system_config = R"({"source": "system", "level": 4})";

  // Change to test directory and create local config
  chdir(test_dir_.c_str());
  createFile("config.json", local_config);
  createDirectory("/tmp/gopher-mcp-test");
  createFile("/tmp/gopher-mcp-test/config.json", system_config);

  // Test local precedence (no CLI or ENV)
  test_sink_->clear();
  auto source = createFileConfigSource("test", 1, "");
  auto config = source->loadConfiguration();

  EXPECT_EQ(std::string("local"), config["source"].getString());
  EXPECT_TRUE(test_sink_->hasMessage(
      "Configuration source won: local directory", logging::LogLevel::Info));
}

// Test config.d overlay processing
TEST_F(SearchPrecedenceTest, ConfigDOverlayOrder) {
  // Create base config
  std::string base_config = R"({
    "app": "test",
    "server": {
      "host": "localhost",
      "port": 8080
    }
  })";

  createFile(test_dir_ + "/config.json", base_config);
  createDirectory(test_dir_ + "/config.d");

  // Create overlay files (will be processed in lexicographic order)
  std::string overlay1 = R"({"server": {"port": 9090}, "feature1": true})";
  std::string overlay2 = R"({"server": {"port": 9091}, "feature2": true})";
  std::string overlay3 = R"({"server": {"port": 9092}, "feature3": true})";

  createFile(test_dir_ + "/config.d/01-first.json", overlay1);
  createFile(test_dir_ + "/config.d/02-second.yaml", overlay2);
  createFile(test_dir_ + "/config.d/03-third.json", overlay3);

  test_sink_->clear();
  auto source = createFileConfigSource("test", 1, test_dir_ + "/config.json");
  auto config = source->loadConfiguration();

  // Check that overlays were applied in order
  EXPECT_EQ(9092, config["server"]["port"].getInt());  // Last overlay wins
  EXPECT_TRUE(config["feature1"].getBool());
  EXPECT_TRUE(config["feature2"].getBool());
  EXPECT_TRUE(config["feature3"].getBool());

  // Check logs for overlay processing
  EXPECT_TRUE(test_sink_->hasMessage("Scanning config.d directory",
                                     logging::LogLevel::Info));
  EXPECT_TRUE(test_sink_->hasMessage("Directory scan results: found 3",
                                     logging::LogLevel::Info));
  EXPECT_TRUE(test_sink_->hasMessage("Overlay files in lexicographic order",
                                     logging::LogLevel::Info));
  EXPECT_TRUE(test_sink_->hasMessage("01-first.json", logging::LogLevel::Info));
  EXPECT_TRUE(
      test_sink_->hasMessage("02-second.yaml", logging::LogLevel::Info));
  EXPECT_TRUE(test_sink_->hasMessage("03-third.json", logging::LogLevel::Info));
}

// Test include resolution security
TEST_F(SearchPrecedenceTest, IncludeResolutionSecurity) {
  // Create base config with includes
  std::string base_config = R"({
    "app": "test",
    "include": ["relative/include.json", "/absolute/include.json"]
  })";

  std::string relative_include = R"({"relative": "loaded"})";
  std::string absolute_include = R"({"absolute": "loaded"})";

  createFile(test_dir_ + "/config.json", base_config);
  createFile(test_dir_ + "/relative/include.json", relative_include);

  // Note: Absolute path will fail unless within allowed roots
  // This test verifies that relative paths work correctly

  auto source = createFileConfigSource("test", 1, test_dir_ + "/config.json");

  try {
    auto config = source->loadConfiguration();
    // Relative include should work
    EXPECT_EQ(std::string("loaded"), config["relative"].getString());
  } catch (const std::exception& e) {
    // Expected if absolute path is not in allowed roots
    std::string error = e.what();
    EXPECT_TRUE(error.find("/absolute/include.json") != std::string::npos ||
                error.find("not allowed") != std::string::npos);
  }
}

// Test circular include detection
TEST_F(SearchPrecedenceTest, CircularIncludeDetection) {
  // Create configs that include each other
  std::string config1 = R"({"name": "config1", "include": "config2.json"})";
  std::string config2 = R"({"name": "config2", "include": "config1.json"})";

  createFile(test_dir_ + "/config1.json", config1);
  createFile(test_dir_ + "/config2.json", config2);

  auto source = createFileConfigSource("test", 1, test_dir_ + "/config1.json");

  // Should handle circular includes gracefully
  auto config = source->loadConfiguration();
  EXPECT_TRUE(config.contains("name"));

  // Check for circular include warning in logs
  EXPECT_TRUE(
      test_sink_->hasMessage("Circular include detected",
                             logging::LogLevel::Warning) ||
      test_sink_->hasMessage("already processed", logging::LogLevel::Debug));
}

// Test logging of search paths
TEST_F(SearchPrecedenceTest, SearchPathLogging) {
  // Test with no config files present
  test_sink_->clear();

  auto source = createFileConfigSource("test", 1, "");
  auto config = source->loadConfiguration();

  // Should log that no config was found
  EXPECT_TRUE(test_sink_->hasMessage("No configuration file found",
                                     logging::LogLevel::Warning));

  // Create a config and test again
  createFile(test_dir_ + "/config.yaml", R"({"found": true})");
  chdir(test_dir_.c_str());

  test_sink_->clear();
  source = createFileConfigSource("test", 1, "");
  config = source->loadConfiguration();

  // Should log successful discovery
  EXPECT_TRUE(test_sink_->hasMessage("Configuration source won",
                                     logging::LogLevel::Info));
  EXPECT_TRUE(test_sink_->hasMessage("Base configuration file chosen",
                                     logging::LogLevel::Info));
}

// Test environment variable override without exposing value
TEST_F(SearchPrecedenceTest, EnvironmentVariablePrivacy) {
  std::string config = R"({"secret": "value"})";
  createFile(test_dir_ + "/secret.json", config);

  // Set environment variable
  setenv("MCP_CONFIG", (test_dir_ + "/secret.json").c_str(), 1);

  test_sink_->clear();
  auto source = createFileConfigSource("test", 1, "");
  auto result = source->loadConfiguration();

  // Check that ENV override is logged but value is not
  EXPECT_TRUE(test_sink_->hasMessage("Environment override detected",
                                     logging::LogLevel::Info));

  // Ensure the actual path is not logged (privacy)
  EXPECT_FALSE(test_sink_->hasMessage("secret.json", logging::LogLevel::Info));
  EXPECT_TRUE(test_sink_->hasMessage("MCP_CONFIG environment variable",
                                     logging::LogLevel::Info));
}

}  // namespace test
}  // namespace config
}  // namespace mcp
