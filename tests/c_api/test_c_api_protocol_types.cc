/**
 * @file test_c_api_protocol_types.cc
 * @brief Comprehensive unit tests for MCP C API protocol types
 *
 * Tests all MCP protocol-specific types including requests, responses,
 * notifications, capabilities, and protocol messages.
 */

#include <cstring>
#include <map>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/c_api/mcp_c_types.h"

class MCPCApiProtocolTypesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Common setup
  }

  void TearDown() override {
    // Cleanup
  }

  // Helper to verify optional fields
  template <typename T>
  void ExpectOptional(bool has_value, const T& value, const T& expected) {
    if (has_value) {
      EXPECT_EQ(value, expected);
    }
  }
};

// ==================== CLIENT CAPABILITIES ====================

TEST_F(MCPCApiProtocolTypesTest, ClientCapabilitiesEmpty) {
  mcp_client_capabilities_t caps = {};

  EXPECT_EQ(caps.experimental, nullptr);
  EXPECT_FALSE(caps.sampling.has_value);
  EXPECT_FALSE(caps.roots.has_value);
}

TEST_F(MCPCApiProtocolTypesTest, ClientCapabilitiesFull) {
  mcp_client_capabilities_t caps = {};

  // Set experimental
  caps.experimental = mcp_json_create_object();
  ASSERT_NE(caps.experimental, nullptr);

  // Set sampling
  caps.sampling.has_value = true;
  caps.sampling.value.temperature.has_value = true;
  caps.sampling.value.temperature.value = 0.7;
  caps.sampling.value.max_tokens.has_value = true;
  caps.sampling.value.max_tokens.value = 2000;
  caps.sampling.value.stop_sequences_count = 2;
  const char* stops[] = {"END", "STOP"};
  caps.sampling.value.stop_sequences = const_cast<char**>(stops);

  // Set roots capability
  caps.roots.has_value = true;
  caps.roots.value.list_changed.has_value = true;

  // Verify
  EXPECT_NE(caps.experimental, nullptr);
  EXPECT_TRUE(caps.sampling.has_value);
  EXPECT_DOUBLE_EQ(caps.sampling.value.temperature.value, 0.7);
  EXPECT_EQ(caps.sampling.value.max_tokens.value, 2000);
  EXPECT_TRUE(caps.roots.has_value);

  // Cleanup
  mcp_json_free(caps.experimental);
}

// ==================== SERVER CAPABILITIES ====================

TEST_F(MCPCApiProtocolTypesTest, ServerCapabilitiesBasic) {
  mcp_server_capabilities_t caps = {};

  // Set basic boolean capabilities
  caps.tools.has_value = true;
  caps.tools.value = true;

  caps.prompts.has_value = true;
  caps.prompts.value = false;

  caps.logging.has_value = true;
  caps.logging.value = true;

  // Verify
  EXPECT_TRUE(caps.tools.has_value);
  EXPECT_TRUE(caps.tools.value);
  EXPECT_TRUE(caps.prompts.has_value);
  EXPECT_FALSE(caps.prompts.value);
  EXPECT_TRUE(caps.logging.has_value);
  EXPECT_TRUE(caps.logging.value);
}

TEST_F(MCPCApiProtocolTypesTest, ServerCapabilitiesResources) {
  mcp_server_capabilities_t caps = {};

  // Simple boolean resources
  caps.resources.has_value = true;
  caps.resources.value.is_bool = true;
  caps.resources.value.bool_value = true;

  EXPECT_TRUE(caps.resources.has_value);
  EXPECT_TRUE(caps.resources.value.is_bool);
  EXPECT_TRUE(caps.resources.value.bool_value);

  // Complex resources capability
  mcp_server_capabilities_t caps2 = {};
  caps2.resources.has_value = true;
  caps2.resources.value.is_bool = false;
  caps2.resources.value.capability.subscribe.has_value = true;
  caps2.resources.value.capability.list_changed.has_value = true;

  EXPECT_TRUE(caps2.resources.has_value);
  EXPECT_FALSE(caps2.resources.value.is_bool);
  EXPECT_TRUE(caps2.resources.value.capability.subscribe.has_value);
}

// ==================== INITIALIZE REQUEST/RESULT ====================

TEST_F(MCPCApiProtocolTypesTest, InitializeRequest) {
  mcp_initialize_request_t req = {};
  req.protocol_version = const_cast<char*>("2025-06-18");
  req.capabilities.experimental = mcp_json_create_object();
  req.client_info.has_value = true;
  req.client_info.value.name = const_cast<char*>("Test Client");
  req.client_info.value.version = const_cast<char*>("1.0.0");

  EXPECT_STREQ(req.protocol_version, "2025-06-18");
  EXPECT_NE(req.capabilities.experimental, nullptr);
  EXPECT_TRUE(req.client_info.has_value);
  EXPECT_STREQ(req.client_info.value.name, "Test Client");
  EXPECT_STREQ(req.client_info.value.version, "1.0.0");

  // Cleanup
  mcp_json_free(req.capabilities.experimental);
}

TEST_F(MCPCApiProtocolTypesTest, InitializeResult) {
  mcp_initialize_result_t result = {};
  result.protocol_version = const_cast<char*>("2025-06-18");
  result.capabilities.tools.has_value = true;
  result.capabilities.tools.value = true;
  result.server_info.has_value = true;
  result.server_info.value.name = const_cast<char*>("Test Server");
  result.server_info.value.version = const_cast<char*>("2.0.0");
  result.instructions.has_value = true;
  result.instructions.value = const_cast<char*>("Welcome to the server");

  EXPECT_STREQ(result.protocol_version, "2025-06-18");
  EXPECT_TRUE(result.capabilities.tools.value);
  EXPECT_STREQ(result.server_info.value.name, "Test Server");
  EXPECT_STREQ(result.instructions.value, "Welcome to the server");
}

// ==================== PROGRESS NOTIFICATION ====================

TEST_F(MCPCApiProtocolTypesTest, ProgressNotification) {
  mcp_progress_notification_t notif = {};
  notif.progress_token = mcp_progress_token_string("task-123");
  notif.progress = 0.75;
  notif.total.has_value = true;
  notif.total.value = 100.0;

  EXPECT_EQ(notif.progress_token.type, MCP_PROGRESS_TOKEN_STRING);
  EXPECT_STREQ(notif.progress_token.string_value, "task-123");
  EXPECT_DOUBLE_EQ(notif.progress, 0.75);
  EXPECT_TRUE(notif.total.has_value);
  EXPECT_DOUBLE_EQ(notif.total.value, 100.0);

  mcp_progress_token_free(&notif.progress_token);
}

// ==================== CANCELLED NOTIFICATION ====================

TEST_F(MCPCApiProtocolTypesTest, CancelledNotification) {
  mcp_cancelled_notification_t notif = {};
  notif.request_id = mcp_request_id_int(42);
  notif.reason.has_value = true;
  notif.reason.value = const_cast<char*>("User cancelled");

  EXPECT_EQ(notif.request_id.type, MCP_REQUEST_ID_INT);
  EXPECT_EQ(notif.request_id.int_value, 42);
  EXPECT_TRUE(notif.reason.has_value);
  EXPECT_STREQ(notif.reason.value, "User cancelled");
}

// ==================== RESOURCE OPERATIONS ====================

TEST_F(MCPCApiProtocolTypesTest, ListResourcesRequest) {
  mcp_list_resources_request_t req = {};
  req.cursor.has_value = true;
  req.cursor.value = const_cast<char*>("next-page-token");

  EXPECT_TRUE(req.cursor.has_value);
  EXPECT_STREQ(req.cursor.value, "next-page-token");
}

TEST_F(MCPCApiProtocolTypesTest, ListResourcesResult) {
  mcp_list_resources_result_t result = {};
  result.next_cursor.has_value = true;
  result.next_cursor.value = const_cast<char*>("page-2");

  // Create resources
  result.resource_count = 2;
  result.resources =
      static_cast<mcp_resource_t*>(calloc(2, sizeof(mcp_resource_t)));

  result.resources[0].uri = const_cast<char*>("file:///doc1.txt");
  result.resources[0].name = const_cast<char*>("doc1.txt");

  result.resources[1].uri = const_cast<char*>("file:///doc2.txt");
  result.resources[1].name = const_cast<char*>("doc2.txt");

  EXPECT_STREQ(result.next_cursor.value, "page-2");
  EXPECT_EQ(result.resource_count, 2);
  EXPECT_STREQ(result.resources[0].uri, "file:///doc1.txt");
  EXPECT_STREQ(result.resources[1].name, "doc2.txt");

  // Cleanup
  free(result.resources);
}

TEST_F(MCPCApiProtocolTypesTest, ResourceTemplate) {
  mcp_resource_template_t template_res = {};
  template_res.uri_template = const_cast<char*>("/api/resource/{id}");
  template_res.name = const_cast<char*>("Resource Template");
  template_res.description.has_value = true;
  template_res.description.value = const_cast<char*>("Template for resources");
  template_res.mime_type.has_value = true;
  template_res.mime_type.value = const_cast<char*>("application/json");

  EXPECT_STREQ(template_res.uri_template, "/api/resource/{id}");
  EXPECT_STREQ(template_res.name, "Resource Template");
  EXPECT_STREQ(template_res.description.value, "Template for resources");
  EXPECT_STREQ(template_res.mime_type.value, "application/json");
}

TEST_F(MCPCApiProtocolTypesTest, ReadResourceRequest) {
  mcp_read_resource_request_t req = {};
  req.uri = const_cast<char*>("file:///important.txt");

  EXPECT_STREQ(req.uri, "file:///important.txt");
}

TEST_F(MCPCApiProtocolTypesTest, ReadResourceResult) {
  mcp_read_resource_result_t result = {};
  result.content_count = 2;
  result.contents = static_cast<mcp_resource_contents_t*>(
      calloc(2, sizeof(mcp_resource_contents_t)));

  // Text content
  result.contents[0].type = MCP_RESOURCE_CONTENTS_TEXT;
  result.contents[0].text.text = const_cast<char*>("File contents here");
  result.contents[0].text.uri.has_value = true;
  result.contents[0].text.uri.value = const_cast<char*>("file:///doc.txt");
  result.contents[0].text.mime_type.has_value = true;
  result.contents[0].text.mime_type.value = const_cast<char*>("text/plain");

  // Blob content
  result.contents[1].type = MCP_RESOURCE_CONTENTS_BLOB;
  result.contents[1].blob.blob = const_cast<char*>("base64data");
  result.contents[1].blob.uri.has_value = true;
  result.contents[1].blob.uri.value = const_cast<char*>("file:///image.png");
  result.contents[1].blob.mime_type.has_value = true;
  result.contents[1].blob.mime_type.value = const_cast<char*>("image/png");

  EXPECT_EQ(result.content_count, 2);
  EXPECT_EQ(result.contents[0].type, MCP_RESOURCE_CONTENTS_TEXT);
  EXPECT_STREQ(result.contents[0].text.text, "File contents here");
  EXPECT_EQ(result.contents[1].type, MCP_RESOURCE_CONTENTS_BLOB);
  EXPECT_STREQ(result.contents[1].blob.blob, "base64data");

  // Cleanup
  free(result.contents);
}

TEST_F(MCPCApiProtocolTypesTest, SubscribeUnsubscribe) {
  mcp_subscribe_request_t sub_req = {};
  sub_req.uri = const_cast<char*>("file:///watched.txt");

  mcp_unsubscribe_request_t unsub_req = {};
  unsub_req.uri = const_cast<char*>("file:///watched.txt");

  EXPECT_STREQ(sub_req.uri, "file:///watched.txt");
  EXPECT_STREQ(unsub_req.uri, "file:///watched.txt");
}

TEST_F(MCPCApiProtocolTypesTest, ResourceUpdatedNotification) {
  mcp_resource_updated_notification_t notif = {};
  notif.uri = const_cast<char*>("file:///changed.txt");

  EXPECT_STREQ(notif.uri, "file:///changed.txt");
}

// ==================== PROMPT OPERATIONS ====================

TEST_F(MCPCApiProtocolTypesTest, ListPromptsRequest) {
  mcp_list_prompts_request_t req = {};
  req.cursor.has_value = true;
  req.cursor.value = const_cast<char*>("prompt-page-2");

  EXPECT_STREQ(req.cursor.value, "prompt-page-2");
}

TEST_F(MCPCApiProtocolTypesTest, ListPromptsResult) {
  mcp_list_prompts_result_t result = {};
  result.prompt_count = 2;
  result.prompts = static_cast<mcp_prompt_t*>(calloc(2, sizeof(mcp_prompt_t)));

  result.prompts[0].name = const_cast<char*>("greeting");
  result.prompts[0].description = const_cast<char*>("Greeting prompt");

  result.prompts[1].name = const_cast<char*>("summary");
  result.prompts[1].description = const_cast<char*>("Summary prompt");

  EXPECT_EQ(result.prompt_count, 2);
  EXPECT_STREQ(result.prompts[0].name, "greeting");
  EXPECT_STREQ(result.prompts[1].description, "Summary prompt");

  // Cleanup
  free(result.prompts);
}

TEST_F(MCPCApiProtocolTypesTest, GetPromptRequest) {
  mcp_get_prompt_request_t req = {};
  req.name = const_cast<char*>("code_review");
  req.arguments.has_value = true;
  req.arguments.value = mcp_json_create_object();

  EXPECT_STREQ(req.name, "code_review");
  EXPECT_TRUE(req.arguments.has_value);
  EXPECT_NE(req.arguments.value, nullptr);

  // Cleanup
  mcp_json_free(req.arguments.value);
}

TEST_F(MCPCApiProtocolTypesTest, GetPromptResult) {
  mcp_get_prompt_result_t result = {};
  result.description.has_value = true;
  result.description.value = const_cast<char*>("Review code");

  result.message_count = 2;
  result.messages = static_cast<mcp_prompt_message_t*>(
      calloc(2, sizeof(mcp_prompt_message_t)));

  result.messages[0].role = MCP_ROLE_USER;
  result.messages[0].content_type = MCP_PROMPT_MESSAGE_TEXT;
  result.messages[0].text.text = const_cast<char*>("Review this code");

  result.messages[1].role = MCP_ROLE_ASSISTANT;
  result.messages[1].content_type = MCP_PROMPT_MESSAGE_TEXT;
  result.messages[1].text.text = const_cast<char*>("I'll review it");

  EXPECT_STREQ(result.description.value, "Review code");
  EXPECT_EQ(result.message_count, 2);
  EXPECT_EQ(result.messages[0].role, MCP_ROLE_USER);
  EXPECT_STREQ(result.messages[0].text.text, "Review this code");

  // Cleanup
  free(result.messages);
}

// ==================== TOOL OPERATIONS ====================

TEST_F(MCPCApiProtocolTypesTest, ListToolsRequest) {
  mcp_list_tools_request_t req = {};
  req.cursor.has_value = true;
  req.cursor.value = const_cast<char*>("tools-next");

  EXPECT_STREQ(req.cursor.value, "tools-next");
}

TEST_F(MCPCApiProtocolTypesTest, ListToolsResult) {
  mcp_list_tools_result_t result = {};
  result.tool_count = 2;
  result.tools = static_cast<mcp_tool_t*>(calloc(2, sizeof(mcp_tool_t)));

  result.tools[0].name = const_cast<char*>("calculator");
  result.tools[1].name = const_cast<char*>("translator");

  EXPECT_EQ(result.tool_count, 2);
  EXPECT_STREQ(result.tools[0].name, "calculator");
  EXPECT_STREQ(result.tools[1].name, "translator");

  // Cleanup
  free(result.tools);
}

TEST_F(MCPCApiProtocolTypesTest, CallToolRequest) {
  mcp_call_tool_request_t req = {};
  req.name = const_cast<char*>("weather");
  req.arguments.has_value = true;
  req.arguments.value = mcp_json_create_object();

  EXPECT_STREQ(req.name, "weather");
  EXPECT_NE(req.arguments.value, nullptr);

  // Cleanup
  mcp_json_free(req.arguments.value);
}

TEST_F(MCPCApiProtocolTypesTest, CallToolResult) {
  mcp_call_tool_result_t result = {};
  result.content_count = 2;
  result.content =
      static_cast<mcp_content_block_t*>(calloc(2, sizeof(mcp_content_block_t)));

  result.content[0].type = MCP_CONTENT_TEXT;
  result.content[0].text.text = const_cast<char*>("Tool result");

  result.content[1].type = MCP_CONTENT_IMAGE;
  result.content[1].image.data = const_cast<char*>("imagedata");
  result.content[1].image.mime_type = const_cast<char*>("image/png");

  result.is_error = false;

  EXPECT_EQ(result.content_count, 2);
  EXPECT_FALSE(result.is_error);
  EXPECT_STREQ(result.content[0].text.text, "Tool result");

  // Cleanup
  free(result.content);
}

// ==================== LOGGING OPERATIONS ====================

TEST_F(MCPCApiProtocolTypesTest, SetLevelRequest) {
  mcp_set_level_request_t req = {};
  req.level = MCP_LOGGING_WARNING;

  EXPECT_EQ(req.level, MCP_LOGGING_WARNING);
}

TEST_F(MCPCApiProtocolTypesTest, LoggingMessageNotification) {
  mcp_logging_message_notification_t notif = {};
  notif.level = MCP_LOGGING_ERROR;
  notif.logger.has_value = true;
  notif.logger.value = const_cast<char*>("main");
  notif.data_type = MCP_LOGGING_DATA_STRING;
  notif.data_string = const_cast<char*>("Error occurred");

  EXPECT_EQ(notif.level, MCP_LOGGING_ERROR);
  EXPECT_STREQ(notif.logger.value, "main");
  EXPECT_EQ(notif.data_type, MCP_LOGGING_DATA_STRING);
  EXPECT_STREQ(notif.data_string, "Error occurred");

  // Test with JSON data
  mcp_logging_message_notification_t notif2 = {};
  notif2.level = MCP_LOGGING_INFO;
  notif2.data_type = MCP_LOGGING_DATA_JSON;
  notif2.data_json = mcp_json_create_object();

  EXPECT_EQ(notif2.data_type, MCP_LOGGING_DATA_JSON);
  EXPECT_NE(notif2.data_json, nullptr);

  // Cleanup
  mcp_json_free(notif2.data_json);
}

// ==================== COMPLETION OPERATIONS ====================

TEST_F(MCPCApiProtocolTypesTest, CompleteRequest) {
  mcp_complete_request_t req = {};
  req.ref.type = const_cast<char*>("prompt");
  req.ref.name = const_cast<char*>("code_complete");
  req.argument.has_value = true;
  req.argument.value = const_cast<char*>("function");

  EXPECT_STREQ(req.ref.type, "prompt");
  EXPECT_STREQ(req.ref.name, "code_complete");
  EXPECT_STREQ(req.argument.value, "function");
}

TEST_F(MCPCApiProtocolTypesTest, CompleteResult) {
  mcp_complete_result_t result = {};
  result.completion.value_count = 3;
  const char* values[] = {"option1", "option2", "option3"};
  result.completion.values = const_cast<char**>(values);
  result.completion.total.has_value = true;
  result.completion.total.value = 10.0;
  result.completion.has_more = true;

  EXPECT_EQ(result.completion.value_count, 3);
  EXPECT_STREQ(result.completion.values[0], "option1");
  EXPECT_DOUBLE_EQ(result.completion.total.value, 10.0);
  EXPECT_TRUE(result.completion.has_more);
}

// ==================== ROOTS OPERATIONS ====================

TEST_F(MCPCApiProtocolTypesTest, ListRootsResult) {
  mcp_list_roots_result_t result = {};
  result.root_count = 2;
  result.roots = static_cast<mcp_root_t*>(calloc(2, sizeof(mcp_root_t)));

  result.roots[0].uri = const_cast<char*>("file:///home");
  result.roots[0].name = const_cast<char*>("Home");

  result.roots[1].uri = const_cast<char*>("file:///workspace");
  result.roots[1].name = const_cast<char*>("Workspace");

  EXPECT_EQ(result.root_count, 2);
  EXPECT_STREQ(result.roots[0].uri, "file:///home");
  EXPECT_STREQ(result.roots[1].name, "Workspace");

  // Cleanup
  free(result.roots);
}

// ==================== SAMPLING/MESSAGE CREATION ====================

TEST_F(MCPCApiProtocolTypesTest, CreateMessageRequest) {
  mcp_create_message_request_t req = {};

  // Set messages
  req.message_count = 2;
  req.messages = static_cast<mcp_sampling_message_t*>(
      calloc(2, sizeof(mcp_sampling_message_t)));

  req.messages[0].role = MCP_ROLE_USER;
  req.messages[0].content_type = MCP_SAMPLING_MESSAGE_TEXT;
  req.messages[0].text.text = const_cast<char*>("Hello");

  req.messages[1].role = MCP_ROLE_ASSISTANT;
  req.messages[1].content_type = MCP_SAMPLING_MESSAGE_TEXT;
  req.messages[1].text.text = const_cast<char*>("Hi there");

  // Set model preferences
  req.model_preferences.has_value = true;
  req.model_preferences.value.cost_priority = 0.3;
  req.model_preferences.value.cost_priority_set = true;

  // Set system prompt
  req.system_prompt.has_value = true;
  req.system_prompt.value = const_cast<char*>("You are helpful");

  // Set temperature
  req.temperature.has_value = true;
  req.temperature.value = 0.8;

  // Set max tokens
  req.max_tokens.has_value = true;
  req.max_tokens.value = 1000;

  EXPECT_EQ(req.message_count, 2);
  EXPECT_STREQ(req.messages[0].text.text, "Hello");
  EXPECT_DOUBLE_EQ(req.model_preferences.value.cost_priority, 0.3);
  EXPECT_STREQ(req.system_prompt.value, "You are helpful");
  EXPECT_DOUBLE_EQ(req.temperature.value, 0.8);
  EXPECT_EQ(req.max_tokens.value, 1000);

  // Cleanup
  free(req.messages);
}

TEST_F(MCPCApiProtocolTypesTest, CreateMessageResult) {
  mcp_create_message_result_t result = {};
  result.message.role = MCP_ROLE_ASSISTANT;
  result.message.content_type = MCP_SAMPLING_MESSAGE_TEXT;
  result.message.text.text = const_cast<char*>("Generated response");
  result.model = const_cast<char*>("gpt-4");
  result.stop_reason.has_value = true;
  result.stop_reason.value = const_cast<char*>("end_turn");

  EXPECT_EQ(result.message.role, MCP_ROLE_ASSISTANT);
  EXPECT_STREQ(result.message.text.text, "Generated response");
  EXPECT_STREQ(result.model, "gpt-4");
  EXPECT_STREQ(result.stop_reason.value, "end_turn");
}

// ==================== ELICITATION ====================

TEST_F(MCPCApiProtocolTypesTest, ElicitRequest) {
  mcp_elicit_request_t req = {};
  req.name = const_cast<char*>("user_choice");
  req.schema_type = MCP_PRIMITIVE_SCHEMA_ENUM;
  req.schema_enum.value_count = 3;
  const char* values[] = {"yes", "no", "maybe"};
  req.schema_enum.values = const_cast<char**>(values);
  req.prompt.has_value = true;
  req.prompt.value = const_cast<char*>("Choose an option");

  EXPECT_STREQ(req.name, "user_choice");
  EXPECT_EQ(req.schema_type, MCP_PRIMITIVE_SCHEMA_ENUM);
  EXPECT_EQ(req.schema_enum.value_count, 3);
  EXPECT_STREQ(req.schema_enum.values[1], "no");
  EXPECT_STREQ(req.prompt.value, "Choose an option");
}

TEST_F(MCPCApiProtocolTypesTest, ElicitResult) {
  // String result
  mcp_elicit_result_t result1 = {};
  result1.value_type = MCP_ELICIT_VALUE_STRING;
  result1.string_value = const_cast<char*>("user input");

  EXPECT_EQ(result1.value_type, MCP_ELICIT_VALUE_STRING);
  EXPECT_STREQ(result1.string_value, "user input");

  // Number result
  mcp_elicit_result_t result2 = {};
  result2.value_type = MCP_ELICIT_VALUE_NUMBER;
  result2.number_value = 42.5;

  EXPECT_EQ(result2.value_type, MCP_ELICIT_VALUE_NUMBER);
  EXPECT_DOUBLE_EQ(result2.number_value, 42.5);

  // Boolean result
  mcp_elicit_result_t result3 = {};
  result3.value_type = MCP_ELICIT_VALUE_BOOL;
  result3.bool_value = true;

  EXPECT_EQ(result3.value_type, MCP_ELICIT_VALUE_BOOL);
  EXPECT_TRUE(result3.bool_value);

  // Null result
  mcp_elicit_result_t result4 = {};
  result4.value_type = MCP_ELICIT_VALUE_NULL;

  EXPECT_EQ(result4.value_type, MCP_ELICIT_VALUE_NULL);
}

// ==================== COMPLEX PROTOCOL SCENARIOS ====================

TEST_F(MCPCApiProtocolTypesTest, FullInitializationFlow) {
  // Client sends initialize request
  mcp_initialize_request_t init_req = {};
  init_req.protocol_version = const_cast<char*>("2025-06-18");
  init_req.capabilities.tools.has_value = true;
  init_req.capabilities.tools.value = true;
  init_req.client_info.has_value = true;
  init_req.client_info.value.name = const_cast<char*>("Test Client");
  init_req.client_info.value.version = const_cast<char*>("1.0.0");

  // Server responds with initialize result
  mcp_initialize_result_t init_result = {};
  init_result.protocol_version = const_cast<char*>("2025-06-18");
  init_result.capabilities.tools.has_value = true;
  init_result.capabilities.tools.value = true;
  init_result.capabilities.prompts.has_value = true;
  init_result.capabilities.prompts.value = true;
  init_result.server_info.has_value = true;
  init_result.server_info.value.name = const_cast<char*>("Test Server");
  init_result.server_info.value.version = const_cast<char*>("2.0.0");

  // Client sends initialized notification
  mcp_initialized_notification_t initialized = {};

  // Verify flow
  EXPECT_STREQ(init_req.protocol_version, init_result.protocol_version);
  EXPECT_TRUE(init_req.capabilities.tools.value);
  EXPECT_TRUE(init_result.capabilities.tools.value);
  EXPECT_TRUE(init_result.capabilities.prompts.value);
}

TEST_F(MCPCApiProtocolTypesTest, ProgressTrackingFlow) {
  // Start a long-running operation
  mcp_request_id_t req_id = mcp_request_id_int(123);
  mcp_progress_token_t token = mcp_progress_token_string("op-456");

  // Send progress updates
  std::vector<mcp_progress_notification_t> updates;

  // 25% progress
  mcp_progress_notification_t prog1 = {};
  prog1.progress_token = token;
  prog1.progress = 0.25;
  updates.push_back(prog1);

  // 50% progress
  mcp_progress_notification_t prog2 = {};
  prog2.progress_token = token;
  prog2.progress = 0.50;
  updates.push_back(prog2);

  // 100% progress
  mcp_progress_notification_t prog3 = {};
  prog3.progress_token = token;
  prog3.progress = 1.0;
  prog3.total.has_value = true;
  prog3.total.value = 100.0;
  updates.push_back(prog3);

  // Verify progression
  EXPECT_DOUBLE_EQ(updates[0].progress, 0.25);
  EXPECT_DOUBLE_EQ(updates[1].progress, 0.50);
  EXPECT_DOUBLE_EQ(updates[2].progress, 1.0);
  EXPECT_TRUE(updates[2].total.has_value);

  // Cleanup
  mcp_progress_token_free(&token);
}

TEST_F(MCPCApiProtocolTypesTest, ResourceSubscriptionFlow) {
  const char* resource_uri = "file:///important.txt";

  // Subscribe to resource
  mcp_subscribe_request_t sub_req = {};
  sub_req.uri = const_cast<char*>(resource_uri);

  // Resource gets updated
  mcp_resource_updated_notification_t update_notif = {};
  update_notif.uri = const_cast<char*>(resource_uri);

  // Read updated resource
  mcp_read_resource_request_t read_req = {};
  read_req.uri = const_cast<char*>(resource_uri);

  // Unsubscribe
  mcp_unsubscribe_request_t unsub_req = {};
  unsub_req.uri = const_cast<char*>(resource_uri);

  // Verify URIs match throughout flow
  EXPECT_STREQ(sub_req.uri, resource_uri);
  EXPECT_STREQ(update_notif.uri, resource_uri);
  EXPECT_STREQ(read_req.uri, resource_uri);
  EXPECT_STREQ(unsub_req.uri, resource_uri);
}

// Main test runner
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}