/**
 * @file config_types.h
 * @brief Core configuration data models with proper optional field support
 *
 * This file defines configuration structures with optional fields to properly
 * track field presence for deterministic merge operations.
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

#include <mcp/core/compat.h>  // For mcp::optional
#include "mcp/json/json_bridge.h"

namespace mcp {
namespace config {

// Forward declaration
class ConfigValidationError;

/**
 * @brief Configuration field wrapper to track presence
 *
 * This wrapper distinguishes between "not set" and "set to default value"
 */
template <typename T>
struct ConfigField {
  mcp::optional<T> value;
  T default_value;

  ConfigField() = default;
  explicit ConfigField(const T& def) : default_value(def) {}

  // Get the effective value (set value or default)
  const T& get() const { return value.value_or(default_value); }

  T& get() {
    if (!value.has_value()) {
      value = default_value;
    }
    return value.value();
  }

  // Set a value (marks as present)
  void set(const T& val) { value = val; }

  // Check if explicitly set
  bool is_set() const { return value.has_value(); }

  // Reset to unset state
  void reset() { value.reset(); }

  // Merge another field (if other is set, use its value)
  void merge(const ConfigField<T>& other) {
    if (other.is_set()) {
      value = other.value;
    }
  }

  // Operators for convenience
  ConfigField& operator=(const T& val) {
    set(val);
    return *this;
  }

  operator T() const { return get(); }

  bool operator==(const ConfigField& other) const {
    // Two fields are equal if they have the same effective value
    return get() == other.get();
  }

  bool operator!=(const ConfigField& other) const { return !(*this == other); }
};

/**
 * @brief Node configuration with optional field tracking
 */
struct NodeConfig {
  // Required fields (always have values)
  ConfigField<std::string> id{"gopher-mcp-node-1"};
  ConfigField<std::string> cluster{"default"};

  // Optional fields (may not be set)
  ConfigField<std::map<std::string, std::string>> metadata{{}};
  ConfigField<std::string> region{""};
  ConfigField<std::string> zone{""};

  void validate() const;
  mcp::json::JsonValue toJson() const;
  static NodeConfig fromJson(const mcp::json::JsonValue& j);
  void merge(const NodeConfig& other);

  bool operator==(const NodeConfig& other) const {
    return id == other.id && cluster == other.cluster &&
           metadata == other.metadata && region == other.region &&
           zone == other.zone;
  }
};

/**
 * @brief Admin interface configuration with optional field tracking
 */
struct AdminConfig {
  ConfigField<std::string> address{"127.0.0.1"};
  ConfigField<uint16_t> port{9901};
  ConfigField<std::vector<std::string>> allowed_ips{{"127.0.0.1", "::1"}};
  ConfigField<bool> enabled{true};
  ConfigField<std::string> path_prefix{"/admin"};
  ConfigField<bool> enable_cors{false};
  ConfigField<std::vector<std::string>> cors_origins{{"*"}};

  void validate() const;
  mcp::json::JsonValue toJson() const;
  static AdminConfig fromJson(const mcp::json::JsonValue& j);
  void merge(const AdminConfig& other);

  bool operator==(const AdminConfig& other) const {
    return address == other.address && port == other.port &&
           allowed_ips == other.allowed_ips && enabled == other.enabled &&
           path_prefix == other.path_prefix &&
           enable_cors == other.enable_cors &&
           cors_origins == other.cors_origins;
  }
};

/**
 * @brief Bootstrap configuration with optional field tracking
 */
struct BootstrapConfig {
  ConfigField<std::string> version{"1.0"};
  NodeConfig node;
  AdminConfig admin;
  ConfigField<std::string> config_path{""};

  void validate() const;
  mcp::json::JsonValue toJson() const;
  static BootstrapConfig fromJson(const mcp::json::JsonValue& j);
  void merge(const BootstrapConfig& other);

  bool operator==(const BootstrapConfig& other) const {
    return version == other.version && node == other.node &&
           admin == other.admin && config_path == other.config_path;
  }
};

// ============ Implementation ============

/**
 * @brief Configuration validation exception
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

// NodeConfig implementation
inline void NodeConfig::validate() const {
  const auto& id_val = id.get();
  if (id_val.empty()) {
    throw ConfigValidationError("node.id", "Node ID cannot be empty");
  }

  if (id_val.length() > 256) {
    throw ConfigValidationError("node.id",
                                "Node ID length cannot exceed 256 characters");
  }

  // Validate ID contains only valid characters
  for (char c : id_val) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
      throw ConfigValidationError("node.id",
                                  "Node ID can only contain alphanumeric "
                                  "characters, dashes, and underscores");
    }
  }

  const auto& cluster_val = cluster.get();
  if (cluster_val.empty()) {
    throw ConfigValidationError("node.cluster", "Cluster name cannot be empty");
  }

  if (cluster_val.length() > 128) {
    throw ConfigValidationError(
        "node.cluster", "Cluster name length cannot exceed 128 characters");
  }

  // Validate metadata if set
  if (metadata.is_set()) {
    for (const auto& kv : metadata.get()) {
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
}

inline mcp::json::JsonValue NodeConfig::toJson() const {
  auto j = mcp::json::JsonValue::object();

  // Always include id and cluster
  j["id"] = mcp::json::JsonValue(id.get());
  j["cluster"] = mcp::json::JsonValue(cluster.get());

  // Only include optional fields if they are set
  if (metadata.is_set() && !metadata.get().empty()) {
    auto meta_obj = mcp::json::JsonValue::object();
    for (const auto& pair : metadata.get()) {
      meta_obj[pair.first] = mcp::json::JsonValue(pair.second);
    }
    j["metadata"] = meta_obj;
  }

  if (region.is_set() && !region.get().empty()) {
    j["region"] = mcp::json::JsonValue(region.get());
  }

  if (zone.is_set() && !zone.get().empty()) {
    j["zone"] = mcp::json::JsonValue(zone.get());
  }

  return j;
}

inline NodeConfig NodeConfig::fromJson(const mcp::json::JsonValue& j) {
  NodeConfig config;

  if (j.contains("id")) {
    config.id.set(j["id"].getString());
  }

  if (j.contains("cluster")) {
    config.cluster.set(j["cluster"].getString());
  }

  if (j.contains("metadata") && j["metadata"].isObject()) {
    std::map<std::string, std::string> meta;
    for (const auto& key : j["metadata"].keys()) {
      meta[key] = j["metadata"][key].getString();
    }
    config.metadata.set(meta);
  }

  if (j.contains("region")) {
    config.region.set(j["region"].getString());
  }

  if (j.contains("zone")) {
    config.zone.set(j["zone"].getString());
  }

  return config;
}

inline void NodeConfig::merge(const NodeConfig& other) {
  id.merge(other.id);
  cluster.merge(other.cluster);
  metadata.merge(other.metadata);
  region.merge(other.region);
  zone.merge(other.zone);
}

// AdminConfig implementation
inline void AdminConfig::validate() const {
  const auto& addr = address.get();
  if (addr.empty()) {
    throw ConfigValidationError("admin.address", "Address cannot be empty");
  }

  if (port.get() == 0) {
    throw ConfigValidationError("admin.port", "Port cannot be 0");
  }

  const auto& prefix = path_prefix.get();
  if (!prefix.empty() && prefix[0] != '/') {
    throw ConfigValidationError("admin.path_prefix",
                                "Path prefix must start with '/' or be empty");
  }

  // Validate allowed IPs format (basic check)
  for (const auto& ip : allowed_ips.get()) {
    if (ip.empty()) {
      throw ConfigValidationError("admin.allowed_ips",
                                  "Empty IP address in allowed list");
    }
  }
}

inline mcp::json::JsonValue AdminConfig::toJson() const {
  auto j = mcp::json::JsonValue::object();

  j["address"] = mcp::json::JsonValue(address.get());
  j["port"] = mcp::json::JsonValue(static_cast<int>(port.get()));
  j["enabled"] = mcp::json::JsonValue(enabled.get());
  j["path_prefix"] = mcp::json::JsonValue(path_prefix.get());
  j["enable_cors"] = mcp::json::JsonValue(enable_cors.get());

  if (allowed_ips.is_set()) {
    auto ips_array = mcp::json::JsonValue::array();
    for (const auto& ip : allowed_ips.get()) {
      ips_array.push_back(mcp::json::JsonValue(ip));
    }
    j["allowed_ips"] = ips_array;
  }

  if (enable_cors.get() && cors_origins.is_set()) {
    auto origins_array = mcp::json::JsonValue::array();
    for (const auto& origin : cors_origins.get()) {
      origins_array.push_back(mcp::json::JsonValue(origin));
    }
    j["cors_origins"] = origins_array;
  }

  return j;
}

inline AdminConfig AdminConfig::fromJson(const mcp::json::JsonValue& j) {
  AdminConfig config;

  if (j.contains("address")) {
    config.address.set(j["address"].getString());
  }

  if (j.contains("port")) {
    config.port.set(static_cast<uint16_t>(j["port"].getInt()));
  }

  if (j.contains("allowed_ips") && j["allowed_ips"].isArray()) {
    std::vector<std::string> ips;
    for (size_t i = 0; i < j["allowed_ips"].size(); ++i) {
      ips.push_back(j["allowed_ips"][i].getString());
    }
    config.allowed_ips.set(ips);
  }

  if (j.contains("enabled")) {
    config.enabled.set(j["enabled"].getBool());
  }

  if (j.contains("path_prefix")) {
    config.path_prefix.set(j["path_prefix"].getString());
  }

  if (j.contains("enable_cors")) {
    config.enable_cors.set(j["enable_cors"].getBool());
  }

  if (j.contains("cors_origins") && j["cors_origins"].isArray()) {
    std::vector<std::string> origins;
    for (size_t i = 0; i < j["cors_origins"].size(); ++i) {
      origins.push_back(j["cors_origins"][i].getString());
    }
    config.cors_origins.set(origins);
  }

  return config;
}

inline void AdminConfig::merge(const AdminConfig& other) {
  address.merge(other.address);
  port.merge(other.port);
  allowed_ips.merge(other.allowed_ips);
  enabled.merge(other.enabled);
  path_prefix.merge(other.path_prefix);
  enable_cors.merge(other.enable_cors);
  cors_origins.merge(other.cors_origins);
}

// BootstrapConfig implementation
inline void BootstrapConfig::validate() const {
  const auto& ver = version.get();
  if (ver.empty()) {
    throw ConfigValidationError("version", "Version cannot be empty");
  }

  // Validate nested configurations
  try {
    node.validate();
  } catch (const ConfigValidationError& e) {
    throw ConfigValidationError("node." + e.field(), e.reason());
  }

  try {
    admin.validate();
  } catch (const ConfigValidationError& e) {
    throw ConfigValidationError("admin." + e.field(), e.reason());
  }
}

inline mcp::json::JsonValue BootstrapConfig::toJson() const {
  auto j = mcp::json::JsonValue::object();

  j["version"] = mcp::json::JsonValue(version.get());
  j["node"] = node.toJson();
  j["admin"] = admin.toJson();

  if (config_path.is_set() && !config_path.get().empty()) {
    j["config_path"] = mcp::json::JsonValue(config_path.get());
  }

  return j;
}

inline BootstrapConfig BootstrapConfig::fromJson(const mcp::json::JsonValue& j) {
  BootstrapConfig config;

  if (j.contains("version")) {
    config.version.set(j["version"].getString());
  }

  if (j.contains("node") && j["node"].isObject()) {
    config.node = NodeConfig::fromJson(j["node"]);
  }

  if (j.contains("admin") && j["admin"].isObject()) {
    config.admin = AdminConfig::fromJson(j["admin"]);
  }

  if (j.contains("config_path")) {
    config.config_path.set(j["config_path"].getString());
  }

  return config;
}

inline void BootstrapConfig::merge(const BootstrapConfig& other) {
  version.merge(other.version);
  node.merge(other.node);
  admin.merge(other.admin);
  config_path.merge(other.config_path);
}

}  // namespace config
}  // namespace mcp