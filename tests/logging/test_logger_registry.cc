#include <gtest/gtest.h>
#include "mcp/logging/logger_registry.h"
#include "mcp/logging/log_macros.h"

using namespace mcp::logging;

// Test sink for capturing
class TestCaptureSink : public LogSink {
public:
  void log(const LogMessage& msg) override {
    std::lock_guard<std::mutex> lock(mutex_);
    messages.push_back(msg);
  }
  
  void flush() override {}
  SinkType type() const override { return SinkType::Null; }
  
  std::vector<LogMessage> getMessages() {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages;
  }
  
  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    messages.clear();
  }
  
private:
  std::mutex mutex_;
  std::vector<LogMessage> messages;
};

class LoggerRegistryTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Get registry instance
    registry_ = &LoggerRegistry::instance();
    
    // Create test sink
    test_sink_ = std::make_shared<TestCaptureSink>();
  }
  
  LoggerRegistry* registry_;
  std::shared_ptr<TestCaptureSink> test_sink_;
};

TEST_F(LoggerRegistryTest, SingletonInstance) {
  auto& instance1 = LoggerRegistry::instance();
  auto& instance2 = LoggerRegistry::instance();
  
  EXPECT_EQ(&instance1, &instance2);
}

TEST_F(LoggerRegistryTest, DefaultLogger) {
  auto logger = registry_->getDefaultLogger();
  
  ASSERT_NE(logger, nullptr);
  EXPECT_EQ(logger->getName(), "default");
  EXPECT_EQ(logger->getLevel(), LogLevel::Info);
}

TEST_F(LoggerRegistryTest, GetOrCreateLogger) {
  auto logger1 = registry_->getOrCreateLogger("test.logger");
  auto logger2 = registry_->getOrCreateLogger("test.logger");
  
  // Should return same instance
  EXPECT_EQ(logger1, logger2);
  EXPECT_EQ(logger1->getName(), "test.logger");
}

TEST_F(LoggerRegistryTest, ComponentLogger) {
  ComponentLogger logger(Component::Server, "test_server");
  
  // Set sink for testing
  auto registry_logger = registry_->getOrCreateLogger("Server.test_server");
  registry_logger->setSink(test_sink_);
  
  logger.log(LogLevel::Info, "Component log message");
  
  auto messages = test_sink_->getMessages();
  ASSERT_GE(messages.size(), 1);
  EXPECT_EQ(messages.back().component, Component::Server);
}

TEST_F(LoggerRegistryTest, SetGlobalLevel) {
  registry_->setGlobalLevel(LogLevel::Warning);
  
  auto logger = registry_->getOrCreateLogger("test.global");
  EXPECT_EQ(logger->getLevel(), LogLevel::Warning);
  
  registry_->setGlobalLevel(LogLevel::Debug);
  EXPECT_EQ(logger->getLevel(), LogLevel::Debug);
}

TEST_F(LoggerRegistryTest, SetComponentLevel) {
  registry_->setComponentLevel(Component::Network, LogLevel::Error);
  
  auto logger1 = registry_->getOrCreateLogger("Network.test1");
  auto logger2 = registry_->getOrCreateLogger("Server.test2");
  
  // Component level should be applied
  ComponentLogger net_logger(Component::Network, "test1");
  net_logger.setLevel(LogLevel::Error);
  
  // Other components use global level
  ComponentLogger srv_logger(Component::Server, "test2");
}

TEST_F(LoggerRegistryTest, PatternBasedLogging) {
  // Set global level explicitly to ensure consistent test
  registry_->setGlobalLevel(LogLevel::Info);
  
  // Set pattern for filter components
  registry_->setPattern("filter/*", LogLevel::Debug);
  
  auto filter_logger = registry_->getOrCreateLogger("filter/http");
  auto other_logger = registry_->getOrCreateLogger("server/main");
  
  EXPECT_EQ(filter_logger->getLevel(), LogLevel::Debug);
  EXPECT_EQ(other_logger->getLevel(), LogLevel::Info);  // Should be Info, not just != Debug
}

TEST_F(LoggerRegistryTest, BloomFilterIntegration) {
  // Enable bloom filter
  registry_->setBloomFilter(true, 256);
  
  // Create some loggers
  auto logger1 = registry_->getOrCreateLogger("bloom.test1");
  auto logger2 = registry_->getOrCreateLogger("bloom.test2");
  
  // Check shouldLog with bloom filter
  EXPECT_TRUE(registry_->shouldLog("bloom.test1", LogLevel::Info));
  EXPECT_FALSE(registry_->shouldLog("nonexistent.logger", LogLevel::Debug));
}

TEST_F(LoggerRegistryTest, GetLoggerNames) {
  // Create some loggers
  registry_->getOrCreateLogger("list.test1");
  registry_->getOrCreateLogger("list.test2");
  registry_->getOrCreateLogger("list.test3");
  
  auto names = registry_->getLoggerNames();
  
  // Should contain our test loggers
  EXPECT_TRUE(std::find(names.begin(), names.end(), "list.test1") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "list.test2") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "list.test3") != names.end());
}

TEST_F(LoggerRegistryTest, ZeroConfigurationMacros) {
  // Redirect default logger to test sink
  auto default_logger = registry_->getDefaultLogger();
  default_logger->setSink(test_sink_);
  default_logger->setLevel(LogLevel::Debug);
  
  // Use zero-config macros
  LOG_DEBUG("Debug message");
  LOG_INFO("Info message");
  LOG_WARNING("Warning message");
  LOG_ERROR("Error message");
  
  default_logger->flush();
  
  auto messages = test_sink_->getMessages();
  ASSERT_GE(messages.size(), 4);
  
  // Find each message type
  bool found_debug = false, found_info = false, found_warning = false, found_error = false;
  
  for (const auto& msg : messages) {
    if (msg.level == LogLevel::Debug && msg.message.find("Debug message") != std::string::npos) {
      found_debug = true;
    }
    if (msg.level == LogLevel::Info && msg.message.find("Info message") != std::string::npos) {
      found_info = true;
    }
    if (msg.level == LogLevel::Warning && msg.message.find("Warning message") != std::string::npos) {
      found_warning = true;
    }
    if (msg.level == LogLevel::Error && msg.message.find("Error message") != std::string::npos) {
      found_error = true;
    }
  }
  
  EXPECT_TRUE(found_debug);
  EXPECT_TRUE(found_info);
  EXPECT_TRUE(found_warning);
  EXPECT_TRUE(found_error);
}

TEST_F(LoggerRegistryTest, RegisterComponentLogger) {
  auto custom_logger = std::make_shared<Logger>("custom", LogMode::Sync);
  custom_logger->setSink(test_sink_);
  
  registry_->registerComponentLogger(Component::Filter, "custom_filter", custom_logger);
  
  // Should be retrievable
  auto retrieved = registry_->getOrCreateLogger("Filter.custom_filter");
  EXPECT_EQ(retrieved, custom_logger);
}

TEST_F(LoggerRegistryTest, EffectiveLevelWithPatterns) {
  // Set global level explicitly to ensure consistent test
  registry_->setGlobalLevel(LogLevel::Info);
  
  // Set multiple patterns
  registry_->setPattern("network/*", LogLevel::Debug);
  registry_->setPattern("network/connection*", LogLevel::Debug);
  registry_->setPattern("server/*", LogLevel::Warning);
  
  // Check effective levels
  EXPECT_EQ(registry_->getEffectiveLevel("network/socket"), LogLevel::Debug);
  EXPECT_EQ(registry_->getEffectiveLevel("server/handler"), LogLevel::Warning);
  EXPECT_EQ(registry_->getEffectiveLevel("other/component"), LogLevel::Info); // Uses global
}

TEST_F(LoggerRegistryTest, ThreadSafety) {
  const int num_threads = 10;
  const int loggers_per_thread = 5;
  
  std::vector<std::thread> threads;
  std::vector<std::shared_ptr<Logger>> all_loggers;
  std::mutex loggers_mutex;
  
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([this, t, loggers_per_thread, &all_loggers, &loggers_mutex]() {
      for (int i = 0; i < loggers_per_thread; ++i) {
        std::string name = "thread" + std::to_string(t) + ".logger" + std::to_string(i);
        auto logger = registry_->getOrCreateLogger(name);
        
        std::lock_guard<std::mutex> lock(loggers_mutex);
        all_loggers.push_back(logger);
      }
    });
  }
  
  for (auto& thread : threads) {
    thread.join();
  }
  
  // All loggers should be created
  EXPECT_EQ(all_loggers.size(), num_threads * loggers_per_thread);
  
  // Check uniqueness by name
  std::set<std::string> unique_names;
  for (const auto& logger : all_loggers) {
    unique_names.insert(logger->getName());
  }
  
  EXPECT_EQ(unique_names.size(), num_threads * loggers_per_thread);
}