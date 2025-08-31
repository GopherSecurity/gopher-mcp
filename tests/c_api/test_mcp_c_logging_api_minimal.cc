/**
 * @file test_mcp_c_logging_api_minimal.cc
 * @brief Minimal tests for MCP Logging C API to ensure it compiles
 */

#include <gtest/gtest.h>
#include "mcp/c_api/mcp_c_logging_api.h"
#include "mcp/c_api/mcp_c_types.h"

namespace mcp {
namespace c_api {
namespace {

// Helper to create string views
mcp_string_view_t makeStringView(const std::string& str) {
  mcp_string_view_t view;
  view.data = str.c_str();
  view.length = str.length();
  return view;
}

// Test basic logger creation and usage
TEST(LoggingApiTest, BasicLogger) {
  auto name = makeStringView("test_logger");
  mcp_logger_handle_t handle;
  ASSERT_EQ(mcp_logger_get_or_create(name, &handle), MCP_LOG_OK);
  ASSERT_NE(handle, MCP_INVALID_HANDLE);
  
  // Log a message
  auto message = makeStringView("Test message");
  EXPECT_EQ(mcp_logger_log(handle, MCP_LOG_LEVEL_INFO, message), MCP_LOG_OK);
  
  // Release logger
  EXPECT_EQ(mcp_logger_release(handle), MCP_LOG_OK);
}

// Test file sink
TEST(LoggingApiTest, FileSink) {
  auto filename = makeStringView("test.log");
  mcp_sink_handle_t sink_handle;
  ASSERT_EQ(mcp_sink_create_file(filename, 0, 0, &sink_handle), MCP_LOG_OK);
  ASSERT_NE(sink_handle, MCP_INVALID_HANDLE);
  
  auto logger_name = makeStringView("file_test");
  mcp_logger_handle_t logger_handle;
  ASSERT_EQ(mcp_logger_get_or_create(logger_name, &logger_handle), MCP_LOG_OK);
  
  // Set sink
  EXPECT_EQ(mcp_logger_set_sink(logger_handle, sink_handle), MCP_LOG_OK);
  
  // Log message
  auto msg = makeStringView("File test message");
  EXPECT_EQ(mcp_logger_log(logger_handle, MCP_LOG_LEVEL_INFO, msg), MCP_LOG_OK);
  
  // Clean up
  mcp_logger_release(logger_handle);
  mcp_sink_release(sink_handle);
  
  // Clean up test file
  std::remove("test.log");
}

// Test stdio sinks
TEST(LoggingApiTest, StdioSink) {
  mcp_sink_handle_t sink_handle;
  ASSERT_EQ(mcp_sink_create_stdio(0, &sink_handle), MCP_LOG_OK);
  ASSERT_NE(sink_handle, MCP_INVALID_HANDLE);
  
  mcp_sink_release(sink_handle);
}

} // namespace
} // namespace c_api
} // namespace mcp
