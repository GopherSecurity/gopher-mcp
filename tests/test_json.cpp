#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "mcp/json.h"
#include "mcp/types.h"

using namespace mcp;
using json = nlohmann::json;

class JsonSerializationTest : public ::testing::Test {
 protected:
  // Helper to test round-trip serialization
  template <typename T>
  void testRoundTrip(const T& value) {
    json j;
    to_json(j, value);

    T deserialized;
    from_json(j, deserialized);

    json j2;
    to_json(j2, deserialized);

    EXPECT_EQ(j, j2);
  }
};

// Basic types tests
TEST_F(JsonSerializationTest, Role) {
  json j = "user";
  enums::Role::Value role;
  from_json(j, role);
  EXPECT_EQ(role, enums::Role::USER);

  json j2;
  to_json(j2, role);
  EXPECT_EQ(j2, "user");
}

TEST_F(JsonSerializationTest, LoggingLevel) {
  std::vector<std::string> levels = {"debug",   "info",     "notice",
                                     "warning", "error",    "critical",
                                     "alert",   "emergency"};

  for (const auto& level_str : levels) {
    json j = level_str;
    enums::LoggingLevel::Value level;
    from_json(j, level);

    json j2;
    to_json(j2, level);
    EXPECT_EQ(j2, level_str);
  }
}

// Content types tests
TEST_F(JsonSerializationTest, TextContent) {
  TextContent content;
  content.text = "Hello, World!";
  content.annotations = Annotations();
  content.annotations.value().priority = 0.8;

  testRoundTrip(content);
}

TEST_F(JsonSerializationTest, ImageContent) {
  ImageContent content;
  content.data = "base64encodeddata==";
  content.mimeType = "image/png";

  testRoundTrip(content);
}

TEST_F(JsonSerializationTest, AudioContent) {
  AudioContent content;
  content.data = "base64audiodata==";
  content.mimeType = "audio/mp3";

  json j;
  to_json(j, content);
  EXPECT_EQ(j["type"], "audio");
  EXPECT_EQ(j["data"], "base64audiodata==");
  EXPECT_EQ(j["mimeType"], "audio/mp3");

  AudioContent deserialized;
  from_json(j, deserialized);
  EXPECT_EQ(deserialized.data, content.data);
  EXPECT_EQ(deserialized.mimeType, content.mimeType);
}

TEST_F(JsonSerializationTest, Resource) {
  Resource resource;
  resource.uri = "file:///path/to/resource.txt";
  resource.name = "Test Resource";
  resource.description = "A test resource";
  resource.mimeType = "text/plain";

  testRoundTrip(resource);
}

TEST_F(JsonSerializationTest, ResourceContent) {
  ResourceContent content;
  content.resource.uri = "file:///test.txt";
  content.resource.name = "Test";

  testRoundTrip(content);
}

TEST_F(JsonSerializationTest, ContentBlock) {
  // Text variant
  ContentBlock text_block = make_text_content("Hello");
  json j;
  to_json(j, text_block);
  EXPECT_EQ(j["type"], "text");
  EXPECT_EQ(j["text"], "Hello");

  ContentBlock deserialized;
  from_json(j, deserialized);
  EXPECT_TRUE(holds_alternative<TextContent>(deserialized));

  // Image variant
  ContentBlock image_block = make_image_content("data", "image/png");
  to_json(j, image_block);
  EXPECT_EQ(j["type"], "image");
  EXPECT_EQ(j["data"], "data");
  EXPECT_EQ(j["mimeType"], "image/png");
}

// Tool types tests
TEST_F(JsonSerializationTest, Tool) {
  Tool tool;
  tool.name = "calculator";
  tool.description = "Performs calculations";
  tool.inputSchema = json::parse(R"({
    "type": "object",
    "properties": {
      "expression": {"type": "string"}
    }
  })");

  testRoundTrip(tool);
}

// Prompt types tests
TEST_F(JsonSerializationTest, Prompt) {
  Prompt prompt;
  prompt.name = "code_review";
  prompt.description = "Reviews code";

  PromptArgument arg;
  arg.name = "code";
  arg.required = true;
  prompt.arguments = std::vector<PromptArgument>{arg};

  testRoundTrip(prompt);
}

// Server/Client info tests
TEST_F(JsonSerializationTest, Implementation) {
  Implementation impl;
  impl.name = "test-server";
  impl.version = "1.0.0";

  testRoundTrip(impl);
}

// Capabilities tests
TEST_F(JsonSerializationTest, ServerCapabilities) {
  ServerCapabilities caps;
  caps.tools = true;
  ResourcesCapability res_cap;
  res_cap.subscribe = EmptyCapability();
  caps.resources = res_cap;
  caps.prompts = true;
  caps.logging = true;

  json j;
  to_json(j, caps);

  EXPECT_TRUE(j["tools"].is_boolean());
  EXPECT_TRUE(j["resources"].is_object());
  EXPECT_TRUE(j["resources"]["subscribe"].is_object());
  EXPECT_TRUE(j["prompts"].is_boolean());
  EXPECT_TRUE(j["logging"].is_boolean());

  ServerCapabilities deserialized;
  from_json(j, deserialized);

  EXPECT_TRUE(deserialized.tools.has_value());
  EXPECT_TRUE(deserialized.resources.has_value());
  EXPECT_TRUE(
      holds_alternative<ResourcesCapability>(deserialized.resources.value()));
  auto& res = get<ResourcesCapability>(deserialized.resources.value());
  EXPECT_TRUE(res.subscribe.has_value());
  EXPECT_TRUE(deserialized.prompts.has_value());
  EXPECT_TRUE(deserialized.logging.has_value());
}

TEST_F(JsonSerializationTest, ClientCapabilities) {
  ClientCapabilities caps;
  RootsCapability roots;
  roots.listChanged = EmptyCapability();
  caps.roots = roots;
  SamplingParams sampling;
  sampling.temperature = 0.7;
  caps.sampling = sampling;

  testRoundTrip(caps);
}

// Request/Response tests
TEST_F(JsonSerializationTest, InitializeRequest) {
  InitializeRequest req;
  req.jsonrpc = "2.0";
  req.method = "initialize";
  req.id = make_request_id("init-123");
  req.protocolVersion = "1.0";
  Implementation clientInfo;
  clientInfo.name = "Test Client";
  clientInfo.version = "1.0.0";
  req.clientInfo = clientInfo;
  req.capabilities = ClientCapabilities();

  testRoundTrip(req);
}

TEST_F(JsonSerializationTest, InitializeResult) {
  InitializeResult result;
  result.protocolVersion = "1.0";
  Implementation serverInfo;
  serverInfo.name = "Test Server";
  serverInfo.version = "1.0.0";
  result.serverInfo = serverInfo;
  ServerCapabilities caps;
  caps.tools = true;
  result.capabilities = caps;

  testRoundTrip(result);
}

TEST_F(JsonSerializationTest, CallToolResult) {
  CallToolResult result;
  TextContent tc;
  tc.type = "text";
  tc.text = "Result: 42";
  result.content.push_back(tc);
  result.isError = false;

  json j;
  to_json(j, result);

  EXPECT_TRUE(j["content"].is_array());
  EXPECT_EQ(j["content"][0]["type"], "text");
  EXPECT_EQ(j["content"][0]["text"], "Result: 42");
  EXPECT_FALSE(j.contains("isError"));  // Default false not serialized

  CallToolResult deserialized;
  from_json(j, deserialized);

  EXPECT_EQ(deserialized.content.size(), 1u);
  EXPECT_FALSE(deserialized.isError);
}

// Metadata tests - Metadata is a map<string, MetadataValue> where MetadataValue
// is a variant
TEST_F(JsonSerializationTest, Metadata) {
  Metadata meta;
  meta["string"] = std::string("value");
  meta["number"] = int64_t(42);
  meta["float"] = 3.14;
  meta["bool"] = true;
  meta["null"] = nullptr;
  // Note: Metadata only supports basic types, not arrays or objects

  json j;
  to_json(j, meta);

  EXPECT_EQ(j["string"], "value");
  EXPECT_EQ(j["number"], 42);
  EXPECT_DOUBLE_EQ(j["float"].get<double>(), 3.14);
  EXPECT_EQ(j["bool"], true);
  EXPECT_TRUE(j["null"].is_null());

  Metadata deserialized;
  from_json(j, deserialized);

  // Check the variant values
  EXPECT_TRUE(holds_alternative<std::string>(deserialized["string"]));
  EXPECT_EQ(get<std::string>(deserialized["string"]), "value");
  EXPECT_TRUE(holds_alternative<int64_t>(deserialized["number"]));
  EXPECT_EQ(get<int64_t>(deserialized["number"]), 42);
  EXPECT_TRUE(holds_alternative<double>(deserialized["float"]));
  EXPECT_DOUBLE_EQ(get<double>(deserialized["float"]), 3.14);
  EXPECT_TRUE(holds_alternative<bool>(deserialized["bool"]));
  EXPECT_EQ(get<bool>(deserialized["bool"]), true);
  EXPECT_TRUE(holds_alternative<std::nullptr_t>(deserialized["null"]));
}

TEST_F(JsonSerializationTest, PromptMessage) {
  // Test PromptMessage structure
  PromptMessage msg;
  msg.role = enums::Role::USER;
  TextContent tc;
  tc.type = "text";
  tc.text = "Hello";
  msg.content = tc;

  json j;
  to_json(j, msg);

  EXPECT_EQ(j["role"], "user");
  EXPECT_EQ(j["content"]["type"], "text");
  EXPECT_EQ(j["content"]["text"], "Hello");

  PromptMessage deserialized;
  from_json(j, deserialized);

  EXPECT_EQ(deserialized.role, enums::Role::USER);
  EXPECT_TRUE(holds_alternative<TextContent>(deserialized.content));
}

// Edge cases tests
TEST_F(JsonSerializationTest, OptionalFields) {
  // Test with all optional fields missing
  Resource resource;
  resource.uri = "file:///required.txt";
  resource.name = "Required name";  // name is required

  json j;
  to_json(j, resource);

  EXPECT_EQ(j["uri"], "file:///required.txt");
  EXPECT_EQ(j["name"], "Required name");
  EXPECT_FALSE(j.contains("description"));
  EXPECT_FALSE(j.contains("mimeType"));

  Resource deserialized;
  from_json(j, deserialized);

  EXPECT_EQ(deserialized.uri, resource.uri);
  EXPECT_EQ(deserialized.name, resource.name);
  EXPECT_FALSE(deserialized.description.has_value());
  EXPECT_FALSE(deserialized.mimeType.has_value());
}

// Error handling tests
TEST_F(JsonSerializationTest, InvalidJsonThrows) {
  // Missing required field
  json j = json::object();

  EXPECT_THROW(
      {
        Resource resource;
        from_json(j, resource);
      },
      std::exception);

  // Invalid type field
  j = json{{"type", "invalid_type"}};

  EXPECT_THROW(
      {
        ContentBlock block;
        from_json(j, block);
      },
      std::runtime_error);

  // Invalid enum value
  j = "invalid_level";
  EXPECT_THROW(
      {
        enums::LoggingLevel::Value level;
        from_json(j, level);
      },
      std::runtime_error);
}

TEST_F(JsonSerializationTest, ExtendedContentBlock) {
  // Test ResourceLink variant
  ResourceLink link;
  link.uri = "https://example.com/resource";
  link.name = "Example Resource";
  link.type = "resource";

  ExtendedContentBlock block = link;
  json j;
  to_json(j, block);

  EXPECT_EQ(j["type"], "resource");
  EXPECT_EQ(j["uri"], "https://example.com/resource");
  EXPECT_EQ(j["name"], "Example Resource");

  ExtendedContentBlock deserialized;
  from_json(j, deserialized);
  EXPECT_TRUE(holds_alternative<ResourceLink>(deserialized));
}

TEST_F(JsonSerializationTest, ResourceTemplate) {
  ResourceTemplate tmpl;
  tmpl.uriTemplate = "/files/{path}";
  tmpl.name = "File Browser";
  tmpl.description = "Browse files in the system";

  testRoundTrip(tmpl);
}

TEST_F(JsonSerializationTest, Root) {
  Root root;
  root.uri = "file:///home/user/project";
  root.name = "My Project";

  testRoundTrip(root);
}

TEST_F(JsonSerializationTest, ModelPreferences) {
  ModelPreferences prefs;
  prefs.costPriority = 0.3;
  prefs.speedPriority = 0.5;
  prefs.intelligencePriority = 0.2;

  ModelHint hint;
  hint.name = "gpt-4";
  prefs.hints = std::vector<ModelHint>{hint};

  testRoundTrip(prefs);
}

TEST_F(JsonSerializationTest, SamplingMessage) {
  SamplingMessage msg;
  msg.role = enums::Role::ASSISTANT;
  TextContent tc;
  tc.type = "text";
  tc.text = "Here's the answer";
  msg.content = tc;

  testRoundTrip(msg);
}

TEST_F(JsonSerializationTest, CreateMessageResult) {
  CreateMessageResult result;
  result.role = enums::Role::ASSISTANT;
  TextContent tc;
  tc.type = "text";
  tc.text = "Generated response";
  result.content = tc;
  result.model = "claude-3";
  result.stopReason = "max_tokens";

  testRoundTrip(result);
}

TEST_F(JsonSerializationTest, Error) {
  Error err;
  err.code = -32600;
  err.message = "Invalid Request";
  err.data = std::string("Missing required field");

  json j;
  to_json(j, err);

  EXPECT_EQ(j["code"], -32600);
  EXPECT_EQ(j["message"], "Invalid Request");
  EXPECT_EQ(j["data"], "Missing required field");

  Error deserialized;
  from_json(j, deserialized);

  EXPECT_EQ(deserialized.code, err.code);
  EXPECT_EQ(deserialized.message, err.message);
  EXPECT_TRUE(deserialized.data.has_value());
  EXPECT_TRUE(holds_alternative<std::string>(deserialized.data.value()));
}

// Tests for new JSON serialization functions

// BaseMetadata tests
TEST_F(JsonSerializationTest, BaseMetadata) {
  BaseMetadata meta;
  Metadata m;
  m["key1"] = std::string("value1");
  m["key2"] = int64_t(42);
  meta._meta = m;

  testRoundTrip(meta);
}

// JSON-RPC base types tests
TEST_F(JsonSerializationTest, JsonRpcRequest) {
  jsonrpc::Request req;
  req.jsonrpc = "2.0";
  req.method = "test/method";
  req.id = make_request_id("test-123");
  json params = json::object();
  params["param1"] = "value1";
  req.params = params;

  json j;
  to_json(j, req);

  EXPECT_EQ(j["jsonrpc"], "2.0");
  EXPECT_EQ(j["method"], "test/method");
  EXPECT_EQ(j["id"], "test-123");
  EXPECT_EQ(j["params"]["param1"], "value1");

  jsonrpc::Request deserialized;
  from_json(j, deserialized);

  EXPECT_EQ(deserialized.jsonrpc, req.jsonrpc);
  EXPECT_EQ(deserialized.method, req.method);
}

TEST_F(JsonSerializationTest, JsonRpcResponse) {
  jsonrpc::Response resp;
  resp.jsonrpc = "2.0";
  resp.id = make_request_id(123);
  resp.result = jsonrpc::ResponseResult(std::string("success"));

  json j;
  to_json(j, resp);

  EXPECT_EQ(j["jsonrpc"], "2.0");
  EXPECT_EQ(j["id"], 123);
  EXPECT_EQ(j["result"], "success");
  EXPECT_FALSE(j.contains("error"));

  jsonrpc::Response deserialized;
  from_json(j, deserialized);

  EXPECT_TRUE(deserialized.result.has_value());
  EXPECT_TRUE(holds_alternative<std::string>(deserialized.result.value()));
}

TEST_F(JsonSerializationTest, JsonRpcResponseWithError) {
  jsonrpc::Response resp;
  resp.jsonrpc = "2.0";
  resp.id = make_request_id("error-test");
  Error err;
  err.code = -32600;
  err.message = "Invalid Request";
  resp.error = err;

  testRoundTrip(resp);
}

TEST_F(JsonSerializationTest, JsonRpcNotification) {
  jsonrpc::Notification notif;
  notif.jsonrpc = "2.0";
  notif.method = "notification/test";
  json params = json::object();
  params["data"] = "test data";
  notif.params = params;

  testRoundTrip(notif);
}

// Paginated types tests
TEST_F(JsonSerializationTest, PaginatedRequest) {
  PaginatedRequest req;
  req.jsonrpc = "2.0";
  req.method = "resources/list";
  req.id = make_request_id("paginated-1");
  req.cursor = "next-page-token";

  json j;
  to_json(j, req);

  EXPECT_EQ(j["params"]["cursor"], "next-page-token");

  PaginatedRequest deserialized;
  from_json(j, deserialized);

  EXPECT_TRUE(deserialized.cursor.has_value());
  EXPECT_EQ(deserialized.cursor.value(), "next-page-token");
}

TEST_F(JsonSerializationTest, PaginatedResult) {
  PaginatedResult result;
  result.nextCursor = "continue-from-here";

  testRoundTrip(result);
}

// Request message tests
TEST_F(JsonSerializationTest, PingRequest) {
  PingRequest req;
  req.id = make_request_id("ping-1");

  json j;
  to_json(j, req);

  EXPECT_EQ(j["method"], "ping");

  PingRequest deserialized;
  from_json(j, deserialized);

  EXPECT_EQ(deserialized.method, "ping");
}

TEST_F(JsonSerializationTest, ListResourcesRequest) {
  ListResourcesRequest req;
  req.id = make_request_id("list-res-1");
  req.cursor = "page-2";

  testRoundTrip(req);
}

TEST_F(JsonSerializationTest, ListResourcesResult) {
  ListResourcesResult result;
  result.nextCursor = "page-3";

  Resource res1;
  res1.uri = "file:///test1.txt";
  res1.name = "Test 1";
  res1.mimeType = "text/plain";

  Resource res2;
  res2.uri = "file:///test2.txt";
  res2.name = "Test 2";

  result.resources.push_back(res1);
  result.resources.push_back(res2);

  testRoundTrip(result);
}

TEST_F(JsonSerializationTest, ReadResourceRequest) {
  ReadResourceRequest req("file:///test.txt");
  req.id = make_request_id("read-1");

  json j;
  to_json(j, req);

  EXPECT_EQ(j["params"]["uri"], "file:///test.txt");

  ReadResourceRequest deserialized;
  from_json(j, deserialized);

  EXPECT_EQ(deserialized.uri, "file:///test.txt");
}

TEST_F(JsonSerializationTest, ReadResourceResult) {
  ReadResourceResult result;

  TextResourceContents text;
  text.uri = "file:///text.txt";
  text.mimeType = "text/plain";
  text.text = "Hello, World!";

  BlobResourceContents blob;
  blob.uri = "file:///image.png";
  blob.mimeType = "image/png";
  blob.blob = "base64encodeddata==";

  result.contents.push_back(text);
  result.contents.push_back(blob);

  json j;
  to_json(j, result);

  EXPECT_EQ(j["contents"].size(), 2u);
  EXPECT_EQ(j["contents"][0]["type"], "text");
  EXPECT_EQ(j["contents"][0]["text"], "Hello, World!");
  EXPECT_EQ(j["contents"][1]["type"], "blob");
  EXPECT_EQ(j["contents"][1]["blob"], "base64encodeddata==");

  ReadResourceResult deserialized;
  from_json(j, deserialized);

  EXPECT_EQ(deserialized.contents.size(), 2u);
}

TEST_F(JsonSerializationTest, GetPromptRequest) {
  GetPromptRequest req("test-prompt");
  req.id = make_request_id("prompt-1");
  Metadata args;
  args["arg1"] = std::string("value1");
  req.arguments = args;

  testRoundTrip(req);
}

TEST_F(JsonSerializationTest, GetPromptResult) {
  GetPromptResult result;
  result.description = "Test prompt";

  PromptMessage msg;
  msg.role = enums::Role::USER;
  TextContent tc;
  tc.text = "Test message";
  msg.content = tc;

  result.messages.push_back(msg);

  testRoundTrip(result);
}

TEST_F(JsonSerializationTest, CreateMessageRequest) {
  CreateMessageRequest req;
  req.id = make_request_id("create-msg-1");

  SamplingMessage msg;
  msg.role = enums::Role::USER;
  TextContent tc;
  tc.text = "Hello AI";
  msg.content = tc;
  req.messages.push_back(msg);

  ModelPreferences prefs;
  prefs.costPriority = 0.3;
  prefs.speedPriority = 0.7;
  req.modelPreferences = prefs;

  req.temperature = 0.8;
  req.maxTokens = 1000;
  req.systemPrompt = "You are a helpful assistant";

  testRoundTrip(req);
}

// Notification message tests
TEST_F(JsonSerializationTest, InitializedNotification) {
  InitializedNotification notif;

  json j;
  to_json(j, notif);

  EXPECT_EQ(j["method"], "initialized");

  InitializedNotification deserialized;
  from_json(j, deserialized);

  EXPECT_EQ(deserialized.method, "initialized");
}

TEST_F(JsonSerializationTest, ProgressNotification) {
  ProgressNotification notif;
  notif.progressToken = make_progress_token("task-123");
  notif.progress = 0.75;
  notif.total = 100.0;

  json j;
  to_json(j, notif);

  EXPECT_EQ(j["method"], "notifications/progress");
  EXPECT_EQ(j["params"]["progressToken"], "task-123");
  EXPECT_EQ(j["params"]["progress"], 0.75);
  EXPECT_EQ(j["params"]["total"], 100.0);

  ProgressNotification deserialized;
  from_json(j, deserialized);

  EXPECT_EQ(deserialized.progress, 0.75);
  EXPECT_TRUE(deserialized.total.has_value());
  EXPECT_EQ(deserialized.total.value(), 100.0);
}

TEST_F(JsonSerializationTest, CancelledNotification) {
  CancelledNotification notif;
  notif.requestId = make_request_id(456);
  notif.reason = "User cancelled";

  testRoundTrip(notif);
}

TEST_F(JsonSerializationTest, LoggingMessageNotification) {
  LoggingMessageNotification notif;
  notif.level = enums::LoggingLevel::ERROR;
  notif.logger = "test.logger";
  notif.data = std::string("Error message");

  json j;
  to_json(j, notif);

  EXPECT_EQ(j["params"]["level"], "error");
  EXPECT_EQ(j["params"]["logger"], "test.logger");
  EXPECT_EQ(j["params"]["data"], "Error message");

  LoggingMessageNotification deserialized;
  from_json(j, deserialized);

  EXPECT_EQ(deserialized.level, enums::LoggingLevel::ERROR);
  EXPECT_TRUE(holds_alternative<std::string>(deserialized.data));
}

// Schema type tests
TEST_F(JsonSerializationTest, StringSchema) {
  StringSchema schema;
  schema.description = "A test string";
  schema.minLength = 5;
  schema.maxLength = 20;
  schema.pattern = "^[A-Za-z]+$";

  json j;
  to_json(j, schema);

  EXPECT_EQ(j["type"], "string");
  EXPECT_EQ(j["description"], "A test string");
  EXPECT_EQ(j["minLength"], 5);
  EXPECT_EQ(j["maxLength"], 20);
  EXPECT_EQ(j["pattern"], "^[A-Za-z]+$");

  StringSchema deserialized;
  from_json(j, deserialized);

  EXPECT_TRUE(deserialized.minLength.has_value());
  EXPECT_EQ(deserialized.minLength.value(), 5);
}

TEST_F(JsonSerializationTest, NumberSchema) {
  NumberSchema schema;
  schema.description = "A test number";
  schema.minimum = 0.0;
  schema.maximum = 100.0;
  schema.multipleOf = 0.5;

  testRoundTrip(schema);
}

TEST_F(JsonSerializationTest, BooleanSchema) {
  BooleanSchema schema;
  schema.description = "A test boolean";

  json j;
  to_json(j, schema);

  EXPECT_EQ(j["type"], "boolean");
  EXPECT_EQ(j["description"], "A test boolean");

  BooleanSchema deserialized;
  from_json(j, deserialized);

  EXPECT_TRUE(deserialized.description.has_value());
}

TEST_F(JsonSerializationTest, EnumSchema) {
  EnumSchema schema;
  schema.description = "Choose one";
  schema.values = {"option1", "option2", "option3"};

  json j;
  to_json(j, schema);

  EXPECT_EQ(j["type"], "string");
  EXPECT_EQ(j["enum"].size(), 3u);
  EXPECT_EQ(j["enum"][0], "option1");

  EnumSchema deserialized;
  from_json(j, deserialized);

  EXPECT_EQ(deserialized.values.size(), 3u);
}

TEST_F(JsonSerializationTest, PrimitiveSchemaDefinition) {
  // Test with StringSchema
  PrimitiveSchemaDefinition def = StringSchema();
  json j;
  to_json(j, def);
  EXPECT_EQ(j["type"], "string");

  // Test with EnumSchema
  EnumSchema e;
  e.values = {"a", "b", "c"};
  def = e;
  to_json(j, def);
  EXPECT_EQ(j["type"], "string");
  EXPECT_EQ(j["enum"].size(), 3u);

  // Test deserialization
  json enum_json =
      json{{"type", "string"}, {"enum", json::array({"x", "y", "z"})}};
  PrimitiveSchemaDefinition deserialized;
  from_json(enum_json, deserialized);
  EXPECT_TRUE(holds_alternative<EnumSchema>(deserialized));
}

// Other type tests
TEST_F(JsonSerializationTest, Message) {
  Message msg;
  msg.role = enums::Role::ASSISTANT;
  TextContent tc;
  tc.text = "Hello!";
  msg.content = tc;

  json j;
  to_json(j, msg);

  EXPECT_EQ(j["role"], "assistant");
  EXPECT_EQ(j["content"]["type"], "text");
  EXPECT_EQ(j["content"]["text"], "Hello!");

  Message deserialized;
  from_json(j, deserialized);

  EXPECT_EQ(deserialized.role, enums::Role::ASSISTANT);
}

TEST_F(JsonSerializationTest, ToolAnnotations) {
  ToolAnnotations ann;
  ann.audience = std::vector<enums::Role::Value>{enums::Role::USER,
                                                 enums::Role::ASSISTANT};

  json j;
  to_json(j, ann);

  EXPECT_EQ(j["audience"].size(), 2u);
  EXPECT_EQ(j["audience"][0], "user");
  EXPECT_EQ(j["audience"][1], "assistant");

  ToolAnnotations deserialized;
  from_json(j, deserialized);

  EXPECT_TRUE(deserialized.audience.has_value());
  EXPECT_EQ(deserialized.audience.value().size(), 2u);
}

TEST_F(JsonSerializationTest, PromptsCapability) {
  PromptsCapability cap;
  cap.listChanged = EmptyCapability();

  json j;
  to_json(j, cap);

  EXPECT_TRUE(j["listChanged"].is_object());

  PromptsCapability deserialized;
  from_json(j, deserialized);

  EXPECT_TRUE(deserialized.listChanged.has_value());
}

TEST_F(JsonSerializationTest, ResourceContents) {
  ResourceContents contents;
  contents.uri = "file:///test.txt";
  contents.mimeType = "text/plain";

  testRoundTrip(contents);
}

TEST_F(JsonSerializationTest, TextResourceContents) {
  TextResourceContents contents;
  contents.uri = "file:///doc.txt";
  contents.mimeType = "text/plain";
  contents.text = "Document content";

  json j;
  to_json(j, contents);

  EXPECT_EQ(j["type"], "text");
  EXPECT_EQ(j["uri"], "file:///doc.txt");
  EXPECT_EQ(j["text"], "Document content");

  TextResourceContents deserialized;
  from_json(j, deserialized);

  EXPECT_EQ(deserialized.text, "Document content");
}

TEST_F(JsonSerializationTest, BlobResourceContents) {
  BlobResourceContents contents;
  contents.uri = "file:///image.png";
  contents.mimeType = "image/png";
  contents.blob =
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA"
      "60e6kgAAAABJRU5ErkJggg==";

  testRoundTrip(contents);
}

TEST_F(JsonSerializationTest, PromptReference) {
  PromptReference ref;
  ref.name = "test-prompt";
  ref._meta = Metadata();
  ref._meta.value()["custom"] = std::string("data");

  testRoundTrip(ref);
}

TEST_F(JsonSerializationTest, ResourceTemplateReference) {
  ResourceTemplateReference ref;
  ref.type = "file";
  ref.name = "document-template";

  json j;
  to_json(j, ref);

  EXPECT_EQ(j["type"], "file");
  EXPECT_EQ(j["name"], "document-template");

  ResourceTemplateReference deserialized;
  from_json(j, deserialized);

  EXPECT_EQ(deserialized.type, "file");
  EXPECT_EQ(deserialized.name, "document-template");
}

TEST_F(JsonSerializationTest, CompleteRequest) {
  CompleteRequest req;
  req.id = make_request_id("complete-1");
  PromptReference ref;
  ref.name = "code-complete";
  req.ref = ref;
  req.argument = "def hello";

  testRoundTrip(req);
}

TEST_F(JsonSerializationTest, CompleteResult) {
  CompleteResult result;
  result.completion.values = {"world", "there", "everyone"};
  result.completion.total = 3.0;
  result.completion.hasMore = false;

  json j;
  to_json(j, result);

  EXPECT_EQ(j["completion"]["values"].size(), 3u);
  EXPECT_EQ(j["completion"]["total"], 3.0);
  EXPECT_FALSE(j["completion"]["hasMore"]);

  CompleteResult deserialized;
  from_json(j, deserialized);

  EXPECT_EQ(deserialized.completion.values.size(), 3u);
}

TEST_F(JsonSerializationTest, ElicitRequest) {
  ElicitRequest req;
  req.id = make_request_id("elicit-1");
  req.name = "user-input";
  StringSchema schema;
  schema.description = "Enter your name";
  req.schema = schema;
  req.prompt = "What is your name?";

  testRoundTrip(req);
}

TEST_F(JsonSerializationTest, ElicitResult) {
  // Test with string value
  ElicitResult result;
  result.value = std::string("John Doe");

  json j;
  to_json(j, result);
  EXPECT_EQ(j["value"], "John Doe");

  ElicitResult deserialized;
  from_json(j, deserialized);
  EXPECT_TRUE(holds_alternative<std::string>(deserialized.value));
  EXPECT_EQ(get<std::string>(deserialized.value), "John Doe");

  // Test with number value
  result.value = 42.5;
  to_json(j, result);
  EXPECT_EQ(j["value"], 42.5);

  // Test with boolean value
  result.value = true;
  to_json(j, result);
  EXPECT_EQ(j["value"], true);

  // Test with null value
  result.value = nullptr;
  to_json(j, result);
  EXPECT_TRUE(j["value"].is_null());
}

TEST_F(JsonSerializationTest, InitializeParams) {
  InitializeParams params;
  params.protocolVersion = "1.0";
  params.clientName = "test-client";
  params.clientVersion = "0.1.0";
  Metadata caps;
  caps["feature1"] = true;
  params.capabilities = caps;

  testRoundTrip(params);
}

// Test complex nested structures
TEST_F(JsonSerializationTest, ComplexNestedStructure) {
  // Create a complex CreateMessageRequest with multiple nested types
  CreateMessageRequest req;
  req.id = make_request_id("complex-1");

  // Add messages with different content types
  SamplingMessage msg1;
  msg1.role = enums::Role::USER;
  TextContent tc;
  tc.text = "Show me an image";
  tc.annotations = Annotations();
  tc.annotations.value().priority = 0.9;
  msg1.content = tc;
  req.messages.push_back(msg1);

  SamplingMessage msg2;
  msg2.role = enums::Role::ASSISTANT;
  ImageContent ic;
  ic.data = "base64imagedata==";
  ic.mimeType = "image/png";
  msg2.content = ic;
  req.messages.push_back(msg2);

  // Add model preferences
  ModelPreferences prefs;
  prefs.costPriority = 0.2;
  prefs.speedPriority = 0.3;
  prefs.intelligencePriority = 0.5;
  ModelHint hint;
  hint.name = "gpt-4";
  prefs.hints = std::vector<ModelHint>{hint};
  req.modelPreferences = prefs;

  // Add other parameters
  req.systemPrompt = "You are a helpful assistant";
  req.temperature = 0.7;
  req.maxTokens = 2000;
  req.stopSequences = std::vector<std::string>{"END", "STOP"};

  Metadata meta;
  meta["session_id"] = std::string("sess-123");
  meta["user_id"] = int64_t(456);
  req.metadata = meta;

  // Test round-trip
  testRoundTrip(req);
}

// Test for SubscribeRequest/UnsubscribeRequest
TEST_F(JsonSerializationTest, SubscribeUnsubscribeRequests) {
  SubscribeRequest sub("file:///watch/this.txt");
  sub.id = make_request_id("sub-1");

  json j;
  to_json(j, sub);
  EXPECT_EQ(j["method"], "resources/subscribe");
  EXPECT_EQ(j["params"]["uri"], "file:///watch/this.txt");

  SubscribeRequest deserialized;
  from_json(j, deserialized);
  EXPECT_EQ(deserialized.uri, "file:///watch/this.txt");

  // Test unsubscribe
  UnsubscribeRequest unsub("file:///watch/this.txt");
  unsub.id = make_request_id("unsub-1");
  testRoundTrip(unsub);
}

// Test for ListPromptsRequest/Result
TEST_F(JsonSerializationTest, ListPromptsRequestResult) {
  ListPromptsRequest req;
  req.id = make_request_id("list-prompts-1");
  req.cursor = "next-page";

  testRoundTrip(req);

  ListPromptsResult result;
  result.nextCursor = "page-3";

  Prompt p1;
  p1.name = "greeting";
  p1.description = "Greet the user";
  PromptArgument arg;
  arg.name = "username";
  arg.required = true;
  p1.arguments = std::vector<PromptArgument>{arg};

  result.prompts.push_back(p1);

  testRoundTrip(result);
}

// Test for ListToolsRequest/Result
TEST_F(JsonSerializationTest, ListToolsRequestResult) {
  ListToolsRequest req;
  req.id = make_request_id("list-tools-1");

  testRoundTrip(req);

  ListToolsResult result;
  Tool tool;
  tool.name = "calculator";
  tool.description = "Performs calculations";
  result.tools.push_back(tool);

  testRoundTrip(result);
}

// Test for SetLevelRequest
TEST_F(JsonSerializationTest, SetLevelRequest) {
  SetLevelRequest req(enums::LoggingLevel::WARNING);
  req.id = make_request_id("set-level-1");

  json j;
  to_json(j, req);
  EXPECT_EQ(j["params"]["level"], "warning");

  SetLevelRequest deserialized;
  from_json(j, deserialized);
  EXPECT_EQ(deserialized.level, enums::LoggingLevel::WARNING);
}

// Test for ListRootsRequest/Result
TEST_F(JsonSerializationTest, ListRootsRequestResult) {
  ListRootsRequest req;
  req.id = make_request_id("list-roots-1");

  testRoundTrip(req);

  ListRootsResult result;
  Root root;
  root.uri = "file:///home/user/project";
  root.name = "My Project";
  result.roots.push_back(root);

  testRoundTrip(result);
}

// Test remaining notification types
TEST_F(JsonSerializationTest, ResourceNotifications) {
  ResourceListChangedNotification listChanged;
  testRoundTrip(listChanged);

  ResourceUpdatedNotification updated;
  updated.uri = "file:///changed.txt";

  json j;
  to_json(j, updated);
  EXPECT_EQ(j["params"]["uri"], "file:///changed.txt");

  ResourceUpdatedNotification deserialized;
  from_json(j, deserialized);
  EXPECT_EQ(deserialized.uri, "file:///changed.txt");
}

TEST_F(JsonSerializationTest, PromptListChangedNotification) {
  PromptListChangedNotification notif;
  testRoundTrip(notif);
}

TEST_F(JsonSerializationTest, ToolListChangedNotification) {
  ToolListChangedNotification notif;
  testRoundTrip(notif);
}

TEST_F(JsonSerializationTest, RootsListChangedNotification) {
  RootsListChangedNotification notif;
  testRoundTrip(notif);
}

// Test EmptyCapability and EmptyResult
TEST_F(JsonSerializationTest, EmptyTypes) {
  EmptyCapability cap;
  json j;
  to_json(j, cap);
  EXPECT_TRUE(j.is_object());
  EXPECT_TRUE(j.empty());

  EmptyResult result;
  to_json(j, result);
  EXPECT_TRUE(j.is_object());
  EXPECT_TRUE(j.empty());
}

// Test ToolParameter
TEST_F(JsonSerializationTest, ToolParameter) {
  ToolParameter param;
  param.name = "input";
  param.description = "The input value";
  param.required = true;

  testRoundTrip(param);
}

// Test ToolInputSchema
TEST_F(JsonSerializationTest, ToolInputSchema) {
  ToolInputSchema schema = json::parse(R"({
    "type": "object",
    "properties": {
      "expression": {"type": "string"}
    },
    "required": ["expression"]
  })");

  json j;
  to_json(j, schema);
  EXPECT_EQ(j["type"], "object");
  EXPECT_TRUE(j.contains("properties"));

  ToolInputSchema deserialized;
  from_json(j, deserialized);
  EXPECT_EQ(deserialized["type"], "object");
}

// Test variant error handling
TEST_F(JsonSerializationTest, RequestIdErrorHandling) {
  json j = json::array({1, 2, 3});  // Invalid type for RequestId

  EXPECT_THROW(
      {
        RequestId id;
        from_json(j, id);
      },
      std::runtime_error);
}

// Test LoggingMessageNotification with Metadata data
TEST_F(JsonSerializationTest, LoggingMessageNotificationWithMetadata) {
  LoggingMessageNotification notif;
  notif.level = enums::LoggingLevel::INFO;
  Metadata data;
  data["event"] = std::string("user_login");
  data["user_id"] = int64_t(12345);
  notif.data = data;

  json j;
  to_json(j, notif);
  EXPECT_TRUE(j["params"]["data"].is_object());
  EXPECT_EQ(j["params"]["data"]["event"], "user_login");
  EXPECT_EQ(j["params"]["data"]["user_id"], 12345);

  LoggingMessageNotification deserialized;
  from_json(j, deserialized);
  EXPECT_TRUE(holds_alternative<Metadata>(deserialized.data));
}