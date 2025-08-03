#include <gtest/gtest.h>

#include "mcp/types.h"

using namespace mcp;

class MCPTypesTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

// Test extended content block types
TEST_F(MCPTypesTest, AudioContent) {
  auto audio = make_audio_content("base64audiodata", "audio/mp3");
  EXPECT_TRUE(audio.holds_alternative<AudioContent>());

  auto& content = audio.get<AudioContent>();
  EXPECT_EQ(content.type, "audio");
  EXPECT_EQ(content.data, "base64audiodata");
  EXPECT_EQ(content.mimeType, "audio/mp3");
}

TEST_F(MCPTypesTest, ResourceLink) {
  auto resource = make_resource("file:///test.txt", "test.txt");
  auto link = make_resource_link(resource);

  EXPECT_TRUE(link.holds_alternative<ResourceLink>());
  auto& content = link.get<ResourceLink>();
  EXPECT_EQ(content.type, "resource");
  EXPECT_EQ(content.uri, "file:///test.txt");
}

TEST_F(MCPTypesTest, EmbeddedResource) {
  auto resource = make_resource("file:///doc.pdf", "document.pdf");

  auto embedded = build_embedded_resource(resource)
                      .add_text("This is page 1")
                      .add_text("This is page 2")
                      .add_image("base64imagedata", "image/png")
                      .build();

  EXPECT_TRUE(embedded.resource.uri == "file:///doc.pdf");
  EXPECT_EQ(embedded.content.size(), 3u);
  EXPECT_TRUE(embedded.content[0].holds_alternative<TextContent>());
  EXPECT_TRUE(embedded.content[2].holds_alternative<ImageContent>());
}

// Test extended logging levels
TEST_F(MCPTypesTest, ExtendedLoggingLevels) {
  using namespace enums;

  // Test all levels
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::DEBUG),
               "debug");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::CRITICAL),
               "critical");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::EMERGENCY),
               "emergency");

  // Test conversions
  auto level = LoggingLevel::from_string("alert");
  ASSERT_TRUE(level.has_value());
  EXPECT_EQ(level.value(), LoggingLevel::ALERT);

  auto invalid = LoggingLevel::from_string("invalid");
  EXPECT_FALSE(invalid.has_value());
}

// Test capability builders
TEST_F(MCPTypesTest, ClientCapabilities) {
  auto caps =
      build_client_capabilities()
          .experimental(make_metadata())
          .sampling(
              build_sampling_params().temperature(0.7).maxTokens(1000).build())
          .build();

  EXPECT_TRUE(caps.experimental.has_value());
  EXPECT_TRUE(caps.sampling.has_value());
  EXPECT_TRUE(caps.sampling->temperature.has_value());
  EXPECT_DOUBLE_EQ(caps.sampling->temperature.value(), 0.7);
}

TEST_F(MCPTypesTest, ServerCapabilities) {
  auto caps = build_server_capabilities()
                  .resources(true)
                  .tools(true)
                  .prompts(false)
                  .logging(true)
                  .build();

  ASSERT_TRUE(caps.resources.has_value());
  EXPECT_TRUE(caps.resources.value());
  ASSERT_TRUE(caps.tools.has_value());
  EXPECT_TRUE(caps.tools.value());
  ASSERT_TRUE(caps.prompts.has_value());
  EXPECT_FALSE(caps.prompts.value());
}

// Test model preferences
TEST_F(MCPTypesTest, ModelPreferences) {
  auto prefs = build_model_preferences()
                   .add_hint("gpt-4")
                   .add_hint("claude-3")
                   .cost_priority(0.3)
                   .speed_priority(0.7)
                   .intelligence_priority(0.9)
                   .build();

  ASSERT_TRUE(prefs.hints.has_value());
  EXPECT_EQ(prefs.hints->size(), 2u);
  EXPECT_EQ(prefs.hints->at(0).name.value(), "gpt-4");

  ASSERT_TRUE(prefs.costPriority.has_value());
  EXPECT_DOUBLE_EQ(prefs.costPriority.value(), 0.3);
}

// Test schema types
TEST_F(MCPTypesTest, SchemaTypes) {
  // String schema
  auto string_schema = build_string_schema()
                           .description("Email address")
                           .pattern("^[\\w\\.-]+@[\\w\\.-]+\\.\\w+$")
                           .min_length(5)
                           .max_length(100)
                           .build();

  EXPECT_EQ(string_schema.type, "string");
  ASSERT_TRUE(string_schema.pattern.has_value());
  EXPECT_EQ(string_schema.minLength.value(), 5);

  // Enum schema
  auto enum_schema = make_enum_schema({"red", "green", "blue"});
  EXPECT_TRUE(enum_schema.holds_alternative<EnumSchema>());
  auto& enum_def = enum_schema.get<EnumSchema>();
  EXPECT_EQ(enum_def.values.size(), 3u);
  EXPECT_EQ(enum_def.values[1], "green");
}

// Test request types
TEST_F(MCPTypesTest, InitializeRequest) {
  auto caps = build_client_capabilities().resources(true).build();

  auto req = make_initialize_request("2025-06-18", caps);
  EXPECT_EQ(req.method, "initialize");
  EXPECT_EQ(req.protocolVersion, "2025-06-18");
}

TEST_F(MCPTypesTest, ProgressNotification) {
  auto notif =
      make_progress_notification(make_progress_token("task-123"), 0.75);

  EXPECT_EQ(notif.method, "notifications/progress");
  EXPECT_TRUE(notif.progressToken.holds_alternative<std::string>());
  EXPECT_DOUBLE_EQ(notif.progress, 0.75);
}

TEST_F(MCPTypesTest, CallToolRequest) {
  auto args = make_metadata();
  add_metadata(args, "expression", "2 + 2");

  auto req = make_call_tool_request("calculator", args);
  EXPECT_EQ(req.method, "tools/call");
  EXPECT_EQ(req.name, "calculator");
  ASSERT_TRUE(req.arguments.has_value());
}

TEST_F(MCPTypesTest, LoggingNotification) {
  auto notif = make_log_notification(enums::LoggingLevel::WARNING,
                                     "This is a warning message");

  EXPECT_EQ(notif.method, "notifications/message");
  EXPECT_EQ(notif.level, enums::LoggingLevel::WARNING);
  EXPECT_TRUE(notif.data.holds_alternative<std::string>());
}

// Test CreateMessageRequest builder
TEST_F(MCPTypesTest, CreateMessageRequest) {
  auto req = build_create_message_request()
                 .add_user_message("What is 2+2?")
                 .add_assistant_message("2+2 equals 4.")
                 .add_user_message("What about 3+3?")
                 .model_preferences(build_model_preferences()
                                        .add_hint("gpt-4")
                                        .intelligence_priority(0.8)
                                        .build())
                 .temperature(0.7)
                 .max_tokens(150)
                 .stop_sequence("\\n\\n")
                 .stop_sequence("END")
                 .system_prompt("You are a helpful math tutor.")
                 .build();

  EXPECT_EQ(req.method, "sampling/createMessage");
  EXPECT_EQ(req.messages.size(), 3u);
  EXPECT_EQ(req.messages[0].role, enums::Role::USER);

  ASSERT_TRUE(req.temperature.has_value());
  EXPECT_DOUBLE_EQ(req.temperature.value(), 0.7);

  ASSERT_TRUE(req.modelPreferences.has_value());
  ASSERT_TRUE(req.modelPreferences->hints.has_value());
  EXPECT_EQ(req.modelPreferences->hints->size(), 1u);

  ASSERT_TRUE(req.systemPrompt.has_value());
  EXPECT_EQ(req.systemPrompt.value(), "You are a helpful math tutor.");
}

// Test resource contents variations
TEST_F(MCPTypesTest, ResourceContents) {
  auto text_contents = make_text_resource("Hello, world!");
  EXPECT_EQ(text_contents.text, "Hello, world!");

  auto blob_contents = make_blob_resource("base64encodeddata");
  EXPECT_EQ(blob_contents.blob, "base64encodeddata");
}

// Test reference types
TEST_F(MCPTypesTest, ReferenceTypes) {
  auto template_ref = make_resource_template_ref("template", "my-template");
  EXPECT_EQ(template_ref.type, "template");
  EXPECT_EQ(template_ref.name, "my-template");

  auto prompt_ref = make_prompt_ref("system", "math-tutor");
  EXPECT_EQ(prompt_ref.type, "system");
  EXPECT_EQ(prompt_ref.name, "math-tutor");
}

// Test root types
TEST_F(MCPTypesTest, RootTypes) {
  auto root = make_root("file:///home/user", "User Home");
  EXPECT_EQ(root.uri, "file:///home/user");
  ASSERT_TRUE(root.name.has_value());
  EXPECT_EQ(root.name.value(), "User Home");
}

// Test client/server message unions
TEST_F(MCPTypesTest, MessageUnions) {
  // Client request
  ClientRequest req = CallToolRequest("test", make_metadata());

  auto method =
      match(req, [](const auto& r) -> std::string { return r.method; });

  EXPECT_EQ(method, "tools/call");

  // Server notification
  ServerNotification notif =
      make_progress_notification(make_progress_token(42), 0.5);

  bool is_progress = match(
      notif, [](const ProgressNotification&) { return true; },
      [](const auto&) { return false; });

  EXPECT_TRUE(is_progress);
}

// Test pagination
TEST_F(MCPTypesTest, Pagination) {
  ListResourcesRequest req;
  req.cursor = make_optional(Cursor("next-page-token"));

  EXPECT_EQ(req.method, "resources/list");
  ASSERT_TRUE(req.cursor.has_value());
  EXPECT_EQ(req.cursor.value(), "next-page-token");

  ListResourcesResult result;
  result.nextCursor = make_optional(Cursor("page-3-token"));
  result.resources.push_back(make_resource("file:///test.txt", "test"));

  ASSERT_TRUE(result.nextCursor.has_value());
  EXPECT_EQ(result.resources.size(), 1u);
}

// Integration test - Complete request/response flow
TEST_F(MCPTypesTest, IntegrationRequestResponse) {
  // Initialize request
  auto init_req = make_initialize_request(
      "2025-06-18",
      build_client_capabilities().tools(true).resources(true).build());

  // Simulate server response
  InitializeResult init_result;
  init_result.protocolVersion = "2025-06-18";
  init_result.capabilities = build_server_capabilities()
                                 .tools(true)
                                 .resources(true)
                                 .logging(true)
                                 .build();
  init_result.serverInfo =
      make_optional(make_implementation("test-server", "1.0.0"));

  EXPECT_EQ(init_result.protocolVersion, "2025-06-18");
  ASSERT_TRUE(init_result.serverInfo.has_value());
  EXPECT_EQ(init_result.serverInfo->name, "test-server");

  // Call tool
  auto tool_args = make_metadata();
  add_metadata(tool_args, "query", "SELECT * FROM users");

  auto tool_req = make_call_tool_request("sql_query", tool_args);

  // Tool response
  CallToolResult tool_result;
  tool_result.content.push_back(
      ExtendedContentBlock(TextContent("Found 42 users")));
  tool_result.isError = false;

  EXPECT_EQ(tool_result.content.size(), 1u);
  EXPECT_FALSE(tool_result.isError);
}

// Test JSON-RPC error codes
TEST_F(MCPTypesTest, JSONRPCErrorCodes) {
  using namespace jsonrpc;

  EXPECT_EQ(PARSE_ERROR, -32700);
  EXPECT_EQ(INVALID_REQUEST, -32600);
  EXPECT_EQ(METHOD_NOT_FOUND, -32601);
  EXPECT_EQ(INVALID_PARAMS, -32602);
  EXPECT_EQ(INTERNAL_ERROR, -32603);

  // Use in error
  Error err(METHOD_NOT_FOUND, "Unknown method: foo");
  EXPECT_EQ(err.code, -32601);
}

// Test base metadata
TEST_F(MCPTypesTest, BaseMetadata) {
  ResourceTemplate tmpl;
  tmpl.uriTemplate = "file:///{path}";
  tmpl.name = "file-template";

  // Add metadata
  tmpl._meta = make_optional(make_metadata());
  add_metadata(*tmpl._meta, "version", "1.0");
  add_metadata(*tmpl._meta, "author", "test");

  ASSERT_TRUE(tmpl._meta.has_value());
  EXPECT_EQ(tmpl._meta->size(), 2u);
  EXPECT_TRUE(tmpl._meta->at("version").holds_alternative<std::string>());
}