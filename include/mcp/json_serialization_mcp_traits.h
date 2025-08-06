#pragma once
// MCP Type Serialization Traits
// This file contains trait specializations for all MCP types

namespace mcp {
namespace json {

// Forward declarations of the actual implementations
// These are defined in json_serialization.cc
namespace impl {
  JsonValue serialize_Error(const Error& value);
  Error deserialize_Error(const JsonValue& json);
  
  JsonValue serialize_ErrorData(const ErrorData& value);
  ErrorData deserialize_ErrorData(const JsonValue& json);
  
  JsonValue serialize_RequestId(const RequestId& value);
  RequestId deserialize_RequestId(const JsonValue& json);
  
  JsonValue serialize_TextContent(const TextContent& value);
  TextContent deserialize_TextContent(const JsonValue& json);
  
  JsonValue serialize_ImageContent(const ImageContent& value);
  ImageContent deserialize_ImageContent(const JsonValue& json);
  
  JsonValue serialize_AudioContent(const AudioContent& value);
  AudioContent deserialize_AudioContent(const JsonValue& json);
  
  JsonValue serialize_ResourceContent(const ResourceContent& value);
  
  JsonValue serialize_ContentBlock(const ContentBlock& value);
  ContentBlock deserialize_ContentBlock(const JsonValue& json);
  
  JsonValue serialize_ExtendedContentBlock(const ExtendedContentBlock& value);
  ExtendedContentBlock deserialize_ExtendedContentBlock(const JsonValue& json);
  
  JsonValue serialize_Tool(const Tool& value);
  Tool deserialize_Tool(const JsonValue& json);
  
  JsonValue serialize_Prompt(const Prompt& value);
  Prompt deserialize_Prompt(const JsonValue& json);
  
  JsonValue serialize_PromptMessage(const PromptMessage& value);
  PromptMessage deserialize_PromptMessage(const JsonValue& json);
  
  JsonValue serialize_Resource(const Resource& value);
  Resource deserialize_Resource(const JsonValue& json);
  
  JsonValue serialize_ResourceTemplate(const ResourceTemplate& value);
  ResourceTemplate deserialize_ResourceTemplate(const JsonValue& json);
  
  JsonValue serialize_Root(const Root& value);
  Root deserialize_Root(const JsonValue& json);
  
  JsonValue serialize_Message(const Message& value);
  Message deserialize_Message(const JsonValue& json);
  
  JsonValue serialize_SamplingMessage(const SamplingMessage& value);
  SamplingMessage deserialize_SamplingMessage(const JsonValue& json);
  
  JsonValue serialize_ModelPreferences(const ModelPreferences& value);
  ModelPreferences deserialize_ModelPreferences(const JsonValue& json);
  
  JsonValue serialize_ModelHint(const ModelHint& value);
  ModelHint deserialize_ModelHint(const JsonValue& json);
  
  JsonValue serialize_Annotations(const Annotations& value);
  Annotations deserialize_Annotations(const JsonValue& json);
  
  JsonValue serialize_ToolAnnotations(const ToolAnnotations& value);
  ToolAnnotations deserialize_ToolAnnotations(const JsonValue& json);
  
  JsonValue serialize_PromptReference(const PromptReference& value);
  PromptReference deserialize_PromptReference(const JsonValue& json);
  
  JsonValue serialize_ResourceTemplateReference(const ResourceTemplateReference& value);
  ResourceTemplateReference deserialize_ResourceTemplateReference(const JsonValue& json);
  
  JsonValue serialize_ServerCapabilities(const ServerCapabilities& value);
  ServerCapabilities deserialize_ServerCapabilities(const JsonValue& json);
  
  JsonValue serialize_ClientCapabilities(const ClientCapabilities& value);
  ClientCapabilities deserialize_ClientCapabilities(const JsonValue& json);
  
  JsonValue serialize_RootsCapability(const RootsCapability& value);
  RootsCapability deserialize_RootsCapability(const JsonValue& json);
  
  JsonValue serialize_ResourcesCapability(const ResourcesCapability& value);
  ResourcesCapability deserialize_ResourcesCapability(const JsonValue& json);
  
  JsonValue serialize_PromptsCapability(const PromptsCapability& value);
  PromptsCapability deserialize_PromptsCapability(const JsonValue& json);
  
  JsonValue serialize_EmptyCapability(const EmptyCapability& value);
  EmptyCapability deserialize_EmptyCapability(const JsonValue& json);
  
  JsonValue serialize_SamplingParams(const SamplingParams& value);
  SamplingParams deserialize_SamplingParams(const JsonValue& json);
  
  JsonValue serialize_ResourceContents(const ResourceContents& value);
  variant<TextResourceContents, BlobResourceContents> deserialize_ResourceContents(const JsonValue& json);
  
  JsonValue serialize_TextResourceContents(const TextResourceContents& value);
  TextResourceContents deserialize_TextResourceContents(const JsonValue& json);
  
  JsonValue serialize_BlobResourceContents(const BlobResourceContents& value);
  BlobResourceContents deserialize_BlobResourceContents(const JsonValue& json);
  
  // Request types
  JsonValue serialize_InitializeRequest(const InitializeRequest& value);
  InitializeRequest deserialize_InitializeRequest(const JsonValue& json);
  
  JsonValue serialize_PingRequest(const PingRequest& value);
  PingRequest deserialize_PingRequest(const JsonValue& json);
  
  JsonValue serialize_CompleteRequest(const CompleteRequest& value);
  CompleteRequest deserialize_CompleteRequest(const JsonValue& json);
  
  JsonValue serialize_SetLevelRequest(const SetLevelRequest& value);
  SetLevelRequest deserialize_SetLevelRequest(const JsonValue& json);
  
  JsonValue serialize_CallToolRequest(const CallToolRequest& value);
  CallToolRequest deserialize_CallToolRequest(const JsonValue& json);
  
  JsonValue serialize_ListToolsRequest(const ListToolsRequest& value);
  ListToolsRequest deserialize_ListToolsRequest(const JsonValue& json);
  
  JsonValue serialize_GetPromptRequest(const GetPromptRequest& value);
  GetPromptRequest deserialize_GetPromptRequest(const JsonValue& json);
  
  JsonValue serialize_ListPromptsRequest(const ListPromptsRequest& value);
  ListPromptsRequest deserialize_ListPromptsRequest(const JsonValue& json);
  
  JsonValue serialize_ReadResourceRequest(const ReadResourceRequest& value);
  ReadResourceRequest deserialize_ReadResourceRequest(const JsonValue& json);
  
  JsonValue serialize_ListResourcesRequest(const ListResourcesRequest& value);
  ListResourcesRequest deserialize_ListResourcesRequest(const JsonValue& json);
  
  JsonValue serialize_ListResourceTemplatesRequest(const ListResourceTemplatesRequest& value);
  ListResourceTemplatesRequest deserialize_ListResourceTemplatesRequest(const JsonValue& json);
  
  JsonValue serialize_SubscribeRequest(const SubscribeRequest& value);
  SubscribeRequest deserialize_SubscribeRequest(const JsonValue& json);
  
  JsonValue serialize_UnsubscribeRequest(const UnsubscribeRequest& value);
  UnsubscribeRequest deserialize_UnsubscribeRequest(const JsonValue& json);
  
  JsonValue serialize_ListRootsRequest(const ListRootsRequest& value);
  ListRootsRequest deserialize_ListRootsRequest(const JsonValue& json);
  
  JsonValue serialize_CreateMessageRequest(const CreateMessageRequest& value);
  CreateMessageRequest deserialize_CreateMessageRequest(const JsonValue& json);
  
  JsonValue serialize_ElicitRequest(const ElicitRequest& value);
  ElicitRequest deserialize_ElicitRequest(const JsonValue& json);
  
  // Result types
  JsonValue serialize_InitializeResult(const InitializeResult& value);
  InitializeResult deserialize_InitializeResult(const JsonValue& json);
  
  JsonValue serialize_CompleteResult(const CompleteResult& value);
  CompleteResult deserialize_CompleteResult(const JsonValue& json);
  
  JsonValue serialize_CallToolResult(const CallToolResult& value);
  CallToolResult deserialize_CallToolResult(const JsonValue& json);
  
  JsonValue serialize_ListToolsResult(const ListToolsResult& value);
  ListToolsResult deserialize_ListToolsResult(const JsonValue& json);
  
  JsonValue serialize_GetPromptResult(const GetPromptResult& value);
  GetPromptResult deserialize_GetPromptResult(const JsonValue& json);
  
  JsonValue serialize_ListPromptsResult(const ListPromptsResult& value);
  ListPromptsResult deserialize_ListPromptsResult(const JsonValue& json);
  
  JsonValue serialize_ReadResourceResult(const ReadResourceResult& value);
  ReadResourceResult deserialize_ReadResourceResult(const JsonValue& json);
  
  JsonValue serialize_ListResourcesResult(const ListResourcesResult& value);
  ListResourcesResult deserialize_ListResourcesResult(const JsonValue& json);
  
  JsonValue serialize_ListResourceTemplatesResult(const ListResourceTemplatesResult& value);
  ListResourceTemplatesResult deserialize_ListResourceTemplatesResult(const JsonValue& json);
  
  JsonValue serialize_ListRootsResult(const ListRootsResult& value);
  ListRootsResult deserialize_ListRootsResult(const JsonValue& json);
  
  JsonValue serialize_CreateMessageResult(const CreateMessageResult& value);
  CreateMessageResult deserialize_CreateMessageResult(const JsonValue& json);
  
  JsonValue serialize_ElicitResult(const ElicitResult& value);
  ElicitResult deserialize_ElicitResult(const JsonValue& json);
  
  // Notification types
  JsonValue serialize_CancelledNotification(const CancelledNotification& value);
  CancelledNotification deserialize_CancelledNotification(const JsonValue& json);
  
  JsonValue serialize_ProgressNotification(const ProgressNotification& value);
  ProgressNotification deserialize_ProgressNotification(const JsonValue& json);
  
  JsonValue serialize_InitializedNotification(const InitializedNotification& value);
  InitializedNotification deserialize_InitializedNotification(const JsonValue& json);
  
  JsonValue serialize_RootsListChangedNotification(const RootsListChangedNotification& value);
  RootsListChangedNotification deserialize_RootsListChangedNotification(const JsonValue& json);
  
  JsonValue serialize_LoggingMessageNotification(const LoggingMessageNotification& value);
  LoggingMessageNotification deserialize_LoggingMessageNotification(const JsonValue& json);
  
  JsonValue serialize_ResourceUpdatedNotification(const ResourceUpdatedNotification& value);
  ResourceUpdatedNotification deserialize_ResourceUpdatedNotification(const JsonValue& json);
  
  JsonValue serialize_ResourceListChangedNotification(const ResourceListChangedNotification& value);
  ResourceListChangedNotification deserialize_ResourceListChangedNotification(const JsonValue& json);
  
  JsonValue serialize_ToolListChangedNotification(const ToolListChangedNotification& value);
  ToolListChangedNotification deserialize_ToolListChangedNotification(const JsonValue& json);
  
  JsonValue serialize_PromptListChangedNotification(const PromptListChangedNotification& value);
  PromptListChangedNotification deserialize_PromptListChangedNotification(const JsonValue& json);
  
  // JSONRPC types
  JsonValue serialize_Request(const jsonrpc::Request& value);
  jsonrpc::Request deserialize_Request(const JsonValue& json);
  
  JsonValue serialize_Response(const jsonrpc::Response& value);
  jsonrpc::Response deserialize_Response(const JsonValue& json);
  
  JsonValue serialize_ResponseResult(const jsonrpc::ResponseResult& value);
  jsonrpc::ResponseResult deserialize_ResponseResult(const JsonValue& json);
  
  JsonValue serialize_Notification(const jsonrpc::Notification& value);
  jsonrpc::Notification deserialize_Notification(const JsonValue& json);
  
  // Special deserialization
  ResourceLink deserialize_ResourceLink(const JsonValue& json);
  EmbeddedResource deserialize_EmbeddedResource(const JsonValue& json);
}

// ============ TRAIT SPECIALIZATIONS ============

#define SERIALIZE_TRAIT(Type) \
  template<> struct JsonSerializeTraits<Type> { \
    static JsonValue serialize(const Type& value) { \
      return impl::serialize_##Type(value); \
    } \
  };

#define DESERIALIZE_TRAIT(Type) \
  template<> struct JsonDeserializeTraits<Type> { \
    static Type deserialize(const JsonValue& json) { \
      return impl::deserialize_##Type(json); \
    } \
  };

// Error types
SERIALIZE_TRAIT(Error)
DESERIALIZE_TRAIT(Error)
SERIALIZE_TRAIT(ErrorData)
DESERIALIZE_TRAIT(ErrorData)

// RequestId
SERIALIZE_TRAIT(RequestId)
DESERIALIZE_TRAIT(RequestId)

// Content types
SERIALIZE_TRAIT(TextContent)
DESERIALIZE_TRAIT(TextContent)
SERIALIZE_TRAIT(ImageContent)
DESERIALIZE_TRAIT(ImageContent)
SERIALIZE_TRAIT(AudioContent)
DESERIALIZE_TRAIT(AudioContent)
SERIALIZE_TRAIT(ResourceContent)
SERIALIZE_TRAIT(ContentBlock)
DESERIALIZE_TRAIT(ContentBlock)
SERIALIZE_TRAIT(ExtendedContentBlock)
DESERIALIZE_TRAIT(ExtendedContentBlock)

// Core types
SERIALIZE_TRAIT(Tool)
DESERIALIZE_TRAIT(Tool)
SERIALIZE_TRAIT(Prompt)
DESERIALIZE_TRAIT(Prompt)
SERIALIZE_TRAIT(PromptMessage)
DESERIALIZE_TRAIT(PromptMessage)
SERIALIZE_TRAIT(Resource)
DESERIALIZE_TRAIT(Resource)
SERIALIZE_TRAIT(ResourceTemplate)
DESERIALIZE_TRAIT(ResourceTemplate)
SERIALIZE_TRAIT(Root)
DESERIALIZE_TRAIT(Root)
SERIALIZE_TRAIT(Message)
DESERIALIZE_TRAIT(Message)
SERIALIZE_TRAIT(SamplingMessage)
DESERIALIZE_TRAIT(SamplingMessage)
SERIALIZE_TRAIT(ModelPreferences)
DESERIALIZE_TRAIT(ModelPreferences)
SERIALIZE_TRAIT(ModelHint)
DESERIALIZE_TRAIT(ModelHint)
SERIALIZE_TRAIT(Annotations)
DESERIALIZE_TRAIT(Annotations)
SERIALIZE_TRAIT(ToolAnnotations)
DESERIALIZE_TRAIT(ToolAnnotations)
SERIALIZE_TRAIT(PromptReference)
DESERIALIZE_TRAIT(PromptReference)
SERIALIZE_TRAIT(ResourceTemplateReference)
DESERIALIZE_TRAIT(ResourceTemplateReference)

// Capabilities
SERIALIZE_TRAIT(ServerCapabilities)
DESERIALIZE_TRAIT(ServerCapabilities)
SERIALIZE_TRAIT(ClientCapabilities)
DESERIALIZE_TRAIT(ClientCapabilities)
SERIALIZE_TRAIT(RootsCapability)
DESERIALIZE_TRAIT(RootsCapability)
SERIALIZE_TRAIT(ResourcesCapability)
DESERIALIZE_TRAIT(ResourcesCapability)
SERIALIZE_TRAIT(PromptsCapability)
DESERIALIZE_TRAIT(PromptsCapability)
SERIALIZE_TRAIT(EmptyCapability)
DESERIALIZE_TRAIT(EmptyCapability)
SERIALIZE_TRAIT(SamplingParams)
DESERIALIZE_TRAIT(SamplingParams)

// Resource contents - note ResourceContents is a variant type
SERIALIZE_TRAIT(ResourceContents)
template<> struct JsonDeserializeTraits<variant<TextResourceContents, BlobResourceContents>> {
  static variant<TextResourceContents, BlobResourceContents> deserialize(const JsonValue& json) {
    return impl::deserialize_ResourceContents(json);
  }
};
SERIALIZE_TRAIT(TextResourceContents)
DESERIALIZE_TRAIT(TextResourceContents)
SERIALIZE_TRAIT(BlobResourceContents)
DESERIALIZE_TRAIT(BlobResourceContents)

// Request types
SERIALIZE_TRAIT(InitializeRequest)
DESERIALIZE_TRAIT(InitializeRequest)
SERIALIZE_TRAIT(PingRequest)
DESERIALIZE_TRAIT(PingRequest)
SERIALIZE_TRAIT(CompleteRequest)
DESERIALIZE_TRAIT(CompleteRequest)
SERIALIZE_TRAIT(SetLevelRequest)
DESERIALIZE_TRAIT(SetLevelRequest)
SERIALIZE_TRAIT(CallToolRequest)
DESERIALIZE_TRAIT(CallToolRequest)
SERIALIZE_TRAIT(ListToolsRequest)
DESERIALIZE_TRAIT(ListToolsRequest)
SERIALIZE_TRAIT(GetPromptRequest)
DESERIALIZE_TRAIT(GetPromptRequest)
SERIALIZE_TRAIT(ListPromptsRequest)
DESERIALIZE_TRAIT(ListPromptsRequest)
SERIALIZE_TRAIT(ReadResourceRequest)
DESERIALIZE_TRAIT(ReadResourceRequest)
SERIALIZE_TRAIT(ListResourcesRequest)
DESERIALIZE_TRAIT(ListResourcesRequest)
SERIALIZE_TRAIT(ListResourceTemplatesRequest)
DESERIALIZE_TRAIT(ListResourceTemplatesRequest)
SERIALIZE_TRAIT(SubscribeRequest)
DESERIALIZE_TRAIT(SubscribeRequest)
SERIALIZE_TRAIT(UnsubscribeRequest)
DESERIALIZE_TRAIT(UnsubscribeRequest)
SERIALIZE_TRAIT(ListRootsRequest)
DESERIALIZE_TRAIT(ListRootsRequest)
SERIALIZE_TRAIT(CreateMessageRequest)
DESERIALIZE_TRAIT(CreateMessageRequest)
SERIALIZE_TRAIT(ElicitRequest)
DESERIALIZE_TRAIT(ElicitRequest)

// Result types
SERIALIZE_TRAIT(InitializeResult)
DESERIALIZE_TRAIT(InitializeResult)
SERIALIZE_TRAIT(CompleteResult)
DESERIALIZE_TRAIT(CompleteResult)
SERIALIZE_TRAIT(CallToolResult)
DESERIALIZE_TRAIT(CallToolResult)
SERIALIZE_TRAIT(ListToolsResult)
DESERIALIZE_TRAIT(ListToolsResult)
SERIALIZE_TRAIT(GetPromptResult)
DESERIALIZE_TRAIT(GetPromptResult)
SERIALIZE_TRAIT(ListPromptsResult)
DESERIALIZE_TRAIT(ListPromptsResult)
SERIALIZE_TRAIT(ReadResourceResult)
DESERIALIZE_TRAIT(ReadResourceResult)
SERIALIZE_TRAIT(ListResourcesResult)
DESERIALIZE_TRAIT(ListResourcesResult)
SERIALIZE_TRAIT(ListResourceTemplatesResult)
DESERIALIZE_TRAIT(ListResourceTemplatesResult)
SERIALIZE_TRAIT(ListRootsResult)
DESERIALIZE_TRAIT(ListRootsResult)
SERIALIZE_TRAIT(CreateMessageResult)
DESERIALIZE_TRAIT(CreateMessageResult)
SERIALIZE_TRAIT(ElicitResult)
DESERIALIZE_TRAIT(ElicitResult)

// Notification types
SERIALIZE_TRAIT(CancelledNotification)
DESERIALIZE_TRAIT(CancelledNotification)
SERIALIZE_TRAIT(ProgressNotification)
DESERIALIZE_TRAIT(ProgressNotification)
SERIALIZE_TRAIT(InitializedNotification)
DESERIALIZE_TRAIT(InitializedNotification)
SERIALIZE_TRAIT(RootsListChangedNotification)
DESERIALIZE_TRAIT(RootsListChangedNotification)
SERIALIZE_TRAIT(LoggingMessageNotification)
DESERIALIZE_TRAIT(LoggingMessageNotification)
SERIALIZE_TRAIT(ResourceUpdatedNotification)
DESERIALIZE_TRAIT(ResourceUpdatedNotification)
SERIALIZE_TRAIT(ResourceListChangedNotification)
DESERIALIZE_TRAIT(ResourceListChangedNotification)
SERIALIZE_TRAIT(ToolListChangedNotification)
DESERIALIZE_TRAIT(ToolListChangedNotification)
SERIALIZE_TRAIT(PromptListChangedNotification)
DESERIALIZE_TRAIT(PromptListChangedNotification)

// JSONRPC types
template<> struct JsonSerializeTraits<jsonrpc::Request> {
  static JsonValue serialize(const jsonrpc::Request& value) {
    return impl::serialize_Request(value);
  }
};
template<> struct JsonDeserializeTraits<jsonrpc::Request> {
  static jsonrpc::Request deserialize(const JsonValue& json) {
    return impl::deserialize_Request(json);
  }
};

template<> struct JsonSerializeTraits<jsonrpc::Response> {
  static JsonValue serialize(const jsonrpc::Response& value) {
    return impl::serialize_Response(value);
  }
};
template<> struct JsonDeserializeTraits<jsonrpc::Response> {
  static jsonrpc::Response deserialize(const JsonValue& json) {
    return impl::deserialize_Response(json);
  }
};

template<> struct JsonSerializeTraits<jsonrpc::ResponseResult> {
  static JsonValue serialize(const jsonrpc::ResponseResult& value) {
    return impl::serialize_ResponseResult(value);
  }
};
template<> struct JsonDeserializeTraits<jsonrpc::ResponseResult> {
  static jsonrpc::ResponseResult deserialize(const JsonValue& json) {
    return impl::deserialize_ResponseResult(json);
  }
};

template<> struct JsonSerializeTraits<jsonrpc::Notification> {
  static JsonValue serialize(const jsonrpc::Notification& value) {
    return impl::serialize_Notification(value);
  }
};
template<> struct JsonDeserializeTraits<jsonrpc::Notification> {
  static jsonrpc::Notification deserialize(const JsonValue& json) {
    return impl::deserialize_Notification(json);
  }
};

// Special deserialization for variant types
template<> struct JsonDeserializeTraits<ResourceLink> {
  static ResourceLink deserialize(const JsonValue& json) {
    return impl::deserialize_ResourceLink(json);
  }
};

template<> struct JsonDeserializeTraits<EmbeddedResource> {
  static EmbeddedResource deserialize(const JsonValue& json) {
    return impl::deserialize_EmbeddedResource(json);
  }
};

#undef SERIALIZE_TRAIT
#undef DESERIALIZE_TRAIT

}  // namespace json
}  // namespace mcp