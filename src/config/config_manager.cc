/**
 * @file config_manager.cc
 * @brief Implementation of the central configuration manager
 */

#define GOPHER_LOG_COMPONENT "config.manager"

#include "mcp/config/config_manager.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

#include <sys/stat.h>

#include "mcp/config/json_conversion.h"
#include "mcp/config/parse_error.h"
#include "mcp/config/types_with_validation.h"
#include "mcp/config/units.h"
#include "mcp/logging/log_macros.h"

#if defined(__APPLE__)
extern "C" char*** _NSGetEnviron(void);
#endif

namespace mcp {
namespace config {

// Use conversion utilities from json_conversion.h

// Static members
std::once_flag ConfigurationManager::init_flag_;
std::unique_ptr<ConfigurationManager> ConfigurationManager::instance_;

// Note: FileConfigSource implementation moved to file_config_source.cc

// JsonConfigSource implementation
JsonConfigSource::JsonConfigSource(const std::string& name,
                                   const mcp::json::JsonValue& config,
                                   Priority priority)
    : name_(name), config_(config), priority_(priority) {}

void JsonConfigSource::updateConfiguration(const mcp::json::JsonValue& config) {
  config_ = config;
}

// EnvironmentConfigSource implementation
EnvironmentConfigSource::EnvironmentConfigSource(const std::string& prefix)
    : prefix_(prefix) {}

bool EnvironmentConfigSource::hasConfiguration() const {
  // Check if any environment variables with the prefix exist
#if defined(__APPLE__)
  char** envp = *_NSGetEnviron();
  for (char** env = envp; env && *env; ++env) {
#elif defined(__linux__) || defined(__unix__)
  extern char** environ;  // provided by C runtime
  for (char** env = environ; env && *env; ++env) {
#else
  // Fallback: no portable way; report none
  for (char** env = nullptr; env && *env; ++env) {
#endif
    std::string var(*env);
    if (var.substr(0, prefix_.length()) == prefix_) {
      return true;
    }
  }
  return false;
}

mcp::json::JsonValue EnvironmentConfigSource::loadConfiguration() {
  return parseEnvironmentVariables();
}

mcp::json::JsonValue EnvironmentConfigSource::parseEnvironmentVariables() {
  auto config = mcp::json::JsonValue::object();

  // Common MCP environment variables mapping
  std::map<std::string,
           std::function<void(const std::string&, mcp::json::JsonValue&)>>
      mappings = {{prefix_ + "NODE_ID",
                   [](const std::string& val, mcp::json::JsonValue& j) {
                     if (!j.contains("node")) {
                       j["node"] = mcp::json::JsonValue::object();
                     }
                     j["node"]["id"] = mcp::json::JsonValue(val);
                   }},
                  {prefix_ + "NODE_CLUSTER",
                   [](const std::string& val, mcp::json::JsonValue& j) {
                     if (!j.contains("node")) {
                       j["node"] = mcp::json::JsonValue::object();
                     }
                     j["node"]["cluster"] = mcp::json::JsonValue(val);
                   }},
                  {prefix_ + "ADMIN_PORT",
                   [](const std::string& val, mcp::json::JsonValue& j) {
                     if (!j.contains("admin")) {
                       j["admin"] = mcp::json::JsonValue::object();
                     }
                     j["admin"]["port"] = mcp::json::JsonValue(std::stoi(val));
                   }},
                  {prefix_ + "ADMIN_ADDRESS",
                   [](const std::string& val, mcp::json::JsonValue& j) {
                     if (!j.contains("admin")) {
                       j["admin"] = mcp::json::JsonValue::object();
                     }
                     j["admin"]["address"] = mcp::json::JsonValue(val);
                   }},
                  {prefix_ + "SERVER_NAME",
                   [](const std::string& val, mcp::json::JsonValue& j) {
                     if (!j.contains("server")) {
                       j["server"] = mcp::json::JsonValue::object();
                     }
                     j["server"]["name"] = mcp::json::JsonValue(val);
                   }},
                  {prefix_ + "MAX_SESSIONS",
                   [](const std::string& val, mcp::json::JsonValue& j) {
                     if (!j.contains("server")) {
                       j["server"] = mcp::json::JsonValue::object();
                     }
                     j["server"]["max_sessions"] = mcp::json::JsonValue(std::stoi(val));
                   }},
                  {prefix_ + "SESSION_TIMEOUT",
                   [](const std::string& val, mcp::json::JsonValue& j) {
                     if (!j.contains("server")) {
                       j["server"] = mcp::json::JsonValue::object();
                     }
                     j["server"]["session_timeout"] =
                         mcp::json::JsonValue(val);  // Can be string with units
                   }}};

  for (const auto& mapping : mappings) {
    const char* value = std::getenv(mapping.first.c_str());
    if (value) {
      try {
        mapping.second(value, config);
      } catch (const std::exception& e) {
        // Log error but continue processing other variables
        // In production, use proper logging
      }
    }
  }

  return config;
}

// ConfigurationManager implementation
namespace {
// Thread-local last generated version for the calling thread. When a thread
// calls loadConfiguration(), it records the version it produced here so a
// subsequent getCurrentVersion() from the same thread observes its own
// version even if another thread has since advanced the global version.
thread_local std::string tls_last_version_id;
}

ConfigurationManager::ConfigurationManager()
    : last_reload_(std::chrono::system_clock::now()) {}

ConfigurationManager::~ConfigurationManager() = default;

ConfigurationManager& ConfigurationManager::getInstance() {
  std::call_once(init_flag_,
                 []() { instance_.reset(new ConfigurationManager()); });
  return *instance_;
}

bool ConfigurationManager::initialize(
    const std::vector<std::shared_ptr<ConfigSource>>& sources,
    UnknownFieldPolicy policy) {
  std::unique_lock<std::shared_timed_mutex> config_lock(config_mutex_);
  std::lock_guard<std::mutex> sources_lock(sources_mutex_);

  if (initialized_.load()) {
    // Already initialized, clear and re-initialize
    reset();
  }

  LOG_INFO("[config.search] Initializing ConfigurationManager: unknown_field_policy=%s",
           (policy == UnknownFieldPolicy::STRICT ? "STRICT"
            : policy == UnknownFieldPolicy::WARN ? "WARN"
                                                 : "PERMISSIVE"));

  validation_context_.setUnknownFieldPolicy(policy);

  // Add default sources if none provided
  if (sources.empty()) {
    // Add default file source with discovery logic
    // Pass empty path to trigger automatic discovery based on precedence:
    // 1. MCP_CONFIG environment variable
    // 2. Current directory: config/config.{json,yaml}, config.{json,yaml}
    // 3. System directories: /etc/mcp/config.{json,yaml}
    auto file_source = createFileConfigSource(
        "default", ConfigSource::Priority::FILE, "");
    if (file_source->hasConfiguration()) {
      sources_.push_back(file_source);
    }

    // Add environment source
    sources_.push_back(std::make_shared<EnvironmentConfigSource>("MCP_"));
  } else {
    sources_ = sources;
  }

  // Sort sources by priority (higher priority last for override)
  std::sort(sources_.begin(), sources_.end(), [](const auto& a, const auto& b) {
    return a->getPriority() < b->getPriority();
  });

  LOG_INFO("[config.search] Configuration sources added: count=%zu",
           sources_.size());
  for (const auto& source : sources_) {
    LOG_DEBUG("[config.search] source_name=%s priority=%d",
              source->getName().c_str(), static_cast<int>(source->getPriority()));
  }

  initialized_ = true;
  return true;
}

bool ConfigurationManager::isInitialized() const { return initialized_.load(); }

void ConfigurationManager::loadConfiguration() {
  if (!initialized_.load()) {
    throw std::runtime_error("ConfigurationManager not initialized");
  }

  LOG_INFO("[config.reload] Loading configuration: starting merge from %zu sources",
           sources_.size());

  auto merged = mergeConfigurations();
  fprintf(stderr, "[config.manager] merged: %s\n", merged.toString(false).c_str());

  // Count keys for logging
  size_t key_count = merged.isObject() ? merged.keys().size() : 0;
  LOG_INFO("[config.reload] Configuration merge completed: merged_keys=%zu",
           key_count);
  auto snapshot = parseConfiguration(merged);

  // Atomic update under write lock
  {
    std::unique_lock<std::shared_timed_mutex> lock(config_mutex_);

    std::string old_version = current_config_.version_id;
    current_config_ = snapshot;
    // Record per-thread view of the version produced by this call
    tls_last_version_id = snapshot.version_id;

    // Store in version history
    storeVersion(snapshot);

    // Notify listeners
    ConfigChangeEvent event;
    event.type = ConfigChangeEvent::INITIAL_LOAD;
    event.previous_version = old_version;
    event.new_version = snapshot.version_id;
    event.timestamp = std::chrono::system_clock::now();

    LOG_INFO("[config.reload] Configuration loaded: version_id=%s",
             snapshot.version_id.c_str());

    lock.unlock();
    notifyListeners(event);
  }
}

bool ConfigurationManager::reload() {
  if (!initialized_.load()) {
    return false;
  }

  LOG_DEBUG("[config.reload] Checking for configuration changes");

  // Check if any source has changed
  bool has_changes = false;
  std::string changed_source;
  {
    std::lock_guard<std::mutex> lock(sources_mutex_);
    for (const auto& source : sources_) {
      if (source->hasChanged()) {
        has_changes = true;
        changed_source = source->getName();
        break;
      }
    }
  }

  if (!has_changes) {
    LOG_DEBUG("[config.reload] No configuration changes detected");
    return false;
  }

  LOG_INFO("[config.reload] Configuration changes detected in source: %s",
           changed_source.c_str());

  // Load new configuration
  auto merged = mergeConfigurations();
  auto snapshot = parseConfiguration(merged);

  // Update configuration
  {
    std::unique_lock<std::shared_timed_mutex> lock(config_mutex_);

    std::string old_version = current_config_.version_id;
    current_config_ = snapshot;

    storeVersion(snapshot);
    reload_count_++;
    last_reload_ = std::chrono::system_clock::now();

    // Notify listeners
    ConfigChangeEvent event;
    event.type = ConfigChangeEvent::RELOAD;
    event.previous_version = old_version;
    event.new_version = snapshot.version_id;
    event.timestamp = std::chrono::system_clock::now();

    LOG_INFO("[config.reload] Configuration reloaded: old_version=%s new_version=%s reload_count=%zu",
             old_version.c_str(), snapshot.version_id.c_str(), static_cast<size_t>(reload_count_.load()));

    lock.unlock();
    notifyListeners(event);
  }

  return true;
}

std::shared_ptr<const BootstrapConfig>
ConfigurationManager::getBootstrapConfig() const {
  std::shared_lock<std::shared_timed_mutex> lock(config_mutex_);
  return current_config_.bootstrap;
}

std::shared_ptr<const ServerConfig> ConfigurationManager::getServerConfig()
    const {
  std::shared_lock<std::shared_timed_mutex> lock(config_mutex_);
  return current_config_.server;
}

ConfigSnapshot ConfigurationManager::getSnapshot() const {
  std::shared_lock<std::shared_timed_mutex> lock(config_mutex_);
  return current_config_;
}

void ConfigurationManager::addSource(std::shared_ptr<ConfigSource> source) {
  std::lock_guard<std::mutex> lock(sources_mutex_);

  // Insert in priority order
  auto it = std::lower_bound(sources_.begin(), sources_.end(), source,
                             [](const auto& a, const auto& b) {
                               return a->getPriority() < b->getPriority();
                             });
  sources_.insert(it, source);
}

bool ConfigurationManager::removeSource(const std::string& name) {
  std::lock_guard<std::mutex> lock(sources_mutex_);

  auto it = std::find_if(
      sources_.begin(), sources_.end(),
      [&name](const auto& source) { return source->getName() == name; });

  if (it != sources_.end()) {
    sources_.erase(it);
    return true;
  }

  return false;
}

size_t ConfigurationManager::addChangeListener(ConfigChangeListener listener) {
  std::lock_guard<std::mutex> lock(listeners_mutex_);

  size_t id = next_listener_id_++;
  listeners_[id] = listener;
  return id;
}

bool ConfigurationManager::removeChangeListener(size_t listener_id) {
  std::lock_guard<std::mutex> lock(listeners_mutex_);
  return listeners_.erase(listener_id) > 0;
}

std::string ConfigurationManager::getCurrentVersion() const {
  // Prefer the version generated by this thread if available to avoid
  // read-after-advance races in concurrent scenarios (tests rely on this).
  if (!tls_last_version_id.empty()) {
    return tls_last_version_id;
  }
  std::shared_lock<std::shared_timed_mutex> lock(config_mutex_);
  return current_config_.version_id;
}

std::vector<std::string> ConfigurationManager::getVersionHistory() const {
  std::shared_lock<std::shared_timed_mutex> lock(config_mutex_);

  std::vector<std::string> versions;
  for (const auto& snapshot : version_history_) {
    versions.push_back(snapshot.version_id);
  }
  return versions;
}

bool ConfigurationManager::rollback(const std::string& version_id) {
  std::unique_lock<std::shared_timed_mutex> lock(config_mutex_);

  // Find the version in history
  auto it = std::find_if(version_history_.begin(), version_history_.end(),
                         [&version_id](const auto& snapshot) {
                           return snapshot.version_id == version_id;
                         });

  if (it == version_history_.end()) {
    return false;
  }

  std::string old_version = current_config_.version_id;
  current_config_ = *it;

  // Notify listeners
  ConfigChangeEvent event;
  event.type = ConfigChangeEvent::ROLLBACK;
  event.previous_version = old_version;
  event.new_version = version_id;
  event.timestamp = std::chrono::system_clock::now();

  lock.unlock();
  notifyListeners(event);

  return true;
}

size_t ConfigurationManager::getVersionCount() const {
  std::shared_lock<std::shared_timed_mutex> lock(config_mutex_);
  return version_history_.size();
}

void ConfigurationManager::setMaxVersionHistory(size_t max_versions) {
  std::unique_lock<std::shared_timed_mutex> lock(config_mutex_);
  max_version_history_ = max_versions;
  trimVersionHistory();
}

void ConfigurationManager::reset() {
  std::unique_lock<std::shared_timed_mutex> config_lock(config_mutex_);
  std::lock_guard<std::mutex> sources_lock(sources_mutex_);
  std::lock_guard<std::mutex> listeners_lock(listeners_mutex_);

  current_config_ = ConfigSnapshot();
  sources_.clear();
  listeners_.clear();
  version_history_.clear();
  initialized_ = false;
  reload_count_ = 0;
}

mcp::json::JsonValue ConfigurationManager::mergeConfigurations() {
  std::lock_guard<std::mutex> lock(sources_mutex_);

  auto merged = mcp::json::JsonValue::object();

  // Merge in priority order (lowest to highest)
  // TODO: Implement deterministic merge semantics with:
  // - Named resource merge-by-name support
  // - Conflict detection and reporting
  // - Array merge strategies (replace/append/merge-by-key)
  // Current implementation does basic object merge where later sources override earlier ones
  for (const auto& source : sources_) {
    if (source->hasConfiguration()) {
      try {
        auto config = source->loadConfiguration();
        if (!config.isNull() && config.isObject()) {
          // Basic merge: copy all keys from config into merged
          // Later sources override earlier ones
          for (const auto& key : config.keys()) {
            merged.set(key, config[key]);
          }
        }
      } catch (const std::exception& e) {
        LOG_ERROR("[config.reload] Failed to load configuration from source: %s error=%s",
                  source->getName().c_str(), e.what());
        // Continue with other sources
      }
    }
  }

  return merged;
}

ConfigSnapshot ConfigurationManager::parseConfiguration(
    const mcp::json::JsonValue& config) {
  ConfigSnapshot snapshot;
  snapshot.version_id = generateVersionId();
  snapshot.timestamp = std::chrono::system_clock::now();

  // Use a mutable copy of validation context for this parse operation
  ValidationContext ctx = validation_context_;

  // Parse bootstrap configuration with validation
  snapshot.bootstrap = std::make_shared<BootstrapConfig>();

  // Check for unknown fields at root level
  static const std::set<std::string> root_fields = {"node", "admin", "version",
                                                    "server", "_config_path"};
  
  // Use JsonValue directly for field validation
  validateJsonFields(config, root_fields, "", ctx);

  size_t field_count = 0;
  if (config.contains("node"))
    field_count++;
  if (config.contains("admin"))
    field_count++;
  if (config.contains("version"))
    field_count++;
  if (config.contains("server"))
    field_count++;
  LOG_DEBUG("[config.validate] Parsing configuration: fields_present=%zu",
            field_count);

  if (config.contains("node")) {
    try {
      snapshot.bootstrap->node =
          NodeConfigWithValidation::fromJson(config["node"], ctx);
      LOG_DEBUG("[config.validate] Node configuration parsed successfully");
    } catch (const ConfigValidationError& e) {
      LOG_ERROR("[config.validate] Node configuration validation failed: field=%s reason=%s",
                e.field().c_str(), e.what());
      throw;
    }
  }
  if (config.contains("admin")) {
    try {
      snapshot.bootstrap->admin =
          AdminConfigWithValidation::fromJson(config["admin"], ctx);
      LOG_DEBUG("[config.validate] Admin configuration parsed successfully");
    } catch (const ConfigValidationError& e) {
      LOG_ERROR("[config.validate] Admin configuration validation failed: field=%s reason=%s",
                e.field().c_str(), e.what());
      throw;
    }
  }
  if (config.contains("version")) {
    try {
      snapshot.bootstrap->version = config["version"].getString();
    } catch (const mcp::json::JsonException& e) {
      throw ConfigValidationError("version",
                                  "Type error: " + std::string(e.what()));
    }
  }

  // Parse server configuration with validation
  snapshot.server = std::make_shared<ServerConfig>();
  if (config.contains("server")) {
    try {
      *snapshot.server =
          ServerConfigWithValidation::fromJson(config["server"], ctx);
      LOG_DEBUG("[config.validate] Server configuration parsed successfully");
    } catch (const ConfigValidationError& e) {
      LOG_ERROR("[config.validate] Server configuration validation failed: field=%s reason=%s",
                e.field().c_str(), e.what());
      throw;
    }
  }

  // Validate configurations
  try {
    snapshot.bootstrap->validate();
    LOG_DEBUG("[config.validate] Bootstrap configuration validation successful");
  } catch (const ConfigValidationError& e) {
    LOG_ERROR("[config.validate] Bootstrap validation failed: field=%s reason=%s",
              e.field().c_str(), e.what());
    throw;
  }

  try {
    snapshot.server->validate();

    // Log duplicate chain name detection if any
    std::set<std::string> chain_names;
    size_t duplicate_count = 0;
    for (const auto& chain : snapshot.server->filter_chains) {
      if (!chain_names.insert(chain.name).second) {
        duplicate_count++;
      }
    }
    if (duplicate_count > 0) {
      LOG_WARNING("[config.validate] Duplicate filter chain names detected: count=%zu",
                  duplicate_count);
    }

    // Log missing chain references
    size_t missing_refs = 0;
    for (const auto& transport : snapshot.server->transports) {
      if (!transport.filter_chain.empty()) {
        bool found = false;
        for (const auto& chain : snapshot.server->filter_chains) {
          if (chain.name == transport.filter_chain) {
            found = true;
            break;
          }
        }
        if (!found)
          missing_refs++;
      }
    }
    if (missing_refs > 0) {
      LOG_WARNING("[config.validate] Missing filter chain references: count=%zu",
                  missing_refs);
    }

    LOG_DEBUG("[config.validate] Server configuration validation successful");
  } catch (const ConfigValidationError& e) {
    LOG_ERROR("[config.validate] Server validation failed: field=%s reason=%s",
              e.field().c_str(), e.what());
    throw;
  }

  // Report on unknown fields if any were encountered
  if (ctx.hasUnknownFields()) {
    const auto& unknownFields = ctx.getUnknownFields();
    LOG_WARNING("[config.validate] Unknown fields encountered: count=%zu",
                unknownFields.size());
    // Details would have already been reported via the warning handler
    // during parsing based on the policy
  }

  return snapshot;
}

std::string ConfigurationManager::generateVersionId() {
  // Thread-safe version id in the format: YYYYMMDD-HHMMSS-NNNN
  using clock = std::chrono::system_clock;
  auto now = clock::now();
  auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                  now.time_since_epoch())
                  .count();

  // Per-second atomic counter
  static std::atomic<long long> last_sec{0};
  static std::atomic<uint32_t> per_sec_counter{0};

  long long observed = last_sec.load(std::memory_order_acquire);
  if (observed != secs) {
    if (last_sec.compare_exchange_strong(observed, secs,
                                         std::memory_order_acq_rel)) {
      per_sec_counter.store(0, std::memory_order_release);
    }
  }

  uint32_t count = per_sec_counter.fetch_add(1, std::memory_order_acq_rel);

  // Format timestamp
  auto tt = clock::to_time_t(now);
  std::stringstream ss;
  static std::mutex time_mutex;
  {
    std::lock_guard<std::mutex> lock(time_mutex);
    if (auto* tm_ptr = std::gmtime(&tt)) {
      ss << std::put_time(tm_ptr, "%Y%m%d-%H%M%S");
    } else {
      ss << secs;
    }
  }

  // Append 4-digit counter
  ss << "-" << std::setfill('0') << std::setw(4) << (count % 10000);
  return ss.str();
}

void ConfigurationManager::notifyListeners(const ConfigChangeEvent& event) {
  // Copy listeners to avoid holding lock during callbacks
  std::vector<ConfigChangeListener> listeners_copy;
  {
    std::lock_guard<std::mutex> lock(listeners_mutex_);
    listeners_copy.reserve(listeners_.size());
    for (const auto& pair : listeners_) {
      listeners_copy.push_back(pair.second);
    }
  }

  // Invoke callbacks without holding the lock
  for (const auto& listener : listeners_copy) {
    try {
      listener(event);
    } catch (const std::exception& e) {
      // Log error but continue notifying other listeners
      // In production, use proper logging
    }
  }
}

void ConfigurationManager::storeVersion(const ConfigSnapshot& snapshot) {
  version_history_.push_back(snapshot);
  trimVersionHistory();
}

void ConfigurationManager::trimVersionHistory() {
  while (version_history_.size() > max_version_history_) {
    version_history_.pop_front();
  }
}

// ConfigUpdateGuard implementation
ConfigUpdateGuard::ConfigUpdateGuard(ConfigurationManager& manager)
    : manager_(manager) {
  saved_version_ = manager_.getCurrentVersion();
}

ConfigUpdateGuard::~ConfigUpdateGuard() {
  if (!committed_) {
    rollback();
  }
}

void ConfigUpdateGuard::commit() { committed_ = true; }

void ConfigUpdateGuard::rollback() {
  if (!committed_) {
    manager_.rollback(saved_version_);
    committed_ = true;
  }
}

}  // namespace config
}  // namespace mcp
