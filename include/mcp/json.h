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

}  // namespace mcp

#endif  // MCP_JSON_H