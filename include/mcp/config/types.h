/**
 * @file types.h
 * @brief Core configuration data models for Gopher-MCP server
 * 
 * This file defines the fundamental configuration structures used throughout
 * the MCP server for type-safe configuration access. These structures support
 * JSON serialization/deserialization and provide validation capabilities.
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <chrono>

#include <nlohmann/json.hpp>

namespace mcp {
namespace config {

/**
 * @brief Configuration validation exception
 * 
 * Thrown when configuration validation fails, providing detailed error context
 */
class ConfigValidationError : public std::runtime_error {
public:
    ConfigValidationError(const std::string& field, const std::string& reason)
        : std::runtime_error(formatError(field, reason)),
          field_(field),
          reason_(reason) {}
    
    const std::string& field() const { return field_; }
    const std::string& reason() const { return reason_; }

private:
    static std::string formatError(const std::string& field, const std::string& reason) {
        std::ostringstream oss;
        oss << "Configuration validation failed for field '" << field << "': " << reason;
        return oss.str();
    }
    
    std::string field_;
    std::string reason_;
};

/**
 * @brief Node configuration
 * 
 * Defines the identity and metadata for a node in the MCP cluster
 */
struct NodeConfig {
    /// Unique identifier for this node
    std::string id = "gopher-mcp-node-1";
    
    /// Cluster this node belongs to
    std::string cluster = "default";
    
    /// Optional metadata key-value pairs for this node
    std::map<std::string, std::string> metadata;
    
    /// Optional region identifier
    std::string region;
    
    /// Optional availability zone
    std::string zone;
    
    /**
     * @brief Validate the node configuration
     * @throws ConfigValidationError if validation fails
     */
    void validate() const {
        if (id.empty()) {
            throw ConfigValidationError("node.id", "Node ID cannot be empty");
        }
        
        if (id.length() > 256) {
            throw ConfigValidationError("node.id", 
                "Node ID length cannot exceed 256 characters");
        }
        
        // Validate ID contains only valid characters (alphanumeric, dash, underscore)
        for (char c : id) {
            if (!std::isalnum(c) && c != '-' && c != '_') {
                throw ConfigValidationError("node.id", 
                    "Node ID can only contain alphanumeric characters, dashes, and underscores");
            }
        }
        
        if (cluster.empty()) {
            throw ConfigValidationError("node.cluster", "Cluster name cannot be empty");
        }
        
        if (cluster.length() > 128) {
            throw ConfigValidationError("node.cluster", 
                "Cluster name length cannot exceed 128 characters");
        }
        
        // Validate metadata keys
        for (const auto& kv : metadata) {
            if (kv.first.empty()) {
                throw ConfigValidationError("node.metadata", 
                    "Metadata keys cannot be empty");
            }
            if (kv.first.length() > 128) {
                throw ConfigValidationError("node.metadata", 
                    "Metadata key '" + kv.first + "' exceeds maximum length of 128");
            }
            if (kv.second.length() > 512) {
                throw ConfigValidationError("node.metadata", 
                    "Metadata value for key '" + kv.first + "' exceeds maximum length of 512");
            }
        }
    }
    
    /**
     * @brief Convert to JSON
     */
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["id"] = id;
        j["cluster"] = cluster;
        
        if (!metadata.empty()) {
            j["metadata"] = metadata;
        }
        if (!region.empty()) {
            j["region"] = region;
        }
        if (!zone.empty()) {
            j["zone"] = zone;
        }
        
        return j;
    }
    
    /**
     * @brief Create from JSON
     */
    static NodeConfig fromJson(const nlohmann::json& j) {
        NodeConfig config;
        
        if (j.contains("id") && !j["id"].is_null()) {
            config.id = j["id"].get<std::string>();
        }
        
        if (j.contains("cluster") && !j["cluster"].is_null()) {
            config.cluster = j["cluster"].get<std::string>();
        }
        
        if (j.contains("metadata") && j["metadata"].is_object()) {
            config.metadata = j["metadata"].get<std::map<std::string, std::string>>();
        }
        
        if (j.contains("region") && !j["region"].is_null()) {
            config.region = j["region"].get<std::string>();
        }
        
        if (j.contains("zone") && !j["zone"].is_null()) {
            config.zone = j["zone"].get<std::string>();
        }
        
        return config;
    }
};

/**
 * @brief Admin interface configuration
 * 
 * Defines settings for the administrative HTTP interface
 */
struct AdminConfig {
    /// Address to bind the admin interface to
    std::string address = "127.0.0.1";
    
    /// Port for the admin interface
    uint16_t port = 9901;
    
    /// List of IP addresses or CIDR blocks allowed to access admin interface
    std::vector<std::string> allowed_ips = {"127.0.0.1", "::1"};
    
    /// Enable/disable the admin interface
    bool enabled = true;
    
    /// Path prefix for admin endpoints
    std::string path_prefix = "/admin";
    
    /// Enable CORS for browser-based tools
    bool enable_cors = false;
    
    /// CORS allowed origins (if enable_cors is true)
    std::vector<std::string> cors_origins = {"*"};
    
    /**
     * @brief Validate the admin configuration
     * @throws ConfigValidationError if validation fails
     */
    void validate() const {
        if (!enabled) {
            return; // Skip validation if admin interface is disabled
        }
        
        if (address.empty()) {
            throw ConfigValidationError("admin.address", 
                "Admin address cannot be empty when admin interface is enabled");
        }
        
        // Basic IP address validation (simplified)
        if (address != "0.0.0.0" && address != "127.0.0.1" && address != "::1" && address != "::") {
            // For other addresses, check basic format
            if (address.find_first_not_of("0123456789.:abcdefABCDEF") != std::string::npos) {
                throw ConfigValidationError("admin.address", 
                    "Invalid characters in admin address: " + address);
            }
        }
        
        if (port == 0) {
            throw ConfigValidationError("admin.port", "Admin port cannot be 0");
        }
        
        if (port < 1024 && port != 0) {
            // Warning: using privileged port, but don't fail validation
            // This would be logged in a real implementation
        }
        
        if (allowed_ips.empty()) {
            throw ConfigValidationError("admin.allowed_ips", 
                "At least one allowed IP must be specified for admin interface");
        }
        
        // Validate allowed IPs format (basic validation)
        for (const auto& ip : allowed_ips) {
            if (ip.empty()) {
                throw ConfigValidationError("admin.allowed_ips", 
                    "Empty IP address in allowed_ips list");
            }
            
            // Check for CIDR notation
            size_t slash_pos = ip.find('/');
            if (slash_pos != std::string::npos) {
                std::string prefix = ip.substr(slash_pos + 1);
                try {
                    int prefix_len = std::stoi(prefix);
                    if (prefix_len < 0 || prefix_len > 128) {
                        throw ConfigValidationError("admin.allowed_ips", 
                            "Invalid CIDR prefix length in: " + ip);
                    }
                } catch (const std::exception&) {
                    throw ConfigValidationError("admin.allowed_ips", 
                        "Invalid CIDR notation in: " + ip);
                }
            }
        }
        
        if (path_prefix.empty()) {
            throw ConfigValidationError("admin.path_prefix", 
                "Admin path prefix cannot be empty");
        }
        
        if (path_prefix[0] != '/') {
            throw ConfigValidationError("admin.path_prefix", 
                "Admin path prefix must start with '/'");
        }
        
        if (enable_cors && cors_origins.empty()) {
            throw ConfigValidationError("admin.cors_origins", 
                "CORS origins must be specified when CORS is enabled");
        }
    }
    
    /**
     * @brief Convert to JSON
     */
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["address"] = address;
        j["port"] = port;
        j["allowed_ips"] = allowed_ips;
        j["enabled"] = enabled;
        j["path_prefix"] = path_prefix;
        j["enable_cors"] = enable_cors;
        
        if (enable_cors && !cors_origins.empty()) {
            j["cors_origins"] = cors_origins;
        }
        
        return j;
    }
    
    /**
     * @brief Create from JSON
     */
    static AdminConfig fromJson(const nlohmann::json& j) {
        AdminConfig config;
        
        if (j.contains("address") && !j["address"].is_null()) {
            config.address = j["address"].get<std::string>();
        }
        
        if (j.contains("port") && !j["port"].is_null()) {
            config.port = j["port"].get<uint16_t>();
        }
        
        if (j.contains("allowed_ips") && j["allowed_ips"].is_array()) {
            config.allowed_ips = j["allowed_ips"].get<std::vector<std::string>>();
        }
        
        if (j.contains("enabled") && !j["enabled"].is_null()) {
            config.enabled = j["enabled"].get<bool>();
        }
        
        if (j.contains("path_prefix") && !j["path_prefix"].is_null()) {
            config.path_prefix = j["path_prefix"].get<std::string>();
        }
        
        if (j.contains("enable_cors") && !j["enable_cors"].is_null()) {
            config.enable_cors = j["enable_cors"].get<bool>();
        }
        
        if (j.contains("cors_origins") && j["cors_origins"].is_array()) {
            config.cors_origins = j["cors_origins"].get<std::vector<std::string>>();
        }
        
        return config;
    }
};

/**
 * @brief Bootstrap configuration
 * 
 * Root configuration structure that contains all server configuration settings.
 * This is the entry point for configuration loading and validation.
 */
struct BootstrapConfig {
    /// Node configuration
    NodeConfig node;
    
    /// Admin interface configuration
    AdminConfig admin;
    
    /// Configuration format version for compatibility checking
    std::string version = "1.0";
    
    /// Optional configuration file path (for tracking source)
    std::string config_path;
    
    /// Timestamp when configuration was loaded (set automatically)
    std::chrono::system_clock::time_point loaded_at;
    
    /**
     * @brief Validate the entire bootstrap configuration
     * @throws ConfigValidationError if validation fails
     */
    void validate() const {
        // Validate version format
        if (version.empty()) {
            throw ConfigValidationError("version", "Configuration version cannot be empty");
        }
        
        // Simple version format check (e.g., "1.0", "2.1.3")
        bool valid_version = true;
        int dot_count = 0;
        for (size_t i = 0; i < version.length(); ++i) {
            char c = version[i];
            if (c == '.') {
                dot_count++;
                if (i == 0 || i == version.length() - 1) {
                    valid_version = false;
                    break;
                }
                if (i > 0 && version[i-1] == '.') {
                    valid_version = false;
                    break;
                }
            } else if (!std::isdigit(c)) {
                valid_version = false;
                break;
            }
        }
        
        if (!valid_version || dot_count > 2) {
            throw ConfigValidationError("version", 
                "Invalid version format. Expected format: X.Y or X.Y.Z where X,Y,Z are numbers");
        }
        
        // Validate nested configurations
        try {
            node.validate();
        } catch (const ConfigValidationError& e) {
            // Re-throw with context
            throw e;
        }
        
        try {
            admin.validate();
        } catch (const ConfigValidationError& e) {
            // Re-throw with context
            throw e;
        }
    }
    
    /**
     * @brief Convert to JSON
     */
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["version"] = version;
        j["node"] = node.toJson();
        j["admin"] = admin.toJson();
        
        if (!config_path.empty()) {
            j["_config_path"] = config_path;
        }
        
        // Convert timestamp to ISO string
        if (loaded_at.time_since_epoch().count() > 0) {
            auto time_t = std::chrono::system_clock::to_time_t(loaded_at);
            char buffer[100];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&time_t));
            j["_loaded_at"] = std::string(buffer);
        }
        
        return j;
    }
    
    /**
     * @brief Create from JSON
     */
    static BootstrapConfig fromJson(const nlohmann::json& j) {
        BootstrapConfig config;
        
        if (j.contains("version") && !j["version"].is_null()) {
            config.version = j["version"].get<std::string>();
        }
        
        if (j.contains("node") && j["node"].is_object()) {
            config.node = NodeConfig::fromJson(j["node"]);
        }
        
        if (j.contains("admin") && j["admin"].is_object()) {
            config.admin = AdminConfig::fromJson(j["admin"]);
        }
        
        if (j.contains("_config_path") && !j["_config_path"].is_null()) {
            config.config_path = j["_config_path"].get<std::string>();
        }
        
        // Set loaded_at to current time
        config.loaded_at = std::chrono::system_clock::now();
        
        return config;
    }
    
    /**
     * @brief Create a default bootstrap configuration
     * 
     * Returns a valid default configuration that can be used when no
     * configuration file is provided.
     */
    static BootstrapConfig createDefault() {
        BootstrapConfig config;
        config.loaded_at = std::chrono::system_clock::now();
        return config;
    }
    
    /**
     * @brief Merge another configuration into this one
     * 
     * Used for configuration overlays and updates. Later values override earlier ones.
     * 
     * @param other Configuration to merge into this one
     */
    void merge(const BootstrapConfig& other) {
        // Version is not merged, kept from base
        
        // Merge node configuration
        if (!other.node.id.empty() && other.node.id != "gopher-mcp-node-1") {
            node.id = other.node.id;
        }
        if (!other.node.cluster.empty() && other.node.cluster != "default") {
            node.cluster = other.node.cluster;
        }
        if (!other.node.region.empty()) {
            node.region = other.node.region;
        }
        if (!other.node.zone.empty()) {
            node.zone = other.node.zone;
        }
        
        // Merge metadata (additive)
        for (const auto& kv : other.node.metadata) {
            node.metadata[kv.first] = kv.second;
        }
        
        // Merge admin configuration
        if (other.admin.address != "127.0.0.1") {
            admin.address = other.admin.address;
        }
        if (other.admin.port != 9901) {
            admin.port = other.admin.port;
        }
        if (!other.admin.allowed_ips.empty() && 
            other.admin.allowed_ips != std::vector<std::string>{"127.0.0.1", "::1"}) {
            admin.allowed_ips = other.admin.allowed_ips;
        }
        admin.enabled = other.admin.enabled;
        if (other.admin.path_prefix != "/admin") {
            admin.path_prefix = other.admin.path_prefix;
        }
        admin.enable_cors = other.admin.enable_cors;
        if (!other.admin.cors_origins.empty() && 
            other.admin.cors_origins != std::vector<std::string>{"*"}) {
            admin.cors_origins = other.admin.cors_origins;
        }
        
        // Update config path if provided
        if (!other.config_path.empty()) {
            config_path = other.config_path;
        }
    }
    
    /**
     * @brief Equality operator for testing
     */
    bool operator==(const BootstrapConfig& other) const {
        return version == other.version &&
               node.id == other.node.id &&
               node.cluster == other.node.cluster &&
               node.metadata == other.node.metadata &&
               node.region == other.node.region &&
               node.zone == other.node.zone &&
               admin.address == other.admin.address &&
               admin.port == other.admin.port &&
               admin.allowed_ips == other.admin.allowed_ips &&
               admin.enabled == other.admin.enabled &&
               admin.path_prefix == other.admin.path_prefix &&
               admin.enable_cors == other.admin.enable_cors &&
               admin.cors_origins == other.admin.cors_origins;
    }
    
    bool operator!=(const BootstrapConfig& other) const {
        return !(*this == other);
    }
};

} // namespace config
} // namespace mcp

#endif // MCP_CONFIG_TYPES_H