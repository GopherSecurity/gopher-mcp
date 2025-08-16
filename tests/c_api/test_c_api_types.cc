/**
 * @file test_c_api_types.cc
 * @brief Unit tests for MCP C API type bindings
 *
 * Tests type construction, JSON serialization/deserialization,
 * builder patterns, and utility functions.
 */

#include <cstring>

#include <gtest/gtest.h>

#include "mcp/c_api/mcp_c_types.h"

class MCPCApiTypesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Any setup needed
  }

  void TearDown() override {
    // Cleanup
  }
};

// Test content block creation and manipulation
TEST_F(MCPCApiTypesTest, TextContentBlock) {
  // Create text content
  mcp_content_block_t* block = mcp_text_content_create("Hello, World!");
  ASSERT_NE(block, nullptr);
  EXPECT_EQ(block->type, MCP_CONTENT_TEXT);
  EXPECT_STREQ(block->content.text->text.data, "Hello, World!");

  // Validate
  EXPECT_TRUE(mcp_content_block_is_text(block));
  EXPECT_FALSE(mcp_content_block_is_image(block));

  // Test JSON serialization
  mcp_json_value_t json = mcp_content_block_to_json(block);
  ASSERT_NE(json, nullptr);

  char* json_str = mcp_json_stringify(json);
  ASSERT_NE(json_str, nullptr);
  EXPECT_NE(strstr(json_str, "\"type\":\"text\""), nullptr);
  EXPECT_NE(strstr(json_str, "\"text\":\"Hello, World!\""), nullptr);

  // Test JSON deserialization
  mcp_content_block_t* deserialized = mcp_content_block_from_json(json);
  ASSERT_NE(deserialized, nullptr);
  EXPECT_EQ(deserialized->type, MCP_CONTENT_TEXT);
  EXPECT_STREQ(deserialized->content.text->text.data, "Hello, World!");

  // Clean up
  mcp_string_free(json_str);
  mcp_json_free(json);
  mcp_content_block_free(block);
  mcp_content_block_free(deserialized);
}

TEST_F(MCPCApiTypesTest, TextContentWithAnnotations) {
  // Create text content with annotations
  mcp_role_t audience[] = {MCP_ROLE_USER, MCP_ROLE_ASSISTANT};
  mcp_content_block_t* block =
      mcp_text_content_with_annotations("Annotated text", audience, 2, 0.8);

  ASSERT_NE(block, nullptr);
  // TODO: Annotations handling needs to be redesigned
  // The content block structure doesn't have annotations field

  // Test deep copy
  mcp_content_block_t* copy = mcp_content_block_copy(block);
  ASSERT_NE(copy, nullptr);
  EXPECT_STREQ(copy->content.text->text.data, "Annotated text");
  // TODO: Verify annotations copy

  // Clean up
  mcp_content_block_free(block);
  mcp_content_block_free(copy);
}

TEST_F(MCPCApiTypesTest, ImageContentBlock) {
  // Create image content
  mcp_content_block_t* block =
      mcp_image_content_create("base64encodeddata", "image/png");

  ASSERT_NE(block, nullptr);
  EXPECT_EQ(block->type, MCP_CONTENT_IMAGE);
  EXPECT_STREQ(block->content.image->data.data, "base64encodeddata");
  EXPECT_STREQ(block->content.image->mime_type.data, "image/png");

  // Validate
  EXPECT_TRUE(mcp_content_block_is_image(block));
  EXPECT_FALSE(mcp_content_block_is_text(block));

  // Test JSON serialization
  mcp_json_value_t json = mcp_content_block_to_json(block);
  ASSERT_NE(json, nullptr);

  // Test JSON deserialization
  mcp_content_block_t* deserialized = mcp_content_block_from_json(json);
  ASSERT_NE(deserialized, nullptr);
  EXPECT_EQ(deserialized->type, MCP_CONTENT_IMAGE);
  EXPECT_STREQ(deserialized->image.data, "base64encodeddata");
  EXPECT_STREQ(deserialized->image.mime_type, "image/png");

  // Clean up
  mcp_json_free(json);
  mcp_content_block_free(block);
  mcp_content_block_free(deserialized);
}

TEST_F(MCPCApiTypesTest, AudioContentBlock) {
  // Create audio content
  mcp_content_block_t* block =
      mcp_audio_content_create("audiodata", "audio/mp3");

  ASSERT_NE(block, nullptr);
  EXPECT_EQ(block->type, MCP_CONTENT_AUDIO);
  EXPECT_TRUE(mcp_content_block_is_audio(block));

  // Clean up
  mcp_content_block_free(block);
}

TEST_F(MCPCApiTypesTest, ResourceContentBlock) {
  // Create resource
  mcp_resource_t* resource = mcp_resource_with_details(
      "file:///path/to/file.txt", "file.txt", "A text file", "text/plain");

  ASSERT_NE(resource, nullptr);
  EXPECT_TRUE(mcp_resource_is_valid(resource));

  // Create resource content
  mcp_content_block_t* block = mcp_resource_content_create(resource);
  ASSERT_NE(block, nullptr);
  EXPECT_EQ(block->type, MCP_CONTENT_RESOURCE);
  EXPECT_TRUE(mcp_content_is_resource(block));

  // Test JSON serialization
  mcp_json_value_t json = mcp_resource_to_json(resource);
  ASSERT_NE(json, nullptr);

  // Test JSON deserialization
  mcp_resource_t* deserialized = mcp_resource_from_json(json);
  ASSERT_NE(deserialized, nullptr);
  EXPECT_STREQ(deserialized->uri, "file:///path/to/file.txt");
  EXPECT_STREQ(deserialized->name, "file.txt");

  // Clean up
  mcp_json_free(json);
  mcp_resource_free(resource);
  mcp_resource_free(deserialized);
  mcp_content_block_free(block);
}

TEST_F(MCPCApiTypesTest, EmbeddedResourceContent) {
  // Create resource
  mcp_resource_t resource = {.uri = const_cast<char*>("file:///doc.md"),
                             .name = const_cast<char*>("doc.md"),
                             .description = const_cast<char*>("Documentation"),
                             .mime_type = const_cast<char*>("text/markdown")};

  // Create nested content
  mcp_content_block_t* text = mcp_text_content_create("Embedded text");
  ASSERT_NE(text, nullptr);

  // Create embedded resource
  mcp_content_block_t* block = mcp_embedded_resource_create(&resource, text, 1);

  ASSERT_NE(block, nullptr);
  EXPECT_EQ(block->type, MCP_CONTENT_EMBEDDED);
  EXPECT_TRUE(mcp_content_is_embedded(block));
  EXPECT_EQ(block->embedded.content_count, 1);

  // Clean up (text is copied, so we need to free both)
  mcp_content_block_free(text);
  mcp_content_block_free(block);
}

TEST_F(MCPCApiTypesTest, ToolCreation) {
  // Create tool with schema
  mcp_json_value_t schema = mcp_json_create_object();
  ASSERT_NE(schema, nullptr);

  mcp_tool_t* tool =
      mcp_tool_complete("calculator", "A simple calculator tool", schema);

  ASSERT_NE(tool, nullptr);
  EXPECT_STREQ(tool->name, "calculator");
  EXPECT_STREQ(tool->description, "A simple calculator tool");
  EXPECT_TRUE(mcp_tool_is_valid(tool));

  // Test JSON serialization
  mcp_json_value_t json = mcp_tool_to_json(tool);
  ASSERT_NE(json, nullptr);

  // Test JSON deserialization
  mcp_tool_t* deserialized = mcp_tool_from_json(json);
  ASSERT_NE(deserialized, nullptr);
  EXPECT_STREQ(deserialized->name, "calculator");

  // Clean up
  mcp_json_free(json);
  mcp_json_free(schema);
  mcp_tool_free(tool);
  mcp_tool_free(deserialized);
}

TEST_F(MCPCApiTypesTest, PromptCreation) {
  // Create prompt arguments
  mcp_prompt_argument_t args[] = {
      {.name = const_cast<char*>("input"),
       .description = const_cast<char*>("Input text"),
       .required = true},
      {.name = const_cast<char*>("format"),
       .description = const_cast<char*>("Output format"),
       .required = false}};

  mcp_prompt_t* prompt = mcp_prompt_with_arguments("text_processor", args, 2);

  ASSERT_NE(prompt, nullptr);
  EXPECT_STREQ(prompt->name, "text_processor");
  EXPECT_EQ(prompt->argument_count, 2);
  EXPECT_TRUE(mcp_prompt_is_valid(prompt));

  // Test deep copy
  mcp_prompt_t* copy = mcp_prompt_copy(prompt);
  ASSERT_NE(copy, nullptr);
  EXPECT_STREQ(copy->name, "text_processor");
  EXPECT_EQ(copy->argument_count, 2);

  // Test JSON serialization
  mcp_json_value_t json = mcp_prompt_to_json(prompt);
  ASSERT_NE(json, nullptr);

  // Test JSON deserialization
  mcp_prompt_t* deserialized = mcp_prompt_from_json(json);
  ASSERT_NE(deserialized, nullptr);
  EXPECT_STREQ(deserialized->name, "text_processor");
  EXPECT_EQ(deserialized->argument_count, 2);

  // Clean up
  mcp_json_free(json);
  mcp_prompt_free(prompt);
  mcp_prompt_free(copy);
  mcp_prompt_free(deserialized);
}

TEST_F(MCPCApiTypesTest, MessageCreation) {
  // Create user message
  mcp_message_t* user_msg = mcp_user_message("Hello from user");
  ASSERT_NE(user_msg, nullptr);
  EXPECT_EQ(user_msg->role, MCP_ROLE_USER);
  EXPECT_TRUE(mcp_message_is_valid(user_msg));

  // Create assistant message
  mcp_message_t* assistant_msg = mcp_assistant_message("Hello from assistant");
  ASSERT_NE(assistant_msg, nullptr);
  EXPECT_EQ(assistant_msg->role, MCP_ROLE_ASSISTANT);

  // Test JSON serialization
  mcp_json_value_t json = mcp_message_to_json(user_msg);
  ASSERT_NE(json, nullptr);

  // Test JSON deserialization
  mcp_message_t* deserialized = mcp_message_from_json(json);
  ASSERT_NE(deserialized, nullptr);
  EXPECT_EQ(deserialized->role, MCP_ROLE_USER);

  // Clean up
  mcp_json_free(json);
  mcp_message_free(user_msg);
  mcp_message_free(assistant_msg);
  mcp_message_free(deserialized);
}

TEST_F(MCPCApiTypesTest, ErrorCreation) {
  // Create error with data
  mcp_json_value_t data = mcp_json_create_string("Additional error info");
  mcp_error_t* error = mcp_error_with_data(-32600, "Invalid Request", data);

  ASSERT_NE(error, nullptr);
  EXPECT_EQ(error->code, -32600);
  EXPECT_STREQ(error->message, "Invalid Request");
  EXPECT_NE(error->data, nullptr);

  // Test JSON serialization
  mcp_json_value_t json = mcp_error_to_json(error);
  ASSERT_NE(json, nullptr);

  // Test JSON deserialization
  mcp_error_t* deserialized = mcp_error_from_json(json);
  ASSERT_NE(deserialized, nullptr);
  EXPECT_EQ(deserialized->code, -32600);
  EXPECT_STREQ(deserialized->message, "Invalid Request");

  // Clean up
  mcp_json_free(json);
  mcp_json_free(data);
  mcp_error_free(error);
  mcp_error_free(deserialized);
}

TEST_F(MCPCApiTypesTest, SchemaCreation) {
  // String schema
  mcp_string_schema_t* str_schema = mcp_string_schema_with_constraints(
      "Email address", "^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$", 5,
      100);

  ASSERT_NE(str_schema, nullptr);
  EXPECT_STREQ(str_schema->description, "Email address");
  EXPECT_NE(str_schema->pattern, nullptr);
  EXPECT_TRUE(str_schema->min_length_set);
  EXPECT_EQ(str_schema->min_length, 5);

  // Number schema
  mcp_number_schema_t* num_schema =
      mcp_number_schema_with_constraints("Temperature", -273.15, 1000.0, 0.1);

  ASSERT_NE(num_schema, nullptr);
  EXPECT_TRUE(num_schema->minimum_set);
  EXPECT_EQ(num_schema->minimum, -273.15);

  // Boolean schema
  mcp_boolean_schema_t* bool_schema =
      mcp_boolean_schema_with_description("Enable feature");

  ASSERT_NE(bool_schema, nullptr);
  EXPECT_STREQ(bool_schema->description, "Enable feature");

  // Enum schema
  const char* values[] = {"red", "green", "blue"};
  mcp_enum_schema_t* enum_schema =
      mcp_enum_schema_with_description(values, 3, "Color selection");

  ASSERT_NE(enum_schema, nullptr);
  EXPECT_EQ(enum_schema->value_count, 3);
  EXPECT_STREQ(enum_schema->values[0], "red");

  // Test JSON serialization
  mcp_json_value_t str_json = mcp_string_schema_to_json(str_schema);
  mcp_json_value_t num_json = mcp_number_schema_to_json(num_schema);
  mcp_json_value_t bool_json = mcp_boolean_schema_to_json(bool_schema);
  mcp_json_value_t enum_json = mcp_enum_schema_to_json(enum_schema);

  EXPECT_NE(str_json, nullptr);
  EXPECT_NE(num_json, nullptr);
  EXPECT_NE(bool_json, nullptr);
  EXPECT_NE(enum_json, nullptr);

  // Clean up
  mcp_json_free(str_json);
  mcp_json_free(num_json);
  mcp_json_free(bool_json);
  mcp_json_free(enum_json);
  mcp_string_schema_free(str_schema);
  mcp_number_schema_free(num_schema);
  mcp_boolean_schema_free(bool_schema);
  mcp_enum_schema_free(enum_schema);
}

TEST_F(MCPCApiTypesTest, RequestIdHandling) {
  // String request ID
  mcp_request_id_t str_id = mcp_request_id_string("req-123");
  EXPECT_EQ(str_id.type, MCP_REQUEST_ID_STRING);
  EXPECT_STREQ(str_id.string_value, "req-123");

  // Integer request ID
  mcp_request_id_t int_id = mcp_request_id_int(42);
  EXPECT_EQ(int_id.type, MCP_REQUEST_ID_INT);
  EXPECT_EQ(int_id.int_value, 42);

  // Test equality
  mcp_request_id_t str_id2 = mcp_request_id_string("req-123");
  EXPECT_TRUE(mcp_request_id_equals(&str_id, &str_id2));
  EXPECT_FALSE(mcp_request_id_equals(&str_id, &int_id));

  mcp_request_id_t int_id2 = mcp_request_id_int(42);
  EXPECT_TRUE(mcp_request_id_equals(&int_id, &int_id2));

  // Clean up
  mcp_request_id_free(&str_id);
  mcp_request_id_free(&str_id2);
}

TEST_F(MCPCApiTypesTest, RequestResponseCreation) {
  // Create request
  mcp_request_id_t id = mcp_request_id_int(1);
  mcp_json_value_t params = mcp_json_create_object();
  mcp_request_t* request = mcp_request_with_params(id, "test/method", params);

  ASSERT_NE(request, nullptr);
  EXPECT_STREQ(request->jsonrpc, "2.0");
  EXPECT_STREQ(request->method, "test/method");

  // Create success response
  mcp_json_value_t result = mcp_json_create_string("success");
  mcp_response_t* response = mcp_response_success(id, result);

  ASSERT_NE(response, nullptr);
  EXPECT_STREQ(response->jsonrpc, "2.0");
  EXPECT_NE(response->result, nullptr);
  EXPECT_EQ(response->error, nullptr);

  // Create error response
  mcp_error_t error = {.code = -32601,
                       .message = const_cast<char*>("Method not found"),
                       .data = nullptr};
  mcp_response_t* error_response = mcp_response_error(id, &error);

  ASSERT_NE(error_response, nullptr);
  EXPECT_EQ(error_response->result, nullptr);
  EXPECT_NE(error_response->error, nullptr);
  EXPECT_EQ(error_response->error->code, -32601);

  // Create notification
  mcp_notification_t* notification =
      mcp_notification_with_params("test/notification", params);

  ASSERT_NE(notification, nullptr);
  EXPECT_STREQ(notification->method, "test/notification");

  // Clean up
  mcp_json_free(params);
  mcp_json_free(result);
  mcp_request_free(request);
  mcp_response_free(response);
  mcp_response_free(error_response);
  mcp_notification_free(notification);
}

TEST_F(MCPCApiTypesTest, ContentBlockArray) {
  // Create array
  mcp_content_block_array_t* array = mcp_content_block_array_create(2);
  ASSERT_NE(array, nullptr);
  EXPECT_EQ(array->capacity, 2);
  EXPECT_EQ(array->count, 0);

  // Add blocks
  mcp_content_block_t* text1 = mcp_text_content_create("First");
  mcp_content_block_t* text2 = mcp_text_content_create("Second");
  mcp_content_block_t* text3 = mcp_text_content_create("Third");

  EXPECT_TRUE(mcp_content_block_array_append(array, text1));
  EXPECT_TRUE(mcp_content_block_array_append(array, text2));
  EXPECT_TRUE(mcp_content_block_array_append(array, text3));  // Should grow

  EXPECT_EQ(array->count, 3);
  EXPECT_GE(array->capacity, 3);

  // Clean up
  mcp_content_block_array_free(array);
}

TEST_F(MCPCApiTypesTest, MemoryPool) {
  // Create pool
  mcp_memory_pool_t pool = mcp_memory_pool_create();
  ASSERT_NE(pool, nullptr);

  // Allocate memory
  void* ptr1 = mcp_memory_pool_alloc(pool, 100);
  void* ptr2 = mcp_memory_pool_alloc(pool, 200);

  ASSERT_NE(ptr1, nullptr);
  ASSERT_NE(ptr2, nullptr);

  EXPECT_EQ(mcp_memory_pool_get_size(pool), 300);
  EXPECT_EQ(mcp_memory_pool_get_peak_size(pool), 300);

  // Free one allocation
  mcp_memory_pool_free(pool, ptr1, 100);
  EXPECT_EQ(mcp_memory_pool_get_size(pool), 200);
  EXPECT_EQ(mcp_memory_pool_get_peak_size(pool), 300);

  // Clean up
  mcp_memory_pool_destroy(pool);
}

TEST_F(MCPCApiTypesTest, StringUtilities) {
  // Test string duplication
  char* dup = mcp_string_duplicate("test string");
  ASSERT_NE(dup, nullptr);
  EXPECT_STREQ(dup, "test string");

  // Test string equality
  EXPECT_TRUE(mcp_string_equals("hello", "hello"));
  EXPECT_FALSE(mcp_string_equals("hello", "world"));
  EXPECT_TRUE(mcp_string_equals(nullptr, nullptr));

  // Test string prefix/suffix
  EXPECT_TRUE(mcp_string_starts_with("hello world", "hello"));
  EXPECT_FALSE(mcp_string_starts_with("hello world", "world"));
  EXPECT_TRUE(mcp_string_ends_with("hello world", "world"));
  EXPECT_FALSE(mcp_string_ends_with("hello world", "hello"));

  // Clean up
  mcp_string_free(dup);
}

TEST_F(MCPCApiTypesTest, LoggingLevelConversion) {
  // Test level to string
  EXPECT_STREQ(mcp_logging_level_to_string(MCP_LOGGING_DEBUG), "debug");
  EXPECT_STREQ(mcp_logging_level_to_string(MCP_LOGGING_ERROR), "error");
  EXPECT_STREQ(mcp_logging_level_to_string(MCP_LOGGING_EMERGENCY), "emergency");

  // Test string to level
  EXPECT_EQ(mcp_logging_level_from_string("debug"), MCP_LOGGING_DEBUG);
  EXPECT_EQ(mcp_logging_level_from_string("error"), MCP_LOGGING_ERROR);
  EXPECT_EQ(mcp_logging_level_from_string("invalid"), MCP_LOGGING_ERROR);
  EXPECT_EQ(mcp_logging_level_from_string(nullptr), MCP_LOGGING_ERROR);
}

TEST_F(MCPCApiTypesTest, JsonValueManipulation) {
  // Create various JSON values
  mcp_json_value_t obj = mcp_json_create_object();
  mcp_json_value_t arr = mcp_json_create_array();
  mcp_json_value_t str = mcp_json_create_string("test");
  mcp_json_value_t num = mcp_json_create_number(42.5);
  mcp_json_value_t bool_val = mcp_json_create_bool(true);
  mcp_json_value_t null_val = mcp_json_create_null();

  EXPECT_NE(obj, nullptr);
  EXPECT_NE(arr, nullptr);
  EXPECT_NE(str, nullptr);
  EXPECT_NE(num, nullptr);
  EXPECT_NE(bool_val, nullptr);
  EXPECT_NE(null_val, nullptr);

  // Parse JSON string
  const char* json_str = "{\"key\": \"value\", \"number\": 123}";
  mcp_json_value_t parsed = mcp_json_parse(json_str);
  ASSERT_NE(parsed, nullptr);

  // Stringify back
  char* stringified = mcp_json_stringify(parsed);
  ASSERT_NE(stringified, nullptr);
  EXPECT_NE(strstr(stringified, "\"key\""), nullptr);
  EXPECT_NE(strstr(stringified, "\"value\""), nullptr);

  // Clean up
  mcp_json_free(obj);
  mcp_json_free(arr);
  mcp_json_free(str);
  mcp_json_free(num);
  mcp_json_free(bool_val);
  mcp_json_free(null_val);
  mcp_json_free(parsed);
  mcp_string_free(stringified);
}

TEST_F(MCPCApiTypesTest, ModelPreferences) {
  // Create model preferences
  mcp_model_preferences_t* prefs =
      mcp_model_preferences_with_priorities(0.3, 0.5, 0.8);

  ASSERT_NE(prefs, nullptr);
  EXPECT_TRUE(prefs->cost_priority_set);
  EXPECT_EQ(prefs->cost_priority, 0.3);
  EXPECT_TRUE(prefs->speed_priority_set);
  EXPECT_EQ(prefs->speed_priority, 0.5);
  EXPECT_TRUE(prefs->intelligence_priority_set);
  EXPECT_EQ(prefs->intelligence_priority, 0.8);

  // Create model hint
  mcp_model_hint_t* hint = mcp_model_hint_create("gpt-4");
  ASSERT_NE(hint, nullptr);
  EXPECT_STREQ(hint->name, "gpt-4");

  // Clean up
  mcp_model_preferences_free(prefs);
  mcp_model_hint_free(hint);
}

TEST_F(MCPCApiTypesTest, RootCreation) {
  // Create root
  mcp_root_t* root = mcp_root_create("file:///home/user", "Home");

  ASSERT_NE(root, nullptr);
  EXPECT_STREQ(root->uri, "file:///home/user");
  EXPECT_STREQ(root->name, "Home");

  // Clean up
  mcp_root_free(root);
}

TEST_F(MCPCApiTypesTest, ImplementationInfo) {
  // Create implementation info
  mcp_implementation_t* impl =
      mcp_implementation_create("MCP C++ SDK", "1.0.0");

  ASSERT_NE(impl, nullptr);
  EXPECT_STREQ(impl->name, "MCP C++ SDK");
  EXPECT_STREQ(impl->version, "1.0.0");

  // Clean up
  mcp_implementation_free(impl);
}

// Main test runner
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}