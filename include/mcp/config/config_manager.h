/**
 * @file config_manager.h
 * @brief Central configuration manager for thread-safe configuration access
 *
 * Provides singleton access to configuration with atomic updates, multiple
 * sources, change notifications, and versioning support.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>  // C++14: std::shared_timed_mutex
#include <vector>


#include "mcp/config/types.h"
#include "mcp/json/json_bridge.h"
#include "mcp/config/validation_policy.h"

namespace mcp {
namespace config {

// Forward declarations
class ConfigSource;
class ConfigurationManager;

/**
 * @brief Configuration snapshot with version information
 */
struct ConfigSnapshot {
  std::shared_ptr<BootstrapConfig> bootstrap;
  std::shared_ptr<ServerConfig> server;
  std::string version_id;
  std::chrono::system_clock::time_point timestamp;

  ConfigSnapshot() : timestamp(std::chrono::system_clock::now()) {}
};

/**
 * @brief Configuration change event
 */
struct ConfigChangeEvent {
  enum Type { INITIAL_LOAD, RELOAD, SOURCE_UPDATE, ROLLBACK };

  Type type;
  std::string previous_version;
  std::string new_version;
  std::chrono::system_clock::time_point timestamp;
  std::vector<std::string> changed_fields;
};

/**
 * @brief Configuration change listener
 */
using ConfigChangeListener = std::function<void(const ConfigChangeEvent&)>;

/**
 * @brief Abstract interface for configuration sources
 */
class ConfigSource {
 public:
  enum Priority {
    DEFAULT = 0,        // Built-in defaults
    FILE = 100,         // Configuration files
    ENVIRONMENT = 200,  // Environment variables
    OVERRIDE = 300,     // Runtime overrides
    CUSTOM = 500        // Custom priority
  };

  virtual ~ConfigSource() = default;

  /**
   * @brief Get the source name for logging
   */
  virtual std::string getName() const = 0;

  /**
   * @brief Get the source priority (higher = takes precedence)
   */
  virtual int getPriority() const = 0;

  /**
   * @brief Check if the source has configuration available
   */
  virtual bool hasConfiguration() const = 0;

  /**
   * @brief Load configuration from this source
   * @return JSON configuration or empty if not available
   */
  virtual mcp::json::JsonValue loadConfiguration() = 0;

  /**
   * @brief Check if the source configuration has changed
   */
  virtual bool hasChanged() const { return false; }

  /**
   * @brief Get last modification time
   */
  virtual std::chrono::system_clock::time_point getLastModified() const {
    return std::chrono::system_clock::now();
  }
};

// FileConfigSource is implemented in file_config_source.cc
// Factory function to create FileConfigSource
std::shared_ptr<ConfigSource> createFileConfigSource(
    const std::string& name,
    int priority = ConfigSource::Priority::FILE,
    const std::string& config_path = "");

/**
 * @brief JSON object configuration source
 */
class JsonConfigSource : public ConfigSource {
 public:
  JsonConfigSource(const std::string& name,
                   const mcp::json::JsonValue& config,
                   Priority priority = Priority::OVERRIDE);

  std::string getName() const override { return name_; }
  int getPriority() const override { return priority_; }
  bool hasConfiguration() const override { return !config_.isNull(); }
  mcp::json::JsonValue loadConfiguration() override { return config_; }

  void updateConfiguration(const mcp::json::JsonValue& config);

 private:
  std::string name_;
  mcp::json::JsonValue config_;
  int priority_;
};

/**
 * @brief Environment variable configuration source
 */
class EnvironmentConfigSource : public ConfigSource {
 public:
  explicit EnvironmentConfigSource(const std::string& prefix = "MCP_");

  std::string getName() const override { return "environment"; }
  int getPriority() const override { return Priority::ENVIRONMENT; }
  bool hasConfiguration() const override;
  mcp::json::JsonValue loadConfiguration() override;

 private:
  std::string prefix_;
  // Keep implementation internal to .cc; avoid exposing nlohmann::json here
  mcp::json::JsonValue parseEnvironmentVariables();
};

/**
 * @brief Central configuration manager (singleton)
 */
class ConfigurationManager {
 public:
  /**
   * @brief Get the singleton instance
   */
  static ConfigurationManager& getInstance();

  // Prevent copying
  ConfigurationManager(const ConfigurationManager&) = delete;
  ConfigurationManager& operator=(const ConfigurationManager&) = delete;

  /**
   * @brief Initialize the configuration manager
   * @param sources Configuration sources in priority order
   * @param policy Validation policy for unknown fields
   * @return true if initialization successful
   */
  bool initialize(
      const std::vector<std::shared_ptr<ConfigSource>>& sources = {},
      UnknownFieldPolicy policy = UnknownFieldPolicy::WARN);

  /**
   * @brief Check if the manager is initialized
   */
  bool isInitialized() const;

  /**
   * @brief Load configuration from all sources
   * @throws std::runtime_error on load failure
   */
  void loadConfiguration();

  /**
   * @brief Reload configuration from all sources
   * @return true if configuration changed
   */
  bool reload();

  /**
   * @brief Get current bootstrap configuration
   * @return Thread-safe shared pointer to bootstrap config
   */
  std::shared_ptr<const BootstrapConfig> getBootstrapConfig() const;

  /**
   * @brief Get current server configuration
   * @return Thread-safe shared pointer to server config
   */
  std::shared_ptr<const ServerConfig> getServerConfig() const;

  /**
   * @brief Get complete configuration snapshot
   */
  ConfigSnapshot getSnapshot() const;

  /**
   * @brief Add a configuration source
   */
  void addSource(std::shared_ptr<ConfigSource> source);

  /**
   * @brief Remove a configuration source by name
   */
  bool removeSource(const std::string& name);

  /**
   * @brief Register a change listener
   * @return Listener ID for later removal
   */
  size_t addChangeListener(ConfigChangeListener listener);

  /**
   * @brief Remove a change listener
   */
  bool removeChangeListener(size_t listener_id);

  /**
   * @brief Get current configuration version
   */
  std::string getCurrentVersion() const;

  /**
   * @brief Get configuration history
   */
  std::vector<std::string> getVersionHistory() const;

  /**
   * @brief Rollback to a previous configuration version
   * @param version_id Version to rollback to
   * @return true if rollback successful
   */
  bool rollback(const std::string& version_id);

  /**
   * @brief Get the number of stored versions
   */
  size_t getVersionCount() const;

  /**
   * @brief Set maximum number of versions to keep
   */
  void setMaxVersionHistory(size_t max_versions);

  /**
   * @brief Clear all configuration and reset
   */
  void reset();

  /**
   * @brief Get validation context for diagnostics
   */
  const ValidationContext& getValidationContext() const {
    return validation_context_;
  }

 public:
  ~ConfigurationManager();

 private:
  ConfigurationManager();

  /**
   * @brief Merge configurations from multiple sources
   */
  // Return merged configuration as JsonValue to avoid exposing nlohmann types
  mcp::json::JsonValue mergeConfigurations();

  /**
   * @brief Parse and validate configuration
   */
  // Accept JsonValue and convert internally where needed
  ConfigSnapshot parseConfiguration(const mcp::json::JsonValue& config);

  /**
   * @brief Generate version ID
   */
  std::string generateVersionId();

  /**
   * @brief Notify listeners of configuration change
   */
  void notifyListeners(const ConfigChangeEvent& event);

  /**
   * @brief Store configuration version in history
   */
  void storeVersion(const ConfigSnapshot& snapshot);

  /**
   * @brief Trim version history to max size
   */
  void trimVersionHistory();

 private:
  // Thread-safe singleton implementation
  static std::once_flag init_flag_;
  static std::unique_ptr<ConfigurationManager> instance_;

  // Configuration state
  mutable std::shared_timed_mutex config_mutex_;
  std::atomic<bool> initialized_{false};
  ConfigSnapshot current_config_;

  // Configuration sources
  std::mutex sources_mutex_;
  std::vector<std::shared_ptr<ConfigSource>> sources_;

  // Version history
  std::deque<ConfigSnapshot> version_history_;
  size_t max_version_history_ = 10;

  // Change listeners
  std::mutex listeners_mutex_;
  std::map<size_t, ConfigChangeListener> listeners_;
  std::atomic<size_t> next_listener_id_{1};

  // Validation
  ValidationContext validation_context_;

  // Statistics
  std::atomic<size_t> reload_count_{0};
  std::chrono::system_clock::time_point last_reload_;
};

/**
 * @brief RAII helper for configuration updates
 */
class ConfigUpdateGuard {
 public:
  explicit ConfigUpdateGuard(ConfigurationManager& manager);
  ~ConfigUpdateGuard();

  void commit();
  void rollback();

 private:
  ConfigurationManager& manager_;
  std::string saved_version_;
  bool committed_ = false;
};

/**
 * @brief Convenience function to get global config manager
 */
inline ConfigurationManager& getConfigManager() {
  return ConfigurationManager::getInstance();
}

/**
 * @brief Convenience function to get bootstrap config
 */
inline std::shared_ptr<const BootstrapConfig> getBootstrapConfig() {
  return ConfigurationManager::getInstance().getBootstrapConfig();
}

/**
 * @brief Convenience function to get server config
 */
inline std::shared_ptr<const ServerConfig> getServerConfig() {
  return ConfigurationManager::getInstance().getServerConfig();
}

}  // namespace config
}  // namespace mcp
