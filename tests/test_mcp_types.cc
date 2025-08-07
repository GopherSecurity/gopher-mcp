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
  EXPECT_TRUE(mcp::holds_alternative<AudioContent>(audio));

  auto& content = mcp::get<AudioContent>(audio);
  EXPECT_EQ(content.type, "audio");
  EXPECT_EQ(content.data, "base64audiodata");
  EXPECT_EQ(content.mimeType, "audio/mp3");
}

TEST_F(MCPTypesTest, ResourceLink) {
  auto resource = make_resource("file:///test.txt", "test.txt");
  auto link = make_resource_link(resource);

  EXPECT_TRUE(mcp::holds_alternative<ResourceLink>(link));
  auto& content = mcp::get<ResourceLink>(link);
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
  EXPECT_TRUE(mcp::holds_alternative<TextContent>(embedded.content[0]));
  EXPECT_TRUE(mcp::holds_alternative<ImageContent>(embedded.content[2]));
}

// Test extended logging levels
TEST_F(MCPTypesTest, ExtendedLoggingLevels) {
  using namespace enums;

  // Test all levels
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::DEBUG), "debug");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::CRITICAL), "critical");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::EMERGENCY), "emergency");

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
  // resources is now a variant<bool, ResourcesCapability>
  ASSERT_TRUE(mcp::holds_alternative<bool>(caps.resources.value()));
  EXPECT_TRUE(mcp::get<bool>(caps.resources.value()));
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
  EXPECT_TRUE(mcp::holds_alternative<EnumSchema>(enum_schema));
  auto& enum_def = mcp::get<EnumSchema>(enum_schema);
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
  EXPECT_TRUE(mcp::holds_alternative<std::string>(notif.progressToken));
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
  EXPECT_TRUE(mcp::holds_alternative<std::string>(notif.data));
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
  req.cursor = mcp::make_optional(Cursor("next-page-token"));

  EXPECT_EQ(req.method, "resources/list");
  ASSERT_TRUE(req.cursor.has_value());
  EXPECT_EQ(req.cursor.value(), "next-page-token");

  ListResourcesResult result;
  result.nextCursor = mcp::make_optional(Cursor("page-3-token"));
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

  EXPECT_EQ(jsonrpc::PARSE_ERROR, -32700);
  EXPECT_EQ(jsonrpc::INVALID_REQUEST, -32600);
  EXPECT_EQ(jsonrpc::METHOD_NOT_FOUND, -32601);
  EXPECT_EQ(jsonrpc::INVALID_PARAMS, -32602);
  EXPECT_EQ(jsonrpc::INTERNAL_ERROR, -32603);

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
  tmpl._meta = mcp::make_optional(make_metadata());
  add_metadata(*tmpl._meta, "version", "1.0");
  add_metadata(*tmpl._meta, "author", "test");

  ASSERT_TRUE(tmpl._meta.has_value());
  EXPECT_EQ(tmpl._meta->size(), 2u);
  EXPECT_TRUE(mcp::holds_alternative<std::string>(tmpl._meta->at("version")));
}

// Extensive tests for protocol type aliases
TEST_F(MCPTypesTest, RequestIdTypeAlias) {
  // Test string variant
  RequestId stringId = std::string("request-123");
  EXPECT_TRUE(mcp::holds_alternative<std::string>(stringId));
  EXPECT_EQ(mcp::get<std::string>(stringId), "request-123");

  // Test int variant
  RequestId intId = 456;
  EXPECT_TRUE(mcp::holds_alternative<int>(intId));
  EXPECT_EQ(mcp::get<int>(intId), 456);

  // Test assignment
  stringId = 789;
  EXPECT_TRUE(mcp::holds_alternative<int>(stringId));
  EXPECT_EQ(mcp::get<int>(stringId), 789);
}

TEST_F(MCPTypesTest, ProgressTokenTypeAlias) {
  // Test string variant
  ProgressToken stringToken = std::string("progress-abc");
  EXPECT_TRUE(mcp::holds_alternative<std::string>(stringToken));
  EXPECT_EQ(mcp::get<std::string>(stringToken), "progress-abc");

  // Test int variant
  ProgressToken intToken = 999;
  EXPECT_TRUE(mcp::holds_alternative<int>(intToken));
  EXPECT_EQ(mcp::get<int>(intToken), 999);
}

TEST_F(MCPTypesTest, CursorTypeAlias) {
  // Cursor is just a string alias
  Cursor cursor = "next-page-token";
  EXPECT_EQ(cursor, "next-page-token");

  // Test it's actually a string
  EXPECT_TRUE((std::is_same<Cursor, std::string>::value));
}

// Extensive tests for RequestId factory functions
TEST_F(MCPTypesTest, RequestIdFactoriesExtensive) {
  // Test string factory
  auto id1 = make_request_id(std::string("req-001"));
  EXPECT_TRUE(mcp::holds_alternative<std::string>(id1));
  EXPECT_EQ(mcp::get<std::string>(id1), "req-001");

  // Test C-string factory
  auto id2 = make_request_id("req-002");
  EXPECT_TRUE(mcp::holds_alternative<std::string>(id2));
  EXPECT_EQ(mcp::get<std::string>(id2), "req-002");

  // Test int factory
  auto id3 = make_request_id(12345);
  EXPECT_TRUE(mcp::holds_alternative<int>(id3));
  EXPECT_EQ(mcp::get<int>(id3), 12345);

  // Test with empty string
  auto id4 = make_request_id("");
  EXPECT_TRUE(mcp::holds_alternative<std::string>(id4));
  EXPECT_EQ(mcp::get<std::string>(id4), "");

  // Test with negative int
  auto id5 = make_request_id(-1);
  EXPECT_TRUE(mcp::holds_alternative<int>(id5));
  EXPECT_EQ(mcp::get<int>(id5), -1);

  // Test with max int
  auto id6 = make_request_id(std::numeric_limits<int>::max());
  EXPECT_TRUE(mcp::holds_alternative<int>(id6));
  EXPECT_EQ(mcp::get<int>(id6), std::numeric_limits<int>::max());
}

// Extensive tests for ProgressToken factory functions
TEST_F(MCPTypesTest, ProgressTokenFactoriesExtensive) {
  // Test string factory
  auto token1 = make_progress_token(std::string("progress-001"));
  EXPECT_TRUE(mcp::holds_alternative<std::string>(token1));
  EXPECT_EQ(mcp::get<std::string>(token1), "progress-001");

  // Test C-string factory
  auto token2 = make_progress_token("progress-002");
  EXPECT_TRUE(mcp::holds_alternative<std::string>(token2));
  EXPECT_EQ(mcp::get<std::string>(token2), "progress-002");

  // Test int factory
  auto token3 = make_progress_token(54321);
  EXPECT_TRUE(mcp::holds_alternative<int>(token3));
  EXPECT_EQ(mcp::get<int>(token3), 54321);

  // Test with special characters in string
  auto token4 = make_progress_token("token-with-special-!@#$%^&*()");
  EXPECT_TRUE(mcp::holds_alternative<std::string>(token4));
  EXPECT_EQ(mcp::get<std::string>(token4), "token-with-special-!@#$%^&*()");

  // Test with zero
  auto token5 = make_progress_token(0);
  EXPECT_TRUE(mcp::holds_alternative<int>(token5));
  EXPECT_EQ(mcp::get<int>(token5), 0);
}

// Extensive tests for Role enum
TEST_F(MCPTypesTest, RoleEnumExtensive) {
  using namespace enums;

  // Test enum values
  EXPECT_EQ(static_cast<int>(Role::USER), 0);
  EXPECT_EQ(static_cast<int>(Role::ASSISTANT), 1);

  // Test to_string for all values
  EXPECT_STREQ(Role::to_string(Role::USER), "user");
  EXPECT_STREQ(Role::to_string(Role::ASSISTANT), "assistant");

  // Test to_string with invalid value (cast from int)
  EXPECT_STREQ(Role::to_string(static_cast<Role::Value>(999)), "");

  // Test from_string for all valid values
  auto user = Role::from_string("user");
  ASSERT_TRUE(user.has_value());
  EXPECT_EQ(user.value(), Role::USER);

  auto assistant = Role::from_string("assistant");
  ASSERT_TRUE(assistant.has_value());
  EXPECT_EQ(assistant.value(), Role::ASSISTANT);

  // Test from_string with invalid values
  EXPECT_FALSE(Role::from_string("").has_value());
  EXPECT_FALSE(Role::from_string("invalid").has_value());
  EXPECT_FALSE(Role::from_string("USER").has_value());  // case sensitive
  EXPECT_FALSE(Role::from_string("ASSISTANT").has_value());
  EXPECT_FALSE(Role::from_string(" user").has_value());  // with space
  EXPECT_FALSE(Role::from_string("user ").has_value());
}

// Extensive tests for LoggingLevel enum
TEST_F(MCPTypesTest, LoggingLevelEnumExtensive) {
  using namespace enums;

  // Test all enum values
  EXPECT_EQ(static_cast<int>(LoggingLevel::DEBUG), 0);
  EXPECT_EQ(static_cast<int>(LoggingLevel::INFO), 1);
  EXPECT_EQ(static_cast<int>(LoggingLevel::NOTICE), 2);
  EXPECT_EQ(static_cast<int>(LoggingLevel::WARNING), 3);
  EXPECT_EQ(static_cast<int>(LoggingLevel::ERROR), 4);
  EXPECT_EQ(static_cast<int>(LoggingLevel::CRITICAL), 5);
  EXPECT_EQ(static_cast<int>(LoggingLevel::ALERT), 6);
  EXPECT_EQ(static_cast<int>(LoggingLevel::EMERGENCY), 7);

  // Test to_string for all values
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::DEBUG), "debug");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::INFO), "info");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::NOTICE), "notice");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::WARNING), "warning");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::ERROR), "error");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::CRITICAL), "critical");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::ALERT), "alert");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::EMERGENCY), "emergency");

  // Test to_string with invalid value
  EXPECT_STREQ(LoggingLevel::to_string(static_cast<LoggingLevel::Value>(999)),
               "");

  // Test from_string for all valid values
  struct TestCase {
    const char* str;
    LoggingLevel::Value expected;
  };

  TestCase validCases[] = {
      {"debug", LoggingLevel::DEBUG},   {"info", LoggingLevel::INFO},
      {"notice", LoggingLevel::NOTICE}, {"warning", LoggingLevel::WARNING},
      {"error", LoggingLevel::ERROR},   {"critical", LoggingLevel::CRITICAL},
      {"alert", LoggingLevel::ALERT},   {"emergency", LoggingLevel::EMERGENCY}};

  for (const auto& tc : validCases) {
    auto result = LoggingLevel::from_string(tc.str);
    ASSERT_TRUE(result.has_value()) << "Failed for: " << tc.str;
    EXPECT_EQ(result.value(), tc.expected) << "Failed for: " << tc.str;
  }

  // Test from_string with invalid values
  const char* invalidCases[] = {"",      "invalid",  "DEBUG",  "INFO",
                                "Error", "CRITICAL", " debug", "debug ",
                                "warn",  "err",      "dbg"};

  for (const auto& invalid : invalidCases) {
    EXPECT_FALSE(LoggingLevel::from_string(invalid).has_value())
        << "Should be invalid: " << invalid;
  }
}

// Test make_method_request function
TEST_F(MCPTypesTest, MakeMethodRequestExtensive) {
  // Test with string RequestId
  struct TestParams {
    std::string data;
  };

  auto req1 = make_method_request(make_request_id("req-123"), "test/method",
                                  TestParams{"test data"});

  EXPECT_TRUE(mcp::holds_alternative<std::string>(req1.first));
  EXPECT_EQ(mcp::get<std::string>(req1.first), "req-123");
  EXPECT_EQ(req1.second.method, "test/method");
  EXPECT_TRUE(req1.second.is_type<TestParams>());

  // Test with int RequestId
  auto req2 = make_method_request(make_request_id(456), "another/method", 42);

  EXPECT_TRUE(mcp::holds_alternative<int>(req2.first));
  EXPECT_EQ(mcp::get<int>(req2.first), 456);
  EXPECT_EQ(req2.second.method, "another/method");
  EXPECT_TRUE(req2.second.is_type<int>());

  // Test with complex params
  std::vector<std::string> complexParams = {"one", "two", "three"};
  auto req3 = make_method_request(make_request_id("complex"), "complex/method",
                                  complexParams);

  EXPECT_EQ(req3.second.method, "complex/method");
  auto params = req3.second.get_if<std::vector<std::string>>();
  ASSERT_NE(params, nullptr);
  EXPECT_EQ(params->size(), 3u);
  EXPECT_EQ((*params)[0], "one");
}

// Test content block creation edge cases
TEST_F(MCPTypesTest, ContentBlockCreationEdgeCases) {
  // Test with empty strings
  auto empty_text = make_text_content("");
  EXPECT_TRUE(mcp::holds_alternative<TextContent>(empty_text));
  EXPECT_EQ(mcp::get<TextContent>(empty_text).text, "");

  // Test with very long strings
  std::string longStr(10000, 'a');
  auto long_text = make_text_content(longStr);
  EXPECT_TRUE(mcp::holds_alternative<TextContent>(long_text));
  EXPECT_EQ(mcp::get<TextContent>(long_text).text.length(), 10000u);

  // Test image content with empty mime type
  auto img = make_image_content("data", "");
  EXPECT_TRUE(mcp::holds_alternative<ImageContent>(img));
  EXPECT_EQ(mcp::get<ImageContent>(img).mimeType, "");
}

// Test error construction and codes
TEST_F(MCPTypesTest, ErrorConstructionExtensive) {
  // Test with all predefined error codes
  Error parseErr(jsonrpc::PARSE_ERROR, "Parse error");
  EXPECT_EQ(parseErr.code, -32700);
  EXPECT_EQ(parseErr.message, "Parse error");

  Error invalidReq(jsonrpc::INVALID_REQUEST, "Invalid request");
  EXPECT_EQ(invalidReq.code, -32600);

  Error methodNotFound(jsonrpc::METHOD_NOT_FOUND, "Method not found");
  EXPECT_EQ(methodNotFound.code, -32601);

  Error invalidParams(jsonrpc::INVALID_PARAMS, "Invalid params");
  EXPECT_EQ(invalidParams.code, -32602);

  Error internalErr(jsonrpc::INTERNAL_ERROR, "Internal error");
  EXPECT_EQ(internalErr.code, -32603);

  // Test with custom error codes
  Error customErr(1001, "Custom error");
  EXPECT_EQ(customErr.code, 1001);
  EXPECT_EQ(customErr.message, "Custom error");

  // Test with data field - using simplified ErrorData type
  Error errWithData(500, "Server error");
  errWithData.data = mcp::make_optional(ErrorData(std::string("Additional error info")));
  ASSERT_TRUE(errWithData.data.has_value());
  EXPECT_TRUE(mcp::holds_alternative<std::string>(*errWithData.data));
  
  // Test vector data
  Error errWithVector(501, "Vector error");
  std::vector<std::string> vecData = {"error1", "error2"};
  errWithVector.data = mcp::make_optional(ErrorData(vecData));
  ASSERT_TRUE(errWithVector.data.has_value());
  EXPECT_TRUE(mcp::holds_alternative<std::vector<std::string>>(*errWithVector.data));
  
  // Test map data
  Error errWithMap(502, "Map error");
  std::map<std::string, std::string> mapData = {{"key1", "value1"}, {"key2", "value2"}};
  errWithMap.data = mcp::make_optional(ErrorData(mapData));
  ASSERT_TRUE(errWithMap.data.has_value());
  EXPECT_TRUE((mcp::holds_alternative<std::map<std::string, std::string>>(*errWithMap.data)));
}

// Test Implementation info
TEST_F(MCPTypesTest, ImplementationInfoExtensive) {
  // Test basic construction
  auto impl1 = make_implementation("test-server", "1.0.0");
  EXPECT_EQ(impl1.name, "test-server");
  EXPECT_EQ(impl1.version, "1.0.0");

  // Test with empty strings
  auto impl2 = make_implementation("", "");
  EXPECT_EQ(impl2.name, "");
  EXPECT_EQ(impl2.version, "");

  // Test copy
  Implementation impl3 = impl1;
  EXPECT_EQ(impl3.name, "test-server");
  EXPECT_EQ(impl3.version, "1.0.0");
}

// Test sampling message types
TEST_F(MCPTypesTest, SamplingMessageExtensive) {
  // Test user message creation
  auto userMsg = make_user_message("Hello AI");
  EXPECT_EQ(userMsg.role, enums::Role::USER);
  EXPECT_TRUE(mcp::holds_alternative<TextContent>(userMsg.content));
  EXPECT_EQ(mcp::get<TextContent>(userMsg.content).text, "Hello AI");

  // Test assistant message creation
  auto assistantMsg = make_assistant_message("Hello Human");
  EXPECT_EQ(assistantMsg.role, enums::Role::ASSISTANT);
  EXPECT_TRUE(mcp::holds_alternative<TextContent>(assistantMsg.content));
  EXPECT_EQ(mcp::get<TextContent>(assistantMsg.content).text, "Hello Human");

  // Test with audio content
  SamplingMessage audioMsg;
  audioMsg.role = enums::Role::ASSISTANT;
  audioMsg.content = AudioContent("base64audio", "audio/mp3");

  EXPECT_TRUE(mcp::holds_alternative<AudioContent>(audioMsg.content));
  auto& audioContent = mcp::get<AudioContent>(audioMsg.content);
  EXPECT_EQ(audioContent.data, "base64audio");
  EXPECT_EQ(audioContent.mimeType, "audio/mp3");
}

// Test prompt message references
TEST_F(MCPTypesTest, PromptMessageReferences) {
  // Test prompt reference
  auto promptRef = make_prompt_ref("system", "greeting-prompt");
  EXPECT_EQ(promptRef.type, "system");
  EXPECT_EQ(promptRef.name, "greeting-prompt");

  // Test resource template reference
  auto resourceRef = make_resource_template_ref("template", "file-template");
  EXPECT_EQ(resourceRef.type, "template");
  EXPECT_EQ(resourceRef.name, "file-template");

  // Test PromptMessage with text content
  PromptMessage msg;
  msg.role = enums::Role::USER;
  msg.content = TextContent("Hello from prompt");

  EXPECT_TRUE(mcp::holds_alternative<TextContent>(msg.content));
  auto& textContent = mcp::get<TextContent>(msg.content);
  EXPECT_EQ(textContent.text, "Hello from prompt");
}

// Test model preferences edge cases
TEST_F(MCPTypesTest, ModelPreferencesEdgeCases) {
  // Test with empty hints
  auto prefs1 = build_model_preferences().build();
  EXPECT_FALSE(prefs1.hints.has_value());
  EXPECT_FALSE(prefs1.costPriority.has_value());

  // Test with multiple priorities
  auto prefs2 = build_model_preferences()
                    .cost_priority(0.0)          // minimum
                    .speed_priority(1.0)         // maximum
                    .intelligence_priority(0.5)  // middle
                    .build();

  EXPECT_DOUBLE_EQ(prefs2.costPriority.value(), 0.0);
  EXPECT_DOUBLE_EQ(prefs2.speedPriority.value(), 1.0);
  EXPECT_DOUBLE_EQ(prefs2.intelligencePriority.value(), 0.5);

  // Test hint without name
  ModelHint hint;
  ModelPreferences prefs3;
  prefs3.hints = mcp::make_optional(std::vector<ModelHint>{hint});
  EXPECT_FALSE(prefs3.hints->at(0).name.has_value());
}

// Test resource contents variations
TEST_F(MCPTypesTest, ResourceContentsVariations) {
  // Test text resource with empty string
  auto emptyText = make_text_resource("");
  EXPECT_EQ(emptyText.text, "");

  // Test blob resource with binary-like data
  auto blobData =
      make_blob_resource("SGVsbG8gV29ybGQh");  // "Hello World!" base64
  EXPECT_EQ(blobData.blob, "SGVsbG8gV29ybGQh");

  // Test that text and blob resource contents are created correctly
  EXPECT_EQ(emptyText.text, "");
  EXPECT_EQ(blobData.blob, "SGVsbG8gV29ybGQh");
}

// Test Root type edge cases
TEST_F(MCPTypesTest, RootTypeEdgeCases) {
  // Test without name
  Root root1;
  root1.uri = "file:///";
  EXPECT_FALSE(root1.name.has_value());

  // Test with special characters in URI
  auto root2 = make_root("file:///path with spaces/", "Spaced Path");
  EXPECT_EQ(root2.uri, "file:///path with spaces/");
  EXPECT_EQ(root2.name.value(), "Spaced Path");

  // Test copy
  Root root3 = root2;
  EXPECT_EQ(root3.uri, root2.uri);
  EXPECT_EQ(root3.name.value(), root2.name.value());
}

// Test schema types thoroughly
TEST_F(MCPTypesTest, SchemaTypesExtensive) {
  // Test number schema
  auto numSchema = make_number_schema();
  EXPECT_TRUE(mcp::holds_alternative<NumberSchema>(numSchema));
  auto& numDef = mcp::get<NumberSchema>(numSchema);
  EXPECT_EQ(numDef.type, "number");

  // Test boolean schema
  auto boolSchema = make_boolean_schema();
  EXPECT_TRUE(mcp::holds_alternative<BooleanSchema>(boolSchema));
  auto& boolDef = mcp::get<BooleanSchema>(boolSchema);
  EXPECT_EQ(boolDef.type, "boolean");

  // Test enum schema with empty values
  auto emptyEnum = make_enum_schema({});
  EXPECT_TRUE(mcp::holds_alternative<EnumSchema>(emptyEnum));
  EXPECT_EQ(mcp::get<EnumSchema>(emptyEnum).values.size(), 0u);

  // Test enum schema with duplicate values
  auto dupEnum = make_enum_schema({"red", "green", "red"});
  EXPECT_EQ(mcp::get<EnumSchema>(dupEnum).values.size(), 3u);
}

// Test client/server message union edge cases
TEST_F(MCPTypesTest, MessageUnionEdgeCases) {
  // Test ClientNotification variants
  ClientNotification notif1 = CancelledNotification();
  mcp::get<CancelledNotification>(notif1).method = "cancelled";

  auto method1 = match(
      notif1, [](const CancelledNotification& n) { return n.method; },
      [](const auto&) { return std::string("unknown"); });
  EXPECT_EQ(method1, "cancelled");

  // Test ServerRequest variants
  CreateMessageRequest createReq;
  createReq.method = "sampling/createMessage";
  SamplingMessage samplingMsg;
  samplingMsg.role = enums::Role::USER;
  samplingMsg.content = TextContent("Test");
  createReq.messages.push_back(samplingMsg);

  ServerRequest req = createReq;
  EXPECT_TRUE(mcp::holds_alternative<CreateMessageRequest>(req));

  // Test method extraction
  auto method2 =
      match(req, [](const auto& r) -> std::string { return r.method; });
  EXPECT_EQ(method2, "sampling/createMessage");
}

// Test notification creation helpers
TEST_F(MCPTypesTest, NotificationCreationExtensive) {
  // Test progress notification with string token
  auto prog1 = make_progress_notification(make_progress_token("task-123"), 0.0);
  EXPECT_DOUBLE_EQ(prog1.progress, 0.0);

  // Test with maximum progress
  auto prog2 = make_progress_notification(make_progress_token(999), 1.0);
  EXPECT_DOUBLE_EQ(prog2.progress, 1.0);

  // Test log notification with all levels
  using namespace enums;
  LoggingLevel::Value levels[] = {
      LoggingLevel::DEBUG,   LoggingLevel::INFO,     LoggingLevel::NOTICE,
      LoggingLevel::WARNING, LoggingLevel::ERROR,    LoggingLevel::CRITICAL,
      LoggingLevel::ALERT,   LoggingLevel::EMERGENCY};

  for (auto level : levels) {
    auto logNotif = make_log_notification(level, "Test message");
    EXPECT_EQ(logNotif.level, level);
    EXPECT_EQ(logNotif.method, "notifications/message");
  }
}

// ==================== EXTENSIVE TESTS FOR TYPES.H ====================

// Test all protocol type aliases comprehensively
TEST_F(MCPTypesTest, ProtocolTypeAliases) {
  // RequestId - string variant
  RequestId id1 = std::string("request-123");
  EXPECT_TRUE(mcp::holds_alternative<std::string>(id1));
  EXPECT_EQ(mcp::get<std::string>(id1), "request-123");

  // RequestId - int variant
  RequestId id2 = 42;
  EXPECT_TRUE(mcp::holds_alternative<int>(id2));
  EXPECT_EQ(mcp::get<int>(id2), 42);

  // ProgressToken - string variant
  ProgressToken token1 = std::string("progress-abc");
  EXPECT_TRUE(mcp::holds_alternative<std::string>(token1));
  EXPECT_EQ(mcp::get<std::string>(token1), "progress-abc");

  // ProgressToken - int variant
  ProgressToken token2 = 99;
  EXPECT_TRUE(mcp::holds_alternative<int>(token2));
  EXPECT_EQ(mcp::get<int>(token2), 99);

  // Cursor (simple string alias)
  Cursor cursor = "cursor-token-xyz";
  EXPECT_EQ(cursor, "cursor-token-xyz");
}

// Test Role enum comprehensively
TEST_F(MCPTypesTest, RoleEnumComprehensive) {
  using namespace enums;

  // Test all enum values
  EXPECT_EQ(Role::USER, 0);  // Verify enum values if they're explicitly set
  EXPECT_EQ(Role::ASSISTANT, 1);

  // Test to_string for all values
  EXPECT_STREQ(Role::to_string(Role::USER), "user");
  EXPECT_STREQ(Role::to_string(Role::ASSISTANT), "assistant");

  // Test invalid enum value (cast from int)
  EXPECT_STREQ(Role::to_string(static_cast<Role::Value>(999)), "");

  // Test from_string for all valid values
  auto user_role = Role::from_string("user");
  ASSERT_TRUE(user_role.has_value());
  EXPECT_EQ(user_role.value(), Role::USER);

  auto assistant_role = Role::from_string("assistant");
  ASSERT_TRUE(assistant_role.has_value());
  EXPECT_EQ(assistant_role.value(), Role::ASSISTANT);

  // Test from_string edge cases
  EXPECT_FALSE(Role::from_string("").has_value());
  EXPECT_FALSE(Role::from_string("invalid").has_value());
  EXPECT_FALSE(Role::from_string("USER").has_value());   // Case sensitive
  EXPECT_FALSE(Role::from_string("user ").has_value());  // Whitespace
  EXPECT_FALSE(Role::from_string(" user").has_value());
}

// Test LoggingLevel enum comprehensively
TEST_F(MCPTypesTest, LoggingLevelEnumComprehensive) {
  using namespace enums;

  // Test all enum values match RFC-5424 severities
  EXPECT_EQ(LoggingLevel::DEBUG, 0);
  EXPECT_EQ(LoggingLevel::INFO, 1);
  EXPECT_EQ(LoggingLevel::NOTICE, 2);
  EXPECT_EQ(LoggingLevel::WARNING, 3);
  EXPECT_EQ(LoggingLevel::ERROR, 4);
  EXPECT_EQ(LoggingLevel::CRITICAL, 5);
  EXPECT_EQ(LoggingLevel::ALERT, 6);
  EXPECT_EQ(LoggingLevel::EMERGENCY, 7);

  // Test to_string for all values
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::DEBUG), "debug");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::INFO), "info");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::NOTICE), "notice");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::WARNING), "warning");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::ERROR), "error");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::CRITICAL), "critical");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::ALERT), "alert");
  EXPECT_STREQ(LoggingLevel::to_string(LoggingLevel::EMERGENCY), "emergency");

  // Test invalid enum value
  EXPECT_STREQ(LoggingLevel::to_string(static_cast<LoggingLevel::Value>(999)),
               "");

  // Test from_string for all valid values
  auto debug_level = LoggingLevel::from_string("debug");
  ASSERT_TRUE(debug_level.has_value());
  EXPECT_EQ(debug_level.value(), LoggingLevel::DEBUG);

  auto emergency_level = LoggingLevel::from_string("emergency");
  ASSERT_TRUE(emergency_level.has_value());
  EXPECT_EQ(emergency_level.value(), LoggingLevel::EMERGENCY);

  // Test from_string edge cases
  EXPECT_FALSE(LoggingLevel::from_string("").has_value());
  EXPECT_FALSE(LoggingLevel::from_string("invalid").has_value());
  EXPECT_FALSE(
      LoggingLevel::from_string("DEBUG").has_value());  // Case sensitive
  EXPECT_FALSE(LoggingLevel::from_string("warn").has_value());  // Partial match
  EXPECT_FALSE(
      LoggingLevel::from_string("error ").has_value());  // Trailing space
}

// Test RequestId factory functions comprehensively
TEST_F(MCPTypesTest, RequestIdFactoriesComprehensive) {
  // Test string overload
  auto id1 = make_request_id(std::string("test-123"));
  EXPECT_TRUE(mcp::holds_alternative<std::string>(id1));
  EXPECT_EQ(mcp::get<std::string>(id1), "test-123");

  // Test int overload
  auto id2 = make_request_id(42);
  EXPECT_TRUE(mcp::holds_alternative<int>(id2));
  EXPECT_EQ(mcp::get<int>(id2), 42);

  // Test const char* overload
  auto id3 = make_request_id("literal-456");
  EXPECT_TRUE(mcp::holds_alternative<std::string>(id3));
  EXPECT_EQ(mcp::get<std::string>(id3), "literal-456");

  // Test edge cases
  auto id4 = make_request_id("");  // Empty string
  EXPECT_TRUE(mcp::holds_alternative<std::string>(id4));
  EXPECT_EQ(mcp::get<std::string>(id4), "");

  auto id5 = make_request_id(0);  // Zero
  EXPECT_TRUE(mcp::holds_alternative<int>(id5));
  EXPECT_EQ(mcp::get<int>(id5), 0);

  auto id6 = make_request_id(-1);  // Negative
  EXPECT_TRUE(mcp::holds_alternative<int>(id6));
  EXPECT_EQ(mcp::get<int>(id6), -1);
}

// Test ProgressToken factory functions comprehensively
TEST_F(MCPTypesTest, ProgressTokenFactoriesComprehensive) {
  // Test string overload
  auto token1 = make_progress_token(std::string("progress-xyz"));
  EXPECT_TRUE(mcp::holds_alternative<std::string>(token1));
  EXPECT_EQ(mcp::get<std::string>(token1), "progress-xyz");

  // Test int overload
  auto token2 = make_progress_token(100);
  EXPECT_TRUE(mcp::holds_alternative<int>(token2));
  EXPECT_EQ(mcp::get<int>(token2), 100);

  // Test const char* overload
  auto token3 = make_progress_token("literal-token");
  EXPECT_TRUE(mcp::holds_alternative<std::string>(token3));
  EXPECT_EQ(mcp::get<std::string>(token3), "literal-token");

  // Test edge cases
  auto token4 = make_progress_token("");  // Empty string
  EXPECT_TRUE(mcp::holds_alternative<std::string>(token4));
  EXPECT_EQ(mcp::get<std::string>(token4), "");

  auto token5 = make_progress_token(0);  // Zero
  EXPECT_TRUE(mcp::holds_alternative<int>(token5));
  EXPECT_EQ(mcp::get<int>(token5), 0);
}

// Test make_method_request function
TEST_F(MCPTypesTest, MakeMethodRequest) {
  struct TestParams {
    std::string param1;
    int param2;
  };

  auto id = make_request_id("test-req-1");
  auto result = make_method_request(id, "test/method", TestParams{"value", 42});

  EXPECT_TRUE(mcp::holds_alternative<std::string>(result.first));
  EXPECT_EQ(mcp::get<std::string>(result.first), "test-req-1");

  EXPECT_TRUE(result.second.has_method("test/method"));
  EXPECT_FALSE(result.second.has_method("other/method"));

  auto params_ptr = result.second.get_if<TestParams>();
  ASSERT_NE(params_ptr, nullptr);
  EXPECT_EQ(params_ptr->param1, "value");
  EXPECT_EQ(params_ptr->param2, 42);
}

// Test all MCP base types and their factories
TEST_F(MCPTypesTest, AllMCPTypesComprehensive) {
  // TextContent
  TextContent text1("Hello world");
  EXPECT_EQ(text1.type, "text");
  EXPECT_EQ(text1.text, "Hello world");

  TextContent text2;  // Default constructor
  EXPECT_EQ(text2.type, "text");
  EXPECT_TRUE(text2.text.empty());

  // ImageContent
  ImageContent image1("base64data", "image/png");
  EXPECT_EQ(image1.type, "image");
  EXPECT_EQ(image1.data, "base64data");
  EXPECT_EQ(image1.mimeType, "image/png");

  ImageContent image2;  // Default constructor
  EXPECT_EQ(image2.type, "image");
  EXPECT_TRUE(image2.data.empty());
  EXPECT_TRUE(image2.mimeType.empty());

  // AudioContent
  AudioContent audio1("audiodata", "audio/mp3");
  EXPECT_EQ(audio1.type, "audio");
  EXPECT_EQ(audio1.data, "audiodata");
  EXPECT_EQ(audio1.mimeType, "audio/mp3");

  // Resource
  Resource resource1("file:///test.txt", "test.txt");
  EXPECT_EQ(resource1.uri, "file:///test.txt");
  EXPECT_EQ(resource1.name, "test.txt");
  EXPECT_FALSE(resource1.description.has_value());
  EXPECT_FALSE(resource1.mimeType.has_value());

  Resource resource2;
  EXPECT_TRUE(resource2.uri.empty());
  EXPECT_TRUE(resource2.name.empty());

  // ResourceContent
  ResourceContent rc(resource1);
  EXPECT_EQ(rc.type, "resource");
  EXPECT_EQ(rc.resource.uri, "file:///test.txt");

  // Tool
  Tool tool1;
  tool1.name = "calculator";
  EXPECT_EQ(tool1.name, "calculator");
  EXPECT_FALSE(tool1.description.has_value());
  EXPECT_FALSE(tool1.inputSchema.has_value());

  Tool tool2;
  EXPECT_TRUE(tool2.name.empty());

  // Prompt
  Prompt prompt1;
  prompt1.name = "math-helper";
  EXPECT_EQ(prompt1.name, "math-helper");
  EXPECT_FALSE(prompt1.description.has_value());
  EXPECT_FALSE(prompt1.arguments.has_value());

  // Error
  Error error1(404, "Not found");
  EXPECT_EQ(error1.code, 404);
  EXPECT_EQ(error1.message, "Not found");
  EXPECT_FALSE(error1.data.has_value());

  Error error2;
  // Default constructor doesn't initialize code, just check message
  EXPECT_TRUE(error2.message.empty());

  // Message
  Message msg1(enums::Role::USER, TextContent("Hello"));
  EXPECT_EQ(msg1.role, enums::Role::USER);
  EXPECT_TRUE(mcp::holds_alternative<TextContent>(msg1.content));

  auto msg2 = make_user_message("Hi there");
  EXPECT_EQ(msg2.role, enums::Role::USER);

  auto msg3 = make_assistant_message("Hello back");
  EXPECT_EQ(msg3.role, enums::Role::ASSISTANT);
}

// Test all extended content types
TEST_F(MCPTypesTest, ExtendedContentTypesComprehensive) {
  // Audio content
  auto audio = make_audio_content("base64audio", "audio/wav");
  EXPECT_TRUE(mcp::holds_alternative<AudioContent>(audio));
  auto& audio_content = mcp::get<AudioContent>(audio);
  EXPECT_EQ(audio_content.type, "audio");
  EXPECT_EQ(audio_content.data, "base64audio");
  EXPECT_EQ(audio_content.mimeType, "audio/wav");

  // Resource link
  auto resource = make_resource("http://example.com/file.pdf", "file.pdf");
  auto link = make_resource_link(resource);
  EXPECT_TRUE(mcp::holds_alternative<ResourceLink>(link));
  auto& link_content = mcp::get<ResourceLink>(link);
  EXPECT_EQ(link_content.type, "resource");
  EXPECT_EQ(link_content.uri, "http://example.com/file.pdf");
  EXPECT_EQ(link_content.name, "file.pdf");

  // Embedded resource
  auto embedded = make_embedded_resource(resource);
  EXPECT_TRUE(mcp::holds_alternative<EmbeddedResource>(embedded));
  auto& embedded_content = mcp::get<EmbeddedResource>(embedded);
  EXPECT_EQ(embedded_content.type, "embedded");
  EXPECT_EQ(embedded_content.resource.uri, "http://example.com/file.pdf");
  EXPECT_TRUE(embedded_content.content.empty());
}

// Test JSON-RPC error codes
TEST_F(MCPTypesTest, JSONRPCErrorCodesComprehensive) {
  using namespace jsonrpc;

  // Test all defined error codes
  EXPECT_EQ(jsonrpc::PARSE_ERROR, -32700);
  EXPECT_EQ(jsonrpc::INVALID_REQUEST, -32600);
  EXPECT_EQ(jsonrpc::METHOD_NOT_FOUND, -32601);
  EXPECT_EQ(jsonrpc::INVALID_PARAMS, -32602);
  EXPECT_EQ(jsonrpc::INTERNAL_ERROR, -32603);

  // Test using error codes in Error objects
  Error parse_err(jsonrpc::PARSE_ERROR, "Parse error");
  EXPECT_EQ(parse_err.code, -32700);

  Error invalid_req(jsonrpc::INVALID_REQUEST, "Invalid request");
  EXPECT_EQ(invalid_req.code, -32600);

  Error method_not_found(jsonrpc::METHOD_NOT_FOUND, "Method not found");
  EXPECT_EQ(method_not_found.code, -32601);

  Error invalid_params(jsonrpc::INVALID_PARAMS, "Invalid params");
  EXPECT_EQ(invalid_params.code, -32602);

  Error internal_err(jsonrpc::INTERNAL_ERROR, "Internal error");
  EXPECT_EQ(internal_err.code, -32603);
}

// Test Result/Error pattern helpers comprehensively
TEST_F(MCPTypesTest, ResultErrorPatternComprehensive) {
  // Test successful results
  Result<int> result1 = make_result<int>(42);
  EXPECT_TRUE(is_success<int>(result1));
  EXPECT_FALSE(is_error<int>(result1));

  auto value_ptr = get_value<int>(result1);
  ASSERT_NE(value_ptr, nullptr);
  EXPECT_EQ(*value_ptr, 42);

  auto error_ptr = get_error<int>(result1);
  EXPECT_EQ(error_ptr, nullptr);

  // Test error results
  Result<int> result2 = make_error_result<int>(Error(500, "Server error"));
  EXPECT_FALSE(is_success<int>(result2));
  EXPECT_TRUE(is_error<int>(result2));

  auto value_ptr2 = get_value<int>(result2);
  EXPECT_EQ(value_ptr2, nullptr);

  auto error_ptr2 = get_error<int>(result2);
  ASSERT_NE(error_ptr2, nullptr);
  EXPECT_EQ(error_ptr2->code, 500);
  EXPECT_EQ(error_ptr2->message, "Server error");

  // Test null result
  Result<std::nullptr_t> result3 = make_result(nullptr);
  EXPECT_TRUE(is_success<std::nullptr_t>(result3));
  EXPECT_FALSE(is_error<std::nullptr_t>(result3));

  // Test move semantics for error
  Error move_error(404, "Not found");
  Result<std::string> result4 =
      make_error_result<std::string>(std::move(move_error));
  EXPECT_TRUE(is_error<std::string>(result4));

  // Test with different types
  Result<std::string> string_result =
      make_result<std::string>(std::string("success"));
  EXPECT_TRUE(is_success<std::string>(string_result));
  auto string_value = get_value<std::string>(string_result);
  ASSERT_NE(string_value, nullptr);
  EXPECT_EQ(*string_value, "success");
}

// Test all builder patterns comprehensively
TEST_F(MCPTypesTest, BuilderPatternsComprehensive) {
  // ResourceBuilder
  auto resource = build_resource("file:///complex.pdf", "complex.pdf")
                      .description("A complex document")
                      .mimeType("application/pdf")
                      .build();

  EXPECT_EQ(resource.uri, "file:///complex.pdf");
  EXPECT_EQ(resource.name, "complex.pdf");
  ASSERT_TRUE(resource.description.has_value());
  EXPECT_EQ(resource.description.value(), "A complex document");
  ASSERT_TRUE(resource.mimeType.has_value());
  EXPECT_EQ(resource.mimeType.value(), "application/pdf");

  // Test rvalue and lvalue build()
  auto builder = build_resource("file:///test.txt", "test.txt");
  auto resource_copy = builder.build();             // lvalue build
  auto resource_move = std::move(builder).build();  // rvalue build

  EXPECT_EQ(resource_copy.uri, "file:///test.txt");
  EXPECT_EQ(resource_move.uri, "file:///test.txt");

  // ToolBuilder
  mcp::ToolInputSchema schema = mcp::json::JsonValue::parse(R"({
    "type": "object",
    "properties": {
      "expression": {
        "type": "string",
        "description": "Math expression"
      },
      "precision": {
        "type": "number",
        "description": "Precision for calculations"
      },
      "format": {
        "type": "string",
        "description": "Output format"
      }
    },
    "required": ["expression"]
  })");

  auto tool = build_tool("advanced_calc")
                  .description("Advanced calculator with multiple functions")
                  .inputSchema(schema)
                  .build();

  EXPECT_EQ(tool.name, "advanced_calc");
  ASSERT_TRUE(tool.description.has_value());
  EXPECT_EQ(tool.description.value(),
            "Advanced calculator with multiple functions");
  ASSERT_TRUE(tool.inputSchema.has_value());
  EXPECT_EQ(tool.inputSchema.value()["type"].getString(), "object");
  EXPECT_TRUE(tool.inputSchema.value().contains("properties"));
  EXPECT_TRUE(tool.inputSchema.value()["properties"].contains("expression"));
  EXPECT_EQ(tool.inputSchema.value()["properties"]["expression"]["type"].getString(),
            "string");

  // Note: tool.parameters is not set by the builder, only inputSchema
  EXPECT_FALSE(tool.parameters.has_value());

  // SamplingParamsBuilder
  auto params = build_sampling_params()
                    .temperature(0.8)
                    .maxTokens(2000)
                    .stopSequence("\n\n")
                    .stopSequence("END")
                    .stopSequence("STOP")
                    .metadata("model", "gpt-4")
                    .metadata("stream", true)
                    .metadata("timeout", 30)
                    .build();

  ASSERT_TRUE(params.temperature.has_value());
  EXPECT_DOUBLE_EQ(params.temperature.value(), 0.8);
  ASSERT_TRUE(params.maxTokens.has_value());
  EXPECT_EQ(params.maxTokens.value(), 2000);
  ASSERT_TRUE(params.stopSequences.has_value());
  EXPECT_EQ(params.stopSequences->size(), 3u);
  EXPECT_EQ((*params.stopSequences)[0], "\n\n");
  EXPECT_EQ((*params.stopSequences)[2], "STOP");
  ASSERT_TRUE(params.metadata.has_value());
  EXPECT_EQ(params.metadata->size(), 3u);

  // EmbeddedResourceBuilder
  auto embedded_resource = build_embedded_resource(resource)
                               .add_text("Page 1 content")
                               .add_text("Page 2 content")
                               .add_image("img1_data", "image/png")
                               .add_content(make_text_content("Custom content"))
                               .build();

  EXPECT_EQ(embedded_resource.type, "embedded");
  EXPECT_EQ(embedded_resource.resource.uri, "file:///complex.pdf");
  EXPECT_EQ(embedded_resource.content.size(), 4u);
  EXPECT_TRUE(mcp::holds_alternative<TextContent>(embedded_resource.content[0]));
  EXPECT_TRUE(mcp::holds_alternative<ImageContent>(embedded_resource.content[2]));
}

// Test edge cases and boundary values
TEST_F(MCPTypesTest, EdgeCasesAndBoundaryValues) {
  // Empty strings
  auto empty_text = make_text("");
  EXPECT_EQ(empty_text.text, "");

  auto empty_resource = make_resource("", "");
  EXPECT_EQ(empty_resource.uri, "");
  EXPECT_EQ(empty_resource.name, "");

  // Very long strings
  std::string long_string(10000, 'a');
  auto long_text = make_text(long_string);
  EXPECT_EQ(long_text.text.length(), 10000u);

  // Special characters
  auto special_text = make_text("Hello\nWorld\t\"Test\"");
  EXPECT_EQ(special_text.text, "Hello\nWorld\t\"Test\"");

  // Unicode characters
  auto unicode_text = make_text("こんにちは 🌍 🚀");
  EXPECT_EQ(unicode_text.text, "こんにちは 🌍 🚀");

  // Zero and negative numbers
  auto zero_id = make_request_id(0);
  EXPECT_EQ(mcp::get<int>(zero_id), 0);

  auto negative_id = make_request_id(-999);
  EXPECT_EQ(mcp::get<int>(negative_id), -999);

  // Maximum/minimum values
  auto max_int_id = make_request_id(std::numeric_limits<int>::max());
  EXPECT_EQ(mcp::get<int>(max_int_id), std::numeric_limits<int>::max());

  auto min_int_id = make_request_id(std::numeric_limits<int>::min());
  EXPECT_EQ(mcp::get<int>(min_int_id), std::numeric_limits<int>::min());

  // Error with data - using simplified ErrorData type
  Error error_with_data(400, "Bad request");
  error_with_data.data = mcp::make_optional(
      ErrorData(std::string("Additional error info")));

  ASSERT_TRUE(error_with_data.data.has_value());
  EXPECT_TRUE(mcp::holds_alternative<std::string>(*error_with_data.data));
}