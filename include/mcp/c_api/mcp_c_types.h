/**
 * @file mcp_c_types.h
 * @brief C API type definitions for MCP C++ SDK
 * 
 * This header provides C-compatible type definitions that map to the MCP C++ types.
 * All types are designed to be FFI-friendly for easy binding to other languages.
 * 
 * Design principles:
 * - All structs are POD (Plain Old Data) types
 * - Opaque pointers for complex C++ objects
 * - Fixed-size types for cross-platform compatibility
 * - No C++ features (namespaces, templates, classes)
 */

#ifndef MCP_C_TYPES_H
#define MCP_C_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Core Type Definitions
 * ============================================================================ */

/**
 * Result type for API calls
 */
typedef enum mcp_result {
    MCP_OK = 0,
    MCP_ERROR = -1,
    MCP_ERROR_INVALID_ARGUMENT = -2,
    MCP_ERROR_OUT_OF_MEMORY = -3,
    MCP_ERROR_NOT_CONNECTED = -4,
    MCP_ERROR_TIMEOUT = -5,
    MCP_ERROR_CANCELLED = -6,
    MCP_ERROR_NOT_FOUND = -7,
    MCP_ERROR_ALREADY_EXISTS = -8,
    MCP_ERROR_PERMISSION_DENIED = -9,
    MCP_ERROR_RESOURCE_EXHAUSTED = -10,
    MCP_ERROR_INVALID_STATE = -11,
    MCP_ERROR_PROTOCOL = -12,
    MCP_ERROR_NOT_IMPLEMENTED = -13,
    MCP_ERROR_IO = -14,
    MCP_ERROR_SSL = -15
} mcp_result_t;

/**
 * String type - Immutable string view
 */
typedef struct mcp_string {
    const char* data;
    size_t length;
} mcp_string_t;

/**
 * Mutable string buffer
 */
typedef struct mcp_string_buffer {
    char* data;
    size_t length;
    size_t capacity;
} mcp_string_buffer_t;

/**
 * Binary data buffer
 */
typedef struct mcp_buffer {
    uint8_t* data;
    size_t length;
    size_t capacity;
} mcp_buffer_t;

/**
 * Optional value wrapper
 */
typedef struct mcp_optional {
    bool has_value;
    void* value;
} mcp_optional_t;

/**
 * List/Array type
 */
typedef struct mcp_list {
    void** items;
    size_t count;
    size_t capacity;
} mcp_list_t;

/**
 * Map/Dictionary type
 */
typedef struct mcp_map_entry {
    mcp_string_t key;
    void* value;
} mcp_map_entry_t;

typedef struct mcp_map {
    mcp_map_entry_t* entries;
    size_t count;
    size_t capacity;
} mcp_map_t;

/* ============================================================================
 * Opaque Handle Types
 * ============================================================================ */

/**
 * Opaque handles for C++ objects
 * These hide implementation details and maintain ABI stability
 */
typedef struct mcp_dispatcher_impl* mcp_dispatcher_t;
typedef struct mcp_connection_impl* mcp_connection_t;
typedef struct mcp_transport_socket_impl* mcp_transport_socket_t;
typedef struct mcp_listener_impl* mcp_listener_t;
typedef struct mcp_filter_impl* mcp_filter_t;
typedef struct mcp_client_impl* mcp_client_t;
typedef struct mcp_server_impl* mcp_server_t;
typedef struct mcp_json_value_impl* mcp_json_value_t;
typedef struct mcp_state_machine_impl* mcp_state_machine_t;
typedef struct mcp_connection_manager_impl* mcp_connection_manager_t;

/* ============================================================================
 * MCP Protocol Types (from types.h)
 * ============================================================================ */

/**
 * Request ID type
 */
typedef struct mcp_request_id {
    enum { MCP_REQUEST_ID_STRING, MCP_REQUEST_ID_NUMBER } type;
    union {
        mcp_string_t string_value;
        int64_t number_value;
    } value;
} mcp_request_id_t;

/**
 * JSON-RPC Error
 */
typedef struct mcp_jsonrpc_error {
    int32_t code;
    mcp_string_t message;
    mcp_optional_t data; /* Optional JSON value */
} mcp_jsonrpc_error_t;

/**
 * MCP Role
 */
typedef enum mcp_role {
    MCP_ROLE_USER,
    MCP_ROLE_ASSISTANT
} mcp_role_t;

/**
 * Tool call result
 */
typedef struct mcp_call_tool_result {
    mcp_list_t content; /* List of content blocks */
    bool is_error;
} mcp_call_tool_result_t;

/**
 * Text content
 */
typedef struct mcp_text_content {
    mcp_string_t type; /* "text" */
    mcp_string_t text;
} mcp_text_content_t;

/**
 * Image content
 */
typedef struct mcp_image_content {
    mcp_string_t type; /* "image" */
    mcp_string_t data;
    mcp_string_t mime_type;
} mcp_image_content_t;

/**
 * Resource content
 */
typedef struct mcp_embedded_resource {
    mcp_string_t type; /* "resource" */
    mcp_string_t id;
    mcp_optional_t text;
    mcp_optional_t blob;
} mcp_embedded_resource_t;

/**
 * Tool definition
 */
typedef struct mcp_tool {
    mcp_string_t name;
    mcp_optional_t description;
    mcp_json_value_t input_schema;
} mcp_tool_t;

/**
 * Resource template
 */
typedef struct mcp_resource_template {
    mcp_string_t uri_template;
    mcp_string_t name;
    mcp_optional_t description;
    mcp_optional_t mime_type;
} mcp_resource_template_t;

/**
 * Prompt definition
 */
typedef struct mcp_prompt {
    mcp_string_t name;
    mcp_optional_t description;
    mcp_list_t arguments; /* List of prompt arguments */
} mcp_prompt_t;

/**
 * Implementation info
 */
typedef struct mcp_implementation {
    mcp_string_t name;
    mcp_string_t version;
} mcp_implementation_t;

/**
 * Client capabilities
 */
typedef struct mcp_client_capabilities {
    mcp_optional_t experimental;
    mcp_optional_t sampling;
    mcp_optional_t roots;
} mcp_client_capabilities_t;

/**
 * Server capabilities
 */
typedef struct mcp_server_capabilities {
    mcp_optional_t experimental;
    mcp_optional_t logging;
    mcp_optional_t prompts;
    mcp_optional_t resources;
    mcp_optional_t tools;
} mcp_server_capabilities_t;

/* ============================================================================
 * Network & Event Types
 * ============================================================================ */

/**
 * Connection state
 */
typedef enum mcp_connection_state {
    MCP_CONNECTION_STATE_CONNECTING,
    MCP_CONNECTION_STATE_CONNECTED,
    MCP_CONNECTION_STATE_DISCONNECTING,
    MCP_CONNECTION_STATE_DISCONNECTED,
    MCP_CONNECTION_STATE_ERROR
} mcp_connection_state_t;

/**
 * Transport type
 */
typedef enum mcp_transport_type {
    MCP_TRANSPORT_TCP,
    MCP_TRANSPORT_SSL,
    MCP_TRANSPORT_HTTP_SSE,
    MCP_TRANSPORT_STDIO,
    MCP_TRANSPORT_PIPE
} mcp_transport_type_t;

/**
 * Address family
 */
typedef enum mcp_address_family {
    MCP_AF_INET,
    MCP_AF_INET6,
    MCP_AF_UNIX
} mcp_address_family_t;

/**
 * Network address
 */
typedef struct mcp_address {
    mcp_address_family_t family;
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

/**
 * Socket options
 */
typedef struct mcp_socket_options {
    bool reuse_addr;
    bool reuse_port;
    bool keep_alive;
    bool tcp_nodelay;
    uint32_t send_buffer_size;
    uint32_t recv_buffer_size;
    uint32_t connect_timeout_ms;
} mcp_socket_options_t;

/**
 * SSL configuration
 */
typedef struct mcp_ssl_config {
    mcp_string_t cert_file;
    mcp_string_t key_file;
    mcp_string_t ca_file;
    bool verify_peer;
    mcp_string_t cipher_list;
    mcp_list_t alpn_protocols; /* List of strings */
} mcp_ssl_config_t;

/**
 * Watermark configuration
 */
typedef struct mcp_watermark_config {
    uint32_t low_watermark;
    uint32_t high_watermark;
} mcp_watermark_config_t;

/* ============================================================================
 * Callback Function Types
 * ============================================================================ */

/**
 * Generic callback with user data
 */
typedef void (*mcp_callback_t)(void* user_data);

/**
 * Error callback
 */
typedef void (*mcp_error_callback_t)(mcp_result_t error, const char* message, void* user_data);

/**
 * Connection state callback
 */
typedef void (*mcp_connection_state_callback_t)(
    mcp_connection_t connection,
    mcp_connection_state_t old_state,
    mcp_connection_state_t new_state,
    void* user_data
);

/**
 * Data received callback
 */
typedef void (*mcp_data_callback_t)(
    mcp_connection_t connection,
    const uint8_t* data,
    size_t length,
    void* user_data
);

/**
 * Write complete callback
 */
typedef void (*mcp_write_callback_t)(
    mcp_connection_t connection,
    mcp_result_t result,
    size_t bytes_written,
    void* user_data
);

/**
 * Accept callback for listeners
 */
typedef void (*mcp_accept_callback_t)(
    mcp_listener_t listener,
    mcp_connection_t connection,
    void* user_data
);

/**
 * Timer callback
 */
typedef void (*mcp_timer_callback_t)(void* user_data);

/**
 * MCP message callbacks
 */
typedef void (*mcp_request_callback_t)(
    mcp_request_id_t id,
    mcp_string_t method,
    mcp_json_value_t params,
    void* user_data
);

typedef void (*mcp_response_callback_t)(
    mcp_request_id_t id,
    mcp_json_value_t result,
    mcp_jsonrpc_error_t* error,
    void* user_data
);

typedef void (*mcp_notification_callback_t)(
    mcp_string_t method,
    mcp_json_value_t params,
    void* user_data
);

/* ============================================================================
 * Configuration Structures
 * ============================================================================ */

/**
 * Client configuration
 */
typedef struct mcp_client_config {
    mcp_implementation_t client_info;
    mcp_client_capabilities_t capabilities;
    mcp_transport_type_t transport;
    mcp_address_t* server_address; /* Optional for TCP */
    mcp_ssl_config_t* ssl_config; /* Optional for SSL */
    mcp_watermark_config_t watermarks;
    uint32_t reconnect_delay_ms;
    uint32_t max_reconnect_attempts;
} mcp_client_config_t;

/**
 * Server configuration
 */
typedef struct mcp_server_config {
    mcp_implementation_t server_info;
    mcp_server_capabilities_t capabilities;
    mcp_transport_type_t transport;
    mcp_address_t* bind_address; /* Optional for TCP */
    mcp_ssl_config_t* ssl_config; /* Optional for SSL */
    mcp_watermark_config_t watermarks;
    uint32_t max_connections;
    mcp_string_t instructions; /* Optional instructions */
} mcp_server_config_t;

/* ============================================================================
 * Memory Management
 * ============================================================================ */

/**
 * Custom allocator interface
 */
typedef struct mcp_allocator {
    void* (*alloc)(size_t size, void* context);
    void* (*realloc)(void* ptr, size_t size, void* context);
    void (*free)(void* ptr, void* context);
    void* context;
} mcp_allocator_t;

#ifdef __cplusplus
}
#endif

#endif /* MCP_C_TYPES_H */