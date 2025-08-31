#include "mcp/logging/logger_registry.h"
#include <regex>
#include <algorithm>

namespace mcp {
namespace logging {

LoggerRegistry& LoggerRegistry::instance() {
  static LoggerRegistry instance;
  return instance;
}

LoggerRegistry::LoggerRegistry()
  : global_level_(LogLevel::Info) {
  initializeDefaults();
}

void LoggerRegistry::initializeDefaults() {
  // Create default logger with console output
  default_logger_ = std::make_shared<Logger>("default", LogMode::Sync);
  default_sink_ = std::make_shared<StdioSink>(StdioSink::Stderr);
  default_logger_->setSink(default_sink_);
  default_logger_->setLevel(global_level_);
  
  // Initialize bloom filter
  bloom_filter_ = BloomFilter<std::string>(4096, 3);
  bloom_filter_.add("default");
  
  // Register default logger
  loggers_["default"] = default_logger_;
}

std::shared_ptr<Logger> LoggerRegistry::getDefaultLogger() {
  std::lock_guard<std::mutex> lock(mutex_);
  return default_logger_;
}

std::shared_ptr<Logger> LoggerRegistry::getOrCreateLogger(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto it = loggers_.find(name);
  if (it != loggers_.end()) {
    return it->second;
  }
  
  // Create new logger
  auto logger = std::make_shared<Logger>(name, LogMode::Sync);
  
  // Set effective level based on patterns and component
  LogLevel effective_level = getEffectiveLevel(name);
  logger->setLevel(effective_level);
  
  // Use default sink if available
  if (default_logger_) {
    // Share the same sink as default logger
    logger->setSink(default_logger_->getSink());
  }
  
  // Set bloom filter hint
  bloom_filter_.add(name);
  logger->setBloomFilterHint(&bloom_filter_);
  
  loggers_[name] = logger;
  return logger;
}

void LoggerRegistry::setGlobalLevel(LogLevel level) {
  std::lock_guard<std::mutex> lock(mutex_);
  global_level_ = level;
  
  // Update all existing loggers
  for (auto& [name, logger] : loggers_) {
    // Check if logger has specific pattern
    bool has_pattern = false;
    for (const auto& [pattern, _] : patterns_) {
      std::regex re(pattern);
      if (std::regex_match(name, re)) {
        has_pattern = true;
        break;
      }
    }
    
    if (!has_pattern) {
      logger->setLevel(level);
    }
  }
}

void LoggerRegistry::setComponentLevel(Component component, LogLevel level) {
  std::lock_guard<std::mutex> lock(mutex_);
  component_levels_[component] = level;
  
  // Update existing component loggers
  std::string comp_prefix = componentToString(component);
  for (auto& [name, logger] : loggers_) {
    if (name.find(comp_prefix + ".") == 0) {
      logger->setLevel(level);
    }
  }
}

void LoggerRegistry::setPattern(const std::string& pattern, LogLevel level) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  // Store the pattern (LogPattern constructor will convert glob to regex)
  patterns_.emplace_back(pattern, level);
  
  // Update existing loggers that match the pattern
  for (auto& [name, logger] : loggers_) {
    if (std::regex_match(name, patterns_.back().pattern)) {
      logger->setLevel(level);
    }
  }
}

void LoggerRegistry::registerComponentLogger(Component component, 
                                            const std::string& name,
                                            std::shared_ptr<Logger> logger) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  std::string full_name = getComponentPath(component, name);
  
  // Store in component map
  component_loggers_[component][name] = logger;
  
  // Apply component level if set
  auto comp_it = component_levels_.find(component);
  if (comp_it != component_levels_.end()) {
    logger->setLevel(comp_it->second);
  }
  
  loggers_[full_name] = logger;
  bloom_filter_.add(full_name);
  logger->setBloomFilterHint(&bloom_filter_);
}

bool LoggerRegistry::shouldLog(const std::string& name, LogLevel level) {
  // Fast path with bloom filter
  if (!bloom_filter_.mayContain(name)) {
    return false;
  }
  
  std::lock_guard<std::mutex> lock(mutex_);
  return checkActualLevel(name, level);
}

LogLevel LoggerRegistry::getEffectiveLevel(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  // Check for specific patterns (most specific first)
  for (const auto& pattern : patterns_) {
    if (std::regex_match(name, pattern.pattern)) {
      return pattern.level;
    }
  }
  
  // Check component level
  size_t dot_pos = name.find('.');
  if (dot_pos != std::string::npos) {
    std::string comp_str = name.substr(0, dot_pos);
    
    // Convert string to component
    for (int i = 0; i < static_cast<int>(Component::Count); ++i) {
      Component comp = static_cast<Component>(i);
      if (componentToString(comp) == comp_str) {
        auto it = component_levels_.find(comp);
        if (it != component_levels_.end()) {
          return it->second;
        }
        break;
      }
    }
  }
  
  // Use global level
  return global_level_;
}

std::vector<std::string> LoggerRegistry::getLoggerNames() const {
  std::lock_guard<std::mutex> lock(mutex_);
  
  std::vector<std::string> names;
  names.reserve(loggers_.size());
  
  for (const auto& [name, logger] : loggers_) {
    names.push_back(name);
  }
  
  return names;
}

void LoggerRegistry::setBloomFilter(bool enabled, size_t size, size_t num_hashes) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  if (enabled) {
    bloom_filter_ = BloomFilter<std::string>(size, num_hashes);
    
    // Add all existing logger names
    for (const auto& [name, logger] : loggers_) {
      bloom_filter_.add(name);
      logger->setBloomFilterHint(&bloom_filter_);
    }
  } else {
    bloom_filter_.clear();
    
    // Clear bloom filter hints
    for (auto& [name, logger] : loggers_) {
      logger->setBloomFilterHint(nullptr);
    }
  }
}

bool LoggerRegistry::checkActualLevel(const std::string& logger_name, LogLevel level) const {
  auto it = loggers_.find(logger_name);
  if (it != loggers_.end()) {
    return it->second->shouldLog(level);
  }
  return level >= global_level_;
}

std::shared_ptr<LogSink> LoggerRegistry::getDefaultSink() {
  if (!default_sink_) {
    default_sink_ = std::make_shared<StdioSink>(StdioSink::Stderr);
  }
  return default_sink_;
}

std::string LoggerRegistry::getComponentPath(Component comp, const std::string& name) {
  return std::string(componentToString(comp)) + "." + name;
}

void LoggerRegistry::setComponentLevel(const std::string& path, LogLevel level) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = loggers_.find(path);
  if (it != loggers_.end()) {
    it->second->setLevel(level);
  }
}

// ComponentLogger implementation
std::string ComponentLogger::getComponentPath(Component comp, const std::string& name) {
  return LoggerRegistry::getComponentPath(comp, name);
}

} // namespace logging
} // namespace mcp