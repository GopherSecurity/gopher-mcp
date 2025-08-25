/**
 * @file mcp_c_types_api.h
 * @brief C API functions for MCP types manipulation
 *
 * This header provides the function declarations for creating, accessing,
 * and destroying MCP types. These functions operate on the opaque types
 * defined in mcp_c_types.h.
 */

#ifndef MCP_C_TYPES_API_H
#define MCP_C_TYPES_API_H

#include "mcp_c_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Request ID Functions (variant<string, int>)
 * ============================================================================ */

MCP_API mcp_request_id_t mcp_request_id_create_string(const char* str) MCP_NOEXCEPT;
MCP_API mcp_request_id_t mcp_request_id_create_number(int64_t num) MCP_NOEXCEPT;
MCP_API void mcp_request_id_free(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_request_id_is_string(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_request_id_is_number(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API mcp_request_id_type_t mcp_request_id_get_type(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API const char* mcp_request_id_get_string(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API int64_t mcp_request_id_get_number(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API mcp_request_id_t mcp_request_id_clone(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_request_id_is_valid(mcp_request_id_t id) MCP_NOEXCEPT;

/* ============================================================================
 * Progress Token Functions (variant<string, int>)
 * ============================================================================ */

MCP_API mcp_progress_token_t mcp_progress_token_create_string(const char* str) MCP_NOEXCEPT;
MCP_API mcp_progress_token_t mcp_progress_token_create_number(int64_t num) MCP_NOEXCEPT;
MCP_API void mcp_progress_token_free(mcp_progress_token_t token) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_progress_token_is_string(mcp_progress_token_t token) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_progress_token_is_number(mcp_progress_token_t token) MCP_NOEXCEPT;
MCP_API mcp_progress_token_type_t mcp_progress_token_get_type(mcp_progress_token_t token) MCP_NOEXCEPT;
MCP_API const char* mcp_progress_token_get_string(mcp_progress_token_t token) MCP_NOEXCEPT;
MCP_API int64_t mcp_progress_token_get_number(mcp_progress_token_t token) MCP_NOEXCEPT;

/* ============================================================================
 * Cursor Functions
 * ============================================================================ */

MCP_API mcp_cursor_t mcp_cursor_create(const char* value) MCP_NOEXCEPT;
MCP_API void mcp_cursor_free(mcp_cursor_t cursor) MCP_NOEXCEPT;
MCP_API const char* mcp_cursor_get_value(mcp_cursor_t cursor) MCP_NOEXCEPT;

/* ============================================================================
 * Content Block Functions
 * ============================================================================ */

MCP_API mcp_content_block_t mcp_content_block_create_text(const char* text) MCP_NOEXCEPT;
MCP_API mcp_content_block_t mcp_content_block_create_image(const char* data, const char* mime_type) MCP_NOEXCEPT;
MCP_API mcp_content_block_t mcp_content_block_create_resource(mcp_resource_t resource) MCP_NOEXCEPT;
MCP_API void mcp_content_block_free(mcp_content_block_t block) MCP_NOEXCEPT;
MCP_API mcp_content_block_type_t mcp_content_block_get_type(mcp_content_block_t block) MCP_NOEXCEPT;
MCP_API const char* mcp_content_block_get_text(mcp_content_block_t block) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_content_block_get_image(mcp_content_block_t block, 
                                                const char** out_data, 
                                                const char** out_mime_type) MCP_NOEXCEPT;
MCP_API mcp_resource_t mcp_content_block_get_resource(mcp_content_block_t block) MCP_NOEXCEPT;

/* ============================================================================
 * Tool Functions
 * ============================================================================ */

MCP_API mcp_tool_t mcp_tool_create(const char* name, const char* description) MCP_NOEXCEPT;
MCP_API void mcp_tool_free(mcp_tool_t tool) MCP_NOEXCEPT;
MCP_API const char* mcp_tool_get_name(mcp_tool_t tool) MCP_NOEXCEPT;
MCP_API const char* mcp_tool_get_description(mcp_tool_t tool) MCP_NOEXCEPT;
MCP_API void mcp_tool_set_input_schema(mcp_tool_t tool, mcp_tool_input_schema_t schema) MCP_NOEXCEPT;
MCP_API mcp_tool_input_schema_t mcp_tool_get_input_schema(mcp_tool_t tool) MCP_NOEXCEPT;

MCP_API mcp_tool_input_schema_t mcp_tool_input_schema_create(void) MCP_NOEXCEPT;
MCP_API void mcp_tool_input_schema_free(mcp_tool_input_schema_t schema) MCP_NOEXCEPT;
MCP_API void mcp_tool_input_schema_set_type(mcp_tool_input_schema_t schema, const char* type) MCP_NOEXCEPT;
MCP_API const char* mcp_tool_input_schema_get_type(mcp_tool_input_schema_t schema) MCP_NOEXCEPT;
MCP_API void mcp_tool_input_schema_add_property(mcp_tool_input_schema_t schema, 
                                                 const char* name, 
                                                 const char* json_schema) MCP_NOEXCEPT;
MCP_API void mcp_tool_input_schema_add_required(mcp_tool_input_schema_t schema, const char* name) MCP_NOEXCEPT;
MCP_API size_t mcp_tool_input_schema_get_property_count(mcp_tool_input_schema_t schema) MCP_NOEXCEPT;
MCP_API size_t mcp_tool_input_schema_get_required_count(mcp_tool_input_schema_t schema) MCP_NOEXCEPT;

/* ============================================================================
 * Prompt Functions
 * ============================================================================ */

MCP_API mcp_prompt_t mcp_prompt_create(const char* name, const char* description) MCP_NOEXCEPT;
MCP_API void mcp_prompt_free(mcp_prompt_t prompt) MCP_NOEXCEPT;
MCP_API const char* mcp_prompt_get_name(mcp_prompt_t prompt) MCP_NOEXCEPT;
MCP_API const char* mcp_prompt_get_description(mcp_prompt_t prompt) MCP_NOEXCEPT;
MCP_API void mcp_prompt_add_argument(mcp_prompt_t prompt, 
                                     const char* name, 
                                     const char* description, 
                                     mcp_bool_t required) MCP_NOEXCEPT;
MCP_API size_t mcp_prompt_get_argument_count(mcp_prompt_t prompt) MCP_NOEXCEPT;

/* ============================================================================
 * Error Functions
 * ============================================================================ */

MCP_API mcp_error_t mcp_error_create(int32_t code, const char* message) MCP_NOEXCEPT;
MCP_API void mcp_error_free(mcp_error_t error) MCP_NOEXCEPT;
MCP_API int32_t mcp_error_get_code(mcp_error_t error) MCP_NOEXCEPT;
MCP_API const char* mcp_error_get_message(mcp_error_t error) MCP_NOEXCEPT;
MCP_API void mcp_error_set_data(mcp_error_t error, const char* json_data) MCP_NOEXCEPT;
MCP_API const char* mcp_error_get_data(mcp_error_t error) MCP_NOEXCEPT;

/* ============================================================================
 * Message Functions
 * ============================================================================ */

MCP_API mcp_message_t mcp_message_create(const char* role) MCP_NOEXCEPT;
MCP_API void mcp_message_free(mcp_message_t message) MCP_NOEXCEPT;
MCP_API const char* mcp_message_get_role(mcp_message_t message) MCP_NOEXCEPT;
MCP_API void mcp_message_add_content(mcp_message_t message, mcp_content_block_t content) MCP_NOEXCEPT;
MCP_API size_t mcp_message_get_content_count(mcp_message_t message) MCP_NOEXCEPT;
MCP_API mcp_content_block_t mcp_message_get_content(mcp_message_t message, size_t index) MCP_NOEXCEPT;

/* ============================================================================
 * Resource Functions
 * ============================================================================ */

MCP_API mcp_resource_t mcp_resource_create(const char* uri, const char* name) MCP_NOEXCEPT;
MCP_API void mcp_resource_free(mcp_resource_t resource) MCP_NOEXCEPT;
MCP_API const char* mcp_resource_get_uri(mcp_resource_t resource) MCP_NOEXCEPT;
MCP_API const char* mcp_resource_get_name(mcp_resource_t resource) MCP_NOEXCEPT;
MCP_API void mcp_resource_set_description(mcp_resource_t resource, const char* description) MCP_NOEXCEPT;
MCP_API const char* mcp_resource_get_description(mcp_resource_t resource) MCP_NOEXCEPT;
MCP_API void mcp_resource_set_mime_type(mcp_resource_t resource, const char* mime_type) MCP_NOEXCEPT;
MCP_API const char* mcp_resource_get_mime_type(mcp_resource_t resource) MCP_NOEXCEPT;

/* ============================================================================
 * JSON-RPC Request Functions
 * ============================================================================ */

MCP_API mcp_jsonrpc_request_t mcp_jsonrpc_request_create(const char* jsonrpc, const char* method) MCP_NOEXCEPT;
MCP_API void mcp_jsonrpc_request_free(mcp_jsonrpc_request_t request) MCP_NOEXCEPT;
MCP_API const char* mcp_jsonrpc_request_get_jsonrpc(mcp_jsonrpc_request_t request) MCP_NOEXCEPT;
MCP_API const char* mcp_jsonrpc_request_get_method(mcp_jsonrpc_request_t request) MCP_NOEXCEPT;
MCP_API void mcp_jsonrpc_request_set_id(mcp_jsonrpc_request_t request, mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API mcp_request_id_t mcp_jsonrpc_request_get_id(mcp_jsonrpc_request_t request) MCP_NOEXCEPT;
MCP_API void mcp_jsonrpc_request_set_params(mcp_jsonrpc_request_t request, const char* json_params) MCP_NOEXCEPT;
MCP_API const char* mcp_jsonrpc_request_get_params(mcp_jsonrpc_request_t request) MCP_NOEXCEPT;

/* ============================================================================
 * JSON-RPC Response Functions
 * ============================================================================ */

MCP_API mcp_jsonrpc_response_t mcp_jsonrpc_response_create(const char* jsonrpc) MCP_NOEXCEPT;
MCP_API void mcp_jsonrpc_response_free(mcp_jsonrpc_response_t response) MCP_NOEXCEPT;
MCP_API const char* mcp_jsonrpc_response_get_jsonrpc(mcp_jsonrpc_response_t response) MCP_NOEXCEPT;
MCP_API void mcp_jsonrpc_response_set_id(mcp_jsonrpc_response_t response, mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API mcp_request_id_t mcp_jsonrpc_response_get_id(mcp_jsonrpc_response_t response) MCP_NOEXCEPT;
MCP_API void mcp_jsonrpc_response_set_result(mcp_jsonrpc_response_t response, const char* json_result) MCP_NOEXCEPT;
MCP_API const char* mcp_jsonrpc_response_get_result(mcp_jsonrpc_response_t response) MCP_NOEXCEPT;
MCP_API void mcp_jsonrpc_response_set_error(mcp_jsonrpc_response_t response, mcp_error_t error) MCP_NOEXCEPT;
MCP_API mcp_error_t mcp_jsonrpc_response_get_error(mcp_jsonrpc_response_t response) MCP_NOEXCEPT;

/* ============================================================================
 * Protocol Message Functions
 * ============================================================================ */

/* Initialize Request/Response */
MCP_API mcp_initialize_request_t mcp_initialize_request_create(const char* protocol_version,
                                                               const char* client_name,
                                                               const char* client_version) MCP_NOEXCEPT;
MCP_API void mcp_initialize_request_free(mcp_initialize_request_t request) MCP_NOEXCEPT;
MCP_API const char* mcp_initialize_request_get_protocol_version(mcp_initialize_request_t request) MCP_NOEXCEPT;
MCP_API const char* mcp_initialize_request_get_client_name(mcp_initialize_request_t request) MCP_NOEXCEPT;
MCP_API const char* mcp_initialize_request_get_client_version(mcp_initialize_request_t request) MCP_NOEXCEPT;

MCP_API mcp_initialize_response_t mcp_initialize_response_create(const char* protocol_version,
                                                                 const char* server_name,
                                                                 const char* server_version) MCP_NOEXCEPT;
MCP_API void mcp_initialize_response_free(mcp_initialize_response_t response) MCP_NOEXCEPT;
MCP_API const char* mcp_initialize_response_get_protocol_version(mcp_initialize_response_t response) MCP_NOEXCEPT;
MCP_API const char* mcp_initialize_response_get_server_name(mcp_initialize_response_t response) MCP_NOEXCEPT;
MCP_API const char* mcp_initialize_response_get_server_version(mcp_initialize_response_t response) MCP_NOEXCEPT;

/* Implementation/Server Info Functions */
MCP_API mcp_implementation_t mcp_implementation_create(const char* name, const char* version) MCP_NOEXCEPT;
MCP_API void mcp_implementation_free(mcp_implementation_t impl) MCP_NOEXCEPT;
MCP_API const char* mcp_implementation_get_name(mcp_implementation_t impl) MCP_NOEXCEPT;
MCP_API const char* mcp_implementation_get_version(mcp_implementation_t impl) MCP_NOEXCEPT;
MCP_API void mcp_implementation_set_title(mcp_implementation_t impl, const char* title) MCP_NOEXCEPT;
MCP_API const char* mcp_implementation_get_title(mcp_implementation_t impl) MCP_NOEXCEPT;

/* Client Capabilities Functions */
MCP_API mcp_client_capabilities_t mcp_client_capabilities_create(void) MCP_NOEXCEPT;
MCP_API void mcp_client_capabilities_free(mcp_client_capabilities_t caps) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_client_capabilities_has_roots(mcp_client_capabilities_t caps) MCP_NOEXCEPT;
MCP_API void mcp_client_capabilities_set_roots(mcp_client_capabilities_t caps, mcp_bool_t enabled) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_client_capabilities_has_sampling(mcp_client_capabilities_t caps) MCP_NOEXCEPT;
MCP_API void mcp_client_capabilities_set_sampling(mcp_client_capabilities_t caps, mcp_bool_t enabled) MCP_NOEXCEPT;

/* Server Capabilities Functions */
MCP_API mcp_server_capabilities_t mcp_server_capabilities_create(void) MCP_NOEXCEPT;
MCP_API void mcp_server_capabilities_free(mcp_server_capabilities_t caps) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_server_capabilities_has_tools(mcp_server_capabilities_t caps) MCP_NOEXCEPT;
MCP_API void mcp_server_capabilities_set_tools(mcp_server_capabilities_t caps, mcp_bool_t enabled) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_server_capabilities_has_prompts(mcp_server_capabilities_t caps) MCP_NOEXCEPT;
MCP_API void mcp_server_capabilities_set_prompts(mcp_server_capabilities_t caps, mcp_bool_t enabled) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_server_capabilities_has_resources(mcp_server_capabilities_t caps) MCP_NOEXCEPT;
MCP_API void mcp_server_capabilities_set_resources(mcp_server_capabilities_t caps, mcp_bool_t enabled) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_server_capabilities_has_logging(mcp_server_capabilities_t caps) MCP_NOEXCEPT;
MCP_API void mcp_server_capabilities_set_logging(mcp_server_capabilities_t caps, mcp_bool_t enabled) MCP_NOEXCEPT;

/* Initialize Result Functions */
MCP_API mcp_initialize_result_t mcp_initialize_result_create(const char* protocol_version) MCP_NOEXCEPT;
MCP_API void mcp_initialize_result_free(mcp_initialize_result_t result) MCP_NOEXCEPT;
MCP_API const char* mcp_initialize_result_get_protocol_version(mcp_initialize_result_t result) MCP_NOEXCEPT;
MCP_API void mcp_initialize_result_set_server_info(mcp_initialize_result_t result, mcp_implementation_t info) MCP_NOEXCEPT;
MCP_API mcp_implementation_t mcp_initialize_result_get_server_info(mcp_initialize_result_t result) MCP_NOEXCEPT;
MCP_API void mcp_initialize_result_set_capabilities(mcp_initialize_result_t result, mcp_server_capabilities_t caps) MCP_NOEXCEPT;
MCP_API mcp_server_capabilities_t mcp_initialize_result_get_capabilities(mcp_initialize_result_t result) MCP_NOEXCEPT;

/* JSON-RPC Notification Functions */
MCP_API mcp_jsonrpc_notification_t mcp_jsonrpc_notification_create(const char* jsonrpc, const char* method) MCP_NOEXCEPT;
MCP_API void mcp_jsonrpc_notification_free(mcp_jsonrpc_notification_t notification) MCP_NOEXCEPT;
MCP_API const char* mcp_jsonrpc_notification_get_jsonrpc(mcp_jsonrpc_notification_t notification) MCP_NOEXCEPT;
MCP_API const char* mcp_jsonrpc_notification_get_method(mcp_jsonrpc_notification_t notification) MCP_NOEXCEPT;
MCP_API void mcp_jsonrpc_notification_set_params(mcp_jsonrpc_notification_t notification, const char* json_params) MCP_NOEXCEPT;
MCP_API const char* mcp_jsonrpc_notification_get_params(mcp_jsonrpc_notification_t notification) MCP_NOEXCEPT;

/* Additional JSON-RPC Functions */
MCP_API mcp_request_id_t mcp_jsonrpc_request_get_id(mcp_jsonrpc_request_t request) MCP_NOEXCEPT;
MCP_API const char* mcp_jsonrpc_request_get_params(mcp_jsonrpc_request_t request) MCP_NOEXCEPT;
MCP_API mcp_request_id_t mcp_jsonrpc_response_get_id(mcp_jsonrpc_response_t response) MCP_NOEXCEPT;

/* Add more protocol message functions as needed... */

#ifdef __cplusplus
}
#endif

#endif /* MCP_C_TYPES_API_H */