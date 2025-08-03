#ifndef MCP_JSON_H
#define MCP_JSON_H

// This header provides JSON serialization for MCP types
// Include this AFTER types.h to ensure all types are defined

#include <nlohmann/json.hpp>

#include "mcp/types.h"

namespace mcp {

using json = nlohmann::json;

// Basic content types
void to_json(json& j, const TextContent& content);
void from_json(const json& j, TextContent& content);

void to_json(json& j, const ImageContent& content);
void from_json(const json& j, ImageContent& content);

void to_json(json& j, const AudioContent& content);
void from_json(const json& j, AudioContent& content);

void to_json(json& j, const Resource& resource);
void from_json(const json& j, Resource& resource);

void to_json(json& j, const ResourceContent& content);
void from_json(const json& j, ResourceContent& content);

void to_json(json& j, const ResourceLink& link);
void from_json(const json& j, ResourceLink& link);

void to_json(json& j, const EmbeddedResource& resource);
void from_json(const json& j, EmbeddedResource& resource);

// Tool and prompt types
void to_json(json& j, const Tool& tool);
void from_json(const json& j, Tool& tool);

void to_json(json& j, const Prompt& prompt);
void from_json(const json& j, Prompt& prompt);

void to_json(json& j, const PromptArgument& arg);
void from_json(const json& j, PromptArgument& arg);

// Error and result types
void to_json(json& j, const Error& err);
void from_json(const json& j, Error& err);

// Capability types
void to_json(json& j, const ServerCapabilities& caps);
void from_json(const json& j, ServerCapabilities& caps);

void to_json(json& j, const ClientCapabilities& caps);
void from_json(const json& j, ClientCapabilities& caps);

void to_json(json& j, const ResourcesCapability& caps);
void from_json(const json& j, ResourcesCapability& caps);

void to_json(json& j, const RootsCapability& caps);
void from_json(const json& j, RootsCapability& caps);

// Content blocks (variants)
void to_json(json& j, const ContentBlock& block);
void from_json(const json& j, ContentBlock& block);

void to_json(json& j, const ExtendedContentBlock& block);
void from_json(const json& j, ExtendedContentBlock& block);

// Annotations and metadata
void to_json(json& j, const Annotations& ann);
void from_json(const json& j, Annotations& ann);

void to_json(json& j, const BaseMetadata& meta);
void from_json(const json& j, BaseMetadata& meta);

// Implementation info
void to_json(json& j, const Implementation& impl);
void from_json(const json& j, Implementation& impl);

// Protocol message types
void to_json(json& j, const InitializeRequest& req);
void from_json(const json& j, InitializeRequest& req);

void to_json(json& j, const InitializeResult& result);
void from_json(const json& j, InitializeResult& result);

void to_json(json& j, const CallToolRequest& req);
void from_json(const json& j, CallToolRequest& req);

void to_json(json& j, const CallToolResult& result);
void from_json(const json& j, CallToolResult& result);

void to_json(json& j, const PromptMessage& msg);
void from_json(const json& j, PromptMessage& msg);

void to_json(json& j, const SamplingMessage& msg);
void from_json(const json& j, SamplingMessage& msg);

// Resource types
void to_json(json& j, const ResourceTemplate& tmpl);
void from_json(const json& j, ResourceTemplate& tmpl);

void to_json(json& j, const TextResourceContents& contents);
void from_json(const json& j, TextResourceContents& contents);

void to_json(json& j, const BlobResourceContents& contents);
void from_json(const json& j, BlobResourceContents& contents);

// Root type
void to_json(json& j, const Root& root);
void from_json(const json& j, Root& root);

// Model preferences
void to_json(json& j, const ModelPreferences& prefs);
void from_json(const json& j, ModelPreferences& prefs);

void to_json(json& j, const ModelHint& hint);
void from_json(const json& j, ModelHint& hint);

// Message results
void to_json(json& j, const CreateMessageResult& result);
void from_json(const json& j, CreateMessageResult& result);

// JSON-RPC types
void to_json(json& j, const jsonrpc::Request& req);
void from_json(const json& j, jsonrpc::Request& req);

void to_json(json& j, const jsonrpc::Response& resp);
void from_json(const json& j, jsonrpc::Response& resp);

void to_json(json& j, const jsonrpc::Notification& notif);
void from_json(const json& j, jsonrpc::Notification& notif);

// Enum serialization
void to_json(json& j, const enums::Role::Value& role);
void from_json(const json& j, enums::Role::Value& role);

void to_json(json& j, const enums::LoggingLevel::Value& level);
void from_json(const json& j, enums::LoggingLevel::Value& level);

// Variant types (RequestId, ProgressToken)
void to_json(json& j, const RequestId& id);
void from_json(const json& j, RequestId& id);

void to_json(json& j, const ProgressToken& token);
void from_json(const json& j, ProgressToken& token);

// Metadata
void to_json(json& j, const Metadata& metadata);
void from_json(const json& j, Metadata& metadata);

// Sampling parameters
void to_json(json& j, const SamplingParams& params);
void from_json(const json& j, SamplingParams& params);

// Base types for pagination
void to_json(json& j, const PaginatedRequest& req);
void from_json(const json& j, PaginatedRequest& req);

void to_json(json& j, const PaginatedResult& result);
void from_json(const json& j, PaginatedResult& result);

// Request/Response messages
void to_json(json& j, const PingRequest& req);
void from_json(const json& j, PingRequest& req);

void to_json(json& j, const ListResourcesRequest& req);
void from_json(const json& j, ListResourcesRequest& req);

void to_json(json& j, const ListResourcesResult& result);
void from_json(const json& j, ListResourcesResult& result);

void to_json(json& j, const ReadResourceRequest& req);
void from_json(const json& j, ReadResourceRequest& req);

void to_json(json& j, const ReadResourceResult& result);
void from_json(const json& j, ReadResourceResult& result);

void to_json(json& j, const SubscribeRequest& req);
void from_json(const json& j, SubscribeRequest& req);

void to_json(json& j, const UnsubscribeRequest& req);
void from_json(const json& j, UnsubscribeRequest& req);

void to_json(json& j, const ListPromptsRequest& req);
void from_json(const json& j, ListPromptsRequest& req);

void to_json(json& j, const ListPromptsResult& result);
void from_json(const json& j, ListPromptsResult& result);

void to_json(json& j, const GetPromptRequest& req);
void from_json(const json& j, GetPromptRequest& req);

void to_json(json& j, const GetPromptResult& result);
void from_json(const json& j, GetPromptResult& result);

void to_json(json& j, const ListToolsRequest& req);
void from_json(const json& j, ListToolsRequest& req);

void to_json(json& j, const ListToolsResult& result);
void from_json(const json& j, ListToolsResult& result);

void to_json(json& j, const SetLevelRequest& req);
void from_json(const json& j, SetLevelRequest& req);

void to_json(json& j, const CompleteRequest& req);
void from_json(const json& j, CompleteRequest& req);

void to_json(json& j, const CompleteResult& result);
void from_json(const json& j, CompleteResult& result);

void to_json(json& j, const CompleteResult::Completion& completion);
void from_json(const json& j, CompleteResult::Completion& completion);

void to_json(json& j, const ListRootsRequest& req);
void from_json(const json& j, ListRootsRequest& req);

void to_json(json& j, const ListRootsResult& result);
void from_json(const json& j, ListRootsResult& result);

void to_json(json& j, const CreateMessageRequest& req);
void from_json(const json& j, CreateMessageRequest& req);

void to_json(json& j, const ElicitRequest& req);
void from_json(const json& j, ElicitRequest& req);

void to_json(json& j, const ElicitResult& result);
void from_json(const json& j, ElicitResult& result);

// Notification messages
void to_json(json& j, const InitializedNotification& notif);
void from_json(const json& j, InitializedNotification& notif);

void to_json(json& j, const ProgressNotification& notif);
void from_json(const json& j, ProgressNotification& notif);

void to_json(json& j, const CancelledNotification& notif);
void from_json(const json& j, CancelledNotification& notif);

void to_json(json& j, const ResourceListChangedNotification& notif);
void from_json(const json& j, ResourceListChangedNotification& notif);

void to_json(json& j, const ResourceUpdatedNotification& notif);
void from_json(const json& j, ResourceUpdatedNotification& notif);

void to_json(json& j, const PromptListChangedNotification& notif);
void from_json(const json& j, PromptListChangedNotification& notif);

void to_json(json& j, const ToolListChangedNotification& notif);
void from_json(const json& j, ToolListChangedNotification& notif);

void to_json(json& j, const LoggingMessageNotification& notif);
void from_json(const json& j, LoggingMessageNotification& notif);

void to_json(json& j, const RootsListChangedNotification& notif);
void from_json(const json& j, RootsListChangedNotification& notif);

// Schema types
void to_json(json& j, const StringSchema& schema);
void from_json(const json& j, StringSchema& schema);

void to_json(json& j, const NumberSchema& schema);
void from_json(const json& j, NumberSchema& schema);

void to_json(json& j, const BooleanSchema& schema);
void from_json(const json& j, BooleanSchema& schema);

void to_json(json& j, const EnumSchema& schema);
void from_json(const json& j, EnumSchema& schema);

void to_json(json& j, const PrimitiveSchemaDefinition& def);
void from_json(const json& j, PrimitiveSchemaDefinition& def);

// Other types
void to_json(json& j, const Message& msg);
void from_json(const json& j, Message& msg);

void to_json(json& j, const ToolAnnotations& ann);
void from_json(const json& j, ToolAnnotations& ann);

void to_json(json& j, const PromptsCapability& cap);
void from_json(const json& j, PromptsCapability& cap);

void to_json(json& j, const EmptyResult& result);
void from_json(const json& j, EmptyResult& result);

void to_json(json& j, const EmptyCapability& cap);
void from_json(const json& j, EmptyCapability& cap);

void to_json(json& j, const ResourceContents& contents);
void from_json(const json& j, ResourceContents& contents);

void to_json(json& j, const PromptReference& ref);
void from_json(const json& j, PromptReference& ref);

void to_json(json& j, const ResourceTemplateReference& ref);
void from_json(const json& j, ResourceTemplateReference& ref);

void to_json(json& j, const ToolParameter& param);
void from_json(const json& j, ToolParameter& param);

void to_json(json& j, const ToolInputSchema& schema);
void from_json(const json& j, ToolInputSchema& schema);

void to_json(json& j, const InitializeParams& params);
void from_json(const json& j, InitializeParams& params);

}  // namespace mcp

#endif  // MCP_JSON_H