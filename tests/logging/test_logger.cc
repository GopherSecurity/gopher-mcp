#include <gtest/gtest.h>
#include "mcp/logging/logger.h"
#include "mcp/logging/log_sink.h"
#include <thread>
#include <chrono>

using namespace mcp::logging;
using namespace std::chrono_literals;

// Test sink that captures messages
class CapturingSink : public LogSink {
public:
  void log(const LogMessage& msg) override {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.push_back(msg);
  }
  
  void flush() override {
    flushed_ = true;
  }
  
  SinkType type() const override { return SinkType::Null; }
  
  std::vector<LogMessage> getMessages() {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_;
  }
  
  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.clear();
  }
  
  size_t messageCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_.size();
  }
  
private:
  std::mutex mutex_;
  std::vector<LogMessage> messages_;
  std::atomic<bool> flushed_{false};
};

class LoggerTest : public ::testing::Test {
protected:
  void SetUp() override {
    sink_ = std::make_shared<CapturingSink>();
  }
  
  std::shared_ptr<CapturingSink> sink_;
};

TEST_F(LoggerTest, BasicLogging) {
  Logger logger("test", LogMode::Sync);
  logger.setSink(sink_);
  
  logger.info("Test message");
  
  auto messages = sink_->getMessages();
  ASSERT_EQ(messages.size(), 1);
  EXPECT_EQ(messages[0].level, LogLevel::Info);
  EXPECT_EQ(messages[0].message, "Test message");
  EXPECT_EQ(messages[0].logger_name, "test");
}

TEST_F(LoggerTest, LogLevels) {
  Logger logger("test", LogMode::Sync);
  logger.setSink(sink_);
  
  logger.debug("Debug message");
  logger.info("Info message");
  logger.warning("Warning message");
  logger.error("Error message");
  logger.critical("Critical message");
  
  auto messages = sink_->getMessages();
  ASSERT_EQ(messages.size(), 5);
  
  EXPECT_EQ(messages[0].level, LogLevel::Debug);
  EXPECT_EQ(messages[1].level, LogLevel::Info);
  EXPECT_EQ(messages[2].level, LogLevel::Warning);
  EXPECT_EQ(messages[3].level, LogLevel::Error);
  EXPECT_EQ(messages[4].level, LogLevel::Critical);
}

TEST_F(LoggerTest, LogLevelFiltering) {
  Logger logger("test", LogMode::Sync);
  logger.setSink(sink_);
  logger.setLevel(LogLevel::Warning);
  
  logger.debug("Debug - should not appear");
  logger.info("Info - should not appear");
  logger.warning("Warning - should appear");
  logger.error("Error - should appear");
  
  auto messages = sink_->getMessages();
  ASSERT_EQ(messages.size(), 2);
  EXPECT_EQ(messages[0].message, "Warning - should appear");
  EXPECT_EQ(messages[1].message, "Error - should appear");
}

TEST_F(LoggerTest, FormattedLogging) {
  Logger logger("test", LogMode::Sync);
  logger.setSink(sink_);
  
  int value = 42;
  std::string str = "world";
  logger.info("Hello {} with value {}", str, value);
  
  auto messages = sink_->getMessages();
  ASSERT_EQ(messages.size(), 1);
  EXPECT_EQ(messages[0].message, "Hello world with value 42");
}

TEST_F(LoggerTest, ContextLogging) {
  Logger logger("test", LogMode::Sync);
  logger.setSink(sink_);
  
  LogContext ctx;
  ctx.trace_id = "trace123";
  ctx.request_id = "req456";
  ctx.component = Component::Server;
  ctx.setLocation("test.cc", 100, "testFunc");
  
  logger.logWithContext(LogLevel::Info, ctx, "Context message");
  
  auto messages = sink_->getMessages();
  ASSERT_EQ(messages.size(), 1);
  
  EXPECT_EQ(messages[0].trace_id, "trace123");
  EXPECT_EQ(messages[0].request_id, "req456");
  EXPECT_EQ(messages[0].component, Component::Server);
  EXPECT_STREQ(messages[0].file, "test.cc");
  EXPECT_EQ(messages[0].line, 100);
  EXPECT_STREQ(messages[0].function, "testFunc");
}

TEST_F(LoggerTest, ComponentLogging) {
  Logger logger("test", LogMode::Sync);
  logger.setSink(sink_);
  
  logger.logWithComponent(LogLevel::Info, Component::Network, 
                          "Network component message");
  
  auto messages = sink_->getMessages();
  ASSERT_EQ(messages.size(), 1);
  EXPECT_EQ(messages[0].component, Component::Network);
  EXPECT_EQ(messages[0].message, "Network component message");
}

TEST_F(LoggerTest, LocationLogging) {
  Logger logger("test", LogMode::Sync);
  logger.setSink(sink_);
  
  logger.log(LogLevel::Error, "file.cc", 42, "myFunc", 
            "Error at specific location");
  
  auto messages = sink_->getMessages();
  ASSERT_EQ(messages.size(), 1);
  EXPECT_STREQ(messages[0].file, "file.cc");
  EXPECT_EQ(messages[0].line, 42);
  EXPECT_STREQ(messages[0].function, "myFunc");
}

TEST_F(LoggerTest, AsyncLogging) {
  Logger logger("test", LogMode::Async);
  logger.setSink(sink_);
  
  // Log multiple messages
  for (int i = 0; i < 10; ++i) {
    logger.info("Async message {}", i);
  }
  
  // Flush to ensure all messages are processed
  logger.flush();
  
  auto messages = sink_->getMessages();
  EXPECT_EQ(messages.size(), 10);
  
  // Verify order is preserved
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(messages[i].message, "Async message " + std::to_string(i));
  }
}

TEST_F(LoggerTest, NoOpLogger) {
  NoOpLogger logger;
  logger.setSink(sink_);
  
  // Should not log anything
  logger.debug("Debug");
  logger.info("Info");
  logger.error("Error");
  
  EXPECT_EQ(sink_->messageCount(), 0);
  EXPECT_FALSE(logger.shouldLog(LogLevel::Emergency));
}

TEST_F(LoggerTest, BloomFilterHint) {
  Logger logger("test.component", LogMode::Sync);
  logger.setSink(sink_);
  
  // Without bloom filter
  EXPECT_TRUE(logger.shouldLog(LogLevel::Info));
  
  // With bloom filter that doesn't contain the logger
  BloomFilter<std::string> filter(64, 2);
  filter.add("other.logger");
  
  logger.setBloomFilterHint(&filter);
  EXPECT_FALSE(logger.shouldLog(LogLevel::Info));
  
  // Add to bloom filter
  filter.add("test.component");
  EXPECT_TRUE(logger.shouldLog(LogLevel::Info));
}

TEST_F(LoggerTest, ModeSwitch) {
  Logger logger("test", LogMode::Sync);
  logger.setSink(sink_);
  
  // Start with sync
  logger.info("Sync message");
  EXPECT_EQ(sink_->messageCount(), 1);
  
  // Switch to async
  logger.setMode(LogMode::Async);
  logger.info("Async message");
  logger.flush();
  EXPECT_EQ(sink_->messageCount(), 2);
  
  // Switch to NoOp
  logger.setMode(LogMode::NoOp);
  logger.info("NoOp message - should not appear");
  EXPECT_EQ(sink_->messageCount(), 2);
  
  // Back to sync
  logger.setMode(LogMode::Sync);
  logger.info("Back to sync");
  EXPECT_EQ(sink_->messageCount(), 3);
}

TEST_F(LoggerTest, ThreadSafety) {
  Logger logger("test", LogMode::Async);
  logger.setSink(sink_);
  
  const int num_threads = 4;
  const int messages_per_thread = 100;
  
  std::vector<std::thread> threads;
  
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&logger, t, messages_per_thread]() {
      for (int i = 0; i < messages_per_thread; ++i) {
        logger.info("Thread {} message {}", t, i);
      }
    });
  }
  
  for (auto& thread : threads) {
    thread.join();
  }
  
  logger.flush();
  
  EXPECT_EQ(sink_->messageCount(), num_threads * messages_per_thread);
}

TEST_F(LoggerTest, GettersAndSetters) {
  Logger logger("test_logger", LogMode::Sync);
  
  EXPECT_EQ(logger.getName(), "test_logger");
  
  logger.setLevel(LogLevel::Warning);
  EXPECT_EQ(logger.getLevel(), LogLevel::Warning);
  
  logger.setLevel(LogLevel::Debug);
  EXPECT_EQ(logger.getLevel(), LogLevel::Debug);
}