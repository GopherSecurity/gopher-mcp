/**
 * @file types.h
 * @brief Core configuration data models for Gopher-MCP server
 *
 * This file defines the fundamental configuration structures used throughout
 * the MCP server for type-safe configuration access. These structures support
 * JSON serialization/deserialization and provide validation capabilities.
 */

#pragma once

#include <cctype>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mcp/json/json_bridge.h"
#include "mcp/config/units.h"  // For unit parsing

// Helper macro for safe JSON field extraction with error context
#define SAFE_GET_JSON_FIELD(json, field_name, target, error_path)          \
  do {                                                                     \
    try {                                                                  \
      if (json.contains(field_name)) {                                    \
        target = extractJsonValue<decltype(target)>(json[field_name]);     \
      }                                                                    \
    } catch (const mcp::json::JsonException& e) {                          \
      throw ConfigValidationError(error_path,                              \
                                  "Type error: " + std::string(e.what())); \
    }                                                                      \
  } while (0)

namespace mcp {
namespace config {

// Helper template to extract values from JsonValue
template<typename T>
T extractJsonValue(const mcp::json::JsonValue& value);

template<>
inline std::string extractJsonValue<std::string>(const mcp::json::JsonValue& value) {
  return value.getString();
}

template<>
inline int extractJsonValue<int>(const mcp::json::JsonValue& value) {
  return value.getInt();
}

template<>
inline int64_t extractJsonValue<int64_t>(const mcp::json::JsonValue& value) {
  return value.getInt64();
}

template<>
inline uint32_t extractJsonValue<uint32_t>(const mcp::json::JsonValue& value) {
  return static_cast<uint32_t>(value.getInt64());
}

template<>
inline size_t extractJsonValue<size_t>(const mcp::json::JsonValue& value) {
  return static_cast<size_t>(value.getInt64());
}

template<>
inline bool extractJsonValue<bool>(const mcp::json::JsonValue& value) {
  return value.getBool();
}

template<>
inline double extractJsonValue<double>(const mcp::json::JsonValue& value) {
  return value.getFloat();
}

// Forward declarations for vector and object extractors that depend on full definitions
struct FilterConfig;
struct TransportConfig;
struct FilterChainConfig;

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
  static std::string formatError(const std::string& field,
                                 const std::string& reason) {
    std::ostringstream oss;
    oss << "Configuration validation failed for field '" << field
        << "': " << reason;
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
      throw ConfigValidationError(
          "node.id", "Node ID length cannot exceed 256 characters");
    }

    // Validate ID contains only valid characters (alphanumeric, dash,
    // underscore)
    for (char c : id) {
      if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' &&
          c != '_') {
        throw ConfigValidationError("node.id",
                                    "Node ID can only contain alphanumeric "
                                    "characters, dashes, and underscores");
      }
    }

    if (cluster.empty()) {
      throw ConfigValidationError("node.cluster",
                                  "Cluster name cannot be empty");
    }

    if (cluster.length() > 128) {
      throw ConfigValidationError(
          "node.cluster", "Cluster name length cannot exceed 128 characters");
    }

    // Validate metadata keys
    for (const auto& kv : metadata) {
      if (kv.first.empty()) {
        throw ConfigValidationError("node.metadata",
                                    "Metadata keys cannot be empty");
      }
      if (kv.first.length() > 128) {
        throw ConfigValidationError(
            "node.metadata",
            "Metadata key '" + kv.first + "' exceeds maximum length of 128");
      }
      if (kv.second.length() > 512) {
        throw ConfigValidationError("node.metadata",
                                    "Metadata value for key '" + kv.first +
                                        "' exceeds maximum length of 512");
      }
    }
  }

  /**
   * @brief Convert to JSON
   */
  mcp::json::JsonValue toJson() const {
    mcp::json::JsonObjectBuilder builder;
    builder.add("id", id);
    builder.add("cluster", cluster);

    if (!metadata.empty()) {
      auto metaObj = mcp::json::JsonValue::object();
      for (const auto& [key, value] : metadata) {
        metaObj[key] = mcp::json::JsonValue(value);
      }
      builder.add("metadata", metaObj);
    }
    if (!region.empty()) {
      builder.add("region", region);
    }
    if (!zone.empty()) {
      builder.add("zone", zone);
    }

    return builder.build();
  }

  /**
   * @brief Create from JSON
   */
  static NodeConfig fromJson(const mcp::json::JsonValue& j) {
    NodeConfig config;

    if (j.contains("id") && !j["id"].isNull()) {
      try {
        config.id = j["id"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("node.id",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("cluster") && !j["cluster"].isNull()) {
      try {
        config.cluster = j["cluster"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("node.cluster",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("metadata") && j["metadata"].isObject()) {
      try {
        std::map<std::string, std::string> meta;
        for (const auto& key : j["metadata"].keys()) {
          meta[key] = j["metadata"][key].getString();
        }
        config.metadata = meta;
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("node.metadata",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("region") && !j["region"].isNull()) {
      try {
        config.region = j["region"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("node.region",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("zone") && !j["zone"].isNull()) {
      try {
        config.zone = j["zone"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("node.zone",
                                    "Type error: " + std::string(e.what()));
      }
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
      return;  // Skip validation if admin interface is disabled
    }

    if (address.empty()) {
      throw ConfigValidationError(
          "admin.address",
          "Admin address cannot be empty when admin interface is enabled");
    }

    // Basic IP address validation (simplified)
    if (address != "0.0.0.0" && address != "127.0.0.1" && address != "::1" &&
        address != "::") {
      // For other addresses, check basic format
      if (address.find_first_not_of("0123456789.:abcdefABCDEF") !=
          std::string::npos) {
        throw ConfigValidationError(
            "admin.address", "Invalid characters in admin address: " + address);
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
      throw ConfigValidationError(
          "admin.allowed_ips",
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
      throw ConfigValidationError(
          "admin.cors_origins",
          "CORS origins must be specified when CORS is enabled");
    }
  }

  /**
   * @brief Convert to JSON
   */
  mcp::json::JsonValue toJson() const {
    mcp::json::JsonObjectBuilder builder;
    builder.add("address", address);
    builder.add("port", mcp::json::JsonValue(static_cast<int>(port)));
    builder.add("enabled", enabled);
    builder.add("path_prefix", path_prefix);
    builder.add("enable_cors", enable_cors);

    // Add allowed_ips array
    if (!allowed_ips.empty()) {
      mcp::json::JsonArrayBuilder arr;
      for (const auto& ip : allowed_ips) {
        arr.add(ip);
      }
      builder.add("allowed_ips", arr.build());
    }

    // Add cors_origins array
    if (enable_cors && !cors_origins.empty()) {
      mcp::json::JsonArrayBuilder arr;
      for (const auto& origin : cors_origins) {
        arr.add(origin);
      }
      builder.add("cors_origins", arr.build());
    }

    return builder.build();
  }

  /**
   * @brief Create from JSON
   */
  static AdminConfig fromJson(const mcp::json::JsonValue& j) {
    AdminConfig config;

    if (j.contains("address") && !j["address"].isNull()) {
      try {
        config.address = j["address"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("admin.address",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("port") && !j["port"].isNull()) {
      try {
        config.port = static_cast<uint16_t>(j["port"].getInt());
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("admin.port",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("allowed_ips") && j["allowed_ips"].isArray()) {
      try {
        std::vector<std::string> ips;
        for (size_t i = 0; i < j["allowed_ips"].size(); ++i) {
          ips.push_back(j["allowed_ips"][i].getString());
        }
        config.allowed_ips = ips;
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("admin.allowed_ips",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("enabled") && !j["enabled"].isNull()) {
      try {
        config.enabled = j["enabled"].getBool();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("admin.enabled",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("path_prefix") && !j["path_prefix"].isNull()) {
      try {
        config.path_prefix = j["path_prefix"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("admin.path_prefix",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("enable_cors") && !j["enable_cors"].isNull()) {
      try {
        config.enable_cors = j["enable_cors"].getBool();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("admin.enable_cors",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("cors_origins") && j["cors_origins"].isArray()) {
      try {
        std::vector<std::string> origins;
        for (size_t i = 0; i < j["cors_origins"].size(); ++i) {
          origins.push_back(j["cors_origins"][i].getString());
        }
        config.cors_origins = origins;
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("admin.cors_origins",
                                    "Type error: " + std::string(e.what()));
      }
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
      throw ConfigValidationError("version",
                                  "Configuration version cannot be empty");
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
        if (i > 0 && version[i - 1] == '.') {
          valid_version = false;
          break;
        }
      } else if (!std::isdigit(static_cast<unsigned char>(c))) {
        valid_version = false;
        break;
      }
    }

    if (!valid_version || dot_count > 2) {
      throw ConfigValidationError("version",
                                  "Invalid version format. Expected format: "
                                  "X.Y or X.Y.Z where X,Y,Z are numbers");
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
  mcp::json::JsonValue toJson() const {
    mcp::json::JsonObjectBuilder builder;
    builder.add("version", version);
    builder.add("node", node.toJson());
    builder.add("admin", admin.toJson());

    if (!config_path.empty()) {
      builder.add("_config_path", config_path);
    }

    // Convert timestamp to ISO string
    if (loaded_at.time_since_epoch().count() > 0) {
      auto time_t = std::chrono::system_clock::to_time_t(loaded_at);
      char buffer[100];
      std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ",
                    std::gmtime(&time_t));
      builder.add("_loaded_at", std::string(buffer));
    }

    return builder.build();
  }

  /**
   * @brief Create from JSON
   */
  static BootstrapConfig fromJson(const mcp::json::JsonValue& j) {
    BootstrapConfig config;

    if (j.contains("version") && !j["version"].isNull()) {
      try {
        config.version = j["version"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("bootstrap.version",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("node") && j["node"].isObject()) {
      try {
        config.node = NodeConfig::fromJson(j["node"]);
      } catch (const ConfigValidationError& e) {
        throw ConfigValidationError("bootstrap.node." + e.field(), e.reason());
      } catch (const std::exception& e) {
        throw ConfigValidationError("bootstrap.node",
                                    "Parse error: " + std::string(e.what()));
      }
    }

    if (j.contains("admin") && j["admin"].isObject()) {
      try {
        config.admin = AdminConfig::fromJson(j["admin"]);
      } catch (const ConfigValidationError& e) {
        throw ConfigValidationError("bootstrap.admin." + e.field(), e.reason());
      } catch (const std::exception& e) {
        throw ConfigValidationError("bootstrap.admin",
                                    "Parse error: " + std::string(e.what()));
      }
    }

    if (j.contains("_config_path") && !j["_config_path"].isNull()) {
      try {
        config.config_path = j["_config_path"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("bootstrap._config_path",
                                    "Type error: " + std::string(e.what()));
      }
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
   * Used for configuration overlays and updates. Later values override earlier
   * ones.
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
        other.admin.allowed_ips !=
            std::vector<std::string>{"127.0.0.1", "::1"}) {
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
    return version == other.version && node.id == other.node.id &&
           node.cluster == other.node.cluster &&
           node.metadata == other.node.metadata &&
           node.region == other.node.region && node.zone == other.node.zone &&
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
      throw ConfigValidationError("version",
                                  "Invalid version format: " + version);
    }

    if (iss >> dot2 >> patch) {
      if (dot2 != '.') {
        throw ConfigValidationError("version",
                                    "Invalid version format: " + version);
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
    return major_ == other.major_ && minor_ == other.minor_ &&
           patch_ == other.patch_;
  }

  bool operator!=(const ConfigVersion& other) const {
    return !(*this == other);
  }

  bool operator<(const ConfigVersion& other) const {
    if (major_ != other.major_)
      return major_ < other.major_;
    if (minor_ != other.minor_)
      return minor_ < other.minor_;
    return patch_ < other.patch_;
  }

  bool operator<=(const ConfigVersion& other) const {
    return (*this < other) || (*this == other);
  }

  bool operator>(const ConfigVersion& other) const { return !(*this <= other); }

  bool operator>=(const ConfigVersion& other) const { return !(*this < other); }

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
  mcp::json::JsonValue config = mcp::json::JsonValue::object();

  /// Whether this filter is enabled
  bool enabled = true;
  
  /// Conditional enablement predicates (evaluated at build time)
  mcp::json::JsonValue enabled_when = mcp::json::JsonValue::object();

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
  mcp::json::JsonValue toJson() const {
    mcp::json::JsonObjectBuilder builder;
    builder.add("type", type);
    builder.add("name", name);
    builder.add("config", config);
    builder.add("enabled", enabled);
    if (!enabled_when.isNull() && !enabled_when.isEmpty()) {
      builder.add("enabled_when", enabled_when);
    }
    return builder.build();
  }

  /**
   * @brief Create from JSON
   */
  static FilterConfig fromJson(const mcp::json::JsonValue& j) {
    FilterConfig fc;

    if (j.contains("type")) {
      try {
        fc.type = j["type"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("filter.type",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("name")) {
      try {
        fc.name = j["name"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("filter.name",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("config")) {
      fc.config = j["config"];
    }

    if (j.contains("enabled")) {
      try {
        fc.enabled = j["enabled"].getBool();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("filter.enabled",
                                    "Type error: " + std::string(e.what()));
      }
    }
    
    if (j.contains("enabled_when")) {
      fc.enabled_when = j["enabled_when"];
    }

    return fc;
  }

  bool operator==(const FilterConfig& other) const {
    return type == other.type && name == other.name && config.toString() == other.config.toString() &&
           enabled == other.enabled;
  }

  bool operator!=(const FilterConfig& other) const { return !(*this == other); }
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
  
  /// Route-level filter bypass configuration
  mcp::json::JsonValue bypass_filters = mcp::json::JsonValue::null();

  /**
   * @brief Validate the filter chain configuration
   */
  void validate() const {
    if (name.empty()) {
      throw ConfigValidationError("filter_chain.name",
                                  "Filter chain name cannot be empty");
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
  mcp::json::JsonValue toJson() const {
    mcp::json::JsonObjectBuilder builder;
    builder.add("name", name);
    builder.add("transport_type", transport_type);
    
    mcp::json::JsonArrayBuilder filters_arr;
    for (const auto& filter : filters) {
      filters_arr.add(filter.toJson());
    }
    builder.add("filters", filters_arr.build());

    return builder.build();
  }

  /**
   * @brief Create from JSON
   */
  static FilterChainConfig fromJson(const mcp::json::JsonValue& j) {
    FilterChainConfig fcc;

    if (j.contains("name")) {
      try {
        fcc.name = j["name"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("filter_chain.name",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("transport_type")) {
      try {
        fcc.transport_type = j["transport_type"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("filter_chain.transport_type",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("filters") && j["filters"].isArray()) {
      size_t idx = 0;
      for (size_t i = 0; i < j["filters"].size(); ++i) {
        try {
          fcc.filters.push_back(FilterConfig::fromJson(j["filters"][i]));
        } catch (const ConfigValidationError& e) {
          throw ConfigValidationError(
              "filter_chain.filters[" + std::to_string(idx) + "]." + e.field(),
              e.reason());
        } catch (const std::exception& e) {
          throw ConfigValidationError(
              "filter_chain.filters[" + std::to_string(idx) + "]",
              "Parse error: " + std::string(e.what()));
        }
        idx++;
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
    return name == other.name && transport_type == other.transport_type &&
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
        throw ConfigValidationError(
            "tls.cert_file",
            "Certificate file is required when TLS is enabled");
      }

      if (key_file.empty()) {
        throw ConfigValidationError(
            "tls.key_file", "Private key file is required when TLS is enabled");
      }

      if (verify_client && ca_file.empty()) {
        throw ConfigValidationError(
            "tls.ca_file",
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
  mcp::json::JsonValue toJson() const {
    mcp::json::JsonObjectBuilder builder;
    builder.add("enabled", enabled);
    builder.add("cert_file", cert_file);
    builder.add("key_file", key_file);
    builder.add("ca_file", ca_file);
    builder.add("verify_client", verify_client);
    builder.add("min_version", min_version);
    
    mcp::json::JsonArrayBuilder suites_arr;
    for (const auto& suite : cipher_suites) {
      suites_arr.add(suite);
    }
    builder.add("cipher_suites", suites_arr.build());
    
    return builder.build();
  }

  /**
   * @brief Create from JSON
   */
  static TLSConfig fromJson(const mcp::json::JsonValue& j) {
    TLSConfig tls;

    if (j.contains("enabled")) {
      try {
        tls.enabled = j["enabled"].getBool();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("tls.enabled",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("cert_file")) {
      try {
        tls.cert_file = j["cert_file"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("tls.cert_file",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("key_file")) {
      try {
        tls.key_file = j["key_file"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("tls.key_file",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("ca_file")) {
      try {
        tls.ca_file = j["ca_file"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("tls.ca_file",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("verify_client")) {
      try {
        tls.verify_client = j["verify_client"].getBool();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("tls.verify_client",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("min_version")) {
      try {
        tls.min_version = j["min_version"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("tls.min_version",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("cipher_suites") && j["cipher_suites"].isArray()) {
      try {
        std::vector<std::string> suites;
        for (size_t i = 0; i < j["cipher_suites"].size(); ++i) {
          suites.push_back(j["cipher_suites"][i].getString());
        }
        tls.cipher_suites = suites;
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("tls.cipher_suites",
                                    "Type error: " + std::string(e.what()));
      }
    }

    return tls;
  }

  bool operator==(const TLSConfig& other) const {
    return enabled == other.enabled && cert_file == other.cert_file &&
           key_file == other.key_file && ca_file == other.ca_file &&
           verify_client == other.verify_client &&
           min_version == other.min_version &&
           cipher_suites == other.cipher_suites;
  }

  bool operator!=(const TLSConfig& other) const { return !(*this == other); }
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
      throw ConfigValidationError("transport.type",
                                  "Transport type cannot be empty");
    }

    // Validate network transports
    if (type == "tcp" || type == "http" || type == "https") {
      if (address.empty()) {
        throw ConfigValidationError(
            "transport.address", "Address is required for network transport");
      }

      if (port == 0) {
        throw ConfigValidationError(
            "transport.port", "Valid port is required for network transport");
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
  mcp::json::JsonValue toJson() const {
    mcp::json::JsonObjectBuilder builder;
    builder.add("type", type);
    builder.add("address", address);
    builder.add("port", mcp::json::JsonValue(static_cast<int>(port)));
    builder.add("tls", tls.toJson());
    builder.add("filter_chain", filter_chain);
    builder.add("enabled", enabled);
    return builder.build();
  }

  /**
   * @brief Create from JSON
   */
  static TransportConfig fromJson(const mcp::json::JsonValue& j) {
    TransportConfig tc;

    if (j.contains("type")) {
      try {
        tc.type = j["type"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("transport.type",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("address")) {
      try {
        tc.address = j["address"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("transport.address",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("port")) {
      try {
        tc.port = static_cast<uint16_t>(j["port"].getInt());
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("transport.port",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("tls")) {
      try {
        tc.tls = TLSConfig::fromJson(j["tls"]);
      } catch (const ConfigValidationError& e) {
        throw ConfigValidationError("transport.tls." + e.field(), e.reason());
      } catch (const std::exception& e) {
        throw ConfigValidationError("transport.tls",
                                    "Parse error: " + std::string(e.what()));
      }
    }

    if (j.contains("filter_chain")) {
      try {
        tc.filter_chain = j["filter_chain"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("transport.filter_chain",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("enabled")) {
      try {
        tc.enabled = j["enabled"].getBool();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("transport.enabled",
                                    "Type error: " + std::string(e.what()));
      }
    }

    return tc;
  }

  bool operator==(const TransportConfig& other) const {
    return type == other.type && address == other.address &&
           port == other.port && tls == other.tls &&
           filter_chain == other.filter_chain && enabled == other.enabled;
  }

  bool operator!=(const TransportConfig& other) const {
    return !(*this == other);
  }
};

// =====================
// JsonValue extractors
// =====================

// Extract single objects (now that types are complete)
template<>
inline FilterConfig extractJsonValue<FilterConfig>(const mcp::json::JsonValue& value) {
  return FilterConfig::fromJson(value);
}

template<>
inline TransportConfig extractJsonValue<TransportConfig>(const mcp::json::JsonValue& value) {
  return TransportConfig::fromJson(value);
}

template<>
inline FilterChainConfig extractJsonValue<FilterChainConfig>(const mcp::json::JsonValue& value) {
  return FilterChainConfig::fromJson(value);
}

// Extract vectors
template<>
inline std::vector<std::string> extractJsonValue<std::vector<std::string>>(const mcp::json::JsonValue& value) {
  std::vector<std::string> result;
  if (value.isArray()) {
    for (size_t i = 0; i < value.size(); ++i) {
      result.push_back(extractJsonValue<std::string>(value[i]));
    }
  }
  return result;
}

template<>
inline std::vector<FilterConfig> extractJsonValue<std::vector<FilterConfig>>(const mcp::json::JsonValue& value) {
  std::vector<FilterConfig> result;
  if (value.isArray()) {
    for (size_t i = 0; i < value.size(); ++i) {
      result.push_back(extractJsonValue<FilterConfig>(value[i]));
    }
  }
  return result;
}

template<>
inline std::vector<TransportConfig> extractJsonValue<std::vector<TransportConfig>>(const mcp::json::JsonValue& value) {
  std::vector<TransportConfig> result;
  if (value.isArray()) {
    for (size_t i = 0; i < value.size(); ++i) {
      result.push_back(extractJsonValue<TransportConfig>(value[i]));
    }
  }
  return result;
}

template<>
inline std::vector<FilterChainConfig> extractJsonValue<std::vector<FilterChainConfig>>(const mcp::json::JsonValue& value) {
  std::vector<FilterChainConfig> result;
  if (value.isArray()) {
    for (size_t i = 0; i < value.size(); ++i) {
      result.push_back(extractJsonValue<FilterChainConfig>(value[i]));
    }
  }
  return result;
}

/**
 * @brief MCP protocol capabilities configuration
 */
struct CapabilitiesConfig {
  /// Supported MCP protocol features
  std::vector<std::string> features = {"tools", "prompts", "resources"};

  /// Maximum request size in bytes
  size_t max_request_size = 10 * 1024 * 1024;  // 10MB

  /// Maximum response size in bytes
  size_t max_response_size = 10 * 1024 * 1024;  // 10MB

  /// Request timeout in milliseconds
  uint32_t request_timeout_ms = 30000;  // 30 seconds

  /**
   * @brief Convert to JSON
   */
  mcp::json::JsonValue toJson() const {
    mcp::json::JsonObjectBuilder builder;
    
    mcp::json::JsonArrayBuilder features_arr;
    for (const auto& feature : features) {
      features_arr.add(feature);
    }
    builder.add("features", features_arr.build());
    
    builder.add("max_request_size", mcp::json::JsonValue(static_cast<int64_t>(max_request_size)));
    builder.add("max_response_size", mcp::json::JsonValue(static_cast<int64_t>(max_response_size)));
    builder.add("request_timeout_ms", mcp::json::JsonValue(static_cast<int>(request_timeout_ms)));
    return builder.build();
  }

  /**
   * @brief Create from JSON
   */
  static CapabilitiesConfig fromJson(const mcp::json::JsonValue& j) {
    CapabilitiesConfig cc;

    if (j.contains("features") && j["features"].isArray()) {
      try {
        std::vector<std::string> features;
        for (size_t i = 0; i < j["features"].size(); ++i) {
          features.push_back(j["features"][i].getString());
        }
        cc.features = features;
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("capabilities.features",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("max_request_size")) {
      try {
        cc.max_request_size =
            parseJsonSize<size_t>(j["max_request_size"], "max_request_size");
      } catch (const UnitParseError& e) {
        throw ConfigValidationError("capabilities.max_request_size", e.what());
      } catch (const std::exception& e) {
        throw ConfigValidationError("capabilities.max_request_size",
                                    "Parse error: " + std::string(e.what()));
      }
    }

    if (j.contains("max_response_size")) {
      try {
        cc.max_response_size =
            parseJsonSize<size_t>(j["max_response_size"], "max_response_size");
      } catch (const UnitParseError& e) {
        throw ConfigValidationError("capabilities.max_response_size", e.what());
      } catch (const std::exception& e) {
        throw ConfigValidationError("capabilities.max_response_size",
                                    "Parse error: " + std::string(e.what()));
      }
    }

    if (j.contains("request_timeout_ms")) {
      try {
        cc.request_timeout_ms = parseJsonDuration<uint32_t>(
            j["request_timeout_ms"], "request_timeout_ms");
      } catch (const UnitParseError& e) {
        throw ConfigValidationError("capabilities.request_timeout_ms",
                                    e.what());
      } catch (const std::exception& e) {
        throw ConfigValidationError("capabilities.request_timeout_ms",
                                    "Parse error: " + std::string(e.what()));
      }
    } else if (j.contains("request_timeout")) {
      // Also support "request_timeout" without _ms suffix
      try {
        cc.request_timeout_ms = parseJsonDuration<uint32_t>(
            j["request_timeout"], "request_timeout");
      } catch (const UnitParseError& e) {
        throw ConfigValidationError("capabilities.request_timeout", e.what());
      } catch (const std::exception& e) {
        throw ConfigValidationError("capabilities.request_timeout",
                                    "Parse error: " + std::string(e.what()));
      }
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
  uint32_t session_timeout_ms = 300000;  // 5 minutes

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
            "Transport references non-existent filter chain: " +
                filter_chain_ref);
      }
    }
  }

  /**
   * @brief Convert to JSON
   */
  mcp::json::JsonValue toJson() const {
    mcp::json::JsonObjectBuilder builder;
    builder.add("name", name);
    builder.add("version", version.toString());
    builder.add("capabilities", capabilities.toJson());
    builder.add("max_sessions", mcp::json::JsonValue(static_cast<int>(max_sessions)));
    builder.add("session_timeout_ms", mcp::json::JsonValue(static_cast<int>(session_timeout_ms)));
    builder.add("worker_threads", mcp::json::JsonValue(static_cast<int>(worker_threads)));
    builder.add("event_threads", mcp::json::JsonValue(static_cast<int>(event_threads)));

    mcp::json::JsonArrayBuilder transports_arr;
    for (const auto& transport : transports) {
      transports_arr.add(transport.toJson());
    }
    builder.add("transports", transports_arr.build());

    mcp::json::JsonArrayBuilder chains_arr;
    for (const auto& chain : filter_chains) {
      chains_arr.add(chain.toJson());
    }
    builder.add("filter_chains", chains_arr.build());

    return builder.build();
  }

  /**
   * @brief Create from JSON
   */
  static ServerConfig fromJson(const mcp::json::JsonValue& j) {
    ServerConfig sc;

    if (j.contains("name")) {
      try {
        sc.name = j["name"].getString();
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("server.name",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("version")) {
      try {
        std::string ver_str = j["version"].getString();
        sc.version = ConfigVersion::parse(ver_str);
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("server.version",
                                    "Type error: " + std::string(e.what()));
      } catch (const ConfigValidationError& e) {
        throw ConfigValidationError("server.version", e.reason());
      }
    }

    if (j.contains("capabilities")) {
      try {
        sc.capabilities = CapabilitiesConfig::fromJson(j["capabilities"]);
      } catch (const ConfigValidationError& e) {
        throw ConfigValidationError("server.capabilities." + e.field(),
                                    e.reason());
      } catch (const std::exception& e) {
        throw ConfigValidationError("server.capabilities",
                                    "Parse error: " + std::string(e.what()));
      }
    }

    if (j.contains("max_sessions")) {
      try {
        sc.max_sessions = static_cast<uint32_t>(j["max_sessions"].getInt());
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("server.max_sessions",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("session_timeout_ms")) {
      try {
        sc.session_timeout_ms = parseJsonDuration<uint32_t>(
            j["session_timeout_ms"], "session_timeout_ms");
      } catch (const UnitParseError& e) {
        throw ConfigValidationError("server.session_timeout_ms", e.what());
      } catch (const std::exception& e) {
        throw ConfigValidationError("server.session_timeout_ms",
                                    "Parse error: " + std::string(e.what()));
      }
    } else if (j.contains("session_timeout")) {
      // Also support "session_timeout" without _ms suffix
      try {
        sc.session_timeout_ms = parseJsonDuration<uint32_t>(
            j["session_timeout"], "session_timeout");
      } catch (const UnitParseError& e) {
        throw ConfigValidationError("server.session_timeout", e.what());
      } catch (const std::exception& e) {
        throw ConfigValidationError("server.session_timeout",
                                    "Parse error: " + std::string(e.what()));
      }
    }

    if (j.contains("worker_threads")) {
      try {
        sc.worker_threads = static_cast<uint32_t>(j["worker_threads"].getInt());
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("server.worker_threads",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("event_threads")) {
      try {
        sc.event_threads = static_cast<uint32_t>(j["event_threads"].getInt());
      } catch (const mcp::json::JsonException& e) {
        throw ConfigValidationError("server.event_threads",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("transports") && j["transports"].isArray()) {
      for (size_t idx = 0; idx < j["transports"].size(); ++idx) {
        try {
          sc.transports.push_back(TransportConfig::fromJson(j["transports"][idx]));
        } catch (const ConfigValidationError& e) {
          throw ConfigValidationError(
              "server.transports[" + std::to_string(idx) + "]." + e.field(),
              e.reason());
        } catch (const std::exception& e) {
          throw ConfigValidationError(
              "server.transports[" + std::to_string(idx) + "]",
              "Parse error: " + std::string(e.what()));
        }
      }
    }

    if (j.contains("filter_chains") && j["filter_chains"].isArray()) {
      for (size_t idx = 0; idx < j["filter_chains"].size(); ++idx) {
        try {
          sc.filter_chains.push_back(FilterChainConfig::fromJson(j["filter_chains"][idx]));
        } catch (const ConfigValidationError& e) {
          throw ConfigValidationError(
              "server.filter_chains[" + std::to_string(idx) + "]." + e.field(),
              e.reason());
        } catch (const std::exception& e) {
          throw ConfigValidationError(
              "server.filter_chains[" + std::to_string(idx) + "]",
              "Parse error: " + std::string(e.what()));
        }
        idx++;
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
    return name == other.name && version == other.version &&
           capabilities == other.capabilities &&
           max_sessions == other.max_sessions &&
           session_timeout_ms == other.session_timeout_ms &&
           worker_threads == other.worker_threads &&
           event_threads == other.event_threads &&
           transports == other.transports &&
           filter_chains == other.filter_chains;
  }

  bool operator!=(const ServerConfig& other) const { return !(*this == other); }
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
    buffer_filter.config["max_size"] = 1024 * 1024;  // 1MB
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

}  // namespace config
}  // namespace mcp
