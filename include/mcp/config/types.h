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
#include <set>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <chrono>
#include <cctype>

#include <nlohmann/json.hpp>
#include "mcp/config/units.h"  // For unit parsing

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

/**
 * @brief Configuration version management
 * 
 * Handles semantic versioning for configuration schemas
 */
class ConfigVersion {
public:
    ConfigVersion() : major_(1), minor_(0), patch_(0) {}
    
    ConfigVersion(int major, int minor, int patch = 0)
        : major_(major), minor_(minor), patch_(patch) {}
    
    /**
     * @brief Parse version string (e.g., "1.2.3" or "v1.2.3")
     */
    static ConfigVersion parse(const std::string& version) {
        std::string v = version;
        // Remove leading 'v' if present
        if (!v.empty() && v[0] == 'v') {
            v = v.substr(1);
        }
        
        int major = 0, minor = 0, patch = 0;
        std::istringstream iss(v);
        char dot1, dot2;
        
        iss >> major >> dot1 >> minor;
        if (dot1 != '.') {
            throw ConfigValidationError("version", "Invalid version format: " + version);
        }
        
        if (iss >> dot2 >> patch) {
            if (dot2 != '.') {
                throw ConfigValidationError("version", "Invalid version format: " + version);
            }
        }
        
        return ConfigVersion(major, minor, patch);
    }
    
    std::string toString() const {
        std::ostringstream oss;
        oss << major_ << "." << minor_ << "." << patch_;
        return oss.str();
    }
    
    bool operator==(const ConfigVersion& other) const {
        return major_ == other.major_ && minor_ == other.minor_ && patch_ == other.patch_;
    }
    
    bool operator!=(const ConfigVersion& other) const {
        return !(*this == other);
    }
    
    bool operator<(const ConfigVersion& other) const {
        if (major_ != other.major_) return major_ < other.major_;
        if (minor_ != other.minor_) return minor_ < other.minor_;
        return patch_ < other.patch_;
    }
    
    bool operator<=(const ConfigVersion& other) const {
        return (*this < other) || (*this == other);
    }
    
    bool operator>(const ConfigVersion& other) const {
        return !(*this <= other);
    }
    
    bool operator>=(const ConfigVersion& other) const {
        return !(*this < other);
    }
    
    bool isCompatibleWith(const ConfigVersion& required) const {
        // Same major version and at least the required minor version
        return major_ == required.major_ && *this >= required;
    }
    
    int major() const { return major_; }
    int minor() const { return minor_; }
    int patch() const { return patch_; }

private:
    int major_;
    int minor_;
    int patch_;
};

/**
 * @brief Filter configuration
 * 
 * Defines a single filter in the processing chain
 */
struct FilterConfig {
    /// Filter type (e.g., "buffer", "rate_limit", "tap")
    std::string type;
    
    /// Unique name for this filter instance
    std::string name;
    
    /// Filter-specific configuration as JSON
    nlohmann::json config = nlohmann::json::object();
    
    /// Whether this filter is enabled
    bool enabled = true;
    
    /**
     * @brief Validate the filter configuration
     */
    void validate() const {
        if (type.empty()) {
            throw ConfigValidationError("filter.type", "Filter type cannot be empty");
        }
        
        if (name.empty()) {
            throw ConfigValidationError("filter.name", "Filter name cannot be empty");
        }
        
        // Name should be unique within a chain (validated at chain level)
        // Type should be a valid registered filter type (validated at runtime)
    }
    
    /**
     * @brief Convert to JSON
     */
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["type"] = type;
        j["name"] = name;
        j["config"] = config;
        j["enabled"] = enabled;
        return j;
    }
    
    /**
     * @brief Create from JSON
     */
    static FilterConfig fromJson(const nlohmann::json& j) {
        FilterConfig fc;
        
        if (j.contains("type")) {
            fc.type = j["type"].get<std::string>();
        }
        
        if (j.contains("name")) {
            fc.name = j["name"].get<std::string>();
        }
        
        if (j.contains("config")) {
            fc.config = j["config"];
        }
        
        if (j.contains("enabled")) {
            fc.enabled = j["enabled"].get<bool>();
        }
        
        return fc;
    }
    
    bool operator==(const FilterConfig& other) const {
        return type == other.type && 
               name == other.name &&
               config == other.config &&
               enabled == other.enabled;
    }
    
    bool operator!=(const FilterConfig& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Filter chain configuration
 * 
 * Defines a named sequence of filters for a transport
 */
struct FilterChainConfig {
    /// Chain name (e.g., "default", "http", "grpc")
    std::string name = "default";
    
    /// Transport type this chain applies to
    std::string transport_type = "tcp";
    
    /// Ordered list of filters in the chain
    std::vector<FilterConfig> filters;
    
    /**
     * @brief Validate the filter chain configuration
     */
    void validate() const {
        if (name.empty()) {
            throw ConfigValidationError("filter_chain.name", "Filter chain name cannot be empty");
        }
        
        if (transport_type.empty()) {
            throw ConfigValidationError("filter_chain.transport_type", 
                "Transport type cannot be empty");
        }
        
        // Validate each filter
        for (size_t i = 0; i < filters.size(); ++i) {
            try {
                filters[i].validate();
            } catch (const ConfigValidationError& e) {
                throw ConfigValidationError(
                    "filter_chain.filters[" + std::to_string(i) + "]." + e.field(), 
                    e.reason());
            }
        }
        
        // Check for duplicate filter names within the chain
        std::map<std::string, size_t> name_counts;
        for (const auto& filter : filters) {
            if (++name_counts[filter.name] > 1) {
                throw ConfigValidationError("filter_chain.filters", 
                    "Duplicate filter name: " + filter.name);
            }
        }
    }
    
    /**
     * @brief Convert to JSON
     */
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["name"] = name;
        j["transport_type"] = transport_type;
        j["filters"] = nlohmann::json::array();
        
        for (const auto& filter : filters) {
            j["filters"].push_back(filter.toJson());
        }
        
        return j;
    }
    
    /**
     * @brief Create from JSON
     */
    static FilterChainConfig fromJson(const nlohmann::json& j) {
        FilterChainConfig fcc;
        
        if (j.contains("name")) {
            fcc.name = j["name"].get<std::string>();
        }
        
        if (j.contains("transport_type")) {
            fcc.transport_type = j["transport_type"].get<std::string>();
        }
        
        if (j.contains("filters") && j["filters"].is_array()) {
            for (const auto& filter_json : j["filters"]) {
                fcc.filters.push_back(FilterConfig::fromJson(filter_json));
            }
        }
        
        return fcc;
    }
    
    /**
     * @brief Merge another filter chain configuration
     */
    void merge(const FilterChainConfig& other) {
        if (!other.name.empty() && other.name != "default") {
            name = other.name;
        }
        
        if (!other.transport_type.empty() && other.transport_type != "tcp") {
            transport_type = other.transport_type;
        }
        
        // For filters, replace the entire list if other has filters
        if (!other.filters.empty()) {
            filters = other.filters;
        }
    }
    
    bool operator==(const FilterChainConfig& other) const {
        return name == other.name &&
               transport_type == other.transport_type &&
               filters == other.filters;
    }
    
    bool operator!=(const FilterChainConfig& other) const {
        return !(*this == other);
    }
};

/**
 * @brief TLS configuration settings
 */
struct TLSConfig {
    /// Whether TLS is enabled
    bool enabled = false;
    
    /// Path to certificate file
    std::string cert_file;
    
    /// Path to private key file
    std::string key_file;
    
    /// Path to CA certificate file for client verification
    std::string ca_file;
    
    /// Whether to verify client certificates
    bool verify_client = false;
    
    /// Minimum TLS version (e.g., "1.2", "1.3")
    std::string min_version = "1.2";
    
    /// Cipher suites (empty means use defaults)
    std::vector<std::string> cipher_suites;
    
    /**
     * @brief Validate TLS configuration
     */
    void validate() const {
        if (enabled) {
            if (cert_file.empty()) {
                throw ConfigValidationError("tls.cert_file", 
                    "Certificate file is required when TLS is enabled");
            }
            
            if (key_file.empty()) {
                throw ConfigValidationError("tls.key_file", 
                    "Private key file is required when TLS is enabled");
            }
            
            if (verify_client && ca_file.empty()) {
                throw ConfigValidationError("tls.ca_file", 
                    "CA file is required when client verification is enabled");
            }
            
            // Validate TLS version
            if (min_version != "1.0" && min_version != "1.1" && 
                min_version != "1.2" && min_version != "1.3") {
                throw ConfigValidationError("tls.min_version", 
                    "Invalid TLS version: " + min_version);
            }
        }
    }
    
    /**
     * @brief Convert to JSON
     */
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["enabled"] = enabled;
        j["cert_file"] = cert_file;
        j["key_file"] = key_file;
        j["ca_file"] = ca_file;
        j["verify_client"] = verify_client;
        j["min_version"] = min_version;
        j["cipher_suites"] = cipher_suites;
        return j;
    }
    
    /**
     * @brief Create from JSON
     */
    static TLSConfig fromJson(const nlohmann::json& j) {
        TLSConfig tls;
        
        if (j.contains("enabled")) {
            tls.enabled = j["enabled"].get<bool>();
        }
        
        if (j.contains("cert_file")) {
            tls.cert_file = j["cert_file"].get<std::string>();
        }
        
        if (j.contains("key_file")) {
            tls.key_file = j["key_file"].get<std::string>();
        }
        
        if (j.contains("ca_file")) {
            tls.ca_file = j["ca_file"].get<std::string>();
        }
        
        if (j.contains("verify_client")) {
            tls.verify_client = j["verify_client"].get<bool>();
        }
        
        if (j.contains("min_version")) {
            tls.min_version = j["min_version"].get<std::string>();
        }
        
        if (j.contains("cipher_suites") && j["cipher_suites"].is_array()) {
            tls.cipher_suites = j["cipher_suites"].get<std::vector<std::string>>();
        }
        
        return tls;
    }
    
    bool operator==(const TLSConfig& other) const {
        return enabled == other.enabled &&
               cert_file == other.cert_file &&
               key_file == other.key_file &&
               ca_file == other.ca_file &&
               verify_client == other.verify_client &&
               min_version == other.min_version &&
               cipher_suites == other.cipher_suites;
    }
    
    bool operator!=(const TLSConfig& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Transport configuration
 * 
 * Defines a transport endpoint with its filter chain
 */
struct TransportConfig {
    /// Transport type (e.g., "tcp", "stdio", "http", "https")
    std::string type = "tcp";
    
    /// Bind address (for network transports)
    std::string address = "127.0.0.1";
    
    /// Port number (for network transports)
    uint16_t port = 3333;
    
    /// TLS configuration (for secure transports)
    TLSConfig tls;
    
    /// Filter chain name to use
    std::string filter_chain = "default";
    
    /// Whether this transport is enabled
    bool enabled = true;
    
    /**
     * @brief Validate transport configuration
     */
    void validate() const {
        if (type.empty()) {
            throw ConfigValidationError("transport.type", "Transport type cannot be empty");
        }
        
        // Validate network transports
        if (type == "tcp" || type == "http" || type == "https") {
            if (address.empty()) {
                throw ConfigValidationError("transport.address", 
                    "Address is required for network transport");
            }
            
            if (port == 0) {
                throw ConfigValidationError("transport.port", 
                    "Valid port is required for network transport");
            }
        }
        
        // HTTPS requires TLS
        if (type == "https" && !tls.enabled) {
            throw ConfigValidationError("transport.tls", 
                "TLS must be enabled for HTTPS transport");
        }
        
        // Validate TLS if configured
        if (tls.enabled) {
            tls.validate();
        }
        
        if (filter_chain.empty()) {
            throw ConfigValidationError("transport.filter_chain", 
                "Filter chain name cannot be empty");
        }
    }
    
    /**
     * @brief Convert to JSON
     */
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["type"] = type;
        j["address"] = address;
        j["port"] = port;
        j["tls"] = tls.toJson();
        j["filter_chain"] = filter_chain;
        j["enabled"] = enabled;
        return j;
    }
    
    /**
     * @brief Create from JSON
     */
    static TransportConfig fromJson(const nlohmann::json& j) {
        TransportConfig tc;
        
        if (j.contains("type")) {
            tc.type = j["type"].get<std::string>();
        }
        
        if (j.contains("address")) {
            tc.address = j["address"].get<std::string>();
        }
        
        if (j.contains("port")) {
            tc.port = j["port"].get<uint16_t>();
        }
        
        if (j.contains("tls")) {
            tc.tls = TLSConfig::fromJson(j["tls"]);
        }
        
        if (j.contains("filter_chain")) {
            tc.filter_chain = j["filter_chain"].get<std::string>();
        }
        
        if (j.contains("enabled")) {
            tc.enabled = j["enabled"].get<bool>();
        }
        
        return tc;
    }
    
    bool operator==(const TransportConfig& other) const {
        return type == other.type &&
               address == other.address &&
               port == other.port &&
               tls == other.tls &&
               filter_chain == other.filter_chain &&
               enabled == other.enabled;
    }
    
    bool operator!=(const TransportConfig& other) const {
        return !(*this == other);
    }
};

/**
 * @brief MCP protocol capabilities configuration
 */
struct CapabilitiesConfig {
    /// Supported MCP protocol features
    std::vector<std::string> features = {"tools", "prompts", "resources"};
    
    /// Maximum request size in bytes
    size_t max_request_size = 10 * 1024 * 1024; // 10MB
    
    /// Maximum response size in bytes  
    size_t max_response_size = 10 * 1024 * 1024; // 10MB
    
    /// Request timeout in milliseconds
    uint32_t request_timeout_ms = 30000; // 30 seconds
    
    /**
     * @brief Convert to JSON
     */
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["features"] = features;
        j["max_request_size"] = max_request_size;
        j["max_response_size"] = max_response_size;
        j["request_timeout_ms"] = request_timeout_ms;
        return j;
    }
    
    /**
     * @brief Create from JSON
     */
    static CapabilitiesConfig fromJson(const nlohmann::json& j) {
        CapabilitiesConfig cc;
        
        if (j.contains("features") && j["features"].is_array()) {
            cc.features = j["features"].get<std::vector<std::string>>();
        }
        
        if (j.contains("max_request_size")) {
            cc.max_request_size = parseJsonSize<size_t>(j["max_request_size"], "max_request_size");
        }
        
        if (j.contains("max_response_size")) {
            cc.max_response_size = parseJsonSize<size_t>(j["max_response_size"], "max_response_size");
        }
        
        if (j.contains("request_timeout_ms")) {
            cc.request_timeout_ms = parseJsonDuration<uint32_t>(j["request_timeout_ms"], "request_timeout_ms");
        } else if (j.contains("request_timeout")) {
            // Also support "request_timeout" without _ms suffix
            cc.request_timeout_ms = parseJsonDuration<uint32_t>(j["request_timeout"], "request_timeout");
        }
        
        return cc;
    }
    
    bool operator==(const CapabilitiesConfig& other) const {
        return features == other.features &&
               max_request_size == other.max_request_size &&
               max_response_size == other.max_response_size &&
               request_timeout_ms == other.request_timeout_ms;
    }
    
    bool operator!=(const CapabilitiesConfig& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Server configuration
 * 
 * Comprehensive server settings including protocol, sessions, and resources
 */
struct ServerConfig {
    /// Server name
    std::string name = "gopher-mcp-server";
    
    /// Server version
    ConfigVersion version = ConfigVersion(1, 0, 0);
    
    /// MCP protocol capabilities
    CapabilitiesConfig capabilities;
    
    /// Maximum concurrent sessions
    uint32_t max_sessions = 1000;
    
    /// Session idle timeout in milliseconds (0 = no timeout)
    uint32_t session_timeout_ms = 300000; // 5 minutes
    
    /// Worker thread pool size (0 = auto-detect)
    uint32_t worker_threads = 0;
    
    /// Event loop thread count
    uint32_t event_threads = 1;
    
    /// List of transport configurations
    std::vector<TransportConfig> transports;
    
    /// List of filter chain configurations
    std::vector<FilterChainConfig> filter_chains;
    
    /**
     * @brief Validate server configuration
     */
    void validate() const {
        if (name.empty()) {
            throw ConfigValidationError("server.name", "Server name cannot be empty");
        }
        
        if (max_sessions == 0) {
            throw ConfigValidationError("server.max_sessions", 
                "Maximum sessions must be greater than 0");
        }
        
        if (event_threads == 0) {
            throw ConfigValidationError("server.event_threads", 
                "Event threads must be greater than 0");
        }
        
        // Validate transports
        for (size_t i = 0; i < transports.size(); ++i) {
            try {
                transports[i].validate();
            } catch (const ConfigValidationError& e) {
                throw ConfigValidationError(
                    "server.transports[" + std::to_string(i) + "]." + e.field(),
                    e.reason());
            }
        }
        
        // Validate filter chains
        for (size_t i = 0; i < filter_chains.size(); ++i) {
            try {
                filter_chains[i].validate();
            } catch (const ConfigValidationError& e) {
                throw ConfigValidationError(
                    "server.filter_chains[" + std::to_string(i) + "]." + e.field(),
                    e.reason());
            }
        }
        
        // Check for duplicate filter chain names and build set of available chains
        std::set<std::string> chain_names;
        for (size_t i = 0; i < filter_chains.size(); ++i) {
            const auto& chain_name = filter_chains[i].name;
            if (chain_names.find(chain_name) != chain_names.end()) {
                throw ConfigValidationError(
                    "server.filter_chains[" + std::to_string(i) + "].name",
                    "Duplicate filter chain name: " + chain_name);
            }
            chain_names.insert(chain_name);
        }
        
        // Check that referenced filter chains exist
        for (size_t i = 0; i < transports.size(); ++i) {
            const auto& filter_chain_ref = transports[i].filter_chain;
            if (chain_names.find(filter_chain_ref) == chain_names.end()) {
                throw ConfigValidationError(
                    "server.transports[" + std::to_string(i) + "].filter_chain",
                    "Transport references non-existent filter chain: " + filter_chain_ref);
            }
        }
    }
    
    /**
     * @brief Convert to JSON
     */
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["name"] = name;
        j["version"] = version.toString();
        j["capabilities"] = capabilities.toJson();
        j["max_sessions"] = max_sessions;
        j["session_timeout_ms"] = session_timeout_ms;
        j["worker_threads"] = worker_threads;
        j["event_threads"] = event_threads;
        
        j["transports"] = nlohmann::json::array();
        for (const auto& transport : transports) {
            j["transports"].push_back(transport.toJson());
        }
        
        j["filter_chains"] = nlohmann::json::array();
        for (const auto& chain : filter_chains) {
            j["filter_chains"].push_back(chain.toJson());
        }
        
        return j;
    }
    
    /**
     * @brief Create from JSON
     */
    static ServerConfig fromJson(const nlohmann::json& j) {
        ServerConfig sc;
        
        if (j.contains("name")) {
            sc.name = j["name"].get<std::string>();
        }
        
        if (j.contains("version")) {
            sc.version = ConfigVersion::parse(j["version"].get<std::string>());
        }
        
        if (j.contains("capabilities")) {
            sc.capabilities = CapabilitiesConfig::fromJson(j["capabilities"]);
        }
        
        if (j.contains("max_sessions")) {
            sc.max_sessions = j["max_sessions"].get<uint32_t>();
        }
        
        if (j.contains("session_timeout_ms")) {
            sc.session_timeout_ms = parseJsonDuration<uint32_t>(j["session_timeout_ms"], "session_timeout_ms");
        } else if (j.contains("session_timeout")) {
            // Also support "session_timeout" without _ms suffix
            sc.session_timeout_ms = parseJsonDuration<uint32_t>(j["session_timeout"], "session_timeout");
        }
        
        if (j.contains("worker_threads")) {
            sc.worker_threads = j["worker_threads"].get<uint32_t>();
        }
        
        if (j.contains("event_threads")) {
            sc.event_threads = j["event_threads"].get<uint32_t>();
        }
        
        if (j.contains("transports") && j["transports"].is_array()) {
            for (const auto& transport_json : j["transports"]) {
                sc.transports.push_back(TransportConfig::fromJson(transport_json));
            }
        }
        
        if (j.contains("filter_chains") && j["filter_chains"].is_array()) {
            for (const auto& chain_json : j["filter_chains"]) {
                sc.filter_chains.push_back(FilterChainConfig::fromJson(chain_json));
            }
        }
        
        return sc;
    }
    
    /**
     * @brief Merge another server configuration
     */
    void merge(const ServerConfig& other) {
        if (!other.name.empty() && other.name != "gopher-mcp-server") {
            name = other.name;
        }
        
        if (other.version != ConfigVersion(1, 0, 0)) {
            version = other.version;
        }
        
        // Merge capabilities
        if (!other.capabilities.features.empty()) {
            capabilities = other.capabilities;
        }
        
        if (other.max_sessions != 1000) {
            max_sessions = other.max_sessions;
        }
        
        if (other.session_timeout_ms != 300000) {
            session_timeout_ms = other.session_timeout_ms;
        }
        
        if (other.worker_threads != 0) {
            worker_threads = other.worker_threads;
        }
        
        if (other.event_threads != 1) {
            event_threads = other.event_threads;
        }
        
        // Replace transports and filter chains if provided
        if (!other.transports.empty()) {
            transports = other.transports;
        }
        
        if (!other.filter_chains.empty()) {
            filter_chains = other.filter_chains;
        }
    }
    
    bool operator==(const ServerConfig& other) const {
        return name == other.name &&
               version == other.version &&
               capabilities == other.capabilities &&
               max_sessions == other.max_sessions &&
               session_timeout_ms == other.session_timeout_ms &&
               worker_threads == other.worker_threads &&
               event_threads == other.event_threads &&
               transports == other.transports &&
               filter_chains == other.filter_chains;
    }
    
    bool operator!=(const ServerConfig& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Factory for creating default configurations
 */
class ConfigFactory {
public:
    /**
     * @brief Create default server configuration
     */
    static ServerConfig createDefaultServerConfig() {
        ServerConfig config;
        
        // Add default TCP transport
        TransportConfig tcp;
        tcp.type = "tcp";
        tcp.address = "127.0.0.1";
        tcp.port = 3333;
        tcp.filter_chain = "default";
        config.transports.push_back(tcp);
        
        // Add default filter chain
        FilterChainConfig chain;
        chain.name = "default";
        chain.transport_type = "tcp";
        
        // Add basic filters
        FilterConfig buffer_filter;
        buffer_filter.type = "buffer";
        buffer_filter.name = "request_buffer";
        buffer_filter.config["max_size"] = 1024 * 1024; // 1MB
        chain.filters.push_back(buffer_filter);
        
        config.filter_chains.push_back(chain);
        
        return config;
    }
    
    /**
     * @brief Create HTTP server configuration
     */
    static ServerConfig createHttpServerConfig() {
        ServerConfig config = createDefaultServerConfig();
        config.name = "gopher-mcp-http-server";
        
        // Replace with HTTP transport
        config.transports.clear();
        TransportConfig http;
        http.type = "http";
        http.address = "0.0.0.0";
        http.port = 8080;
        http.filter_chain = "http";
        config.transports.push_back(http);
        
        // Add HTTP filter chain
        FilterChainConfig http_chain;
        http_chain.name = "http";
        http_chain.transport_type = "http";
        
        // HTTP codec filter
        FilterConfig http_codec;
        http_codec.type = "http_codec";
        http_codec.name = "http_codec";
        http_codec.config["max_header_size"] = 8192;
        http_chain.filters.push_back(http_codec);
        
        // SSE codec filter
        FilterConfig sse_codec;
        sse_codec.type = "sse_codec";
        sse_codec.name = "sse_codec";
        http_chain.filters.push_back(sse_codec);
        
        config.filter_chains.push_back(http_chain);
        
        return config;
    }
    
    /**
     * @brief Create secure HTTPS server configuration
     */
    static ServerConfig createHttpsServerConfig() {
        ServerConfig config = createHttpServerConfig();
        config.name = "gopher-mcp-https-server";
        
        // Update transport to HTTPS with TLS
        config.transports[0].type = "https";
        config.transports[0].port = 8443;
        config.transports[0].tls.enabled = true;
        config.transports[0].tls.cert_file = "/etc/mcp/server.crt";
        config.transports[0].tls.key_file = "/etc/mcp/server.key";
        config.transports[0].tls.min_version = "1.2";
        
        return config;
    }
};

} // namespace config
} // namespace mcp