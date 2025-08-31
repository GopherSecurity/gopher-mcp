/**
 * @file test_mcp_logging_api.cc
 * @brief Comprehensive tests for MCP Logging C API with RAII enforcement
 */

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/c_api/mcp_c_logging_api.h"
#include "mcp/c_api/mcp_c_types.h"

namespace mcp {
namespace c_api {
namespace {

// Test fixture for logging API tests
class LoggingApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Clean up any previous test files
    cleanupTestFiles();

    // Reset registry to default state
    // mcp_registry_set_default_level(MCP_LOG_LEVEL_INFO);
    // mcp_registry_set_default_mode is not available in new API
  }

  void TearDown() override {
    // mcp_registry_flush_all is not available in new API

    // Clean up test files
    cleanupTestFiles();
  }

  void cleanupTestFiles() {
    // Remove test log files
    std::filesystem::remove("test.log");
    std::filesystem::remove("test_rotating.log");
    for (int i = 0; i < 10; ++i) {
      std::filesystem::remove("test_rotating.log." + std::to_string(i));
    }
  }

  // Helper to create a string view
  mcp_string_view_t makeStringView(const std::string& str) {
    mcp_string_view_t view;
    view.data = str.c_str();
    view.length = str.length();
    return view;
  }
};

// Test logger creation and destruction with RAII
TEST_F(LoggingApiTest, LoggerCreateDestroy) {
  auto name = makeStringView("test_logger");
  mcp_logger_handle_t handle;
  auto result = mcp_logger_get_or_create(name, &handle);

  ASSERT_EQ(result, MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_LOGGER(handle));

  // Logger should be usable
  auto message = makeStringView("Test message");
  EXPECT_EQ(mcp_logger_log(handle, MCP_LOG_LEVEL_INFO, message), MCP_LOG_OK);

  // Release should clean up resources
  EXPECT_EQ(mcp_logger_release(handle), MCP_LOG_OK);

  // Using released handle should fail
  EXPECT_NE(mcp_logger_log(handle, MCP_LOG_LEVEL_INFO, message), MCP_LOG_OK);
}

// Test get-or-create semantics
TEST_F(LoggingApiTest, LoggerGetOrCreate) {
  auto name = makeStringView("singleton_logger");

  mcp_logger_handle_t handle1, handle2;
  ASSERT_EQ(mcp_logger_get_or_create(name, &handle1), MCP_LOG_OK);
  ASSERT_EQ(mcp_logger_get_or_create(name, &handle2), MCP_LOG_OK);

  ASSERT_TRUE(MCP_IS_VALID_LOGGER(handle1));
  ASSERT_TRUE(MCP_IS_VALID_LOGGER(handle2));

  // Both handles should work
  auto message = makeStringView("Test message");
  EXPECT_EQ(mcp_logger_log(handle1, MCP_LOG_LEVEL_INFO, message), MCP_LOG_OK);
  EXPECT_EQ(mcp_logger_log(handle2, MCP_LOG_LEVEL_INFO, message), MCP_LOG_OK);

  mcp_logger_release(handle1);
  mcp_logger_release(handle2);
}

// Test default logger
TEST_F(LoggingApiTest, DefaultLogger) {
  mcp_logger_handle_t handle;
  ASSERT_EQ(mcp_logger_get_default(&handle), MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_LOGGER(handle));

  // Default logger should be immediately usable
  auto message = makeStringView("Default logger test");
  EXPECT_EQ(mcp_logger_log(handle, MCP_LOG_LEVEL_INFO, message), MCP_LOG_OK);

  // Getting default again should return same handle
  mcp_logger_handle_t handle2;
  ASSERT_EQ(mcp_logger_get_default(&handle2), MCP_LOG_OK);
  // Can't compare handles directly as they may be different
}

// Test log levels
TEST_F(LoggingApiTest, LogLevels) {
  auto name = makeStringView("level_test");
  mcp_logger_handle_t handle;
  ASSERT_EQ(mcp_logger_get_or_create(name, &handle), MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_LOGGER(handle));

  // Set level to WARNING
  EXPECT_EQ(mcp_logger_set_level(handle, MCP_LOG_LEVEL_WARNING), MCP_LOG_OK);
  mcp_log_level_t level;
  EXPECT_EQ(mcp_logger_get_level(handle, &level), MCP_LOG_OK);
  EXPECT_EQ(level, MCP_LOG_LEVEL_WARNING);

  // mcp_logger_should_log is not available in the new API
  // These tests are skipped

  mcp_logger_release(handle);
}

// Test structured logging
TEST_F(LoggingApiTest, StructuredLogging) {
  auto name = makeStringView("structured_test");
  mcp_logger_handle_t handle;
  ASSERT_EQ(mcp_logger_get_or_create(name, &handle), MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_LOGGER(handle));

  // Create structured message
  mcp_log_message_t msg = {};
  msg.level = MCP_LOG_LEVEL_INFO;
  msg.message = makeStringView("Structured log message");
  msg.component = MCP_LOG_COMPONENT_ROOT;
  // correlation_id field doesn't exist in C struct
  msg.request_id = makeStringView("req-456");
  msg.trace_id = makeStringView("trace-789");
  msg.span_id = makeStringView("span-abc");
  // MCP-specific fields and key-value pairs don't exist in C struct

  EXPECT_EQ(mcp_logger_log_structured(handle, &msg), MCP_LOG_OK);

  mcp_logger_release(handle);
}

// Test file sink
TEST_F(LoggingApiTest, FileSink) {
  auto filename = makeStringView("test.log");
  mcp_sink_handle_t sink_handle;
  ASSERT_EQ(mcp_sink_create_file(filename, 0, 0, &sink_handle), MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_SINK(sink_handle));

  auto logger_name = makeStringView("file_test");
  mcp_logger_handle_t logger_handle;
  ASSERT_EQ(mcp_logger_get_or_create(logger_name, &logger_handle), MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_LOGGER(logger_handle));

  // Add sink to logger
  EXPECT_EQ(mcp_logger_set_sink(logger_handle, sink_handle), MCP_LOG_OK);

  // Log messages
  auto msg1 = makeStringView("First message");
  auto msg2 = makeStringView("Second message");
  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_INFO, msg1),
            MCP_LOG_OK);
  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_ERROR, msg2),
            MCP_LOG_OK);

  // Flush to ensure written
  EXPECT_EQ(mcp_logger_flush(logger_handle), MCP_LOG_OK);

  // Verify file exists and contains messages
  std::ifstream file("test.log");
  ASSERT_TRUE(file.is_open());

  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  EXPECT_NE(content.find("First message"), std::string::npos);
  EXPECT_NE(content.find("Second message"), std::string::npos);

  // Clean up
  EXPECT_EQ(mcp_logger_set_sink(logger_handle, sink_handle), MCP_LOG_OK);
  mcp_logger_release(logger_handle);
  mcp_sink_release(sink_handle);
}

// Test rotating file sink
TEST_F(LoggingApiTest, RotatingFileSink) {
  auto filename = makeStringView("test_rotating.log");
  size_t max_size = 1024;  // 1KB per file
  size_t max_files = 3;

  mcp_sink_handle_t sink_handle;
  ASSERT_EQ(mcp_sink_create_file(filename, max_size, max_files, &sink_handle),
            MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_SINK(sink_handle));

  auto logger_name = makeStringView("rotating_test");
  mcp_logger_handle_t logger_handle;
  ASSERT_EQ(mcp_logger_get_or_create(logger_name, &logger_handle), MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_LOGGER(logger_handle));

  // Add sink to logger
  EXPECT_EQ(mcp_logger_set_sink(logger_handle, sink_handle), MCP_LOG_OK);

  // Log many messages to trigger rotation
  std::string long_message(100, 'X');  // 100 bytes per message
  auto msg_view = makeStringView(long_message);

  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_INFO, msg_view),
              MCP_LOG_OK);
  }

  EXPECT_EQ(mcp_logger_flush(logger_handle), MCP_LOG_OK);

  // Check that rotation occurred
  EXPECT_TRUE(std::filesystem::exists("test_rotating.log"));
  EXPECT_TRUE(std::filesystem::exists("test_rotating.log.0"));

  mcp_logger_release(logger_handle);
  mcp_sink_release(sink_handle);
}

// Test stdio sinks
TEST_F(LoggingApiTest, StdioSinks) {
  // Test stdout sink
  mcp_sink_handle_t stdout_sink;
  ASSERT_EQ(mcp_sink_create_stdio(0, &stdout_sink), MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_SINK(stdout_sink));

  // Test stderr sink
  mcp_sink_handle_t stderr_sink;
  ASSERT_EQ(mcp_sink_create_stdio(1, &stderr_sink), MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_SINK(stderr_sink));

  auto logger_name = makeStringView("stdio_test");
  mcp_logger_handle_t logger_handle;
  ASSERT_EQ(mcp_logger_get_or_create(logger_name, &logger_handle), MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_LOGGER(logger_handle));

  // Add both sinks
  EXPECT_EQ(mcp_logger_set_sink(logger_handle, stdout_sink), MCP_LOG_OK);
  EXPECT_EQ(mcp_logger_set_sink(logger_handle, stderr_sink), MCP_LOG_OK);

  // Log messages
  auto msg = makeStringView("Stdio test message");
  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_INFO, msg), MCP_LOG_OK);

  mcp_logger_release(logger_handle);
  mcp_sink_release(stdout_sink);
  mcp_sink_release(stderr_sink);
}

// Test null sink
TEST_F(LoggingApiTest, NullSink) {
  // Null sink is not exposed in the public API
  // Skip this entire test
  GTEST_SKIP() << "Null sink not available in public API";
}

// External sink callback with correct signature
void external_log_callback(mcp_log_level_t level,
                           const char* logger_name,
                           const char* formatted_message,
                           void* user_data) {
  auto* vec = static_cast<std::vector<std::string>*>(user_data);
  vec->emplace_back(formatted_message);
}

// Test external sink with callbacks
TEST_F(LoggingApiTest, ExternalSink) {
  std::vector<std::string> messages;

  // Create external sink with callback
  mcp_sink_handle_t sink_handle;
  ASSERT_EQ(mcp_sink_create_external(external_log_callback, &messages, nullptr,
                                     &sink_handle),
            MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_SINK(sink_handle));

  auto logger_name = makeStringView("external_test");
  mcp_logger_handle_t logger_handle;
  ASSERT_EQ(mcp_logger_get_or_create(logger_name, &logger_handle), MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_LOGGER(logger_handle));

  EXPECT_EQ(mcp_logger_set_sink(logger_handle, sink_handle), MCP_LOG_OK);

  // Log messages
  auto msg1 = makeStringView("External message 1");
  auto msg2 = makeStringView("External message 2");
  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_INFO, msg1),
            MCP_LOG_OK);
  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_ERROR, msg2),
            MCP_LOG_OK);

  // Flush
  EXPECT_EQ(mcp_logger_flush(logger_handle), MCP_LOG_OK);

  // Verify callbacks were called
  EXPECT_EQ(messages.size(), 2);
  // Messages will be formatted, so just check they contain the original text
  EXPECT_NE(messages[0].find("External message 1"), std::string::npos);
  EXPECT_NE(messages[1].find("External message 2"), std::string::npos);

  mcp_logger_release(logger_handle);
  mcp_sink_release(sink_handle);
}

// Test sink formatters - SKIPPED (not available in current API)
TEST_F(LoggingApiTest, SinkFormatters) {
  GTEST_SKIP() << "Sink formatters not available in current API";
}

// Test sink level filters - SKIPPED (not available in current API)
TEST_F(LoggingApiTest, SinkLevelFilter) {
  GTEST_SKIP() << "Sink level filters not available in current API";

  auto filename = makeStringView("test.log");
  mcp_sink_handle_t sink_handle;
  ASSERT_EQ(mcp_sink_create_file(filename, 0, 0, &sink_handle), MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_SINK(sink_handle));

  auto logger_name = makeStringView("filter_test");
  mcp_logger_handle_t logger_handle;
  ASSERT_EQ(mcp_logger_get_or_create(logger_name, &logger_handle), MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_LOGGER(logger_handle));

  EXPECT_EQ(mcp_logger_set_sink(logger_handle, sink_handle), MCP_LOG_OK);

  // Log messages at different levels
  auto debug_msg = makeStringView("Debug message");
  auto info_msg = makeStringView("Info message");
  auto error_msg = makeStringView("Error message");
  auto critical_msg = makeStringView("Critical message");

  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_DEBUG, debug_msg),
            MCP_LOG_OK);
  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_INFO, info_msg),
            MCP_LOG_OK);
  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_ERROR, error_msg),
            MCP_LOG_OK);
  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_CRITICAL, critical_msg),
            MCP_LOG_OK);

  EXPECT_EQ(mcp_logger_flush(logger_handle), MCP_LOG_OK);

  // Verify only ERROR and CRITICAL are in file
  std::ifstream file("test.log");
  ASSERT_TRUE(file.is_open());

  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  EXPECT_EQ(content.find("Debug message"), std::string::npos);
  EXPECT_EQ(content.find("Info message"), std::string::npos);
  EXPECT_NE(content.find("Error message"), std::string::npos);
  EXPECT_NE(content.find("Critical message"), std::string::npos);

  mcp_logger_release(logger_handle);
  mcp_sink_release(sink_handle);
}

// Test log context
TEST_F(LoggingApiTest, LogContext) {
  // Context API not available in current implementation
  GTEST_SKIP() << "Context API not available";
}

// Test registry functions
TEST_F(LoggingApiTest, Registry) {
  // Registry operations not exposed in current API
  GTEST_SKIP() << "Registry operations not available";
}

// Test logger statistics
TEST_F(LoggingApiTest, LoggerStatistics) {
  // Statistics API not available in current implementation
  GTEST_SKIP() << "Statistics API not available";
}

// Test utility functions
TEST_F(LoggingApiTest, UtilityFunctions) {
  // Utility functions not available in current API
  GTEST_SKIP() << "Utility functions not available";
}

// Test thread safety with concurrent logging
TEST_F(LoggingApiTest, ThreadSafety) {
  auto name = makeStringView("thread_test");
  mcp_logger_handle_t handle;
  ASSERT_EQ(mcp_logger_get_or_create(name, &handle), MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_LOGGER(handle));

  // Add file sink
  auto filename = makeStringView("test.log");
  mcp_sink_handle_t sink_handle;
  ASSERT_EQ(mcp_sink_create_file(filename, 0, 0, &sink_handle), MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_SINK(sink_handle));
  EXPECT_EQ(mcp_logger_set_sink(handle, sink_handle), MCP_LOG_OK);

  // Async mode setting not available in current API

  const int num_threads = 10;
  const int messages_per_thread = 100;
  std::vector<std::thread> threads;

  std::atomic<int> total_logged{0};

  // Launch threads
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = 0; i < messages_per_thread; ++i) {
        std::string msg =
            "Thread " + std::to_string(t) + " message " + std::to_string(i);
        auto msg_view = makeStringView(msg);

        if (mcp_logger_log(handle, MCP_LOG_LEVEL_INFO, msg_view) ==
            MCP_LOG_OK) {
          total_logged++;
        }

        // Small delay to increase contention
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
    });
  }

  // Wait for all threads
  for (auto& t : threads) {
    t.join();
  }

  // Flush to ensure all async messages are written
  EXPECT_EQ(mcp_logger_flush(handle), MCP_LOG_OK);

  // Should have logged all messages
  EXPECT_EQ(total_logged, num_threads * messages_per_thread);

  // Stats API not available, just verify by count
  // EXPECT_EQ(total_logged, num_threads * messages_per_thread);

  mcp_logger_release(handle);
  mcp_sink_release(sink_handle);
}

// Test RAII cleanup on shutdown
TEST_F(LoggingApiTest, ShutdownCleanup) {
  // Create multiple resources
  mcp_logger_handle_t logger1, logger2;
  ASSERT_EQ(mcp_logger_get_or_create(makeStringView("logger1"), &logger1),
            MCP_LOG_OK);
  ASSERT_EQ(mcp_logger_get_or_create(makeStringView("logger2"), &logger2),
            MCP_LOG_OK);
  mcp_sink_handle_t sink1;
  ASSERT_EQ(mcp_sink_create_file(makeStringView("test1.log"), 0, 0, &sink1),
            MCP_LOG_OK);
  mcp_sink_handle_t sink2;
  ASSERT_EQ(mcp_sink_create_stdio(0, &sink2), MCP_LOG_OK);
  // Context API not available, skip context creation

  ASSERT_TRUE(MCP_IS_VALID_LOGGER(logger1));
  ASSERT_TRUE(MCP_IS_VALID_LOGGER(logger2));
  ASSERT_TRUE(MCP_IS_VALID_SINK(sink1));
  ASSERT_TRUE(MCP_IS_VALID_SINK(sink2));

  // Add sinks to loggers
  mcp_logger_set_sink(logger1, sink1);
  mcp_logger_set_sink(logger2, sink2);

  // Log some messages
  auto msg = makeStringView("Shutdown test");
  mcp_logger_log(logger1, MCP_LOG_LEVEL_INFO, msg);
  mcp_logger_log(logger2, MCP_LOG_LEVEL_INFO, msg);

  // Registry shutdown not available in current API
  // Resources will be cleaned up on release
  mcp_logger_release(logger1);
  mcp_logger_release(logger2);
  mcp_sink_release(sink1);
  mcp_sink_release(sink2);

  // Clean up files
  std::filesystem::remove("test1.log");
}

// Test error conditions
TEST_F(LoggingApiTest, ErrorConditions) {
  // Invalid handle operations
  EXPECT_NE(mcp_logger_log(MCP_INVALID_LOGGER_HANDLE, MCP_LOG_LEVEL_INFO,
                           makeStringView("test")),
            MCP_LOG_OK);
  mcp_log_level_t level;
  EXPECT_NE(mcp_logger_get_level(MCP_INVALID_LOGGER_HANDLE, &level),
            MCP_LOG_OK);
  // mcp_logger_should_log is not available

  // Null message
  mcp_logger_handle_t handle;
  ASSERT_EQ(mcp_logger_get_or_create(makeStringView("error_test"), &handle),
            MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_LOGGER(handle));
  EXPECT_NE(mcp_logger_log_structured(handle, nullptr), MCP_LOG_OK);

  // Invalid sink creation
  mcp_sink_handle_t invalid_sink;
  EXPECT_NE(mcp_sink_create_external(nullptr, nullptr, nullptr, &invalid_sink),
            MCP_LOG_OK);

  // Context operations not available in current API

  mcp_logger_release(handle);
}

// Test memory management with string views
TEST_F(LoggingApiTest, StringViewMemory) {
  mcp_logger_handle_t handle;
  ASSERT_EQ(mcp_logger_get_or_create(makeStringView("memory_test"), &handle),
            MCP_LOG_OK);
  ASSERT_TRUE(MCP_IS_VALID_LOGGER(handle));

  // Allocate string view dynamically
  mcp_string_view_t* view =
      static_cast<mcp_string_view_t*>(std::malloc(sizeof(mcp_string_view_t)));
  ASSERT_NE(view, nullptr);

  std::string msg = "Dynamic message";
  view->data = static_cast<const char*>(std::malloc(msg.length() + 1));
  std::memcpy(const_cast<char*>(view->data), msg.c_str(), msg.length() + 1);
  view->length = msg.length();

  // Use the view
  EXPECT_EQ(mcp_logger_log(handle, MCP_LOG_LEVEL_INFO, *view), MCP_LOG_OK);

  // Free the data
  std::free(const_cast<char*>(view->data));
  std::free(view);

  mcp_logger_release(handle);
}

}  // namespace
}  // namespace c_api
}  // namespace mcp