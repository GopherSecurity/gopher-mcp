#ifndef MCP_PROTOCOL_TYPES_H
#define MCP_PROTOCOL_TYPES_H

#include "mcp/types_extended.h"

namespace mcp {
namespace protocol {

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

// Initialize request/response (extending existing)
struct InitializeRequestExtended : jsonrpc::Request {
  std::string protocolVersion;
  ClientCapabilities capabilities;
  optional<Implementation> clientInfo;

  InitializeRequestExtended() : jsonrpc::Request() { method = "initialize"; }
};

struct InitializeResultExtended {
  std::string protocolVersion;
  ServerCapabilities capabilities;
  optional<Implementation> serverInfo;
  optional<std::string> instructions;

  InitializeResultExtended() = default;
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

// ListToolsResult is defined in types.h

struct CallToolRequest : jsonrpc::Request {
  std::string name;
  optional<Metadata> arguments;

  CallToolRequest() : jsonrpc::Request() { method = "tools/call"; }

  CallToolRequest(const std::string& n, const Metadata& args)
      : CallToolRequest() {
    name = n;
    arguments = make_optional(args);
  }
};

// CallToolResult is defined in types.h with ContentBlock,
// we'll use extended version here
struct CallToolResultExtended {
  std::vector<ExtendedContentBlock> content;
  bool isError = false;

  CallToolResultExtended() = default;
};

struct ToolListChangedNotification : jsonrpc::Notification {
  ToolListChangedNotification() : jsonrpc::Notification() {
    method = "notifications/tools/list_changed";
  }
};

// Logging types
struct SetLevelRequest : jsonrpc::Request {
  enums::LoggingLevelExtended::Value level;

  SetLevelRequest() : jsonrpc::Request() { method = "logging/setLevel"; }

  explicit SetLevelRequest(enums::LoggingLevelExtended::Value l)
      : SetLevelRequest() {
    level = l;
  }
};

struct LoggingMessageNotification : jsonrpc::Notification {
  enums::LoggingLevelExtended::Value level;
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
inline InitializeRequestExtended make_initialize_request(
    const std::string& version, const ClientCapabilities& caps) {
  InitializeRequestExtended req;
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
    notif.reason = make_optional(reason);
  }
  return notif;
}

inline CallToolRequest make_call_tool_request(
    const std::string& name, const Metadata& arguments = make_metadata()) {
  return CallToolRequest(name, arguments);
}

inline LoggingMessageNotification make_log_notification(
    enums::LoggingLevelExtended::Value level, const std::string& message) {
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
    request_.modelPreferences = make_optional(prefs);
    return *this;
  }

  CreateMessageRequestBuilder& system_prompt(const std::string& prompt) {
    request_.systemPrompt = make_optional(prompt);
    return *this;
  }

  CreateMessageRequestBuilder& temperature(double temp) {
    request_.temperature = make_optional(temp);
    return *this;
  }

  CreateMessageRequestBuilder& max_tokens(int tokens) {
    request_.maxTokens = make_optional(tokens);
    return *this;
  }

  CreateMessageRequestBuilder& stop_sequence(const std::string& seq) {
    if (!request_.stopSequences) {
      request_.stopSequences = make_optional(std::vector<std::string>());
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

// Type unions for client/server messages
using ClientRequest = variant<InitializeRequestExtended,
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

}  // namespace protocol
}  // namespace mcp

#endif  // MCP_PROTOCOL_TYPES_H