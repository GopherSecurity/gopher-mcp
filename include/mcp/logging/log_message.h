#pragma once

#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <unistd.h>

#include "mcp/logging/log_level.h"

namespace mcp {
namespace logging {

// Enhanced log message with comprehensive metadata
struct LogMessage {
  LogLevel level;
  std::string message;
  std::chrono::system_clock::time_point timestamp;

  // Component information
  Component component{Component::Root};
  std::string component_name;
  std::string layer_name;

  // Source location
  const char* file{nullptr};
  int line{0};
  const char* function{nullptr};

  // Process and thread info
  pid_t process_id{0};
  std::thread::id thread_id;
  uint32_t dispatcher_id{0};

  // Correlation IDs for tracing
  std::string trace_id;
  std::string span_id;
  std::string parent_span_id;
  std::string request_id;
  std::string connection_id;
  std::string stream_id;
  std::string session_id;

  // Performance metrics
  std::chrono::nanoseconds latency{0};
  size_t bytes_processed{0};
  size_t messages_processed{0};
  double duration_ms{0};
  uint64_t bytes_sent{0};
  uint64_t bytes_received{0};
  double accumulated_latency{0};

  // MCP-specific fields
  std::string tool_name;
  std::string resource_uri;
  std::string method_name;
  std::string mcp_method;
  std::string mcp_resource;
  std::string mcp_tool;

  // Custom metadata (using std::map)
  std::map<std::string, std::string> metadata;
  std::map<std::string, std::string> key_values;

  // Logger information
  std::string logger_name;

  // Formatted message cache
  mutable std::string formatted_message;

  LogMessage()
      : timestamp(std::chrono::system_clock::now()),
        process_id(getpid()),
        thread_id(std::this_thread::get_id()) {}
};

// Context that flows through filter chain
class LogContext {
 public:
  // Core context fields
  std::string trace_id;
  std::string span_id;
  std::string connection_id;
  std::string stream_id;
  std::string request_id;

  // Component and layer info
  Component component{Component::Root};
  std::string component_name;
  std::string layer_name;

  // MCP-specific fields
  std::string session_id;
  std::string tool_name;
  std::string resource_uri;

  // Performance tracking
  std::chrono::steady_clock::time_point start_time;
  std::chrono::nanoseconds accumulated_latency{0};

  // Custom metadata (using std::map)
  std::map<std::string, std::string> metadata;

  // Thread and timing info
  std::thread::id thread_id;
  std::chrono::system_clock::time_point timestamp;
  uint32_t dispatcher_id{0};

  // Source location
  void setLocation(const char* file, int line, const char* func) {
    source_file = file;
    source_line = line;
    source_function = func;
  }

  const char* getFile() const { return source_file; }
  int getLine() const { return source_line; }
  const char* getFunction() const { return source_function; }

  // Merge contexts as they flow through filters
  void merge(const LogContext& other) {
    if (!other.trace_id.empty())
      trace_id = other.trace_id;
    if (!other.span_id.empty())
      span_id = other.span_id;
    if (!other.connection_id.empty())
      connection_id = other.connection_id;
    if (!other.stream_id.empty())
      stream_id = other.stream_id;
    if (!other.request_id.empty())
      request_id = other.request_id;
    if (!other.session_id.empty())
      session_id = other.session_id;
    if (!other.tool_name.empty())
      tool_name = other.tool_name;
    if (!other.resource_uri.empty())
      resource_uri = other.resource_uri;

    // Merge metadata
    for (const auto& kv : other.metadata) {
      metadata[kv.first] = kv.second;
    }
  }

  // Convert to LogMessage
  LogMessage toLogMessage(LogLevel level, const std::string& msg) const {
    LogMessage log_msg;
    log_msg.level = level;
    log_msg.message = msg;
    log_msg.timestamp = timestamp;

    log_msg.component = component;
    log_msg.component_name = component_name;
    log_msg.layer_name = layer_name;

    log_msg.file = source_file;
    log_msg.line = source_line;
    log_msg.function = source_function;

    log_msg.thread_id = thread_id;
    log_msg.dispatcher_id = dispatcher_id;

    log_msg.trace_id = trace_id;
    log_msg.span_id = span_id;
    log_msg.connection_id = connection_id;
    log_msg.stream_id = stream_id;
    log_msg.request_id = request_id;
    log_msg.session_id = session_id;

    log_msg.tool_name = tool_name;
    log_msg.resource_uri = resource_uri;

    log_msg.accumulated_latency =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
            accumulated_latency)
            .count();
    log_msg.metadata = metadata;

    return log_msg;
  }

  LogContext()
      : thread_id(std::this_thread::get_id()),
        timestamp(std::chrono::system_clock::now()) {}

 private:
  const char* source_file{nullptr};
  int source_line{0};
  const char* source_function{nullptr};
};

}  // namespace logging
}  // namespace mcp