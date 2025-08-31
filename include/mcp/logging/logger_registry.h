#pragma once

#include <mutex>
#include <regex>
#include <unordered_map>

#include "mcp/logging/bloom_filter.h"
#include "mcp/logging/logger.h"

namespace mcp {
namespace logging {

// Pattern for glob-style log level control
struct LogPattern {
  std::regex pattern;
  LogLevel level;

  LogPattern(const std::string& glob, LogLevel lvl)
      : pattern(globToRegex(glob)), level(lvl) {}

 private:
  static std::string globToRegex(const std::string& glob) {
    std::string regex;
    for (char c : glob) {
      switch (c) {
        case '*':
          regex += ".*";
          break;
        case '?':
          regex += ".";
          break;
        case '.':
          regex += "\\.";
          break;
        case '/':
          regex += "/";
          break;
        default:
          regex += c;
          break;
      }
    }
    return regex;
  }
};

class LoggerRegistry {
 public:
  // Singleton instance with zero-configuration defaults
  static LoggerRegistry& instance();

  // Register component logger
  void registerComponentLogger(Component component,
                               const std::string& name,
                               std::shared_ptr<Logger> logger);

  // Get or create logger with hierarchy support
  std::shared_ptr<Logger> getOrCreateLogger(const std::string& name);

  // Get default logger (zero-configuration)
  std::shared_ptr<Logger> getDefaultLogger();

  // Set global log level
  void setGlobalLevel(LogLevel level);

  // Component-level control
  void setComponentLevel(Component component, LogLevel level);

  void setComponentLevel(const std::string& path, LogLevel level);

  // Pattern-based log level control
  void setPattern(const std::string& pattern, LogLevel level);

  // Bloom filter for quick log filtering
  bool shouldLog(const std::string& logger_name, LogLevel level);

  // Get effective level for a logger based on patterns
  LogLevel getEffectiveLevel(const std::string& name);

  // Set bloom filter configuration
  void setBloomFilter(bool enabled, size_t size = 4096, size_t num_hashes = 3);

  // Get all registered loggers (for testing/debugging)
  std::vector<std::string> getLoggerNames() const;

  // Component path helper (public for ComponentLogger)
  static std::string getComponentPath(Component comp, const std::string& name);

 private:
  LoggerRegistry();

  void initializeDefaults();

  bool checkActualLevel(const std::string& logger_name, LogLevel level) const;

  std::shared_ptr<LogSink> getDefaultSink();

  LogLevel getEffectiveLevelLocked(
      const std::string& name);  // Assumes mutex already locked

  // Hierarchical logger map
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<Logger>> loggers_;

  // Component-specific loggers
  std::unordered_map<Component,
                     std::unordered_map<std::string, std::shared_ptr<Logger>>>
      component_loggers_;
  std::unordered_map<Component, LogLevel> component_levels_;

  // Thread-local logger cache for dispatcher (would be implemented per-thread)
  // static thread_local std::unordered_map<std::string,
  // std::shared_ptr<Logger>> tls_cache_;

  // Bloom filter for performance
  mutable BloomFilter<std::string> bloom_filter_;

  // Fine-grain log patterns (glob support)
  std::vector<LogPattern> patterns_;

  LogLevel global_level_{LogLevel::Info};
  std::shared_ptr<Logger> default_logger_;
  std::shared_ptr<LogSink> default_sink_;
};

// Component logger with automatic hierarchy
class ComponentLogger {
 public:
  ComponentLogger(Component component, const std::string& name)
      : component_(component), name_(name) {
    logger_ = LoggerRegistry::instance().getOrCreateLogger(
        getComponentPath(component, name));
  }

  // Component-aware logging
  template <typename... Args>
  void log(LogLevel level, const char* fmt, Args&&... args) {
    if (logger_->shouldLog(level)) {
      logger_->logWithComponent(level, component_, fmt,
                                std::forward<Args>(args)...);
    }
  }

  // Set component-specific level
  void setLevel(LogLevel level) {
    logger_->setLevel(level);
    LoggerRegistry::instance().setComponentLevel(
        getComponentPath(component_, name_), level);
  }

 private:
  static std::string getComponentPath(Component comp, const std::string& name);

  Component component_;
  std::string name_;
  std::shared_ptr<Logger> logger_;
};

}  // namespace logging
}  // namespace mcp