#ifndef MCP_BUILDERS_H
#define MCP_BUILDERS_H

#include <type_traits>
#include <utility>

#include "mcp/types.h"

namespace mcp {

// Base builder template using CRTP
template <typename T, typename Derived>
class Builder {
 protected:
  T value_;

  Derived& self() { return static_cast<Derived&>(*this); }
  const Derived& self() const { return static_cast<const Derived&>(*this); }

 public:
  Builder() = default;
  explicit Builder(T&& value) : value_(std::move(value)) {}
  explicit Builder(const T& value) : value_(value) {}

  T build() && { return std::move(value_); }
  T build() const& { return value_; }

  operator T() && { return std::move(value_); }
  operator T() const& { return value_; }
};

// Resource Builder (moved from types.h)
class ResourceBuilder : public Builder<Resource, ResourceBuilder> {
 public:
  ResourceBuilder(const std::string& uri, const std::string& name) {
    value_.uri = uri;
    value_.name = name;
  }

  explicit ResourceBuilder(const std::string& uri) {
    value_.uri = uri;
    value_.name = "";  // Default empty name
  }

  ResourceBuilder& name(const std::string& n) {
    value_.name = n;
    return *this;
  }

  ResourceBuilder& description(const std::string& desc) {
    value_.description = mcp::make_optional(desc);
    return *this;
  }

  ResourceBuilder& mimeType(const std::string& mime) {
    value_.mimeType = mcp::make_optional(mime);
    return *this;
  }
};

// Tool Builder (moved from types.h)
class ToolBuilder : public Builder<Tool, ToolBuilder> {
 public:
  explicit ToolBuilder(const std::string& name) { value_.name = name; }

  ToolBuilder& description(const std::string& desc) {
    value_.description = mcp::make_optional(desc);
    return *this;
  }

  ToolBuilder& inputSchema(const ToolInputSchema& schema) {
    value_.inputSchema = mcp::make_optional(schema);
    return *this;
  }

  ToolBuilder& parameter(const std::string& name,
                         const std::string& type,
                         bool required = false) {
    if (!value_.parameters) {
      value_.parameters = mcp::make_optional(std::vector<ToolParameter>());
    }
    value_.parameters->push_back(ToolParameter{name, type, nullopt, required});
    return *this;
  }

  ToolBuilder& parameter(const std::string& name,
                         const std::string& type,
                         const std::string& desc,
                         bool required = false) {
    if (!value_.parameters) {
      value_.parameters = mcp::make_optional(std::vector<ToolParameter>());
    }
    value_.parameters->push_back(
        ToolParameter{name, type, mcp::make_optional(desc), required});
    return *this;
  }
};

// SamplingParams Builder (moved from types.h)
class SamplingParamsBuilder
    : public Builder<SamplingParams, SamplingParamsBuilder> {
 public:
  SamplingParamsBuilder() = default;

  SamplingParamsBuilder& temperature(double temp) {
    value_.temperature = mcp::make_optional(temp);
    return *this;
  }

  SamplingParamsBuilder& maxTokens(int max) {
    value_.maxTokens = mcp::make_optional(max);
    return *this;
  }

  SamplingParamsBuilder& stopSequence(const std::string& seq) {
    if (!value_.stopSequences) {
      value_.stopSequences = mcp::make_optional(std::vector<std::string>());
    }
    value_.stopSequences->push_back(seq);
    return *this;
  }

  SamplingParamsBuilder& metadata(const std::string& key,
                                  const std::string& val) {
    if (!value_.metadata) {
      value_.metadata = mcp::make_optional(Metadata());
    }
    add_metadata(*value_.metadata, key, val);
    return *this;
  }

  template <typename T>
  SamplingParamsBuilder& metadata(const std::string& key, T&& val) {
    if (!value_.metadata) {
      value_.metadata = mcp::make_optional(Metadata());
    }
    add_metadata(*value_.metadata, key, std::forward<T>(val));
    return *this;
  }
};

// EmbeddedResource Builder (moved from types.h)
class EmbeddedResourceBuilder
    : public Builder<EmbeddedResource, EmbeddedResourceBuilder> {
 public:
  explicit EmbeddedResourceBuilder(const Resource& r) { value_.resource = r; }

  EmbeddedResourceBuilder& add_content(const ContentBlock& content) {
    value_.content.push_back(content);
    return *this;
  }

  EmbeddedResourceBuilder& add_text(const std::string& text) {
    value_.content.push_back(make_text_content(text));
    return *this;
  }

  EmbeddedResourceBuilder& add_image(const std::string& data,
                                     const std::string& mime_type) {
    value_.content.push_back(make_image_content(data, mime_type));
    return *this;
  }
};

// ModelPreferences Builder (moved from types.h)
class ModelPreferencesBuilder
    : public Builder<ModelPreferences, ModelPreferencesBuilder> {
 public:
  ModelPreferencesBuilder() = default;

  ModelPreferencesBuilder& add_hint(const std::string& model_name) {
    if (!value_.hints) {
      value_.hints = mcp::make_optional(std::vector<ModelHint>());
    }
    value_.hints->push_back(ModelHint(model_name));
    return *this;
  }

  ModelPreferencesBuilder& cost_priority(double priority) {
    value_.costPriority = mcp::make_optional(priority);
    return *this;
  }

  ModelPreferencesBuilder& speed_priority(double priority) {
    value_.speedPriority = mcp::make_optional(priority);
    return *this;
  }

  ModelPreferencesBuilder& intelligence_priority(double priority) {
    value_.intelligencePriority = mcp::make_optional(priority);
    return *this;
  }
};

// StringSchema Builder (moved from types.h)
class StringSchemaBuilder : public Builder<StringSchema, StringSchemaBuilder> {
 public:
  StringSchemaBuilder() = default;

  StringSchemaBuilder& description(const std::string& desc) {
    value_.description = mcp::make_optional(desc);
    return *this;
  }

  StringSchemaBuilder& pattern(const std::string& regex) {
    value_.pattern = mcp::make_optional(regex);
    return *this;
  }

  StringSchemaBuilder& min_length(int len) {
    value_.minLength = mcp::make_optional(len);
    return *this;
  }

  StringSchemaBuilder& max_length(int len) {
    value_.maxLength = mcp::make_optional(len);
    return *this;
  }

  StringSchemaBuilder& minLength(int len) {
    value_.minLength = mcp::make_optional(len);
    return *this;
  }

  StringSchemaBuilder& maxLength(int len) {
    value_.maxLength = mcp::make_optional(len);
    return *this;
  }
};

// ClientCapabilities Builder (moved from types.h)
class ClientCapabilitiesBuilder
    : public Builder<ClientCapabilities, ClientCapabilitiesBuilder> {
 public:
  ClientCapabilitiesBuilder() = default;

  ClientCapabilitiesBuilder& experimental(const Metadata& metadata) {
    value_.experimental = mcp::make_optional(metadata);
    return *this;
  }

  ClientCapabilitiesBuilder& sampling(const SamplingParams& params) {
    value_.sampling = mcp::make_optional(params);
    return *this;
  }

  ClientCapabilitiesBuilder& resources(bool enabled) {
    if (!value_.experimental) {
      value_.experimental = mcp::make_optional(Metadata());
    }
    add_metadata(*value_.experimental, "resources", enabled);
    return *this;
  }

  ClientCapabilitiesBuilder& tools(bool enabled) {
    if (!value_.experimental) {
      value_.experimental = mcp::make_optional(Metadata());
    }
    add_metadata(*value_.experimental, "tools", enabled);
    return *this;
  }
};

// ServerCapabilities Builder (moved from types.h)
class ServerCapabilitiesBuilder
    : public Builder<ServerCapabilities, ServerCapabilitiesBuilder> {
 public:
  ServerCapabilitiesBuilder() = default;

  ServerCapabilitiesBuilder& experimental(const Metadata& metadata) {
    value_.experimental = mcp::make_optional(metadata);
    return *this;
  }

  ServerCapabilitiesBuilder& resources(bool enabled) {
    value_.resources =
        mcp::make_optional(variant<bool, ResourcesCapability>(enabled));
    return *this;
  }

  ServerCapabilitiesBuilder& resources(const ResourcesCapability& res_caps) {
    value_.resources =
        mcp::make_optional(variant<bool, ResourcesCapability>(res_caps));
    return *this;
  }

  ServerCapabilitiesBuilder& tools(bool enabled) {
    value_.tools = mcp::make_optional(enabled);
    return *this;
  }

  ServerCapabilitiesBuilder& prompts(bool enabled) {
    value_.prompts = mcp::make_optional(enabled);
    return *this;
  }

  ServerCapabilitiesBuilder& logging(bool enabled) {
    value_.logging = mcp::make_optional(enabled);
    return *this;
  }
};

// CreateMessageRequest Builder (moved from types.h)
class CreateMessageRequestBuilder
    : public Builder<CreateMessageRequest, CreateMessageRequestBuilder> {
 public:
  CreateMessageRequestBuilder() = default;

  CreateMessageRequestBuilder& add_message(const SamplingMessage& msg) {
    value_.messages.push_back(msg);
    return *this;
  }

  CreateMessageRequestBuilder& add_user_message(const std::string& text) {
    SamplingMessage msg;
    msg.role = enums::Role::USER;
    msg.content = TextContent(text);
    value_.messages.push_back(msg);
    return *this;
  }

  CreateMessageRequestBuilder& add_assistant_message(const std::string& text) {
    SamplingMessage msg;
    msg.role = enums::Role::ASSISTANT;
    msg.content = TextContent(text);
    value_.messages.push_back(msg);
    return *this;
  }

  CreateMessageRequestBuilder& modelPreferences(const ModelPreferences& prefs) {
    value_.modelPreferences = mcp::make_optional(prefs);
    return *this;
  }

  CreateMessageRequestBuilder& systemPrompt(const std::string& prompt) {
    value_.systemPrompt = mcp::make_optional(prompt);
    return *this;
  }

  CreateMessageRequestBuilder& temperature(double temp) {
    value_.temperature = mcp::make_optional(temp);
    return *this;
  }

  CreateMessageRequestBuilder& maxTokens(int tokens) {
    value_.maxTokens = mcp::make_optional(tokens);
    return *this;
  }

  CreateMessageRequestBuilder& stopSequence(const std::string& seq) {
    if (!value_.stopSequences) {
      value_.stopSequences = mcp::make_optional(std::vector<std::string>());
    }
    value_.stopSequences->push_back(seq);
    return *this;
  }

  CreateMessageRequestBuilder& includeContext(const Metadata& context) {
    value_.includeContext = mcp::make_optional(context);
    return *this;
  }

  CreateMessageRequestBuilder& metadata(const Metadata& meta) {
    value_.metadata = mcp::make_optional(meta);
    return *this;
  }
};

// InitializeParams Builder (moved from types.h)
class InitializeParamsBuilder
    : public Builder<InitializeParams, InitializeParamsBuilder> {
 public:
  explicit InitializeParamsBuilder(const std::string& version) {
    value_.protocolVersion = version;
  }

  InitializeParamsBuilder& clientName(const std::string& name) {
    value_.clientName = mcp::make_optional(name);
    return *this;
  }

  InitializeParamsBuilder& clientVersion(const std::string& version) {
    value_.clientVersion = mcp::make_optional(version);
    return *this;
  }

  InitializeParamsBuilder& capability(const std::string& key, bool val) {
    if (!value_.capabilities) {
      value_.capabilities = mcp::make_optional(Metadata());
    }
    add_metadata(*value_.capabilities, key, val);
    return *this;
  }

  template <typename T>
  InitializeParamsBuilder& capability(const std::string& key, T&& val) {
    if (!value_.capabilities) {
      value_.capabilities = mcp::make_optional(Metadata());
    }
    add_metadata(*value_.capabilities, key, std::forward<T>(val));
    return *this;
  }
};

// Additional builders for template-based API

// TextContent Builder
class TextContentBuilder : public Builder<TextContent, TextContentBuilder> {
 public:
  explicit TextContentBuilder(const std::string& text) { value_.text = text; }

  TextContentBuilder& annotations(const Annotations& ann) {
    value_.annotations = mcp::make_optional(ann);
    return *this;
  }

  TextContentBuilder& audience(const std::vector<enums::Role::Value>& aud) {
    if (!value_.annotations) {
      value_.annotations = mcp::make_optional(Annotations());
    }
    value_.annotations->audience = mcp::make_optional(aud);
    return *this;
  }

  TextContentBuilder& priority(double p) {
    if (!value_.annotations) {
      value_.annotations = mcp::make_optional(Annotations());
    }
    value_.annotations->priority = mcp::make_optional(p);
    return *this;
  }
};

// ImageContent Builder
class ImageContentBuilder : public Builder<ImageContent, ImageContentBuilder> {
 public:
  ImageContentBuilder(const std::string& data, const std::string& mimeType) {
    value_.data = data;
    value_.mimeType = mimeType;
  }
};

// AudioContent Builder
class AudioContentBuilder : public Builder<AudioContent, AudioContentBuilder> {
 public:
  AudioContentBuilder(const std::string& data, const std::string& mimeType) {
    value_.data = data;
    value_.mimeType = mimeType;
  }
};

// ResourceContent Builder
class ResourceContentBuilder
    : public Builder<ResourceContent, ResourceContentBuilder> {
 public:
  explicit ResourceContentBuilder(const Resource& resource) {
    value_.resource = resource;
  }
};

// Message Builder
class MessageBuilder : public Builder<Message, MessageBuilder> {
 public:
  explicit MessageBuilder(enums::Role::Value role) { value_.role = role; }

  MessageBuilder& text(const std::string& text) {
    value_.content = ContentBlock(TextContent(text));
    return *this;
  }

  MessageBuilder& image(const std::string& data, const std::string& mimeType) {
    value_.content = ContentBlock(ImageContent(data, mimeType));
    return *this;
  }

  MessageBuilder& resource(const Resource& res) {
    value_.content = ContentBlock(ResourceContent(res));
    return *this;
  }

  MessageBuilder& content(const ContentBlock& block) {
    value_.content = block;
    return *this;
  }
};

// Prompt Builder
class PromptBuilder : public Builder<Prompt, PromptBuilder> {
 public:
  explicit PromptBuilder(const std::string& name) { value_.name = name; }

  PromptBuilder& description(const std::string& d) {
    value_.description = mcp::make_optional(d);
    return *this;
  }

  PromptBuilder& argument(const std::string& name,
                          const std::string& desc = "",
                          bool required = false) {
    if (!value_.arguments) {
      value_.arguments = mcp::make_optional(std::vector<PromptArgument>());
    }
    PromptArgument arg;
    arg.name = name;
    if (!desc.empty()) {
      arg.description = mcp::make_optional(desc);
    }
    arg.required = required;
    value_.arguments->push_back(arg);
    return *this;
  }
};

// ResourceTemplate Builder
class ResourceTemplateBuilder
    : public Builder<ResourceTemplate, ResourceTemplateBuilder> {
 public:
  ResourceTemplateBuilder(const std::string& uriTemplate,
                          const std::string& name) {
    value_.uriTemplate = uriTemplate;
    value_.name = name;
  }

  ResourceTemplateBuilder& description(const std::string& d) {
    value_.description = mcp::make_optional(d);
    return *this;
  }

  ResourceTemplateBuilder& mimeType(const std::string& mt) {
    value_.mimeType = mcp::make_optional(mt);
    return *this;
  }

  ResourceTemplateBuilder& metadata(const std::string& key,
                                    const std::string& val) {
    if (!value_._meta) {
      value_._meta = mcp::make_optional(Metadata());
    }
    add_metadata(*value_._meta, key, val);
    return *this;
  }
};

// Implementation Builder
class ImplementationBuilder
    : public Builder<Implementation, ImplementationBuilder> {
 public:
  ImplementationBuilder(const std::string& name, const std::string& version) {
    value_.name = name;
    value_.version = version;
  }

  ImplementationBuilder& metadata(const std::string& key,
                                  const std::string& val) {
    if (!value_._meta) {
      value_._meta = mcp::make_optional(Metadata());
    }
    add_metadata(*value_._meta, key, val);
    return *this;
  }
};

// Root Builder
class RootBuilder : public Builder<Root, RootBuilder> {
 public:
  explicit RootBuilder(const std::string& uri) { value_.uri = uri; }

  RootBuilder& name(const std::string& n) {
    value_.name = mcp::make_optional(n);
    return *this;
  }
};

// Error Builder
class ErrorBuilder : public Builder<Error, ErrorBuilder> {
 public:
  ErrorBuilder(int code, const std::string& message) {
    value_.code = code;
    value_.message = message;
  }

  template <typename T>
  ErrorBuilder& data(T&& d) {
    value_.data = mcp::make_optional(ErrorData(std::forward<T>(d)));
    return *this;
  }
};

// PromptMessage Builder
class PromptMessageBuilder
    : public Builder<PromptMessage, PromptMessageBuilder> {
 public:
  explicit PromptMessageBuilder(enums::Role::Value role) { value_.role = role; }

  PromptMessageBuilder& text(const std::string& text) {
    value_.content = TextContent(text);
    return *this;
  }

  PromptMessageBuilder& image(const std::string& data,
                              const std::string& mimeType) {
    value_.content = ImageContent(data, mimeType);
    return *this;
  }

  PromptMessageBuilder& embeddedResource(const EmbeddedResource& resource) {
    value_.content = resource;
    return *this;
  }
};

// SamplingMessage Builder
class SamplingMessageBuilder
    : public Builder<SamplingMessage, SamplingMessageBuilder> {
 public:
  explicit SamplingMessageBuilder(enums::Role::Value role) {
    value_.role = role;
  }

  SamplingMessageBuilder& text(const std::string& text) {
    value_.content = TextContent(text);
    return *this;
  }

  SamplingMessageBuilder& image(const std::string& data,
                                const std::string& mimeType) {
    value_.content = ImageContent(data, mimeType);
    return *this;
  }

  SamplingMessageBuilder& audio(const std::string& data,
                                const std::string& mimeType) {
    value_.content = AudioContent(data, mimeType);
    return *this;
  }
};

// Additional Result and Request Builders

class InitializeRequestBuilder
    : public Builder<InitializeRequest, InitializeRequestBuilder> {
 public:
  InitializeRequestBuilder(const std::string& protocolVersion,
                           const ClientCapabilities& capabilities) {
    value_.protocolVersion = protocolVersion;
    value_.capabilities = capabilities;
  }

  InitializeRequestBuilder& clientInfo(const Implementation& impl) {
    value_.clientInfo = mcp::make_optional(impl);
    return *this;
  }

  InitializeRequestBuilder& clientInfo(const std::string& name,
                                       const std::string& version) {
    value_.clientInfo = mcp::make_optional(Implementation(name, version));
    return *this;
  }
};

class InitializeResultBuilder
    : public Builder<InitializeResult, InitializeResultBuilder> {
 public:
  InitializeResultBuilder(const std::string& protocolVersion,
                          const ServerCapabilities& capabilities) {
    value_.protocolVersion = protocolVersion;
    value_.capabilities = capabilities;
  }

  InitializeResultBuilder& serverInfo(const Implementation& impl) {
    value_.serverInfo = mcp::make_optional(impl);
    return *this;
  }

  InitializeResultBuilder& serverInfo(const std::string& name,
                                      const std::string& version) {
    value_.serverInfo = mcp::make_optional(Implementation(name, version));
    return *this;
  }

  InitializeResultBuilder& instructions(const std::string& instr) {
    value_.instructions = mcp::make_optional(instr);
    return *this;
  }
};

class CallToolRequestBuilder
    : public Builder<CallToolRequest, CallToolRequestBuilder> {
 public:
  explicit CallToolRequestBuilder(const std::string& name) {
    value_.name = name;
  }

  template <typename T>
  CallToolRequestBuilder& argument(const std::string& key, T&& val) {
    if (!value_.arguments) {
      value_.arguments = mcp::make_optional(Metadata());
    }
    add_metadata(*value_.arguments, key, std::forward<T>(val));
    return *this;
  }
};

class CallToolResultBuilder
    : public Builder<CallToolResult, CallToolResultBuilder> {
 public:
  CallToolResultBuilder() = default;

  CallToolResultBuilder& addText(const std::string& text) {
    value_.content.push_back(ExtendedContentBlock(TextContent(text)));
    return *this;
  }

  CallToolResultBuilder& addImage(const std::string& data,
                                  const std::string& mimeType) {
    value_.content.push_back(
        ExtendedContentBlock(ImageContent(data, mimeType)));
    return *this;
  }

  CallToolResultBuilder& addAudio(const std::string& data,
                                  const std::string& mimeType) {
    value_.content.push_back(
        ExtendedContentBlock(AudioContent(data, mimeType)));
    return *this;
  }

  CallToolResultBuilder& addResourceLink(const Resource& resource) {
    value_.content.push_back(ExtendedContentBlock(ResourceLink(resource)));
    return *this;
  }

  CallToolResultBuilder& addEmbeddedResource(const EmbeddedResource& resource) {
    value_.content.push_back(ExtendedContentBlock(resource));
    return *this;
  }

  CallToolResultBuilder& isError(bool error) {
    value_.isError = error;
    return *this;
  }
};

class GetPromptRequestBuilder
    : public Builder<GetPromptRequest, GetPromptRequestBuilder> {
 public:
  explicit GetPromptRequestBuilder(const std::string& name) {
    value_.name = name;
  }

  template <typename T>
  GetPromptRequestBuilder& argument(const std::string& key, T&& val) {
    if (!value_.arguments) {
      value_.arguments = mcp::make_optional(Metadata());
    }
    add_metadata(*value_.arguments, key, std::forward<T>(val));
    return *this;
  }
};

class GetPromptResultBuilder
    : public Builder<GetPromptResult, GetPromptResultBuilder> {
 public:
  GetPromptResultBuilder& description(const std::string& desc) {
    value_.description = mcp::make_optional(desc);
    return *this;
  }

  GetPromptResultBuilder& addMessage(const PromptMessage& msg) {
    value_.messages.push_back(msg);
    return *this;
  }

  GetPromptResultBuilder& addUserMessage(const std::string& text) {
    value_.messages.push_back(
        PromptMessage(enums::Role::USER, TextContent(text)));
    return *this;
  }

  GetPromptResultBuilder& addAssistantMessage(const std::string& text) {
    value_.messages.push_back(
        PromptMessage(enums::Role::ASSISTANT, TextContent(text)));
    return *this;
  }
};

class CreateMessageResultBuilder
    : public Builder<CreateMessageResult, CreateMessageResultBuilder> {
 public:
  CreateMessageResultBuilder(enums::Role::Value role,
                             const std::string& model) {
    value_.role = role;
    value_.model = model;
  }

  CreateMessageResultBuilder& text(const std::string& text) {
    value_.content = TextContent(text);
    return *this;
  }

  CreateMessageResultBuilder& image(const std::string& data,
                                    const std::string& mimeType) {
    value_.content = ImageContent(data, mimeType);
    return *this;
  }

  CreateMessageResultBuilder& audio(const std::string& data,
                                    const std::string& mimeType) {
    value_.content = AudioContent(data, mimeType);
    return *this;
  }

  CreateMessageResultBuilder& stopReason(const std::string& reason) {
    value_.stopReason = mcp::make_optional(reason);
    return *this;
  }
};

class ListResourcesResultBuilder
    : public Builder<ListResourcesResult, ListResourcesResultBuilder> {
 public:
  ListResourcesResultBuilder& add(const Resource& resource) {
    value_.resources.push_back(resource);
    return *this;
  }

  ListResourcesResultBuilder& nextCursor(const Cursor& cursor) {
    value_.nextCursor = mcp::make_optional(cursor);
    return *this;
  }
};

class ListToolsResultBuilder
    : public Builder<ListToolsResult, ListToolsResultBuilder> {
 public:
  ListToolsResultBuilder& add(const Tool& tool) {
    value_.tools.push_back(tool);
    return *this;
  }
};

class ListPromptsResultBuilder
    : public Builder<ListPromptsResult, ListPromptsResultBuilder> {
 public:
  ListPromptsResultBuilder& add(const Prompt& prompt) {
    value_.prompts.push_back(prompt);
    return *this;
  }

  ListPromptsResultBuilder& nextCursor(const Cursor& cursor) {
    value_.nextCursor = mcp::make_optional(cursor);
    return *this;
  }
};

class ListRootsResultBuilder
    : public Builder<ListRootsResult, ListRootsResultBuilder> {
 public:
  ListRootsResultBuilder& add(const Root& root) {
    value_.roots.push_back(root);
    return *this;
  }
};

class ReadResourceResultBuilder
    : public Builder<ReadResourceResult, ReadResourceResultBuilder> {
 public:
  ReadResourceResultBuilder& addText(const std::string& text) {
    value_.contents.push_back(TextResourceContents(text));
    return *this;
  }

  ReadResourceResultBuilder& addBlob(const std::string& blob) {
    value_.contents.push_back(BlobResourceContents(blob));
    return *this;
  }
};

class CompleteRequestBuilder
    : public Builder<CompleteRequest, CompleteRequestBuilder> {
 public:
  CompleteRequestBuilder(const std::string& type, const std::string& name) {
    value_.ref = PromptReference(type, name);
  }

  CompleteRequestBuilder& argument(const std::string& arg) {
    value_.argument = mcp::make_optional(arg);
    return *this;
  }
};

class CompleteResultBuilder
    : public Builder<CompleteResult, CompleteResultBuilder> {
 public:
  CompleteResultBuilder& addValue(const std::string& val) {
    value_.completion.values.push_back(val);
    return *this;
  }

  CompleteResultBuilder& total(double t) {
    value_.completion.total = mcp::make_optional(t);
    return *this;
  }

  CompleteResultBuilder& hasMore(bool more) {
    value_.completion.hasMore = more;
    return *this;
  }
};

class LoggingMessageNotificationBuilder
    : public Builder<LoggingMessageNotification,
                     LoggingMessageNotificationBuilder> {
 public:
  explicit LoggingMessageNotificationBuilder(enums::LoggingLevel::Value level) {
    value_.level = level;
  }

  LoggingMessageNotificationBuilder& logger(const std::string& log) {
    value_.logger = mcp::make_optional(log);
    return *this;
  }

  template <typename T>
  LoggingMessageNotificationBuilder& data(T&& d) {
    value_.data = std::forward<T>(d);
    return *this;
  }
};

class ProgressNotificationBuilder
    : public Builder<ProgressNotification, ProgressNotificationBuilder> {
 public:
  ProgressNotificationBuilder(const ProgressToken& token, double progress) {
    value_.progressToken = token;
    value_.progress = progress;
  }

  ProgressNotificationBuilder& total(double t) {
    value_.total = mcp::make_optional(t);
    return *this;
  }
};

class CancelledNotificationBuilder
    : public Builder<CancelledNotification, CancelledNotificationBuilder> {
 public:
  explicit CancelledNotificationBuilder(const RequestId& id) {
    value_.requestId = id;
  }

  CancelledNotificationBuilder& reason(const std::string& r) {
    value_.reason = mcp::make_optional(r);
    return *this;
  }
};

class ResourceUpdatedNotificationBuilder
    : public Builder<ResourceUpdatedNotification,
                     ResourceUpdatedNotificationBuilder> {
 public:
  explicit ResourceUpdatedNotificationBuilder(const std::string& uri) {
    value_.uri = uri;
  }
};

class NumberSchemaBuilder : public Builder<NumberSchema, NumberSchemaBuilder> {
 public:
  NumberSchemaBuilder& description(const std::string& desc) {
    value_.description = mcp::make_optional(desc);
    return *this;
  }

  NumberSchemaBuilder& minimum(double min) {
    value_.minimum = mcp::make_optional(min);
    return *this;
  }

  NumberSchemaBuilder& maximum(double max) {
    value_.maximum = mcp::make_optional(max);
    return *this;
  }

  NumberSchemaBuilder& multipleOf(double multiple) {
    value_.multipleOf = mcp::make_optional(multiple);
    return *this;
  }
};

class BooleanSchemaBuilder
    : public Builder<BooleanSchema, BooleanSchemaBuilder> {
 public:
  BooleanSchemaBuilder& description(const std::string& desc) {
    value_.description = mcp::make_optional(desc);
    return *this;
  }
};

class EnumSchemaBuilder : public Builder<EnumSchema, EnumSchemaBuilder> {
 public:
  explicit EnumSchemaBuilder(std::vector<std::string>&& values) {
    value_.values = std::move(values);
  }

  EnumSchemaBuilder& description(const std::string& desc) {
    value_.description = mcp::make_optional(desc);
    return *this;
  }

  EnumSchemaBuilder& addValue(const std::string& val) {
    value_.values.push_back(val);
    return *this;
  }
};

class ElicitRequestBuilder
    : public Builder<ElicitRequest, ElicitRequestBuilder> {
 public:
  ElicitRequestBuilder(const std::string& name,
                       const PrimitiveSchemaDefinition& schema) {
    value_.name = name;
    value_.schema = schema;
  }

  ElicitRequestBuilder& prompt(const std::string& p) {
    value_.prompt = mcp::make_optional(p);
    return *this;
  }
};

class ElicitResultBuilder : public Builder<ElicitResult, ElicitResultBuilder> {
 public:
  template <typename T>
  ElicitResultBuilder& value(T&& v) {
    value_.value = std::forward<T>(v);
    return *this;
  }
};

// Additional builders for missing types

class AnnotationsBuilder : public Builder<Annotations, AnnotationsBuilder> {
 public:
  AnnotationsBuilder& audience(const std::vector<enums::Role::Value>& roles) {
    value_.audience = roles;
    return *this;
  }

  AnnotationsBuilder& priority(double p) {
    value_.priority = p;
    return *this;
  }
};

class BaseMetadataBuilder : public Builder<BaseMetadata, BaseMetadataBuilder> {
 public:
  BaseMetadataBuilder& _meta(const Metadata& meta) {
    value_._meta = meta;
    return *this;
  }
};

class ToolAnnotationsBuilder
    : public Builder<ToolAnnotations, ToolAnnotationsBuilder> {
 public:
  ToolAnnotationsBuilder& audience(
      const std::vector<enums::Role::Value>& roles) {
    value_.audience = roles;
    return *this;
  }
};

class ResourceLinkBuilder : public Builder<ResourceLink, ResourceLinkBuilder> {
 public:
  ResourceLinkBuilder(const std::string& uri, const std::string& name) {
    value_.uri = uri;
    value_.name = name;
  }

  ResourceLinkBuilder& description(const std::string& desc) {
    value_.description = desc;
    return *this;
  }

  ResourceLinkBuilder& mimeType(const std::string& mime) {
    value_.mimeType = mime;
    return *this;
  }
};

class ToolParameterBuilder
    : public Builder<ToolParameter, ToolParameterBuilder> {
 public:
  ToolParameterBuilder(const std::string& name, const std::string& type) {
    value_.name = name;
    value_.type = type;
    value_.required = false;
  }

  ToolParameterBuilder& description(const std::string& desc) {
    value_.description = desc;
    return *this;
  }

  ToolParameterBuilder& required(bool req) {
    value_.required = req;
    return *this;
  }
};

class ModelHintBuilder : public Builder<ModelHint, ModelHintBuilder> {
 public:
  explicit ModelHintBuilder(const std::string& name) { value_.name = name; }
};

class TextResourceContentsBuilder
    : public Builder<TextResourceContents, TextResourceContentsBuilder> {
 public:
  explicit TextResourceContentsBuilder(const std::string& uri) {
    value_.uri = uri;
  }

  TextResourceContentsBuilder& text(const std::string& t) {
    value_.text = t;
    return *this;
  }

  TextResourceContentsBuilder& mimeType(const std::string& mime) {
    value_.mimeType = mime;
    return *this;
  }
};

class BlobResourceContentsBuilder
    : public Builder<BlobResourceContents, BlobResourceContentsBuilder> {
 public:
  explicit BlobResourceContentsBuilder(const std::string& uri) {
    value_.uri = uri;
  }

  BlobResourceContentsBuilder& blob(const std::string& b) {
    value_.blob = b;
    return *this;
  }

  BlobResourceContentsBuilder& mimeType(const std::string& mime) {
    value_.mimeType = mime;
    return *this;
  }
};

class ResourceTemplateReferenceBuilder
    : public Builder<ResourceTemplateReference,
                     ResourceTemplateReferenceBuilder> {
 public:
  ResourceTemplateReferenceBuilder(const std::string& type,
                                   const std::string& name) {
    value_.type = type;
    value_.name = name;
  }
};

class PromptReferenceBuilder
    : public Builder<PromptReference, PromptReferenceBuilder> {
 public:
  PromptReferenceBuilder(const std::string& type, const std::string& name) {
    value_.type = type;
    value_.name = name;
  }

  PromptReferenceBuilder& _meta(const Metadata& meta) {
    value_._meta = meta;
    return *this;
  }
};

class ResourcesCapabilityBuilder
    : public Builder<ResourcesCapability, ResourcesCapabilityBuilder> {
 public:
  ResourcesCapabilityBuilder& subscribe(const EmptyCapability& cap) {
    value_.subscribe = cap;
    return *this;
  }

  ResourcesCapabilityBuilder& listChanged(const EmptyCapability& cap) {
    value_.listChanged = cap;
    return *this;
  }
};

class PromptsCapabilityBuilder
    : public Builder<PromptsCapability, PromptsCapabilityBuilder> {
 public:
  PromptsCapabilityBuilder& listChanged(const EmptyCapability& cap) {
    value_.listChanged = cap;
    return *this;
  }
};

class RootsCapabilityBuilder
    : public Builder<RootsCapability, RootsCapabilityBuilder> {
 public:
  RootsCapabilityBuilder& listChanged(const EmptyCapability& cap) {
    value_.listChanged = cap;
    return *this;
  }
};

class EmptyResultBuilder : public Builder<EmptyResult, EmptyResultBuilder> {
 public:
  // EmptyResult has no fields
};

class InitializedNotificationBuilder
    : public Builder<InitializedNotification, InitializedNotificationBuilder> {
 public:
  InitializedNotificationBuilder() {
    value_.method = "notifications/initialized";
    value_.params = Metadata();
  }

  InitializedNotificationBuilder& meta(const BaseMetadata& meta) {
    value_.params = meta._meta.value_or(Metadata());
    return *this;
  }
};

class PingRequestBuilder : public Builder<PingRequest, PingRequestBuilder> {
 public:
  PingRequestBuilder() { value_.method = "ping"; }

  PingRequestBuilder& id(const RequestId& reqId) {
    value_.id = reqId;
    return *this;
  }
};

class ListResourcesRequestBuilder
    : public Builder<ListResourcesRequest, ListResourcesRequestBuilder> {
 public:
  ListResourcesRequestBuilder() { value_.method = "resources/list"; }

  ListResourcesRequestBuilder& id(const RequestId& reqId) {
    value_.id = reqId;
    return *this;
  }

  ListResourcesRequestBuilder& cursor(const std::string& c) {
    value_.cursor = c;
    return *this;
  }
};

class ListResourceTemplatesRequestBuilder
    : public Builder<ListResourceTemplatesRequest,
                     ListResourceTemplatesRequestBuilder> {
 public:
  ListResourceTemplatesRequestBuilder() {
    value_.method = "resources/templates/list";
  }

  ListResourceTemplatesRequestBuilder& id(const RequestId& reqId) {
    value_.id = reqId;
    return *this;
  }

  ListResourceTemplatesRequestBuilder& cursor(const std::string& c) {
    value_.cursor = c;
    return *this;
  }
};

class ListResourceTemplatesResultBuilder
    : public Builder<ListResourceTemplatesResult,
                     ListResourceTemplatesResultBuilder> {
 public:
  ListResourceTemplatesResultBuilder& add(const ResourceTemplate& tmpl) {
    value_.resourceTemplates.push_back(tmpl);
    return *this;
  }

  ListResourceTemplatesResultBuilder& nextCursor(const std::string& cursor) {
    value_.nextCursor = cursor;
    return *this;
  }
};

class ReadResourceRequestBuilder
    : public Builder<ReadResourceRequest, ReadResourceRequestBuilder> {
 public:
  explicit ReadResourceRequestBuilder(const std::string& uri) {
    value_.method = "resources/read";
    value_.uri = uri;
  }

  ReadResourceRequestBuilder& id(const RequestId& reqId) {
    value_.id = reqId;
    return *this;
  }
};

class ResourceListChangedNotificationBuilder
    : public Builder<ResourceListChangedNotification,
                     ResourceListChangedNotificationBuilder> {
 public:
  ResourceListChangedNotificationBuilder() {
    value_.method = "notifications/resources/list_changed";
    value_.params = Metadata();
  }
};

class SubscribeRequestBuilder
    : public Builder<SubscribeRequest, SubscribeRequestBuilder> {
 public:
  explicit SubscribeRequestBuilder(const std::string& uri) {
    value_.method = "resources/subscribe";
    value_.uri = uri;
  }

  SubscribeRequestBuilder& id(const RequestId& reqId) {
    value_.id = reqId;
    return *this;
  }
};

class UnsubscribeRequestBuilder
    : public Builder<UnsubscribeRequest, UnsubscribeRequestBuilder> {
 public:
  explicit UnsubscribeRequestBuilder(const std::string& uri) {
    value_.method = "resources/unsubscribe";
    value_.uri = uri;
  }

  UnsubscribeRequestBuilder& id(const RequestId& reqId) {
    value_.id = reqId;
    return *this;
  }
};

class ListPromptsRequestBuilder
    : public Builder<ListPromptsRequest, ListPromptsRequestBuilder> {
 public:
  ListPromptsRequestBuilder() { value_.method = "prompts/list"; }

  ListPromptsRequestBuilder& id(const RequestId& reqId) {
    value_.id = reqId;
    return *this;
  }

  ListPromptsRequestBuilder& cursor(const std::string& c) {
    value_.cursor = c;
    return *this;
  }
};

class PromptListChangedNotificationBuilder
    : public Builder<PromptListChangedNotification,
                     PromptListChangedNotificationBuilder> {
 public:
  PromptListChangedNotificationBuilder() {
    value_.method = "notifications/prompts/list_changed";
    value_.params = Metadata();
  }
};

class ListToolsRequestBuilder
    : public Builder<ListToolsRequest, ListToolsRequestBuilder> {
 public:
  ListToolsRequestBuilder() { value_.method = "tools/list"; }

  ListToolsRequestBuilder& id(const RequestId& reqId) {
    value_.id = reqId;
    return *this;
  }

  ListToolsRequestBuilder& cursor(const std::string& c) {
    value_.cursor = c;
    return *this;
  }
};

class ToolListChangedNotificationBuilder
    : public Builder<ToolListChangedNotification,
                     ToolListChangedNotificationBuilder> {
 public:
  ToolListChangedNotificationBuilder() {
    value_.method = "notifications/tools/list_changed";
    value_.params = Metadata();
  }
};

class SetLevelRequestBuilder
    : public Builder<SetLevelRequest, SetLevelRequestBuilder> {
 public:
  explicit SetLevelRequestBuilder(enums::LoggingLevel::Value level) {
    value_.method = "logging/setLevel";
    value_.level = level;
  }

  SetLevelRequestBuilder& id(const RequestId& reqId) {
    value_.id = reqId;
    return *this;
  }
};

class ListRootsRequestBuilder
    : public Builder<ListRootsRequest, ListRootsRequestBuilder> {
 public:
  ListRootsRequestBuilder() { value_.method = "roots/list"; }

  ListRootsRequestBuilder& id(const RequestId& reqId) {
    value_.id = reqId;
    return *this;
  }
};

class RootsListChangedNotificationBuilder
    : public Builder<RootsListChangedNotification,
                     RootsListChangedNotificationBuilder> {
 public:
  RootsListChangedNotificationBuilder() {
    value_.method = "notifications/roots/list_changed";
    value_.params = Metadata();
  }
};

// Deprecated factory functions removed - use make<T>() instead

// Unified make<T>() implementation for C++14/17
namespace detail {
template <typename T>
struct MakeHelper;
}

template <typename T, typename... Args>
auto make(Args&&... args) {
  return detail::MakeHelper<T>::make(std::forward<Args>(args)...);
}

// Template specializations for all supported types
namespace detail {

template <>
struct MakeHelper<TextContent> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return TextContentBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ImageContent> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ImageContentBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<AudioContent> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return AudioContentBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<Resource> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ResourceBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ResourceContent> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ResourceContentBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<Tool> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ToolBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<Prompt> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return PromptBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ResourceTemplate> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ResourceTemplateBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<Implementation> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ImplementationBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<Root> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return RootBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<Message> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return MessageBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<Error> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ErrorBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<EmbeddedResource> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return EmbeddedResourceBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<PromptMessage> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return PromptMessageBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<SamplingMessage> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return SamplingMessageBuilder(std::forward<Args>(args)...);
  }
};

// Add all remaining type specializations here
template <>
struct MakeHelper<InitializeRequest> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return InitializeRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<InitializeResult> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return InitializeResultBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<InitializeParams> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return InitializeParamsBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<CallToolRequest> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return CallToolRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<CallToolResult> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return CallToolResultBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<GetPromptRequest> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return GetPromptRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<GetPromptResult> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return GetPromptResultBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<CreateMessageRequest> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return CreateMessageRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<CreateMessageResult> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return CreateMessageResultBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ListResourcesRequest> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ListResourcesRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ListResourcesResult> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ListResourcesResultBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ListToolsRequest> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ListToolsRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ListToolsResult> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ListToolsResultBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ListPromptsRequest> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ListPromptsRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ListPromptsResult> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ListPromptsResultBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ReadResourceRequest> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ReadResourceRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ReadResourceResult> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ReadResourceResultBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<CompleteRequest> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return CompleteRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<CompleteResult> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return CompleteResultBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<SamplingParams> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return SamplingParamsBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ModelPreferences> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ModelPreferencesBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ClientCapabilities> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ClientCapabilitiesBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ServerCapabilities> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ServerCapabilitiesBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<StringSchema> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return StringSchemaBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<NumberSchema> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return NumberSchemaBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<BooleanSchema> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return BooleanSchemaBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<EnumSchema> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return EnumSchemaBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<LoggingMessageNotification> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return LoggingMessageNotificationBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ProgressNotification> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ProgressNotificationBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<CancelledNotification> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return CancelledNotificationBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ResourceUpdatedNotification> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ResourceUpdatedNotificationBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ElicitRequest> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ElicitRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ElicitResult> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ElicitResultBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<Annotations> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return AnnotationsBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ToolParameter> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ToolParameterBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ResourceLink> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ResourceLinkBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ModelHint> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ModelHintBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<TextResourceContents> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return TextResourceContentsBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<BlobResourceContents> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return BlobResourceContentsBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<PingRequest> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return PingRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<SubscribeRequest> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return SubscribeRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<SetLevelRequest> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return SetLevelRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<EmptyResult> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return EmptyResultBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<UnsubscribeRequest> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return UnsubscribeRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ListRootsRequest> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ListRootsRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ListRootsResult> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ListRootsResultBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ListResourceTemplatesRequest> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ListResourceTemplatesRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ListResourceTemplatesResult> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ListResourceTemplatesResultBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<InitializedNotification> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return InitializedNotificationBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ResourceListChangedNotification> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ResourceListChangedNotificationBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<PromptListChangedNotification> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return PromptListChangedNotificationBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ToolListChangedNotification> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ToolListChangedNotificationBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<RootsListChangedNotification> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return RootsListChangedNotificationBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ResourceTemplateReference> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ResourceTemplateReferenceBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<PromptReference> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return PromptReferenceBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ResourcesCapability> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ResourcesCapabilityBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<PromptsCapability> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return PromptsCapabilityBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<RootsCapability> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return RootsCapabilityBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<BaseMetadata> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return BaseMetadataBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<ToolAnnotations> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return ToolAnnotationsBuilder(std::forward<Args>(args)...);
  }
};

}  // namespace detail

// Generic JSON-RPC Builders

/**
 * Generic JSON-RPC Request Builder
 */
class JsonRpcRequestBuilder
    : public Builder<jsonrpc::Request, JsonRpcRequestBuilder> {
 public:
  JsonRpcRequestBuilder(const RequestId& id, const std::string& method) {
    value_.jsonrpc = "2.0";
    value_.id = id;
    value_.method = method;
  }

  JsonRpcRequestBuilder& params(const Metadata& p) {
    value_.params = mcp::make_optional(p);
    return *this;
  }

  template <typename K, typename V>
  JsonRpcRequestBuilder& param(const K& key, V&& value) {
    if (!value_.params) {
      value_.params = mcp::make_optional(make_metadata());
    }
    add_metadata(*value_.params, key, std::forward<V>(value));
    return *this;
  }
};

/**
 * Generic JSON-RPC Response Builder
 */
class JsonRpcResponseBuilder
    : public Builder<jsonrpc::Response, JsonRpcResponseBuilder> {
 public:
  explicit JsonRpcResponseBuilder(const RequestId& id) {
    value_.jsonrpc = "2.0";
    value_.id = id;
  }

  JsonRpcResponseBuilder& result(const jsonrpc::ResponseResult& r) {
    value_.result = mcp::make_optional(r);
    return *this;
  }

  JsonRpcResponseBuilder& error(const Error& e) {
    value_.error = mcp::make_optional(e);
    return *this;
  }

  JsonRpcResponseBuilder& error(int code, const std::string& message) {
    value_.error = mcp::make_optional(Error(code, message));
    return *this;
  }
};

/**
 * Generic JSON-RPC Notification Builder
 */
class JsonRpcNotificationBuilder
    : public Builder<jsonrpc::Notification, JsonRpcNotificationBuilder> {
 public:
  explicit JsonRpcNotificationBuilder(const std::string& method) {
    value_.jsonrpc = "2.0";
    value_.method = method;
  }

  JsonRpcNotificationBuilder& params(const Metadata& p) {
    value_.params = mcp::make_optional(p);
    return *this;
  }

  template <typename K, typename V>
  JsonRpcNotificationBuilder& param(const K& key, V&& value) {
    if (!value_.params) {
      value_.params = mcp::make_optional(make_metadata());
    }
    add_metadata(*value_.params, key, std::forward<V>(value));
    return *this;
  }
};

/**
 * Generic Metadata Builder
 */
class MetadataBuilder : public Builder<Metadata, MetadataBuilder> {
 public:
  MetadataBuilder() { value_ = make_metadata(); }

  template <typename K, typename V>
  MetadataBuilder& add(const K& key, V&& value) {
    add_metadata(value_, key, std::forward<V>(value));
    return *this;
  }
};

// Add to detail namespace for make<> support
namespace detail {

template <>
struct MakeHelper<jsonrpc::Request> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return JsonRpcRequestBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<jsonrpc::Response> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return JsonRpcResponseBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<jsonrpc::Notification> {
  template <typename... Args>
  static auto make(Args&&... args) {
    return JsonRpcNotificationBuilder(std::forward<Args>(args)...);
  }
};

template <>
struct MakeHelper<Metadata> {
  static auto make() { return MetadataBuilder(); }
};

}  // namespace detail

}  // namespace mcp

#endif  // MCP_BUILDERS_H
