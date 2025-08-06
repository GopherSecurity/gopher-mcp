#pragma once

#include "mcp/json_bridge.h"
#include "mcp/types.h"
#include <vector>
#include <map>

namespace mcp {
namespace json {

// Forward declarations
class JsonSerializer;
class JsonDeserializer;

// Serialization functions for MCP types
class JsonSerializer {
public:
  // Serialize Error and ErrorData
  static JsonValue serialize(const Error& error);
  static JsonValue serialize(const ErrorData& data);
  
  // Serialize jsonrpc types
  static JsonValue serialize(const RequestId& id);
  static JsonValue serialize(const jsonrpc::Request& request);
  static JsonValue serialize(const jsonrpc::Response& response);
  static JsonValue serialize(const jsonrpc::ResponseResult& result);
  static JsonValue serialize(const jsonrpc::Notification& notification);
  
  // Serialize content types
  static JsonValue serialize(const TextContent& content);
  static JsonValue serialize(const ImageContent& content);
  static JsonValue serialize(const AudioContent& content);
  static JsonValue serialize(const ResourceContent& content);
  static JsonValue serialize(const ContentBlock& block);
  static JsonValue serialize(const ExtendedContentBlock& block);
  
  // Serialize tool types
  static JsonValue serialize(const Tool& tool);
  static JsonValue serialize(const CallToolRequest& request);
  static JsonValue serialize(const CallToolResult& result);
  
  // Serialize prompt types
  static JsonValue serialize(const Prompt& prompt);
  static JsonValue serialize(const PromptMessage& message);
  static JsonValue serialize(const GetPromptRequest& request);
  static JsonValue serialize(const GetPromptResult& result);
  
  // Serialize resource types
  static JsonValue serialize(const Resource& resource);
  static JsonValue serialize(const ReadResourceRequest& request);
  static JsonValue serialize(const ReadResourceResult& result);
  static JsonValue serialize(const ListResourcesRequest& request);
  static JsonValue serialize(const ListResourcesResult& result);
  
  // Serialize metadata
  static JsonValue serialize(const Metadata& metadata);
  
  // Serialize capabilities
  static JsonValue serialize(const ServerCapabilities& caps);
  static JsonValue serialize(const ClientCapabilities& caps);
  
  // Generic vector serialization
  template<typename T>
  static JsonValue serializeVector(const std::vector<T>& vec) {
    JsonArrayBuilder builder;
    for (const auto& item : vec) {
      builder.add(serialize(item));
    }
    return builder.build();
  }
};

// Deserialization functions for MCP types
class JsonDeserializer {
public:
  // Deserialize Error and ErrorData
  static Error deserializeError(const JsonValue& json);
  static ErrorData deserializeErrorData(const JsonValue& json);
  
  // Deserialize jsonrpc types
  static RequestId deserializeRequestId(const JsonValue& json);
  static jsonrpc::Request deserializeRequest(const JsonValue& json);
  static jsonrpc::Response deserializeResponse(const JsonValue& json);
  static jsonrpc::ResponseResult deserializeResponseResult(const JsonValue& json);
  static jsonrpc::Notification deserializeNotification(const JsonValue& json);
  
  // Deserialize content types
  static TextContent deserializeTextContent(const JsonValue& json);
  static ImageContent deserializeImageContent(const JsonValue& json);
  static AudioContent deserializeAudioContent(const JsonValue& json);
  static ResourceContent deserializeResourceContent(const JsonValue& json);
  static ContentBlock deserializeContentBlock(const JsonValue& json);
  static ExtendedContentBlock deserializeExtendedContentBlock(const JsonValue& json);
  
  // Deserialize tool types
  static Tool deserializeTool(const JsonValue& json);
  static CallToolRequest deserializeCallToolRequest(const JsonValue& json);
  static CallToolResult deserializeCallToolResult(const JsonValue& json);
  
  // Deserialize prompt types
  static Prompt deserializePrompt(const JsonValue& json);
  static PromptMessage deserializePromptMessage(const JsonValue& json);
  static GetPromptRequest deserializeGetPromptRequest(const JsonValue& json);
  static GetPromptResult deserializeGetPromptResult(const JsonValue& json);
  
  // Deserialize resource types
  static Resource deserializeResource(const JsonValue& json);
  static ReadResourceRequest deserializeReadResourceRequest(const JsonValue& json);
  static ReadResourceResult deserializeReadResourceResult(const JsonValue& json);
  static ListResourcesRequest deserializeListResourcesRequest(const JsonValue& json);
  static ListResourcesResult deserializeListResourcesResult(const JsonValue& json);
  
  // Deserialize metadata
  static Metadata deserializeMetadata(const JsonValue& json);
  
  // Deserialize capabilities
  static ServerCapabilities deserializeServerCapabilities(const JsonValue& json);
  static ClientCapabilities deserializeClientCapabilities(const JsonValue& json);
  
  // Generic vector deserialization
  template<typename T>
  static std::vector<T> deserializeVector(const JsonValue& json, 
                                          T (*deserializeFunc)(const JsonValue&)) {
    std::vector<T> result;
    if (!json.isArray()) {
      return result;
    }
    
    size_t size = json.size();
    result.reserve(size);
    for (size_t i = 0; i < size; ++i) {
      result.push_back(deserializeFunc(json[i]));
    }
    return result;
  }
};

// Helper functions for common conversions
inline JsonValue metadataToJson(const Metadata& metadata) {
  JsonObjectBuilder builder;
  for (const auto& kv : metadata) {
    const std::string& key = kv.first;
    const MetadataValue& value = kv.second;
    // Handle MetadataValue variant
    match(value,
      [&builder, &key](std::nullptr_t) { builder.addNull(key); },
      [&builder, &key](const std::string& s) { builder.add(key, s); },
      [&builder, &key](int64_t i) { builder.add(key, static_cast<int>(i)); },
      [&builder, &key](double d) { builder.add(key, d); },
      [&builder, &key](bool b) { builder.add(key, b); }
    );
  }
  return builder.build();
}

inline Metadata jsonToMetadata(const JsonValue& json) {
  Metadata metadata;
  if (!json.isObject()) {
    return metadata;
  }
  
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
    } else {
      // For complex types (arrays, objects), convert to string representation
      metadata[key] = MetadataValue(value.toString());
    }
  }
  return metadata;
}

}  // namespace json
}  // namespace mcp