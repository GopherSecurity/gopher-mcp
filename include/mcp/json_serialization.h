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
  
  // ===== Helper Functions =====
  
  // Serialize enums
  static JsonValue serialize(enums::Role::Value role);
  static JsonValue serialize(enums::LoggingLevel::Value level);
  
  // Serialize pagination info
  static JsonValue serializePaginationInfo(const optional<Cursor>& cursor);
  
  // Serialize Implementation
  static JsonValue serialize(const Implementation& impl);
  
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
  
  // ===== Helper Functions =====
  
  // Deserialize enums
  static enums::Role::Value deserializeRole(const JsonValue& json);
  static enums::LoggingLevel::Value deserializeLoggingLevel(const JsonValue& json);
  
  // Deserialize pagination info
  static optional<Cursor> deserializeCursor(const JsonValue& json);
  
  // Deserialize Implementation
  static Implementation deserializeImplementation(const JsonValue& json);
  
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