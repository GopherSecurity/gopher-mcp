#ifndef MCP_TYPES_H
#define MCP_TYPES_H

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>  // Needed for JSON types used in Tool and other structures

#include "mcp/type_helpers.h"

namespace mcp {

// Forward declarations
struct AudioContent;
struct ResourceLink;
struct EmbeddedResource;
struct BaseMetadata;
struct Annotations;
struct ToolAnnotations;
struct ModelHint;

// JSON-RPC error codes as constants
namespace jsonrpc {
constexpr int PARSE_ERROR = -32700;
constexpr int INVALID_REQUEST = -32600;
constexpr int METHOD_NOT_FOUND = -32601;
constexpr int INVALID_PARAMS = -32602;
constexpr int INTERNAL_ERROR = -32603;
}  // namespace jsonrpc

// Protocol type aliases
using RequestId = variant<std::string, int>;
using ProgressToken = variant<std::string, int>;
using Cursor = std::string;

// Enum definitions
namespace enums {

// Role enum
struct Role {
  enum Value { USER, ASSISTANT };

  static const char* to_string(Value v) {
    switch (v) {
      case USER:
        return "user";
      case ASSISTANT:
        return "assistant";
      default:
        return "";
    }
  }

  static optional<Value> from_string(const std::string& s) {
    if (s == "user")
      return mcp::make_optional(USER);
    if (s == "assistant")
      return mcp::make_optional(ASSISTANT);
    return nullopt;
  }
};

// LoggingLevel enum with all RFC-5424 severities
struct LoggingLevel {
  enum Value {
    DEBUG = 0,     // Debug-level messages
    INFO = 1,      // Informational messages
    NOTICE = 2,    // Normal but significant condition
    WARNING = 3,   // Warning conditions
    ERROR = 4,     // Error conditions
    CRITICAL = 5,  // Critical conditions
    ALERT = 6,     // Action must be taken immediately
    EMERGENCY = 7  // System is unusable
  };

  static const char* to_string(Value v) {
    switch (v) {
      case DEBUG:
        return "debug";
      case INFO:
        return "info";
      case NOTICE:
        return "notice";
      case WARNING:
        return "warning";
      case ERROR:
        return "error";
      case CRITICAL:
        return "critical";
      case ALERT:
        return "alert";
      case EMERGENCY:
        return "emergency";
      default:
        return "";
    }
  }

  static optional<Value> from_string(const std::string& s) {
    if (s == "debug")
      return mcp::make_optional(DEBUG);
    if (s == "info")
      return mcp::make_optional(INFO);
    if (s == "notice")
      return mcp::make_optional(NOTICE);
    if (s == "warning")
      return mcp::make_optional(WARNING);
    if (s == "error")
      return mcp::make_optional(ERROR);
    if (s == "critical")
      return mcp::make_optional(CRITICAL);
    if (s == "alert")
      return mcp::make_optional(ALERT);
    if (s == "emergency")
      return mcp::make_optional(EMERGENCY);
    return nullopt;
  }
};

}  // namespace enums

// Factory functions for protocol types
// RequestId factory
inline RequestId make_request_id(const std::string& id) {
  return RequestId(id);
}

inline RequestId make_request_id(int id) { return RequestId(id); }

inline RequestId make_request_id(const char* id) {
  return RequestId(std::string(id));
}

// ProgressToken factory
inline ProgressToken make_progress_token(const std::string& token) {
  return ProgressToken(token);
}

inline ProgressToken make_progress_token(int token) {
  return ProgressToken(token);
}

inline ProgressToken make_progress_token(const char* token) {
  return ProgressToken(std::string(token));
}

// Annotations for content blocks
struct Annotations {
  optional<std::vector<enums::Role::Value>> audience;
  optional<double> priority;  // 1.0 = most important

  Annotations() = default;
};

// Base types
struct TextContent {
  std::string type = "text";
  std::string text;
  optional<Annotations> annotations;

  TextContent() = default;
  explicit TextContent(const std::string& t) : text(t) {}
};

struct ImageContent {
  std::string type = "image";
  std::string data;
  std::string mimeType;

  ImageContent() = default;
  ImageContent(const std::string& d, const std::string& mt)
      : data(d), mimeType(mt) {}
};

struct Resource {
  std::string uri;
  std::string name;
  optional<std::string> description;
  optional<std::string> mimeType;

  Resource() = default;
  explicit Resource(const std::string& u, const std::string& n)
      : uri(u), name(n) {}
};

struct ResourceContent {
  std::string type = "resource";
  Resource resource;

  ResourceContent() = default;
  explicit ResourceContent(const Resource& r) : resource(r) {}
};

using ContentBlock = variant<TextContent, ImageContent, ResourceContent>;

// Base metadata interface (replaces Annotated)
struct BaseMetadata {
  optional<Metadata> _meta;

  BaseMetadata() = default;
};

// Tool-specific annotations
struct ToolAnnotations {
  optional<std::vector<enums::Role::Value>> audience;

  ToolAnnotations() = default;
};

// Audio content type (new in latest schema)
struct AudioContent {
  std::string type = "audio";
  std::string data;  // Base64-encoded audio data
  std::string mimeType;

  AudioContent() = default;
  AudioContent(const std::string& d, const std::string& mt)
      : data(d), mimeType(mt) {}
};

// Resource link (reference to a resource)
struct ResourceLink : Resource {
  std::string type = "resource";

  ResourceLink() = default;
  explicit ResourceLink(const Resource& r) : Resource(r) {}
};

// Embedded resource with nested content (uses ContentBlock for now)
struct EmbeddedResource {
  std::string type = "embedded";
  Resource resource;
  std::vector<ContentBlock> content;

  EmbeddedResource() = default;
  explicit EmbeddedResource(const Resource& r) : resource(r) {}
};

// Extended ContentBlock to include all types
using ExtendedContentBlock = variant<TextContent,
                                     ImageContent,
                                     AudioContent,
                                     ResourceLink,
                                     EmbeddedResource>;

// Content block factory implementations
inline ContentBlock make_text_content(const std::string& text) {
  return ContentBlock(TextContent(text));
}

inline ContentBlock make_image_content(const std::string& data,
                                       const std::string& mime_type) {
  return ContentBlock(ImageContent(data, mime_type));
}

// Tool definitions
using ToolInputSchema = nlohmann::json;  // Tool input schema is a JSON object

struct ToolParameter {
  std::string name;
  std::string type;
  optional<std::string> description;
  bool required = false;
};

struct Tool {
  std::string name;
  optional<std::string> description;
  optional<ToolInputSchema> inputSchema;
  optional<std::vector<ToolParameter>> parameters;  // Legacy support

  Tool() = default;
  explicit Tool(const std::string& n) : name(n) {}
};

// Prompt definitions
struct PromptArgument {
  std::string name;
  optional<std::string> description;
  bool required = false;
};

struct Prompt {
  std::string name;
  optional<std::string> description;
  optional<std::vector<PromptArgument>> arguments;

  Prompt() = default;
  explicit Prompt(const std::string& n) : name(n) {}
};

// Error type
struct Error {
  int code;
  std::string message;
  optional<variant<
      std::nullptr_t,
      bool,
      int,
      double,
      std::string,
      std::vector<variant<std::nullptr_t, bool, int, double, std::string>>,
      std::map<std::string,
               variant<std::nullptr_t, bool, int, double, std::string>>>>
      data;

  Error() = default;
  Error(int c, const std::string& m) : code(c), message(m) {}
};

// Message types
struct Message {
  enums::Role::Value role;
  ContentBlock content;

  Message() = default;
  Message(enums::Role::Value r, const ContentBlock& c) : role(r), content(c) {}
};

// Sampling parameters
struct SamplingParams {
  optional<double> temperature;
  optional<int> maxTokens;
  optional<std::vector<std::string>> stopSequences;
  optional<Metadata> metadata;

  SamplingParams() = default;
};

// Factory functions for common MCP types
inline TextContent make_text(const std::string& text) {
  return TextContent(text);
}

inline ImageContent make_image(const std::string& data,
                               const std::string& mimeType) {
  return ImageContent(data, mimeType);
}

inline Resource make_resource(const std::string& uri, const std::string& name) {
  return Resource(uri, name);
}

inline ContentBlock make_resource_content(const Resource& resource) {
  return ContentBlock(ResourceContent(resource));
}

// Factory functions for new content types
inline ExtendedContentBlock make_audio_content(const std::string& data,
                                               const std::string& mime_type) {
  return ExtendedContentBlock(AudioContent(data, mime_type));
}

inline ExtendedContentBlock make_resource_link(const Resource& resource) {
  return ExtendedContentBlock(ResourceLink(resource));
}

inline ExtendedContentBlock make_embedded_resource(const Resource& resource) {
  return ExtendedContentBlock(EmbeddedResource(resource));
}

inline Tool make_tool(const std::string& name) { return Tool(name); }

inline Prompt make_prompt(const std::string& name) { return Prompt(name); }

inline Error make_error(int code, const std::string& message) {
  return Error(code, message);
}

inline Message make_message(enums::Role::Value role,
                            const ContentBlock& content) {
  return Message(role, content);
}

inline Message make_user_message(const std::string& text) {
  return Message(enums::Role::USER, make_text_content(text));
}

inline Message make_assistant_message(const std::string& text) {
  return Message(enums::Role::ASSISTANT, make_text_content(text));
}

// Result/Error pattern implementations
template <typename T>
Result<T> make_result(T&& value) {
  return Result<T>(std::forward<T>(value));
}

inline Result<std::nullptr_t> make_result(std::nullptr_t) {
  return Result<std::nullptr_t>(nullptr);
}

template <typename T>
Result<T> make_error_result(const Error& err) {
  return Result<T>(err);
}

template <typename T>
Result<T> make_error_result(Error&& err) {
  return Result<T>(std::move(err));
}

// Helper to check result status
template <typename T>
bool is_success(const Result<T>& result) {
  return result.template holds_alternative<T>();
}

template <typename T>
bool is_error(const Result<T>& result) {
  return result.template holds_alternative<Error>();
}

template <typename T>
const T* get_value(const Result<T>& result) {
  return result.template get_if<T>();
}

template <typename T>
T* get_value(Result<T>& result) {
  return result.template get_if<T>();
}

template <typename T>
const Error* get_error(const Result<T>& result) {
  return result.template get_if<Error>();
}

// Builder pattern for complex types
class ResourceBuilder {
  Resource resource_;

 public:
  ResourceBuilder(const std::string& uri, const std::string& name)
      : resource_(uri, name) {}

  ResourceBuilder& description(const std::string& desc) {
    resource_.description = mcp::make_optional(desc);
    return *this;
  }

  ResourceBuilder& mimeType(const std::string& mime) {
    resource_.mimeType = mcp::make_optional(mime);
    return *this;
  }

  Resource build() && { return std::move(resource_); }
  Resource build() const& { return resource_; }
};

inline ResourceBuilder build_resource(const std::string& uri,
                                      const std::string& name) {
  return ResourceBuilder(uri, name);
}

class ToolBuilder {
  Tool tool_;

 public:
  explicit ToolBuilder(const std::string& name) : tool_(name) {}

  ToolBuilder& description(const std::string& desc) {
    tool_.description = mcp::make_optional(desc);
    return *this;
  }

  ToolBuilder& inputSchema(const ToolInputSchema& schema) {
    tool_.inputSchema = mcp::make_optional(schema);
    return *this;
  }

  // Legacy parameter support
  ToolBuilder& parameter(const std::string& name,
                         const std::string& type,
                         bool required = false) {
    if (!tool_.parameters) {
      tool_.parameters = mcp::make_optional(std::vector<ToolParameter>());
    }
    tool_.parameters->push_back(ToolParameter{name, type, nullopt, required});
    return *this;
  }

  ToolBuilder& parameter(const std::string& name,
                         const std::string& type,
                         const std::string& desc,
                         bool required = false) {
    if (!tool_.parameters) {
      tool_.parameters = mcp::make_optional(std::vector<ToolParameter>());
    }
    tool_.parameters->push_back(
        ToolParameter{name, type, mcp::make_optional(desc), required});
    return *this;
  }

  Tool build() && { return std::move(tool_); }
  Tool build() const& { return tool_; }
};

inline ToolBuilder build_tool(const std::string& name) {
  return ToolBuilder(name);
}

class SamplingParamsBuilder {
  SamplingParams params_;

 public:
  SamplingParamsBuilder() = default;

  SamplingParamsBuilder& temperature(double temp) {
    params_.temperature = mcp::make_optional(temp);
    return *this;
  }

  SamplingParamsBuilder& maxTokens(int max) {
    params_.maxTokens = mcp::make_optional(max);
    return *this;
  }

  SamplingParamsBuilder& stopSequence(const std::string& seq) {
    if (!params_.stopSequences) {
      params_.stopSequences = mcp::make_optional(std::vector<std::string>());
    }
    params_.stopSequences->push_back(seq);
    return *this;
  }

  SamplingParamsBuilder& metadata(const std::string& key,
                                  const std::string& value) {
    if (!params_.metadata) {
      params_.metadata = mcp::make_optional(Metadata());
    }
    add_metadata(*params_.metadata, key, value);
    return *this;
  }

  template <typename T>
  SamplingParamsBuilder& metadata(const std::string& key, T&& value) {
    if (!params_.metadata) {
      params_.metadata = mcp::make_optional(Metadata());
    }
    add_metadata(*params_.metadata, key, std::forward<T>(value));
    return *this;
  }

  SamplingParams build() && { return std::move(params_); }
  SamplingParams build() const& { return params_; }
};

inline SamplingParamsBuilder build_sampling_params() {
  return SamplingParamsBuilder();
}

// Builder for EmbeddedResource
class EmbeddedResourceBuilder {
  EmbeddedResource resource_;

 public:
  explicit EmbeddedResourceBuilder(const Resource& r) : resource_(r) {}

  EmbeddedResourceBuilder& add_content(const ContentBlock& content) {
    resource_.content.push_back(content);
    return *this;
  }

  EmbeddedResourceBuilder& add_text(const std::string& text) {
    resource_.content.push_back(make_text_content(text));
    return *this;
  }

  EmbeddedResourceBuilder& add_image(const std::string& data,
                                     const std::string& mime_type) {
    resource_.content.push_back(make_image_content(data, mime_type));
    return *this;
  }

  EmbeddedResource build() && { return std::move(resource_); }
  EmbeddedResource build() const& { return resource_; }
};

inline EmbeddedResourceBuilder build_embedded_resource(
    const Resource& resource) {
  return EmbeddedResourceBuilder(resource);
}

// JSON-RPC message types
namespace jsonrpc {

struct Request {
  std::string jsonrpc = "2.0";
  RequestId id;
  std::string method;
  optional<Metadata> params;

  Request() = default;
  Request(const RequestId& i, const std::string& m) : id(i), method(m) {}

  Request(const RequestId& i, const std::string& m, const Metadata& p)
      : id(i), method(m), params(mcp::make_optional(p)) {}

  Request(const RequestId& i, const std::string& m, Metadata&& p)
      : id(i), method(m), params(mcp::make_optional(std::move(p))) {}
};

// Generic result type for responses
using ResponseResult = variant<std::nullptr_t,
                               bool,
                               int,
                               double,
                               std::string,
                               Metadata,
                               std::vector<ContentBlock>,
                               std::vector<Tool>,
                               std::vector<Prompt>,
                               std::vector<Resource>>;

struct Response {
  std::string jsonrpc = "2.0";
  RequestId id;
  optional<ResponseResult> result;
  optional<Error> error;

  Response() = default;
  explicit Response(const RequestId& i) : id(i) {}

  template <typename T>
  static Response success(const RequestId& id, T&& result) {
    Response r(id);
    r.result = mcp::make_optional(ResponseResult(std::forward<T>(result)));
    return r;
  }

  static Response make_error(const RequestId& id, const Error& err) {
    Response r(id);
    r.error = mcp::make_optional(err);
    return r;
  }
};

struct Notification {
  std::string jsonrpc = "2.0";
  std::string method;
  optional<Metadata> params;

  Notification() = default;
  explicit Notification(const std::string& m) : method(m) {}

  template <typename T>
  Notification(const std::string& m, T&& p)
      : method(m), params(mcp::make_optional(std::forward<T>(p))) {}
};

// Factory functions for JSON-RPC
inline Request make_request(const RequestId& id, const std::string& method) {
  return Request(id, method);
}

inline Request make_request(const RequestId& id,
                            const std::string& method,
                            const Metadata& params) {
  return Request(id, method, params);
}

inline Request make_request(const RequestId& id,
                            const std::string& method,
                            Metadata&& params) {
  return Request(id, method, std::move(params));
}

inline Notification make_notification(const std::string& method) {
  return Notification(method);
}

template <typename T>
Notification make_notification(const std::string& method, T&& params) {
  return Notification(method, std::forward<T>(params));
}

template <typename T>
Response make_response(const RequestId& id, T&& result) {
  return Response::success(id, std::forward<T>(result));
}

inline Response make_error_response(const RequestId& id, const Error& error) {
  return Response::make_error(id, error);
}

inline Response make_error_response(const RequestId& id,
                                    int code,
                                    const std::string& message) {
  return Response::make_error(id, Error(code, message));
}

}  // namespace jsonrpc

// Resource contents variations
struct ResourceContents {
  optional<std::string> uri;
  optional<std::string> mimeType;

  ResourceContents() = default;
};

struct TextResourceContents : ResourceContents {
  std::string text;

  TextResourceContents() = default;
  explicit TextResourceContents(const std::string& t) : text(t) {}
};

struct BlobResourceContents : ResourceContents {
  std::string blob;  // Base64-encoded data

  BlobResourceContents() = default;
  explicit BlobResourceContents(const std::string& b) : blob(b) {}
};

// Factory functions for resource contents
inline TextResourceContents make_text_resource(const std::string& text) {
  return TextResourceContents(text);
}

inline BlobResourceContents make_blob_resource(const std::string& blob) {
  return BlobResourceContents(blob);
}

// Prompt message with embedded resources
struct PromptMessage {
  enums::Role::Value role;
  variant<TextContent, ImageContent, EmbeddedResource> content;

  PromptMessage() = default;
  PromptMessage(enums::Role::Value r, const TextContent& c)
      : role(r), content(c) {}
};

// Factory for prompt messages
inline PromptMessage make_prompt_message(enums::Role::Value role,
                                         const std::string& text) {
  return PromptMessage(role, TextContent(text));
}

// Sampling message
struct SamplingMessage {
  enums::Role::Value role;
  variant<TextContent, ImageContent, AudioContent> content;

  SamplingMessage() = default;
};

// Model hint
struct ModelHint {
  optional<std::string> name;

  ModelHint() = default;
  explicit ModelHint(const std::string& n) : name(mcp::make_optional(n)) {}
};

// Model preferences
struct ModelPreferences {
  optional<std::vector<ModelHint>> hints;
  optional<double> costPriority;          // 0-1, 0 = cost, 1 = quality
  optional<double> speedPriority;         // 0-1, 0 = speed, 1 = quality
  optional<double> intelligencePriority;  // 0-1, 0 = simpler, 1 = smarter

  ModelPreferences() = default;
};

// Builder for model preferences
class ModelPreferencesBuilder {
  ModelPreferences prefs_;

 public:
  ModelPreferencesBuilder() = default;

  ModelPreferencesBuilder& add_hint(const std::string& model_name) {
    if (!prefs_.hints) {
      prefs_.hints = mcp::make_optional(std::vector<ModelHint>());
    }
    prefs_.hints->push_back(ModelHint(model_name));
    return *this;
  }

  ModelPreferencesBuilder& cost_priority(double priority) {
    prefs_.costPriority = mcp::make_optional(priority);
    return *this;
  }

  ModelPreferencesBuilder& speed_priority(double priority) {
    prefs_.speedPriority = mcp::make_optional(priority);
    return *this;
  }

  ModelPreferencesBuilder& intelligence_priority(double priority) {
    prefs_.intelligencePriority = mcp::make_optional(priority);
    return *this;
  }

  ModelPreferences build() && { return std::move(prefs_); }
  ModelPreferences build() const& { return prefs_; }
};

inline ModelPreferencesBuilder build_model_preferences() {
  return ModelPreferencesBuilder();
}

// Root type for filesystem-like structures
struct Root {
  std::string uri;
  optional<std::string> name;

  Root() = default;
  Root(const std::string& u, const std::string& n)
      : uri(u), name(mcp::make_optional(n)) {}
};

// Factory for roots
inline Root make_root(const std::string& uri, const std::string& name) {
  return Root(uri, name);
}

// Schema types for elicitation
struct StringSchema {
  std::string type = "string";
  optional<std::string> description;
  optional<std::string> pattern;
  optional<int> minLength;
  optional<int> maxLength;

  StringSchema() = default;
};

struct NumberSchema {
  std::string type = "number";
  optional<std::string> description;
  optional<double> minimum;
  optional<double> maximum;
  optional<double> multipleOf;

  NumberSchema() = default;
};

struct BooleanSchema {
  std::string type = "boolean";
  optional<std::string> description;

  BooleanSchema() = default;
};

struct EnumSchema {
  std::string type = "enum";
  optional<std::string> description;
  std::vector<std::string> values;

  EnumSchema() = default;
  explicit EnumSchema(std::vector<std::string>&& vals)
      : values(std::move(vals)) {}
};

// Discriminated union for primitive schemas
using PrimitiveSchemaDefinition =
    variant<StringSchema, NumberSchema, BooleanSchema, EnumSchema>;

// Factory functions for schemas
inline PrimitiveSchemaDefinition make_string_schema() {
  return PrimitiveSchemaDefinition(StringSchema());
}

inline PrimitiveSchemaDefinition make_number_schema() {
  return PrimitiveSchemaDefinition(NumberSchema());
}

inline PrimitiveSchemaDefinition make_boolean_schema() {
  return PrimitiveSchemaDefinition(BooleanSchema());
}

inline PrimitiveSchemaDefinition make_enum_schema(
    std::vector<std::string>&& values) {
  return PrimitiveSchemaDefinition(EnumSchema(std::move(values)));
}

// Schema builders
class StringSchemaBuilder {
  StringSchema schema_;

 public:
  StringSchemaBuilder() = default;

  StringSchemaBuilder& description(const std::string& desc) {
    schema_.description = mcp::make_optional(desc);
    return *this;
  }

  StringSchemaBuilder& pattern(const std::string& regex) {
    schema_.pattern = mcp::make_optional(regex);
    return *this;
  }

  StringSchemaBuilder& min_length(int len) {
    schema_.minLength = mcp::make_optional(len);
    return *this;
  }

  StringSchemaBuilder& max_length(int len) {
    schema_.maxLength = mcp::make_optional(len);
    return *this;
  }

  StringSchema build() && { return std::move(schema_); }
  StringSchema build() const& { return schema_; }
};

inline StringSchemaBuilder build_string_schema() {
  return StringSchemaBuilder();
}

// Reference types
struct ResourceTemplateReference {
  std::string type;
  std::string name;

  ResourceTemplateReference() = default;
  ResourceTemplateReference(const std::string& t, const std::string& n)
      : type(t), name(n) {}
};

struct PromptReference : BaseMetadata {
  std::string type;
  std::string name;

  PromptReference() = default;
  PromptReference(const std::string& t, const std::string& n)
      : type(t), name(n) {}
};

// Factory functions for references
inline ResourceTemplateReference make_resource_template_ref(
    const std::string& type, const std::string& name) {
  return ResourceTemplateReference(type, name);
}

inline PromptReference make_prompt_ref(const std::string& type,
                                       const std::string& name) {
  return PromptReference(type, name);
}

// Pagination support
struct PaginatedRequest : jsonrpc::Request {
  optional<Cursor> cursor;

  PaginatedRequest() = default;
};

struct PaginatedResult {
  optional<Cursor> nextCursor;

  PaginatedResult() = default;
};

// Type aliases for JSON compatibility
using Cursor = std::string;
using EmptyCapability =
    std::map<std::string, nlohmann::json>;  // Empty capability objects
using JSONObject = nlohmann::json;

// Complex capability types for JSON compatibility
struct ResourcesCapability {
  optional<EmptyCapability> subscribe;
  optional<EmptyCapability> listChanged;
};

struct PromptsCapability {
  optional<EmptyCapability> listChanged;
};

struct RootsCapability {
  optional<EmptyCapability> listChanged;
};

// Capability types
struct ClientCapabilities {
  optional<Metadata> experimental;
  optional<SamplingParams> sampling;
  optional<RootsCapability> roots;

  ClientCapabilities() = default;
};

struct ServerCapabilities {
  optional<Metadata> experimental;
  optional<variant<bool, ResourcesCapability>> resources;
  optional<bool> tools;
  optional<bool> prompts;
  optional<bool> logging;

  ServerCapabilities() = default;
};

// Builder for capabilities
class ClientCapabilitiesBuilder {
  ClientCapabilities caps_;

 public:
  ClientCapabilitiesBuilder() = default;

  ClientCapabilitiesBuilder& experimental(const Metadata& metadata) {
    caps_.experimental = mcp::make_optional(metadata);
    return *this;
  }

  ClientCapabilitiesBuilder& sampling(const SamplingParams& params) {
    caps_.sampling = mcp::make_optional(params);
    return *this;
  }

  // Add missing methods that tests expect
  ClientCapabilitiesBuilder& resources(bool enabled) {
    if (!caps_.experimental) {
      caps_.experimental = mcp::make_optional(Metadata());
    }
    add_metadata(*caps_.experimental, "resources", enabled);
    return *this;
  }

  ClientCapabilitiesBuilder& tools(bool enabled) {
    if (!caps_.experimental) {
      caps_.experimental = mcp::make_optional(Metadata());
    }
    add_metadata(*caps_.experimental, "tools", enabled);
    return *this;
  }

  ClientCapabilities build() && { return std::move(caps_); }
  ClientCapabilities build() const& { return caps_; }
};

inline ClientCapabilitiesBuilder build_client_capabilities() {
  return ClientCapabilitiesBuilder();
}

class ServerCapabilitiesBuilder {
  ServerCapabilities caps_;

 public:
  ServerCapabilitiesBuilder() = default;

  ServerCapabilitiesBuilder& experimental(const Metadata& metadata) {
    caps_.experimental = mcp::make_optional(metadata);
    return *this;
  }

  ServerCapabilitiesBuilder& resources(bool enabled) {
    caps_.resources =
        mcp::make_optional(variant<bool, ResourcesCapability>(enabled));
    return *this;
  }

  ServerCapabilitiesBuilder& resources(const ResourcesCapability& res_caps) {
    caps_.resources =
        mcp::make_optional(variant<bool, ResourcesCapability>(res_caps));
    return *this;
  }

  ServerCapabilitiesBuilder& tools(bool enabled) {
    caps_.tools = mcp::make_optional(enabled);
    return *this;
  }

  ServerCapabilitiesBuilder& prompts(bool enabled) {
    caps_.prompts = mcp::make_optional(enabled);
    return *this;
  }

  ServerCapabilitiesBuilder& logging(bool enabled) {
    caps_.logging = mcp::make_optional(enabled);
    return *this;
  }

  ServerCapabilities build() && { return std::move(caps_); }
  ServerCapabilities build() const& { return caps_; }
};

inline ServerCapabilitiesBuilder build_server_capabilities() {
  return ServerCapabilitiesBuilder();
}

// Protocol message types

// Empty result type
struct EmptyResult {
  EmptyResult() = default;
};

// Implementation info
struct Implementation : BaseMetadata {
  std::string name;
  std::string version;

  Implementation() = default;
  Implementation(const std::string& n, const std::string& v)
      : name(n), version(v) {}
};

// Factory for implementation
inline Implementation make_implementation(const std::string& name,
                                          const std::string& version) {
  return Implementation(name, version);
}

// Type aliases that depend on Implementation
using ServerInfo = Implementation;  // Alias for consistency with spec
using ClientInfo = Implementation;  // Alias for consistency with spec

// Initialize request/response
struct InitializeRequest : jsonrpc::Request {
  std::string protocolVersion;
  ClientCapabilities capabilities;
  optional<Implementation> clientInfo;

  InitializeRequest() : jsonrpc::Request() { method = "initialize"; }
};

struct InitializeResult {
  std::string protocolVersion;
  ServerCapabilities capabilities;
  optional<Implementation> serverInfo;
  optional<std::string> instructions;

  InitializeResult() = default;
};

// Initialized notification
struct InitializedNotification : jsonrpc::Notification {
  InitializedNotification() : jsonrpc::Notification() {
    method = "initialized";
  }
};

// Ping request
struct PingRequest : jsonrpc::Request {
  PingRequest() : jsonrpc::Request() { method = "ping"; }
};

// Progress notification
struct ProgressNotification : jsonrpc::Notification {
  ProgressToken progressToken;
  double progress;  // 0.0 to 1.0
  optional<double> total;

  ProgressNotification() : jsonrpc::Notification() {
    method = "notifications/progress";
  }
};

// Cancelled notification
struct CancelledNotification : jsonrpc::Notification {
  RequestId requestId;
  optional<std::string> reason;

  CancelledNotification() : jsonrpc::Notification() {
    method = "notifications/cancelled";
  }
};

// Resource-related types
struct ListResourcesRequest : PaginatedRequest {
  ListResourcesRequest() : PaginatedRequest() { method = "resources/list"; }
};

struct ListResourcesResult : PaginatedResult {
  std::vector<Resource> resources;

  ListResourcesResult() = default;
};

struct ListResourceTemplatesRequest : PaginatedRequest {
  ListResourceTemplatesRequest() : PaginatedRequest() {
    method = "resources/templates/list";
  }
};

struct ResourceTemplate : BaseMetadata {
  std::string uriTemplate;
  std::string name;
  optional<std::string> description;
  optional<std::string> mimeType;

  ResourceTemplate() = default;
};

struct ListResourceTemplatesResult : PaginatedResult {
  std::vector<ResourceTemplate> resourceTemplates;

  ListResourceTemplatesResult() = default;
};

struct ReadResourceRequest : jsonrpc::Request {
  std::string uri;

  ReadResourceRequest() : jsonrpc::Request() { method = "resources/read"; }

  explicit ReadResourceRequest(const std::string& u) : ReadResourceRequest() {
    uri = u;
  }
};

struct ReadResourceResult {
  std::vector<variant<TextResourceContents, BlobResourceContents>> contents;

  ReadResourceResult() = default;
};

struct ResourceListChangedNotification : jsonrpc::Notification {
  ResourceListChangedNotification() : jsonrpc::Notification() {
    method = "notifications/resources/list_changed";
  }
};

struct SubscribeRequest : jsonrpc::Request {
  std::string uri;

  SubscribeRequest() : jsonrpc::Request() { method = "resources/subscribe"; }

  explicit SubscribeRequest(const std::string& u) : SubscribeRequest() {
    uri = u;
  }
};

struct UnsubscribeRequest : jsonrpc::Request {
  std::string uri;

  UnsubscribeRequest() : jsonrpc::Request() {
    method = "resources/unsubscribe";
  }

  explicit UnsubscribeRequest(const std::string& u) : UnsubscribeRequest() {
    uri = u;
  }
};

struct ResourceUpdatedNotification : jsonrpc::Notification {
  std::string uri;

  ResourceUpdatedNotification() : jsonrpc::Notification() {
    method = "notifications/resources/updated";
  }
};

// Prompt-related types
struct ListPromptsRequest : PaginatedRequest {
  ListPromptsRequest() : PaginatedRequest() { method = "prompts/list"; }
};

struct ListPromptsResult : PaginatedResult {
  std::vector<Prompt> prompts;

  ListPromptsResult() = default;
};

struct GetPromptRequest : jsonrpc::Request {
  std::string name;
  optional<Metadata> arguments;

  GetPromptRequest() : jsonrpc::Request() { method = "prompts/get"; }

  explicit GetPromptRequest(const std::string& n) : GetPromptRequest() {
    name = n;
  }
};

struct GetPromptResult {
  optional<std::string> description;
  std::vector<PromptMessage> messages;

  GetPromptResult() = default;
};

struct PromptListChangedNotification : jsonrpc::Notification {
  PromptListChangedNotification() : jsonrpc::Notification() {
    method = "notifications/prompts/list_changed";
  }
};

// Tool-related types
struct ListToolsRequest : PaginatedRequest {
  ListToolsRequest() : PaginatedRequest() { method = "tools/list"; }
};

struct ListToolsResult {
  std::vector<Tool> tools;

  ListToolsResult() = default;
};

struct CallToolRequest : jsonrpc::Request {
  std::string name;
  optional<Metadata> arguments;

  CallToolRequest() : jsonrpc::Request() { method = "tools/call"; }

  CallToolRequest(const std::string& n, const Metadata& args)
      : CallToolRequest() {
    name = n;
    arguments = mcp::make_optional(args);
  }
};

// CallToolResult with ExtendedContentBlock
struct CallToolResult {
  std::vector<ExtendedContentBlock> content;
  bool isError = false;

  CallToolResult() = default;
};

struct ToolListChangedNotification : jsonrpc::Notification {
  ToolListChangedNotification() : jsonrpc::Notification() {
    method = "notifications/tools/list_changed";
  }
};

// Logging types
struct SetLevelRequest : jsonrpc::Request {
  enums::LoggingLevel::Value level;

  SetLevelRequest() : jsonrpc::Request() { method = "logging/setLevel"; }

  explicit SetLevelRequest(enums::LoggingLevel::Value l) : SetLevelRequest() {
    level = l;
  }
};

struct LoggingMessageNotification : jsonrpc::Notification {
  enums::LoggingLevel::Value level;
  optional<std::string> logger;
  variant<std::string, Metadata> data;

  LoggingMessageNotification() : jsonrpc::Notification() {
    method = "notifications/message";
  }
};

// Completion types
struct CompleteRequest : jsonrpc::Request {
  PromptReference ref;
  optional<std::string> argument;

  CompleteRequest() : jsonrpc::Request() { method = "completion/complete"; }
};

struct CompleteResult {
  struct Completion {
    std::vector<std::string> values;
    optional<double> total;
    bool hasMore = false;

    Completion() = default;
  };

  Completion completion;

  CompleteResult() = default;
};

// Roots (filesystem-like) types
struct ListRootsRequest : jsonrpc::Request {
  ListRootsRequest() : jsonrpc::Request() { method = "roots/list"; }
};

struct ListRootsResult {
  std::vector<Root> roots;

  ListRootsResult() = default;
};

struct RootsListChangedNotification : jsonrpc::Notification {
  RootsListChangedNotification() : jsonrpc::Notification() {
    method = "notifications/roots/list_changed";
  }
};

// Sampling/message creation types
struct CreateMessageRequest : jsonrpc::Request {
  std::vector<SamplingMessage> messages;
  optional<ModelPreferences> modelPreferences;
  optional<std::string> systemPrompt;
  optional<Metadata> includeContext;
  optional<double> temperature;
  optional<int> maxTokens;
  optional<std::vector<std::string>> stopSequences;
  optional<Metadata> metadata;

  CreateMessageRequest() : jsonrpc::Request() {
    method = "sampling/createMessage";
  }
};

struct CreateMessageResult : SamplingMessage {
  std::string model;
  optional<std::string> stopReason;

  CreateMessageResult() = default;
};

// Elicitation types
struct ElicitRequest : jsonrpc::Request {
  std::string name;
  PrimitiveSchemaDefinition schema;
  optional<std::string> prompt;

  ElicitRequest() : jsonrpc::Request() { method = "elicit/createMessage"; }
};

struct ElicitResult {
  variant<std::string, double, bool, std::nullptr_t> value;

  ElicitResult() = default;
};

// Factory functions for requests
inline InitializeRequest make_initialize_request(
    const std::string& version, const ClientCapabilities& caps) {
  InitializeRequest req;
  req.protocolVersion = version;
  req.capabilities = caps;
  return req;
}

inline ProgressNotification make_progress_notification(
    const ProgressToken& token, double progress) {
  ProgressNotification notif;
  notif.progressToken = token;
  notif.progress = progress;
  return notif;
}

inline CancelledNotification make_cancelled_notification(
    const RequestId& id, const std::string& reason = "") {
  CancelledNotification notif;
  notif.requestId = id;
  if (!reason.empty()) {
    notif.reason = mcp::make_optional(reason);
  }
  return notif;
}

inline CallToolRequest make_call_tool_request(
    const std::string& name, const Metadata& arguments = make_metadata()) {
  return CallToolRequest(name, arguments);
}

inline LoggingMessageNotification make_log_notification(
    enums::LoggingLevel::Value level, const std::string& message) {
  LoggingMessageNotification notif;
  notif.level = level;
  notif.data = message;
  return notif;
}

// Builder for CreateMessageRequest
class CreateMessageRequestBuilder {
  CreateMessageRequest request_;

 public:
  CreateMessageRequestBuilder() = default;

  CreateMessageRequestBuilder& add_message(const SamplingMessage& msg) {
    request_.messages.push_back(msg);
    return *this;
  }

  CreateMessageRequestBuilder& add_user_message(const std::string& text) {
    SamplingMessage msg;
    msg.role = enums::Role::USER;
    msg.content = TextContent(text);
    request_.messages.push_back(msg);
    return *this;
  }

  CreateMessageRequestBuilder& add_assistant_message(const std::string& text) {
    SamplingMessage msg;
    msg.role = enums::Role::ASSISTANT;
    msg.content = TextContent(text);
    request_.messages.push_back(msg);
    return *this;
  }

  CreateMessageRequestBuilder& model_preferences(
      const ModelPreferences& prefs) {
    request_.modelPreferences = mcp::make_optional(prefs);
    return *this;
  }

  CreateMessageRequestBuilder& system_prompt(const std::string& prompt) {
    request_.systemPrompt = mcp::make_optional(prompt);
    return *this;
  }

  CreateMessageRequestBuilder& temperature(double temp) {
    request_.temperature = mcp::make_optional(temp);
    return *this;
  }

  CreateMessageRequestBuilder& max_tokens(int tokens) {
    request_.maxTokens = mcp::make_optional(tokens);
    return *this;
  }

  CreateMessageRequestBuilder& stop_sequence(const std::string& seq) {
    if (!request_.stopSequences) {
      request_.stopSequences = mcp::make_optional(std::vector<std::string>());
    }
    request_.stopSequences->push_back(seq);
    return *this;
  }

  CreateMessageRequest build() && { return std::move(request_); }
  CreateMessageRequest build() const& { return request_; }
};

inline CreateMessageRequestBuilder build_create_message_request() {
  return CreateMessageRequestBuilder();
}

// Additional factory functions for protocol types
inline CallToolRequest make_tool_call(const std::string& name) {
  CallToolRequest req;
  req.method = "tools/call";
  req.name = name;
  // Don't set arguments to leave it as nullopt
  return req;
}

inline CallToolRequest make_tool_call(const std::string& name,
                                      const Metadata& args) {
  return CallToolRequest(name, args);
}

inline CallToolResult make_tool_result(
    std::vector<ExtendedContentBlock>&& content) {
  CallToolResult result;
  result.content = std::move(content);
  return result;
}

inline CallToolResult make_tool_result(std::vector<ContentBlock>&& content) {
  CallToolResult result;
  // Convert ContentBlock to ExtendedContentBlock
  for (auto& cb : content) {
    if (mcp::holds_alternative<TextContent>(cb)) {
      result.content.push_back(ExtendedContentBlock(mcp::get<TextContent>(cb)));
    } else if (mcp::holds_alternative<ImageContent>(cb)) {
      result.content.push_back(
          ExtendedContentBlock(mcp::get<ImageContent>(cb)));
    } else if (mcp::holds_alternative<ResourceContent>(cb)) {
      auto& rc = mcp::get<ResourceContent>(cb);
      result.content.push_back(ExtendedContentBlock(ResourceLink(rc.resource)));
    }
  }
  return result;
}

// Builder for InitializeParams (for protocol compatibility)
struct InitializeParams {
  std::string protocolVersion;
  optional<std::string> clientName;
  optional<std::string> clientVersion;
  optional<Metadata> capabilities;
};

class InitializeParamsBuilder {
  InitializeParams params_;

 public:
  explicit InitializeParamsBuilder(const std::string& version) {
    params_.protocolVersion = version;
  }

  InitializeParamsBuilder& clientName(const std::string& name) {
    params_.clientName = mcp::make_optional(name);
    return *this;
  }

  InitializeParamsBuilder& clientVersion(const std::string& version) {
    params_.clientVersion = mcp::make_optional(version);
    return *this;
  }

  InitializeParamsBuilder& capability(const std::string& key, bool value) {
    if (!params_.capabilities) {
      params_.capabilities = mcp::make_optional(Metadata());
    }
    add_metadata(*params_.capabilities, key, value);
    return *this;
  }

  template <typename T>
  InitializeParamsBuilder& capability(const std::string& key, T&& value) {
    if (!params_.capabilities) {
      params_.capabilities = mcp::make_optional(Metadata());
    }
    add_metadata(*params_.capabilities, key, std::forward<T>(value));
    return *this;
  }

  InitializeParams build() && { return std::move(params_); }
  InitializeParams build() const& { return params_; }
};

inline InitializeParamsBuilder build_initialize_params(
    const std::string& version) {
  return InitializeParamsBuilder(version);
}

// Type unions for client/server messages
using ClientRequest = variant<InitializeRequest,
                              PingRequest,
                              ListResourcesRequest,
                              ListResourceTemplatesRequest,
                              ReadResourceRequest,
                              SubscribeRequest,
                              UnsubscribeRequest,
                              ListPromptsRequest,
                              GetPromptRequest,
                              ListToolsRequest,
                              CallToolRequest,
                              SetLevelRequest,
                              CreateMessageRequest,
                              CompleteRequest,
                              ListRootsRequest,
                              ElicitRequest>;

using ClientNotification =
    variant<InitializedNotification, CancelledNotification>;

using ServerRequest = variant<
    // Server can also make requests in some cases
    CreateMessageRequest,
    ElicitRequest>;

using ServerNotification = variant<ProgressNotification,
                                   ResourceListChangedNotification,
                                   ResourceUpdatedNotification,
                                   PromptListChangedNotification,
                                   ToolListChangedNotification,
                                   LoggingMessageNotification,
                                   RootsListChangedNotification>;

// Helper to determine message type
template <typename T>
struct MessageTypeHelper {
  static std::string get_method(const T& msg) {
    return match(
        msg, [](const auto& m) -> std::string { return get_method_impl(m); });
  }

 private:
  template <typename U>
  static std::string get_method_impl(
      const U& m,
      typename std::enable_if<
          std::is_base_of<jsonrpc::Request, U>::value ||
          std::is_base_of<jsonrpc::Notification, U>::value>::type* = nullptr) {
    return m.method;
  }

  template <typename U>
  static std::string get_method_impl(
      const U&,
      typename std::enable_if<
          !std::is_base_of<jsonrpc::Request, U>::value &&
          !std::is_base_of<jsonrpc::Notification, U>::value>::type* = nullptr) {
    return "";
  }
};

// Helper function for creating method requests (depends on RequestId)
template <typename T>
auto make_method_request(const RequestId& id,
                         const std::string& method,
                         T&& params)
    -> std::pair<RequestId, MethodDiscriminator<typename std::decay<T>::type>> {
  return std::make_pair(
      id, MethodDiscriminator<typename std::decay<T>::type>::create(
              method, std::forward<T>(params)));
}

}  // namespace mcp

#endif  // MCP_TYPES_H
