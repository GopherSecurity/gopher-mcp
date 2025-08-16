/**
 * @file test_c_api_builders.cc
 * @brief Comprehensive unit tests for MCP C API builder patterns
 *
 * Tests all builder functions for creating MCP types in C API,
 * ensuring production-ready quality and complete coverage.
 */

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/c_api/mcp_c_types.h"

class MCPCApiBuildersTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Any common setup
  }

  void TearDown() override {
    // Cleanup
  }

  // Helper to verify string content
  void ExpectStringEqual(const char* actual, const char* expected) {
    if (expected == nullptr) {
      EXPECT_EQ(actual, nullptr);
    } else {
      ASSERT_NE(actual, nullptr);
      EXPECT_STREQ(actual, expected);
    }
  }
};

// ==================== CONTENT BLOCK BUILDERS ====================

TEST_F(MCPCApiBuildersTest, TextContentBuilder) {
  // Simple text content
  auto* content = mcp_text_content_create("Hello, World!");
  ASSERT_NE(content, nullptr);
  EXPECT_EQ(content->type, MCP_CONTENT_TEXT);
  ExpectStringEqual(content->text.text, "Hello, World!");
  EXPECT_FALSE(content->text.annotations.has_value);

  // Validate the created content
  EXPECT_TRUE(mcp_content_block_is_valid(content));
  EXPECT_TRUE(mcp_content_is_text(content));

  mcp_content_block_free(content);
}

TEST_F(MCPCApiBuildersTest, TextContentWithAnnotations) {
  // Text content with full annotations
  mcp_role_t audience[] = {MCP_ROLE_USER, MCP_ROLE_ASSISTANT};
  auto* content = mcp_text_content_with_annotations("Annotated text content",
                                                    audience, 2, 0.85);

  ASSERT_NE(content, nullptr);
  EXPECT_EQ(content->type, MCP_CONTENT_TEXT);
  ExpectStringEqual(content->text.text, "Annotated text content");

  // Check annotations
  EXPECT_TRUE(content->text.annotations.has_value);
  EXPECT_EQ(content->text.annotations.value.audience_count, 2);
  EXPECT_EQ(content->text.annotations.value.audience[0], MCP_ROLE_USER);
  EXPECT_EQ(content->text.annotations.value.audience[1], MCP_ROLE_ASSISTANT);
  EXPECT_TRUE(content->text.annotations.value.priority_set);
  EXPECT_DOUBLE_EQ(content->text.annotations.value.priority, 0.85);

  mcp_content_block_free(content);
}

TEST_F(MCPCApiBuildersTest, TextContentEdgeCases) {
  // Empty string
  auto* empty = mcp_text_content_create("");
  ASSERT_NE(empty, nullptr);
  ExpectStringEqual(empty->text.text, "");
  EXPECT_TRUE(mcp_content_block_is_valid(empty));
  mcp_content_block_free(empty);

  // Very long text
  std::string longText(10000, 'a');
  auto* longContent = mcp_text_content_create(longText.c_str());
  ASSERT_NE(longContent, nullptr);
  EXPECT_EQ(strlen(longContent->text.text), 10000);
  mcp_content_block_free(longContent);

  // Invalid priority (should be clamped or ignored)
  mcp_role_t roles[] = {MCP_ROLE_USER};
  auto* invalidPriority =
      mcp_text_content_with_annotations("Test", roles, 1, 2.0  // Priority > 1.0
      );
  ASSERT_NE(invalidPriority, nullptr);
  // Implementation should handle invalid priority appropriately
  mcp_content_block_free(invalidPriority);
}

TEST_F(MCPCApiBuildersTest, ImageContentBuilder) {
  auto* content =
      mcp_image_content_create("iVBORw0KGgoAAAANSUhEUgAAAAEAAAAB", "image/png");

  ASSERT_NE(content, nullptr);
  EXPECT_EQ(content->type, MCP_CONTENT_IMAGE);
  ExpectStringEqual(content->image.data, "iVBORw0KGgoAAAANSUhEUgAAAAEAAAAB");
  ExpectStringEqual(content->image.mime_type, "image/png");

  EXPECT_TRUE(mcp_content_block_is_valid(content));
  EXPECT_TRUE(mcp_content_is_image(content));
  EXPECT_FALSE(mcp_content_is_text(content));

  mcp_content_block_free(content);
}

TEST_F(MCPCApiBuildersTest, AudioContentBuilder) {
  auto* content = mcp_audio_content_create("base64audiodata", "audio/mp3");

  ASSERT_NE(content, nullptr);
  EXPECT_EQ(content->type, MCP_CONTENT_AUDIO);
  ExpectStringEqual(content->audio.data, "base64audiodata");
  ExpectStringEqual(content->audio.mime_type, "audio/mp3");

  EXPECT_TRUE(mcp_content_block_is_valid(content));
  EXPECT_TRUE(mcp_content_is_audio(content));

  mcp_content_block_free(content);
}

TEST_F(MCPCApiBuildersTest, ResourceContentBuilder) {
  // Create resource first
  mcp_resource_t resource = {
      .uri = const_cast<char*>("file:///path/to/file.txt"),
      .name = const_cast<char*>("file.txt"),
      .description = const_cast<char*>("A text file"),
      .mime_type = const_cast<char*>("text/plain")};

  auto* content = mcp_resource_content_create(&resource);

  ASSERT_NE(content, nullptr);
  EXPECT_EQ(content->type, MCP_CONTENT_RESOURCE);
  ExpectStringEqual(content->resource.resource.uri, "file:///path/to/file.txt");
  ExpectStringEqual(content->resource.resource.name, "file.txt");
  ExpectStringEqual(content->resource.resource.description, "A text file");
  ExpectStringEqual(content->resource.resource.mime_type, "text/plain");

  EXPECT_TRUE(mcp_content_block_is_valid(content));
  EXPECT_TRUE(mcp_content_is_resource(content));

  mcp_content_block_free(content);
}

TEST_F(MCPCApiBuildersTest, EmbeddedResourceBuilder) {
  // Create resource
  mcp_resource_t resource = {.uri = const_cast<char*>("file:///doc.pdf"),
                             .name = const_cast<char*>("document.pdf"),
                             .description = const_cast<char*>("PDF Document"),
                             .mime_type = const_cast<char*>("application/pdf")};

  // Create nested content
  mcp_content_block_t nested[3];
  nested[0] = *mcp_text_content_create("Page 1 content");
  nested[1] = *mcp_text_content_create("Page 2 content");
  nested[2] = *mcp_image_content_create("imagedata", "image/png");

  auto* embedded = mcp_embedded_resource_create(&resource, nested, 3);

  ASSERT_NE(embedded, nullptr);
  EXPECT_EQ(embedded->type, MCP_CONTENT_EMBEDDED);
  ExpectStringEqual(embedded->embedded.resource.uri, "file:///doc.pdf");
  EXPECT_EQ(embedded->embedded.content_count, 3);

  // Verify nested content
  EXPECT_EQ(embedded->embedded.content[0].type, MCP_CONTENT_TEXT);
  EXPECT_EQ(embedded->embedded.content[2].type, MCP_CONTENT_IMAGE);

  EXPECT_TRUE(mcp_content_block_is_valid(embedded));
  EXPECT_TRUE(mcp_content_is_embedded(embedded));

  // Clean up nested content originals
  mcp_content_block_free(&nested[0]);
  mcp_content_block_free(&nested[1]);
  mcp_content_block_free(&nested[2]);

  mcp_content_block_free(embedded);
}

// ==================== RESOURCE BUILDERS ====================

TEST_F(MCPCApiBuildersTest, ResourceBuilder) {
  // Simple resource
  auto* resource =
      mcp_resource_create("https://example.com/api", "API Endpoint");

  ASSERT_NE(resource, nullptr);
  ExpectStringEqual(resource->uri, "https://example.com/api");
  ExpectStringEqual(resource->name, "API Endpoint");
  EXPECT_EQ(resource->description, nullptr);
  EXPECT_EQ(resource->mime_type, nullptr);

  EXPECT_TRUE(mcp_resource_is_valid(resource));

  mcp_resource_free(resource);
}

TEST_F(MCPCApiBuildersTest, ResourceWithDetails) {
  auto* resource =
      mcp_resource_with_details("file:///data/report.csv", "report.csv",
                                "Monthly sales report", "text/csv");

  ASSERT_NE(resource, nullptr);
  ExpectStringEqual(resource->uri, "file:///data/report.csv");
  ExpectStringEqual(resource->name, "report.csv");
  ExpectStringEqual(resource->description, "Monthly sales report");
  ExpectStringEqual(resource->mime_type, "text/csv");

  EXPECT_TRUE(mcp_resource_is_valid(resource));

  mcp_resource_free(resource);
}

// ==================== TOOL BUILDERS ====================

TEST_F(MCPCApiBuildersTest, ToolBuilder) {
  // Simple tool
  auto* tool = mcp_tool_create("calculator");

  ASSERT_NE(tool, nullptr);
  ExpectStringEqual(tool->name, "calculator");
  EXPECT_EQ(tool->description, nullptr);
  EXPECT_EQ(tool->input_schema, nullptr);

  EXPECT_TRUE(mcp_tool_is_valid(tool));

  mcp_tool_free(tool);
}

TEST_F(MCPCApiBuildersTest, ToolWithDescription) {
  auto* tool =
      mcp_tool_with_description("weather", "Get current weather information");

  ASSERT_NE(tool, nullptr);
  ExpectStringEqual(tool->name, "weather");
  ExpectStringEqual(tool->description, "Get current weather information");

  mcp_tool_free(tool);
}

TEST_F(MCPCApiBuildersTest, ToolWithSchema) {
  // Create schema
  auto* schema = mcp_json_create_object();
  ASSERT_NE(schema, nullptr);

  auto* tool = mcp_tool_with_schema("database_query", schema);

  ASSERT_NE(tool, nullptr);
  ExpectStringEqual(tool->name, "database_query");
  EXPECT_EQ(tool->input_schema, schema);

  mcp_json_free(schema);
  mcp_tool_free(tool);
}

TEST_F(MCPCApiBuildersTest, ToolComplete) {
  auto* schema = mcp_json_create_object();

  auto* tool = mcp_tool_complete("translator",
                                 "Translate text between languages", schema);

  ASSERT_NE(tool, nullptr);
  ExpectStringEqual(tool->name, "translator");
  ExpectStringEqual(tool->description, "Translate text between languages");
  EXPECT_EQ(tool->input_schema, schema);

  mcp_json_free(schema);
  mcp_tool_free(tool);
}

// ==================== PROMPT BUILDERS ====================

TEST_F(MCPCApiBuildersTest, PromptBuilder) {
  auto* prompt = mcp_prompt_create("greeting");

  ASSERT_NE(prompt, nullptr);
  ExpectStringEqual(prompt->name, "greeting");
  EXPECT_EQ(prompt->description, nullptr);
  EXPECT_EQ(prompt->arguments, nullptr);
  EXPECT_EQ(prompt->argument_count, 0);

  EXPECT_TRUE(mcp_prompt_is_valid(prompt));

  mcp_prompt_free(prompt);
}

TEST_F(MCPCApiBuildersTest, PromptWithDescription) {
  auto* prompt = mcp_prompt_with_description("code_review",
                                             "Review code for best practices");

  ASSERT_NE(prompt, nullptr);
  ExpectStringEqual(prompt->name, "code_review");
  ExpectStringEqual(prompt->description, "Review code for best practices");

  mcp_prompt_free(prompt);
}

TEST_F(MCPCApiBuildersTest, PromptWithArguments) {
  mcp_prompt_argument_t args[] = {
      {.name = const_cast<char*>("language"),
       .description = const_cast<char*>("Programming language"),
       .required = true},
      {.name = const_cast<char*>("style"),
       .description = const_cast<char*>("Code style guide"),
       .required = false}};

  auto* prompt = mcp_prompt_with_arguments("formatter", args, 2);

  ASSERT_NE(prompt, nullptr);
  ExpectStringEqual(prompt->name, "formatter");
  EXPECT_EQ(prompt->argument_count, 2);

  // Verify arguments were copied
  ExpectStringEqual(prompt->arguments[0].name, "language");
  ExpectStringEqual(prompt->arguments[0].description, "Programming language");
  EXPECT_TRUE(prompt->arguments[0].required);

  ExpectStringEqual(prompt->arguments[1].name, "style");
  EXPECT_FALSE(prompt->arguments[1].required);

  mcp_prompt_free(prompt);
}

// ==================== MESSAGE BUILDERS ====================

TEST_F(MCPCApiBuildersTest, MessageBuilder) {
  auto* content = mcp_text_content_create("Test message");
  auto* message = mcp_message_create(MCP_ROLE_USER, content);

  ASSERT_NE(message, nullptr);
  EXPECT_EQ(message->role, MCP_ROLE_USER);
  EXPECT_EQ(message->content, content);

  EXPECT_TRUE(mcp_message_is_valid(message));

  mcp_message_free(message);
}

TEST_F(MCPCApiBuildersTest, UserMessage) {
  auto* message = mcp_user_message("Hello from user");

  ASSERT_NE(message, nullptr);
  EXPECT_EQ(message->role, MCP_ROLE_USER);
  ASSERT_NE(message->content, nullptr);
  EXPECT_EQ(message->content->type, MCP_CONTENT_TEXT);
  ExpectStringEqual(message->content->text.text, "Hello from user");

  mcp_message_free(message);
}

TEST_F(MCPCApiBuildersTest, AssistantMessage) {
  auto* message = mcp_assistant_message("Response from assistant");

  ASSERT_NE(message, nullptr);
  EXPECT_EQ(message->role, MCP_ROLE_ASSISTANT);
  ASSERT_NE(message->content, nullptr);
  ExpectStringEqual(message->content->text.text, "Response from assistant");

  mcp_message_free(message);
}

// ==================== ERROR BUILDERS ====================

TEST_F(MCPCApiBuildersTest, ErrorBuilder) {
  auto* error = mcp_error_create(-32600, "Invalid Request");

  ASSERT_NE(error, nullptr);
  EXPECT_EQ(error->code, -32600);
  ExpectStringEqual(error->message, "Invalid Request");
  EXPECT_EQ(error->data, nullptr);

  mcp_error_free(error);
}

TEST_F(MCPCApiBuildersTest, ErrorWithData) {
  auto* data = mcp_json_create_string("Missing required parameter: id");
  auto* error = mcp_error_with_data(-32602, "Invalid params", data);

  ASSERT_NE(error, nullptr);
  EXPECT_EQ(error->code, -32602);
  ExpectStringEqual(error->message, "Invalid params");
  EXPECT_EQ(error->data, data);

  mcp_json_free(data);
  mcp_error_free(error);
}

// ==================== SCHEMA BUILDERS ====================

TEST_F(MCPCApiBuildersTest, StringSchemaBuilder) {
  auto* schema = mcp_string_schema_create();

  ASSERT_NE(schema, nullptr);
  EXPECT_EQ(schema->description, nullptr);
  EXPECT_EQ(schema->pattern, nullptr);
  EXPECT_FALSE(schema->min_length_set);
  EXPECT_FALSE(schema->max_length_set);

  mcp_string_schema_free(schema);
}

TEST_F(MCPCApiBuildersTest, StringSchemaWithConstraints) {
  auto* schema = mcp_string_schema_with_constraints(
      "Email address", "^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$", 5,
      254);

  ASSERT_NE(schema, nullptr);
  ExpectStringEqual(schema->description, "Email address");
  ExpectStringEqual(schema->pattern,
                    "^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$");
  EXPECT_TRUE(schema->min_length_set);
  EXPECT_EQ(schema->min_length, 5);
  EXPECT_TRUE(schema->max_length_set);
  EXPECT_EQ(schema->max_length, 254);

  mcp_string_schema_free(schema);
}

TEST_F(MCPCApiBuildersTest, NumberSchemaBuilder) {
  auto* schema = mcp_number_schema_create();

  ASSERT_NE(schema, nullptr);
  EXPECT_FALSE(schema->minimum_set);
  EXPECT_FALSE(schema->maximum_set);
  EXPECT_FALSE(schema->multiple_of_set);

  mcp_number_schema_free(schema);
}

TEST_F(MCPCApiBuildersTest, NumberSchemaWithConstraints) {
  auto* schema =
      mcp_number_schema_with_constraints("Temperature in Celsius",
                                         -273.15,  // Absolute zero
                                         1000.0,   // Reasonable upper limit
                                         0.01      // Precision
      );

  ASSERT_NE(schema, nullptr);
  ExpectStringEqual(schema->description, "Temperature in Celsius");
  EXPECT_TRUE(schema->minimum_set);
  EXPECT_DOUBLE_EQ(schema->minimum, -273.15);
  EXPECT_TRUE(schema->maximum_set);
  EXPECT_DOUBLE_EQ(schema->maximum, 1000.0);
  EXPECT_TRUE(schema->multiple_of_set);
  EXPECT_DOUBLE_EQ(schema->multiple_of, 0.01);

  mcp_number_schema_free(schema);
}

TEST_F(MCPCApiBuildersTest, BooleanSchemaBuilder) {
  auto* schema = mcp_boolean_schema_create();

  ASSERT_NE(schema, nullptr);
  EXPECT_EQ(schema->description, nullptr);

  mcp_boolean_schema_free(schema);
}

TEST_F(MCPCApiBuildersTest, BooleanSchemaWithDescription) {
  auto* schema = mcp_boolean_schema_with_description("Enable feature");

  ASSERT_NE(schema, nullptr);
  ExpectStringEqual(schema->description, "Enable feature");

  mcp_boolean_schema_free(schema);
}

TEST_F(MCPCApiBuildersTest, EnumSchemaBuilder) {
  const char* values[] = {"red", "green", "blue", "yellow"};
  auto* schema = mcp_enum_schema_create(values, 4);

  ASSERT_NE(schema, nullptr);
  EXPECT_EQ(schema->value_count, 4);
  ExpectStringEqual(schema->values[0], "red");
  ExpectStringEqual(schema->values[1], "green");
  ExpectStringEqual(schema->values[2], "blue");
  ExpectStringEqual(schema->values[3], "yellow");

  mcp_enum_schema_free(schema);
}

TEST_F(MCPCApiBuildersTest, EnumSchemaWithDescription) {
  const char* priorities[] = {"low", "medium", "high", "critical"};
  auto* schema =
      mcp_enum_schema_with_description(priorities, 4, "Task priority level");

  ASSERT_NE(schema, nullptr);
  ExpectStringEqual(schema->description, "Task priority level");
  EXPECT_EQ(schema->value_count, 4);

  mcp_enum_schema_free(schema);
}

// ==================== REQUEST/RESPONSE BUILDERS ====================

TEST_F(MCPCApiBuildersTest, RequestBuilder) {
  mcp_request_id_t id = mcp_request_id_int(1);
  auto* request = mcp_request_create(id, "test/method");

  ASSERT_NE(request, nullptr);
  ExpectStringEqual(request->jsonrpc, "2.0");
  ExpectStringEqual(request->method, "test/method");
  EXPECT_EQ(request->id.type, MCP_REQUEST_ID_INT);
  EXPECT_EQ(request->id.int_value, 1);
  EXPECT_EQ(request->params, nullptr);

  mcp_request_free(request);
}

TEST_F(MCPCApiBuildersTest, RequestWithParams) {
  mcp_request_id_t id = mcp_request_id_string("req-123");
  auto* params = mcp_json_create_object();

  auto* request = mcp_request_with_params(id, "tools/call", params);

  ASSERT_NE(request, nullptr);
  ExpectStringEqual(request->method, "tools/call");
  EXPECT_EQ(request->params, params);

  mcp_json_free(params);
  mcp_request_free(request);
}

TEST_F(MCPCApiBuildersTest, ResponseSuccess) {
  mcp_request_id_t id = mcp_request_id_int(42);
  auto* result = mcp_json_create_string("Success");

  auto* response = mcp_response_success(id, result);

  ASSERT_NE(response, nullptr);
  ExpectStringEqual(response->jsonrpc, "2.0");
  EXPECT_EQ(response->result, result);
  EXPECT_EQ(response->error, nullptr);

  mcp_json_free(result);
  mcp_response_free(response);
}

TEST_F(MCPCApiBuildersTest, ResponseError) {
  mcp_request_id_t id = mcp_request_id_int(99);
  mcp_error_t error = {.code = -32601,
                       .message = const_cast<char*>("Method not found"),
                       .data = nullptr};

  auto* response = mcp_response_error(id, &error);

  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->result, nullptr);
  ASSERT_NE(response->error, nullptr);
  EXPECT_EQ(response->error->code, -32601);
  ExpectStringEqual(response->error->message, "Method not found");

  mcp_response_free(response);
}

TEST_F(MCPCApiBuildersTest, NotificationBuilder) {
  auto* notification = mcp_notification_create("test/event");

  ASSERT_NE(notification, nullptr);
  ExpectStringEqual(notification->jsonrpc, "2.0");
  ExpectStringEqual(notification->method, "test/event");
  EXPECT_EQ(notification->params, nullptr);

  mcp_notification_free(notification);
}

TEST_F(MCPCApiBuildersTest, NotificationWithParams) {
  auto* params = mcp_json_create_object();
  auto* notification = mcp_notification_with_params("progress/update", params);

  ASSERT_NE(notification, nullptr);
  ExpectStringEqual(notification->method, "progress/update");
  EXPECT_EQ(notification->params, params);

  mcp_json_free(params);
  mcp_notification_free(notification);
}

// ==================== IMPLEMENTATION INFO BUILDER ====================

TEST_F(MCPCApiBuildersTest, ImplementationBuilder) {
  auto* impl = mcp_implementation_create("MCP C Client", "1.0.0");

  ASSERT_NE(impl, nullptr);
  ExpectStringEqual(impl->name, "MCP C Client");
  ExpectStringEqual(impl->version, "1.0.0");

  mcp_implementation_free(impl);
}

// ==================== ROOT BUILDER ====================

TEST_F(MCPCApiBuildersTest, RootBuilder) {
  auto* root = mcp_root_create("file:///home/user", "Home Directory");

  ASSERT_NE(root, nullptr);
  ExpectStringEqual(root->uri, "file:///home/user");
  ExpectStringEqual(root->name, "Home Directory");

  mcp_root_free(root);
}

TEST_F(MCPCApiBuildersTest, RootBuilderWithoutName) {
  auto* root = mcp_root_create("https://api.example.com", nullptr);

  ASSERT_NE(root, nullptr);
  ExpectStringEqual(root->uri, "https://api.example.com");
  EXPECT_EQ(root->name, nullptr);

  mcp_root_free(root);
}

// ==================== MODEL PREFERENCES BUILDER ====================

TEST_F(MCPCApiBuildersTest, ModelHintBuilder) {
  auto* hint = mcp_model_hint_create("gpt-4-turbo");

  ASSERT_NE(hint, nullptr);
  ExpectStringEqual(hint->name, "gpt-4-turbo");

  mcp_model_hint_free(hint);
}

TEST_F(MCPCApiBuildersTest, ModelPreferencesBuilder) {
  auto* prefs = mcp_model_preferences_create();

  ASSERT_NE(prefs, nullptr);
  EXPECT_EQ(prefs->hints, nullptr);
  EXPECT_EQ(prefs->hint_count, 0);
  EXPECT_FALSE(prefs->cost_priority_set);
  EXPECT_FALSE(prefs->speed_priority_set);
  EXPECT_FALSE(prefs->intelligence_priority_set);

  mcp_model_preferences_free(prefs);
}

TEST_F(MCPCApiBuildersTest, ModelPreferencesWithPriorities) {
  auto* prefs = mcp_model_preferences_with_priorities(0.3, 0.5, 0.9);

  ASSERT_NE(prefs, nullptr);
  EXPECT_TRUE(prefs->cost_priority_set);
  EXPECT_DOUBLE_EQ(prefs->cost_priority, 0.3);
  EXPECT_TRUE(prefs->speed_priority_set);
  EXPECT_DOUBLE_EQ(prefs->speed_priority, 0.5);
  EXPECT_TRUE(prefs->intelligence_priority_set);
  EXPECT_DOUBLE_EQ(prefs->intelligence_priority, 0.9);

  mcp_model_preferences_free(prefs);
}

TEST_F(MCPCApiBuildersTest, ModelPreferencesWithInvalidPriorities) {
  // Test with out-of-range priorities (should be ignored)
  auto* prefs = mcp_model_preferences_with_priorities(-0.5, 1.5, 0.7);

  ASSERT_NE(prefs, nullptr);
  EXPECT_FALSE(prefs->cost_priority_set);   // -0.5 is invalid
  EXPECT_FALSE(prefs->speed_priority_set);  // 1.5 is invalid
  EXPECT_TRUE(prefs->intelligence_priority_set);
  EXPECT_DOUBLE_EQ(prefs->intelligence_priority, 0.7);

  mcp_model_preferences_free(prefs);
}

// ==================== COMPLEX BUILDER COMBINATIONS ====================

TEST_F(MCPCApiBuildersTest, ComplexNestedStructures) {
  // Build a complex embedded resource with multiple content types
  mcp_resource_t resource = {
      .uri = const_cast<char*>("file:///report.pdf"),
      .name = const_cast<char*>("Annual Report"),
      .description = const_cast<char*>("2024 Annual Report"),
      .mime_type = const_cast<char*>("application/pdf")};

  // Create various content blocks
  std::vector<mcp_content_block_t*> contents;
  contents.push_back(mcp_text_content_create("Executive Summary"));
  contents.push_back(mcp_text_content_create("Financial Overview"));
  contents.push_back(mcp_image_content_create("chart1", "image/png"));
  contents.push_back(mcp_audio_content_create("narration", "audio/mp3"));

  auto* embedded =
      mcp_embedded_resource_create(&resource, contents.data(), contents.size());

  ASSERT_NE(embedded, nullptr);
  EXPECT_EQ(embedded->embedded.content_count, 4);

  // Verify each content type
  EXPECT_EQ(embedded->embedded.content[0].type, MCP_CONTENT_TEXT);
  EXPECT_EQ(embedded->embedded.content[1].type, MCP_CONTENT_TEXT);
  EXPECT_EQ(embedded->embedded.content[2].type, MCP_CONTENT_IMAGE);
  EXPECT_EQ(embedded->embedded.content[3].type, MCP_CONTENT_AUDIO);

  // Clean up
  for (auto* content : contents) {
    mcp_content_block_free(content);
  }
  mcp_content_block_free(embedded);
}

TEST_F(MCPCApiBuildersTest, BuilderMemoryStressTest) {
  // Test creating and destroying many builders rapidly
  const int iterations = 1000;

  for (int i = 0; i < iterations; ++i) {
    auto* text = mcp_text_content_create("Test");
    auto* image = mcp_image_content_create("data", "image/png");
    auto* tool = mcp_tool_create("tool");
    auto* prompt = mcp_prompt_create("prompt");
    auto* resource = mcp_resource_create("uri", "name");
    auto* message = mcp_user_message("msg");
    auto* error = mcp_error_create(i, "error");

    // Verify at least one
    if (i == iterations / 2) {
      EXPECT_NE(text, nullptr);
      EXPECT_NE(tool, nullptr);
    }

    mcp_content_block_free(text);
    mcp_content_block_free(image);
    mcp_tool_free(tool);
    mcp_prompt_free(prompt);
    mcp_resource_free(resource);
    mcp_message_free(message);
    mcp_error_free(error);
  }
}

// Main test runner
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}