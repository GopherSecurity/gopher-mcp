/**
 * @file test_c_api_type_helpers.cc
 * @brief Comprehensive unit tests for MCP C API type helpers and utilities
 *
 * Tests validation, type checking, deep copy, memory management,
 * and utility functions for production-ready quality.
 */

#include <chrono>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/c_api/mcp_c_types.h"

class MCPCApiTypeHelpersTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Common setup
  }

  void TearDown() override {
    // Cleanup
  }
};

// ==================== VALIDATION FUNCTIONS ====================

TEST_F(MCPCApiTypeHelpersTest, ContentBlockValidation) {
  // Valid text content
  auto* text = mcp_text_content_create("Valid text");
  EXPECT_TRUE(mcp_content_block_is_valid(text));
  mcp_content_block_free(text);

  // Invalid text content (null text)
  mcp_content_block_t invalid_text;
  invalid_text.type = MCP_CONTENT_TEXT;
  invalid_text.text.text = nullptr;
  EXPECT_FALSE(mcp_content_block_is_valid(&invalid_text));

  // Valid image content
  auto* image = mcp_image_content_create("data", "image/png");
  EXPECT_TRUE(mcp_content_block_is_valid(image));
  mcp_content_block_free(image);

  // Invalid image content (missing mime type)
  mcp_content_block_t invalid_image;
  invalid_image.type = MCP_CONTENT_IMAGE;
  invalid_image.image.data = const_cast<char*>("data");
  invalid_image.image.mime_type = nullptr;
  EXPECT_FALSE(mcp_content_block_is_valid(&invalid_image));

  // Valid audio content
  auto* audio = mcp_audio_content_create("audiodata", "audio/mp3");
  EXPECT_TRUE(mcp_content_block_is_valid(audio));
  mcp_content_block_free(audio);

  // Valid resource content
  mcp_resource_t resource = {.uri = const_cast<char*>("file:///test.txt"),
                             .name = const_cast<char*>("test.txt"),
                             .description = nullptr,
                             .mime_type = nullptr};
  auto* res_content = mcp_resource_content_create(&resource);
  EXPECT_TRUE(mcp_content_block_is_valid(res_content));
  mcp_content_block_free(res_content);

  // Invalid resource content (missing uri)
  mcp_content_block_t invalid_resource;
  invalid_resource.type = MCP_CONTENT_RESOURCE;
  invalid_resource.resource.resource.uri = nullptr;
  invalid_resource.resource.resource.name = const_cast<char*>("name");
  EXPECT_FALSE(mcp_content_block_is_valid(&invalid_resource));

  // Null pointer
  EXPECT_FALSE(mcp_content_block_is_valid(nullptr));
}

TEST_F(MCPCApiTypeHelpersTest, ToolValidation) {
  // Valid tool
  auto* tool = mcp_tool_create("calculator");
  EXPECT_TRUE(mcp_tool_is_valid(tool));
  mcp_tool_free(tool);

  // Invalid tool (no name)
  mcp_tool_t invalid_tool;
  invalid_tool.name = nullptr;
  invalid_tool.description = const_cast<char*>("Description");
  EXPECT_FALSE(mcp_tool_is_valid(&invalid_tool));

  // Null pointer
  EXPECT_FALSE(mcp_tool_is_valid(nullptr));
}

TEST_F(MCPCApiTypeHelpersTest, PromptValidation) {
  // Valid prompt
  auto* prompt = mcp_prompt_create("greeting");
  EXPECT_TRUE(mcp_prompt_is_valid(prompt));
  mcp_prompt_free(prompt);

  // Invalid prompt (no name)
  mcp_prompt_t invalid_prompt;
  invalid_prompt.name = nullptr;
  EXPECT_FALSE(mcp_prompt_is_valid(&invalid_prompt));

  // Null pointer
  EXPECT_FALSE(mcp_prompt_is_valid(nullptr));
}

TEST_F(MCPCApiTypeHelpersTest, ResourceValidation) {
  // Valid resource
  auto* resource = mcp_resource_create("https://example.com", "Example");
  EXPECT_TRUE(mcp_resource_is_valid(resource));
  mcp_resource_free(resource);

  // Invalid resource (missing uri)
  mcp_resource_t invalid1;
  invalid1.uri = nullptr;
  invalid1.name = const_cast<char*>("Name");
  EXPECT_FALSE(mcp_resource_is_valid(&invalid1));

  // Invalid resource (missing name)
  mcp_resource_t invalid2;
  invalid2.uri = const_cast<char*>("uri");
  invalid2.name = nullptr;
  EXPECT_FALSE(mcp_resource_is_valid(&invalid2));

  // Null pointer
  EXPECT_FALSE(mcp_resource_is_valid(nullptr));
}

TEST_F(MCPCApiTypeHelpersTest, MessageValidation) {
  // Valid message
  auto* message = mcp_user_message("Hello");
  EXPECT_TRUE(mcp_message_is_valid(message));
  mcp_message_free(message);

  // Invalid message (null content)
  mcp_message_t invalid_msg;
  invalid_msg.role = MCP_ROLE_USER;
  invalid_msg.content = nullptr;
  EXPECT_FALSE(mcp_message_is_valid(&invalid_msg));

  // Invalid message (invalid content)
  mcp_content_block_t invalid_content;
  invalid_content.type = MCP_CONTENT_TEXT;
  invalid_content.text.text = nullptr;

  mcp_message_t msg_with_invalid_content;
  msg_with_invalid_content.role = MCP_ROLE_ASSISTANT;
  msg_with_invalid_content.content = &invalid_content;
  EXPECT_FALSE(mcp_message_is_valid(&msg_with_invalid_content));

  // Null pointer
  EXPECT_FALSE(mcp_message_is_valid(nullptr));
}

// ==================== TYPE CHECKING FUNCTIONS ====================

TEST_F(MCPCApiTypeHelpersTest, ContentTypeChecking) {
  // Text content
  auto* text = mcp_text_content_create("Text");
  EXPECT_TRUE(mcp_content_is_text(text));
  EXPECT_FALSE(mcp_content_is_image(text));
  EXPECT_FALSE(mcp_content_is_audio(text));
  EXPECT_FALSE(mcp_content_is_resource(text));
  EXPECT_FALSE(mcp_content_is_embedded(text));
  mcp_content_block_free(text);

  // Image content
  auto* image = mcp_image_content_create("data", "image/png");
  EXPECT_FALSE(mcp_content_is_text(image));
  EXPECT_TRUE(mcp_content_is_image(image));
  EXPECT_FALSE(mcp_content_is_audio(image));
  EXPECT_FALSE(mcp_content_is_resource(image));
  EXPECT_FALSE(mcp_content_is_embedded(image));
  mcp_content_block_free(image);

  // Audio content
  auto* audio = mcp_audio_content_create("audiodata", "audio/mp3");
  EXPECT_FALSE(mcp_content_is_text(audio));
  EXPECT_FALSE(mcp_content_is_image(audio));
  EXPECT_TRUE(mcp_content_is_audio(audio));
  EXPECT_FALSE(mcp_content_is_resource(audio));
  EXPECT_FALSE(mcp_content_is_embedded(audio));
  mcp_content_block_free(audio);

  // Resource content
  mcp_resource_t res = {.uri = const_cast<char*>("uri"),
                        .name = const_cast<char*>("name"),
                        .description = nullptr,
                        .mime_type = nullptr};
  auto* resource = mcp_resource_content_create(&res);
  EXPECT_FALSE(mcp_content_is_text(resource));
  EXPECT_FALSE(mcp_content_is_image(resource));
  EXPECT_FALSE(mcp_content_is_audio(resource));
  EXPECT_TRUE(mcp_content_is_resource(resource));
  EXPECT_FALSE(mcp_content_is_embedded(resource));
  mcp_content_block_free(resource);

  // Embedded resource
  auto* embedded = mcp_embedded_resource_create(&res, nullptr, 0);
  EXPECT_FALSE(mcp_content_is_text(embedded));
  EXPECT_FALSE(mcp_content_is_image(embedded));
  EXPECT_FALSE(mcp_content_is_audio(embedded));
  EXPECT_FALSE(mcp_content_is_resource(embedded));
  EXPECT_TRUE(mcp_content_is_embedded(embedded));
  mcp_content_block_free(embedded);

  // Null pointer
  EXPECT_FALSE(mcp_content_is_text(nullptr));
  EXPECT_FALSE(mcp_content_is_image(nullptr));
  EXPECT_FALSE(mcp_content_is_audio(nullptr));
  EXPECT_FALSE(mcp_content_is_resource(nullptr));
  EXPECT_FALSE(mcp_content_is_embedded(nullptr));
}

// ==================== DEEP COPY FUNCTIONS ====================

TEST_F(MCPCApiTypeHelpersTest, ContentBlockDeepCopy) {
  // Text content with annotations
  mcp_role_t audience[] = {MCP_ROLE_USER, MCP_ROLE_ASSISTANT};
  auto* original =
      mcp_text_content_with_annotations("Original text", audience, 2, 0.75);

  auto* copy = mcp_content_block_copy(original);

  ASSERT_NE(copy, nullptr);
  EXPECT_NE(copy, original);  // Different pointers
  EXPECT_EQ(copy->type, MCP_CONTENT_TEXT);
  EXPECT_STREQ(copy->text.text, "Original text");
  EXPECT_NE(copy->text.text, original->text.text);  // Different string pointers

  // Check annotations were copied
  EXPECT_TRUE(copy->text.annotations.has_value);
  EXPECT_EQ(copy->text.annotations.value.audience_count, 2);
  EXPECT_NE(
      copy->text.annotations.value.audience,
      original->text.annotations.value.audience);  // Different array pointers
  EXPECT_DOUBLE_EQ(copy->text.annotations.value.priority, 0.75);

  // Modify original shouldn't affect copy
  free(original->text.text);
  original->text.text = strdup("Modified text");
  EXPECT_STREQ(copy->text.text, "Original text");

  mcp_content_block_free(original);
  mcp_content_block_free(copy);
}

TEST_F(MCPCApiTypeHelpersTest, EmbeddedResourceDeepCopy) {
  // Create embedded resource with nested content
  mcp_resource_t resource = {.uri = const_cast<char*>("file:///doc.pdf"),
                             .name = const_cast<char*>("doc.pdf"),
                             .description = const_cast<char*>("Document"),
                             .mime_type = const_cast<char*>("application/pdf")};

  mcp_content_block_t nested[2];
  nested[0] = *mcp_text_content_create("Page 1");
  nested[1] = *mcp_image_content_create("imagedata", "image/png");

  auto* original = mcp_embedded_resource_create(&resource, nested, 2);
  auto* copy = mcp_content_block_copy(original);

  ASSERT_NE(copy, nullptr);
  EXPECT_NE(copy, original);
  EXPECT_EQ(copy->type, MCP_CONTENT_EMBEDDED);
  EXPECT_STREQ(copy->embedded.resource.uri, "file:///doc.pdf");
  EXPECT_NE(copy->embedded.resource.uri, original->embedded.resource.uri);

  // Check nested content was deep copied
  EXPECT_EQ(copy->embedded.content_count, 2);
  EXPECT_NE(copy->embedded.content, original->embedded.content);
  EXPECT_EQ(copy->embedded.content[0].type, MCP_CONTENT_TEXT);
  EXPECT_EQ(copy->embedded.content[1].type, MCP_CONTENT_IMAGE);

  // Clean up
  mcp_content_block_free(&nested[0]);
  mcp_content_block_free(&nested[1]);
  mcp_content_block_free(original);
  mcp_content_block_free(copy);
}

TEST_F(MCPCApiTypeHelpersTest, ToolDeepCopy) {
  auto* schema = mcp_json_create_object();
  auto* original = mcp_tool_complete("calculator", "Calculate math", schema);

  auto* copy = mcp_tool_copy(original);

  ASSERT_NE(copy, nullptr);
  EXPECT_NE(copy, original);
  EXPECT_STREQ(copy->name, "calculator");
  EXPECT_NE(copy->name, original->name);  // Different string pointers
  EXPECT_STREQ(copy->description, "Calculate math");
  EXPECT_NE(copy->description, original->description);
  EXPECT_EQ(copy->input_schema, schema);  // Schema is shallow copied (handle)

  mcp_json_free(schema);
  mcp_tool_free(original);
  mcp_tool_free(copy);
}

TEST_F(MCPCApiTypeHelpersTest, PromptDeepCopy) {
  mcp_prompt_argument_t args[] = {
      {.name = const_cast<char*>("arg1"),
       .description = const_cast<char*>("First argument"),
       .required = true},
      {.name = const_cast<char*>("arg2"),
       .description = const_cast<char*>("Second argument"),
       .required = false}};

  auto* original = mcp_prompt_with_arguments("template", args, 2);
  auto* copy = mcp_prompt_copy(original);

  ASSERT_NE(copy, nullptr);
  EXPECT_NE(copy, original);
  EXPECT_STREQ(copy->name, "template");
  EXPECT_NE(copy->name, original->name);

  // Check arguments were deep copied
  EXPECT_EQ(copy->argument_count, 2);
  EXPECT_NE(copy->arguments, original->arguments);
  EXPECT_STREQ(copy->arguments[0].name, "arg1");
  EXPECT_NE(copy->arguments[0].name, original->arguments[0].name);
  EXPECT_TRUE(copy->arguments[0].required);

  mcp_prompt_free(original);
  mcp_prompt_free(copy);
}

TEST_F(MCPCApiTypeHelpersTest, NullCopy) {
  // Test copying null pointers
  EXPECT_EQ(mcp_content_block_copy(nullptr), nullptr);
  EXPECT_EQ(mcp_tool_copy(nullptr), nullptr);
  EXPECT_EQ(mcp_prompt_copy(nullptr), nullptr);
}

// ==================== REQUEST ID HELPERS ====================

TEST_F(MCPCApiTypeHelpersTest, RequestIdString) {
  auto id = mcp_request_id_string("req-abc-123");

  EXPECT_EQ(id.type, MCP_REQUEST_ID_STRING);
  EXPECT_STREQ(id.string_value, "req-abc-123");

  mcp_request_id_free(&id);
  EXPECT_EQ(id.string_value, nullptr);
}

TEST_F(MCPCApiTypeHelpersTest, RequestIdInt) {
  auto id = mcp_request_id_int(42);

  EXPECT_EQ(id.type, MCP_REQUEST_ID_INT);
  EXPECT_EQ(id.int_value, 42);

  mcp_request_id_free(&id);  // Should be safe for int type
}

TEST_F(MCPCApiTypeHelpersTest, RequestIdEquals) {
  auto str1 = mcp_request_id_string("req-123");
  auto str2 = mcp_request_id_string("req-123");
  auto str3 = mcp_request_id_string("req-456");
  auto int1 = mcp_request_id_int(42);
  auto int2 = mcp_request_id_int(42);
  auto int3 = mcp_request_id_int(99);

  // Same string IDs
  EXPECT_TRUE(mcp_request_id_equals(&str1, &str2));

  // Different string IDs
  EXPECT_FALSE(mcp_request_id_equals(&str1, &str3));

  // Same int IDs
  EXPECT_TRUE(mcp_request_id_equals(&int1, &int2));

  // Different int IDs
  EXPECT_FALSE(mcp_request_id_equals(&int1, &int3));

  // Different types
  EXPECT_FALSE(mcp_request_id_equals(&str1, &int1));

  // Null handling
  EXPECT_FALSE(mcp_request_id_equals(nullptr, &str1));
  EXPECT_FALSE(mcp_request_id_equals(&str1, nullptr));
  EXPECT_FALSE(mcp_request_id_equals(nullptr, nullptr));

  // Cleanup
  mcp_request_id_free(&str1);
  mcp_request_id_free(&str2);
  mcp_request_id_free(&str3);
}

TEST_F(MCPCApiTypeHelpersTest, RequestIdEdgeCases) {
  // Empty string ID
  auto empty = mcp_request_id_string("");
  EXPECT_STREQ(empty.string_value, "");
  mcp_request_id_free(&empty);

  // Negative int ID
  auto negative = mcp_request_id_int(-1);
  EXPECT_EQ(negative.int_value, -1);

  // Max int ID
  auto max_int = mcp_request_id_int(std::numeric_limits<int>::max());
  EXPECT_EQ(max_int.int_value, std::numeric_limits<int>::max());

  // Min int ID
  auto min_int = mcp_request_id_int(std::numeric_limits<int>::min());
  EXPECT_EQ(min_int.int_value, std::numeric_limits<int>::min());
}

// ==================== PROGRESS TOKEN HELPERS ====================

TEST_F(MCPCApiTypeHelpersTest, ProgressTokenString) {
  auto token = mcp_progress_token_string("task-xyz-789");

  EXPECT_EQ(token.type, MCP_PROGRESS_TOKEN_STRING);
  EXPECT_STREQ(token.string_value, "task-xyz-789");

  mcp_progress_token_free(&token);
  EXPECT_EQ(token.string_value, nullptr);
}

TEST_F(MCPCApiTypeHelpersTest, ProgressTokenInt) {
  auto token = mcp_progress_token_int(999);

  EXPECT_EQ(token.type, MCP_PROGRESS_TOKEN_INT);
  EXPECT_EQ(token.int_value, 999);

  mcp_progress_token_free(&token);  // Should be safe for int type
}

// ==================== ARRAY HELPERS ====================

TEST_F(MCPCApiTypeHelpersTest, ContentBlockArray) {
  // Create array with initial capacity
  auto* array = mcp_content_block_array_create(2);

  ASSERT_NE(array, nullptr);
  EXPECT_EQ(array->capacity, 2);
  EXPECT_EQ(array->count, 0);
  EXPECT_NE(array->items, nullptr);

  // Add items
  auto* text1 = mcp_text_content_create("First");
  auto* text2 = mcp_text_content_create("Second");
  auto* text3 = mcp_text_content_create("Third");

  EXPECT_TRUE(mcp_content_block_array_append(array, text1));
  EXPECT_EQ(array->count, 1);

  EXPECT_TRUE(mcp_content_block_array_append(array, text2));
  EXPECT_EQ(array->count, 2);

  // Should trigger reallocation
  EXPECT_TRUE(mcp_content_block_array_append(array, text3));
  EXPECT_EQ(array->count, 3);
  EXPECT_GE(array->capacity, 3);

  // Verify items
  EXPECT_EQ(array->items[0], text1);
  EXPECT_EQ(array->items[1], text2);
  EXPECT_EQ(array->items[2], text3);

  // Free array (should free all items)
  mcp_content_block_array_free(array);
}

TEST_F(MCPCApiTypeHelpersTest, ContentBlockArrayZeroCapacity) {
  // Create array with zero initial capacity
  auto* array = mcp_content_block_array_create(0);

  ASSERT_NE(array, nullptr);
  EXPECT_EQ(array->capacity, 0);
  EXPECT_EQ(array->count, 0);

  // Should allocate on first append
  auto* text = mcp_text_content_create("Test");
  EXPECT_TRUE(mcp_content_block_array_append(array, text));
  EXPECT_GT(array->capacity, 0);
  EXPECT_EQ(array->count, 1);

  mcp_content_block_array_free(array);
}

TEST_F(MCPCApiTypeHelpersTest, ContentBlockArrayNullHandling) {
  auto* array = mcp_content_block_array_create(1);

  // Append null should fail
  EXPECT_FALSE(mcp_content_block_array_append(array, nullptr));
  EXPECT_EQ(array->count, 0);

  // Append to null array should fail
  auto* text = mcp_text_content_create("Test");
  EXPECT_FALSE(mcp_content_block_array_append(nullptr, text));

  mcp_content_block_free(text);
  mcp_content_block_array_free(array);

  // Free null should be safe
  mcp_content_block_array_free(nullptr);
}

// ==================== MEMORY POOL ====================

TEST_F(MCPCApiTypeHelpersTest, MemoryPool) {
  auto pool = mcp_memory_pool_create();

  ASSERT_NE(pool, nullptr);
  EXPECT_EQ(mcp_memory_pool_get_size(pool), 0);
  EXPECT_EQ(mcp_memory_pool_get_peak_size(pool), 0);

  // Allocate memory
  void* ptr1 = mcp_memory_pool_alloc(pool, 100);
  ASSERT_NE(ptr1, nullptr);
  EXPECT_EQ(mcp_memory_pool_get_size(pool), 100);
  EXPECT_EQ(mcp_memory_pool_get_peak_size(pool), 100);

  void* ptr2 = mcp_memory_pool_alloc(pool, 200);
  ASSERT_NE(ptr2, nullptr);
  EXPECT_EQ(mcp_memory_pool_get_size(pool), 300);
  EXPECT_EQ(mcp_memory_pool_get_peak_size(pool), 300);

  // Free one allocation
  mcp_memory_pool_free(pool, ptr1, 100);
  EXPECT_EQ(mcp_memory_pool_get_size(pool), 200);
  EXPECT_EQ(mcp_memory_pool_get_peak_size(pool), 300);  // Peak unchanged

  // Allocate again
  void* ptr3 = mcp_memory_pool_alloc(pool, 500);
  ASSERT_NE(ptr3, nullptr);
  EXPECT_EQ(mcp_memory_pool_get_size(pool), 700);
  EXPECT_EQ(mcp_memory_pool_get_peak_size(pool), 700);  // New peak

  // Destroy pool (should free all remaining allocations)
  mcp_memory_pool_destroy(pool);
}

TEST_F(MCPCApiTypeHelpersTest, MemoryPoolStressTest) {
  auto pool = mcp_memory_pool_create();

  std::vector<std::pair<void*, size_t>> allocations;
  const int num_allocs = 1000;

  // Allocate many blocks
  for (int i = 0; i < num_allocs; ++i) {
    size_t size = (i % 100) + 1;  // Vary sizes
    void* ptr = mcp_memory_pool_alloc(pool, size);
    ASSERT_NE(ptr, nullptr);
    allocations.push_back({ptr, size});
  }

  // Free half of them
  for (int i = 0; i < num_allocs / 2; ++i) {
    mcp_memory_pool_free(pool, allocations[i].first, allocations[i].second);
  }

  // Allocate more
  for (int i = 0; i < num_allocs / 2; ++i) {
    size_t size = (i % 50) + 1;
    void* ptr = mcp_memory_pool_alloc(pool, size);
    ASSERT_NE(ptr, nullptr);
  }

  size_t peak = mcp_memory_pool_get_peak_size(pool);
  EXPECT_GT(peak, 0);

  mcp_memory_pool_destroy(pool);
}

// ==================== STRING UTILITIES ====================

TEST_F(MCPCApiTypeHelpersTest, StringDuplicate) {
  const char* original = "Hello, World!";
  char* dup = mcp_string_duplicate(original);

  ASSERT_NE(dup, nullptr);
  EXPECT_NE(dup, original);  // Different pointers
  EXPECT_STREQ(dup, original);

  mcp_string_free(dup);

  // Null handling
  EXPECT_EQ(mcp_string_duplicate(nullptr), nullptr);
}

TEST_F(MCPCApiTypeHelpersTest, StringEquals) {
  EXPECT_TRUE(mcp_string_equals("hello", "hello"));
  EXPECT_FALSE(mcp_string_equals("hello", "Hello"));
  EXPECT_FALSE(mcp_string_equals("hello", "world"));

  // Null handling
  EXPECT_TRUE(mcp_string_equals(nullptr, nullptr));
  EXPECT_FALSE(mcp_string_equals("hello", nullptr));
  EXPECT_FALSE(mcp_string_equals(nullptr, "hello"));
}

TEST_F(MCPCApiTypeHelpersTest, StringStartsWith) {
  EXPECT_TRUE(mcp_string_starts_with("hello world", "hello"));
  EXPECT_TRUE(mcp_string_starts_with("hello", "hello"));
  EXPECT_TRUE(mcp_string_starts_with("hello", ""));
  EXPECT_FALSE(mcp_string_starts_with("hello", "world"));
  EXPECT_FALSE(mcp_string_starts_with("hi", "hello"));

  // Null handling
  EXPECT_FALSE(mcp_string_starts_with(nullptr, "hello"));
  EXPECT_FALSE(mcp_string_starts_with("hello", nullptr));
  EXPECT_FALSE(mcp_string_starts_with(nullptr, nullptr));
}

TEST_F(MCPCApiTypeHelpersTest, StringEndsWith) {
  EXPECT_TRUE(mcp_string_ends_with("hello world", "world"));
  EXPECT_TRUE(mcp_string_ends_with("hello", "hello"));
  EXPECT_TRUE(mcp_string_ends_with("hello", ""));
  EXPECT_FALSE(mcp_string_ends_with("hello", "world"));
  EXPECT_FALSE(mcp_string_ends_with("hello", "hello world"));

  // Null handling
  EXPECT_FALSE(mcp_string_ends_with(nullptr, "world"));
  EXPECT_FALSE(mcp_string_ends_with("hello", nullptr));
  EXPECT_FALSE(mcp_string_ends_with(nullptr, nullptr));
}

// ==================== LOGGING LEVEL UTILITIES ====================

TEST_F(MCPCApiTypeHelpersTest, LoggingLevelToString) {
  EXPECT_STREQ(mcp_logging_level_to_string(MCP_LOGGING_DEBUG), "debug");
  EXPECT_STREQ(mcp_logging_level_to_string(MCP_LOGGING_INFO), "info");
  EXPECT_STREQ(mcp_logging_level_to_string(MCP_LOGGING_NOTICE), "notice");
  EXPECT_STREQ(mcp_logging_level_to_string(MCP_LOGGING_WARNING), "warning");
  EXPECT_STREQ(mcp_logging_level_to_string(MCP_LOGGING_ERROR), "error");
  EXPECT_STREQ(mcp_logging_level_to_string(MCP_LOGGING_CRITICAL), "critical");
  EXPECT_STREQ(mcp_logging_level_to_string(MCP_LOGGING_ALERT), "alert");
  EXPECT_STREQ(mcp_logging_level_to_string(MCP_LOGGING_EMERGENCY), "emergency");

  // Invalid level
  EXPECT_STREQ(
      mcp_logging_level_to_string(static_cast<mcp_logging_level_t>(999)),
      "unknown");
}

TEST_F(MCPCApiTypeHelpersTest, LoggingLevelFromString) {
  EXPECT_EQ(mcp_logging_level_from_string("debug"), MCP_LOGGING_DEBUG);
  EXPECT_EQ(mcp_logging_level_from_string("info"), MCP_LOGGING_INFO);
  EXPECT_EQ(mcp_logging_level_from_string("notice"), MCP_LOGGING_NOTICE);
  EXPECT_EQ(mcp_logging_level_from_string("warning"), MCP_LOGGING_WARNING);
  EXPECT_EQ(mcp_logging_level_from_string("error"), MCP_LOGGING_ERROR);
  EXPECT_EQ(mcp_logging_level_from_string("critical"), MCP_LOGGING_CRITICAL);
  EXPECT_EQ(mcp_logging_level_from_string("alert"), MCP_LOGGING_ALERT);
  EXPECT_EQ(mcp_logging_level_from_string("emergency"), MCP_LOGGING_EMERGENCY);

  // Invalid string (default to ERROR)
  EXPECT_EQ(mcp_logging_level_from_string("invalid"), MCP_LOGGING_ERROR);
  EXPECT_EQ(mcp_logging_level_from_string(""), MCP_LOGGING_ERROR);
  EXPECT_EQ(mcp_logging_level_from_string(nullptr), MCP_LOGGING_ERROR);

  // Case sensitivity
  EXPECT_EQ(mcp_logging_level_from_string("DEBUG"), MCP_LOGGING_ERROR);
  EXPECT_EQ(mcp_logging_level_from_string("Error"), MCP_LOGGING_ERROR);
}

// ==================== FREE FUNCTIONS ====================

TEST_F(MCPCApiTypeHelpersTest, FreeNullPointers) {
  // All free functions should handle null safely
  mcp_content_block_free(nullptr);
  mcp_tool_free(nullptr);
  mcp_prompt_free(nullptr);
  mcp_resource_free(nullptr);
  mcp_message_free(nullptr);
  mcp_error_free(nullptr);
  mcp_string_schema_free(nullptr);
  mcp_number_schema_free(nullptr);
  mcp_boolean_schema_free(nullptr);
  mcp_enum_schema_free(nullptr);
  mcp_request_free(nullptr);
  mcp_response_free(nullptr);
  mcp_notification_free(nullptr);
  mcp_implementation_free(nullptr);
  mcp_root_free(nullptr);
  mcp_model_hint_free(nullptr);
  mcp_model_preferences_free(nullptr);
  mcp_string_free(nullptr);

  // Should not crash
  EXPECT_TRUE(true);
}

// ==================== THREAD SAFETY TEST ====================

TEST_F(MCPCApiTypeHelpersTest, ThreadSafetyBasic) {
  // Test that different threads can create and destroy objects independently
  const int num_threads = 10;
  const int iterations = 100;

  std::vector<std::thread> threads;

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([iterations, t]() {
      for (int i = 0; i < iterations; ++i) {
        // Create various objects
        auto* text = mcp_text_content_create("Thread test");
        auto* tool = mcp_tool_create("tool");
        auto* prompt = mcp_prompt_create("prompt");

        // Use them
        EXPECT_TRUE(mcp_content_block_is_valid(text));
        EXPECT_TRUE(mcp_tool_is_valid(tool));
        EXPECT_TRUE(mcp_prompt_is_valid(prompt));

        // Clean up
        mcp_content_block_free(text);
        mcp_tool_free(tool);
        mcp_prompt_free(prompt);

        // Small delay to increase chance of interleaving
        std::this_thread::sleep_for(std::chrono::microseconds(1));
      }
    });
  }

  // Wait for all threads
  for (auto& thread : threads) {
    thread.join();
  }
}

// Main test runner
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}