/**
 * @file test_mcp_logging_api.cc
 * @brief Comprehensive tests for MCP Logging C API with RAII enforcement
 */

#include <gtest/gtest.h>
#include "mcp/c_api/mcp_c_logging_api.h"
#include "mcp/c_api/mcp_c_types.h"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <cstdlib>

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
    mcp_registry_set_default_level(MCP_LOG_LEVEL_INFO);
    mcp_registry_set_default_mode(MCP_LOG_MODE_SYNC);
  }
  
  void TearDown() override {
    // Flush all loggers
    mcp_registry_flush_all();
    
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
  auto handle = mcp_logger_create(name);
  
  ASSERT_NE(handle, MCP_INVALID_HANDLE);
  
  // Logger should be usable
  auto message = makeStringView("Test message");
  EXPECT_EQ(mcp_logger_log(handle, MCP_LOG_LEVEL_INFO, message), MCP_LOG_OK);
  
  // Destroy should clean up resources
  mcp_logger_destroy(handle);
  
  // Using destroyed handle should fail
  EXPECT_NE(mcp_logger_log(handle, MCP_LOG_LEVEL_INFO, message), MCP_LOG_OK);
}

// Test get-or-create semantics
TEST_F(LoggingApiTest, LoggerGetOrCreate) {
  auto name = makeStringView("singleton_logger");
  
  auto handle1 = mcp_logger_get_or_create(name);
  auto handle2 = mcp_logger_get_or_create(name);
  
  ASSERT_NE(handle1, MCP_INVALID_HANDLE);
  ASSERT_NE(handle2, MCP_INVALID_HANDLE);
  
  // Both handles should work
  auto message = makeStringView("Test message");
  EXPECT_EQ(mcp_logger_log(handle1, MCP_LOG_LEVEL_INFO, message), MCP_LOG_OK);
  EXPECT_EQ(mcp_logger_log(handle2, MCP_LOG_LEVEL_INFO, message), MCP_LOG_OK);
  
  mcp_logger_destroy(handle1);
  mcp_logger_destroy(handle2);
}

// Test default logger
TEST_F(LoggingApiTest, DefaultLogger) {
  auto handle = mcp_logger_get_default();
  ASSERT_NE(handle, MCP_INVALID_HANDLE);
  
  // Default logger should be immediately usable
  auto message = makeStringView("Default logger test");
  EXPECT_EQ(mcp_logger_log(handle, MCP_LOG_LEVEL_INFO, message), MCP_LOG_OK);
  
  // Getting default again should return same handle
  auto handle2 = mcp_logger_get_default();
  EXPECT_EQ(handle, handle2);
}

// Test log levels
TEST_F(LoggingApiTest, LogLevels) {
  auto name = makeStringView("level_test");
  auto handle = mcp_logger_create(name);
  ASSERT_NE(handle, MCP_INVALID_HANDLE);
  
  // Set level to WARNING
  EXPECT_EQ(mcp_logger_set_level(handle, MCP_LOG_LEVEL_WARNING), MCP_LOG_OK);
  EXPECT_EQ(mcp_logger_get_level(handle), MCP_LOG_LEVEL_WARNING);
  
  // Should not log DEBUG or INFO
  EXPECT_EQ(mcp_logger_should_log(handle, MCP_LOG_LEVEL_DEBUG), MCP_FALSE);
  EXPECT_EQ(mcp_logger_should_log(handle, MCP_LOG_LEVEL_INFO), MCP_FALSE);
  
  // Should log WARNING and above
  EXPECT_EQ(mcp_logger_should_log(handle, MCP_LOG_LEVEL_WARNING), MCP_TRUE);
  EXPECT_EQ(mcp_logger_should_log(handle, MCP_LOG_LEVEL_ERROR), MCP_TRUE);
  EXPECT_EQ(mcp_logger_should_log(handle, MCP_LOG_LEVEL_CRITICAL), MCP_TRUE);
  
  mcp_logger_destroy(handle);
}

// Test structured logging
TEST_F(LoggingApiTest, StructuredLogging) {
  auto name = makeStringView("structured_test");
  auto handle = mcp_logger_create(name);
  ASSERT_NE(handle, MCP_INVALID_HANDLE);
  
  // Create structured message
  mcp_log_message_t msg = {};
  msg.level = MCP_LOG_LEVEL_INFO;
  msg.message = makeStringView("Structured log message");
  msg.component = makeStringView("TestComponent");
  msg.correlation_id = makeStringView("corr-123");
  msg.request_id = makeStringView("req-456");
  msg.trace_id = makeStringView("trace-789");
  msg.span_id = makeStringView("span-abc");
  msg.mcp_method = makeStringView("test.method");
  msg.mcp_resource = makeStringView("/test/resource");
  msg.mcp_tool = makeStringView("test_tool");
  
  // Add key-value pairs
  mcp_key_value_pair_t kvs[2] = {
    {makeStringView("key1"), makeStringView("value1")},
    {makeStringView("key2"), makeStringView("value2")}
  };
  msg.key_value_pairs = kvs;
  msg.key_value_count = 2;
  
  EXPECT_EQ(mcp_logger_log_structured(handle, &msg), MCP_LOG_OK);
  
  mcp_logger_destroy(handle);
}

// Test file sink
TEST_F(LoggingApiTest, FileSink) {
  auto filename = makeStringView("test.log");
  auto sink_handle = mcp_sink_create_file(filename);
  ASSERT_NE(sink_handle, MCP_INVALID_HANDLE);
  
  auto logger_name = makeStringView("file_test");
  auto logger_handle = mcp_logger_create(logger_name);
  ASSERT_NE(logger_handle, MCP_INVALID_HANDLE);
  
  // Add sink to logger
  EXPECT_EQ(mcp_logger_add_sink(logger_handle, sink_handle), MCP_LOG_OK);
  
  // Log messages
  auto msg1 = makeStringView("First message");
  auto msg2 = makeStringView("Second message");
  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_INFO, msg1), MCP_LOG_OK);
  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_ERROR, msg2), MCP_LOG_OK);
  
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
  EXPECT_EQ(mcp_logger_remove_sink(logger_handle, sink_handle), MCP_LOG_OK);
  mcp_logger_destroy(logger_handle);
  mcp_sink_destroy(sink_handle);
}

// Test rotating file sink
TEST_F(LoggingApiTest, RotatingFileSink) {
  auto filename = makeStringView("test_rotating.log");
  size_t max_size = 1024;  // 1KB per file
  size_t max_files = 3;
  
  auto sink_handle = mcp_sink_create_rotating_file(filename, max_size, max_files);
  ASSERT_NE(sink_handle, MCP_INVALID_HANDLE);
  
  auto logger_name = makeStringView("rotating_test");
  auto logger_handle = mcp_logger_create(logger_name);
  ASSERT_NE(logger_handle, MCP_INVALID_HANDLE);
  
  // Add sink to logger
  EXPECT_EQ(mcp_logger_add_sink(logger_handle, sink_handle), MCP_LOG_OK);
  
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
  
  mcp_logger_destroy(logger_handle);
  mcp_sink_destroy(sink_handle);
}

// Test stdio sinks
TEST_F(LoggingApiTest, StdioSinks) {
  // Test stdout sink
  auto stdout_sink = mcp_sink_create_stdio(MCP_FALSE);
  ASSERT_NE(stdout_sink, MCP_INVALID_HANDLE);
  
  // Test stderr sink
  auto stderr_sink = mcp_sink_create_stdio(MCP_TRUE);
  ASSERT_NE(stderr_sink, MCP_INVALID_HANDLE);
  
  auto logger_name = makeStringView("stdio_test");
  auto logger_handle = mcp_logger_create(logger_name);
  ASSERT_NE(logger_handle, MCP_INVALID_HANDLE);
  
  // Add both sinks
  EXPECT_EQ(mcp_logger_add_sink(logger_handle, stdout_sink), MCP_LOG_OK);
  EXPECT_EQ(mcp_logger_add_sink(logger_handle, stderr_sink), MCP_LOG_OK);
  
  // Log messages
  auto msg = makeStringView("Stdio test message");
  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_INFO, msg), MCP_LOG_OK);
  
  mcp_logger_destroy(logger_handle);
  mcp_sink_destroy(stdout_sink);
  mcp_sink_destroy(stderr_sink);
}

// Test null sink
TEST_F(LoggingApiTest, NullSink) {
  auto sink_handle = mcp_sink_create_null();
  ASSERT_NE(sink_handle, MCP_INVALID_HANDLE);
  
  auto logger_name = makeStringView("null_test");
  auto logger_handle = mcp_logger_create(logger_name);
  ASSERT_NE(logger_handle, MCP_INVALID_HANDLE);
  
  EXPECT_EQ(mcp_logger_add_sink(logger_handle, sink_handle), MCP_LOG_OK);
  
  // Logging to null sink should succeed but do nothing
  auto msg = makeStringView("This goes nowhere");
  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_INFO, msg), MCP_LOG_OK);
  
  mcp_logger_destroy(logger_handle);
  mcp_sink_destroy(sink_handle);
}

// Test external sink with callbacks
TEST_F(LoggingApiTest, ExternalSink) {
  struct CallbackData {
    std::atomic<int> write_count{0};
    std::atomic<int> flush_count{0};
    std::vector<std::string> messages;
    std::mutex mutex;
  };
  
  CallbackData data;
  
  auto write_callback = [](const mcp_log_message_t* msg, void* user_data) {
    auto* data = static_cast<CallbackData*>(user_data);
    data->write_count++;
    
    std::lock_guard<std::mutex> lock(data->mutex);
    data->messages.emplace_back(msg->message.data, msg->message.length);
  };
  
  auto flush_callback = [](void* user_data) {
    auto* data = static_cast<CallbackData*>(user_data);
    data->flush_count++;
  };
  
  auto sink_handle = mcp_sink_create_external(write_callback, flush_callback, &data);
  ASSERT_NE(sink_handle, MCP_INVALID_HANDLE);
  
  auto logger_name = makeStringView("external_test");
  auto logger_handle = mcp_logger_create(logger_name);
  ASSERT_NE(logger_handle, MCP_INVALID_HANDLE);
  
  EXPECT_EQ(mcp_logger_add_sink(logger_handle, sink_handle), MCP_LOG_OK);
  
  // Log messages
  auto msg1 = makeStringView("External message 1");
  auto msg2 = makeStringView("External message 2");
  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_INFO, msg1), MCP_LOG_OK);
  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_ERROR, msg2), MCP_LOG_OK);
  
  // Flush
  EXPECT_EQ(mcp_logger_flush(logger_handle), MCP_LOG_OK);
  
  // Verify callbacks were called
  EXPECT_EQ(data.write_count, 2);
  EXPECT_EQ(data.flush_count, 1);
  EXPECT_EQ(data.messages.size(), 2);
  EXPECT_EQ(data.messages[0], "External message 1");
  EXPECT_EQ(data.messages[1], "External message 2");
  
  mcp_logger_destroy(logger_handle);
  mcp_sink_destroy(sink_handle);
}

// Test sink formatters
TEST_F(LoggingApiTest, SinkFormatters) {
  auto filename = makeStringView("test.log");
  auto sink_handle = mcp_sink_create_file(filename);
  ASSERT_NE(sink_handle, MCP_INVALID_HANDLE);
  
  // Set JSON formatter
  EXPECT_EQ(mcp_sink_set_formatter(sink_handle, MCP_FORMATTER_JSON), MCP_LOG_OK);
  
  auto logger_name = makeStringView("formatter_test");
  auto logger_handle = mcp_logger_create(logger_name);
  ASSERT_NE(logger_handle, MCP_INVALID_HANDLE);
  
  EXPECT_EQ(mcp_logger_add_sink(logger_handle, sink_handle), MCP_LOG_OK);
  
  // Log a message
  auto msg = makeStringView("JSON formatted message");
  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_INFO, msg), MCP_LOG_OK);
  EXPECT_EQ(mcp_logger_flush(logger_handle), MCP_LOG_OK);
  
  // Verify JSON format in file
  std::ifstream file("test.log");
  ASSERT_TRUE(file.is_open());
  
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  // JSON format should have quotes and braces
  EXPECT_NE(content.find("{"), std::string::npos);
  EXPECT_NE(content.find("\"message\""), std::string::npos);
  
  mcp_logger_destroy(logger_handle);
  mcp_sink_destroy(sink_handle);
}

// Test sink level filters
TEST_F(LoggingApiTest, SinkLevelFilter) {
  auto filename = makeStringView("test.log");
  auto sink_handle = mcp_sink_create_file(filename);
  ASSERT_NE(sink_handle, MCP_INVALID_HANDLE);
  
  // Set sink to only accept ERROR and above
  EXPECT_EQ(mcp_sink_set_level_filter(sink_handle, MCP_LOG_LEVEL_ERROR), 
            MCP_LOG_OK);
  
  auto logger_name = makeStringView("filter_test");
  auto logger_handle = mcp_logger_create(logger_name);
  ASSERT_NE(logger_handle, MCP_INVALID_HANDLE);
  
  EXPECT_EQ(mcp_logger_add_sink(logger_handle, sink_handle), MCP_LOG_OK);
  
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
  
  mcp_logger_destroy(logger_handle);
  mcp_sink_destroy(sink_handle);
}

// Test log context
TEST_F(LoggingApiTest, LogContext) {
  // Create context with initial data
  auto corr_id = makeStringView("correlation-123");
  auto req_id = makeStringView("request-456");
  auto ctx_handle = mcp_context_create_with_data(corr_id, req_id);
  ASSERT_NE(ctx_handle, MCP_INVALID_HANDLE);
  
  // Add latency
  EXPECT_EQ(mcp_context_add_latency(ctx_handle, 10.5), MCP_LOG_OK);
  EXPECT_EQ(mcp_context_add_latency(ctx_handle, 20.3), MCP_LOG_OK);
  
  // Create another context and propagate
  auto ctx2_handle = mcp_context_create();
  ASSERT_NE(ctx2_handle, MCP_INVALID_HANDLE);
  
  EXPECT_EQ(mcp_context_propagate(ctx_handle, ctx2_handle), MCP_LOG_OK);
  
  // Update second context
  auto new_req_id = makeStringView("request-789");
  EXPECT_EQ(mcp_context_set_request_id(ctx2_handle, new_req_id), MCP_LOG_OK);
  
  mcp_context_destroy(ctx_handle);
  mcp_context_destroy(ctx2_handle);
}

// Test registry functions
TEST_F(LoggingApiTest, Registry) {
  // Set default level
  EXPECT_EQ(mcp_registry_set_default_level(MCP_LOG_LEVEL_WARNING), MCP_LOG_OK);
  
  // Create new logger - should inherit default level
  auto name = makeStringView("registry_test");
  auto handle = mcp_logger_create(name);
  ASSERT_NE(handle, MCP_INVALID_HANDLE);
  
  EXPECT_EQ(mcp_logger_get_level(handle), MCP_LOG_LEVEL_WARNING);
  
  // Set pattern-specific level
  auto pattern = makeStringView("registry_*");
  EXPECT_EQ(mcp_registry_set_pattern_level(pattern, MCP_LOG_LEVEL_DEBUG), 
            MCP_LOG_OK);
  
  // Flush all loggers
  EXPECT_EQ(mcp_registry_flush_all(), MCP_LOG_OK);
  
  mcp_logger_destroy(handle);
}

// Test logger statistics
TEST_F(LoggingApiTest, LoggerStatistics) {
  auto name = makeStringView("stats_test");
  auto handle = mcp_logger_create(name);
  ASSERT_NE(handle, MCP_INVALID_HANDLE);
  
  // Add a file sink
  auto filename = makeStringView("test.log");
  auto sink_handle = mcp_sink_create_file(filename);
  ASSERT_NE(sink_handle, MCP_INVALID_HANDLE);
  EXPECT_EQ(mcp_logger_add_sink(handle, sink_handle), MCP_LOG_OK);
  
  // Log some messages
  auto msg = makeStringView("Test message for stats");
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(mcp_logger_log(handle, MCP_LOG_LEVEL_INFO, msg), MCP_LOG_OK);
  }
  
  // Flush
  EXPECT_EQ(mcp_logger_flush(handle), MCP_LOG_OK);
  
  // Get statistics
  mcp_logger_stats_t stats;
  mcp_logger_get_stats(handle, &stats);
  
  EXPECT_EQ(stats.messages_logged, 5);
  EXPECT_EQ(stats.messages_dropped, 0);
  EXPECT_GT(stats.bytes_written, 0);
  EXPECT_GT(stats.flush_count, 0);
  EXPECT_EQ(stats.error_count, 0);
  
  mcp_logger_destroy(handle);
  mcp_sink_destroy(sink_handle);
}

// Test utility functions
TEST_F(LoggingApiTest, UtilityFunctions) {
  // Test level to string
  EXPECT_STREQ(mcp_log_level_to_string(MCP_LOG_LEVEL_DEBUG), "DEBUG");
  EXPECT_STREQ(mcp_log_level_to_string(MCP_LOG_LEVEL_INFO), "INFO");
  EXPECT_STREQ(mcp_log_level_to_string(MCP_LOG_LEVEL_WARNING), "WARNING");
  EXPECT_STREQ(mcp_log_level_to_string(MCP_LOG_LEVEL_ERROR), "ERROR");
  
  // Test level from string
  auto debug_str = makeStringView("debug");
  auto info_str = makeStringView("INFO");
  auto warn_str = makeStringView("warning");
  auto error_str = makeStringView("ERROR");
  
  EXPECT_EQ(mcp_log_level_from_string(debug_str), MCP_LOG_LEVEL_DEBUG);
  EXPECT_EQ(mcp_log_level_from_string(info_str), MCP_LOG_LEVEL_INFO);
  EXPECT_EQ(mcp_log_level_from_string(warn_str), MCP_LOG_LEVEL_WARNING);
  EXPECT_EQ(mcp_log_level_from_string(error_str), MCP_LOG_LEVEL_ERROR);
  
  // Test unknown string defaults to INFO
  auto unknown_str = makeStringView("unknown");
  EXPECT_EQ(mcp_log_level_from_string(unknown_str), MCP_LOG_LEVEL_INFO);
}

// Test thread safety with concurrent logging
TEST_F(LoggingApiTest, ThreadSafety) {
  auto name = makeStringView("thread_test");
  auto handle = mcp_logger_create(name);
  ASSERT_NE(handle, MCP_INVALID_HANDLE);
  
  // Add file sink
  auto filename = makeStringView("test.log");
  auto sink_handle = mcp_sink_create_file(filename);
  ASSERT_NE(sink_handle, MCP_INVALID_HANDLE);
  EXPECT_EQ(mcp_logger_add_sink(handle, sink_handle), MCP_LOG_OK);
  
  // Set to async mode for better concurrency testing
  EXPECT_EQ(mcp_registry_set_default_mode(MCP_LOG_MODE_ASYNC), MCP_LOG_OK);
  
  const int num_threads = 10;
  const int messages_per_thread = 100;
  std::vector<std::thread> threads;
  
  std::atomic<int> total_logged{0};
  
  // Launch threads
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = 0; i < messages_per_thread; ++i) {
        std::string msg = "Thread " + std::to_string(t) + " message " + std::to_string(i);
        auto msg_view = makeStringView(msg);
        
        if (mcp_logger_log(handle, MCP_LOG_LEVEL_INFO, msg_view) == MCP_LOG_OK) {
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
  
  // Get stats to verify
  mcp_logger_stats_t stats;
  mcp_logger_get_stats(handle, &stats);
  EXPECT_EQ(stats.messages_logged, total_logged);
  
  mcp_logger_destroy(handle);
  mcp_sink_destroy(sink_handle);
}

// Test RAII cleanup on shutdown
TEST_F(LoggingApiTest, ShutdownCleanup) {
  // Create multiple resources
  auto logger1 = mcp_logger_create(makeStringView("logger1"));
  auto logger2 = mcp_logger_create(makeStringView("logger2"));
  auto sink1 = mcp_sink_create_file(makeStringView("test1.log"));
  auto sink2 = mcp_sink_create_stdio(MCP_FALSE);
  auto ctx1 = mcp_context_create();
  
  ASSERT_NE(logger1, MCP_INVALID_HANDLE);
  ASSERT_NE(logger2, MCP_INVALID_HANDLE);
  ASSERT_NE(sink1, MCP_INVALID_HANDLE);
  ASSERT_NE(sink2, MCP_INVALID_HANDLE);
  ASSERT_NE(ctx1, MCP_INVALID_HANDLE);
  
  // Add sinks to loggers
  mcp_logger_add_sink(logger1, sink1);
  mcp_logger_add_sink(logger2, sink2);
  
  // Log some messages
  auto msg = makeStringView("Shutdown test");
  mcp_logger_log(logger1, MCP_LOG_LEVEL_INFO, msg);
  mcp_logger_log(logger2, MCP_LOG_LEVEL_INFO, msg);
  
  // Shutdown should clean everything
  mcp_registry_shutdown();
  
  // All handles should now be invalid
  EXPECT_NE(mcp_logger_log(logger1, MCP_LOG_LEVEL_INFO, msg), MCP_LOG_OK);
  EXPECT_NE(mcp_logger_log(logger2, MCP_LOG_LEVEL_INFO, msg), MCP_LOG_OK);
  
  // Clean up files
  std::filesystem::remove("test1.log");
}

// Test error conditions
TEST_F(LoggingApiTest, ErrorConditions) {
  // Invalid handle operations
  EXPECT_NE(mcp_logger_log(MCP_INVALID_HANDLE, MCP_LOG_LEVEL_INFO, 
                           makeStringView("test")), MCP_LOG_OK);
  EXPECT_EQ(mcp_logger_get_level(MCP_INVALID_HANDLE), MCP_LOG_LEVEL_OFF);
  EXPECT_EQ(mcp_logger_should_log(MCP_INVALID_HANDLE, MCP_LOG_LEVEL_INFO), MCP_FALSE);
  
  // Null message
  auto handle = mcp_logger_create(makeStringView("error_test"));
  ASSERT_NE(handle, MCP_INVALID_HANDLE);
  EXPECT_NE(mcp_logger_log_structured(handle, nullptr), MCP_LOG_OK);
  
  // Invalid sink creation
  EXPECT_EQ(mcp_sink_create_external(nullptr, nullptr, nullptr), MCP_INVALID_HANDLE);
  
  // Invalid context operations
  EXPECT_NE(mcp_context_propagate(MCP_INVALID_HANDLE, MCP_INVALID_HANDLE), 
            MCP_LOG_OK);
  
  mcp_logger_destroy(handle);
}

// Test memory management with string views
TEST_F(LoggingApiTest, StringViewMemory) {
  auto handle = mcp_logger_create(makeStringView("memory_test"));
  ASSERT_NE(handle, MCP_INVALID_HANDLE);
  
  // Allocate string view dynamically
  mcp_string_view_t* view = static_cast<mcp_string_view_t*>(
    std::malloc(sizeof(mcp_string_view_t)));
  ASSERT_NE(view, nullptr);
  
  std::string msg = "Dynamic message";
  view->data = static_cast<const char*>(std::malloc(msg.length() + 1));
  std::memcpy(const_cast<char*>(view->data), msg.c_str(), msg.length() + 1);
  view->length = msg.length();
  
  // Use the view
  EXPECT_EQ(mcp_logger_log(handle, MCP_LOG_LEVEL_INFO, *view), MCP_LOG_OK);
  
  // Free the view
  mcp_free_string_view(view);
  std::free(view);
  
  mcp_logger_destroy(handle);
}

} // namespace
} // namespace c_api
} // namespace mcp