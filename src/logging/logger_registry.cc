#include "mcp/logging/logger_registry.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>

#include "mcp/logging/log_macros.h"  // GOPHER_LOG_FLOW_COMPONENT

namespace mcp {
namespace logging {

namespace {
// Resolve the initial global log level. Honors the GOPHER_LOG_LEVEL environment
// variable (e.g. GOPHER_LOG_LEVEL=debug) so verbose output can be switched on
// without code changes; defaults to Info when unset or empty. This affects
// every component; use GOPHER_MCP_LOG_FLOW for the MCP request/response flow
// only.
LogLevel resolveInitialLevel() {
  const char* env = std::getenv("GOPHER_LOG_LEVEL");
  if (env != nullptr && env[0] != '\0') {
    return stringToLogLevel(env);
  }
  return LogLevel::Info;
}

// Whether the MCP protocol-flow logs (logger component "mcp.flow": initialize,
// tools/list, tools/call, resources, prompts, HTTP request/response) should be
// switched on independently of the global level. Driven by GOPHER_MCP_LOG_FLOW;
// accepts boolean-ish truthy values (1/true/on/yes/enabled). When off, the flow
// logs simply follow the global level.
bool resolveFlowEnabled() {
  const char* env = std::getenv("GOPHER_MCP_LOG_FLOW");
  if (env == nullptr || env[0] == '\0') {
    return false;
  }
  std::string v(env);
  std::transform(v.begin(), v.end(), v.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return v == "1" || v == "true" || v == "on" || v == "yes" || v == "enabled" ||
         v == "debug";
}
}  // namespace

LoggerRegistry& LoggerRegistry::instance() {
  static LoggerRegistry instance;
  return instance;
}

LoggerRegistry::LoggerRegistry() : global_level_(resolveInitialLevel()) {
  initializeDefaults();

  // If the MCP flow switch is on, raise just the "mcp.flow" component to Debug
  // (leaving the global level untouched) and pre-create its logger so the bloom
  // filter routes to the component level instead of the global fallback. The
  // pattern also shields it from later setGlobalLevel() calls.
  if (resolveFlowEnabled()) {
    setPattern(GOPHER_LOG_FLOW_COMPONENT, LogLevel::Debug);
    getOrCreateLogger(GOPHER_LOG_FLOW_COMPONENT);
  }
}

void LoggerRegistry::initializeDefaults() {
  // Create default logger with stderr sink
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

std::shared_ptr<Logger> LoggerRegistry::getOrCreateLogger(
    const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = loggers_.find(name);
  if (it != loggers_.end()) {
    return it->second;
  }

  // Create new logger
  auto logger = std::make_shared<Logger>(name, LogMode::Sync);

  // Set effective level based on patterns and component
  LogLevel effective_level = getEffectiveLevelLocked(name);
  logger->setLevel(effective_level);

  // Use default sink for all loggers
  if (default_sink_) {
    logger->setSink(default_sink_);
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
  for (auto& logger_pair : loggers_) {
    auto& name = logger_pair.first;
    auto& logger = logger_pair.second;
    // Check if logger has specific pattern
    bool has_pattern = false;
    for (const auto& pattern_pair : patterns_) {
      const auto& pattern = pattern_pair.pattern;
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
  for (auto& logger_pair : loggers_) {
    auto& name = logger_pair.first;
    auto& logger = logger_pair.second;
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
  for (auto& logger_pair : loggers_) {
    auto& name = logger_pair.first;
    auto& logger = logger_pair.second;
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
    // Logger doesn't exist yet, fall back to global level check
    // This allows new component loggers to work without pre-registration
    return level >= global_level_;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  return checkActualLevel(name, level);
}

LogLevel LoggerRegistry::getEffectiveLevelLocked(const std::string& name) {
  // NOTE: Assumes mutex_ is already locked by caller

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

LogLevel LoggerRegistry::getEffectiveLevel(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  return getEffectiveLevelLocked(name);
}

std::vector<std::string> LoggerRegistry::getLoggerNames() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<std::string> names;
  names.reserve(loggers_.size());

  for (const auto& logger_pair : loggers_) {
    const auto& name = logger_pair.first;
    const auto& logger = logger_pair.second;
    names.push_back(name);
  }

  return names;
}

void LoggerRegistry::setBloomFilter(bool enabled,
                                    size_t size,
                                    size_t num_hashes) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (enabled) {
    bloom_filter_ = BloomFilter<std::string>(size, num_hashes);

    // Add all existing logger names
    for (const auto& logger_pair : loggers_) {
      const auto& name = logger_pair.first;
      const auto& logger = logger_pair.second;
      bloom_filter_.add(name);
      logger->setBloomFilterHint(&bloom_filter_);
    }
  } else {
    bloom_filter_.clear();

    // Clear bloom filter hints
    for (auto& logger_pair : loggers_) {
      auto& name = logger_pair.first;
      auto& logger = logger_pair.second;
      logger->setBloomFilterHint(nullptr);
    }
  }
}

bool LoggerRegistry::checkActualLevel(const std::string& logger_name,
                                      LogLevel level) const {
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

std::string LoggerRegistry::getComponentPath(Component comp,
                                             const std::string& name) {
  return std::string(componentToString(comp)) + "." + name;
}

void LoggerRegistry::setComponentLevel(const std::string& path,
                                       LogLevel level) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = loggers_.find(path);
  if (it != loggers_.end()) {
    it->second->setLevel(level);
  }
}

// ComponentLogger implementation
std::string ComponentLogger::getComponentPath(Component comp,
                                              const std::string& name) {
  return LoggerRegistry::getComponentPath(comp, name);
}

}  // namespace logging
}  // namespace mcp