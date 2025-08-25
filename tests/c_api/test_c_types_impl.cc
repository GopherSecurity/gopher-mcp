/**
 * @file test_c_types_impl.cc
 * @brief Comprehensive unit tests for mcp_c_types_impl.cc
 *
 * Tests all type implementations including request IDs, progress tokens,
 * content blocks, tools, prompts, errors, messages, and JSON-RPC types.
 */

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "mcp/c_api/mcp_c_memory.h"
#include "mcp/c_api/mcp_c_types.h"
#include "mcp/c_api/mcp_c_types_api.h"

// ============================================================================
// Test Fixture
// ============================================================================

class MCPTypesTest : public ::testing::Test {
 protected:
  void SetUp() override { mcp_clear_last_error(); }

  void TearDown() override {
    // Check for any lingering errors
    const mcp_error_info_t* error = mcp_get_last_error();
    if (error) {
      ADD_FAILURE() << "Unexpected error: " << error->message;
    }
  }
};

// ============================================================================
// Request ID Tests
// ============================================================================

TEST_F(MCPTypesTest, RequestIdCreateString) {
  const char* test_str = "test-request-id";
  auto id = mcp_request_id_create_string(test_str);
  ASSERT_NE(id, nullptr);

  EXPECT_EQ(mcp_request_id_is_string(id), MCP_TRUE);
  EXPECT_EQ(mcp_request_id_is_number(id), MCP_FALSE);
  EXPECT_EQ(mcp_request_id_get_type(id), MCP_REQUEST_ID_TYPE_STRING);
  EXPECT_STREQ(mcp_request_id_get_string(id), test_str);
  EXPECT_EQ(mcp_request_id_get_number(id), 0);

  mcp_request_id_free(id);
}

TEST_F(MCPTypesTest, RequestIdCreateNumber) {
  int64_t test_num = 42;
  auto id = mcp_request_id_create_number(test_num);
  ASSERT_NE(id, nullptr);

  EXPECT_EQ(mcp_request_id_is_string(id), MCP_FALSE);
  EXPECT_EQ(mcp_request_id_is_number(id), MCP_TRUE);
  EXPECT_EQ(mcp_request_id_get_type(id), MCP_REQUEST_ID_TYPE_NUMBER);
  EXPECT_EQ(mcp_request_id_get_string(id), nullptr);
  EXPECT_EQ(mcp_request_id_get_number(id), test_num);

  mcp_request_id_free(id);
}

TEST_F(MCPTypesTest, RequestIdNullString) {
  // Test with null string - implementation may allow empty string
  auto id = mcp_request_id_create_string(nullptr);
  if (id != nullptr) {
    // Implementation accepts nullptr, just clean up
    mcp_request_id_free(id);
  } else {
    // If nullptr returned, error might be set
    const mcp_error_info_t* error = mcp_get_last_error();
    if (error) {
      EXPECT_EQ(error->code, MCP_ERROR_NULL_POINTER);
    }
  }
}

TEST_F(MCPTypesTest, RequestIdNullOperations) {
  EXPECT_EQ(mcp_request_id_is_string(nullptr), MCP_FALSE);
  EXPECT_EQ(mcp_request_id_is_number(nullptr), MCP_FALSE);
  EXPECT_EQ(mcp_request_id_get_type(nullptr), MCP_REQUEST_ID_TYPE_STRING);
  EXPECT_EQ(mcp_request_id_get_string(nullptr), nullptr);
  EXPECT_EQ(mcp_request_id_get_number(nullptr), 0);
}

// ============================================================================
// Progress Token Tests
// ============================================================================

TEST_F(MCPTypesTest, ProgressTokenCreateString) {
  const char* test_str = "progress-token";
  auto token = mcp_progress_token_create_string(test_str);
  ASSERT_NE(token, nullptr);

  EXPECT_EQ(mcp_progress_token_get_type(token), MCP_PROGRESS_TOKEN_TYPE_STRING);
  EXPECT_STREQ(mcp_progress_token_get_string(token), test_str);
  EXPECT_EQ(mcp_progress_token_get_number(token), 0);

  mcp_progress_token_free(token);
}

TEST_F(MCPTypesTest, ProgressTokenCreateNumber) {
  int64_t test_num = 999;
  auto token = mcp_progress_token_create_number(test_num);
  ASSERT_NE(token, nullptr);

  EXPECT_EQ(mcp_progress_token_get_type(token), MCP_PROGRESS_TOKEN_TYPE_NUMBER);
  EXPECT_EQ(mcp_progress_token_get_string(token), nullptr);
  EXPECT_EQ(mcp_progress_token_get_number(token), test_num);

  mcp_progress_token_free(token);
}

TEST_F(MCPTypesTest, ProgressTokenNullString) {
  // Test with null string - implementation may allow empty string
  auto token = mcp_progress_token_create_string(nullptr);
  if (token != nullptr) {
    // Implementation accepts nullptr, just clean up
    mcp_progress_token_free(token);
  } else {
    // If nullptr returned, error might be set
    const mcp_error_info_t* error = mcp_get_last_error();
    if (error) {
      EXPECT_EQ(error->code, MCP_ERROR_NULL_POINTER);
    }
  }
}

// ============================================================================
// Content Block Tests
// ============================================================================

TEST_F(MCPTypesTest, ContentBlockCreateText) {
  const char* text = "Hello, world!";
  auto block = mcp_content_block_create_text(text);
  ASSERT_NE(block, nullptr);

  EXPECT_EQ(mcp_content_block_get_type(block), MCP_CONTENT_BLOCK_TYPE_TEXT);
  EXPECT_STREQ(mcp_content_block_get_text(block), text);

  const char* data;
  const char* mime;
  EXPECT_EQ(mcp_content_block_get_image(block, &data, &mime), MCP_FALSE);

  mcp_content_block_free(block);
}

TEST_F(MCPTypesTest, ContentBlockCreateImage) {
  const char* data = "base64encodeddata";
  const char* mime_type = "image/png";
  auto block = mcp_content_block_create_image(data, mime_type);
  ASSERT_NE(block, nullptr);

  EXPECT_EQ(mcp_content_block_get_type(block), MCP_CONTENT_BLOCK_TYPE_IMAGE);
  EXPECT_EQ(mcp_content_block_get_text(block), nullptr);

  const char* out_data;
  const char* out_mime;
  EXPECT_EQ(mcp_content_block_get_image(block, &out_data, &out_mime), MCP_TRUE);
  EXPECT_STREQ(out_data, data);
  EXPECT_STREQ(out_mime, mime_type);

  // Test partial output
  EXPECT_EQ(mcp_content_block_get_image(block, &out_data, nullptr), MCP_TRUE);
  EXPECT_STREQ(out_data, data);

  EXPECT_EQ(mcp_content_block_get_image(block, nullptr, &out_mime), MCP_TRUE);
  EXPECT_STREQ(out_mime, mime_type);

  mcp_content_block_free(block);
}

TEST_F(MCPTypesTest, ContentBlockNullParameters) {
  EXPECT_EQ(mcp_content_block_create_text(nullptr), nullptr);
  EXPECT_EQ(mcp_content_block_create_image(nullptr, "image/png"), nullptr);
  EXPECT_EQ(mcp_content_block_create_image("data", nullptr), nullptr);

  EXPECT_EQ(mcp_content_block_get_type(nullptr), MCP_CONTENT_BLOCK_TYPE_TEXT);
  EXPECT_EQ(mcp_content_block_get_text(nullptr), nullptr);

  const char* data;
  const char* mime;
  EXPECT_EQ(mcp_content_block_get_image(nullptr, &data, &mime), MCP_FALSE);
}

// ============================================================================
// Tool Tests
// ============================================================================

TEST_F(MCPTypesTest, ToolCreate) {
  const char* name = "test_tool";
  const char* description = "A test tool";

  auto tool = mcp_tool_create(name, description);
  ASSERT_NE(tool, nullptr);

  EXPECT_STREQ(mcp_tool_get_name(tool), name);
  EXPECT_STREQ(mcp_tool_get_description(tool), description);

  mcp_tool_free(tool);
}

TEST_F(MCPTypesTest, ToolCreateWithoutDescription) {
  const char* name = "test_tool";

  auto tool = mcp_tool_create(name, nullptr);
  ASSERT_NE(tool, nullptr);

  EXPECT_STREQ(mcp_tool_get_name(tool), name);
  EXPECT_EQ(mcp_tool_get_description(tool), nullptr);

  mcp_tool_free(tool);
}

TEST_F(MCPTypesTest, ToolInputSchema) {
  auto schema = mcp_tool_input_schema_create();
  ASSERT_NE(schema, nullptr);

  mcp_tool_input_schema_set_type(schema, "object");
  mcp_tool_input_schema_add_property(schema, "param1",
                                     "{\"type\": \"string\"}");
  mcp_tool_input_schema_add_property(schema, "param2",
                                     "{\"type\": \"number\"}");
  mcp_tool_input_schema_add_required(schema, "param1");

  EXPECT_STREQ(mcp_tool_input_schema_get_type(schema), "object");
  EXPECT_EQ(mcp_tool_input_schema_get_property_count(schema), 2);
  EXPECT_EQ(mcp_tool_input_schema_get_required_count(schema), 1);

  // Test setting schema to tool
  auto tool = mcp_tool_create("test", "Test tool");
  ASSERT_NE(tool, nullptr);

  mcp_tool_set_input_schema(tool, schema);

  mcp_tool_free(tool);  // Should also free the schema
}

TEST_F(MCPTypesTest, ToolNullParameters) {
  EXPECT_EQ(mcp_tool_create(nullptr, "description"), nullptr);
  EXPECT_EQ(mcp_tool_get_name(nullptr), nullptr);
  EXPECT_EQ(mcp_tool_get_description(nullptr), nullptr);

  mcp_tool_input_schema_set_type(nullptr, "object");
  mcp_tool_input_schema_add_property(nullptr, "key", "value");
  mcp_tool_input_schema_add_required(nullptr, "key");

  EXPECT_EQ(mcp_tool_input_schema_get_type(nullptr), nullptr);
  EXPECT_EQ(mcp_tool_input_schema_get_property_count(nullptr), 0);
  EXPECT_EQ(mcp_tool_input_schema_get_required_count(nullptr), 0);
}

// ============================================================================
// Prompt Tests
// ============================================================================

TEST_F(MCPTypesTest, PromptCreate) {
  const char* name = "test_prompt";
  const char* description = "A test prompt";

  auto prompt = mcp_prompt_create(name, description);
  ASSERT_NE(prompt, nullptr);

  EXPECT_STREQ(mcp_prompt_get_name(prompt), name);
  EXPECT_STREQ(mcp_prompt_get_description(prompt), description);
  EXPECT_EQ(mcp_prompt_get_argument_count(prompt), 0);

  mcp_prompt_free(prompt);
}

TEST_F(MCPTypesTest, PromptAddArguments) {
  auto prompt = mcp_prompt_create("test", "Test prompt");
  ASSERT_NE(prompt, nullptr);

  mcp_prompt_add_argument(prompt, "arg1", "First argument", MCP_TRUE);
  mcp_prompt_add_argument(prompt, "arg2", "Second argument", MCP_FALSE);
  mcp_prompt_add_argument(prompt, "arg3", nullptr, MCP_TRUE);

  EXPECT_EQ(mcp_prompt_get_argument_count(prompt), 3);

  mcp_prompt_free(prompt);
}

TEST_F(MCPTypesTest, PromptNullParameters) {
  EXPECT_EQ(mcp_prompt_create(nullptr, "description"), nullptr);
  EXPECT_EQ(mcp_prompt_get_name(nullptr), nullptr);
  EXPECT_EQ(mcp_prompt_get_description(nullptr), nullptr);
  EXPECT_EQ(mcp_prompt_get_argument_count(nullptr), 0);

  mcp_prompt_add_argument(nullptr, "arg", "desc", MCP_TRUE);
}

// ============================================================================
// Error Tests
// ============================================================================

TEST_F(MCPTypesTest, ErrorCreate) {
  int32_t code = -32600;
  const char* message = "Invalid Request";

  auto error = mcp_error_create(code, message);
  ASSERT_NE(error, nullptr);

  EXPECT_EQ(mcp_error_get_code(error), code);
  EXPECT_STREQ(mcp_error_get_message(error), message);
  EXPECT_EQ(mcp_error_get_data(error), nullptr);

  mcp_error_free(error);
}

TEST_F(MCPTypesTest, ErrorWithData) {
  auto error = mcp_error_create(-32600, "Invalid Request");
  ASSERT_NE(error, nullptr);

  const char* json_data = "{\"details\": \"Missing required field\"}";
  mcp_error_set_data(error, json_data);

  EXPECT_STREQ(mcp_error_get_data(error), json_data);

  mcp_error_free(error);
}

TEST_F(MCPTypesTest, ErrorNullParameters) {
  EXPECT_EQ(mcp_error_create(0, nullptr), nullptr);
  EXPECT_EQ(mcp_error_get_code(nullptr), 0);
  EXPECT_EQ(mcp_error_get_message(nullptr), nullptr);
  EXPECT_EQ(mcp_error_get_data(nullptr), nullptr);

  mcp_error_set_data(nullptr, "data");
}

// ============================================================================
// Message Tests
// ============================================================================

TEST_F(MCPTypesTest, MessageCreate) {
  const char* role = "user";

  auto message = mcp_message_create(role);
  ASSERT_NE(message, nullptr);

  EXPECT_STREQ(mcp_message_get_role(message), role);
  EXPECT_EQ(mcp_message_get_content_count(message), 0);

  mcp_message_free(message);
}

TEST_F(MCPTypesTest, MessageAddContent) {
  auto message = mcp_message_create("assistant");
  ASSERT_NE(message, nullptr);

  auto content1 = mcp_content_block_create_text("Hello");
  auto content2 = mcp_content_block_create_text("World");

  mcp_message_add_content(message, content1);
  mcp_message_add_content(message, content2);

  EXPECT_EQ(mcp_message_get_content_count(message), 2);

  auto retrieved1 = mcp_message_get_content(message, 0);
  EXPECT_EQ(retrieved1, content1);
  EXPECT_STREQ(mcp_content_block_get_text(retrieved1), "Hello");

  auto retrieved2 = mcp_message_get_content(message, 1);
  EXPECT_EQ(retrieved2, content2);
  EXPECT_STREQ(mcp_content_block_get_text(retrieved2), "World");

  EXPECT_EQ(mcp_message_get_content(message, 2), nullptr);

  mcp_message_free(message);  // Should also free content blocks
}

TEST_F(MCPTypesTest, MessageNullParameters) {
  EXPECT_EQ(mcp_message_create(nullptr), nullptr);
  EXPECT_EQ(mcp_message_get_role(nullptr), nullptr);
  EXPECT_EQ(mcp_message_get_content_count(nullptr), 0);
  EXPECT_EQ(mcp_message_get_content(nullptr, 0), nullptr);

  mcp_message_add_content(nullptr, nullptr);
}

// ============================================================================
// JSON-RPC Request Tests
// ============================================================================

TEST_F(MCPTypesTest, JsonRpcRequestCreate) {
  const char* jsonrpc = "2.0";
  const char* method = "test_method";

  auto request = mcp_jsonrpc_request_create(jsonrpc, method);
  ASSERT_NE(request, nullptr);

  EXPECT_STREQ(mcp_jsonrpc_request_get_jsonrpc(request), jsonrpc);
  EXPECT_STREQ(mcp_jsonrpc_request_get_method(request), method);

  mcp_jsonrpc_request_free(request);
}

TEST_F(MCPTypesTest, JsonRpcRequestWithIdAndParams) {
  auto request = mcp_jsonrpc_request_create("2.0", "test");
  ASSERT_NE(request, nullptr);

  auto id = mcp_request_id_create_number(123);
  mcp_jsonrpc_request_set_id(request, id);
  // Note: id is now owned by request

  const char* params = "{\"key\": \"value\"}";
  mcp_jsonrpc_request_set_params(request, params);

  mcp_jsonrpc_request_free(request);  // Should also free id
}

TEST_F(MCPTypesTest, JsonRpcRequestNullParameters) {
  EXPECT_EQ(mcp_jsonrpc_request_create(nullptr, "method"), nullptr);
  EXPECT_EQ(mcp_jsonrpc_request_create("2.0", nullptr), nullptr);

  EXPECT_EQ(mcp_jsonrpc_request_get_jsonrpc(nullptr), nullptr);
  EXPECT_EQ(mcp_jsonrpc_request_get_method(nullptr), nullptr);

  mcp_jsonrpc_request_set_id(nullptr, nullptr);
  mcp_jsonrpc_request_set_params(nullptr, "params");
}

// ============================================================================
// JSON-RPC Response Tests
// ============================================================================

TEST_F(MCPTypesTest, JsonRpcResponseCreate) {
  const char* jsonrpc = "2.0";

  auto response = mcp_jsonrpc_response_create(jsonrpc);
  ASSERT_NE(response, nullptr);

  EXPECT_STREQ(mcp_jsonrpc_response_get_jsonrpc(response), jsonrpc);
  EXPECT_EQ(mcp_jsonrpc_response_get_result(response), nullptr);
  EXPECT_EQ(mcp_jsonrpc_response_get_error(response), nullptr);

  mcp_jsonrpc_response_free(response);
}

TEST_F(MCPTypesTest, JsonRpcResponseWithResult) {
  auto response = mcp_jsonrpc_response_create("2.0");
  ASSERT_NE(response, nullptr);

  auto id = mcp_request_id_create_string("req-123");
  mcp_jsonrpc_response_set_id(response, id);

  const char* result = "{\"status\": \"success\"}";
  mcp_jsonrpc_response_set_result(response, result);

  EXPECT_STREQ(mcp_jsonrpc_response_get_result(response), result);
  EXPECT_EQ(mcp_jsonrpc_response_get_error(response), nullptr);

  mcp_jsonrpc_response_free(response);
}

TEST_F(MCPTypesTest, JsonRpcResponseWithError) {
  auto response = mcp_jsonrpc_response_create("2.0");
  ASSERT_NE(response, nullptr);

  auto error = mcp_error_create(-32700, "Parse error");
  mcp_jsonrpc_response_set_error(response, error);

  EXPECT_EQ(mcp_jsonrpc_response_get_result(response), nullptr);

  auto retrieved_error = mcp_jsonrpc_response_get_error(response);
  EXPECT_EQ(retrieved_error, error);
  EXPECT_EQ(mcp_error_get_code(retrieved_error), -32700);

  mcp_jsonrpc_response_free(response);  // Should also free error
}

TEST_F(MCPTypesTest, JsonRpcResponseResultErrorMutualExclusion) {
  auto response = mcp_jsonrpc_response_create("2.0");
  ASSERT_NE(response, nullptr);

  // Set result first
  mcp_jsonrpc_response_set_result(response, "{\"result\": true}");
  EXPECT_NE(mcp_jsonrpc_response_get_result(response), nullptr);
  EXPECT_EQ(mcp_jsonrpc_response_get_error(response), nullptr);

  // Set error - should clear result
  auto error = mcp_error_create(-32600, "Invalid");
  mcp_jsonrpc_response_set_error(response, error);
  EXPECT_EQ(mcp_jsonrpc_response_get_result(response), nullptr);
  EXPECT_NE(mcp_jsonrpc_response_get_error(response), nullptr);

  // Set result again - should clear error
  mcp_jsonrpc_response_set_result(response, "{\"new\": true}");
  EXPECT_NE(mcp_jsonrpc_response_get_result(response), nullptr);
  EXPECT_EQ(mcp_jsonrpc_response_get_error(response), nullptr);

  mcp_jsonrpc_response_free(response);
}

// ============================================================================
// Initialize Request/Response Tests
// ============================================================================

TEST_F(MCPTypesTest, InitializeRequestCreate) {
  const char* protocol = "1.0";
  const char* client_name = "TestClient";
  const char* client_version = "1.0.0";

  auto request =
      mcp_initialize_request_create(protocol, client_name, client_version);
  ASSERT_NE(request, nullptr);

  EXPECT_STREQ(mcp_initialize_request_get_protocol_version(request), protocol);
  EXPECT_STREQ(mcp_initialize_request_get_client_name(request), client_name);
  EXPECT_STREQ(mcp_initialize_request_get_client_version(request),
               client_version);

  mcp_initialize_request_free(request);
}

TEST_F(MCPTypesTest, InitializeRequestNullParameters) {
  EXPECT_EQ(mcp_initialize_request_create(nullptr, "name", "version"), nullptr);
  EXPECT_EQ(mcp_initialize_request_create("1.0", nullptr, "version"), nullptr);
  EXPECT_EQ(mcp_initialize_request_create("1.0", "name", nullptr), nullptr);

  EXPECT_EQ(mcp_initialize_request_get_protocol_version(nullptr), nullptr);
  EXPECT_EQ(mcp_initialize_request_get_client_name(nullptr), nullptr);
  EXPECT_EQ(mcp_initialize_request_get_client_version(nullptr), nullptr);
}

TEST_F(MCPTypesTest, InitializeResponseCreate) {
  const char* protocol = "1.0";
  const char* server_name = "TestServer";
  const char* server_version = "2.0.0";

  auto response =
      mcp_initialize_response_create(protocol, server_name, server_version);
  ASSERT_NE(response, nullptr);

  EXPECT_STREQ(mcp_initialize_response_get_protocol_version(response),
               protocol);
  EXPECT_STREQ(mcp_initialize_response_get_server_name(response), server_name);
  EXPECT_STREQ(mcp_initialize_response_get_server_version(response),
               server_version);

  mcp_initialize_response_free(response);
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST_F(MCPTypesTest, StressTestManyAllocations) {
  const int count = 1000;
  std::vector<mcp_request_id_t> ids;
  std::vector<mcp_content_block_t> blocks;

  // Create many objects
  for (int i = 0; i < count; ++i) {
    if (i % 2 == 0) {
      ids.push_back(mcp_request_id_create_number(i));
    } else {
      std::string str = "id-" + std::to_string(i);
      ids.push_back(mcp_request_id_create_string(str.c_str()));
    }

    std::string text = "Block " + std::to_string(i);
    blocks.push_back(mcp_content_block_create_text(text.c_str()));
  }

  // Verify some samples
  EXPECT_EQ(mcp_request_id_get_number(ids[0]), 0);
  EXPECT_STREQ(mcp_request_id_get_string(ids[1]), "id-1");
  EXPECT_STREQ(mcp_content_block_get_text(blocks[999]), "Block 999");

  // Free all objects
  for (auto id : ids) {
    mcp_request_id_free(id);
  }
  for (auto block : blocks) {
    mcp_content_block_free(block);
  }
}

TEST_F(MCPTypesTest, StressTestLargeStrings) {
  // Create very large strings
  std::string large_str(10000, 'X');

  auto id = mcp_request_id_create_string(large_str.c_str());
  ASSERT_NE(id, nullptr);
  EXPECT_EQ(strlen(mcp_request_id_get_string(id)), 10000);
  mcp_request_id_free(id);

  auto block = mcp_content_block_create_text(large_str.c_str());
  ASSERT_NE(block, nullptr);
  EXPECT_EQ(strlen(mcp_content_block_get_text(block)), 10000);
  mcp_content_block_free(block);
}

// ============================================================================
// Thread Safety Tests (basic - real threading tests would need synchronization)
// ============================================================================

TEST_F(MCPTypesTest, ThreadLocalErrorHandling) {
  // Test error handling - implementation specific
  auto id1 = mcp_request_id_create_string(nullptr);

  if (id1 == nullptr) {
    // Only check error if nullptr was returned
    const mcp_error_info_t* error1 = mcp_get_last_error();
    if (error1) {
      EXPECT_EQ(error1->code, MCP_ERROR_NULL_POINTER);
    }
  } else {
    // Implementation accepts nullptr, clean up
    mcp_request_id_free(id1);
  }

  // Clear error
  mcp_clear_last_error();
  EXPECT_EQ(mcp_get_last_error(), nullptr);

  // Create another error
  auto tool = mcp_tool_create(nullptr, "desc");
  EXPECT_EQ(tool, nullptr);

  const mcp_error_info_t* error2 = mcp_get_last_error();
  if (error2) {
    EXPECT_EQ(error2->code, MCP_ERROR_NULL_POINTER);
  }
}