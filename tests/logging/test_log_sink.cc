#include <gtest/gtest.h>
#include "mcp/logging/log_sink.h"
#include <sstream>
#include <fstream>
#include <filesystem>

using namespace mcp::logging;
namespace fs = std::__fs::filesystem;

class TestLogSink : public LogSink {
public:
  void log(const LogMessage& msg) override {
    messages_.push_back(msg);
    last_formatted_ = formatter_->format(msg);
  }
  
  void flush() override {
    flushed_ = true;
    if (flush_callback) {
      flush_callback();
    }
  }
  
  SinkType type() const override { return SinkType::Null; }
  
  std::vector<LogMessage> messages_;
  std::string last_formatted_;
  bool flushed_ = false;
  std::function<void()> flush_callback;
};

class LogSinkTest : public ::testing::Test {
protected:
  void SetUp() override {
    test_message_.level = LogLevel::Info;
    test_message_.message = "Test message";
    test_message_.logger_name = "test_logger";
    test_message_.component = Component::Server;
    test_message_.component_name = "test_server";
  }
  
  void TearDown() override {
    // Clean up test files
    for (const auto& file : test_files_) {
      fs::remove(file);
      // Also remove rotated files
      for (int i = 1; i <= 10; ++i) {
        fs::remove(file + "." + std::to_string(i));
      }
    }
  }
  
  LogMessage test_message_;
  std::vector<std::string> test_files_;
};

TEST_F(LogSinkTest, NullSink) {
  NullSink sink;
  
  // Should not crash
  sink.log(test_message_);
  sink.flush();
  
  EXPECT_EQ(sink.type(), SinkType::Null);
  EXPECT_FALSE(sink.supportsAsync());
  EXPECT_FALSE(sink.supportsRotation());
}

TEST_F(LogSinkTest, StdioSinkStderr) {
  // Capture stderr
  std::stringstream buffer;
  std::streambuf* old = std::cerr.rdbuf(buffer.rdbuf());
  
  {
    StdioSink sink(StdioSink::Stderr);
    sink.log(test_message_);
    sink.flush();
  }
  
  // Restore stderr
  std::cerr.rdbuf(old);
  
  std::string output = buffer.str();
  EXPECT_TRUE(output.find("Test message") != std::string::npos);
  EXPECT_TRUE(output.find("INFO") != std::string::npos);
}

TEST_F(LogSinkTest, StdioSinkStdout) {
  // Capture stdout
  std::stringstream buffer;
  std::streambuf* old = std::cout.rdbuf(buffer.rdbuf());
  
  {
    StdioSink sink(StdioSink::Stdout);
    sink.log(test_message_);
    sink.flush();
  }
  
  // Restore stdout
  std::cout.rdbuf(old);
  
  std::string output = buffer.str();
  EXPECT_TRUE(output.find("Test message") != std::string::npos);
  EXPECT_TRUE(output.find("INFO") != std::string::npos);
}

TEST_F(LogSinkTest, ExternalSink) {
  std::vector<std::string> captured_messages;
  LogLevel captured_level;
  std::string captured_logger;
  
  auto callback = [&](LogLevel level, const std::string& logger, const std::string& msg) {
    captured_level = level;
    captured_logger = logger;
    captured_messages.push_back(msg);
  };
  
  ExternalSink sink(callback);
  
  sink.log(test_message_);
  
  EXPECT_EQ(captured_messages.size(), 1);
  EXPECT_EQ(captured_level, LogLevel::Info);
  EXPECT_EQ(captured_logger, "test_logger");
  EXPECT_TRUE(captured_messages[0].find("Test message") != std::string::npos);
  
  EXPECT_EQ(sink.type(), SinkType::External);
}

TEST_F(LogSinkTest, RotatingFileSinkBasic) {
  std::string test_file = "/tmp/test_log_" + std::to_string(getpid()) + ".log";
  test_files_.push_back(test_file);
  
  RotatingFileSink::Config config;
  config.base_filename = test_file;
  config.max_file_size = 1024; // 1KB for testing
  config.max_files = 3;
  
  {
    RotatingFileSink sink(config);
    
    // Write some messages
    for (int i = 0; i < 10; ++i) {
      test_message_.message = "Message " + std::to_string(i);
      sink.log(test_message_);
    }
    
    sink.flush();
  }
  
  // Check file exists and has content
  EXPECT_TRUE(fs::exists(test_file));
  
  std::ifstream file(test_file);
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  
  EXPECT_TRUE(content.find("Message") != std::string::npos);
}

TEST_F(LogSinkTest, RotatingFileSinkRotation) {
  std::string test_file = "/tmp/test_rotate_" + std::to_string(getpid()) + ".log";
  test_files_.push_back(test_file);
  
  RotatingFileSink::Config config;
  config.base_filename = test_file;
  config.max_file_size = 100; // Very small for testing
  config.max_files = 2;
  
  {
    RotatingFileSink sink(config);
    
    // Write enough to trigger rotation
    for (int i = 0; i < 20; ++i) {
      test_message_.message = "This is a longer message to trigger rotation " + 
                              std::to_string(i);
      sink.log(test_message_);
    }
    
    sink.flush();
  }
  
  // Check that rotation happened
  EXPECT_TRUE(fs::exists(test_file));
  EXPECT_TRUE(fs::exists(test_file + ".1"));
}

TEST_F(LogSinkTest, SinkGuardRAII) {
  // Use a shared flag to track if flush was called
  auto flush_called = std::make_shared<bool>(false);
  
  {
    auto test_sink = std::make_unique<TestLogSink>();
    // Capture the flush flag in the sink
    test_sink->flush_callback = [flush_called]() { *flush_called = true; };
    
    SinkGuard<TestLogSink> guard(std::move(test_sink));
    guard->log(test_message_);
    EXPECT_FALSE(*flush_called);
  }
  
  // After guard is destroyed, sink should be flushed
  EXPECT_TRUE(*flush_called);
}

TEST_F(LogSinkTest, SinkFactory) {
  // File sink
  auto file_sink = SinkFactory::createFileSink("/tmp/factory_test.log");
  test_files_.push_back("/tmp/factory_test.log");
  EXPECT_EQ(file_sink->type(), SinkType::File);
  
  // Stdio sink
  auto stdio_sink = SinkFactory::createStdioSink(true);
  EXPECT_EQ(stdio_sink->type(), SinkType::Stdio);
  
  // Null sink
  auto null_sink = SinkFactory::createNullSink();
  EXPECT_EQ(null_sink->type(), SinkType::Null);
  
  // External sink
  auto external_sink = SinkFactory::createExternalSink(
    [](LogLevel, const std::string&, const std::string&) {});
  EXPECT_EQ(external_sink->type(), SinkType::External);
}

TEST_F(LogSinkTest, CustomFormatter) {
  TestLogSink sink;
  
  // Use JSON formatter
  sink.setFormatter(std::make_unique<JsonFormatter>());
  
  test_message_.trace_id = "trace123";
  test_message_.request_id = "req456";
  
  sink.log(test_message_);
  
  // Check JSON output
  EXPECT_TRUE(sink.last_formatted_.find("\"level\":\"INFO\"") != std::string::npos);
  EXPECT_TRUE(sink.last_formatted_.find("\"message\":\"Test message\"") != std::string::npos);
  EXPECT_TRUE(sink.last_formatted_.find("\"trace_id\":\"trace123\"") != std::string::npos);
  EXPECT_TRUE(sink.last_formatted_.find("\"request_id\":\"req456\"") != std::string::npos);
}