/**
 * @file mcp_c_api_json.h
 * @brief JSON serialization/deserialization for MCP C API types
 *
 * This header declares functions for converting between MCP C API types
 * and JSON representations using RAII memory management.
 */

#ifndef MCP_C_API_JSON_H
#define MCP_C_API_JSON_H

#include "mcp_c_collections.h"
#include "mcp_c_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * JSON Value Operations
 * ============================================================================
 */

/**
 * Parse JSON string to JSON value
 * @param json_string JSON string to parse
 * @return JSON value or NULL on error (caller must free with mcp_json_free)
 */
MCP_API mcp_json_value_t mcp_json_parse(const char* json_string) MCP_NOEXCEPT;

/**
 * Convert JSON value to string
 * @param json JSON value to stringify
 * @return JSON string or NULL on error (caller must free with mcp_string_free)
 */
MCP_API char* mcp_json_stringify(mcp_json_value_t json) MCP_NOEXCEPT;

/* Note: mcp_json_free is declared in mcp_c_collections.h where JSON
 * constructors reside */

/* ============================================================================
 * Request ID JSON Conversion
 * ============================================================================
 */

MCP_API mcp_json_value_t mcp_request_id_to_json(const mcp_request_id_t* id)
    MCP_NOEXCEPT;
MCP_API mcp_request_id_t* mcp_request_id_from_json(mcp_json_value_t json)
    MCP_NOEXCEPT;

/* ============================================================================
 * Progress Token JSON Conversion
 * ============================================================================
 */

MCP_API mcp_json_value_t
mcp_progress_token_to_json(const mcp_progress_token_t* token) MCP_NOEXCEPT;
MCP_API mcp_progress_token_t* mcp_progress_token_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT;

/* ============================================================================
 * Content Block JSON Conversion
 * ============================================================================
 */

MCP_API mcp_json_value_t
mcp_content_block_to_json(const mcp_content_block_t* block) MCP_NOEXCEPT;
MCP_API mcp_content_block_t* mcp_content_block_from_json(mcp_json_value_t json)
    MCP_NOEXCEPT;

/* ============================================================================
 * Tool JSON Conversion
 * ============================================================================
 */

MCP_API mcp_json_value_t mcp_tool_to_json(const mcp_tool_t* tool) MCP_NOEXCEPT;
MCP_API mcp_tool_t* mcp_tool_from_json(mcp_json_value_t json) MCP_NOEXCEPT;

/* ============================================================================
 * Prompt JSON Conversion
 * ============================================================================
 */

MCP_API mcp_json_value_t mcp_prompt_to_json(const mcp_prompt_t* prompt)
    MCP_NOEXCEPT;
MCP_API mcp_prompt_t* mcp_prompt_from_json(mcp_json_value_t json) MCP_NOEXCEPT;

/* ============================================================================
 * Message JSON Conversion
 * ============================================================================
 */

MCP_API mcp_json_value_t mcp_message_to_json(const mcp_message_t* message)
    MCP_NOEXCEPT;
MCP_API mcp_message_t* mcp_message_from_json(mcp_json_value_t json)
    MCP_NOEXCEPT;

/* ============================================================================
 * Error JSON Conversion
 * ============================================================================
 */

MCP_API mcp_json_value_t
mcp_jsonrpc_error_to_json(const mcp_jsonrpc_error_t* error) MCP_NOEXCEPT;
MCP_API mcp_jsonrpc_error_t* mcp_jsonrpc_error_from_json(mcp_json_value_t json)
    MCP_NOEXCEPT;

/* ============================================================================
 * JSON-RPC Request/Response/Notification JSON Conversion
 * ============================================================================
 */

MCP_API mcp_json_value_t
mcp_jsonrpc_request_to_json(const mcp_jsonrpc_request_t* req) MCP_NOEXCEPT;
MCP_API mcp_jsonrpc_request_t* mcp_jsonrpc_request_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT;

MCP_API mcp_json_value_t
mcp_jsonrpc_response_to_json(const mcp_jsonrpc_response_t* resp) MCP_NOEXCEPT;
MCP_API mcp_jsonrpc_response_t* mcp_jsonrpc_response_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT;

MCP_API mcp_json_value_t mcp_jsonrpc_notification_to_json(
    const mcp_jsonrpc_notification_t* notif) MCP_NOEXCEPT;
MCP_API mcp_jsonrpc_notification_t* mcp_jsonrpc_notification_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT;

/* ============================================================================
 * Initialize Request/Response JSON Conversion
 * ============================================================================
 */

MCP_API mcp_json_value_t mcp_initialize_request_to_json(
    const mcp_initialize_request_t* req) MCP_NOEXCEPT;
MCP_API mcp_initialize_request_t* mcp_initialize_request_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT;

MCP_API mcp_json_value_t mcp_initialize_result_to_json(
    const mcp_initialize_result_t* result) MCP_NOEXCEPT;
MCP_API mcp_initialize_result_t* mcp_initialize_result_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT;

/* ============================================================================
 * Other Type JSON Conversions
 * ============================================================================
 */

MCP_API mcp_json_value_t mcp_role_to_json(mcp_role_t role) MCP_NOEXCEPT;
MCP_API mcp_role_t mcp_role_from_json(mcp_json_value_t json) MCP_NOEXCEPT;

MCP_API mcp_json_value_t mcp_logging_level_to_json(mcp_logging_level_t level)
    MCP_NOEXCEPT;
MCP_API mcp_logging_level_t mcp_logging_level_from_json(mcp_json_value_t json)
    MCP_NOEXCEPT;

MCP_API mcp_json_value_t mcp_resource_to_json(const mcp_resource_t* resource)
    MCP_NOEXCEPT;
MCP_API mcp_resource_t* mcp_resource_from_json(mcp_json_value_t json)
    MCP_NOEXCEPT;

MCP_API mcp_json_value_t
mcp_implementation_to_json(const mcp_implementation_t* impl) MCP_NOEXCEPT;
MCP_API mcp_implementation_t* mcp_implementation_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT;

MCP_API mcp_json_value_t mcp_client_capabilities_to_json(
    const mcp_client_capabilities_t* caps) MCP_NOEXCEPT;
MCP_API mcp_client_capabilities_t* mcp_client_capabilities_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT;

MCP_API mcp_json_value_t mcp_server_capabilities_to_json(
    const mcp_server_capabilities_t* caps) MCP_NOEXCEPT;
MCP_API mcp_server_capabilities_t* mcp_server_capabilities_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT;

/* ============================================================================
 * String JSON Conversion
 * ============================================================================
 */

MCP_API mcp_json_value_t mcp_string_to_json(mcp_string_t str) MCP_NOEXCEPT;
MCP_API mcp_string_t mcp_string_from_json(mcp_json_value_t json) MCP_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* MCP_C_API_JSON_H */