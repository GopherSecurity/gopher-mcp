/**
 * @file json_conversion.h
 * @brief Conversion utilities between nlohmann::json and JsonValue
 *
 * These conversion functions are isolated here to be used ONLY at system
 * boundaries where we need to interface with external libraries that require
 * nlohmann::json (like yaml-cpp parsing or json-schema-validator).
 * 
 * The rest of the codebase should use JsonValue exclusively.
 */

#pragma once

#include <nlohmann/json.hpp>
#include "mcp/json/json_bridge.h"

namespace mcp {
namespace config {

/**
 * @brief Convert nlohmann::json to JsonValue
 * 
 * Used at system boundaries where we receive nlohmann::json from external
 * libraries (e.g., YAML parsing).
 */
inline mcp::json::JsonValue fromNlohmann(const nlohmann::json& nj) {
  if (nj.is_null()) {
    return mcp::json::JsonValue::null();
  } else if (nj.is_boolean()) {
    return mcp::json::JsonValue(nj.get<bool>());
  } else if (nj.is_number_integer()) {
    return mcp::json::JsonValue(nj.get<int64_t>());
  } else if (nj.is_number_float()) {
    return mcp::json::JsonValue(nj.get<double>());
  } else if (nj.is_string()) {
    return mcp::json::JsonValue(nj.get<std::string>());
  } else if (nj.is_array()) {
    auto arr = mcp::json::JsonValue::array();
    for (const auto& item : nj) {
      arr.push_back(fromNlohmann(item));
    }
    return arr;
  } else if (nj.is_object()) {
    auto obj = mcp::json::JsonValue::object();
    for (auto it = nj.begin(); it != nj.end(); ++it) {
      obj[it.key()] = fromNlohmann(it.value());
    }
    return obj;
  }
  return mcp::json::JsonValue::null();
}

/**
 * @brief Convert JsonValue to nlohmann::json
 * 
 * Used at system boundaries where we need to pass nlohmann::json to external
 * libraries (e.g., json-schema-validator).
 */
inline nlohmann::json toNlohmann(const mcp::json::JsonValue& jv) {
  if (jv.isNull()) {
    return nullptr;
  } else if (jv.isBoolean()) {
    return jv.getBool();
  } else if (jv.isInteger()) {
    return jv.getInt64();
  } else if (jv.isFloat()) {
    return jv.getFloat();
  } else if (jv.isString()) {
    return jv.getString();
  } else if (jv.isArray()) {
    nlohmann::json arr = nlohmann::json::array();
    for (size_t i = 0; i < jv.size(); ++i) {
      arr.push_back(toNlohmann(jv[i]));
    }
    return arr;
  } else if (jv.isObject()) {
    nlohmann::json obj = nlohmann::json::object();
    for (const auto& key : jv.keys()) {
      obj[key] = toNlohmann(jv[key]);
    }
    return obj;
  }
  return nullptr;
}

}  // namespace config
}  // namespace mcp