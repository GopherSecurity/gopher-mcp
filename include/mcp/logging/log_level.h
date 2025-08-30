#pragma once

#include <cstdint>
#include <string>

namespace mcp {
namespace logging {

// Log levels matching MCP specification (RFC-5424)
enum class LogLevel : uint8_t {
  Debug = 0,
  Info = 1,
  Notice = 2,
  Warning = 3,
  Error = 4,
  Critical = 5,
  Alert = 6,
  Emergency = 7,
  Off = 8
};

// Logging mode selection
enum class LogMode {
  Sync,    // Direct logging (for debugging)
  Async,   // Buffered async logging (for production)
  NoOp     // No operation (for critical paths)
};

// Component identifiers for hierarchical logging
enum class Component {
  Root,
  Server,
  Client,
  Network,
  Filter,
  Transport,
  Protocol,
  Event,
  Http,
  Json,
  // Add more components as needed
};

// Sink types
enum class SinkType {
  File,
  Stdio,
  Null,
  External,
  MCP
};

// Helper functions
inline const char* logLevelToString(LogLevel level) {
  switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Notice: return "NOTICE";
    case LogLevel::Warning: return "WARNING";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Critical: return "CRITICAL";
    case LogLevel::Alert: return "ALERT";
    case LogLevel::Emergency: return "EMERGENCY";
    case LogLevel::Off: return "OFF";
    default: return "UNKNOWN";
  }
}

inline LogLevel stringToLogLevel(const std::string& str) {
  if (str == "DEBUG" || str == "debug") return LogLevel::Debug;
  if (str == "INFO" || str == "info") return LogLevel::Info;
  if (str == "NOTICE" || str == "notice") return LogLevel::Notice;
  if (str == "WARNING" || str == "warning") return LogLevel::Warning;
  if (str == "ERROR" || str == "error") return LogLevel::Error;
  if (str == "CRITICAL" || str == "critical") return LogLevel::Critical;
  if (str == "ALERT" || str == "alert") return LogLevel::Alert;
  if (str == "EMERGENCY" || str == "emergency") return LogLevel::Emergency;
  if (str == "OFF" || str == "off") return LogLevel::Off;
  return LogLevel::Info; // Default
}

inline const char* componentToString(Component component) {
  switch (component) {
    case Component::Root: return "Root";
    case Component::Server: return "Server";
    case Component::Client: return "Client";
    case Component::Network: return "Network";
    case Component::Filter: return "Filter";
    case Component::Transport: return "Transport";
    case Component::Protocol: return "Protocol";
    case Component::Event: return "Event";
    case Component::Http: return "Http";
    case Component::Json: return "Json";
    default: return "Unknown";
  }
}

} // namespace logging
} // namespace mcp