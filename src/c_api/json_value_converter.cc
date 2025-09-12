/**
 * @file json_value_converter.cc
 * @brief Implementation of JSON conversion utilities for C API
 */

#include "json_value_converter.h"

#include <sstream>
#include <stdexcept>

#include "mcp/c_api/mcp_c_api_json.h"
#include "mcp/logging/log_macros.h"

#define GOPHER_LOG_COMPONENT "capi.json.converter"

namespace mcp {
namespace c_api {
namespace internal {

namespace {

// Helper to recursively convert C API JSON to internal JsonValue
json::JsonValue convertValue(mcp_json_value_t json) {
  if (!json) {
    return json::JsonValue();  // null
  }
  
  mcp_json_type_t type = mcp_json_get_type(json);
  
  switch (type) {
    case MCP_JSON_TYPE_NULL:
      return json::JsonValue();
      
    case MCP_JSON_TYPE_BOOL:
      return json::JsonValue(mcp_json_get_bool(json));
      
    case MCP_JSON_TYPE_NUMBER: {
      double num = mcp_json_get_number(json);
      // Try to preserve integer type if possible
      if (num == static_cast<int64_t>(num)) {
        return json::JsonValue(static_cast<int64_t>(num));
      }
      return json::JsonValue(num);
    }
    
    case MCP_JSON_TYPE_STRING: {
      const char* str = mcp_json_get_string(json);
      return json::JsonValue(str ? std::string(str) : std::string());
    }
    
    case MCP_JSON_TYPE_ARRAY: {
      auto array = json::JsonValue::array();
      size_t size = mcp_json_array_size(json);
      for (size_t i = 0; i < size; ++i) {
        mcp_json_value_t elem = mcp_json_array_get(json, i);
        array.push_back(convertValue(elem));
      }
      return array;
    }
    
    case MCP_JSON_TYPE_OBJECT: {
      auto obj = json::JsonValue::object();
      // For now, we'll create an empty object as we don't have iteration API in C
      // TODO: Add iteration support to C API
      GOPHER_LOG(Warning, "Object iteration not yet supported in C API conversion");
      return obj;
    }
    
    default:
      throw std::runtime_error("Unknown JSON type in C API handle");
  }
}

// Helper to recursively convert internal JsonValue to C API JSON
mcp_json_value_t convertToHandle(const json::JsonValue& json) {
  if (json.isNull()) {
    return mcp_json_create_null();
  }
  
  if (json.isBoolean()) {
    return mcp_json_create_bool(json.getBool());
  }
  
  if (json.isInteger()) {
    // C API uses double for all numbers
    return mcp_json_create_number(static_cast<double>(json.getInt64()));
  }
  
  if (json.isFloat()) {
    return mcp_json_create_number(json.getFloat());
  }
  
  if (json.isString()) {
    return mcp_json_create_string(json.getString().c_str());
  }
  
  if (json.isArray()) {
    mcp_json_value_t array = mcp_json_create_array();
    size_t size = json.size();
    
    for (size_t i = 0; i < size; ++i) {
      mcp_json_value_t elem = convertToHandle(json[i]);
      mcp_json_array_append(array, elem);
    }
    
    return array;
  }
  
  if (json.isObject()) {
    mcp_json_value_t obj = mcp_json_create_object();
    
    for (auto it = json.begin(); it != json.end(); ++it) {
      auto pair = *it;  // Returns std::pair<std::string, JsonValue>
      mcp_json_value_t value = convertToHandle(pair.second);
      mcp_json_object_set(obj, pair.first.c_str(), value);
    }
    
    return obj;
  }
  
  throw std::runtime_error("Unknown JsonValue type in conversion");
}

}  // anonymous namespace

json::JsonValue convertFromCApi(mcp_json_value_t json) {
  try {
    return convertValue(json);
  } catch (const std::exception& e) {
    GOPHER_LOG(Error, "Failed to convert C API JSON to JsonValue: {}", e.what());
    throw std::runtime_error(std::string("JSON conversion failed: ") + e.what());
  }
}

mcp_json_value_t convertToCApi(const json::JsonValue& json) {
  try {
    return convertToHandle(json);
  } catch (const std::exception& e) {
    GOPHER_LOG(Error, "Failed to convert JsonValue to C API JSON: {}", e.what());
    return mcp_json_create_null();
  }
}

bool normalizeTypedConfig(json::JsonValue& filter) {
  if (!filter.isObject()) {
    return false;
  }
  
  if (!filter.contains("typed_config")) {
    return false;
  }
  
  auto typed_config = filter["typed_config"];
  if (!typed_config.isObject()) {
    GOPHER_LOG(Warning, "typed_config is not an object, skipping normalization");
    return false;
  }
  
  // Remove @type field if present
  if (typed_config.contains("@type")) {
    std::string type_url = typed_config["@type"].getString("");
    GOPHER_LOG(Debug, "Normalizing typed_config, dropping @type: {}", type_url);
    typed_config.erase("@type");
  }
  
  // Move remaining fields to config
  filter["config"] = typed_config;
  filter.erase("typed_config");
  
  return true;
}

size_t normalizeFilterChain(json::JsonValue& config) {
  if (!config.isObject()) {
    throw std::runtime_error("Configuration must be an object");
  }
  
  if (!config.contains("filters")) {
    throw std::runtime_error("Configuration missing 'filters' array");
  }
  
  auto& filters = config["filters"];
  if (!filters.isArray()) {
    throw std::runtime_error("'filters' must be an array");
  }
  
  size_t normalized_count = 0;
  size_t filter_count = filters.size();
  
  for (size_t i = 0; i < filter_count; ++i) {
    auto& filter = filters[i];
    
    if (!filter.isObject()) {
      throw std::runtime_error(
          "Filter at index " + std::to_string(i) + " is not an object");
    }
    
    // Normalize typed_config if present
    if (normalizeTypedConfig(filter)) {
      normalized_count++;
    }
    
    // Ensure 'type' field exists
    if (!filter.contains("type")) {
      // Try to use 'name' as 'type' if available
      if (filter.contains("name") && filter["name"].isString()) {
        filter["type"] = filter["name"];
        GOPHER_LOG(Debug, "Using 'name' as 'type' for filter at index {}", i);
      } else {
        throw std::runtime_error(
            "Filter at index " + std::to_string(i) + " missing required 'type' field");
      }
    }
    
    // Validate type is a string
    if (!filter["type"].isString()) {
      throw std::runtime_error(
          "Filter at index " + std::to_string(i) + " has non-string 'type' field");
    }
  }
  
  GOPHER_LOG(Debug, "Normalized {} of {} filters", normalized_count, filter_count);
  
  return normalized_count;
}

}  // namespace internal
}  // namespace c_api
}  // namespace mcp