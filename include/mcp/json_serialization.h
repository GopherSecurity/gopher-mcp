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

// Forward declaration for template specialization
template<typename T>
struct JsonSerializeTraits;

// Serialization functions for MCP types
class JsonSerializer {
public:
  // Template-based serialization
  template<typename T>
  static JsonValue serialize(const T& value) {
    return JsonSerializeTraits<T>::serialize(value);
  }
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
  
  // ===== Request Types =====
  
  // Initialize and session requests
  static JsonValue serialize(const InitializeRequest& request);
  static JsonValue serialize(const PingRequest& request);
  static JsonValue serialize(const CompleteRequest& request);
  static JsonValue serialize(const SetLevelRequest& request);
  
  // Tool requests
  static JsonValue serialize(const Tool& tool);
  static JsonValue serialize(const CallToolRequest& request);
  static JsonValue serialize(const ListToolsRequest& request);
  
  // Prompt requests
  static JsonValue serialize(const Prompt& prompt);
  static JsonValue serialize(const PromptMessage& message);
  static JsonValue serialize(const GetPromptRequest& request);
  static JsonValue serialize(const ListPromptsRequest& request);
  
  // Resource requests
  static JsonValue serialize(const Resource& resource);
  static JsonValue serialize(const ResourceTemplate& resourceTemplate);
  static JsonValue serialize(const ReadResourceRequest& request);
  static JsonValue serialize(const ListResourcesRequest& request);
  static JsonValue serialize(const ListResourceTemplatesRequest& request);
  static JsonValue serialize(const SubscribeRequest& request);
  static JsonValue serialize(const UnsubscribeRequest& request);
  
  // Root requests
  static JsonValue serialize(const Root& root);
  static JsonValue serialize(const ListRootsRequest& request);
  
  // Message requests
  static JsonValue serialize(const CreateMessageRequest& request);
  static JsonValue serialize(const ElicitRequest& request);
  
  // ===== Response/Result Types =====
  
  // Initialize and session results
  static JsonValue serialize(const InitializeResult& result);
  static JsonValue serialize(const CompleteResult& result);
  
  // Tool results
  static JsonValue serialize(const CallToolResult& result);
  static JsonValue serialize(const ListToolsResult& result);
  
  // Prompt results
  static JsonValue serialize(const GetPromptResult& result);
  static JsonValue serialize(const ListPromptsResult& result);
  
  // Resource results
  static JsonValue serialize(const ReadResourceResult& result);
  static JsonValue serialize(const ListResourcesResult& result);
  static JsonValue serialize(const ListResourceTemplatesResult& result);
  
  // Root results
  static JsonValue serialize(const ListRootsResult& result);
  
  // Message results
  static JsonValue serialize(const CreateMessageResult& result);
  static JsonValue serialize(const ElicitResult& result);
  
  // ===== Notification Types =====
  
  static JsonValue serialize(const CancelledNotification& notification);
  static JsonValue serialize(const ProgressNotification& notification);
  static JsonValue serialize(const InitializedNotification& notification);
  static JsonValue serialize(const RootsListChangedNotification& notification);
  static JsonValue serialize(const LoggingMessageNotification& notification);
  static JsonValue serialize(const ResourceUpdatedNotification& notification);
  static JsonValue serialize(const ResourceListChangedNotification& notification);
  static JsonValue serialize(const ToolListChangedNotification& notification);
  static JsonValue serialize(const PromptListChangedNotification& notification);
  
  // ===== Core Data Structures =====
  
  // Metadata
  static JsonValue serialize(const Metadata& metadata);
  
  // Resources
  static JsonValue serialize(const ResourceContents& contents);
  static JsonValue serialize(const TextResourceContents& contents);
  static JsonValue serialize(const BlobResourceContents& contents);
  
  // Messages
  static JsonValue serialize(const Message& message);
  static JsonValue serialize(const SamplingMessage& message);
  static JsonValue serialize(const ModelPreferences& prefs);
  static JsonValue serialize(const ModelHint& hint);
  
  // Annotations
  static JsonValue serialize(const Annotations& annotations);
  static JsonValue serialize(const ToolAnnotations& annotations);
  
  // References
  static JsonValue serialize(const PromptReference& ref);
  static JsonValue serialize(const ResourceTemplateReference& ref);
  
  // ===== Capability Types =====
  
  static JsonValue serialize(const ServerCapabilities& caps);
  static JsonValue serialize(const ClientCapabilities& caps);
  static JsonValue serialize(const RootsCapability& cap);
  static JsonValue serialize(const ResourcesCapability& cap);
  static JsonValue serialize(const PromptsCapability& cap);
  static JsonValue serialize(const EmptyCapability& cap);
  static JsonValue serialize(const SamplingParams& params);
  
  // These will be removed - using template specializations instead
};

// Forward declaration for template specialization
template<typename T>
struct JsonDeserializeTraits;

// Deserialization functions for MCP types
class JsonDeserializer {
public:
  // Template-based deserialization
  template<typename T>
  static T deserialize(const JsonValue& json) {
    return JsonDeserializeTraits<T>::deserialize(json);
  }
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
  static ResourceLink deserializeResourceLink(const JsonValue& json);
  static EmbeddedResource deserializeEmbeddedResource(const JsonValue& json);
  static ContentBlock deserializeContentBlock(const JsonValue& json);
  static ExtendedContentBlock deserializeExtendedContentBlock(const JsonValue& json);
  
  // ===== Request Types =====
  
  // Initialize and session requests
  static InitializeRequest deserializeInitializeRequest(const JsonValue& json);
  static PingRequest deserializePingRequest(const JsonValue& json);
  static CompleteRequest deserializeCompleteRequest(const JsonValue& json);
  static SetLevelRequest deserializeSetLevelRequest(const JsonValue& json);
  
  // Tool requests
  static Tool deserializeTool(const JsonValue& json);
  static CallToolRequest deserializeCallToolRequest(const JsonValue& json);
  static ListToolsRequest deserializeListToolsRequest(const JsonValue& json);
  
  // Prompt requests
  static Prompt deserializePrompt(const JsonValue& json);
  static PromptMessage deserializePromptMessage(const JsonValue& json);
  static GetPromptRequest deserializeGetPromptRequest(const JsonValue& json);
  static ListPromptsRequest deserializeListPromptsRequest(const JsonValue& json);
  
  // Resource requests
  static Resource deserializeResource(const JsonValue& json);
  static ResourceTemplate deserializeResourceTemplate(const JsonValue& json);
  static ReadResourceRequest deserializeReadResourceRequest(const JsonValue& json);
  static ListResourcesRequest deserializeListResourcesRequest(const JsonValue& json);
  static ListResourceTemplatesRequest deserializeListResourceTemplatesRequest(const JsonValue& json);
  static SubscribeRequest deserializeSubscribeRequest(const JsonValue& json);
  static UnsubscribeRequest deserializeUnsubscribeRequest(const JsonValue& json);
  
  // Root requests
  static Root deserializeRoot(const JsonValue& json);
  static ListRootsRequest deserializeListRootsRequest(const JsonValue& json);
  
  // Message requests
  static CreateMessageRequest deserializeCreateMessageRequest(const JsonValue& json);
  static ElicitRequest deserializeElicitRequest(const JsonValue& json);
  
  // ===== Response/Result Types =====
  
  // Initialize and session results
  static InitializeResult deserializeInitializeResult(const JsonValue& json);
  static CompleteResult deserializeCompleteResult(const JsonValue& json);
  
  // Tool results
  static CallToolResult deserializeCallToolResult(const JsonValue& json);
  static ListToolsResult deserializeListToolsResult(const JsonValue& json);
  
  // Prompt results
  static GetPromptResult deserializeGetPromptResult(const JsonValue& json);
  static ListPromptsResult deserializeListPromptsResult(const JsonValue& json);
  
  // Resource results
  static ReadResourceResult deserializeReadResourceResult(const JsonValue& json);
  static ListResourcesResult deserializeListResourcesResult(const JsonValue& json);
  static ListResourceTemplatesResult deserializeListResourceTemplatesResult(const JsonValue& json);
  
  // Root results
  static ListRootsResult deserializeListRootsResult(const JsonValue& json);
  
  // Message results
  static CreateMessageResult deserializeCreateMessageResult(const JsonValue& json);
  static ElicitResult deserializeElicitResult(const JsonValue& json);
  
  // ===== Notification Types =====
  
  static CancelledNotification deserializeCancelledNotification(const JsonValue& json);
  static ProgressNotification deserializeProgressNotification(const JsonValue& json);
  static InitializedNotification deserializeInitializedNotification(const JsonValue& json);
  static RootsListChangedNotification deserializeRootsListChangedNotification(const JsonValue& json);
  static LoggingMessageNotification deserializeLoggingMessageNotification(const JsonValue& json);
  static ResourceUpdatedNotification deserializeResourceUpdatedNotification(const JsonValue& json);
  static ResourceListChangedNotification deserializeResourceListChangedNotification(const JsonValue& json);
  static ToolListChangedNotification deserializeToolListChangedNotification(const JsonValue& json);
  static PromptListChangedNotification deserializePromptListChangedNotification(const JsonValue& json);
  
  // ===== Core Data Structures =====
  
  // Metadata
  static Metadata deserializeMetadata(const JsonValue& json);
  
  // Resources
  static variant<TextResourceContents, BlobResourceContents> deserializeResourceContents(const JsonValue& json);
  static TextResourceContents deserializeTextResourceContents(const JsonValue& json);
  static BlobResourceContents deserializeBlobResourceContents(const JsonValue& json);
  
  // Messages
  static Message deserializeMessage(const JsonValue& json);
  static SamplingMessage deserializeSamplingMessage(const JsonValue& json);
  static ModelPreferences deserializeModelPreferences(const JsonValue& json);
  static ModelHint deserializeModelHint(const JsonValue& json);
  
  // Annotations
  static Annotations deserializeAnnotations(const JsonValue& json);
  static ToolAnnotations deserializeToolAnnotations(const JsonValue& json);
  
  // References
  static PromptReference deserializePromptReference(const JsonValue& json);
  static ResourceTemplateReference deserializeResourceTemplateReference(const JsonValue& json);
  
  // ===== Capability Types =====
  
  static ServerCapabilities deserializeServerCapabilities(const JsonValue& json);
  static ClientCapabilities deserializeClientCapabilities(const JsonValue& json);
  static RootsCapability deserializeRootsCapability(const JsonValue& json);
  static ResourcesCapability deserializeResourcesCapability(const JsonValue& json);
  static PromptsCapability deserializePromptsCapability(const JsonValue& json);
  static EmptyCapability deserializeEmptyCapability(const JsonValue& json);
  static SamplingParams deserializeSamplingParams(const JsonValue& json);
  
  // These will be removed - using template specializations instead
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

// ============ SERIALIZATION TRAITS ============

// Basic type specializations for serialization
template<>
struct JsonSerializeTraits<std::string> {
  static JsonValue serialize(const std::string& value) {
    return JsonValue(value);
  }
};

template<>
struct JsonSerializeTraits<int> {
  static JsonValue serialize(int value) {
    return JsonValue(value);
  }
};

template<>
struct JsonSerializeTraits<int64_t> {
  static JsonValue serialize(int64_t value) {
    return JsonValue(value);
  }
};

template<>
struct JsonSerializeTraits<double> {
  static JsonValue serialize(double value) {
    return JsonValue(value);
  }
};

template<>
struct JsonSerializeTraits<bool> {
  static JsonValue serialize(bool value) {
    return JsonValue(value);
  }
};

// Container specializations for serialization
template<typename T>
struct JsonSerializeTraits<std::vector<T>> {
  static JsonValue serialize(const std::vector<T>& vec) {
    JsonArrayBuilder builder;
    for (const auto& item : vec) {
      builder.add(JsonSerializer::serialize<T>(item));
    }
    return builder.build();
  }
};

template<typename T>
struct JsonSerializeTraits<optional<T>> {
  static JsonValue serialize(const optional<T>& opt) {
    if (opt.has_value()) {
      return JsonSerializer::serialize<T>(opt.value());
    }
    return JsonValue::null();
  }
};

template<typename K, typename V>
struct JsonSerializeTraits<std::map<K, V>> {
  static JsonValue serialize(const std::map<K, V>& map) {
    JsonObjectBuilder builder;
    for (const auto& kv : map) {
      // Assume K can be converted to string for JSON keys
      builder.add(std::to_string(kv.first), JsonSerializer::serialize<V>(kv.second));
    }
    return builder.build();
  }
};

// Specialization for string keys (most common case)
template<typename V>
struct JsonSerializeTraits<std::map<std::string, V>> {
  static JsonValue serialize(const std::map<std::string, V>& map) {
    JsonObjectBuilder builder;
    for (const auto& kv : map) {
      builder.add(kv.first, JsonSerializer::serialize<V>(kv.second));
    }
    return builder.build();
  }
};

// Template macro for MCP type serialization traits
#define DECLARE_SERIALIZE_TRAIT(Type) \
  template<> \
  struct JsonSerializeTraits<Type> { \
    static JsonValue serialize(const Type& value) { \
      return JsonSerializer::serialize(value); \
    } \
  };

// Generate all MCP type serialize traits
DECLARE_SERIALIZE_TRAIT(Error)
DECLARE_SERIALIZE_TRAIT(ErrorData)
DECLARE_SERIALIZE_TRAIT(RequestId)
DECLARE_SERIALIZE_TRAIT(TextContent)
DECLARE_SERIALIZE_TRAIT(ImageContent)
DECLARE_SERIALIZE_TRAIT(AudioContent)
DECLARE_SERIALIZE_TRAIT(ResourceContent)
DECLARE_SERIALIZE_TRAIT(ContentBlock)
DECLARE_SERIALIZE_TRAIT(ExtendedContentBlock)
DECLARE_SERIALIZE_TRAIT(Tool)
DECLARE_SERIALIZE_TRAIT(Prompt)
DECLARE_SERIALIZE_TRAIT(PromptMessage)
DECLARE_SERIALIZE_TRAIT(Resource)
DECLARE_SERIALIZE_TRAIT(ResourceTemplate)
DECLARE_SERIALIZE_TRAIT(ResourceContents)
DECLARE_SERIALIZE_TRAIT(TextResourceContents)
DECLARE_SERIALIZE_TRAIT(BlobResourceContents)
DECLARE_SERIALIZE_TRAIT(Root)
DECLARE_SERIALIZE_TRAIT(Message)
DECLARE_SERIALIZE_TRAIT(SamplingMessage)
DECLARE_SERIALIZE_TRAIT(ModelPreferences)
DECLARE_SERIALIZE_TRAIT(ModelHint)
DECLARE_SERIALIZE_TRAIT(Annotations)
DECLARE_SERIALIZE_TRAIT(ToolAnnotations)
DECLARE_SERIALIZE_TRAIT(PromptReference)
DECLARE_SERIALIZE_TRAIT(ResourceTemplateReference)
DECLARE_SERIALIZE_TRAIT(ServerCapabilities)
DECLARE_SERIALIZE_TRAIT(ClientCapabilities)
DECLARE_SERIALIZE_TRAIT(RootsCapability)
DECLARE_SERIALIZE_TRAIT(ResourcesCapability)
DECLARE_SERIALIZE_TRAIT(PromptsCapability)
DECLARE_SERIALIZE_TRAIT(EmptyCapability)
DECLARE_SERIALIZE_TRAIT(SamplingParams)
// Implementation has inline specialization below
DECLARE_SERIALIZE_TRAIT(Metadata)

// Request types
DECLARE_SERIALIZE_TRAIT(InitializeRequest)
DECLARE_SERIALIZE_TRAIT(PingRequest)
DECLARE_SERIALIZE_TRAIT(CompleteRequest)
DECLARE_SERIALIZE_TRAIT(SetLevelRequest)
DECLARE_SERIALIZE_TRAIT(CallToolRequest)
DECLARE_SERIALIZE_TRAIT(ListToolsRequest)
DECLARE_SERIALIZE_TRAIT(GetPromptRequest)
DECLARE_SERIALIZE_TRAIT(ListPromptsRequest)
DECLARE_SERIALIZE_TRAIT(ReadResourceRequest)
DECLARE_SERIALIZE_TRAIT(ListResourcesRequest)
DECLARE_SERIALIZE_TRAIT(ListResourceTemplatesRequest)
DECLARE_SERIALIZE_TRAIT(SubscribeRequest)
DECLARE_SERIALIZE_TRAIT(UnsubscribeRequest)
DECLARE_SERIALIZE_TRAIT(ListRootsRequest)
DECLARE_SERIALIZE_TRAIT(CreateMessageRequest)
DECLARE_SERIALIZE_TRAIT(ElicitRequest)

// Result types  
DECLARE_SERIALIZE_TRAIT(InitializeResult)
DECLARE_SERIALIZE_TRAIT(CompleteResult)
DECLARE_SERIALIZE_TRAIT(CallToolResult)
DECLARE_SERIALIZE_TRAIT(ListToolsResult)
DECLARE_SERIALIZE_TRAIT(GetPromptResult)
DECLARE_SERIALIZE_TRAIT(ListPromptsResult)
DECLARE_SERIALIZE_TRAIT(ReadResourceResult)
DECLARE_SERIALIZE_TRAIT(ListResourcesResult)
DECLARE_SERIALIZE_TRAIT(ListResourceTemplatesResult)
DECLARE_SERIALIZE_TRAIT(ListRootsResult)
DECLARE_SERIALIZE_TRAIT(CreateMessageResult)
DECLARE_SERIALIZE_TRAIT(ElicitResult)

// Notification types
DECLARE_SERIALIZE_TRAIT(CancelledNotification)
DECLARE_SERIALIZE_TRAIT(ProgressNotification)
DECLARE_SERIALIZE_TRAIT(InitializedNotification)
DECLARE_SERIALIZE_TRAIT(RootsListChangedNotification)
DECLARE_SERIALIZE_TRAIT(LoggingMessageNotification)
DECLARE_SERIALIZE_TRAIT(ResourceUpdatedNotification)
DECLARE_SERIALIZE_TRAIT(ResourceListChangedNotification)
DECLARE_SERIALIZE_TRAIT(ToolListChangedNotification)
DECLARE_SERIALIZE_TRAIT(PromptListChangedNotification)

// JSONRPC types
template<>
struct JsonSerializeTraits<jsonrpc::Request> {
  static JsonValue serialize(const jsonrpc::Request& value) {
    return JsonSerializer::serialize(value);
  }
};

template<>
struct JsonSerializeTraits<jsonrpc::Response> {
  static JsonValue serialize(const jsonrpc::Response& value) {
    return JsonSerializer::serialize(value);
  }
};

template<>
struct JsonSerializeTraits<jsonrpc::ResponseResult> {
  static JsonValue serialize(const jsonrpc::ResponseResult& value) {
    return JsonSerializer::serialize(value);
  }
};

template<>
struct JsonSerializeTraits<jsonrpc::Notification> {
  static JsonValue serialize(const jsonrpc::Notification& value) {
    return JsonSerializer::serialize(value);
  }
};

// Enum specializations with direct implementation
template<>
struct JsonSerializeTraits<enums::Role::Value> {
  static JsonValue serialize(enums::Role::Value value) {
    return JsonValue(enums::Role::to_string(value));
  }
};

template<>
struct JsonSerializeTraits<enums::LoggingLevel::Value> {
  static JsonValue serialize(enums::LoggingLevel::Value value) {
    return JsonValue(enums::LoggingLevel::to_string(value));
  }
};

// Implementation specialization
template<>
struct JsonSerializeTraits<Implementation> {
  static JsonValue serialize(const Implementation& impl) {
    JsonObjectBuilder builder;
    builder.add("name", impl.name);
    builder.add("version", impl.version);
    return builder.build();
  }
};

#undef DECLARE_SERIALIZE_TRAIT

// ============ DESERIALIZATION TRAITS ============

// Basic type specializations
template<>
struct JsonDeserializeTraits<std::string> {
  static std::string deserialize(const JsonValue& json) {
    return json.getString();
  }
};

template<>
struct JsonDeserializeTraits<int> {
  static int deserialize(const JsonValue& json) {
    return json.getInt();
  }
};

template<>
struct JsonDeserializeTraits<int64_t> {
  static int64_t deserialize(const JsonValue& json) {
    return json.getInt64();
  }
};

template<>
struct JsonDeserializeTraits<double> {
  static double deserialize(const JsonValue& json) {
    return json.getFloat();
  }
};

template<>
struct JsonDeserializeTraits<bool> {
  static bool deserialize(const JsonValue& json) {
    return json.getBool();
  }
};

// Container specializations for deserialization
template<typename T>
struct JsonDeserializeTraits<std::vector<T>> {
  static std::vector<T> deserialize(const JsonValue& json) {
    std::vector<T> result;
    if (!json.isArray()) {
      return result;
    }
    
    size_t size = json.size();
    result.reserve(size);
    for (size_t i = 0; i < size; ++i) {
      result.push_back(JsonDeserializer::deserialize<T>(json[i]));
    }
    return result;
  }
};

template<typename T>
struct JsonDeserializeTraits<optional<T>> {
  static optional<T> deserialize(const JsonValue& json) {
    if (json.isNull()) {
      return nullopt;
    }
    return make_optional(JsonDeserializer::deserialize<T>(json));
  }
};

template<typename K, typename V>
struct JsonDeserializeTraits<std::map<K, V>> {
  static std::map<K, V> deserialize(const JsonValue& json) {
    std::map<K, V> result;
    if (!json.isObject()) {
      return result;
    }
    
    for (const auto& key : json.keys()) {
      // This requires K to be constructible from string
      // For numeric keys, might need std::stoi, std::stol, etc.
      result[K(key)] = JsonDeserializer::deserialize<V>(json[key]);
    }
    return result;
  }
};

// Specialization for string keys (most common case)
template<typename V>
struct JsonDeserializeTraits<std::map<std::string, V>> {
  static std::map<std::string, V> deserialize(const JsonValue& json) {
    std::map<std::string, V> result;
    if (!json.isObject()) {
      return result;
    }
    
    for (const auto& key : json.keys()) {
      result[key] = JsonDeserializer::deserialize<V>(json[key]);
    }
    return result;
  }
};

// Template specializations for all MCP types
#define DECLARE_DESERIALIZE_TRAIT(Type) \
  template<> \
  struct JsonDeserializeTraits<Type> { \
    static Type deserialize(const JsonValue& json) { \
      return JsonDeserializer::deserialize##Type(json); \
    } \
  };

// Error and ErrorData
DECLARE_DESERIALIZE_TRAIT(Error)
DECLARE_DESERIALIZE_TRAIT(ErrorData)

// Request ID
DECLARE_DESERIALIZE_TRAIT(RequestId)

// Content types
DECLARE_DESERIALIZE_TRAIT(TextContent)
DECLARE_DESERIALIZE_TRAIT(ImageContent)
DECLARE_DESERIALIZE_TRAIT(AudioContent)
DECLARE_DESERIALIZE_TRAIT(ResourceLink)
DECLARE_DESERIALIZE_TRAIT(EmbeddedResource)
DECLARE_DESERIALIZE_TRAIT(ContentBlock)
DECLARE_DESERIALIZE_TRAIT(ExtendedContentBlock)

// Tools and Prompts
DECLARE_DESERIALIZE_TRAIT(Tool)
DECLARE_DESERIALIZE_TRAIT(Prompt)
DECLARE_DESERIALIZE_TRAIT(PromptMessage)

// Resources
DECLARE_DESERIALIZE_TRAIT(Resource)
DECLARE_DESERIALIZE_TRAIT(ResourceTemplate)
DECLARE_DESERIALIZE_TRAIT(TextResourceContents)
DECLARE_DESERIALIZE_TRAIT(BlobResourceContents)

// Roots
DECLARE_DESERIALIZE_TRAIT(Root)

// Messages
DECLARE_DESERIALIZE_TRAIT(Message)
DECLARE_DESERIALIZE_TRAIT(SamplingMessage)
DECLARE_DESERIALIZE_TRAIT(ModelPreferences)
DECLARE_DESERIALIZE_TRAIT(ModelHint)

// Annotations
DECLARE_DESERIALIZE_TRAIT(Annotations)
DECLARE_DESERIALIZE_TRAIT(ToolAnnotations)

// References
DECLARE_DESERIALIZE_TRAIT(PromptReference)
DECLARE_DESERIALIZE_TRAIT(ResourceTemplateReference)

// Capabilities
DECLARE_DESERIALIZE_TRAIT(ServerCapabilities)
DECLARE_DESERIALIZE_TRAIT(ClientCapabilities)
DECLARE_DESERIALIZE_TRAIT(RootsCapability)
DECLARE_DESERIALIZE_TRAIT(ResourcesCapability)
DECLARE_DESERIALIZE_TRAIT(PromptsCapability)
DECLARE_DESERIALIZE_TRAIT(EmptyCapability)
DECLARE_DESERIALIZE_TRAIT(SamplingParams)

// Implementation
// Implementation has inline specialization below

// Metadata
DECLARE_DESERIALIZE_TRAIT(Metadata)

// Request types
DECLARE_DESERIALIZE_TRAIT(InitializeRequest)
DECLARE_DESERIALIZE_TRAIT(PingRequest)
DECLARE_DESERIALIZE_TRAIT(CompleteRequest)
DECLARE_DESERIALIZE_TRAIT(SetLevelRequest)
DECLARE_DESERIALIZE_TRAIT(CallToolRequest)
DECLARE_DESERIALIZE_TRAIT(ListToolsRequest)
DECLARE_DESERIALIZE_TRAIT(GetPromptRequest)
DECLARE_DESERIALIZE_TRAIT(ListPromptsRequest)
DECLARE_DESERIALIZE_TRAIT(ReadResourceRequest)
DECLARE_DESERIALIZE_TRAIT(ListResourcesRequest)
DECLARE_DESERIALIZE_TRAIT(ListResourceTemplatesRequest)
DECLARE_DESERIALIZE_TRAIT(SubscribeRequest)
DECLARE_DESERIALIZE_TRAIT(UnsubscribeRequest)
DECLARE_DESERIALIZE_TRAIT(ListRootsRequest)
DECLARE_DESERIALIZE_TRAIT(CreateMessageRequest)
DECLARE_DESERIALIZE_TRAIT(ElicitRequest)

// Result types
DECLARE_DESERIALIZE_TRAIT(InitializeResult)
DECLARE_DESERIALIZE_TRAIT(CompleteResult)
DECLARE_DESERIALIZE_TRAIT(CallToolResult)
DECLARE_DESERIALIZE_TRAIT(ListToolsResult)
DECLARE_DESERIALIZE_TRAIT(GetPromptResult)
DECLARE_DESERIALIZE_TRAIT(ListPromptsResult)
DECLARE_DESERIALIZE_TRAIT(ReadResourceResult)
DECLARE_DESERIALIZE_TRAIT(ListResourcesResult)
DECLARE_DESERIALIZE_TRAIT(ListResourceTemplatesResult)
DECLARE_DESERIALIZE_TRAIT(ListRootsResult)
DECLARE_DESERIALIZE_TRAIT(CreateMessageResult)
DECLARE_DESERIALIZE_TRAIT(ElicitResult)

// Notification types
DECLARE_DESERIALIZE_TRAIT(CancelledNotification)
DECLARE_DESERIALIZE_TRAIT(ProgressNotification)
DECLARE_DESERIALIZE_TRAIT(InitializedNotification)
DECLARE_DESERIALIZE_TRAIT(RootsListChangedNotification)
DECLARE_DESERIALIZE_TRAIT(LoggingMessageNotification)
DECLARE_DESERIALIZE_TRAIT(ResourceUpdatedNotification)
DECLARE_DESERIALIZE_TRAIT(ResourceListChangedNotification)
DECLARE_DESERIALIZE_TRAIT(ToolListChangedNotification)
DECLARE_DESERIALIZE_TRAIT(PromptListChangedNotification)

// JSONRPC types - use fully qualified names
template<>
struct JsonDeserializeTraits<jsonrpc::Request> {
  static jsonrpc::Request deserialize(const JsonValue& json) {
    return JsonDeserializer::deserializeRequest(json);
  }
};

template<>
struct JsonDeserializeTraits<jsonrpc::Response> {
  static jsonrpc::Response deserialize(const JsonValue& json) {
    return JsonDeserializer::deserializeResponse(json);
  }
};

template<>
struct JsonDeserializeTraits<jsonrpc::ResponseResult> {
  static jsonrpc::ResponseResult deserialize(const JsonValue& json) {
    return JsonDeserializer::deserializeResponseResult(json);
  }
};

template<>
struct JsonDeserializeTraits<jsonrpc::Notification> {
  static jsonrpc::Notification deserialize(const JsonValue& json) {
    return JsonDeserializer::deserializeNotification(json);
  }
};

// Enum specializations with direct implementation
template<>
struct JsonDeserializeTraits<enums::Role::Value> {
  static enums::Role::Value deserialize(const JsonValue& json) {
    auto role_str = json.getString();
    auto role = enums::Role::from_string(role_str);
    if (!role.has_value()) {
      throw JsonException("Invalid role: " + role_str);
    }
    return role.value();
  }
};

template<>
struct JsonDeserializeTraits<enums::LoggingLevel::Value> {
  static enums::LoggingLevel::Value deserialize(const JsonValue& json) {
    auto level_str = json.getString();
    auto level = enums::LoggingLevel::from_string(level_str);
    if (!level.has_value()) {
      throw JsonException("Invalid logging level: " + level_str);
    }
    return level.value();
  }
};

// Implementation specialization
template<>
struct JsonDeserializeTraits<Implementation> {
  static Implementation deserialize(const JsonValue& json) {
    Implementation impl;
    impl.name = json.at("name").getString();
    impl.version = json.at("version").getString();
    return impl;
  }
};

// Clean up the macro
#undef DECLARE_DESERIALIZE_TRAIT

}  // namespace json
}  // namespace mcp