/**
 * @file mcp_c_types.h
 * @brief Complete FFI-safe C API type definitions for Gopher MCP library
 *
 * This header provides comprehensive C-compatible type definitions that map ALL
 * MCP C++ types from types.h. All types are designed to be FFI-friendly using
 * opaque handles and accessor functions instead of unions for cross-language
 * compatibility.
 *
 * Design principles:
 * - No unions in public API (uses opaque handles)
 * - All structs are opaque pointers with accessor functions
 * - Fixed-size types for cross-platform compatibility
 * - No C++ features (namespaces, templates, classes)
 * - Complete 1:1 mapping with types.h
 * - Thread-safe operations where needed
 */

#ifndef MCP_C_TYPES_H
#define MCP_C_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Platform and Compiler Configuration
 * ============================================================================ */

/* Export/Import macros for shared library support */
#if defined(_MSC_VER)
  #define MCP_API_EXPORT __declspec(dllexport)
  #define MCP_API_IMPORT __declspec(dllimport)
  #define MCP_CALLBACK __stdcall
  #define MCP_NOEXCEPT
#elif defined(__GNUC__) || defined(__clang__)
  #define MCP_API_EXPORT __attribute__((visibility("default")))
  #define MCP_API_IMPORT
  #define MCP_CALLBACK
  #define MCP_NOEXCEPT __attribute__((nothrow))
#else
  #define MCP_API_EXPORT
  #define MCP_API_IMPORT
  #define MCP_CALLBACK
  #define MCP_NOEXCEPT
#endif

/* API decoration based on build configuration */
#ifdef MCP_BUILD_SHARED
  #ifdef MCP_BUILD_LIBRARY
    #define MCP_API MCP_API_EXPORT
  #else
    #define MCP_API MCP_API_IMPORT
  #endif
#else
  #define MCP_API
#endif

/* ============================================================================
 * FFI-Safe Primitive Types
 * ============================================================================ */

/* FFI-safe boolean type (guaranteed 1 byte) */
typedef uint8_t mcp_bool_t;
#define MCP_TRUE  ((mcp_bool_t)1)
#define MCP_FALSE ((mcp_bool_t)0)

/* Result codes for all API operations */
typedef enum {
    MCP_OK = 0,
    MCP_ERROR_INVALID_ARGUMENT = -1,
    MCP_ERROR_NULL_POINTER = -2,
    MCP_ERROR_OUT_OF_MEMORY = -3,
    MCP_ERROR_NOT_FOUND = -4,
    MCP_ERROR_ALREADY_EXISTS = -5,
    MCP_ERROR_PERMISSION_DENIED = -6,
    MCP_ERROR_IO_ERROR = -7,
    MCP_ERROR_TIMEOUT = -8,
    MCP_ERROR_CANCELLED = -9,
    MCP_ERROR_NOT_IMPLEMENTED = -10,
    MCP_ERROR_INVALID_STATE = -11,
    MCP_ERROR_BUFFER_TOO_SMALL = -12,
    MCP_ERROR_PROTOCOL_ERROR = -13,
    MCP_ERROR_CONNECTION_FAILED = -14,
    MCP_ERROR_CONNECTION_CLOSED = -15,
    MCP_ERROR_ALREADY_INITIALIZED = -16,
    MCP_ERROR_NOT_INITIALIZED = -17,
    MCP_ERROR_RESOURCE_EXHAUSTED = -18,
    MCP_ERROR_INVALID_FORMAT = -19,
    MCP_ERROR_UNKNOWN = -999
} mcp_result_t;

/* String reference for zero-copy string passing */
typedef struct mcp_string_ref {
    const char* data;
    size_t length;
} mcp_string_ref;

/* Error information structure */
typedef struct mcp_error_info {
    mcp_result_t code;
    char message[256];
    char file[256];
    int line;
} mcp_error_info_t;

/* Memory allocator callbacks */
typedef struct mcp_allocator {
    void* (*alloc)(size_t size, void* user_data);
    void* (*realloc)(void* ptr, size_t new_size, void* user_data);
    void (*free)(void* ptr, void* user_data);
    void* user_data;
} mcp_allocator_t;

/* ============================================================================
 * Forward Declarations - Opaque Handle Types
 * ============================================================================ */

/* Core runtime types */
typedef struct mcp_dispatcher_impl* mcp_dispatcher_t;
typedef struct mcp_connection_impl* mcp_connection_t;
typedef struct mcp_listener_impl* mcp_listener_t;
typedef struct mcp_filter_impl* mcp_filter_t;
typedef struct mcp_client_impl* mcp_client_t;
typedef struct mcp_server_impl* mcp_server_t;
typedef struct mcp_transport_socket_impl* mcp_transport_socket_t;
typedef struct mcp_state_machine_impl* mcp_state_machine_t;

/* Core variant types (no unions - use opaque handles) */
typedef struct mcp_request_id_impl* mcp_request_id_t;
typedef struct mcp_progress_token_impl* mcp_progress_token_t;
typedef struct mcp_cursor_impl* mcp_cursor_t;

/* Enum types */
typedef enum {
    MCP_ROLE_USER = 0,
    MCP_ROLE_ASSISTANT = 1
} mcp_role_t;

typedef enum {
    MCP_LOG_DEBUG = 0,
    MCP_LOG_INFO = 1,
    MCP_LOG_NOTICE = 2,
    MCP_LOG_WARNING = 3,
    MCP_LOG_ERROR = 4,
    MCP_LOG_CRITICAL = 5,
    MCP_LOG_ALERT = 6,
    MCP_LOG_EMERGENCY = 7
} mcp_logging_level_t;

/* Transport types */
typedef enum {
    MCP_TRANSPORT_HTTP_SSE = 0,
    MCP_TRANSPORT_STDIO = 1,
    MCP_TRANSPORT_PIPE = 2
} mcp_transport_type_t;

/* Connection states */
typedef enum {
    MCP_CONNECTION_STATE_IDLE = 0,
    MCP_CONNECTION_STATE_CONNECTING = 1,
    MCP_CONNECTION_STATE_CONNECTED = 2,
    MCP_CONNECTION_STATE_CLOSING = 3,
    MCP_CONNECTION_STATE_DISCONNECTED = 4,
    MCP_CONNECTION_STATE_ERROR = 5
} mcp_connection_state_t;

/* Type identifiers for collections and validation */
typedef enum {
    MCP_TYPE_UNKNOWN = 0,
    MCP_TYPE_STRING = 1,
    MCP_TYPE_NUMBER = 2,
    MCP_TYPE_BOOL = 3,
    MCP_TYPE_JSON = 4,
    MCP_TYPE_RESOURCE = 5,
    MCP_TYPE_TOOL = 6,
    MCP_TYPE_PROMPT = 7,
    MCP_TYPE_MESSAGE = 8,
    MCP_TYPE_CONTENT_BLOCK = 9,
    MCP_TYPE_ERROR = 10,
    MCP_TYPE_REQUEST = 11,
    MCP_TYPE_RESPONSE = 12,
    MCP_TYPE_NOTIFICATION = 13
} mcp_type_id_t;

/* Content types */
typedef struct mcp_annotations_impl* mcp_annotations_t;
typedef struct mcp_text_content_impl* mcp_text_content_t;
typedef struct mcp_image_content_impl* mcp_image_content_t;
typedef struct mcp_audio_content_impl* mcp_audio_content_t;
typedef struct mcp_resource_impl* mcp_resource_t;
typedef struct mcp_resource_content_impl* mcp_resource_content_t;
typedef struct mcp_resource_link_impl* mcp_resource_link_t;
typedef struct mcp_embedded_resource_impl* mcp_embedded_resource_t;
typedef struct mcp_content_block_impl* mcp_content_block_t;
typedef struct mcp_extended_content_block_impl* mcp_extended_content_block_t;

/* Tool and Prompt types */
typedef struct mcp_tool_parameter_impl* mcp_tool_parameter_t;
typedef struct mcp_tool_impl* mcp_tool_t;
typedef struct mcp_tool_input_schema_impl* mcp_tool_input_schema_t;
typedef struct mcp_prompt_argument_impl* mcp_prompt_argument_t;
typedef struct mcp_prompt_impl* mcp_prompt_t;

/* Error and Message types */
typedef struct mcp_error_data_impl* mcp_error_data_t;
typedef struct mcp_error_impl* mcp_error_t;
typedef struct mcp_message_impl* mcp_message_t;
typedef struct mcp_prompt_message_impl* mcp_prompt_message_t;
typedef struct mcp_sampling_message_impl* mcp_sampling_message_t;
typedef struct mcp_sampling_params_impl* mcp_sampling_params_t;

/* JSON-RPC types */
typedef struct mcp_request_impl* mcp_request_t;
typedef struct mcp_response_impl* mcp_response_t;
typedef struct mcp_notification_impl* mcp_notification_t;
typedef struct mcp_response_result_impl* mcp_response_result_t;

/* Resource content variations */
typedef struct mcp_resource_contents_impl* mcp_resource_contents_t;
typedef struct mcp_text_resource_contents_impl* mcp_text_resource_contents_t;
typedef struct mcp_blob_resource_contents_impl* mcp_blob_resource_contents_t;

/* Model types */
typedef struct mcp_model_hint_impl* mcp_model_hint_t;
typedef struct mcp_model_preferences_impl* mcp_model_preferences_t;

/* Schema types */
typedef struct mcp_root_impl* mcp_root_t;
typedef struct mcp_string_schema_impl* mcp_string_schema_t;
typedef struct mcp_number_schema_impl* mcp_number_schema_t;
typedef struct mcp_boolean_schema_impl* mcp_boolean_schema_t;
typedef struct mcp_enum_schema_impl* mcp_enum_schema_t;
typedef struct mcp_primitive_schema_impl* mcp_primitive_schema_t;

/* Reference types */
typedef struct mcp_resource_template_reference_impl* mcp_resource_template_reference_t;
typedef struct mcp_prompt_reference_impl* mcp_prompt_reference_t;

/* Capability types */
typedef struct mcp_empty_capability_impl* mcp_empty_capability_t;
typedef struct mcp_resources_capability_impl* mcp_resources_capability_t;
typedef struct mcp_prompts_capability_impl* mcp_prompts_capability_t;
typedef struct mcp_roots_capability_impl* mcp_roots_capability_t;
typedef struct mcp_client_capabilities_impl* mcp_client_capabilities_t;
typedef struct mcp_server_capabilities_impl* mcp_server_capabilities_t;

/* Implementation info types */
typedef struct mcp_implementation_impl* mcp_implementation_t;
typedef mcp_implementation_t mcp_server_info_t;  /* Alias */
typedef mcp_implementation_t mcp_client_info_t;  /* Alias */

/* Protocol message types */
typedef struct mcp_initialize_request_impl* mcp_initialize_request_t;
typedef struct mcp_initialize_result_impl* mcp_initialize_result_t;
typedef struct mcp_initialized_notification_impl* mcp_initialized_notification_t;
typedef struct mcp_ping_request_impl* mcp_ping_request_t;
typedef struct mcp_progress_notification_impl* mcp_progress_notification_t;
typedef struct mcp_cancelled_notification_impl* mcp_cancelled_notification_t;

/* Resource operation types */
typedef struct mcp_list_resources_request_impl* mcp_list_resources_request_t;
typedef struct mcp_list_resources_result_impl* mcp_list_resources_result_t;
typedef struct mcp_list_resource_templates_request_impl* mcp_list_resource_templates_request_t;
typedef struct mcp_resource_template_impl* mcp_resource_template_t;
typedef struct mcp_list_resource_templates_result_impl* mcp_list_resource_templates_result_t;
typedef struct mcp_read_resource_request_impl* mcp_read_resource_request_t;
typedef struct mcp_read_resource_result_impl* mcp_read_resource_result_t;
typedef struct mcp_resource_list_changed_notification_impl* mcp_resource_list_changed_notification_t;
typedef struct mcp_subscribe_request_impl* mcp_subscribe_request_t;
typedef struct mcp_unsubscribe_request_impl* mcp_unsubscribe_request_t;
typedef struct mcp_resource_updated_notification_impl* mcp_resource_updated_notification_t;

/* Prompt operation types */
typedef struct mcp_list_prompts_request_impl* mcp_list_prompts_request_t;
typedef struct mcp_list_prompts_result_impl* mcp_list_prompts_result_t;
typedef struct mcp_get_prompt_request_impl* mcp_get_prompt_request_t;
typedef struct mcp_get_prompt_result_impl* mcp_get_prompt_result_t;
typedef struct mcp_prompt_list_changed_notification_impl* mcp_prompt_list_changed_notification_t;

/* Tool operation types */
typedef struct mcp_list_tools_request_impl* mcp_list_tools_request_t;
typedef struct mcp_list_tools_result_impl* mcp_list_tools_result_t;
typedef struct mcp_call_tool_request_impl* mcp_call_tool_request_t;
typedef struct mcp_call_tool_result_impl* mcp_call_tool_result_t;
typedef struct mcp_tool_list_changed_notification_impl* mcp_tool_list_changed_notification_t;

/* Logging types */
typedef struct mcp_set_level_request_impl* mcp_set_level_request_t;
typedef struct mcp_logging_message_notification_impl* mcp_logging_message_notification_t;

/* Completion types */
typedef struct mcp_complete_request_impl* mcp_complete_request_t;
typedef struct mcp_complete_result_impl* mcp_complete_result_t;

/* Roots types */
typedef struct mcp_list_roots_request_impl* mcp_list_roots_request_t;
typedef struct mcp_list_roots_result_impl* mcp_list_roots_result_t;
typedef struct mcp_roots_list_changed_notification_impl* mcp_roots_list_changed_notification_t;

/* Message creation types */
typedef struct mcp_create_message_request_impl* mcp_create_message_request_t;
typedef struct mcp_create_message_result_impl* mcp_create_message_result_t;

/* Elicit types */
typedef struct mcp_elicit_request_impl* mcp_elicit_request_t;
typedef struct mcp_elicit_result_impl* mcp_elicit_result_t;

/* Initialize params */
typedef struct mcp_initialize_params_impl* mcp_initialize_params_t;

/* Paginated types */
typedef struct mcp_paginated_request_impl* mcp_paginated_request_t;
typedef struct mcp_paginated_result_impl* mcp_paginated_result_t;

/* Empty result */
typedef struct mcp_empty_result_impl* mcp_empty_result_t;

/* JSON/Metadata type */
typedef struct mcp_metadata_impl* mcp_metadata_t;
typedef struct mcp_json_value_impl* mcp_json_value_t;

/* Collections */
typedef struct mcp_list_impl* mcp_list_t;
typedef struct mcp_map_impl* mcp_map_t;

/* Optional type for nullable values */
typedef struct mcp_optional {
    mcp_bool_t has_value;
    void* value;
} mcp_optional_t;

/* Buffer types */
typedef struct mcp_buffer_impl* mcp_buffer_t;
typedef struct mcp_string_buffer_impl* mcp_string_buffer_t;

/* String type for API compatibility */
typedef mcp_string_ref mcp_string_t;

/* JSON-RPC error (alias for compatibility) */
typedef mcp_error_t mcp_jsonrpc_error_t;

/* ============================================================================
 * Configuration Structures (Non-opaque for direct use)
 * ============================================================================ */

/* Address structure for network connections */
typedef struct mcp_address {
    enum { MCP_AF_INET, MCP_AF_INET6, MCP_AF_UNIX } family;
    union {
        struct {
            char host[256];
            uint16_t port;
        } inet;
        struct {
            char path[256];
        } unix;
    } addr;
} mcp_address_t;

/* Socket options */
typedef struct mcp_socket_options {
    mcp_bool_t reuse_addr;
    mcp_bool_t keep_alive;
    mcp_bool_t tcp_nodelay;
    uint32_t send_buffer_size;
    uint32_t recv_buffer_size;
    uint32_t connect_timeout_ms;
} mcp_socket_options_t;

/* SSL configuration */
typedef struct mcp_ssl_config {
    const char* ca_cert_path;
    const char* client_cert_path;
    const char* client_key_path;
    mcp_bool_t verify_peer;
    const char* cipher_list;
    const char** alpn_protocols;
    size_t alpn_count;
} mcp_ssl_config_t;

/* Watermark configuration */
typedef struct mcp_watermark_config {
    uint32_t low_watermark;
    uint32_t high_watermark;
} mcp_watermark_config_t;

/* Client configuration */
typedef struct mcp_client_config {
    mcp_implementation_t client_info;
    mcp_client_capabilities_t capabilities;
    mcp_transport_type_t transport;
    mcp_address_t* server_address;
    mcp_ssl_config_t* ssl_config;
    mcp_watermark_config_t watermarks;
    uint32_t reconnect_delay_ms;
    uint32_t max_reconnect_attempts;
} mcp_client_config_t;

/* Server configuration */
typedef struct mcp_server_config {
    mcp_implementation_t server_info;
    mcp_server_capabilities_t capabilities;
    mcp_transport_type_t transport;
    mcp_address_t* bind_address;
    mcp_ssl_config_t* ssl_config;
    mcp_watermark_config_t watermarks;
    uint32_t max_connections;
    const char* instructions;
} mcp_server_config_t;

/* ============================================================================
 * Request ID Functions (replaces variant<string, int>)
 * ============================================================================ */

MCP_API mcp_request_id_t mcp_request_id_create_string(const char* str) MCP_NOEXCEPT;
MCP_API mcp_request_id_t mcp_request_id_create_number(int64_t num) MCP_NOEXCEPT;
MCP_API void mcp_request_id_free(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_request_id_is_string(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_request_id_is_number(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API const char* mcp_request_id_get_string(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API int64_t mcp_request_id_get_number(mcp_request_id_t id) MCP_NOEXCEPT;

/* ============================================================================
 * Progress Token Functions (replaces variant<string, int>)
 * ============================================================================ */

MCP_API mcp_progress_token_t mcp_progress_token_create_string(const char* str) MCP_NOEXCEPT;
MCP_API mcp_progress_token_t mcp_progress_token_create_number(int64_t num) MCP_NOEXCEPT;
MCP_API void mcp_progress_token_free(mcp_progress_token_t token) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_progress_token_is_string(mcp_progress_token_t token) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_progress_token_is_number(mcp_progress_token_t token) MCP_NOEXCEPT;
MCP_API const char* mcp_progress_token_get_string(mcp_progress_token_t token) MCP_NOEXCEPT;
MCP_API int64_t mcp_progress_token_get_number(mcp_progress_token_t token) MCP_NOEXCEPT;

/* ============================================================================
 * Cursor Functions
 * ============================================================================ */

MCP_API mcp_cursor_t mcp_cursor_create(const char* cursor) MCP_NOEXCEPT;
MCP_API void mcp_cursor_free(mcp_cursor_t cursor) MCP_NOEXCEPT;
MCP_API const char* mcp_cursor_get_value(mcp_cursor_t cursor) MCP_NOEXCEPT;

/* ============================================================================
 * Annotations Functions
 * ============================================================================ */

MCP_API mcp_annotations_t mcp_annotations_create(void) MCP_NOEXCEPT;
MCP_API void mcp_annotations_free(mcp_annotations_t ann) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_annotations_set_audience(mcp_annotations_t ann, const mcp_role_t* roles, size_t count) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_annotations_set_priority(mcp_annotations_t ann, double priority) MCP_NOEXCEPT;
MCP_API size_t mcp_annotations_get_audience_count(mcp_annotations_t ann) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_annotations_get_audience(mcp_annotations_t ann, mcp_role_t* roles, size_t max_count) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_annotations_has_priority(mcp_annotations_t ann) MCP_NOEXCEPT;
MCP_API double mcp_annotations_get_priority(mcp_annotations_t ann) MCP_NOEXCEPT;

/* ============================================================================
 * Content Type Functions
 * ============================================================================ */

/* Text Content */
MCP_API mcp_text_content_t mcp_text_content_create(const char* text) MCP_NOEXCEPT;
MCP_API void mcp_text_content_free(mcp_text_content_t content) MCP_NOEXCEPT;
MCP_API const char* mcp_text_content_get_text(mcp_text_content_t content) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_text_content_set_annotations(mcp_text_content_t content, mcp_annotations_t ann) MCP_NOEXCEPT;
MCP_API mcp_annotations_t mcp_text_content_get_annotations(mcp_text_content_t content) MCP_NOEXCEPT;

/* Image Content */
MCP_API mcp_image_content_t mcp_image_content_create(const char* data, const char* mime_type) MCP_NOEXCEPT;
MCP_API void mcp_image_content_free(mcp_image_content_t content) MCP_NOEXCEPT;
MCP_API const char* mcp_image_content_get_data(mcp_image_content_t content) MCP_NOEXCEPT;
MCP_API const char* mcp_image_content_get_mime_type(mcp_image_content_t content) MCP_NOEXCEPT;

/* Audio Content */
MCP_API mcp_audio_content_t mcp_audio_content_create(const char* data, const char* mime_type) MCP_NOEXCEPT;
MCP_API void mcp_audio_content_free(mcp_audio_content_t content) MCP_NOEXCEPT;
MCP_API const char* mcp_audio_content_get_data(mcp_audio_content_t content) MCP_NOEXCEPT;
MCP_API const char* mcp_audio_content_get_mime_type(mcp_audio_content_t content) MCP_NOEXCEPT;

/* Resource */
MCP_API mcp_resource_t mcp_resource_create(const char* uri, const char* name) MCP_NOEXCEPT;
MCP_API void mcp_resource_free(mcp_resource_t resource) MCP_NOEXCEPT;
MCP_API const char* mcp_resource_get_uri(mcp_resource_t resource) MCP_NOEXCEPT;
MCP_API const char* mcp_resource_get_name(mcp_resource_t resource) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_resource_set_description(mcp_resource_t resource, const char* desc) MCP_NOEXCEPT;
MCP_API const char* mcp_resource_get_description(mcp_resource_t resource) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_resource_set_mime_type(mcp_resource_t resource, const char* mime_type) MCP_NOEXCEPT;
MCP_API const char* mcp_resource_get_mime_type(mcp_resource_t resource) MCP_NOEXCEPT;

/* Resource Content */
MCP_API mcp_resource_content_t mcp_resource_content_create(mcp_resource_t resource) MCP_NOEXCEPT;
MCP_API void mcp_resource_content_free(mcp_resource_content_t content) MCP_NOEXCEPT;
MCP_API mcp_resource_t mcp_resource_content_get_resource(mcp_resource_content_t content) MCP_NOEXCEPT;

/* Content Block (variant) */
typedef enum {
    MCP_CONTENT_TYPE_TEXT,
    MCP_CONTENT_TYPE_IMAGE,
    MCP_CONTENT_TYPE_RESOURCE
} mcp_content_type_t;

MCP_API mcp_content_block_t mcp_content_block_create_text(mcp_text_content_t text) MCP_NOEXCEPT;
MCP_API mcp_content_block_t mcp_content_block_create_image(mcp_image_content_t image) MCP_NOEXCEPT;
MCP_API mcp_content_block_t mcp_content_block_create_resource(mcp_resource_content_t resource) MCP_NOEXCEPT;
MCP_API void mcp_content_block_free(mcp_content_block_t block) MCP_NOEXCEPT;
MCP_API mcp_content_type_t mcp_content_block_get_type(mcp_content_block_t block) MCP_NOEXCEPT;
MCP_API mcp_text_content_t mcp_content_block_get_text(mcp_content_block_t block) MCP_NOEXCEPT;
MCP_API mcp_image_content_t mcp_content_block_get_image(mcp_content_block_t block) MCP_NOEXCEPT;
MCP_API mcp_resource_content_t mcp_content_block_get_resource(mcp_content_block_t block) MCP_NOEXCEPT;

/* ============================================================================
 * Tool Functions
 * ============================================================================ */

MCP_API mcp_tool_t mcp_tool_create(const char* name) MCP_NOEXCEPT;
MCP_API void mcp_tool_free(mcp_tool_t tool) MCP_NOEXCEPT;
MCP_API const char* mcp_tool_get_name(mcp_tool_t tool) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_tool_set_description(mcp_tool_t tool, const char* desc) MCP_NOEXCEPT;
MCP_API const char* mcp_tool_get_description(mcp_tool_t tool) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_tool_set_input_schema(mcp_tool_t tool, mcp_json_value_t schema) MCP_NOEXCEPT;
MCP_API mcp_json_value_t mcp_tool_get_input_schema(mcp_tool_t tool) MCP_NOEXCEPT;

/* ============================================================================
 * Prompt Functions
 * ============================================================================ */

MCP_API mcp_prompt_t mcp_prompt_create(const char* name) MCP_NOEXCEPT;
MCP_API void mcp_prompt_free(mcp_prompt_t prompt) MCP_NOEXCEPT;
MCP_API const char* mcp_prompt_get_name(mcp_prompt_t prompt) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_prompt_set_description(mcp_prompt_t prompt, const char* desc) MCP_NOEXCEPT;
MCP_API const char* mcp_prompt_get_description(mcp_prompt_t prompt) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_prompt_add_argument(mcp_prompt_t prompt, mcp_prompt_argument_t arg) MCP_NOEXCEPT;
MCP_API size_t mcp_prompt_get_argument_count(mcp_prompt_t prompt) MCP_NOEXCEPT;
MCP_API mcp_prompt_argument_t mcp_prompt_get_argument(mcp_prompt_t prompt, size_t index) MCP_NOEXCEPT;

/* Prompt Argument */
MCP_API mcp_prompt_argument_t mcp_prompt_argument_create(const char* name) MCP_NOEXCEPT;
MCP_API void mcp_prompt_argument_free(mcp_prompt_argument_t arg) MCP_NOEXCEPT;
MCP_API const char* mcp_prompt_argument_get_name(mcp_prompt_argument_t arg) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_prompt_argument_set_description(mcp_prompt_argument_t arg, const char* desc) MCP_NOEXCEPT;
MCP_API const char* mcp_prompt_argument_get_description(mcp_prompt_argument_t arg) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_prompt_argument_set_required(mcp_prompt_argument_t arg, mcp_bool_t required) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_prompt_argument_is_required(mcp_prompt_argument_t arg) MCP_NOEXCEPT;

/* ============================================================================
 * Error Functions
 * ============================================================================ */

MCP_API mcp_error_t mcp_error_create(int32_t code, const char* message) MCP_NOEXCEPT;
MCP_API void mcp_error_free(mcp_error_t error) MCP_NOEXCEPT;
MCP_API int32_t mcp_error_get_code(mcp_error_t error) MCP_NOEXCEPT;
MCP_API const char* mcp_error_get_message(mcp_error_t error) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_error_set_data(mcp_error_t error, mcp_error_data_t data) MCP_NOEXCEPT;
MCP_API mcp_error_data_t mcp_error_get_data(mcp_error_t error) MCP_NOEXCEPT;

/* Error Data (variant type) */
typedef enum {
    MCP_ERROR_DATA_NULL,
    MCP_ERROR_DATA_BOOL,
    MCP_ERROR_DATA_INT,
    MCP_ERROR_DATA_DOUBLE,
    MCP_ERROR_DATA_STRING,
    MCP_ERROR_DATA_STRING_ARRAY,
    MCP_ERROR_DATA_STRING_MAP
} mcp_error_data_type_t;

MCP_API mcp_error_data_t mcp_error_data_create_null(void) MCP_NOEXCEPT;
MCP_API mcp_error_data_t mcp_error_data_create_bool(mcp_bool_t value) MCP_NOEXCEPT;
MCP_API mcp_error_data_t mcp_error_data_create_int(int64_t value) MCP_NOEXCEPT;
MCP_API mcp_error_data_t mcp_error_data_create_double(double value) MCP_NOEXCEPT;
MCP_API mcp_error_data_t mcp_error_data_create_string(const char* value) MCP_NOEXCEPT;
MCP_API void mcp_error_data_free(mcp_error_data_t data) MCP_NOEXCEPT;
MCP_API mcp_error_data_type_t mcp_error_data_get_type(mcp_error_data_t data) MCP_NOEXCEPT;

/* ============================================================================
 * Message Functions
 * ============================================================================ */

MCP_API mcp_message_t mcp_message_create(mcp_role_t role, mcp_content_block_t content) MCP_NOEXCEPT;
MCP_API void mcp_message_free(mcp_message_t message) MCP_NOEXCEPT;
MCP_API mcp_role_t mcp_message_get_role(mcp_message_t message) MCP_NOEXCEPT;
MCP_API mcp_content_block_t mcp_message_get_content(mcp_message_t message) MCP_NOEXCEPT;

/* ============================================================================
 * JSON-RPC Functions
 * ============================================================================ */

/* Request */
MCP_API mcp_request_t mcp_request_create(mcp_request_id_t id, const char* method) MCP_NOEXCEPT;
MCP_API void mcp_request_free(mcp_request_t request) MCP_NOEXCEPT;
MCP_API mcp_request_id_t mcp_request_get_id(mcp_request_t request) MCP_NOEXCEPT;
MCP_API const char* mcp_request_get_method(mcp_request_t request) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_request_set_params(mcp_request_t request, mcp_metadata_t params) MCP_NOEXCEPT;
MCP_API mcp_metadata_t mcp_request_get_params(mcp_request_t request) MCP_NOEXCEPT;

/* Response */
MCP_API mcp_response_t mcp_response_create_success(mcp_request_id_t id, mcp_response_result_t result) MCP_NOEXCEPT;
MCP_API mcp_response_t mcp_response_create_error(mcp_request_id_t id, mcp_error_t error) MCP_NOEXCEPT;
MCP_API void mcp_response_free(mcp_response_t response) MCP_NOEXCEPT;
MCP_API mcp_request_id_t mcp_response_get_id(mcp_response_t response) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_response_is_success(mcp_response_t response) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_response_is_error(mcp_response_t response) MCP_NOEXCEPT;
MCP_API mcp_response_result_t mcp_response_get_result(mcp_response_t response) MCP_NOEXCEPT;
MCP_API mcp_error_t mcp_response_get_error(mcp_response_t response) MCP_NOEXCEPT;

/* Notification */
MCP_API mcp_notification_t mcp_notification_create(const char* method) MCP_NOEXCEPT;
MCP_API void mcp_notification_free(mcp_notification_t notification) MCP_NOEXCEPT;
MCP_API const char* mcp_notification_get_method(mcp_notification_t notification) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_notification_set_params(mcp_notification_t notification, mcp_metadata_t params) MCP_NOEXCEPT;
MCP_API mcp_metadata_t mcp_notification_get_params(mcp_notification_t notification) MCP_NOEXCEPT;

/* ============================================================================
 * Protocol Message Functions
 * ============================================================================ */

/* Initialize Request */
MCP_API mcp_initialize_request_t mcp_initialize_request_create(mcp_request_id_t id, mcp_client_info_t client_info, mcp_client_capabilities_t capabilities) MCP_NOEXCEPT;
MCP_API void mcp_initialize_request_free(mcp_initialize_request_t request) MCP_NOEXCEPT;
MCP_API mcp_client_info_t mcp_initialize_request_get_client_info(mcp_initialize_request_t request) MCP_NOEXCEPT;
MCP_API mcp_client_capabilities_t mcp_initialize_request_get_capabilities(mcp_initialize_request_t request) MCP_NOEXCEPT;

/* Initialize Result */
MCP_API mcp_initialize_result_t mcp_initialize_result_create(mcp_server_info_t server_info, mcp_server_capabilities_t capabilities) MCP_NOEXCEPT;
MCP_API void mcp_initialize_result_free(mcp_initialize_result_t result) MCP_NOEXCEPT;
MCP_API mcp_server_info_t mcp_initialize_result_get_server_info(mcp_initialize_result_t result) MCP_NOEXCEPT;
MCP_API mcp_server_capabilities_t mcp_initialize_result_get_capabilities(mcp_initialize_result_t result) MCP_NOEXCEPT;

/* Implementation (ClientInfo/ServerInfo) */
MCP_API mcp_implementation_t mcp_implementation_create(const char* name, const char* version) MCP_NOEXCEPT;
MCP_API void mcp_implementation_free(mcp_implementation_t impl) MCP_NOEXCEPT;
MCP_API const char* mcp_implementation_get_name(mcp_implementation_t impl) MCP_NOEXCEPT;
MCP_API const char* mcp_implementation_get_version(mcp_implementation_t impl) MCP_NOEXCEPT;

/* Capabilities */
MCP_API mcp_client_capabilities_t mcp_client_capabilities_create(void) MCP_NOEXCEPT;
MCP_API void mcp_client_capabilities_free(mcp_client_capabilities_t caps) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_client_capabilities_set_roots(mcp_client_capabilities_t caps, mcp_roots_capability_t roots) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_client_capabilities_set_sampling(mcp_client_capabilities_t caps, mcp_empty_capability_t sampling) MCP_NOEXCEPT;

MCP_API mcp_server_capabilities_t mcp_server_capabilities_create(void) MCP_NOEXCEPT;
MCP_API void mcp_server_capabilities_free(mcp_server_capabilities_t caps) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_server_capabilities_set_prompts(mcp_server_capabilities_t caps, mcp_prompts_capability_t prompts) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_server_capabilities_set_resources(mcp_server_capabilities_t caps, mcp_resources_capability_t resources) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_server_capabilities_set_tools(mcp_server_capabilities_t caps, mcp_empty_capability_t tools) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_server_capabilities_set_logging(mcp_server_capabilities_t caps, mcp_empty_capability_t logging) MCP_NOEXCEPT;

/* ============================================================================
 * List Operations
 * ============================================================================ */

/* List Resources */
MCP_API mcp_list_resources_request_t mcp_list_resources_request_create(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API void mcp_list_resources_request_free(mcp_list_resources_request_t request) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_resources_request_set_cursor(mcp_list_resources_request_t request, mcp_cursor_t cursor) MCP_NOEXCEPT;

MCP_API mcp_list_resources_result_t mcp_list_resources_result_create(void) MCP_NOEXCEPT;
MCP_API void mcp_list_resources_result_free(mcp_list_resources_result_t result) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_resources_result_add_resource(mcp_list_resources_result_t result, mcp_resource_t resource) MCP_NOEXCEPT;
MCP_API size_t mcp_list_resources_result_get_resource_count(mcp_list_resources_result_t result) MCP_NOEXCEPT;
MCP_API mcp_resource_t mcp_list_resources_result_get_resource(mcp_list_resources_result_t result, size_t index) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_resources_result_set_next_cursor(mcp_list_resources_result_t result, mcp_cursor_t cursor) MCP_NOEXCEPT;

/* List Tools */
MCP_API mcp_list_tools_request_t mcp_list_tools_request_create(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API void mcp_list_tools_request_free(mcp_list_tools_request_t request) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_tools_request_set_cursor(mcp_list_tools_request_t request, mcp_cursor_t cursor) MCP_NOEXCEPT;

MCP_API mcp_list_tools_result_t mcp_list_tools_result_create(void) MCP_NOEXCEPT;
MCP_API void mcp_list_tools_result_free(mcp_list_tools_result_t result) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_tools_result_add_tool(mcp_list_tools_result_t result, mcp_tool_t tool) MCP_NOEXCEPT;
MCP_API size_t mcp_list_tools_result_get_tool_count(mcp_list_tools_result_t result) MCP_NOEXCEPT;
MCP_API mcp_tool_t mcp_list_tools_result_get_tool(mcp_list_tools_result_t result, size_t index) MCP_NOEXCEPT;

/* List Prompts */
MCP_API mcp_list_prompts_request_t mcp_list_prompts_request_create(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API void mcp_list_prompts_request_free(mcp_list_prompts_request_t request) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_prompts_request_set_cursor(mcp_list_prompts_request_t request, mcp_cursor_t cursor) MCP_NOEXCEPT;

MCP_API mcp_list_prompts_result_t mcp_list_prompts_result_create(void) MCP_NOEXCEPT;
MCP_API void mcp_list_prompts_result_free(mcp_list_prompts_result_t result) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_prompts_result_add_prompt(mcp_list_prompts_result_t result, mcp_prompt_t prompt) MCP_NOEXCEPT;
MCP_API size_t mcp_list_prompts_result_get_prompt_count(mcp_list_prompts_result_t result) MCP_NOEXCEPT;
MCP_API mcp_prompt_t mcp_list_prompts_result_get_prompt(mcp_list_prompts_result_t result, size_t index) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_prompts_result_set_next_cursor(mcp_list_prompts_result_t result, mcp_cursor_t cursor) MCP_NOEXCEPT;

/* ============================================================================
 * Tool Operations
 * ============================================================================ */

MCP_API mcp_call_tool_request_t mcp_call_tool_request_create(mcp_request_id_t id, const char* name) MCP_NOEXCEPT;
MCP_API void mcp_call_tool_request_free(mcp_call_tool_request_t request) MCP_NOEXCEPT;
MCP_API const char* mcp_call_tool_request_get_name(mcp_call_tool_request_t request) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_call_tool_request_set_arguments(mcp_call_tool_request_t request, mcp_json_value_t args) MCP_NOEXCEPT;
MCP_API mcp_json_value_t mcp_call_tool_request_get_arguments(mcp_call_tool_request_t request) MCP_NOEXCEPT;

MCP_API mcp_call_tool_result_t mcp_call_tool_result_create(void) MCP_NOEXCEPT;
MCP_API void mcp_call_tool_result_free(mcp_call_tool_result_t result) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_call_tool_result_add_content(mcp_call_tool_result_t result, mcp_content_block_t content) MCP_NOEXCEPT;
MCP_API size_t mcp_call_tool_result_get_content_count(mcp_call_tool_result_t result) MCP_NOEXCEPT;
MCP_API mcp_content_block_t mcp_call_tool_result_get_content(mcp_call_tool_result_t result, size_t index) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_call_tool_result_set_is_error(mcp_call_tool_result_t result, mcp_bool_t is_error) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_call_tool_result_is_error(mcp_call_tool_result_t result) MCP_NOEXCEPT;

/* ============================================================================
 * Collection Functions
 * ============================================================================ */

/* Generic List */
MCP_API mcp_list_t mcp_list_create(mcp_type_id_t element_type) MCP_NOEXCEPT;
MCP_API void mcp_list_free(mcp_list_t list) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_append(mcp_list_t list, void* item) MCP_NOEXCEPT;
MCP_API size_t mcp_list_size(mcp_list_t list) MCP_NOEXCEPT;
MCP_API void* mcp_list_get(mcp_list_t list, size_t index) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_clear(mcp_list_t list) MCP_NOEXCEPT;

/* Generic Map */
MCP_API mcp_map_t mcp_map_create(mcp_type_id_t value_type) MCP_NOEXCEPT;
MCP_API void mcp_map_free(mcp_map_t map) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_map_set(mcp_map_t map, const char* key, void* value) MCP_NOEXCEPT;
MCP_API void* mcp_map_get(mcp_map_t map, const char* key) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_map_has(mcp_map_t map, const char* key) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_map_remove(mcp_map_t map, const char* key) MCP_NOEXCEPT;
MCP_API size_t mcp_map_size(mcp_map_t map) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_map_clear(mcp_map_t map) MCP_NOEXCEPT;

/* ============================================================================
 * Error Handling
 * ============================================================================ */

/* Get last error information (thread-local) */
MCP_API const mcp_error_info_t* mcp_get_last_error(void) MCP_NOEXCEPT;

/* ============================================================================
 * Callback Types
 * ============================================================================ */

/* Generic callback with user data */
typedef void (*mcp_callback_t)(void* user_data);

/* Timer callback */
typedef void (*mcp_timer_callback_t)(void* user_data);

/* Error callback */
typedef void (*mcp_error_callback_t)(mcp_result_t error,
                                     const char* message,
                                     void* user_data);

/* Data received callback */
typedef void (*mcp_data_callback_t)(mcp_connection_t connection,
                                    const uint8_t* data,
                                    size_t length,
                                    void* user_data);

/* Write complete callback */
typedef void (*mcp_write_callback_t)(mcp_connection_t connection,
                                     mcp_result_t result,
                                     size_t bytes_written,
                                     void* user_data);

/* Connection state callback */
typedef void (*mcp_connection_state_callback_t)(
    mcp_connection_t connection,
    int state,  /* Connection state enum value */
    void* user_data);

/* Accept callback for listeners */
typedef void (*mcp_accept_callback_t)(mcp_listener_t listener,
                                      mcp_connection_t connection,
                                      void* user_data);

/* MCP message callbacks */
typedef void (*mcp_request_callback_t)(mcp_client_t client,
                                       mcp_request_t request,
                                       void* user_data);

typedef void (*mcp_response_callback_t)(mcp_client_t client,
                                        mcp_response_t response,
                                        void* user_data);

typedef void (*mcp_notification_callback_t)(mcp_client_t client,
                                            mcp_notification_t notification,
                                            void* user_data);

/* ============================================================================
 * JSON/Metadata Functions
 * ============================================================================ */

MCP_API mcp_json_value_t mcp_json_create_null(void) MCP_NOEXCEPT;
MCP_API mcp_json_value_t mcp_json_create_bool(mcp_bool_t value) MCP_NOEXCEPT;
MCP_API mcp_json_value_t mcp_json_create_number(double value) MCP_NOEXCEPT;
MCP_API mcp_json_value_t mcp_json_create_string(const char* value) MCP_NOEXCEPT;
MCP_API mcp_json_value_t mcp_json_create_array(void) MCP_NOEXCEPT;
MCP_API mcp_json_value_t mcp_json_create_object(void) MCP_NOEXCEPT;
MCP_API void mcp_json_free(mcp_json_value_t json) MCP_NOEXCEPT;

MCP_API mcp_metadata_t mcp_metadata_create(void) MCP_NOEXCEPT;
MCP_API void mcp_metadata_free(mcp_metadata_t metadata) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_metadata_from_json(mcp_metadata_t metadata, mcp_json_value_t json) MCP_NOEXCEPT;
MCP_API mcp_json_value_t mcp_metadata_to_json(mcp_metadata_t metadata) MCP_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* MCP_C_TYPES_H */