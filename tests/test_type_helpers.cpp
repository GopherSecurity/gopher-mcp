#include <gtest/gtest.h>

#include "mcp/type_helpers.h"
#include "mcp/types.h"

using namespace mcp;

class TypeHelpersTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

// Test optional factory functions
TEST_F(TypeHelpersTest, OptionalFactories) {
  // Test opt() function
  auto opt1 = opt(42);
  EXPECT_TRUE(opt1.has_value());
  EXPECT_EQ(opt1.value(), 42);

  auto opt2 = opt(std::string("hello"));
  EXPECT_TRUE(opt2.has_value());
  EXPECT_EQ(opt2.value(), "hello");

  // Test none() function
  auto opt3 = none<int>();
  EXPECT_FALSE(opt3.has_value());

  auto opt4 = none<std::string>();
  EXPECT_FALSE(opt4.has_value());
}

// Test RequestId factory functions
TEST_F(TypeHelpersTest, RequestIdFactories) {
  // String ID
  auto id1 = make_request_id("req-123");
  EXPECT_TRUE(id1.holds_alternative<std::string>());
  EXPECT_EQ(id1.get<std::string>(), "req-123");

  // Integer ID
  auto id2 = make_request_id(42);
  EXPECT_TRUE(id2.holds_alternative<int>());
  EXPECT_EQ(id2.get<int>(), 42);

  // C-string ID (should convert to std::string)
  auto id3 = make_request_id("req-456");
  EXPECT_TRUE(id3.holds_alternative<std::string>());
  EXPECT_EQ(id3.get<std::string>(), "req-456");
}

// Test ProgressToken factory functions
TEST_F(TypeHelpersTest, ProgressTokenFactories) {
  auto token1 = make_progress_token("progress-1");
  EXPECT_TRUE(token1.holds_alternative<std::string>());

  auto token2 = make_progress_token(100);
  EXPECT_TRUE(token2.holds_alternative<int>());
}

// Test ContentBlock factories
TEST_F(TypeHelpersTest, ContentBlockFactories) {
  // Text content
  auto text = make_text_content("Hello, world!");
  EXPECT_TRUE(text.holds_alternative<TextContent>());
  EXPECT_EQ(text.get<TextContent>().text, "Hello, world!");

  // Image content
  auto image = make_image_content("base64data", "image/png");
  EXPECT_TRUE(image.holds_alternative<ImageContent>());
  EXPECT_EQ(image.get<ImageContent>().data, "base64data");
  EXPECT_EQ(image.get<ImageContent>().mimeType, "image/png");

  // Resource content
  Resource res("file:///test.txt", "test.txt");
  auto resource = make_resource_content(res);
  EXPECT_TRUE(resource.holds_alternative<ResourceContent>());
  EXPECT_EQ(resource.get<ResourceContent>().resource.uri, "file:///test.txt");
}

// Test TypeDiscriminator
TEST_F(TypeHelpersTest, TypeDiscriminator) {
  using ContentDiscriminator =
      TypeDiscriminator<TextContent, ImageContent, ResourceContent>;

  auto content1 = ContentDiscriminator::create(TextContent("Hello"));
  EXPECT_TRUE(ContentDiscriminator::is_type<TextContent>(content1));
  EXPECT_FALSE(ContentDiscriminator::is_type<ImageContent>(content1));

  auto text_ptr = ContentDiscriminator::get_if<TextContent>(content1);
  ASSERT_NE(text_ptr, nullptr);
  EXPECT_EQ(text_ptr->text, "Hello");

  auto image_ptr = ContentDiscriminator::get_if<ImageContent>(content1);
  EXPECT_EQ(image_ptr, nullptr);
}

// Test MethodDiscriminator
TEST_F(TypeHelpersTest, MethodDiscriminator) {
  struct InitializeParams {
    int version;
  };
  struct ShutdownParams {
    bool restart;
  };

  auto notif1 = make_method_notification("initialize", InitializeParams{1});
  EXPECT_TRUE(notif1.has_method("initialize"));
  EXPECT_FALSE(notif1.has_method("shutdown"));
  EXPECT_TRUE(notif1.is_type<InitializeParams>());

  auto params = notif1.get_if<InitializeParams>();
  ASSERT_NE(params, nullptr);
  EXPECT_EQ(params->version, 1);
}

// Test enum helpers
TEST_F(TypeHelpersTest, EnumHelpers) {
  // Role enum
  EXPECT_STREQ(enums::Role::to_string(enums::Role::USER), "user");
  EXPECT_STREQ(enums::Role::to_string(enums::Role::ASSISTANT), "assistant");

  auto role1 = enums::Role::from_string("user");
  ASSERT_TRUE(role1.has_value());
  EXPECT_EQ(role1.value(), enums::Role::USER);

  auto role2 = enums::Role::from_string("invalid");
  EXPECT_FALSE(role2.has_value());

  // LoggingLevel enum
  EXPECT_STREQ(enums::LoggingLevel::to_string(enums::LoggingLevel::DEBUG),
               "debug");
  EXPECT_STREQ(enums::LoggingLevel::to_string(enums::LoggingLevel::ERROR),
               "error");

  auto level = enums::LoggingLevel::from_string("warning");
  ASSERT_TRUE(level.has_value());
  EXPECT_EQ(level.value(), enums::LoggingLevel::WARNING);
}

// Test Metadata
TEST_F(TypeHelpersTest, Metadata) {
  auto meta = make_metadata();
  EXPECT_TRUE(meta.empty());

  add_metadata(meta, "string_key", "value");
  add_metadata(meta, "int_key", 42);
  add_metadata(meta, "bool_key", true);
  add_metadata(meta, "null_key", nullptr);

  EXPECT_EQ(meta.size(), 4u);
  EXPECT_TRUE(meta["string_key"].holds_alternative<std::string>());
  EXPECT_TRUE(meta["int_key"].holds_alternative<int64_t>());
  EXPECT_TRUE(meta["bool_key"].holds_alternative<bool>());
  EXPECT_TRUE(meta["null_key"].holds_alternative<std::nullptr_t>());
}

// Test Capability helpers
TEST_F(TypeHelpersTest, CapabilityHelpers) {
  struct ExperimentalFeatures {
    bool feature1;
    bool feature2;
  };

  auto cap1 = make_capability<ExperimentalFeatures>();
  EXPECT_FALSE(cap1.experimental.has_value());

  auto cap2 = make_capability(ExperimentalFeatures{true, false});
  EXPECT_TRUE(cap2.experimental.has_value());
  EXPECT_TRUE(cap2.experimental->feature1);
  EXPECT_FALSE(cap2.experimental->feature2);
}

// Test Result/Error pattern
TEST_F(TypeHelpersTest, ResultErrorPattern) {
  // Success case
  Result<int> result1 = make_result<int>(42);
  EXPECT_TRUE(is_success<int>(result1));
  EXPECT_FALSE(is_error<int>(result1));

  auto value = get_value<int>(result1);
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(*value, 42);

  // Error case
  Result<int> result2 = make_error_result<int>(Error(404, "Not found"));
  EXPECT_FALSE(is_success<int>(result2));
  EXPECT_TRUE(is_error<int>(result2));

  auto error = get_error<int>(result2);
  ASSERT_NE(error, nullptr);
  EXPECT_EQ(error->code, 404);
  EXPECT_EQ(error->message, "Not found");

  // Null result
  Result<std::nullptr_t> result3 = make_result(nullptr);
  EXPECT_TRUE(is_success<std::nullptr_t>(result3));
}

// Test array factories
TEST_F(TypeHelpersTest, ArrayFactories) {
  auto arr1 = make_array<int>();
  EXPECT_TRUE(arr1.empty());

  auto arr2 = make_array<int>({1, 2, 3, 4, 5});
  EXPECT_EQ(arr2.size(), 5u);
  EXPECT_EQ(arr2[0], 1);
  EXPECT_EQ(arr2[4], 5);

  auto arr3 = make_array<std::string>("hello", "world");
  EXPECT_EQ(arr3.size(), 2u);
  EXPECT_EQ(arr3[0], "hello");
  EXPECT_EQ(arr3[1], "world");
}

// Test ObjectBuilder
TEST_F(TypeHelpersTest, ObjectBuilder) {
  struct Person {
    std::string name;
    int age;
    optional<std::string> email;
  };

  auto person =
      make_object<Person>()
          .set(&Person::name, std::string("John Doe"))
          .set(&Person::age, 30)
          .set_optional(&Person::email, std::string("john@example.com"))
          .build();

  EXPECT_EQ(person.name, "John Doe");
  EXPECT_EQ(person.age, 30);
  ASSERT_TRUE(person.email.has_value());
  EXPECT_EQ(person.email.value(), "john@example.com");
}

// Test match helper (variant visitation)
TEST_F(TypeHelpersTest, MatchHelper) {
  variant<int, double, std::string> v(42);

  auto result = match(
      v, [](int i) { return i * 2; },
      [](double d) { return static_cast<int>(d); },
      [](const std::string& s) { return static_cast<int>(s.length()); });

  EXPECT_EQ(result, 84);

  v = 3.14;
  result = match(
      v, [](int i) { return i * 2; },
      [](double d) { return static_cast<int>(d); },
      [](const std::string& s) { return static_cast<int>(s.length()); });

  EXPECT_EQ(result, 3);

  v = std::string("hello");
  result = match(
      v, [](int i) { return i * 2; },
      [](double d) { return static_cast<int>(d); },
      [](const std::string& s) { return static_cast<int>(s.length()); });

  EXPECT_EQ(result, 5);
}

// Test MCP types factories
TEST_F(TypeHelpersTest, MCPTypeFactories) {
  // Test simple message creation
  auto msg1 = make_user_message("Hello");
  EXPECT_EQ(msg1.role, enums::Role::USER);
  EXPECT_TRUE(msg1.content.holds_alternative<TextContent>());

  auto msg2 = make_assistant_message("Hi there");
  EXPECT_EQ(msg2.role, enums::Role::ASSISTANT);
  EXPECT_TRUE(msg2.content.holds_alternative<TextContent>());

  // Test resource builder
  auto resource = build_resource("file:///doc.pdf", "document.pdf")
                      .description("Important document")
                      .mimeType("application/pdf")
                      .build();

  EXPECT_EQ(resource.uri, "file:///doc.pdf");
  EXPECT_EQ(resource.name, "document.pdf");
  ASSERT_TRUE(resource.description.has_value());
  EXPECT_EQ(resource.description.value(), "Important document");
  ASSERT_TRUE(resource.mimeType.has_value());
  EXPECT_EQ(resource.mimeType.value(), "application/pdf");

  // Test tool builder
  mcp::ToolInputSchema schema;
  schema["type"] = "object";
  schema["properties"]["expression"] = {
    {"type", "string"},
    {"description", "The math expression to evaluate"}
  };
  schema["required"] = {"expression"};

  auto tool = build_tool("calculator")
                  .description("Performs mathematical calculations")
                  .inputSchema(schema)
                  .build();

  EXPECT_EQ(tool.name, "calculator");
  ASSERT_TRUE(tool.description.has_value());
  EXPECT_EQ(tool.description.value(), "Performs mathematical calculations");
  ASSERT_TRUE(tool.inputSchema.has_value());
  EXPECT_EQ(tool.inputSchema.value()["type"], "object");

  // Test sampling params builder
  auto params = build_sampling_params()
                    .temperature(0.7)
                    .maxTokens(1000)
                    .stopSequence("\\n\\n")
                    .stopSequence("END")
                    .metadata("model", "gpt-4")
                    .metadata("stream", true)
                    .build();

  ASSERT_TRUE(params.temperature.has_value());
  EXPECT_DOUBLE_EQ(params.temperature.value(), 0.7);
  ASSERT_TRUE(params.maxTokens.has_value());
  EXPECT_EQ(params.maxTokens.value(), 1000);
  ASSERT_TRUE(params.stopSequences.has_value());
  EXPECT_EQ(params.stopSequences->size(), 2u);
  ASSERT_TRUE(params.metadata.has_value());
  EXPECT_TRUE(params.metadata->at("model").holds_alternative<std::string>());
  EXPECT_TRUE(params.metadata->at("stream").holds_alternative<bool>());
}

// Test JSON-RPC factories
TEST_F(TypeHelpersTest, JSONRPCFactories) {
  using namespace jsonrpc;

  // Test request creation
  auto req1 = make_request(make_request_id(1), "initialize");
  EXPECT_EQ(req1.jsonrpc, "2.0");
  EXPECT_TRUE(req1.id.holds_alternative<int>());
  EXPECT_EQ(req1.method, "initialize");
  EXPECT_FALSE(req1.params.has_value());

  // Test request with params
  Metadata params;
  add_metadata(params, "version", "1.0");
  auto req2 = make_request(make_request_id("req-2"), "call_tool", params);
  EXPECT_TRUE(req2.params.has_value());

  // Test notification
  auto notif = make_notification("progress");
  EXPECT_EQ(notif.jsonrpc, "2.0");
  EXPECT_EQ(notif.method, "progress");

  // Test response
  auto resp1 = make_response(make_request_id(1), 42);
  EXPECT_TRUE(resp1.result.has_value());
  EXPECT_FALSE(resp1.error.has_value());

  auto resp2 = make_error_response(make_request_id(1), 404, "Not found");
  EXPECT_FALSE(resp2.result.has_value());
  EXPECT_TRUE(resp2.error.has_value());
  EXPECT_EQ(resp2.error->code, 404);
}

// Test protocol message factories
TEST_F(TypeHelpersTest, ProtocolMessageFactories) {
  // Test initialize params builder
  auto init_params = build_initialize_params("1.0.0")
                         .clientName("test-client")
                         .clientVersion("0.1.0")
                         .capability("tools", true)
                         .capability("prompts", false)
                         .build();

  EXPECT_EQ(init_params.protocolVersion, "1.0.0");
  ASSERT_TRUE(init_params.clientName.has_value());
  EXPECT_EQ(init_params.clientName.value(), "test-client");
  ASSERT_TRUE(init_params.capabilities.has_value());
  EXPECT_TRUE(init_params.capabilities->at("tools").holds_alternative<bool>());

  // Test tool call
  auto call1 = make_tool_call("calculator");
  EXPECT_EQ(call1.name, "calculator");
  EXPECT_FALSE(call1.arguments.has_value());

  Metadata args;
  add_metadata(args, "expression", "2 + 2");
  auto call2 = make_tool_call("calculator", args);
  EXPECT_TRUE(call2.arguments.has_value());

  // Test tool result
  std::vector<ContentBlock> content;
  content.push_back(make_text_content("Result: 4"));
  auto result = make_tool_result(std::move(content));
  EXPECT_EQ(result.content.size(), 1u);
  EXPECT_TRUE(result.content[0].holds_alternative<TextContent>());
}

// Test string literal helper
TEST_F(TypeHelpersTest, StringLiteral) {
  constexpr auto lit1 = make_string_literal("hello");
  constexpr auto lit2 = make_string_literal("hello");
  constexpr auto lit3 = make_string_literal("world");

  static_assert(lit1 == lit2, "String literals should compare equal");
  static_assert(!(lit1 == lit3),
                "Different string literals should not be equal");

  EXPECT_STREQ(lit1.c_str(), "hello");
  EXPECT_EQ(lit1.size(), 5u);
}

// Integration test - Building a complete MCP request
TEST_F(TypeHelpersTest, IntegrationCompleteRequest) {
  using namespace jsonrpc;

  // Build a complete tool call request
  auto tool_params = make_metadata();
  add_metadata(tool_params, "expression", "sqrt(16) + 3^2");
  add_metadata(tool_params, "precision", 2);

  auto call_params = make_tool_call("calculator", tool_params);

  // Convert CallToolRequest to a proper JSONRPC request
  auto request =
      call_params;  // CallToolRequest already inherits from jsonrpc::Request
  request.id = make_request_id("req-calc-1");

  EXPECT_EQ(request.method, "tools/call");
  EXPECT_TRUE(request.id.holds_alternative<std::string>());

  // Simulate response with content blocks
  std::vector<ContentBlock> result_content;
  result_content.push_back(make_text_content("Result: 13.00"));

  // Create response directly with content blocks
  auto response = make_response(request.id, result_content);

  EXPECT_TRUE(response.result.has_value());
  EXPECT_FALSE(response.error.has_value());

  // Check that result contains vector of ContentBlocks
  EXPECT_TRUE(response.result->holds_alternative<std::vector<ContentBlock>>());
}
