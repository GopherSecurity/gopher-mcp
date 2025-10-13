/**
 * @file json_value_converter.cc
 * @brief Implementation of JSON conversion utilities for C API
 */

#include "json_value_converter.h"

#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <variant>
#include <vector>

#include "mcp/c_api/mcp_c_api_json.h"
#include "mcp/c_api/mcp_c_memory.h"
#include "mcp/logging/log_macros.h"

#define GOPHER_LOG_COMPONENT "capi.json.converter"

// Forward declare internal structure so we can access it directly
struct mcp_json_value_impl {
  mcp_json_type_t type;
  std::variant<std::monostate,                                    // null
               bool,                                              // boolean
               double,                                            // number
               std::string,                                       // string
               std::vector<mcp_json_value_t>,                     // array
               std::unordered_map<std::string, mcp_json_value_t>  // object
               >
      value;

  mcp_json_value_impl() : type(MCP_JSON_TYPE_NULL), value(std::monostate{}) {}
};

namespace mcp {
namespace c_api {
namespace internal {

namespace {

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
  fprintf(stderr, "[convertFromCApi] ENTRY with json=%p\n", json); fflush(stderr);

  if (!json) {
    fprintf(stderr, "[convertFromCApi] Null json, returning empty JsonValue\n"); fflush(stderr);
    return json::JsonValue();
  }

  // Get the type of the C API JSON value
  mcp_json_type_t type = mcp_json_get_type(json);
  fprintf(stderr, "[convertFromCApi] Type: %d\n", type); fflush(stderr);

  switch (type) {
    case MCP_JSON_TYPE_NULL:
      fprintf(stderr, "[convertFromCApi] Returning null JsonValue\n"); fflush(stderr);
      return json::JsonValue();

    case MCP_JSON_TYPE_BOOL: {
      mcp_bool_t value = mcp_json_get_bool(json);
      fprintf(stderr, "[convertFromCApi] Returning bool: %d\n", value); fflush(stderr);
      return json::JsonValue(value == MCP_TRUE);
    }

    case MCP_JSON_TYPE_NUMBER: {
      double value = mcp_json_get_number(json);
      fprintf(stderr, "[convertFromCApi] Returning number: %f\n", value); fflush(stderr);
      return json::JsonValue(value);
    }

    case MCP_JSON_TYPE_STRING: {
      const char* value = mcp_json_get_string(json);
      fprintf(stderr, "[convertFromCApi] Returning string: %s\n", value ? value : "(null)"); fflush(stderr);
      return json::JsonValue(value ? std::string(value) : std::string());
    }

    case MCP_JSON_TYPE_ARRAY: {
      fprintf(stderr, "[convertFromCApi] Converting array\n"); fflush(stderr);
      json::JsonValue array = json::JsonValue::array();
      size_t size = mcp_json_array_size(json);
      fprintf(stderr, "[convertFromCApi] Array size: %zu\n", size); fflush(stderr);

      for (size_t i = 0; i < size; ++i) {
        mcp_json_value_t elem = mcp_json_array_get(json, i);
        if (elem) {
          fprintf(stderr, "[convertFromCApi] Converting array element %zu\n", i); fflush(stderr);
          array.push_back(convertFromCApi(elem));
        } else {
          fprintf(stderr, "[convertFromCApi] Array element %zu is null, appending null\n", i); fflush(stderr);
          array.push_back(json::JsonValue());
        }
      }
      fprintf(stderr, "[convertFromCApi] Array conversion complete\n"); fflush(stderr);
      return array;
    }

    case MCP_JSON_TYPE_OBJECT: {
      fprintf(stderr, "[convertFromCApi] Converting object\n"); fflush(stderr);
      json::JsonValue object = json::JsonValue::object();

      // Access the internal structure to iterate over object keys
      // mcp_json_value_t is actually mcp_json_value_impl*
      auto* impl = reinterpret_cast<mcp_json_value_impl*>(json);

      if (impl->type == MCP_JSON_TYPE_OBJECT) {
        auto& map = std::get<std::unordered_map<std::string, mcp_json_value_t>>(impl->value);
        fprintf(stderr, "[convertFromCApi] Object has %zu keys\n", map.size()); fflush(stderr);

        for (const auto& [key, value] : map) {
          fprintf(stderr, "[convertFromCApi] Converting object key: %s\n", key.c_str()); fflush(stderr);
          if (value) {
            object[key] = convertFromCApi(value);
          } else {
            fprintf(stderr, "[convertFromCApi] Value for key '%s' is null\n", key.c_str()); fflush(stderr);
            object[key] = json::JsonValue();
          }
        }
      }

      fprintf(stderr, "[convertFromCApi] Object conversion complete\n"); fflush(stderr);
      return object;
    }

    default:
      fprintf(stderr, "[convertFromCApi] Unknown type: %d\n", type); fflush(stderr);
      throw std::runtime_error("Unknown C API JSON type");
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

  if (!config.contains("name") || !config["name"].isString()) {
    config["name"] = json::JsonValue("default");
  }

  if (!config.contains("transport_type") || !config["transport_type"].isString()) {
    config["transport_type"] = json::JsonValue("tcp");
  }

  if (!config.contains("filters")) {
    throw std::runtime_error("Configuration missing 'filters' array");
  }

  auto& filters = config["filters"];
  if (!filters.isArray()) {
    throw std::runtime_error("'filters' must be an array");
  }

  size_t normalized_count = 0;
  const size_t filter_count = filters.size();

  for (size_t i = 0; i < filter_count; ++i) {
    auto& filter = filters[i];

    if (!filter.isObject()) {
      throw std::runtime_error(
          "Filter at index " + std::to_string(i) + " is not an object");
    }

    if (normalizeTypedConfig(filter)) {
      normalized_count++;
    }

    if (!filter.contains("type") || !filter["type"].isString()) {
      throw std::runtime_error(
          "Filter at index " + std::to_string(i) + " missing required 'type' field");
    }

    if (!filter.contains("name") || !filter["name"].isString() ||
        filter["name"].getString().empty()) {
      std::string fallback_name = filter["type"].getString("filter");
      fallback_name += ":" + std::to_string(i);
      filter["name"] = json::JsonValue(fallback_name);
    }

    if (!filter.contains("config") || !filter["config"].isObject()) {
      filter["config"] = json::JsonValue::object();
    }

    if (!filter.contains("enabled")) {
      filter["enabled"] = json::JsonValue(true);
    }

    if (!filter.contains("enabled_when")) {
      filter["enabled_when"] = json::JsonValue::object();
    }
  }

  GOPHER_LOG(Debug, "Normalized {} typed configs across {} filters", normalized_count, filter_count);

  return normalized_count;
}

}  // namespace internal
}  // namespace c_api
}  // namespace mcp
