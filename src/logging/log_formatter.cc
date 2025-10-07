#include "mcp/logging/log_formatter.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace mcp {
namespace logging {

// Helper function to format timestamp
static std::string formatTimestamp(
    const std::chrono::system_clock::time_point& tp) {
  auto time_t = std::chrono::system_clock::to_time_t(tp);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                tp.time_since_epoch()) %
            1000;

  std::tm tm_buf;
#ifdef _WIN32
  localtime_s(&tm_buf, &time_t);
#else
  localtime_r(&time_t, &tm_buf);
#endif

  std::ostringstream oss;
  oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
  oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

  return oss.str();
}

// DefaultFormatter implementation
std::string DefaultFormatter::format(const LogMessage& msg) const {
  std::ostringstream oss;

  // Timestamp
  oss << '[' << formatTimestamp(msg.timestamp) << "] ";

  // Log level
  oss << '[' << logLevelToString(msg.level) << "] ";

  // Thread ID
  oss << "[T:" << msg.thread_id << "] ";

  // Component if present
  if (msg.component != Component::Root) {
    oss << '[' << componentToString(msg.component);
    if (!msg.component_name.empty()) {
      oss << '.' << msg.component_name;
    }
    oss << "] ";
  }

  // Logger name
  oss << '[' << msg.logger_name << "] ";

  // Location if present
  if (msg.file && msg.line > 0) {
    oss << '[' << msg.file << ':' << msg.line;
    if (msg.function) {
      oss << " " << msg.function << "()";
    }
    oss << "] ";
  }

  // Trace/Request IDs if present
  if (!msg.trace_id.empty()) {
    oss << "[trace:" << msg.trace_id << "] ";
  }
  if (!msg.request_id.empty()) {
    oss << "[req:" << msg.request_id << "] ";
  }

  // Message
  oss << msg.message;

  // Key-value pairs if present
  if (!msg.key_values.empty()) {
    oss << " {";
    bool first = true;
    for (const auto& kv : msg.key_values) {
      const auto& key = kv.first;
      const auto& value = kv.second;
      if (!first)
        oss << ", ";
      oss << key << "=" << value;
      first = false;
    }
    oss << "}";
  }

  return oss.str();
}

// JsonFormatter implementation
std::string JsonFormatter::format(const LogMessage& msg) const {
  std::ostringstream oss;

  oss << "{";

  // Timestamp in ISO format
  oss << "\"timestamp\":\"" << formatTimestamp(msg.timestamp) << "\"";

  // Level
  oss << ",\"level\":\"" << logLevelToString(msg.level) << "\"";

  // Logger name
  oss << ",\"logger\":\"" << escapeJson(msg.logger_name) << "\"";

  // Thread ID
  oss << ",\"thread\":\"" << msg.thread_id << "\"";

  // Process ID if available
  if (msg.process_id > 0) {
    oss << ",\"pid\":" << msg.process_id;
  }

  // Component
  if (msg.component != Component::Root) {
    oss << ",\"component\":\"" << componentToString(msg.component) << "\"";
    if (!msg.component_name.empty()) {
      oss << ",\"component_name\":\"" << escapeJson(msg.component_name) << "\"";
    }
  }

  // Location
  if (msg.file) {
    oss << ",\"file\":\"" << escapeJson(msg.file) << "\"";
    oss << ",\"line\":" << msg.line;
    if (msg.function) {
      oss << ",\"function\":\"" << escapeJson(msg.function) << "\"";
    }
  }

  // Correlation IDs
  if (!msg.trace_id.empty()) {
    oss << ",\"trace_id\":\"" << escapeJson(msg.trace_id) << "\"";
  }
  if (!msg.request_id.empty()) {
    oss << ",\"request_id\":\"" << escapeJson(msg.request_id) << "\"";
  }

  // Message
  oss << ",\"message\":\"" << escapeJson(msg.message) << "\"";

  // MCP specific fields
  if (!msg.mcp_method.empty()) {
    oss << ",\"mcp_method\":\"" << escapeJson(msg.mcp_method) << "\"";
  }
  if (!msg.mcp_resource.empty()) {
    oss << ",\"mcp_resource\":\"" << escapeJson(msg.mcp_resource) << "\"";
  }
  if (!msg.mcp_tool.empty()) {
    oss << ",\"mcp_tool\":\"" << escapeJson(msg.mcp_tool) << "\"";
  }

  // Performance metrics
  if (msg.duration_ms > 0) {
    oss << ",\"duration_ms\":" << msg.duration_ms;
  }
  if (msg.bytes_sent > 0) {
    oss << ",\"bytes_sent\":" << msg.bytes_sent;
  }
  if (msg.bytes_received > 0) {
    oss << ",\"bytes_received\":" << msg.bytes_received;
  }

  // Key-value pairs
  if (!msg.key_values.empty()) {
    oss << ",\"metadata\":{";
    bool first = true;
    for (const auto& kv : msg.key_values) {
      const auto& key = kv.first;
      const auto& value = kv.second;
      if (!first)
        oss << ",";
      oss << "\"" << escapeJson(key) << "\":\"" << escapeJson(value) << "\"";
      first = false;
    }
    oss << "}";
  }

  oss << "}";

  return oss.str();
}

std::string JsonFormatter::escapeJson(const std::string& str) const {
  std::ostringstream oss;

  for (char c : str) {
    switch (c) {
      case '"':
        oss << "\\\"";
        break;
      case '\\':
        oss << "\\\\";
        break;
      case '\b':
        oss << "\\b";
        break;
      case '\f':
        oss << "\\f";
        break;
      case '\n':
        oss << "\\n";
        break;
      case '\r':
        oss << "\\r";
        break;
      case '\t':
        oss << "\\t";
        break;
      default:
        if (c >= 0x20 && c <= 0x7E) {
          oss << c;
        } else {
          // Unicode escape
          oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<unsigned>(static_cast<unsigned char>(c));
        }
        break;
    }
  }

  return oss.str();
}

}  // namespace logging
}  // namespace mcp