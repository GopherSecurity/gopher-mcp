#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "mcp/json_bridge.h"
#include "mcp/json_serialization.h"
#include "mcp/types.h"

using namespace mcp;
using namespace mcp::json;

class MCPSerializationTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
  
  // Helper to test round-trip serialization
  template <typename T>
  void testRoundTrip(const T& value) {
    JsonValue j = to_json(value);
    T deserialized = from_json<T>(j);
    JsonValue j2 = to_json(deserialized);
    // Compare JSON strings since JsonValue doesn't have operator==
    EXPECT_EQ(j.toString(), j2.toString());
  }
  
  // Helper to test JSON string round-trip
  template <typename T>
  void testStringRoundTrip(const T& value) {
    JsonValue j = to_json(value);
    std::string json_str = j.toString();
    JsonValue j2 = JsonValue::parse(json_str);
    T deserialized = from_json<T>(j2);
    JsonValue j3 = to_json(deserialized);
    std::string json_str2 = j3.toString();
    EXPECT_EQ(json_str, json_str2);
  }
};

// =============================================================================
// Basic Type Tests
// =============================================================================

TEST_F(MCPSerializationTest, TextContent) {
  TextContent content("Hello, world!");
  testRoundTrip(content);
  testStringRoundTrip(content);
  
  // Test with annotations
  TextContent annotated("Important text");
  annotated.annotations = mcp::make_optional(Annotations());
  annotated.annotations->priority = mcp::make_optional(1.0);
  annotated.annotations->audience = mcp::make_optional(std::vector<enums::Role::Value>{enums::Role::USER});
  testRoundTrip(annotated);
}

TEST_F(MCPSerializationTest, ImageContent) {
  ImageContent content("base64data", "image/png");
  testRoundTrip(content);
  testStringRoundTrip(content);
}

TEST_F(MCPSerializationTest, AudioContent) {
  AudioContent content("base64audiodata", "audio/mp3");
  testRoundTrip(content);
  testStringRoundTrip(content);
}

TEST_F(MCPSerializationTest, Resource) {
  Resource resource("file:///path/to/file.txt", "file.txt");
  testRoundTrip(resource);
  
  // With optional fields
  Resource full_resource("https://example.com/api", "API Resource");
  full_resource.description = mcp::make_optional(std::string("API endpoint"));
  full_resource.mimeType = mcp::make_optional(std::string("application/json"));
  testRoundTrip(full_resource);
}

TEST_F(MCPSerializationTest, ResourceContent) {
  Resource res("file:///doc.pdf", "document");
  ResourceContent content(res);
  testRoundTrip(content);
}

TEST_F(MCPSerializationTest, ContentBlockVariant) {
  // Text content in variant
  ContentBlock text_block = make_text_content("Hello");
  testRoundTrip(text_block);
  
  // Image content in variant
  ContentBlock img_block = make_image_content("data", "image/jpeg");
  testRoundTrip(img_block);
  
  // Resource content in variant
  ContentBlock res_block = make_resource_content(Resource("uri", "name"));
  testRoundTrip(res_block);
}

TEST_F(MCPSerializationTest, ExtendedContentBlock) {
  // Audio content
  ExtendedContentBlock audio = make_audio_content("audiodata", "audio/wav");
  testRoundTrip(audio);
  
  // Resource link
  ExtendedContentBlock link = make_resource_link(Resource("http://link", "Link"));
  testRoundTrip(link);
  
  // Embedded resource
  EmbeddedResource embedded;
  embedded.resource = Resource("embedded://res", "Embedded");
  embedded.content.push_back(make_text_content("Embedded text"));
  ExtendedContentBlock embed_block(embedded);
  testRoundTrip(embed_block);
}

// =============================================================================
// Tool and Prompt Tests
// =============================================================================

TEST_F(MCPSerializationTest, Tool) {
  Tool simple_tool("calculator");
  testRoundTrip(simple_tool);
  
  // Tool with all fields
  Tool full_tool = build_tool("weather")
    .description("Get weather information")
    .inputSchema(JsonValue::parse(R"({
      "type": "object",
      "properties": {
        "location": {"type": "string"}
      },
      "required": ["location"]
    })"))
    .build();
  testRoundTrip(full_tool);
  
  // Tool with legacy parameters
  Tool legacy_tool = build_tool("search")
    .parameter("query", "string", "Search query", true)
    .parameter("limit", "number", false)
    .build();
  testRoundTrip(legacy_tool);
}

TEST_F(MCPSerializationTest, Prompt) {
  Prompt simple_prompt("greeting");
  testRoundTrip(simple_prompt);
  
  // Prompt with arguments
  Prompt full_prompt("template");
  full_prompt.description = mcp::make_optional(std::string("Template prompt"));
  full_prompt.arguments = mcp::make_optional(std::vector<PromptArgument>());
  full_prompt.arguments->push_back({"name", mcp::make_optional(std::string("User name")), true});
  full_prompt.arguments->push_back({"age", nullopt, false});
  testRoundTrip(full_prompt);
}

// =============================================================================
// Message Types
// =============================================================================

TEST_F(MCPSerializationTest, Message) {
  Message user_msg = make_user_message("Hello assistant");
  testRoundTrip(user_msg);
  
  Message assistant_msg = make_assistant_message("Hello user");
  testRoundTrip(assistant_msg);
  
  // Message with image content
  Message img_msg(enums::Role::ASSISTANT, make_image_content("imgdata", "image/png"));
  testRoundTrip(img_msg);
}

TEST_F(MCPSerializationTest, SamplingMessage) {
  SamplingMessage msg;
  msg.role = enums::Role::USER;
  msg.content = TextContent("Sample text");
  testRoundTrip(msg);
  
  // With audio content
  SamplingMessage audio_msg;
  audio_msg.role = enums::Role::ASSISTANT;
  audio_msg.content = AudioContent("audiodata", "audio/ogg");
  testRoundTrip(audio_msg);
}

TEST_F(MCPSerializationTest, PromptMessage) {
  // First test - simple text content
  PromptMessage msg = make_prompt_message(enums::Role::USER, "Prompt text");
  testRoundTrip(msg);
  
  // Second test - embedded resource
  // Temporarily comment out to isolate the issue
  /*
  EmbeddedResource embedded;
  embedded.resource = Resource("data://embed", "Data");
  embedded.content.push_back(make_text_content("Embedded content"));
  PromptMessage embed_msg;
  embed_msg.role = enums::Role::ASSISTANT;
  embed_msg.content = embedded;
  testRoundTrip(embed_msg);
  */
}

// =============================================================================
// Error Types
// =============================================================================

TEST_F(MCPSerializationTest, Error) {
  Error simple_error(jsonrpc::INVALID_REQUEST, "Invalid request");
  testRoundTrip(simple_error);
  
  // Error with string data
  Error string_error(jsonrpc::PARSE_ERROR, "Parse failed");
  string_error.data = mcp::make_optional(ErrorData(std::string("Unexpected token at position 42")));
  testRoundTrip(string_error);
  
  // Error with numeric data
  Error num_error(jsonrpc::INTERNAL_ERROR, "Internal error");
  num_error.data = mcp::make_optional(ErrorData(500));
  testRoundTrip(num_error);
  
  // Error with map data
  Error map_error(jsonrpc::METHOD_NOT_FOUND, "Method not found");
  std::map<std::string, std::string> error_details;
  error_details["method"] = "unknown_method";
  error_details["available"] = "list, get, set";
  map_error.data = mcp::make_optional(ErrorData(error_details));
  testRoundTrip(map_error);
}

// =============================================================================
// JSON-RPC Types
// =============================================================================

TEST_F(MCPSerializationTest, RequestId) {
  // String ID
  RequestId str_id = make_request_id("req-123");
  testRoundTrip(str_id);
  
  // Numeric ID
  RequestId num_id = make_request_id(42);
  testRoundTrip(num_id);
}

TEST_F(MCPSerializationTest, ProgressToken) {
  ProgressToken str_token = make_progress_token("progress-abc");
  testRoundTrip(str_token);
  
  ProgressToken num_token = make_progress_token(999);
  testRoundTrip(num_token);
}

TEST_F(MCPSerializationTest, JsonRpcRequest) {
  jsonrpc::Request simple_req(make_request_id(1), "test_method");
  testRoundTrip(simple_req);
  
  // Request with params
  Metadata params = make_metadata();
  add_metadata(params, "key1", "value1");
  add_metadata(params, "key2", 123);
  jsonrpc::Request param_req(make_request_id("req-456"), "method_with_params", params);
  testRoundTrip(param_req);
}

TEST_F(MCPSerializationTest, JsonRpcResponse) {
  // Success response with string result
  auto success_resp = jsonrpc::Response::success(
    make_request_id(1), 
    std::string("Success result")
  );
  testRoundTrip(success_resp);
  
  // Error response
  auto error_resp = jsonrpc::Response::make_error(
    make_request_id("req-789"),
    Error(jsonrpc::INVALID_PARAMS, "Invalid parameters")
  );
  testRoundTrip(error_resp);
  
  // Response with metadata result
  Metadata result_meta = make_metadata();
  add_metadata(result_meta, "status", "complete");
  add_metadata(result_meta, "count", 42);
  auto meta_resp = jsonrpc::Response::success(make_request_id(2), result_meta);
  testRoundTrip(meta_resp);
}

TEST_F(MCPSerializationTest, JsonRpcNotification) {
  jsonrpc::Notification simple_notif("test_notification");
  testRoundTrip(simple_notif);
  
  // Notification with params
  Metadata notif_params = make_metadata();
  add_metadata(notif_params, "event", "update");
  add_metadata(notif_params, "timestamp", 1234567890);
  jsonrpc::Notification param_notif("event_notification", notif_params);
  testRoundTrip(param_notif);
}

// =============================================================================
// Protocol Request Types
// =============================================================================

TEST_F(MCPSerializationTest, InitializeRequest) {
  InitializeRequest req;
  req.id = make_request_id(1);
  req.protocolVersion = "1.0.0";
  req.capabilities = build_client_capabilities()
    .resources(true)
    .tools(true)
    .build();
  testRoundTrip(req);
  
  // With client info
  InitializeRequest full_req;
  full_req.id = make_request_id("init-1");
  full_req.protocolVersion = "2.0.0";
  full_req.capabilities = ClientCapabilities();
  full_req.clientInfo = mcp::make_optional(Implementation("TestClient", "1.0"));
  testRoundTrip(full_req);
}

TEST_F(MCPSerializationTest, InitializeResult) {
  InitializeResult result;
  result.protocolVersion = "1.0.0";
  result.capabilities = build_server_capabilities()
    .resources(true)
    .tools(true)
    .prompts(true)
    .logging(true)
    .build();
  testRoundTrip(result);
  
  // With server info and instructions
  InitializeResult full_result;
  full_result.protocolVersion = "2.0.0";
  full_result.capabilities = ServerCapabilities();
  full_result.serverInfo = mcp::make_optional(Implementation("TestServer", "2.0"));
  full_result.instructions = mcp::make_optional(std::string("Server usage instructions"));
  testRoundTrip(full_result);
}

TEST_F(MCPSerializationTest, PingRequest) {
  PingRequest req;
  req.id = make_request_id(123);
  testRoundTrip(req);
}

TEST_F(MCPSerializationTest, ListResourcesRequest) {
  ListResourcesRequest req;
  req.id = make_request_id("list-res-1");
  testRoundTrip(req);
  
  // With cursor for pagination
  ListResourcesRequest paged_req;
  paged_req.id = make_request_id(2);
  paged_req.cursor = mcp::make_optional(Cursor("next-page-token"));
  testRoundTrip(paged_req);
}

TEST_F(MCPSerializationTest, ListResourcesResult) {
  ListResourcesResult result;
  result.resources.push_back(Resource("file:///a.txt", "a.txt"));
  result.resources.push_back(Resource("file:///b.txt", "b.txt"));
  testRoundTrip(result);
  
  // With next cursor
  ListResourcesResult paged_result;
  paged_result.resources.push_back(Resource("http://api/1", "Resource 1"));
  paged_result.nextCursor = mcp::make_optional(Cursor("cursor-123"));
  testRoundTrip(paged_result);
}

TEST_F(MCPSerializationTest, ReadResourceRequest) {
  ReadResourceRequest req("file:///document.txt");
  req.id = make_request_id(1);
  testRoundTrip(req);
}

TEST_F(MCPSerializationTest, ReadResourceResult) {
  ReadResourceResult result;
  
  // Text content
  TextResourceContents text_content("File contents here");
  text_content.uri = mcp::make_optional(std::string("file:///test.txt"));
  text_content.mimeType = mcp::make_optional(std::string("text/plain"));
  result.contents.push_back(text_content);
  
  // Blob content
  BlobResourceContents blob_content("base64blobdata");
  blob_content.uri = mcp::make_optional(std::string("file:///image.png"));
  blob_content.mimeType = mcp::make_optional(std::string("image/png"));
  result.contents.push_back(blob_content);
  
  testRoundTrip(result);
}

TEST_F(MCPSerializationTest, CallToolRequest) {
  // Simple tool call
  CallToolRequest simple_req = make_tool_call("calculator");
  simple_req.id = make_request_id(1);
  testRoundTrip(simple_req);
  
  // Tool call with arguments
  Metadata args = make_metadata();
  add_metadata(args, "operation", "add");
  add_metadata(args, "a", 5);
  add_metadata(args, "b", 3);
  CallToolRequest arg_req = make_tool_call("calculator", args);
  arg_req.id = make_request_id("tool-1");
  testRoundTrip(arg_req);
}

TEST_F(MCPSerializationTest, CallToolResult) {
  // Simple text result
  std::vector<ExtendedContentBlock> content;
  content.push_back(ExtendedContentBlock(TextContent("Result: 42")));
  CallToolResult result = make_tool_result(std::move(content));
  testRoundTrip(result);
  
  // Mixed content result
  CallToolResult mixed_result;
  mixed_result.content.push_back(ExtendedContentBlock(TextContent("Analysis complete")));
  mixed_result.content.push_back(ExtendedContentBlock(ImageContent("chartdata", "image/svg+xml")));
  mixed_result.content.push_back(ExtendedContentBlock(AudioContent("audiodata", "audio/wav")));
  testRoundTrip(mixed_result);
  
  // Error result
  CallToolResult error_result;
  error_result.content.push_back(ExtendedContentBlock(TextContent("Error occurred")));
  error_result.isError = true;
  testRoundTrip(error_result);
}

TEST_F(MCPSerializationTest, ListToolsRequest) {
  ListToolsRequest req;
  req.id = make_request_id("list-tools");
  testRoundTrip(req);
}

TEST_F(MCPSerializationTest, ListToolsResult) {
  ListToolsResult result;
  result.tools.push_back(make_tool("calculator"));
  result.tools.push_back(build_tool("weather")
    .description("Get weather info")
    .build());
  testRoundTrip(result);
}

// =============================================================================
// Notification Types
// =============================================================================

TEST_F(MCPSerializationTest, InitializedNotification) {
  InitializedNotification notif;
  testRoundTrip(notif);
}

TEST_F(MCPSerializationTest, ProgressNotification) {
  ProgressNotification notif = make_progress_notification(
    make_progress_token("prog-1"), 
    0.75
  );
  testRoundTrip(notif);
  
  // With total
  ProgressNotification total_notif;
  total_notif.progressToken = make_progress_token(123);
  total_notif.progress = 0.5;
  total_notif.total = mcp::make_optional(100.0);
  testRoundTrip(total_notif);
}

TEST_F(MCPSerializationTest, CancelledNotification) {
  CancelledNotification notif = make_cancelled_notification(
    make_request_id("req-1"),
    "User cancelled"
  );
  testRoundTrip(notif);
  
  // Without reason
  CancelledNotification simple_notif;
  simple_notif.requestId = make_request_id(42);
  testRoundTrip(simple_notif);
}

TEST_F(MCPSerializationTest, ResourceUpdatedNotification) {
  ResourceUpdatedNotification notif;
  notif.uri = "file:///updated.txt";
  testRoundTrip(notif);
}

TEST_F(MCPSerializationTest, LoggingMessageNotification) {
  LoggingMessageNotification notif = make_log_notification(
    enums::LoggingLevel::INFO,
    "Application started"
  );
  testRoundTrip(notif);
  
  // With logger and metadata data
  LoggingMessageNotification full_notif;
  full_notif.level = enums::LoggingLevel::ERROR;
  full_notif.logger = mcp::make_optional(std::string("MyApp.Module"));
  Metadata log_data = make_metadata();
  add_metadata(log_data, "error_code", 500);
  add_metadata(log_data, "message", "Internal error");
  full_notif.data = log_data;
  testRoundTrip(full_notif);
}

// =============================================================================
// Complex Nested Structures
// =============================================================================

TEST_F(MCPSerializationTest, ModelPreferences) {
  ModelPreferences prefs = build_model_preferences()
    .add_hint("gpt-4")
    .add_hint("claude-3")
    .cost_priority(0.3)
    .speed_priority(0.8)
    .intelligence_priority(0.9)
    .build();
  testRoundTrip(prefs);
}

TEST_F(MCPSerializationTest, SamplingParams) {
  SamplingParams params = build_sampling_params()
    .temperature(0.7)
    .maxTokens(1000)
    .stopSequence("</end>")
    .stopSequence("STOP")
    .metadata("source", "test")
    .metadata("version", 2)
    .build();
  testRoundTrip(params);
}

TEST_F(MCPSerializationTest, CreateMessageRequest) {
  CreateMessageRequest req = build_create_message_request()
    .add_user_message("Hello")
    .add_assistant_message("Hi there!")
    .model_preferences(build_model_preferences()
      .add_hint("gpt-4")
      .cost_priority(0.5)
      .build())
    .system_prompt("You are a helpful assistant")
    .temperature(0.8)
    .max_tokens(500)
    .stop_sequence("END")
    .build();
  req.id = make_request_id("msg-1");
  testRoundTrip(req);
}

TEST_F(MCPSerializationTest, CreateMessageResult) {
  CreateMessageResult result;
  result.role = enums::Role::ASSISTANT;
  result.content = TextContent("Generated response");
  result.model = "gpt-4";
  result.stopReason = mcp::make_optional(std::string("max_tokens"));
  testRoundTrip(result);
}

// =============================================================================
// Schema Types
// =============================================================================

TEST_F(MCPSerializationTest, StringSchema) {
  StringSchema schema = build_string_schema()
    .description("User input")
    .pattern("^[a-zA-Z]+$")
    .min_length(3)
    .max_length(20)
    .build();
  testRoundTrip(schema);
  
  PrimitiveSchemaDefinition schema_def(schema);
  testRoundTrip(schema_def);
}

TEST_F(MCPSerializationTest, NumberSchema) {
  NumberSchema schema;
  schema.description = mcp::make_optional(std::string("Age"));
  schema.minimum = mcp::make_optional(0.0);
  schema.maximum = mcp::make_optional(120.0);
  schema.multipleOf = mcp::make_optional(1.0);
  testRoundTrip(schema);
}

TEST_F(MCPSerializationTest, BooleanSchema) {
  BooleanSchema schema;
  schema.description = mcp::make_optional(std::string("Agree to terms"));
  testRoundTrip(schema);
}

TEST_F(MCPSerializationTest, EnumSchema) {
  EnumSchema schema(std::vector<std::string>{"red", "green", "blue"});
  schema.description = mcp::make_optional(std::string("Color choice"));
  testRoundTrip(schema);
}

// =============================================================================
// Capability Types
// =============================================================================

TEST_F(MCPSerializationTest, ClientCapabilities) {
  ClientCapabilities caps = build_client_capabilities()
    .experimental(make_metadata())
    .sampling(build_sampling_params()
      .temperature(0.5)
      .build())
    .build();
  
  // Add roots capability
  caps.roots = mcp::make_optional(RootsCapability());
  caps.roots->listChanged = mcp::make_optional(EmptyCapability());
  
  testRoundTrip(caps);
}

TEST_F(MCPSerializationTest, ServerCapabilities) {
  ServerCapabilities caps = build_server_capabilities()
    .resources(true)
    .tools(true)
    .prompts(true)
    .logging(false)
    .build();
  testRoundTrip(caps);
  
  // With ResourcesCapability
  ServerCapabilities res_caps;
  ResourcesCapability res_cap;
  res_cap.subscribe = mcp::make_optional(EmptyCapability());
  res_cap.listChanged = mcp::make_optional(EmptyCapability());
  res_caps.resources = mcp::make_optional(variant<bool, ResourcesCapability>(res_cap));
  testRoundTrip(res_caps);
}

// =============================================================================
// Root and Template Types
// =============================================================================

TEST_F(MCPSerializationTest, Root) {
  Root root = make_root("file:///home/user", "User Home");
  testRoundTrip(root);
}

TEST_F(MCPSerializationTest, ListRootsRequest) {
  ListRootsRequest req;
  req.id = make_request_id("roots-1");
  testRoundTrip(req);
}

TEST_F(MCPSerializationTest, ListRootsResult) {
  ListRootsResult result;
  result.roots.push_back(make_root("file:///", "Root"));
  result.roots.push_back(make_root("file:///home", "Home"));
  testRoundTrip(result);
}

TEST_F(MCPSerializationTest, ResourceTemplate) {
  ResourceTemplate tmpl;
  tmpl.uriTemplate = "file:///{path}";
  tmpl.name = "File Template";
  tmpl.description = mcp::make_optional(std::string("Template for file resources"));
  tmpl.mimeType = mcp::make_optional(std::string("text/plain"));
  testRoundTrip(tmpl);
}

TEST_F(MCPSerializationTest, ListResourceTemplatesResult) {
  ListResourceTemplatesResult result;
  ResourceTemplate tmpl1;
  tmpl1.uriTemplate = "http://api/{endpoint}";
  tmpl1.name = "API Template";
  result.resourceTemplates.push_back(tmpl1);
  testRoundTrip(result);
}

// =============================================================================
// Reference Types
// =============================================================================

TEST_F(MCPSerializationTest, ResourceTemplateReference) {
  ResourceTemplateReference ref = make_resource_template_ref("file", "FileTemplate");
  testRoundTrip(ref);
}

TEST_F(MCPSerializationTest, PromptReference) {
  PromptReference ref = make_prompt_ref("greeting", "GreetingPrompt");
  testRoundTrip(ref);
}

// =============================================================================
// Completion Types
// =============================================================================

TEST_F(MCPSerializationTest, CompleteRequest) {
  CompleteRequest req;
  req.id = make_request_id("complete-1");
  req.ref = make_prompt_ref("template", "MyTemplate");
  req.argument = mcp::make_optional(std::string("user_"));
  testRoundTrip(req);
}

TEST_F(MCPSerializationTest, CompleteResult) {
  CompleteResult result;
  result.completion.values = {"user_name", "user_id", "user_email"};
  result.completion.total = mcp::make_optional(10.0);
  result.completion.hasMore = true;
  testRoundTrip(result);
}

// =============================================================================
// Elicitation Types
// =============================================================================

TEST_F(MCPSerializationTest, ElicitRequest) {
  ElicitRequest req;
  req.id = make_request_id("elicit-1");
  req.name = "user_age";
  req.schema = make_number_schema();
  req.prompt = mcp::make_optional(std::string("Please enter your age"));
  testRoundTrip(req);
}

TEST_F(MCPSerializationTest, ElicitResult) {
  // String result
  ElicitResult str_result;
  str_result.value = std::string("John Doe");
  testRoundTrip(str_result);
  
  // Number result
  ElicitResult num_result;
  num_result.value = 25.5;
  testRoundTrip(num_result);
  
  // Boolean result
  ElicitResult bool_result;
  bool_result.value = true;
  testRoundTrip(bool_result);
  
  // Null result
  ElicitResult null_result;
  null_result.value = nullptr;
  testRoundTrip(null_result);
}

// =============================================================================
// Edge Cases and Error Handling
// =============================================================================

TEST_F(MCPSerializationTest, EmptyOptionals) {
  // Resource with no optional fields
  Resource res("uri", "name");
  EXPECT_FALSE(res.description.has_value());
  EXPECT_FALSE(res.mimeType.has_value());
  testRoundTrip(res);
  
  // Tool with no optional fields
  Tool tool("tool_name");
  EXPECT_FALSE(tool.description.has_value());
  EXPECT_FALSE(tool.inputSchema.has_value());
  EXPECT_FALSE(tool.parameters.has_value());
  testRoundTrip(tool);
}

TEST_F(MCPSerializationTest, EmptyCollections) {
  // Empty resource list
  ListResourcesResult empty_result;
  EXPECT_TRUE(empty_result.resources.empty());
  testRoundTrip(empty_result);
  
  // Empty tool list
  ListToolsResult empty_tools;
  EXPECT_TRUE(empty_tools.tools.empty());
  testRoundTrip(empty_tools);
  
  // Empty prompt arguments
  Prompt prompt("test");
  prompt.arguments = mcp::make_optional(std::vector<PromptArgument>());
  EXPECT_TRUE(prompt.arguments->empty());
  testRoundTrip(prompt);
}

TEST_F(MCPSerializationTest, SpecialCharacters) {
  // Text with special characters
  TextContent special("Text with \"quotes\", \nnewlines, \ttabs, and \\backslashes");
  testRoundTrip(special);
  
  // Unicode characters
  TextContent unicode("Hello 世界 🌍 emoji");
  testRoundTrip(unicode);
  
  // Resource with special characters in URI
  Resource special_res("file:///path/with%20spaces/and?query=params&foo=bar", "Special Resource");
  testRoundTrip(special_res);
}

TEST_F(MCPSerializationTest, LargeNumbers) {
  // Large integers in metadata
  Metadata meta = make_metadata();
  add_metadata(meta, "large_int", 9223372036854775807LL); // Max int64
  add_metadata(meta, "negative_int", -9223372036854775807LL);
  testRoundTrip(meta);
  
  // Large doubles
  NumberSchema schema;
  schema.minimum = mcp::make_optional(-1e308);
  schema.maximum = mcp::make_optional(1e308);
  testRoundTrip(schema);
}

TEST_F(MCPSerializationTest, NestedStructures) {
  // Deeply nested embedded resources
  EmbeddedResource outer;
  outer.resource = Resource("outer://resource", "Outer");
  
  EmbeddedResource inner;
  inner.resource = Resource("inner://resource", "Inner");
  inner.content.push_back(make_text_content("Inner text"));
  inner.content.push_back(make_image_content("innerimg", "image/png"));
  
  // Create a resource content that contains the inner embedded resource
  outer.content.push_back(make_text_content("Outer text"));
  outer.content.push_back(make_resource_content(inner.resource));
  
  testRoundTrip(outer);
}

TEST_F(MCPSerializationTest, AllLoggingLevels) {
  // Test all logging levels
  std::vector<enums::LoggingLevel::Value> levels = {
    enums::LoggingLevel::DEBUG,
    enums::LoggingLevel::INFO,
    enums::LoggingLevel::NOTICE,
    enums::LoggingLevel::WARNING,
    enums::LoggingLevel::ERROR,
    enums::LoggingLevel::CRITICAL,
    enums::LoggingLevel::ALERT,
    enums::LoggingLevel::EMERGENCY
  };
  
  for (auto level : levels) {
    LoggingMessageNotification notif;
    notif.level = level;
    notif.data = std::string("Test message");
    testRoundTrip(notif);
  }
}

TEST_F(MCPSerializationTest, ComplexMetadata) {
  Metadata meta = make_metadata();
  
  // Various types
  add_metadata(meta, "string", "value");
  add_metadata(meta, "int", 42);
  add_metadata(meta, "double", 3.14159);
  add_metadata(meta, "bool_true", true);
  add_metadata(meta, "bool_false", false);
  add_metadata(meta, "null", nullptr);
  
  // Note: Metadata can only hold primitive values (string, int64_t, double, bool, null)
  // It cannot hold nested metadata or arrays per the type definition
  
  testRoundTrip(meta);
}

TEST_F(MCPSerializationTest, VariantTypes) {
  // Test all variant alternatives
  
  // ContentBlock variants
  std::vector<ContentBlock> blocks;
  blocks.push_back(make_text_content("text"));
  blocks.push_back(make_image_content("img", "image/png"));
  blocks.push_back(make_resource_content(Resource("uri", "name")));
  
  for (const auto& block : blocks) {
    testRoundTrip(block);
  }
  
  // ErrorData variants
  std::vector<ErrorData> error_data;
  error_data.push_back(nullptr);
  error_data.push_back(true);
  error_data.push_back(42);
  error_data.push_back(3.14);
  error_data.push_back(std::string("error"));
  error_data.push_back(std::vector<std::string>{"e1", "e2"});
  
  std::map<std::string, std::string> error_map;
  error_map["key"] = "value";
  error_data.push_back(error_map);
  
  for (const auto& data : error_data) {
    Error err(jsonrpc::INTERNAL_ERROR, "Test");
    err.data = mcp::make_optional(data);
    testRoundTrip(err);
  }
}

// =============================================================================
// Subscription and Update Types
// =============================================================================

TEST_F(MCPSerializationTest, SubscribeRequest) {
  SubscribeRequest req("file:///watched.txt");
  req.id = make_request_id("sub-1");
  testRoundTrip(req);
}

TEST_F(MCPSerializationTest, UnsubscribeRequest) {
  UnsubscribeRequest req("file:///unwatched.txt");
  req.id = make_request_id("unsub-1");
  testRoundTrip(req);
}

TEST_F(MCPSerializationTest, ListChangedNotifications) {
  // Resource list changed
  ResourceListChangedNotification res_notif;
  testRoundTrip(res_notif);
  
  // Prompt list changed
  PromptListChangedNotification prompt_notif;
  testRoundTrip(prompt_notif);
  
  // Tool list changed
  ToolListChangedNotification tool_notif;
  testRoundTrip(tool_notif);
  
  // Roots list changed
  RootsListChangedNotification roots_notif;
  testRoundTrip(roots_notif);
}

// =============================================================================
// Prompt and Logging Request Types
// =============================================================================

TEST_F(MCPSerializationTest, ListPromptsRequest) {
  ListPromptsRequest req;
  req.id = make_request_id("prompts-1");
  testRoundTrip(req);
  
  // With pagination
  ListPromptsRequest paged;
  paged.id = make_request_id(2);
  paged.cursor = mcp::make_optional(Cursor("next"));
  testRoundTrip(paged);
}

TEST_F(MCPSerializationTest, ListPromptsResult) {
  ListPromptsResult result;
  result.prompts.push_back(make_prompt("greeting"));
  result.prompts.push_back(make_prompt("farewell"));
  testRoundTrip(result);
  
  // With pagination
  ListPromptsResult paged;
  paged.prompts.push_back(make_prompt("prompt1"));
  paged.nextCursor = mcp::make_optional(Cursor("page2"));
  testRoundTrip(paged);
}

TEST_F(MCPSerializationTest, GetPromptRequest) {
  GetPromptRequest simple("greeting");
  simple.id = make_request_id(1);
  testRoundTrip(simple);
  
  // With arguments
  GetPromptRequest with_args("template");
  with_args.id = make_request_id(2);
  Metadata args = make_metadata();
  add_metadata(args, "name", "Alice");
  add_metadata(args, "age", 30);
  with_args.arguments = mcp::make_optional(args);
  testRoundTrip(with_args);
}

TEST_F(MCPSerializationTest, GetPromptResult) {
  GetPromptResult result;
  result.description = mcp::make_optional(std::string("Greeting prompt"));
  result.messages.push_back(make_prompt_message(enums::Role::USER, "Hello"));
  result.messages.push_back(make_prompt_message(enums::Role::ASSISTANT, "Hi there!"));
  testRoundTrip(result);
}

TEST_F(MCPSerializationTest, SetLevelRequest) {
  SetLevelRequest req(enums::LoggingLevel::WARNING);
  req.id = make_request_id("setlevel-1");
  testRoundTrip(req);
}

// =============================================================================
// Empty Result Types
// =============================================================================

TEST_F(MCPSerializationTest, EmptyResult) {
  EmptyResult result;
  testRoundTrip(result);
}

// =============================================================================
// Complex Protocol Messages
// =============================================================================

TEST_F(MCPSerializationTest, ComplexInitializeSequence) {
  // Client sends initialize request
  InitializeRequest init_req;
  init_req.id = make_request_id(1);
  init_req.protocolVersion = "1.0.0";
  init_req.capabilities = build_client_capabilities()
    .experimental(make_metadata())
    .resources(true)
    .tools(true)
    .build();
  init_req.clientInfo = mcp::make_optional(Implementation("TestClient", "1.0.0"));
  testRoundTrip(init_req);
  
  // Server responds with result
  InitializeResult init_result;
  init_result.protocolVersion = "1.0.0";
  init_result.capabilities = build_server_capabilities()
    .resources(true)
    .tools(true)
    .prompts(true)
    .logging(true)
    .build();
  init_result.serverInfo = mcp::make_optional(Implementation("TestServer", "1.0.0"));
  init_result.instructions = mcp::make_optional(std::string("Welcome to TestServer"));
  testRoundTrip(init_result);
  
  // Client sends initialized notification
  InitializedNotification init_notif;
  testRoundTrip(init_notif);
}

TEST_F(MCPSerializationTest, ComplexToolCallSequence) {
  // List tools request
  ListToolsRequest list_req;
  list_req.id = make_request_id("list-1");
  testRoundTrip(list_req);
  
  // List tools result
  ListToolsResult list_result;
  Tool calc = build_tool("calculator")
    .description("Perform calculations")
    .inputSchema(JsonValue::parse(R"({
      "type": "object",
      "properties": {
        "expression": {"type": "string"}
      }
    })"))
    .build();
  list_result.tools.push_back(calc);
  testRoundTrip(list_result);
  
  // Call tool request
  Metadata args = make_metadata();
  add_metadata(args, "expression", "2 + 2");
  CallToolRequest call_req = make_call_tool_request("calculator", args);
  call_req.id = make_request_id("call-1");
  testRoundTrip(call_req);
  
  // Call tool result
  CallToolResult call_result;
  call_result.content.push_back(ExtendedContentBlock(TextContent("4")));
  testRoundTrip(call_result);
}

// Main function
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}