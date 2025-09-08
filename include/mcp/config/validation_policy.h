/**
 * @file validation_policy.h
 * @brief Configuration validation policies and unknown field handling
 *
 * This file defines policies for handling unknown fields and validation
 * strictness levels when parsing configuration files.
 */

#pragma once

#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "mcp/json/json_bridge.h"

namespace mcp {
namespace config {

/**
 * @brief Policy for handling unknown configuration fields
 */
enum class UnknownFieldPolicy {
  /// Throw an error when unknown fields are encountered
  STRICT,

  /// Log a warning for unknown fields but continue processing
  WARN,

  /// Silently ignore unknown fields
  PERMISSIVE
};

/**
 * @brief Validation context for tracking parsing state and policies
 */
class ValidationContext {
 public:
  using WarningHandler = std::function<void(const std::string&)>;

  ValidationContext(UnknownFieldPolicy policy = UnknownFieldPolicy::WARN)
      : unknown_field_policy_(policy),
        warning_handler_([](const std::string& msg) {
          std::cerr << "CONFIG WARNING: " << msg << std::endl;
        }) {}

  /**
   * @brief Set the unknown field policy
   */
  void setUnknownFieldPolicy(UnknownFieldPolicy policy) {
    unknown_field_policy_ = policy;
  }

  /**
   * @brief Get the current unknown field policy
   */
  UnknownFieldPolicy getUnknownFieldPolicy() const {
    return unknown_field_policy_;
  }

  /**
   * @brief Set a custom warning handler
   */
  void setWarningHandler(WarningHandler handler) { warning_handler_ = handler; }

  /**
   * @brief Report an unknown field
   * @param path The configuration path (e.g., "server.capabilities")
   * @param field The unknown field name
   * @throws ConfigValidationError if policy is STRICT
   */
  void reportUnknownField(const std::string& path, const std::string& field) {
    std::string full_path = path.empty() ? field : path + "." + field;
    unknown_fields_.push_back(full_path);

    switch (unknown_field_policy_) {
      case UnknownFieldPolicy::STRICT:
        throw std::runtime_error("Unknown configuration field: " + full_path);

      case UnknownFieldPolicy::WARN:
        if (warning_handler_) {
          warning_handler_("Unknown configuration field: " + full_path);
        }
        break;

      case UnknownFieldPolicy::PERMISSIVE:
        // Silently ignore
        break;
    }
  }

  /**
   * @brief Get all unknown fields encountered during parsing
   */
  const std::vector<std::string>& getUnknownFields() const {
    return unknown_fields_;
  }

  /**
   * @brief Clear the list of unknown fields
   */
  void clearUnknownFields() { unknown_fields_.clear(); }

  /**
   * @brief Check if any unknown fields were encountered
   */
  bool hasUnknownFields() const { return !unknown_fields_.empty(); }

 private:
  UnknownFieldPolicy unknown_field_policy_;
  WarningHandler warning_handler_;
  std::vector<std::string> unknown_fields_;
};

/**
 * @brief Helper to validate JSON object fields against known fields
 *
 * @param json The JSON object to validate
 * @param known_fields Set of known field names
 * @param path The configuration path for error reporting
 * @param ctx The validation context
 */
inline void validateJsonFields(const mcp::json::JsonValue& json,
                               const std::set<std::string>& known_fields,
                               const std::string& path,
                               ValidationContext& ctx) {
  if (!json.isObject()) {
    return;
  }

  for (const auto& key : json.keys()) {
    if (known_fields.find(key) == known_fields.end()) {
      ctx.reportUnknownField(path, key);
    }
  }
}

/**
 * @brief RAII helper to track configuration path during nested parsing
 */
class PathScope {
 public:
  PathScope(std::string& path, const std::string& segment)
      : path_(path), original_path_(path) {
    if (!path_.empty()) {
      path_ += ".";
    }
    path_ += segment;
  }

  ~PathScope() { path_ = original_path_; }

 private:
  std::string& path_;
  std::string original_path_;
};

/**
 * @brief Default validation context (can be replaced globally)
 */
inline ValidationContext& getDefaultValidationContext() {
  static ValidationContext default_context(UnknownFieldPolicy::WARN);
  return default_context;
}

/**
 * @brief Set the global unknown field policy
 */
inline void setGlobalUnknownFieldPolicy(UnknownFieldPolicy policy) {
  getDefaultValidationContext().setUnknownFieldPolicy(policy);
}

}  // namespace config
}  // namespace mcp