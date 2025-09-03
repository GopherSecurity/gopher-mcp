/**
 * @file config_manager.cc
 * @brief Implementation of the central configuration manager
 */

#include "mcp/config/config_manager.h"
#include "mcp/config/parse_error.h"
#include "mcp/config/units.h"
#include "mcp/config/types_with_validation.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <algorithm>
#include <cstdlib>
#include <sys/stat.h>

namespace mcp {
namespace config {

// Static members
std::once_flag ConfigurationManager::init_flag_;
std::unique_ptr<ConfigurationManager> ConfigurationManager::instance_;

// FileConfigSource implementation
FileConfigSource::FileConfigSource(const std::string& filepath, Priority priority)
    : filepath_(filepath), 
      priority_(priority),
      last_check_(std::chrono::system_clock::now()),
      last_modified_(std::chrono::system_clock::time_point()) {}

std::string FileConfigSource::getName() const {
    return "file:" + filepath_;
}

bool FileConfigSource::hasConfiguration() const {
    struct stat file_stat;
    return stat(filepath_.c_str(), &file_stat) == 0;
}

nlohmann::json FileConfigSource::loadConfiguration() {
    std::ifstream file(filepath_);
    if (!file.is_open()) {
        return nlohmann::json();
    }
    
    nlohmann::json config;
    try {
        file >> config;
        
        // Update last modified time
        struct stat file_stat;
        if (stat(filepath_.c_str(), &file_stat) == 0) {
            last_modified_ = std::chrono::system_clock::from_time_t(file_stat.st_mtime);
        }
    } catch (const nlohmann::json::exception& e) {
        throw ConfigParseError("Failed to parse JSON from " + filepath_ + ": " + e.what());
    }
    
    return config;
}

bool FileConfigSource::hasChanged() const {
    auto now = std::chrono::system_clock::now();
    
    // Rate limit checks to once per second
    if (now - last_check_ < std::chrono::seconds(1)) {
        return false;
    }
    
    last_check_ = now;
    
    struct stat file_stat;
    if (stat(filepath_.c_str(), &file_stat) != 0) {
        return false;
    }
    
    auto file_time = std::chrono::system_clock::from_time_t(file_stat.st_mtime);
    return file_time > last_modified_;
}

std::chrono::system_clock::time_point FileConfigSource::getLastModified() const {
    return last_modified_;
}

// JsonConfigSource implementation
JsonConfigSource::JsonConfigSource(const std::string& name,
                                   const nlohmann::json& config,
                                   Priority priority)
    : name_(name), config_(config), priority_(priority) {}

void JsonConfigSource::updateConfiguration(const nlohmann::json& config) {
    config_ = config;
}

// EnvironmentConfigSource implementation
EnvironmentConfigSource::EnvironmentConfigSource(const std::string& prefix)
    : prefix_(prefix) {}

bool EnvironmentConfigSource::hasConfiguration() const {
    // Check if any environment variables with the prefix exist
    extern char **environ;
    for (char **env = environ; *env; ++env) {
        std::string var(*env);
        if (var.substr(0, prefix_.length()) == prefix_) {
            return true;
        }
    }
    return false;
}

nlohmann::json EnvironmentConfigSource::loadConfiguration() {
    return parseEnvironmentVariables();
}

nlohmann::json EnvironmentConfigSource::parseEnvironmentVariables() {
    nlohmann::json config;
    
    // Common MCP environment variables mapping
    std::map<std::string, std::function<void(const std::string&, nlohmann::json&)>> mappings = {
        {prefix_ + "NODE_ID", [](const std::string& val, nlohmann::json& j) {
            j["node"]["id"] = val;
        }},
        {prefix_ + "NODE_CLUSTER", [](const std::string& val, nlohmann::json& j) {
            j["node"]["cluster"] = val;
        }},
        {prefix_ + "ADMIN_PORT", [](const std::string& val, nlohmann::json& j) {
            j["admin"]["port"] = std::stoi(val);
        }},
        {prefix_ + "ADMIN_ADDRESS", [](const std::string& val, nlohmann::json& j) {
            j["admin"]["address"] = val;
        }},
        {prefix_ + "SERVER_NAME", [](const std::string& val, nlohmann::json& j) {
            j["server"]["name"] = val;
        }},
        {prefix_ + "MAX_SESSIONS", [](const std::string& val, nlohmann::json& j) {
            j["server"]["max_sessions"] = std::stoi(val);
        }},
        {prefix_ + "SESSION_TIMEOUT", [](const std::string& val, nlohmann::json& j) {
            j["server"]["session_timeout"] = val;  // Can be string with units
        }}
    };
    
    for (const auto& [env_var, setter] : mappings) {
        const char* value = std::getenv(env_var.c_str());
        if (value) {
            try {
                setter(value, config);
            } catch (const std::exception& e) {
                // Log error but continue processing other variables
                // In production, use proper logging
            }
        }
    }
    
    return config;
}

// ConfigurationManager implementation
ConfigurationManager::ConfigurationManager() 
    : last_reload_(std::chrono::system_clock::now()) {}

ConfigurationManager::~ConfigurationManager() = default;

ConfigurationManager& ConfigurationManager::getInstance() {
    std::call_once(init_flag_, []() {
        instance_.reset(new ConfigurationManager());
    });
    return *instance_;
}

bool ConfigurationManager::initialize(const std::vector<std::shared_ptr<ConfigSource>>& sources,
                                       UnknownFieldPolicy policy) {
    std::unique_lock<std::shared_mutex> config_lock(config_mutex_);
    std::lock_guard<std::mutex> sources_lock(sources_mutex_);
    
    if (initialized_.load()) {
        // Already initialized, clear and re-initialize
        reset();
    }
    
    validation_context_.setUnknownFieldPolicy(policy);
    
    // Add default sources if none provided
    if (sources.empty()) {
        // Add default file source
        auto file_source = std::make_shared<FileConfigSource>(
            "/etc/mcp/config.json", ConfigSource::Priority::FILE);
        if (file_source->hasConfiguration()) {
            sources_.push_back(file_source);
        }
        
        // Add environment source
        sources_.push_back(std::make_shared<EnvironmentConfigSource>("MCP_"));
    } else {
        sources_ = sources;
    }
    
    // Sort sources by priority (higher priority last for override)
    std::sort(sources_.begin(), sources_.end(),
              [](const auto& a, const auto& b) {
                  return a->getPriority() < b->getPriority();
              });
    
    initialized_ = true;
    return true;
}

bool ConfigurationManager::isInitialized() const {
    return initialized_.load();
}

void ConfigurationManager::loadConfiguration() {
    if (!initialized_.load()) {
        throw std::runtime_error("ConfigurationManager not initialized");
    }
    
    auto merged = mergeConfigurations();
    auto snapshot = parseConfiguration(merged);
    
    // Atomic update under write lock
    {
        std::unique_lock<std::shared_mutex> lock(config_mutex_);
        
        std::string old_version = current_config_.version_id;
        current_config_ = snapshot;
        
        // Store in version history
        storeVersion(snapshot);
        
        // Notify listeners
        ConfigChangeEvent event;
        event.type = ConfigChangeEvent::INITIAL_LOAD;
        event.previous_version = old_version;
        event.new_version = snapshot.version_id;
        event.timestamp = std::chrono::system_clock::now();
        
        lock.unlock();
        notifyListeners(event);
    }
}

bool ConfigurationManager::reload() {
    if (!initialized_.load()) {
        return false;
    }
    
    // Check if any source has changed
    bool has_changes = false;
    {
        std::lock_guard<std::mutex> lock(sources_mutex_);
        for (const auto& source : sources_) {
            if (source->hasChanged()) {
                has_changes = true;
                break;
            }
        }
    }
    
    if (!has_changes) {
        return false;
    }
    
    // Load new configuration
    auto merged = mergeConfigurations();
    auto snapshot = parseConfiguration(merged);
    
    // Update configuration
    {
        std::unique_lock<std::shared_mutex> lock(config_mutex_);
        
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
        
        lock.unlock();
        notifyListeners(event);
    }
    
    return true;
}

std::shared_ptr<const BootstrapConfig> ConfigurationManager::getBootstrapConfig() const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    return current_config_.bootstrap;
}

std::shared_ptr<const ServerConfig> ConfigurationManager::getServerConfig() const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    return current_config_.server;
}

ConfigSnapshot ConfigurationManager::getSnapshot() const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
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
    
    auto it = std::find_if(sources_.begin(), sources_.end(),
                            [&name](const auto& source) {
                                return source->getName() == name;
                            });
    
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
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    return current_config_.version_id;
}

std::vector<std::string> ConfigurationManager::getVersionHistory() const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    
    std::vector<std::string> versions;
    for (const auto& snapshot : version_history_) {
        versions.push_back(snapshot.version_id);
    }
    return versions;
}

bool ConfigurationManager::rollback(const std::string& version_id) {
    std::unique_lock<std::shared_mutex> lock(config_mutex_);
    
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
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    return version_history_.size();
}

void ConfigurationManager::setMaxVersionHistory(size_t max_versions) {
    std::unique_lock<std::shared_mutex> lock(config_mutex_);
    max_version_history_ = max_versions;
    trimVersionHistory();
}

void ConfigurationManager::reset() {
    std::unique_lock<std::shared_mutex> config_lock(config_mutex_);
    std::lock_guard<std::mutex> sources_lock(sources_mutex_);
    std::lock_guard<std::mutex> listeners_lock(listeners_mutex_);
    
    current_config_ = ConfigSnapshot();
    sources_.clear();
    listeners_.clear();
    version_history_.clear();
    initialized_ = false;
    reload_count_ = 0;
}

nlohmann::json ConfigurationManager::mergeConfigurations() {
    std::lock_guard<std::mutex> lock(sources_mutex_);
    
    nlohmann::json merged;
    
    // Merge in priority order (lowest to highest)
    // TODO: Implement deterministic merge semantics with:
    // - Named resource merge-by-name support
    // - Conflict detection and reporting
    // - Array merge strategies (replace/append/merge-by-key)
    // Current implementation uses merge_patch which replaces arrays wholesale
    for (const auto& source : sources_) {
        if (source->hasConfiguration()) {
            try {
                auto config = source->loadConfiguration();
                if (!config.empty()) {
                    // Deep merge using JSON merge patch (RFC 7396)
                    // Note: Arrays are replaced entirely, not merged
                    merged.merge_patch(config);
                }
            } catch (const std::exception& e) {
                // Log error but continue with other sources
                // In production, use proper logging
            }
        }
    }
    
    return merged;
}

ConfigSnapshot ConfigurationManager::parseConfiguration(const nlohmann::json& config) {
    ConfigSnapshot snapshot;
    snapshot.version_id = generateVersionId();
    snapshot.timestamp = std::chrono::system_clock::now();
    
    // Use a mutable copy of validation context for this parse operation
    ValidationContext ctx = validation_context_;
    
    // Parse bootstrap configuration with validation
    snapshot.bootstrap = std::make_shared<BootstrapConfig>();
    
    // Check for unknown fields at root level
    static const std::set<std::string> root_fields = {
        "node", "admin", "version", "server", "_config_path"
    };
    validateJsonFields(config, root_fields, "", ctx);
    
    if (config.contains("node")) {
        snapshot.bootstrap->node = NodeConfigWithValidation::fromJson(config["node"], ctx);
    }
    if (config.contains("admin")) {
        snapshot.bootstrap->admin = AdminConfigWithValidation::fromJson(config["admin"], ctx);
    }
    if (config.contains("version")) {
        try {
            snapshot.bootstrap->version = config["version"].get<std::string>();
        } catch (const nlohmann::json::exception& e) {
            throw ConfigValidationError("version", "Type error: " + std::string(e.what()));
        }
    }
    
    // Parse server configuration with validation
    snapshot.server = std::make_shared<ServerConfig>();
    if (config.contains("server")) {
        *snapshot.server = ServerConfigWithValidation::fromJson(config["server"], ctx);
    }
    
    // Validate configurations
    snapshot.bootstrap->validate();
    snapshot.server->validate();
    
    // Report on unknown fields if any were encountered
    if (ctx.hasUnknownFields()) {
        const auto& unknownFields = ctx.getUnknownFields();
        // These would have already been reported via the warning handler
        // during parsing based on the policy
    }
    
    return snapshot;
}

std::string ConfigurationManager::generateVersionId() {
    // Generate a unique version ID
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y%m%d-%H%M%S");
    
    // Add random component for uniqueness
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    ss << "-" << dis(gen);
    
    return ss.str();
}

void ConfigurationManager::notifyListeners(const ConfigChangeEvent& event) {
    std::lock_guard<std::mutex> lock(listeners_mutex_);
    
    for (const auto& [id, listener] : listeners_) {
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

void ConfigUpdateGuard::commit() {
    committed_ = true;
}

void ConfigUpdateGuard::rollback() {
    if (!committed_) {
        manager_.rollback(saved_version_);
        committed_ = true;
    }
}

} // namespace config
} // namespace mcp