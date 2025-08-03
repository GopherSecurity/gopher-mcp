#ifndef MCP_TYPES_EXTENDED_H
#define MCP_TYPES_EXTENDED_H

#include "mcp/types.h"

namespace mcp {

// Forward declarations
struct AudioContent;
struct ResourceLink;
struct EmbeddedResource;
struct BaseMetadata;
struct Annotations;
struct ToolAnnotations;
struct ModelHint;

// Update LoggingLevel enum with all RFC-5424 severities
namespace enums {
struct LoggingLevelExtended {
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
      return make_optional(DEBUG);
    if (s == "info")
      return make_optional(INFO);
    if (s == "notice")
      return make_optional(NOTICE);
    if (s == "warning")
      return make_optional(WARNING);
    if (s == "error")
      return make_optional(ERROR);
    if (s == "critical")
      return make_optional(CRITICAL);
    if (s == "alert")
      return make_optional(ALERT);
    if (s == "emergency")
      return make_optional(EMERGENCY);
    return nullopt;
  }
};
}  // namespace enums

// JSON-RPC error codes as constants
namespace jsonrpc {
constexpr int PARSE_ERROR = -32700;
constexpr int INVALID_REQUEST = -32600;
constexpr int METHOD_NOT_FOUND = -32601;
constexpr int INVALID_PARAMS = -32602;
constexpr int INTERNAL_ERROR = -32603;
}  // namespace jsonrpc

// Base metadata interface (replaces Annotated)
struct BaseMetadata {
  optional<Metadata> _meta;

  BaseMetadata() = default;
};

// Annotations for content blocks
struct Annotations {
  optional<std::vector<enums::Role::Value>> audience;
  optional<double> priority;  // 1.0 = most important

  Annotations() = default;
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

// Pagination support
struct PaginatedRequest : jsonrpc::Request {
  optional<Cursor> cursor;

  PaginatedRequest() = default;
};

struct PaginatedResult {
  optional<Cursor> nextCursor;

  PaginatedResult() = default;
};

// Capability types
struct ClientCapabilities {
  optional<Metadata> experimental;
  optional<SamplingParams> sampling;

  ClientCapabilities() = default;
};

struct ServerCapabilities {
  optional<Metadata> experimental;
  optional<bool> resources;
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
    caps_.experimental = make_optional(metadata);
    return *this;
  }

  ClientCapabilitiesBuilder& sampling(const SamplingParams& params) {
    caps_.sampling = make_optional(params);
    return *this;
  }

  // Add missing methods that tests expect
  ClientCapabilitiesBuilder& resources(bool enabled) {
    if (!caps_.experimental) {
      caps_.experimental = make_optional(Metadata());
    }
    add_metadata(*caps_.experimental, "resources", enabled);
    return *this;
  }

  ClientCapabilitiesBuilder& tools(bool enabled) {
    if (!caps_.experimental) {
      caps_.experimental = make_optional(Metadata());
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
    caps_.experimental = make_optional(metadata);
    return *this;
  }

  ServerCapabilitiesBuilder& resources(bool enabled) {
    caps_.resources = make_optional(enabled);
    return *this;
  }

  ServerCapabilitiesBuilder& tools(bool enabled) {
    caps_.tools = make_optional(enabled);
    return *this;
  }

  ServerCapabilitiesBuilder& prompts(bool enabled) {
    caps_.prompts = make_optional(enabled);
    return *this;
  }

  ServerCapabilitiesBuilder& logging(bool enabled) {
    caps_.logging = make_optional(enabled);
    return *this;
  }

  ServerCapabilities build() && { return std::move(caps_); }
  ServerCapabilities build() const& { return caps_; }
};

inline ServerCapabilitiesBuilder build_server_capabilities() {
  return ServerCapabilitiesBuilder();
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
  explicit ModelHint(const std::string& n) : name(make_optional(n)) {}
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
      prefs_.hints = make_optional(std::vector<ModelHint>());
    }
    prefs_.hints->push_back(ModelHint(model_name));
    return *this;
  }

  ModelPreferencesBuilder& cost_priority(double priority) {
    prefs_.costPriority = make_optional(priority);
    return *this;
  }

  ModelPreferencesBuilder& speed_priority(double priority) {
    prefs_.speedPriority = make_optional(priority);
    return *this;
  }

  ModelPreferencesBuilder& intelligence_priority(double priority) {
    prefs_.intelligencePriority = make_optional(priority);
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
      : uri(u), name(make_optional(n)) {}
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
    schema_.description = make_optional(desc);
    return *this;
  }

  StringSchemaBuilder& pattern(const std::string& regex) {
    schema_.pattern = make_optional(regex);
    return *this;
  }

  StringSchemaBuilder& min_length(int len) {
    schema_.minLength = make_optional(len);
    return *this;
  }

  StringSchemaBuilder& max_length(int len) {
    schema_.maxLength = make_optional(len);
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

}  // namespace mcp

#endif  // MCP_TYPES_EXTENDED_H