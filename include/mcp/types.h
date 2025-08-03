#ifndef MCP_TYPES_H
#define MCP_TYPES_H

#include <chrono>
#include <string>
#include <vector>

#include "mcp/type_helpers.h"

namespace mcp {

// Base types
struct TextContent {
  std::string type = "text";
  std::string text;

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

// Content block factory implementations
inline ContentBlock make_text_content(const std::string& text) {
  return ContentBlock(TextContent(text));
}

inline ContentBlock make_image_content(const std::string& data,
                                       const std::string& mime_type) {
  return ContentBlock(ImageContent(data, mime_type));
}

// Tool definitions
struct ToolParameter {
  std::string name;
  std::string type;
  optional<std::string> description;
  bool required = false;
};

struct Tool {
  std::string name;
  optional<std::string> description;
  optional<std::vector<ToolParameter>> parameters;

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
    resource_.description = make_optional(desc);
    return *this;
  }

  ResourceBuilder& mimeType(const std::string& mime) {
    resource_.mimeType = make_optional(mime);
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
    tool_.description = make_optional(desc);
    return *this;
  }

  ToolBuilder& parameter(const std::string& name,
                         const std::string& type,
                         bool required = false) {
    if (!tool_.parameters) {
      tool_.parameters = make_optional(std::vector<ToolParameter>());
    }
    tool_.parameters->push_back(ToolParameter{name, type, nullopt, required});
    return *this;
  }

  ToolBuilder& parameter(const std::string& name,
                         const std::string& type,
                         const std::string& desc,
                         bool required = false) {
    if (!tool_.parameters) {
      tool_.parameters = make_optional(std::vector<ToolParameter>());
    }
    tool_.parameters->push_back(
        ToolParameter{name, type, make_optional(desc), required});
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
    params_.temperature = make_optional(temp);
    return *this;
  }

  SamplingParamsBuilder& maxTokens(int max) {
    params_.maxTokens = make_optional(max);
    return *this;
  }

  SamplingParamsBuilder& stopSequence(const std::string& seq) {
    if (!params_.stopSequences) {
      params_.stopSequences = make_optional(std::vector<std::string>());
    }
    params_.stopSequences->push_back(seq);
    return *this;
  }

  SamplingParamsBuilder& metadata(const std::string& key,
                                  const std::string& value) {
    if (!params_.metadata) {
      params_.metadata = make_optional(Metadata());
    }
    add_metadata(*params_.metadata, key, value);
    return *this;
  }

  template <typename T>
  SamplingParamsBuilder& metadata(const std::string& key, T&& value) {
    if (!params_.metadata) {
      params_.metadata = make_optional(Metadata());
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
      : id(i), method(m), params(make_optional(p)) {}

  Request(const RequestId& i, const std::string& m, Metadata&& p)
      : id(i), method(m), params(make_optional(std::move(p))) {}
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
    r.result = make_optional(ResponseResult(std::forward<T>(result)));
    return r;
  }

  static Response make_error(const RequestId& id, const Error& err) {
    Response r(id);
    r.error = make_optional(err);
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
      : method(m), params(make_optional(std::forward<T>(p))) {}
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

// Specific MCP protocol messages
namespace protocol {

// Initialize request/response
struct InitializeParams {
  std::string protocolVersion;
  optional<std::string> clientName;
  optional<std::string> clientVersion;
  optional<Metadata> capabilities;
};

struct InitializeResult {
  std::string protocolVersion;
  optional<std::string> serverName;
  optional<std::string> serverVersion;
  optional<Metadata> capabilities;
};

// List tools request/response
struct ListToolsParams {};

struct ListToolsResult {
  std::vector<Tool> tools;
};

// Call tool request/response
struct CallToolParams {
  std::string name;
  optional<Metadata> arguments;

  // Conversion to Metadata for JSON-RPC
  Metadata to_metadata() const {
    Metadata m;
    add_metadata(m, "name", name);
    if (arguments) {
      // Create a nested metadata object for arguments
      for (const auto& kv : *arguments) {
        m["arguments." + kv.first] = kv.second;
      }
    }
    return m;
  }
};

struct CallToolResult {
  std::vector<ContentBlock> content;
};

// Factory functions
inline InitializeParams make_initialize_params(const std::string& version) {
  return InitializeParams{version, nullopt, nullopt, nullopt};
}

inline InitializeResult make_initialize_result(const std::string& version) {
  return InitializeResult{version, nullopt, nullopt, nullopt};
}

inline ListToolsResult make_tools_list(std::vector<Tool>&& tools) {
  return ListToolsResult{std::move(tools)};
}

inline CallToolParams make_tool_call(const std::string& name) {
  return CallToolParams{name, nullopt};
}

template <typename T>
CallToolParams make_tool_call(const std::string& name, T&& args) {
  return CallToolParams{name, make_optional(std::forward<T>(args))};
}

inline CallToolResult make_tool_result(std::vector<ContentBlock>&& content) {
  return CallToolResult{std::move(content)};
}

// Builder for InitializeParams
class InitializeParamsBuilder {
  InitializeParams params_;

 public:
  explicit InitializeParamsBuilder(const std::string& version) {
    params_.protocolVersion = version;
  }

  InitializeParamsBuilder& clientName(const std::string& name) {
    params_.clientName = make_optional(name);
    return *this;
  }

  InitializeParamsBuilder& clientVersion(const std::string& version) {
    params_.clientVersion = make_optional(version);
    return *this;
  }

  InitializeParamsBuilder& capability(const std::string& key, bool value) {
    if (!params_.capabilities) {
      params_.capabilities = make_optional(Metadata());
    }
    add_metadata(*params_.capabilities, key, value);
    return *this;
  }

  template <typename T>
  InitializeParamsBuilder& capability(const std::string& key, T&& value) {
    if (!params_.capabilities) {
      params_.capabilities = make_optional(Metadata());
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

}  // namespace protocol

// Additional factory functions that depend on protocol types
namespace jsonrpc {

inline Request make_request(const RequestId& id,
                            const std::string& method,
                            const protocol::CallToolParams& params) {
  return Request(id, method, params.to_metadata());
}

inline Request make_request(const RequestId& id,
                            const std::string& method,
                            const protocol::InitializeParams& params) {
  Metadata m;
  add_metadata(m, "protocolVersion", params.protocolVersion);
  if (params.clientName)
    add_metadata(m, "clientName", *params.clientName);
  if (params.clientVersion)
    add_metadata(m, "clientVersion", *params.clientVersion);
  // Note: capabilities would need custom serialization
  return Request(id, method, m);
}

}  // namespace jsonrpc

}  // namespace mcp

#endif  // MCP_TYPES_H
