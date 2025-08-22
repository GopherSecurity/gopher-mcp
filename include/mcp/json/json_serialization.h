#pragma once

#include <map>
#include <vector>

#include "mcp/json/json_bridge.h"
#include "mcp/types.h"

namespace mcp {
namespace json {

// Forward declarations
template <typename T>
struct JsonSerializeTraits;
template <typename T>
struct JsonDeserializeTraits;

// ============ UNIFIED SERIALIZATION CLASS ============
// Just ONE template function for ALL types!
class JsonSerializer {
 public:
  template <typename T>
  static JsonValue serialize(const T& value) {
    return JsonSerializeTraits<T>::serialize(value);
  }
};

// ============ UNIFIED DESERIALIZATION CLASS ============
// Just ONE template function for ALL types!
class JsonDeserializer {
 public:
  template <typename T>
  static T deserialize(const JsonValue& json) {
    return JsonDeserializeTraits<T>::deserialize(json);
  }
};

// ============ SHORT ALIASES FOR CONVENIENCE ============
// Much easier to type: to_json(value) and from_json<T>(json)

template <typename T>
inline JsonValue to_json(const T& value) {
  return JsonSerializer::serialize<T>(value);
}

template <typename T>
inline T from_json(const JsonValue& json) {
  return JsonDeserializer::deserialize<T>(json);
}

// ============ BASIC TYPE TRAITS ============

// Serialization traits for basic types
template <>
struct JsonSerializeTraits<std::string> {
  static JsonValue serialize(const std::string& value) {
    return JsonValue(value);
  }
};
template <>
struct JsonSerializeTraits<int> {
  static JsonValue serialize(int value) { return JsonValue(value); }
};
template <>
struct JsonSerializeTraits<int64_t> {
  static JsonValue serialize(int64_t value) { return JsonValue(value); }
};
template <>
struct JsonSerializeTraits<double> {
  static JsonValue serialize(double value) { return JsonValue(value); }
};
template <>
struct JsonSerializeTraits<bool> {
  static JsonValue serialize(bool value) { return JsonValue(value); }
};

// Deserialization traits for basic types
template <>
struct JsonDeserializeTraits<std::string> {
  static std::string deserialize(const JsonValue& json) {
    return json.getString();
  }
};
template <>
struct JsonDeserializeTraits<int> {
  static int deserialize(const JsonValue& json) { return json.getInt(); }
};
template <>
struct JsonDeserializeTraits<int64_t> {
  static int64_t deserialize(const JsonValue& json) { return json.getInt64(); }
};
template <>
struct JsonDeserializeTraits<double> {
  static double deserialize(const JsonValue& json) { return json.getFloat(); }
};
template <>
struct JsonDeserializeTraits<bool> {
  static bool deserialize(const JsonValue& json) { return json.getBool(); }
};

// ============ CONTAINER TRAITS ============

// std::vector serialization/deserialization
template <typename T>
struct JsonSerializeTraits<std::vector<T>> {
  static JsonValue serialize(const std::vector<T>& vec) {
    JsonArrayBuilder builder;
    for (const auto& item : vec) {
      builder.add(JsonSerializer::serialize<T>(item));
    }
    return builder.build();
  }
};

template <typename T>
struct JsonDeserializeTraits<std::vector<T>> {
  static std::vector<T> deserialize(const JsonValue& json) {
    std::vector<T> result;
    if (!json.isArray())
      return result;

    size_t size = json.size();
    result.reserve(size);
    for (size_t i = 0; i < size; ++i) {
      result.push_back(JsonDeserializer::deserialize<T>(json[i]));
    }
    return result;
  }
};

// optional serialization/deserialization
template <typename T>
struct JsonSerializeTraits<optional<T>> {
  static JsonValue serialize(const optional<T>& opt) {
    if (opt.has_value()) {
      return JsonSerializer::serialize<T>(opt.value());
    }
    return JsonValue::null();
  }
};

template <typename T>
struct JsonDeserializeTraits<optional<T>> {
  static optional<T> deserialize(const JsonValue& json) {
    if (json.isNull()) {
      return nullopt;
    }
    return mcp::make_optional(JsonDeserializer::deserialize<T>(json));
  }
};

// std::map serialization/deserialization
template <typename V>
struct JsonSerializeTraits<std::map<std::string, V>> {
  static JsonValue serialize(const std::map<std::string, V>& map) {
    JsonObjectBuilder builder;
    for (const auto& kv : map) {
      builder.add(kv.first, JsonSerializer::serialize<V>(kv.second));
    }
    return builder.build();
  }
};

template <typename V>
struct JsonDeserializeTraits<std::map<std::string, V>> {
  static std::map<std::string, V> deserialize(const JsonValue& json) {
    std::map<std::string, V> result;
    if (!json.isObject())
      return result;

    for (const auto& key : json.keys()) {
      result[key] = JsonDeserializer::deserialize<V>(json[key]);
    }
    return result;
  }
};

// ============ ENUM TRAITS ============

template <>
struct JsonSerializeTraits<enums::Role::Value> {
  static JsonValue serialize(enums::Role::Value value) {
    return JsonValue(enums::Role::to_string(value));
  }
};

template <>
struct JsonDeserializeTraits<enums::Role::Value> {
  static enums::Role::Value deserialize(const JsonValue& json) {
    auto str = json.getString();
    auto value = enums::Role::from_string(str);
    if (!value.has_value()) {
      throw JsonException("Invalid role: " + str);
    }
    return value.value();
  }
};

template <>
struct JsonSerializeTraits<enums::LoggingLevel::Value> {
  static JsonValue serialize(enums::LoggingLevel::Value value) {
    return JsonValue(enums::LoggingLevel::to_string(value));
  }
};

template <>
struct JsonDeserializeTraits<enums::LoggingLevel::Value> {
  static enums::LoggingLevel::Value deserialize(const JsonValue& json) {
    auto str = json.getString();
    auto value = enums::LoggingLevel::from_string(str);
    if (!value.has_value()) {
      throw JsonException("Invalid logging level: " + str);
    }
    return value.value();
  }
};

// ============ SPECIAL TYPE TRAITS ============

// Implementation type
template <>
struct JsonSerializeTraits<Implementation> {
  static JsonValue serialize(const Implementation& impl) {
    JsonObjectBuilder builder;
    builder.add("name", impl.name);
    builder.add("version", impl.version);
    return builder.build();
  }
};

template <>
struct JsonDeserializeTraits<Implementation> {
  static Implementation deserialize(const JsonValue& json) {
    Implementation impl;
    impl.name = json.at("name").getString();
    impl.version = json.at("version").getString();
    return impl;
  }
};

// Metadata type
template <>
struct JsonSerializeTraits<Metadata> {
  static JsonValue serialize(const Metadata& metadata) {
    JsonObjectBuilder builder;
    for (const auto& kv : metadata) {
      match(
          kv.second, [&](std::nullptr_t) { builder.addNull(kv.first); },
          [&](const std::string& s) { builder.add(kv.first, s); },
          [&](int64_t i) { builder.add(kv.first, static_cast<int>(i)); },
          [&](double d) { builder.add(kv.first, d); },
          [&](bool b) { builder.add(kv.first, b); });
    }
    return builder.build();
  }
};

template <>
struct JsonDeserializeTraits<Metadata> {
  static Metadata deserialize(const JsonValue& json) {
    Metadata metadata;
    if (!json.isObject())
      return metadata;

    for (const auto& key : json.keys()) {
      const auto& value = json[key];
      if (value.isNull()) {
        metadata[key] = MetadataValue(nullptr);
      } else if (value.isString()) {
        metadata[key] = MetadataValue(value.getString());
      } else if (value.isInteger()) {
        metadata[key] = MetadataValue(value.getInt64());
      } else if (value.isFloat()) {
        metadata[key] = MetadataValue(value.getFloat());
      } else if (value.isBoolean()) {
        metadata[key] = MetadataValue(value.getBool());
      } else if (value.isObject() || value.isArray()) {
        // Store nested objects and arrays as JSON strings
        // They can be parsed back when needed by specific handlers
        // This is a pragmatic approach to support MCP's nested arguments
        // without changing the MetadataValue variant type
        metadata[key] = MetadataValue(value.toString());
      } else {
        // Fallback for any other type
        metadata[key] = MetadataValue(value.toString());
      }
    }
    return metadata;
  }
};

// ============ HELPER FUNCTIONS (kept for compatibility) ============

inline JsonValue metadataToJson(const Metadata& metadata) {
  return JsonSerializer::serialize(metadata);
}

inline Metadata jsonToMetadata(const JsonValue& json) {
  return JsonDeserializer::deserialize<Metadata>(json);
}

}  // namespace json
}  // namespace mcp

// Include the generated traits for all MCP types
// This keeps the main header clean and allows easy regeneration
#include "mcp/json/json_serialization_mcp_traits.h"