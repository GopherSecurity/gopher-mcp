/**
 * @file mcp_c_types.h
 * @brief FFI-safe C type definitions for Gopher MCP library
 *
 * This header provides ONLY type definitions (typedefs, enums, structs) that
 * provide a 1:1 mapping with MCP C++ types from types.h. All types are designed
 * to be FFI-friendly for cross-language compatibility.
 *
 * Design principles:
 * - No function declarations (those go in separate API headers)
 * - No unions in public API (uses opaque handles)
 * - Fixed-size types for cross-platform consistency
 * - No C++ features (namespaces, templates, classes)
 * - Complete 1:1 mapping with types.h
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
 * ============================================================================
 */

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
 * ============================================================================
 */

/* FFI-safe boolean type (guaranteed 1 byte) */
typedef uint8_t mcp_bool_t;
#define MCP_TRUE ((mcp_bool_t)1)
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

/* ============================================================================
 * Basic Types
 * ============================================================================
 */

/* String reference for zero-copy string passing */
typedef struct mcp_string_ref {
  const char* data;
  size_t length;
} mcp_string_ref;

/* String type for API compatibility */
typedef mcp_string_ref mcp_string_t;

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

/* Optional type for nullable values */
typedef struct mcp_optional {
  mcp_bool_t has_value;
  void* value;
} mcp_optional_t;

/* ============================================================================
 * Enumerations from types.h
 * ============================================================================
 */

/* MCP Role */
typedef enum { MCP_ROLE_USER = 0, MCP_ROLE_ASSISTANT = 1 } mcp_role_t;

/* Logging levels */
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

/* Request ID type enum */
typedef enum {
  MCP_REQUEST_ID_TYPE_STRING = 0,
  MCP_REQUEST_ID_TYPE_NUMBER = 1
} mcp_request_id_type_t;

/* Progress token type enum */
typedef enum {
  MCP_PROGRESS_TOKEN_TYPE_STRING = 0,
  MCP_PROGRESS_TOKEN_TYPE_NUMBER = 1
} mcp_progress_token_type_t;

/* Content block type enum */
typedef enum {
  MCP_CONTENT_BLOCK_TYPE_TEXT = 0,
  MCP_CONTENT_BLOCK_TYPE_IMAGE = 1,
  MCP_CONTENT_BLOCK_TYPE_RESOURCE = 2
} mcp_content_block_type_t;

/* JSON value types */
typedef enum {
  MCP_JSON_TYPE_NULL = 0,
  MCP_JSON_TYPE_BOOL = 1,
  MCP_JSON_TYPE_NUMBER = 2,
  MCP_JSON_TYPE_STRING = 3,
  MCP_JSON_TYPE_ARRAY = 4,
  MCP_JSON_TYPE_OBJECT = 5
} mcp_json_type_t;

/* ============================================================================
 * Opaque Handle Types (1:1 mapping with types.h)
 * ============================================================================
 */

/* Core runtime types (SDK infrastructure) */
typedef struct mcp_dispatcher_impl* mcp_dispatcher_t;
typedef struct mcp_connection_impl* mcp_connection_t;
typedef struct mcp_listener_impl* mcp_listener_t;
typedef struct mcp_filter_impl* mcp_filter_t;
typedef struct mcp_client_impl* mcp_client_t;
typedef struct mcp_server_impl* mcp_server_t;
typedef struct mcp_transport_socket_impl* mcp_transport_socket_t;
typedef struct mcp_state_machine_impl* mcp_state_machine_t;

/* Core MCP protocol types (from types.h) */
typedef struct mcp_request_id_impl* mcp_request_id_t;
typedef struct mcp_progress_token_impl* mcp_progress_token_t;
typedef struct mcp_cursor_impl* mcp_cursor_t;

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
typedef struct mcp_jsonrpc_request_impl* mcp_jsonrpc_request_t;
typedef struct mcp_jsonrpc_response_impl* mcp_jsonrpc_response_t;
typedef struct mcp_jsonrpc_notification_impl* mcp_jsonrpc_notification_t;

/* JSON-RPC error (alias for compatibility) */
typedef mcp_error_t mcp_jsonrpc_error_t;

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
typedef struct mcp_resource_template_reference_impl*
    mcp_resource_template_reference_t;
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
typedef mcp_implementation_t mcp_server_info_t; /* Alias */
typedef mcp_implementation_t mcp_client_info_t; /* Alias */

/* Protocol message types */
typedef struct mcp_initialize_request_impl* mcp_initialize_request_t;
typedef struct mcp_initialize_result_impl* mcp_initialize_result_t;
typedef struct mcp_initialize_response_impl* mcp_initialize_response_t;
typedef struct mcp_initialized_notification_impl*
    mcp_initialized_notification_t;
typedef struct mcp_ping_request_impl* mcp_ping_request_t;
typedef struct mcp_ping_response_impl* mcp_ping_response_t;
typedef struct mcp_progress_notification_impl* mcp_progress_notification_t;
typedef struct mcp_cancelled_notification_impl* mcp_cancelled_notification_t;
typedef struct mcp_resource_list_impl* mcp_resource_list_t;
typedef struct mcp_resource_template_impl* mcp_resource_template_t;
typedef struct mcp_resource_template_list_impl* mcp_resource_template_list_t;
typedef struct mcp_call_tool_request_impl* mcp_call_tool_request_t;
typedef struct mcp_call_tool_result_impl* mcp_call_tool_result_t;
typedef struct mcp_tool_list_changed_notification_impl*
    mcp_tool_list_changed_notification_t;
typedef struct mcp_set_level_request_impl* mcp_set_level_request_t;
typedef struct mcp_logging_message_notification_impl*
    mcp_logging_message_notification_t;
typedef struct mcp_complete_request_impl* mcp_complete_request_t;
typedef struct mcp_complete_result_impl* mcp_complete_result_t;
typedef struct mcp_list_roots_request_impl* mcp_list_roots_request_t;
typedef struct mcp_list_roots_result_impl* mcp_list_roots_result_t;
typedef struct mcp_roots_list_changed_notification_impl*
    mcp_roots_list_changed_notification_t;
typedef struct mcp_create_message_request_impl* mcp_create_message_request_t;
typedef struct mcp_create_message_result_impl* mcp_create_message_result_t;
typedef struct mcp_elicit_request_impl* mcp_elicit_request_t;
typedef struct mcp_elicit_result_impl* mcp_elicit_result_t;
typedef struct mcp_initialize_params_impl* mcp_initialize_params_t;
typedef struct mcp_paginated_request_impl* mcp_paginated_request_t;
typedef struct mcp_paginated_result_impl* mcp_paginated_result_t;
typedef struct mcp_empty_result_impl* mcp_empty_result_t;

/* Collections and utilities (not from types.h but needed for API) */
typedef struct mcp_list_impl* mcp_list_t;
typedef struct mcp_map_impl* mcp_map_t;
typedef struct mcp_buffer_impl* mcp_buffer_t;
typedef struct mcp_string_buffer_impl* mcp_string_buffer_t;
typedef struct mcp_metadata_impl* mcp_metadata_t;
typedef struct mcp_json_value_impl* mcp_json_value_t;

/* ============================================================================
 * Configuration Structures (Non-opaque for direct use)
 * ============================================================================
 */

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
 * Callback Types
 * ============================================================================
 */

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
    int state, /* Connection state enum value */
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

#ifdef __cplusplus
}
#endif

#endif /* MCP_C_TYPES_H */