#include <gtest/gtest.h>
#include "mcp/c_api/mcp_c_types.h"
#include "mcp/c_api/mcp_c_types_api.h"
#include <cstring>

class CApiTypesSimpleTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize FFI system if needed
    }
    
    void TearDown() override {
        // Cleanup
    }
};

// Test RequestID types
TEST_F(CApiTypesSimpleTest, RequestIDString) {
    const char* test_str = "request-123";
    mcp_request_id_t id = mcp_request_id_create_string(test_str);
    ASSERT_NE(id, nullptr);
    
    EXPECT_EQ(mcp_request_id_get_type(id), MCP_REQUEST_ID_TYPE_STRING);
    EXPECT_STREQ(mcp_request_id_get_string(id), test_str);
    EXPECT_EQ(mcp_request_id_get_number(id), 0);
    
    mcp_request_id_free(id);
}

TEST_F(CApiTypesSimpleTest, RequestIDNumber) {
    int64_t test_num = 42;
    mcp_request_id_t id = mcp_request_id_create_number(test_num);
    ASSERT_NE(id, nullptr);
    
    EXPECT_EQ(mcp_request_id_get_type(id), MCP_REQUEST_ID_TYPE_NUMBER);
    EXPECT_EQ(mcp_request_id_get_number(id), test_num);
    EXPECT_EQ(mcp_request_id_get_string(id), nullptr);
    
    mcp_request_id_free(id);
}

// Test ProgressToken types
TEST_F(CApiTypesSimpleTest, ProgressTokenString) {
    const char* test_str = "progress-456";
    mcp_progress_token_t token = mcp_progress_token_create_string(test_str);
    ASSERT_NE(token, nullptr);
    
    EXPECT_EQ(mcp_progress_token_get_type(token), MCP_PROGRESS_TOKEN_TYPE_STRING);
    EXPECT_STREQ(mcp_progress_token_get_string(token), test_str);
    
    mcp_progress_token_free(token);
}

TEST_F(CApiTypesSimpleTest, ProgressTokenNumber) {
    int64_t test_num = 999;
    mcp_progress_token_t token = mcp_progress_token_create_number(test_num);
    ASSERT_NE(token, nullptr);
    
    EXPECT_EQ(mcp_progress_token_get_type(token), MCP_PROGRESS_TOKEN_TYPE_NUMBER);
    EXPECT_EQ(mcp_progress_token_get_number(token), test_num);
    
    mcp_progress_token_free(token);
}

// Test ContentBlock types
TEST_F(CApiTypesSimpleTest, TextContentBlock) {
    const char* test_text = "Hello, world!";
    mcp_content_block_t block = mcp_content_block_create_text(test_text);
    ASSERT_NE(block, nullptr);
    
    EXPECT_EQ(mcp_content_block_get_type(block), MCP_CONTENT_BLOCK_TYPE_TEXT);
    EXPECT_STREQ(mcp_content_block_get_text(block), test_text);
    
    mcp_content_block_free(block);
}

TEST_F(CApiTypesSimpleTest, ImageContentBlock) {
    const char* data = "base64imagedata";
    const char* mime_type = "image/png";
    mcp_content_block_t block = mcp_content_block_create_image(data, mime_type);
    ASSERT_NE(block, nullptr);
    
    EXPECT_EQ(mcp_content_block_get_type(block), MCP_CONTENT_BLOCK_TYPE_IMAGE);
    
    const char* out_data = nullptr;
    const char* out_mime = nullptr;
    EXPECT_EQ(mcp_content_block_get_image(block, &out_data, &out_mime), MCP_TRUE);
    EXPECT_STREQ(out_data, data);
    EXPECT_STREQ(out_mime, mime_type);
    
    mcp_content_block_free(block);
}

// Test Tool Input Schema
TEST_F(CApiTypesSimpleTest, ToolInputSchema) {
    mcp_tool_input_schema_t schema = mcp_tool_input_schema_create();
    ASSERT_NE(schema, nullptr);
    
    mcp_tool_input_schema_set_type(schema, "object");
    
    // Add properties
    mcp_tool_input_schema_add_property(schema, "name", "{ \"type\": \"string\" }");
    mcp_tool_input_schema_add_property(schema, "age", "{ \"type\": \"number\" }");
    
    // Add required fields
    mcp_tool_input_schema_add_required(schema, "name");
    
    EXPECT_STREQ(mcp_tool_input_schema_get_type(schema), "object");
    EXPECT_EQ(mcp_tool_input_schema_get_property_count(schema), 2);
    EXPECT_EQ(mcp_tool_input_schema_get_required_count(schema), 1);
    
    mcp_tool_input_schema_free(schema);
}

// Test Tool
TEST_F(CApiTypesSimpleTest, Tool) {
    mcp_tool_t tool = mcp_tool_create("calculator", "A simple calculator");
    ASSERT_NE(tool, nullptr);
    
    EXPECT_STREQ(mcp_tool_get_name(tool), "calculator");
    EXPECT_STREQ(mcp_tool_get_description(tool), "A simple calculator");
    
    // Create and set input schema
    mcp_tool_input_schema_t schema = mcp_tool_input_schema_create();
    mcp_tool_input_schema_set_type(schema, "object");
    mcp_tool_set_input_schema(tool, schema);
    
    // Schema is now owned by tool, no need to free separately
    
    mcp_tool_free(tool);
}

// Test Prompt
TEST_F(CApiTypesSimpleTest, Prompt) {
    mcp_prompt_t prompt = mcp_prompt_create("test_prompt", "A test prompt");
    ASSERT_NE(prompt, nullptr);
    
    EXPECT_STREQ(mcp_prompt_get_name(prompt), "test_prompt");
    EXPECT_STREQ(mcp_prompt_get_description(prompt), "A test prompt");
    
    // Add argument
    mcp_prompt_add_argument(prompt, "input", "User input", MCP_TRUE);
    EXPECT_EQ(mcp_prompt_get_argument_count(prompt), 1);
    
    mcp_prompt_free(prompt);
}

// Test Error
TEST_F(CApiTypesSimpleTest, Error) {
    mcp_error_t error = mcp_error_create(-32600, "Invalid Request");
    ASSERT_NE(error, nullptr);
    
    EXPECT_EQ(mcp_error_get_code(error), -32600);
    EXPECT_STREQ(mcp_error_get_message(error), "Invalid Request");
    
    mcp_error_set_data(error, "{ \"details\": \"Missing required field\" }");
    EXPECT_NE(mcp_error_get_data(error), nullptr);
    
    mcp_error_free(error);
}

// Test Message
TEST_F(CApiTypesSimpleTest, Message) {
    mcp_message_t msg = mcp_message_create("user");
    ASSERT_NE(msg, nullptr);
    
    EXPECT_STREQ(mcp_message_get_role(msg), "user");
    
    // Add text content
    mcp_content_block_t content = mcp_content_block_create_text("Hello!");
    mcp_message_add_content(msg, content);
    // content is now owned by message
    
    EXPECT_EQ(mcp_message_get_content_count(msg), 1);
    
    mcp_message_free(msg);
}

// Test JSON-RPC Request
TEST_F(CApiTypesSimpleTest, JSONRPCRequest) {
    mcp_jsonrpc_request_t req = mcp_jsonrpc_request_create("2.0", "test_method");
    ASSERT_NE(req, nullptr);
    
    EXPECT_STREQ(mcp_jsonrpc_request_get_jsonrpc(req), "2.0");
    EXPECT_STREQ(mcp_jsonrpc_request_get_method(req), "test_method");
    
    // Set ID
    mcp_request_id_t id = mcp_request_id_create_string("req-1");
    mcp_jsonrpc_request_set_id(req, id);
    // id is now owned by request
    
    // Set params
    mcp_jsonrpc_request_set_params(req, "{ \"key\": \"value\" }");
    
    mcp_jsonrpc_request_free(req);
}

// Test JSON-RPC Response
TEST_F(CApiTypesSimpleTest, JSONRPCResponse) {
    mcp_jsonrpc_response_t resp = mcp_jsonrpc_response_create("2.0");
    ASSERT_NE(resp, nullptr);
    
    EXPECT_STREQ(mcp_jsonrpc_response_get_jsonrpc(resp), "2.0");
    
    // Set ID
    mcp_request_id_t id = mcp_request_id_create_number(123);
    mcp_jsonrpc_response_set_id(resp, id);
    // id is now owned by response
    
    // Set result
    mcp_jsonrpc_response_set_result(resp, "{ \"status\": \"success\" }");
    
    EXPECT_NE(mcp_jsonrpc_response_get_result(resp), nullptr);
    EXPECT_EQ(mcp_jsonrpc_response_get_error(resp), nullptr);
    
    mcp_jsonrpc_response_free(resp);
}

// Test JSON-RPC Error Response
TEST_F(CApiTypesSimpleTest, JSONRPCErrorResponse) {
    mcp_jsonrpc_response_t resp = mcp_jsonrpc_response_create("2.0");
    ASSERT_NE(resp, nullptr);
    
    // Set ID
    mcp_request_id_t id = mcp_request_id_create_string("error-req");
    mcp_jsonrpc_response_set_id(resp, id);
    
    // Set error
    mcp_error_t error = mcp_error_create(-32700, "Parse error");
    mcp_jsonrpc_response_set_error(resp, error);
    // error is now owned by response
    
    EXPECT_EQ(mcp_jsonrpc_response_get_result(resp), nullptr);
    EXPECT_NE(mcp_jsonrpc_response_get_error(resp), nullptr);
    
    mcp_jsonrpc_response_free(resp);
}

// Test Initialize Request
TEST_F(CApiTypesSimpleTest, InitializeRequest) {
    mcp_initialize_request_t req = mcp_initialize_request_create("1.0", "TestClient", "1.0.0");
    ASSERT_NE(req, nullptr);
    
    EXPECT_STREQ(mcp_initialize_request_get_protocol_version(req), "1.0");
    EXPECT_STREQ(mcp_initialize_request_get_client_name(req), "TestClient");
    EXPECT_STREQ(mcp_initialize_request_get_client_version(req), "1.0.0");
    
    mcp_initialize_request_free(req);
}

// Test Initialize Response
TEST_F(CApiTypesSimpleTest, InitializeResponse) {
    mcp_initialize_response_t resp = mcp_initialize_response_create("1.0", "TestServer", "2.0.0");
    ASSERT_NE(resp, nullptr);
    
    EXPECT_STREQ(mcp_initialize_response_get_protocol_version(resp), "1.0");
    EXPECT_STREQ(mcp_initialize_response_get_server_name(resp), "TestServer");
    EXPECT_STREQ(mcp_initialize_response_get_server_version(resp), "2.0.0");
    
    mcp_initialize_response_free(resp);
}

// Test memory cleanup with multiple allocations
TEST_F(CApiTypesSimpleTest, MultipleAllocationsCleanup) {
    // Create multiple objects
    mcp_request_id_t id1 = mcp_request_id_create_string("id1");
    mcp_request_id_t id2 = mcp_request_id_create_number(100);
    mcp_progress_token_t token1 = mcp_progress_token_create_string("token1");
    mcp_content_block_t block1 = mcp_content_block_create_text("text1");
    mcp_tool_t tool1 = mcp_tool_create("tool1", "desc1");
    
    // Verify all created successfully
    ASSERT_NE(id1, nullptr);
    ASSERT_NE(id2, nullptr);
    ASSERT_NE(token1, nullptr);
    ASSERT_NE(block1, nullptr);
    ASSERT_NE(tool1, nullptr);
    
    // Clean up all
    mcp_request_id_free(id1);
    mcp_request_id_free(id2);
    mcp_progress_token_free(token1);
    mcp_content_block_free(block1);
    mcp_tool_free(tool1);
    
    // Test passes if no memory leaks or crashes
}

// Test null pointer handling
TEST_F(CApiTypesSimpleTest, NullPointerHandling) {
    // These should handle null gracefully
    mcp_request_id_free(nullptr);
    mcp_progress_token_free(nullptr);
    mcp_content_block_free(nullptr);
    mcp_tool_free(nullptr);
    mcp_prompt_free(nullptr);
    mcp_error_free(nullptr);
    mcp_message_free(nullptr);
    
    // Getting from null should return safe values
    EXPECT_EQ(mcp_request_id_get_type(nullptr), MCP_REQUEST_ID_TYPE_STRING);
    EXPECT_EQ(mcp_request_id_get_string(nullptr), nullptr);
    EXPECT_EQ(mcp_request_id_get_number(nullptr), 0);
}