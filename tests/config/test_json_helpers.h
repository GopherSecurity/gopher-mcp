/**
 * @file test_json_helpers.h
 * @brief Helper functions for JSON construction in tests
 */

#pragma once

#include <initializer_list>
#include <string>
#include <utility>

#include "mcp/json/json_bridge.h"

namespace mcp {
namespace config {
namespace test {

/**
 * @brief Create JSON object from initializer list
 */
inline mcp::json::JsonValue makeJsonObject(
    std::initializer_list<std::pair<std::string, mcp::json::JsonValue>> items) {
  auto obj = mcp::json::JsonValue::object();
  for (const auto& item : items) {
    // Use set() to reliably assign into the underlying JSON object
    obj.set(item.first, item.second);
  }
  return obj;
}

/**
 * @brief Create JSON array from initializer list
 */
inline mcp::json::JsonValue makeJsonArray(
    std::initializer_list<mcp::json::JsonValue> items) {
  auto arr = mcp::json::JsonValue::array();
  for (const auto& item : items) {
    arr.push_back(item);
  }
  return arr;
}

/**
 * @brief Helper to create string JsonValue
 */
inline mcp::json::JsonValue str(const std::string& s) {
  return mcp::json::JsonValue(s);
}

/**
 * @brief Helper to create int JsonValue
 */
inline mcp::json::JsonValue num(int n) {
  return mcp::json::JsonValue(n);
}

/**
 * @brief Helper to create bool JsonValue
 */
inline mcp::json::JsonValue boolean(bool b) {
  return mcp::json::JsonValue(b);
}

/**
 * @brief Helper to create nested object
 */
inline mcp::json::JsonValue nested(const std::string& key, 
                                   const mcp::json::JsonValue& value) {
  auto obj = mcp::json::JsonValue::object();
  obj.set(key, value);
  return obj;
}

}  // namespace test
}  // namespace config  
}  // namespace mcp
