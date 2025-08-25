/**
 * @file test_c_api_json.cc
 * @brief Comprehensive unit tests for MCP C API JSON serialization with RAII
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <cstring>
#include "mcp/c_api/mcp_c_api_json.h"
#include "mcp/c_api/mcp_c_types.h"
#include "mcp/c_api/mcp_c_collections.h"

class MCPJsonTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize any global state if needed
    }
    
    void TearDown() override {
        // Clean up any global state if needed
    }
};

// ============================================================================
// Request ID Tests
// ============================================================================

TEST_F(MCPJsonTest, RequestIdStringToJson) {
    auto id = mcp_request_id_create_string("req-123");
    ASSERT_NE(id, nullptr);
    
    mcp_json_value_t json = mcp_request_id_to_json(&id);
    ASSERT_NE(json, nullptr);
    
    EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_STRING);
    EXPECT_STREQ(mcp_json_get_string(json), "req-123");
    
    mcp_json_free(json);
    mcp_request_id_free(id);
}

TEST_F(MCPJsonTest, RequestIdNumberToJson) {
    auto id = mcp_request_id_create_number(42);
    ASSERT_NE(id, nullptr);
    
    mcp_json_value_t json = mcp_request_id_to_json(&id);
    ASSERT_NE(json, nullptr);
    
    EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_NUMBER);
    EXPECT_EQ(static_cast<int64_t>(mcp_json_get_number(json)), 42);
    
    mcp_json_free(json);
    mcp_request_id_free(id);
}

TEST_F(MCPJsonTest, RequestIdFromJsonString) {
    auto json = mcp_json_create_string("req-456");
    ASSERT_NE(json, nullptr);
    
    auto* id = mcp_request_id_from_json(json);
    ASSERT_NE(id, nullptr);
    
    EXPECT_EQ(mcp_request_id_get_type(*id), MCP_REQUEST_ID_TYPE_STRING);
    EXPECT_STREQ(mcp_request_id_get_string(*id), "req-456");
    
    mcp_request_id_free(*id);
    std::free(id);
    mcp_json_free(json);
}

TEST_F(MCPJsonTest, RequestIdFromJsonNumber) {
    auto json = mcp_json_create_number(99.0);
    ASSERT_NE(json, nullptr);
    
    auto* id = mcp_request_id_from_json(json);
    ASSERT_NE(id, nullptr);
    
    EXPECT_EQ(mcp_request_id_get_type(*id), MCP_REQUEST_ID_TYPE_NUMBER);
    EXPECT_EQ(mcp_request_id_get_number(*id), 99);
    
    mcp_request_id_free(*id);
    std::free(id);
    mcp_json_free(json);
}

TEST_F(MCPJsonTest, RequestIdNullHandling) {
    EXPECT_EQ(mcp_request_id_to_json(nullptr), nullptr);
    EXPECT_EQ(mcp_request_id_from_json(nullptr), nullptr);
}

// ============================================================================
// Progress Token Tests
// ============================================================================

TEST_F(MCPJsonTest, ProgressTokenStringToJson) {
    auto token = mcp_progress_token_create_string("progress-abc");
    ASSERT_NE(token, nullptr);
    
    mcp_json_value_t json = mcp_progress_token_to_json(&token);
    ASSERT_NE(json, nullptr);
    
    EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_STRING);
    EXPECT_STREQ(mcp_json_get_string(json), "progress-abc");
    
    mcp_json_free(json);
    mcp_progress_token_free(token);
}

TEST_F(MCPJsonTest, ProgressTokenNumberToJson) {
    auto token = mcp_progress_token_create_number(1000);
    ASSERT_NE(token, nullptr);
    
    mcp_json_value_t json = mcp_progress_token_to_json(&token);
    ASSERT_NE(json, nullptr);
    
    EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_NUMBER);
    EXPECT_EQ(static_cast<int64_t>(mcp_json_get_number(json)), 1000);
    
    mcp_json_free(json);
    mcp_progress_token_free(token);
}

TEST_F(MCPJsonTest, ProgressTokenFromJson) {
    auto json = mcp_json_create_string("progress-xyz");
    ASSERT_NE(json, nullptr);
    
    auto* token = mcp_progress_token_from_json(json);
    ASSERT_NE(token, nullptr);
    
    EXPECT_EQ(mcp_progress_token_get_type(*token), MCP_PROGRESS_TOKEN_TYPE_STRING);
    EXPECT_STREQ(mcp_progress_token_get_string(*token), "progress-xyz");
    
    mcp_progress_token_free(*token);
    std::free(token);
    mcp_json_free(json);
}

// ============================================================================
// Content Block Tests
// ============================================================================

TEST_F(MCPJsonTest, ContentBlockTextToJson) {
    auto block = mcp_content_block_create_text("Hello, World!");
    ASSERT_NE(block, nullptr);
    
    mcp_json_value_t json = mcp_content_block_to_json(&block);
    ASSERT_NE(json, nullptr);
    
    EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_OBJECT);
    
    auto type_val = mcp_json_object_get(json, "type");
    ASSERT_NE(type_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(type_val), "text");
    
    auto text_val = mcp_json_object_get(json, "text");
    ASSERT_NE(text_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(text_val), "Hello, World!");
    
    mcp_json_free(json);
    mcp_content_block_free(block);
}

TEST_F(MCPJsonTest, ContentBlockImageToJson) {
    auto block = mcp_content_block_create_image("base64data", "image/png");
    ASSERT_NE(block, nullptr);
    
    mcp_json_value_t json = mcp_content_block_to_json(&block);
    ASSERT_NE(json, nullptr);
    
    EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_OBJECT);
    
    auto type_val = mcp_json_object_get(json, "type");
    ASSERT_NE(type_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(type_val), "image");
    
    auto data_val = mcp_json_object_get(json, "data");
    ASSERT_NE(data_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(data_val), "base64data");
    
    auto mime_val = mcp_json_object_get(json, "mimeType");
    ASSERT_NE(mime_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(mime_val), "image/png");
    
    mcp_json_free(json);
    mcp_content_block_free(block);
}

TEST_F(MCPJsonTest, ContentBlockFromJsonText) {
    auto json = mcp_json_create_object();
    mcp_json_object_set(json, "type", mcp_json_create_string("text"));
    mcp_json_object_set(json, "text", mcp_json_create_string("Test content"));
    
    auto* block = mcp_content_block_from_json(json);
    ASSERT_NE(block, nullptr);
    
    EXPECT_EQ(mcp_content_block_get_type(*block), MCP_CONTENT_BLOCK_TYPE_TEXT);
    EXPECT_STREQ(mcp_content_block_get_text(*block), "Test content");
    
    mcp_content_block_free(*block);
    std::free(block);
    mcp_json_free(json);
}

TEST_F(MCPJsonTest, ContentBlockFromJsonImage) {
    auto json = mcp_json_create_object();
    mcp_json_object_set(json, "type", mcp_json_create_string("image"));
    mcp_json_object_set(json, "data", mcp_json_create_string("imagedata"));
    mcp_json_object_set(json, "mimeType", mcp_json_create_string("image/jpeg"));
    
    auto* block = mcp_content_block_from_json(json);
    ASSERT_NE(block, nullptr);
    
    EXPECT_EQ(mcp_content_block_get_type(*block), MCP_CONTENT_BLOCK_TYPE_IMAGE);
    
    const char* data = nullptr;
    const char* mime_type = nullptr;
    mcp_content_block_get_image(*block, &data, &mime_type);
    EXPECT_STREQ(data, "imagedata");
    EXPECT_STREQ(mime_type, "image/jpeg");
    
    mcp_content_block_free(*block);
    std::free(block);
    mcp_json_free(json);
}

// ============================================================================
// Tool Tests
// ============================================================================

TEST_F(MCPJsonTest, ToolToJson) {
    auto tool = mcp_tool_create("calculator", "A simple calculator tool");
    ASSERT_NE(tool, nullptr);
    
    mcp_json_value_t json = mcp_tool_to_json(&tool);
    ASSERT_NE(json, nullptr);
    
    EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_OBJECT);
    
    auto name_val = mcp_json_object_get(json, "name");
    ASSERT_NE(name_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(name_val), "calculator");
    
    auto desc_val = mcp_json_object_get(json, "description");
    ASSERT_NE(desc_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(desc_val), "A simple calculator tool");
    
    mcp_json_free(json);
    mcp_tool_free(tool);
}

TEST_F(MCPJsonTest, ToolFromJson) {
    auto json = mcp_json_create_object();
    mcp_json_object_set(json, "name", mcp_json_create_string("weather"));
    mcp_json_object_set(json, "description", mcp_json_create_string("Get weather info"));
    
    auto* tool = mcp_tool_from_json(json);
    ASSERT_NE(tool, nullptr);
    
    EXPECT_STREQ(mcp_tool_get_name(*tool), "weather");
    EXPECT_STREQ(mcp_tool_get_description(*tool), "Get weather info");
    
    mcp_tool_free(*tool);
    std::free(tool);
    mcp_json_free(json);
}

// ============================================================================
// Prompt Tests
// ============================================================================

TEST_F(MCPJsonTest, PromptToJson) {
    auto prompt = mcp_prompt_create("greeting", "Generate a greeting message");
    ASSERT_NE(prompt, nullptr);
    
    mcp_prompt_add_argument(prompt, "name", "Person's name", MCP_TRUE);
    
    mcp_json_value_t json = mcp_prompt_to_json(&prompt);
    ASSERT_NE(json, nullptr);
    
    EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_OBJECT);
    
    auto name_val = mcp_json_object_get(json, "name");
    ASSERT_NE(name_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(name_val), "greeting");
    
    auto desc_val = mcp_json_object_get(json, "description");
    ASSERT_NE(desc_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(desc_val), "Generate a greeting message");
    
    // Check arguments array exists
    auto args_val = mcp_json_object_get(json, "arguments");
    ASSERT_NE(args_val, nullptr);
    EXPECT_EQ(mcp_json_get_type(args_val), MCP_JSON_TYPE_ARRAY);
    
    mcp_json_free(json);
    mcp_prompt_free(prompt);
}

TEST_F(MCPJsonTest, PromptFromJson) {
    auto json = mcp_json_create_object();
    mcp_json_object_set(json, "name", mcp_json_create_string("summary"));
    mcp_json_object_set(json, "description", mcp_json_create_string("Summarize text"));
    
    // Add arguments array
    auto args = mcp_json_create_array();
    auto arg1 = mcp_json_create_object();
    mcp_json_object_set(arg1, "name", mcp_json_create_string("text"));
    mcp_json_object_set(arg1, "description", mcp_json_create_string("Text to summarize"));
    mcp_json_object_set(arg1, "required", mcp_json_create_bool(MCP_TRUE));
    mcp_json_array_append(args, arg1);
    mcp_json_object_set(json, "arguments", args);
    
    auto* prompt = mcp_prompt_from_json(json);
    ASSERT_NE(prompt, nullptr);
    
    EXPECT_STREQ(mcp_prompt_get_name(*prompt), "summary");
    EXPECT_STREQ(mcp_prompt_get_description(*prompt), "Summarize text");
    EXPECT_EQ(mcp_prompt_get_argument_count(*prompt), 1);
    
    mcp_prompt_free(*prompt);
    std::free(prompt);
    mcp_json_free(json);
}

// ============================================================================
// Message Tests
// ============================================================================

TEST_F(MCPJsonTest, MessageToJson) {
    auto message = mcp_message_create("user");
    ASSERT_NE(message, nullptr);
    
    auto block = mcp_content_block_create_text("Hello!");
    mcp_message_add_content(message, block);
    mcp_content_block_free(block);
    
    mcp_json_value_t json = mcp_message_to_json(&message);
    ASSERT_NE(json, nullptr);
    
    EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_OBJECT);
    
    auto role_val = mcp_json_object_get(json, "role");
    ASSERT_NE(role_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(role_val), "user");
    
    auto content_val = mcp_json_object_get(json, "content");
    ASSERT_NE(content_val, nullptr);
    EXPECT_EQ(mcp_json_get_type(content_val), MCP_JSON_TYPE_ARRAY);
    
    mcp_json_free(json);
    mcp_message_free(message);
}

TEST_F(MCPJsonTest, MessageFromJson) {
    auto json = mcp_json_create_object();
    mcp_json_object_set(json, "role", mcp_json_create_string("assistant"));
    
    // Add content array
    auto content = mcp_json_create_array();
    auto block_json = mcp_json_create_object();
    mcp_json_object_set(block_json, "type", mcp_json_create_string("text"));
    mcp_json_object_set(block_json, "text", mcp_json_create_string("Response text"));
    mcp_json_array_append(content, block_json);
    mcp_json_object_set(json, "content", content);
    
    auto* message = mcp_message_from_json(json);
    ASSERT_NE(message, nullptr);
    
    EXPECT_STREQ(mcp_message_get_role(*message), "assistant");
    EXPECT_EQ(mcp_message_get_content_count(*message), 1);
    
    mcp_message_free(*message);
    std::free(message);
    mcp_json_free(json);
}

// ============================================================================
// Error Tests
// ============================================================================

TEST_F(MCPJsonTest, JsonRpcErrorToJson) {
    auto error = mcp_error_create(-32600, "Invalid Request");
    ASSERT_NE(error, nullptr);
    
    mcp_error_set_data(error, "{\"detail\":\"Missing required field\"}");
    
    mcp_json_value_t json = mcp_jsonrpc_error_to_json(&error);
    ASSERT_NE(json, nullptr);
    
    EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_OBJECT);
    
    auto code_val = mcp_json_object_get(json, "code");
    ASSERT_NE(code_val, nullptr);
    EXPECT_EQ(static_cast<int32_t>(mcp_json_get_number(code_val)), -32600);
    
    auto message_val = mcp_json_object_get(json, "message");
    ASSERT_NE(message_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(message_val), "Invalid Request");
    
    mcp_json_free(json);
    mcp_error_free(error);
}

TEST_F(MCPJsonTest, JsonRpcErrorFromJson) {
    auto json = mcp_json_create_object();
    mcp_json_object_set(json, "code", mcp_json_create_number(-32700));
    mcp_json_object_set(json, "message", mcp_json_create_string("Parse error"));
    mcp_json_object_set(json, "data", mcp_json_create_string("Unexpected token"));
    
    auto* error = mcp_jsonrpc_error_from_json(json);
    ASSERT_NE(error, nullptr);
    
    EXPECT_EQ(mcp_error_get_code(*error), -32700);
    EXPECT_STREQ(mcp_error_get_message(*error), "Parse error");
    
    mcp_error_free(*error);
    std::free(error);
    mcp_json_free(json);
}

// ============================================================================
// JSON-RPC Request Tests
// ============================================================================

TEST_F(MCPJsonTest, JsonRpcRequestToJson) {
    auto request = mcp_jsonrpc_request_create("tools/call");
    ASSERT_NE(request, nullptr);
    
    auto id = mcp_request_id_create_string("req-001");
    mcp_jsonrpc_request_set_id(request, id);
    mcp_request_id_free(id);
    
    auto params = mcp_json_create_object();
    mcp_json_object_set(params, "tool", mcp_json_create_string("calculator"));
    mcp_jsonrpc_request_set_params(request, params);
    
    mcp_json_value_t json = mcp_jsonrpc_request_to_json(&request);
    ASSERT_NE(json, nullptr);
    
    EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_OBJECT);
    
    auto jsonrpc_val = mcp_json_object_get(json, "jsonrpc");
    ASSERT_NE(jsonrpc_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(jsonrpc_val), "2.0");
    
    auto method_val = mcp_json_object_get(json, "method");
    ASSERT_NE(method_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(method_val), "tools/call");
    
    auto id_val = mcp_json_object_get(json, "id");
    ASSERT_NE(id_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(id_val), "req-001");
    
    auto params_val = mcp_json_object_get(json, "params");
    ASSERT_NE(params_val, nullptr);
    
    mcp_json_free(json);
    mcp_jsonrpc_request_free(request);
}

TEST_F(MCPJsonTest, JsonRpcRequestFromJson) {
    auto json = mcp_json_create_object();
    mcp_json_object_set(json, "jsonrpc", mcp_json_create_string("2.0"));
    mcp_json_object_set(json, "method", mcp_json_create_string("initialize"));
    mcp_json_object_set(json, "id", mcp_json_create_number(1));
    
    auto params = mcp_json_create_object();
    mcp_json_object_set(params, "protocolVersion", mcp_json_create_string("1.0"));
    mcp_json_object_set(json, "params", params);
    
    auto* request = mcp_jsonrpc_request_from_json(json);
    ASSERT_NE(request, nullptr);
    
    EXPECT_STREQ(mcp_jsonrpc_request_get_method(*request), "initialize");
    
    auto id = mcp_jsonrpc_request_get_id(*request);
    ASSERT_NE(id, nullptr);
    EXPECT_EQ(mcp_request_id_get_type(id), MCP_REQUEST_ID_TYPE_NUMBER);
    EXPECT_EQ(mcp_request_id_get_number(id), 1);
    
    mcp_jsonrpc_request_free(*request);
    std::free(request);
    mcp_json_free(json);
}

// ============================================================================
// JSON-RPC Response Tests
// ============================================================================

TEST_F(MCPJsonTest, JsonRpcResponseSuccessToJson) {
    auto response = mcp_jsonrpc_response_create();
    ASSERT_NE(response, nullptr);
    
    auto id = mcp_request_id_create_number(42);
    mcp_jsonrpc_response_set_id(response, id);
    mcp_request_id_free(id);
    
    auto result = mcp_json_create_object();
    mcp_json_object_set(result, "success", mcp_json_create_bool(MCP_TRUE));
    mcp_jsonrpc_response_set_result(response, result);
    
    mcp_json_value_t json = mcp_jsonrpc_response_to_json(&response);
    ASSERT_NE(json, nullptr);
    
    EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_OBJECT);
    
    auto jsonrpc_val = mcp_json_object_get(json, "jsonrpc");
    ASSERT_NE(jsonrpc_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(jsonrpc_val), "2.0");
    
    auto id_val = mcp_json_object_get(json, "id");
    ASSERT_NE(id_val, nullptr);
    EXPECT_EQ(static_cast<int>(mcp_json_get_number(id_val)), 42);
    
    auto result_val = mcp_json_object_get(json, "result");
    ASSERT_NE(result_val, nullptr);
    
    mcp_json_free(json);
    mcp_jsonrpc_response_free(response);
}

TEST_F(MCPJsonTest, JsonRpcResponseErrorToJson) {
    auto response = mcp_jsonrpc_response_create();
    ASSERT_NE(response, nullptr);
    
    auto id = mcp_request_id_create_string("err-req");
    mcp_jsonrpc_response_set_id(response, id);
    mcp_request_id_free(id);
    
    auto error = mcp_error_create(-32601, "Method not found");
    mcp_jsonrpc_response_set_error(response, error);
    mcp_error_free(error);
    
    mcp_json_value_t json = mcp_jsonrpc_response_to_json(&response);
    ASSERT_NE(json, nullptr);
    
    EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_OBJECT);
    
    auto error_val = mcp_json_object_get(json, "error");
    ASSERT_NE(error_val, nullptr);
    
    auto code_val = mcp_json_object_get(error_val, "code");
    ASSERT_NE(code_val, nullptr);
    EXPECT_EQ(static_cast<int32_t>(mcp_json_get_number(code_val)), -32601);
    
    mcp_json_free(json);
    mcp_jsonrpc_response_free(response);
}

// ============================================================================
// JSON-RPC Notification Tests
// ============================================================================

TEST_F(MCPJsonTest, JsonRpcNotificationToJson) {
    auto notification = mcp_jsonrpc_notification_create("progress/update");
    ASSERT_NE(notification, nullptr);
    
    auto params = mcp_json_create_object();
    mcp_json_object_set(params, "progress", mcp_json_create_number(50));
    mcp_jsonrpc_notification_set_params(notification, params);
    
    mcp_json_value_t json = mcp_jsonrpc_notification_to_json(&notification);
    ASSERT_NE(json, nullptr);
    
    EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_OBJECT);
    
    auto jsonrpc_val = mcp_json_object_get(json, "jsonrpc");
    ASSERT_NE(jsonrpc_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(jsonrpc_val), "2.0");
    
    auto method_val = mcp_json_object_get(json, "method");
    ASSERT_NE(method_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(method_val), "progress/update");
    
    auto params_val = mcp_json_object_get(json, "params");
    ASSERT_NE(params_val, nullptr);
    
    // Notifications should NOT have an id field
    auto id_val = mcp_json_object_get(json, "id");
    EXPECT_EQ(id_val, nullptr);
    
    mcp_json_free(json);
    mcp_jsonrpc_notification_free(notification);
}

TEST_F(MCPJsonTest, JsonRpcNotificationFromJson) {
    auto json = mcp_json_create_object();
    mcp_json_object_set(json, "jsonrpc", mcp_json_create_string("2.0"));
    mcp_json_object_set(json, "method", mcp_json_create_string("cancelled"));
    
    auto params = mcp_json_create_object();
    mcp_json_object_set(params, "requestId", mcp_json_create_string("req-123"));
    mcp_json_object_set(json, "params", params);
    
    auto* notification = mcp_jsonrpc_notification_from_json(json);
    ASSERT_NE(notification, nullptr);
    
    EXPECT_STREQ(mcp_jsonrpc_notification_get_method(*notification), "cancelled");
    
    auto params_val = mcp_jsonrpc_notification_get_params(*notification);
    ASSERT_NE(params_val, nullptr);
    
    mcp_jsonrpc_notification_free(*notification);
    std::free(notification);
    mcp_json_free(json);
}

// ============================================================================
// Initialize Request/Response Tests
// ============================================================================

TEST_F(MCPJsonTest, InitializeRequestToJson) {
    auto request = mcp_initialize_request_create("1.0");
    ASSERT_NE(request, nullptr);
    
    auto info = mcp_implementation_create("TestClient", "1.0.0");
    mcp_initialize_request_set_client_info(request, info);
    mcp_implementation_free(info);
    
    auto caps = mcp_client_capabilities_create();
    mcp_client_capabilities_set_roots(caps, MCP_TRUE);
    mcp_initialize_request_set_capabilities(request, caps);
    mcp_client_capabilities_free(caps);
    
    mcp_json_value_t json = mcp_initialize_request_to_json(&request);
    ASSERT_NE(json, nullptr);
    
    EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_OBJECT);
    
    auto version_val = mcp_json_object_get(json, "protocolVersion");
    ASSERT_NE(version_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(version_val), "1.0");
    
    auto info_val = mcp_json_object_get(json, "clientInfo");
    ASSERT_NE(info_val, nullptr);
    
    auto caps_val = mcp_json_object_get(json, "capabilities");
    ASSERT_NE(caps_val, nullptr);
    
    mcp_json_free(json);
    mcp_initialize_request_free(request);
}

TEST_F(MCPJsonTest, InitializeResultToJson) {
    auto result = mcp_initialize_result_create("1.0");
    ASSERT_NE(result, nullptr);
    
    auto info = mcp_implementation_create("TestServer", "2.0.0");
    mcp_initialize_result_set_server_info(result, info);
    mcp_implementation_free(info);
    
    auto caps = mcp_server_capabilities_create();
    mcp_server_capabilities_set_tools(caps, MCP_TRUE);
    mcp_server_capabilities_set_prompts(caps, MCP_TRUE);
    mcp_initialize_result_set_capabilities(result, caps);
    mcp_server_capabilities_free(caps);
    
    mcp_json_value_t json = mcp_initialize_result_to_json(&result);
    ASSERT_NE(json, nullptr);
    
    EXPECT_EQ(mcp_json_get_type(json), MCP_JSON_TYPE_OBJECT);
    
    auto version_val = mcp_json_object_get(json, "protocolVersion");
    ASSERT_NE(version_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(version_val), "1.0");
    
    auto info_val = mcp_json_object_get(json, "serverInfo");
    ASSERT_NE(info_val, nullptr);
    
    auto caps_val = mcp_json_object_get(json, "capabilities");
    ASSERT_NE(caps_val, nullptr);
    
    mcp_json_free(json);
    mcp_initialize_result_free(result);
}

// ============================================================================
// Other Type Conversion Tests
// ============================================================================

TEST_F(MCPJsonTest, RoleConversion) {
    auto user_json = mcp_role_to_json(MCP_ROLE_USER);
    ASSERT_NE(user_json, nullptr);
    EXPECT_STREQ(mcp_json_get_string(user_json), "user");
    EXPECT_EQ(mcp_role_from_json(user_json), MCP_ROLE_USER);
    mcp_json_free(user_json);
    
    auto assistant_json = mcp_role_to_json(MCP_ROLE_ASSISTANT);
    ASSERT_NE(assistant_json, nullptr);
    EXPECT_STREQ(mcp_json_get_string(assistant_json), "assistant");
    EXPECT_EQ(mcp_role_from_json(assistant_json), MCP_ROLE_ASSISTANT);
    mcp_json_free(assistant_json);
}

TEST_F(MCPJsonTest, LoggingLevelConversion) {
    auto debug_json = mcp_logging_level_to_json(MCP_LOGGING_LEVEL_DEBUG);
    ASSERT_NE(debug_json, nullptr);
    EXPECT_STREQ(mcp_json_get_string(debug_json), "debug");
    EXPECT_EQ(mcp_logging_level_from_json(debug_json), MCP_LOGGING_LEVEL_DEBUG);
    mcp_json_free(debug_json);
    
    auto error_json = mcp_logging_level_to_json(MCP_LOGGING_LEVEL_ERROR);
    ASSERT_NE(error_json, nullptr);
    EXPECT_STREQ(mcp_json_get_string(error_json), "error");
    EXPECT_EQ(mcp_logging_level_from_json(error_json), MCP_LOGGING_LEVEL_ERROR);
    mcp_json_free(error_json);
}

TEST_F(MCPJsonTest, ResourceToJson) {
    auto resource = mcp_resource_create("file:///path/to/file.txt");
    mcp_resource_set_name(resource, "file.txt");
    mcp_resource_set_description(resource, "A text file");
    mcp_resource_set_mime_type(resource, "text/plain");
    
    mcp_json_value_t json = mcp_resource_to_json(&resource);
    ASSERT_NE(json, nullptr);
    
    auto uri_val = mcp_json_object_get(json, "uri");
    ASSERT_NE(uri_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(uri_val), "file:///path/to/file.txt");
    
    auto name_val = mcp_json_object_get(json, "name");
    ASSERT_NE(name_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(name_val), "file.txt");
    
    mcp_json_free(json);
    mcp_resource_free(resource);
}

TEST_F(MCPJsonTest, ImplementationToJson) {
    auto impl = mcp_implementation_create("MyApp", "3.0.0");
    
    mcp_json_value_t json = mcp_implementation_to_json(&impl);
    ASSERT_NE(json, nullptr);
    
    auto name_val = mcp_json_object_get(json, "name");
    ASSERT_NE(name_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(name_val), "MyApp");
    
    auto version_val = mcp_json_object_get(json, "version");
    ASSERT_NE(version_val, nullptr);
    EXPECT_STREQ(mcp_json_get_string(version_val), "3.0.0");
    
    mcp_json_free(json);
    mcp_implementation_free(impl);
}

TEST_F(MCPJsonTest, ClientCapabilitiesToJson) {
    auto caps = mcp_client_capabilities_create();
    mcp_client_capabilities_set_roots(caps, MCP_TRUE);
    mcp_client_capabilities_set_sampling(caps, MCP_TRUE);
    
    mcp_json_value_t json = mcp_client_capabilities_to_json(&caps);
    ASSERT_NE(json, nullptr);
    
    auto roots_val = mcp_json_object_get(json, "roots");
    ASSERT_NE(roots_val, nullptr);
    
    auto sampling_val = mcp_json_object_get(json, "sampling");
    ASSERT_NE(sampling_val, nullptr);
    
    mcp_json_free(json);
    mcp_client_capabilities_free(caps);
}

TEST_F(MCPJsonTest, ServerCapabilitiesToJson) {
    auto caps = mcp_server_capabilities_create();
    mcp_server_capabilities_set_tools(caps, MCP_TRUE);
    mcp_server_capabilities_set_prompts(caps, MCP_TRUE);
    mcp_server_capabilities_set_resources(caps, MCP_TRUE);
    mcp_server_capabilities_set_logging(caps, MCP_TRUE);
    
    mcp_json_value_t json = mcp_server_capabilities_to_json(&caps);
    ASSERT_NE(json, nullptr);
    
    auto tools_val = mcp_json_object_get(json, "tools");
    ASSERT_NE(tools_val, nullptr);
    
    auto prompts_val = mcp_json_object_get(json, "prompts");
    ASSERT_NE(prompts_val, nullptr);
    
    auto resources_val = mcp_json_object_get(json, "resources");
    ASSERT_NE(resources_val, nullptr);
    
    auto logging_val = mcp_json_object_get(json, "logging");
    ASSERT_NE(logging_val, nullptr);
    
    mcp_json_free(json);
    mcp_server_capabilities_free(caps);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(MCPJsonTest, ThreadSafetyBasic) {
    const int num_threads = 10;
    const int iterations = 100;
    
    auto thread_func = [iterations]() {
        for (int i = 0; i < iterations; ++i) {
            // Create and serialize various types
            auto id = mcp_request_id_create_number(i);
            auto json = mcp_request_id_to_json(&id);
            mcp_json_free(json);
            mcp_request_id_free(id);
            
            auto token = mcp_progress_token_create_string("progress");
            json = mcp_progress_token_to_json(&token);
            mcp_json_free(json);
            mcp_progress_token_free(token);
            
            auto block = mcp_content_block_create_text("test");
            json = mcp_content_block_to_json(&block);
            mcp_json_free(json);
            mcp_content_block_free(block);
        }
    };
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(thread_func);
    }
    
    for (auto& t : threads) {
        t.join();
    }
}

// ============================================================================
// Memory Management Tests
// ============================================================================

TEST_F(MCPJsonTest, MemoryManagementRAII) {
    // Test RAII tracking with various JSON operations
    for (int i = 0; i < 100; ++i) {
        auto json = mcp_json_create_object();
        
        // Build complex nested structure
        auto nested = mcp_json_create_object();
        mcp_json_object_set(nested, "key1", mcp_json_create_string("value1"));
        mcp_json_object_set(nested, "key2", mcp_json_create_number(42));
        
        auto array = mcp_json_create_array();
        mcp_json_array_append(array, mcp_json_create_string("item1"));
        mcp_json_array_append(array, mcp_json_create_string("item2"));
        
        mcp_json_object_set(json, "nested", nested);
        mcp_json_object_set(json, "array", array);
        
        // Convert to/from various types
        auto* request_id = mcp_request_id_from_json(mcp_json_create_number(i));
        if (request_id) {
            auto id_json = mcp_request_id_to_json(request_id);
            mcp_json_free(id_json);
            mcp_request_id_free(*request_id);
            std::free(request_id);
        }
        
        mcp_json_free(json);
    }
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST_F(MCPJsonTest, EdgeCasesEmptyStrings) {
    auto id = mcp_request_id_create_string("");
    auto json = mcp_request_id_to_json(&id);
    ASSERT_NE(json, nullptr);
    EXPECT_STREQ(mcp_json_get_string(json), "");
    mcp_json_free(json);
    mcp_request_id_free(id);
    
    auto block = mcp_content_block_create_text("");
    json = mcp_content_block_to_json(&block);
    ASSERT_NE(json, nullptr);
    auto text_val = mcp_json_object_get(json, "text");
    EXPECT_STREQ(mcp_json_get_string(text_val), "");
    mcp_json_free(json);
    mcp_content_block_free(block);
}

TEST_F(MCPJsonTest, EdgeCasesLargeNumbers) {
    int64_t large_num = 9223372036854775807LL; // INT64_MAX
    auto id = mcp_request_id_create_number(large_num);
    auto json = mcp_request_id_to_json(&id);
    ASSERT_NE(json, nullptr);
    EXPECT_EQ(static_cast<int64_t>(mcp_json_get_number(json)), large_num);
    mcp_json_free(json);
    mcp_request_id_free(id);
}

TEST_F(MCPJsonTest, EdgeCasesInvalidJson) {
    // Test with wrong JSON types
    auto string_json = mcp_json_create_string("not an object");
    EXPECT_EQ(mcp_content_block_from_json(string_json), nullptr);
    EXPECT_EQ(mcp_tool_from_json(string_json), nullptr);
    EXPECT_EQ(mcp_prompt_from_json(string_json), nullptr);
    mcp_json_free(string_json);
    
    // Test with missing required fields
    auto incomplete_json = mcp_json_create_object();
    // Missing "type" field for content block
    EXPECT_EQ(mcp_content_block_from_json(incomplete_json), nullptr);
    mcp_json_free(incomplete_json);
}

// ============================================================================
// JSON Stringify Tests
// ============================================================================

TEST_F(MCPJsonTest, JsonStringifyBasicTypes) {
    auto null_json = mcp_json_create_null();
    char* str = mcp_json_stringify(null_json);
    ASSERT_NE(str, nullptr);
    EXPECT_STREQ(str, "null");
    std::free(str);
    mcp_json_free(null_json);
    
    auto bool_json = mcp_json_create_bool(MCP_TRUE);
    str = mcp_json_stringify(bool_json);
    ASSERT_NE(str, nullptr);
    EXPECT_STREQ(str, "true");
    std::free(str);
    mcp_json_free(bool_json);
    
    auto num_json = mcp_json_create_number(42.5);
    str = mcp_json_stringify(num_json);
    ASSERT_NE(str, nullptr);
    // Note: Exact format may vary, just check it contains the number
    EXPECT_NE(std::strstr(str, "42"), nullptr);
    std::free(str);
    mcp_json_free(num_json);
    
    auto string_json = mcp_json_create_string("hello");
    str = mcp_json_stringify(string_json);
    ASSERT_NE(str, nullptr);
    EXPECT_STREQ(str, "\"hello\"");
    std::free(str);
    mcp_json_free(string_json);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(MCPJsonTest, IntegrationFullRequestResponse) {
    // Create an initialize request
    auto request = mcp_jsonrpc_request_create("initialize");
    auto id = mcp_request_id_create_number(1);
    mcp_jsonrpc_request_set_id(request, id);
    mcp_request_id_free(id);
    
    auto params = mcp_json_create_object();
    mcp_json_object_set(params, "protocolVersion", mcp_json_create_string("1.0"));
    
    auto client_info = mcp_json_create_object();
    mcp_json_object_set(client_info, "name", mcp_json_create_string("TestClient"));
    mcp_json_object_set(client_info, "version", mcp_json_create_string("1.0.0"));
    mcp_json_object_set(params, "clientInfo", client_info);
    
    mcp_jsonrpc_request_set_params(request, params);
    
    // Serialize to JSON
    auto request_json = mcp_jsonrpc_request_to_json(&request);
    ASSERT_NE(request_json, nullptr);
    
    // Deserialize back
    auto* request2 = mcp_jsonrpc_request_from_json(request_json);
    ASSERT_NE(request2, nullptr);
    
    EXPECT_STREQ(mcp_jsonrpc_request_get_method(*request2), "initialize");
    
    // Create a response
    auto response = mcp_jsonrpc_response_create();
    auto response_id = mcp_request_id_create_number(1);
    mcp_jsonrpc_response_set_id(response, response_id);
    mcp_request_id_free(response_id);
    
    auto result = mcp_json_create_object();
    mcp_json_object_set(result, "protocolVersion", mcp_json_create_string("1.0"));
    
    auto server_info = mcp_json_create_object();
    mcp_json_object_set(server_info, "name", mcp_json_create_string("TestServer"));
    mcp_json_object_set(server_info, "version", mcp_json_create_string("2.0.0"));
    mcp_json_object_set(result, "serverInfo", server_info);
    
    mcp_jsonrpc_response_set_result(response, result);
    
    // Serialize response to JSON
    auto response_json = mcp_jsonrpc_response_to_json(&response);
    ASSERT_NE(response_json, nullptr);
    
    // Clean up
    mcp_json_free(request_json);
    mcp_json_free(response_json);
    mcp_jsonrpc_request_free(request);
    mcp_jsonrpc_request_free(*request2);
    std::free(request2);
    mcp_jsonrpc_response_free(response);
}