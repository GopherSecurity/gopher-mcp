/**
 * @file mcp_c_types.h
 * @brief Complete C API type definitions for Gopher MCP library
 *
 * This header provides comprehensive C-compatible type definitions that map ALL
 * MCP C++ types from types.h, including JSON serialization/deserialization and
 * builder patterns. All types are designed to be FFI-friendly for easy binding
 * to other languages.
 *
 * Design principles:
 * - All structs are POD (Plain Old Data) types
 * - Opaque pointers for complex C++ objects
 * - Fixed-size types for cross-platform compatibility
 * - No C++ features (namespaces, templates, classes)
 * - Complete JSON serialization/deserialization support
 * - Builder patterns for complex type construction
 * - FFI-friendly for all language bindings
 */

#ifndef MCP_C_TYPES_H
#define MCP_C_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Compiler and Platform Detection for FFI
 * ============================================================================
 */

#if defined(_MSC_VER)
  #define MCP_EXPORT __declspec(dllexport)
  #define MCP_IMPORT __declspec(dllimport)
  #define MCP_PACKED_STRUCT(name) __pragma(pack(push, 1)) struct name
  #define MCP_PACKED_STRUCT_END __pragma(pack(pop))
#elif defined(__GNUC__) || defined(__clang__)
  #define MCP_EXPORT __attribute__((visibility("default")))
  #define MCP_IMPORT
  #define MCP_PACKED_STRUCT(name) struct __attribute__((packed)) name
  #define MCP_PACKED_STRUCT_END
#else
  #define MCP_EXPORT
  #define MCP_IMPORT
  #define MCP_PACKED_STRUCT(name) struct name
  #define MCP_PACKED_STRUCT_END
#endif

/* Calling convention for callbacks */
#ifdef _WIN32
  #define MCP_CALLBACK __stdcall
#else
  #define MCP_CALLBACK
#endif

/* ============================================================================
 * FFI-Safe Boolean Type
 * ============================================================================
 */

/**
 * FFI-safe boolean type using fixed-size uint8_t
 * This ensures consistent size and representation across all platforms
 */
typedef uint8_t mcp_bool_t;

#define MCP_TRUE  ((mcp_bool_t)1)
#define MCP_FALSE ((mcp_bool_t)0)

/* ============================================================================
 * Forward Declarations
 * ============================================================================
 */

typedef struct mcp_json_value_impl* mcp_json_value_t;

/* ============================================================================
 * Core Type Definitions
 * ============================================================================
 */

/**
 * JSON-RPC error codes
 */
typedef enum mcp_jsonrpc_error_code {
  MCP_JSONRPC_PARSE_ERROR = -32700,
  MCP_JSONRPC_INVALID_REQUEST = -32600,
  MCP_JSONRPC_METHOD_NOT_FOUND = -32601,
  MCP_JSONRPC_INVALID_PARAMS = -32602,
  MCP_JSONRPC_INTERNAL_ERROR = -32603
} mcp_jsonrpc_error_code_t;

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
  mcp_bool_t has_value;
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
 * ============================================================================
 */

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
 * ============================================================================
 */

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
  mcp_optional_t data; /* Optional error data (see mcp_error_data_t) */
} mcp_jsonrpc_error_t;

/**
 * MCP Role
 */
typedef enum mcp_role { MCP_ROLE_USER, MCP_ROLE_ASSISTANT } mcp_role_t;

/**
 * Tool call result
 */
typedef struct mcp_call_tool_result {
  mcp_list_t content; /* List of content blocks */
  mcp_bool_t is_error;
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
 * Resource type
 */
typedef struct mcp_resource {
  mcp_string_t uri;
  mcp_string_t name;
  mcp_optional_t* description; /* optional string */
  mcp_optional_t* mime_type;   /* optional string */
} mcp_resource_t;

/**
 * Resource content (embedded)
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
 * Prompt argument
 */
typedef struct mcp_prompt_argument {
  mcp_string_t name;
  mcp_optional_t description;
  mcp_bool_t required;
} mcp_prompt_argument_t;

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
 * ============================================================================
 */

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
  mcp_bool_t reuse_addr;
  mcp_bool_t reuse_port;
  mcp_bool_t keep_alive;
  mcp_bool_t tcp_nodelay;
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
  mcp_bool_t verify_peer;
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
 * ============================================================================
 */

/**
 * Generic callback with user data
 */
typedef void (*mcp_callback_t)(void* user_data);

/**
 * Error callback
 */
typedef void (*mcp_error_callback_t)(mcp_result_t error,
                                     const char* message,
                                     void* user_data);

/**
 * Connection state callback
 */
typedef void (*mcp_connection_state_callback_t)(
    mcp_connection_t connection,
    mcp_connection_state_t old_state,
    mcp_connection_state_t new_state,
    void* user_data);

/**
 * Data received callback
 */
typedef void (*mcp_data_callback_t)(mcp_connection_t connection,
                                    const uint8_t* data,
                                    size_t length,
                                    void* user_data);

/**
 * Write complete callback
 */
typedef void (*mcp_write_callback_t)(mcp_connection_t connection,
                                     mcp_result_t result,
                                     size_t bytes_written,
                                     void* user_data);

/**
 * Accept callback for listeners
 */
typedef void (*mcp_accept_callback_t)(mcp_listener_t listener,
                                      mcp_connection_t connection,
                                      void* user_data);

/**
 * Timer callback
 */
typedef void (*mcp_timer_callback_t)(void* user_data);

/**
 * MCP message callbacks
 */
typedef void (*mcp_request_callback_t)(mcp_request_id_t id,
                                       mcp_string_t method,
                                       mcp_json_value_t params,
                                       void* user_data);

typedef void (*mcp_response_callback_t)(mcp_request_id_t id,
                                        mcp_json_value_t result,
                                        mcp_jsonrpc_error_t* error,
                                        void* user_data);

typedef void (*mcp_notification_callback_t)(mcp_string_t method,
                                            mcp_json_value_t params,
                                            void* user_data);

/* ============================================================================
 * Configuration Structures
 * ============================================================================
 */

/**
 * Client configuration
 *
 * TODO: Transport selection flow:
 * 1. Client specifies preferred transport in this config
 * 2. During initialize handshake, server responds with supported transports
 * 3. Client selects best match from server's supported list
 * 4. Transport socket is reconfigured if needed based on negotiation
 *
 * Note: The transport field here is the client's preference, but the actual
 * transport used may differ based on server capabilities
 */
typedef struct mcp_client_config {
  mcp_implementation_t client_info;
  mcp_client_capabilities_t capabilities;
  mcp_transport_type_t transport; /* Client's preferred transport */
  mcp_address_t* server_address;  /* Optional for TCP */
  mcp_ssl_config_t* ssl_config;   /* Optional for SSL */
  mcp_watermark_config_t watermarks;
  uint32_t reconnect_delay_ms;
  uint32_t max_reconnect_attempts;
} mcp_client_config_t;

/**
 * Server configuration
 *
 * TODO: Server transport handling:
 * 1. Server advertises all supported transports in capabilities
 * 2. Client chooses from the advertised list during initialization
 * 3. Server may need to spawn different transport handlers based on client
 * choice
 * 4. For multi-transport servers, may need transport multiplexing
 *
 * Note: The transport field indicates the primary/default transport,
 * but server should be able to handle multiple transport types
 */
typedef struct mcp_server_config {
  mcp_implementation_t server_info;
  mcp_server_capabilities_t capabilities;
  mcp_transport_type_t transport; /* Primary transport type */
  mcp_address_t* bind_address;    /* Optional for TCP */
  mcp_ssl_config_t* ssl_config;   /* Optional for SSL */
  mcp_watermark_config_t watermarks;
  uint32_t max_connections;
  mcp_string_t instructions; /* Optional instructions */
} mcp_server_config_t;

/* ============================================================================
 * Additional Protocol Types
 * ============================================================================
 */

/**
 * Logging level enum (RFC-5424 severities)
 */
typedef enum mcp_logging_level {
  MCP_LOGGING_DEBUG = 0,
  MCP_LOGGING_INFO = 1,
  MCP_LOGGING_NOTICE = 2,
  MCP_LOGGING_WARNING = 3,
  MCP_LOGGING_ERROR = 4,
  MCP_LOGGING_CRITICAL = 5,
  MCP_LOGGING_ALERT = 6,
  MCP_LOGGING_EMERGENCY = 7
} mcp_logging_level_t;

/**
 * Progress token (variant<string, int>)
 */
typedef struct mcp_progress_token {
  enum { MCP_PROGRESS_TOKEN_STRING, MCP_PROGRESS_TOKEN_NUMBER } type;
  union {
    mcp_string_t string_value;
    int64_t number_value;
  } value;
} mcp_progress_token_t;

/**
 * Cursor for pagination
 */
typedef mcp_string_t mcp_cursor_t;

/**
 * Annotations for content blocks
 */
typedef struct mcp_annotations {
  mcp_optional_t audience; /* List of roles */
  mcp_optional_t priority; /* double, 1.0 = most important */
} mcp_annotations_t;

/**
 * Tool-specific annotations
 */
typedef struct mcp_tool_annotations {
  mcp_optional_t audience; /* List of roles */
} mcp_tool_annotations_t;

/**
 * Audio content
 */
typedef struct mcp_audio_content {
  mcp_string_t type; /* "audio" */
  mcp_string_t data; /* Base64 encoded */
  mcp_string_t mime_type;
} mcp_audio_content_t;

/**
 * Resource link
 */
typedef struct mcp_resource_link {
  mcp_string_t type; /* "resource" */
  mcp_string_t uri;
  mcp_string_t name;
  mcp_optional_t description;
  mcp_optional_t mime_type;
} mcp_resource_link_t;

/**
 * Content block type enum
 */
typedef enum mcp_content_block_type {
  MCP_CONTENT_TEXT,
  MCP_CONTENT_IMAGE,
  MCP_CONTENT_AUDIO,
  MCP_CONTENT_RESOURCE,
  MCP_CONTENT_RESOURCE_LINK,
  MCP_CONTENT_EMBEDDED_RESOURCE
} mcp_content_block_type_t;

/**
 * Extended content block (variant)
 */
typedef struct mcp_content_block {
  mcp_content_block_type_t type;
  union {
    mcp_text_content_t* text;
    mcp_image_content_t* image;
    mcp_audio_content_t* audio;
    mcp_embedded_resource_t* resource;
    mcp_resource_link_t* resource_link;
    mcp_embedded_resource_t* embedded;
  } content;
} mcp_content_block_t;

/**
 * Tool parameter
 */
typedef struct mcp_tool_parameter {
  mcp_string_t name;
  mcp_string_t type;
  mcp_optional_t description;
  mcp_bool_t required;
} mcp_tool_parameter_t;

/**
 * Message
 */
typedef struct mcp_message {
  mcp_role_t role;
  mcp_content_block_t content;
} mcp_message_t;

/**
 * Prompt message
 */
typedef struct mcp_prompt_message {
  mcp_role_t role;
  mcp_content_block_t content; /* Text, Image, or EmbeddedResource */
} mcp_prompt_message_t;

/**
 * Sampling message
 */
typedef struct mcp_sampling_message {
  mcp_role_t role;
  mcp_content_block_t content; /* Text, Image, or Audio */
} mcp_sampling_message_t;

/**
 * Sampling parameters
 */
typedef struct mcp_sampling_params {
  mcp_optional_t temperature;    /* double */
  mcp_optional_t max_tokens;     /* int */
  mcp_optional_t stop_sequences; /* List of strings */
  mcp_optional_t metadata;       /* JSON */
} mcp_sampling_params_t;

/**
 * Model hint
 */
typedef struct mcp_model_hint {
  mcp_optional_t name;
} mcp_model_hint_t;

/**
 * Model preferences
 */
typedef struct mcp_model_preferences {
  mcp_optional_t hints;                 /* List of model hints */
  mcp_optional_t cost_priority;         /* double, 0-1 */
  mcp_optional_t speed_priority;        /* double, 0-1 */
  mcp_optional_t intelligence_priority; /* double, 0-1 */
} mcp_model_preferences_t;

/**
 * Resource contents base
 */
typedef struct mcp_resource_contents {
  mcp_optional_t uri;
  mcp_optional_t mime_type;
} mcp_resource_contents_t;

/**
 * Text resource contents
 */
typedef struct mcp_text_resource_contents {
  mcp_resource_contents_t base;
  mcp_string_t text;
} mcp_text_resource_contents_t;

/**
 * Blob resource contents
 */
typedef struct mcp_blob_resource_contents {
  mcp_resource_contents_t base;
  mcp_string_t blob; /* Base64 encoded */
} mcp_blob_resource_contents_t;

/**
 * Root (filesystem-like)
 */
typedef struct mcp_root {
  mcp_string_t uri;
  mcp_optional_t name;
} mcp_root_t;

/**
 * Schema types for elicitation
 */
typedef struct mcp_string_schema {
  mcp_string_t type; /* "string" */
  mcp_optional_t description;
  mcp_optional_t pattern;
  mcp_optional_t min_length; /* int */
  mcp_optional_t max_length; /* int */
} mcp_string_schema_t;

typedef struct mcp_number_schema {
  mcp_string_t type; /* "number" */
  mcp_optional_t description;
  mcp_optional_t minimum;     /* double */
  mcp_optional_t maximum;     /* double */
  mcp_optional_t multiple_of; /* double */
} mcp_number_schema_t;

typedef struct mcp_boolean_schema {
  mcp_string_t type; /* "boolean" */
  mcp_optional_t description;
} mcp_boolean_schema_t;

typedef struct mcp_enum_schema {
  mcp_string_t type; /* "enum" */
  mcp_optional_t description;
  mcp_list_t values; /* List of strings */
} mcp_enum_schema_t;

typedef enum mcp_schema_type {
  MCP_SCHEMA_STRING,
  MCP_SCHEMA_NUMBER,
  MCP_SCHEMA_BOOLEAN,
  MCP_SCHEMA_ENUM
} mcp_schema_type_t;

typedef struct mcp_primitive_schema {
  mcp_schema_type_t type;
  union {
    mcp_string_schema_t* string;
    mcp_number_schema_t* number;
    mcp_boolean_schema_t* boolean_;
    mcp_enum_schema_t* enum_;
  } schema;
} mcp_primitive_schema_t;

/**
 * Reference types
 */
typedef struct mcp_resource_template_reference {
  mcp_string_t type;
  mcp_string_t name;
} mcp_resource_template_reference_t;

typedef struct mcp_prompt_reference {
  mcp_string_t type;
  mcp_string_t name;
  mcp_optional_t _meta; /* Metadata */
} mcp_prompt_reference_t;

/**
 * Empty capability
 */
typedef mcp_map_t mcp_empty_capability_t;

/**
 * Resources capability
 */
typedef struct mcp_resources_capability {
  mcp_optional_t subscribe;    /* Empty capability */
  mcp_optional_t list_changed; /* Empty capability */
} mcp_resources_capability_t;

/**
 * Prompts capability
 */
typedef struct mcp_prompts_capability {
  mcp_optional_t list_changed; /* Empty capability */
} mcp_prompts_capability_t;

/**
 * Roots capability
 */
typedef struct mcp_roots_capability {
  mcp_optional_t list_changed; /* Empty capability */
} mcp_roots_capability_t;

/**
 * Empty result
 */
typedef struct mcp_empty_result {
  char _placeholder; /* C doesn't allow empty structs */
} mcp_empty_result_t;

/**
 * JSON-RPC Request
 */
typedef struct mcp_jsonrpc_request {
  mcp_string_t jsonrpc; /* "2.0" */
  mcp_request_id_t id;
  mcp_string_t method;
  mcp_optional_t params; /* JSON value */
} mcp_jsonrpc_request_t;

/**
 * JSON-RPC Response
 */
typedef struct mcp_jsonrpc_response {
  mcp_string_t jsonrpc; /* "2.0" */
  mcp_request_id_t id;
  mcp_optional_t result; /* Response result (see mcp_response_result_t) */
  mcp_optional_t error;  /* JSONRPCError */
} mcp_jsonrpc_response_t;

/**
 * JSON-RPC Notification
 */
typedef struct mcp_jsonrpc_notification {
  mcp_string_t jsonrpc; /* "2.0" */
  mcp_string_t method;
  mcp_optional_t params; /* JSON value */
} mcp_jsonrpc_notification_t;

/**
 * Initialize request
 */
typedef struct mcp_initialize_request {
  mcp_string_t protocol_version;
  mcp_client_capabilities_t capabilities;
  mcp_optional_t client_info; /* Implementation */
} mcp_initialize_request_t;

/**
 * Initialize result
 */
typedef struct mcp_initialize_result {
  mcp_string_t protocol_version;
  mcp_server_capabilities_t capabilities;
  mcp_optional_t server_info;  /* Implementation */
  mcp_optional_t instructions; /* string */
} mcp_initialize_result_t;

/**
 * Paginated request/result base
 */
typedef struct mcp_paginated_request {
  mcp_optional_t cursor;
} mcp_paginated_request_t;

typedef struct mcp_paginated_result {
  mcp_optional_t next_cursor;
} mcp_paginated_result_t;

/**
 * Protocol-specific requests
 */
typedef struct mcp_call_tool_request {
  mcp_string_t name;
  mcp_optional_t arguments; /* JSON value */
} mcp_call_tool_request_t;

typedef struct mcp_get_prompt_request {
  mcp_string_t name;
  mcp_optional_t arguments; /* JSON value */
} mcp_get_prompt_request_t;

typedef struct mcp_read_resource_request {
  mcp_string_t uri;
} mcp_read_resource_request_t;

typedef struct mcp_set_level_request {
  mcp_logging_level_t level;
} mcp_set_level_request_t;

/**
 * Subscribe/Unsubscribe requests
 */
typedef struct mcp_subscribe_request {
  mcp_string_t uri;
} mcp_subscribe_request_t;

typedef struct mcp_unsubscribe_request {
  mcp_string_t uri;
} mcp_unsubscribe_request_t;

/**
 * List requests (paginated)
 */
typedef struct mcp_list_resources_request {
  mcp_paginated_request_t base;
} mcp_list_resources_request_t;

typedef struct mcp_list_resource_templates_request {
  mcp_paginated_request_t base;
} mcp_list_resource_templates_request_t;

typedef struct mcp_list_prompts_request {
  mcp_paginated_request_t base;
} mcp_list_prompts_request_t;

typedef struct mcp_list_tools_request {
  mcp_paginated_request_t base;
} mcp_list_tools_request_t;

typedef struct mcp_list_roots_request {
  /* No pagination */
  char _placeholder;
} mcp_list_roots_request_t;

/**
 * Protocol-specific results
 */
typedef struct mcp_list_resources_result {
  mcp_list_t resources; /* List of Resource */
  mcp_optional_t next_cursor;
} mcp_list_resources_result_t;

typedef struct mcp_list_resource_templates_result {
  mcp_list_t resource_templates; /* List of ResourceTemplate */
  mcp_optional_t next_cursor;
} mcp_list_resource_templates_result_t;

typedef struct mcp_read_resource_result {
  mcp_list_t
      contents; /* List of TextResourceContents or BlobResourceContents */
} mcp_read_resource_result_t;

typedef struct mcp_list_prompts_result {
  mcp_list_t prompts; /* List of Prompt */
  mcp_optional_t next_cursor;
} mcp_list_prompts_result_t;

typedef struct mcp_get_prompt_result {
  mcp_optional_t description;
  mcp_list_t messages; /* List of PromptMessage */
} mcp_get_prompt_result_t;

typedef struct mcp_list_tools_result {
  mcp_list_t tools; /* List of Tool */
} mcp_list_tools_result_t;

/**
 * Create message request
 */
typedef struct mcp_create_message_request {
  mcp_list_t messages; /* List of SamplingMessage */
  mcp_optional_t model_preferences; /* ModelPreferences */
  mcp_optional_t system_prompt; /* string */
  mcp_optional_t include_context; /* JSON metadata */
  mcp_optional_t temperature; /* double */
  mcp_optional_t max_tokens; /* int */
  mcp_optional_t stop_sequences; /* List of strings */
  mcp_optional_t metadata; /* JSON */
} mcp_create_message_request_t;

/**
 * Create message result
 */
typedef struct mcp_create_message_result {
  mcp_sampling_message_t message;
  mcp_string_t model;
  mcp_optional_t stop_reason; /* string */
} mcp_create_message_result_t;

typedef struct mcp_completion {
  mcp_list_t values;    /* List of strings */
  mcp_optional_t total; /* double */
  mcp_bool_t has_more;
} mcp_completion_t;

typedef struct mcp_complete_result {
  mcp_completion_t completion;
} mcp_complete_result_t;

typedef struct mcp_list_roots_result {
  mcp_list_t roots; /* List of Root */
} mcp_list_roots_result_t;

typedef enum mcp_elicit_value_type {
  MCP_ELICIT_STRING,
  MCP_ELICIT_NUMBER,
  MCP_ELICIT_BOOLEAN,
  MCP_ELICIT_NULL
} mcp_elicit_value_type_t;

/**
 * Elicit request
 */
typedef struct mcp_elicit_request {
  mcp_string_t name;
  mcp_primitive_schema_t schema;
  mcp_optional_t prompt; /* string */
} mcp_elicit_request_t;

/**
 * Elicit result
 */
typedef struct mcp_elicit_result {
  mcp_elicit_value_type_t type;
  union {
    mcp_string_t string_value;
    double number_value;
    mcp_bool_t boolean_value;
  } value;
} mcp_elicit_result_t;

/**
 * Complete request
 */
typedef struct mcp_complete_request {
  mcp_prompt_reference_t ref;
  mcp_optional_t argument; /* string */
} mcp_complete_request_t;

/**
 * Notification types
 */
typedef struct mcp_progress_notification {
  mcp_progress_token_t progress_token;
  double progress;      /* 0.0 to 1.0 */
  mcp_optional_t total; /* double */
} mcp_progress_notification_t;

typedef struct mcp_cancelled_notification {
  mcp_request_id_t request_id;
  mcp_optional_t reason; /* string */
} mcp_cancelled_notification_t;

typedef struct mcp_logging_message_notification {
  mcp_logging_level_t level;
  mcp_optional_t logger; /* string */
  mcp_json_value_t data; /* string or Metadata */
} mcp_logging_message_notification_t;

typedef struct mcp_resource_updated_notification {
  mcp_string_t uri;
} mcp_resource_updated_notification_t;

typedef struct mcp_resource_list_changed_notification {
  char _placeholder; /* Empty notification */
} mcp_resource_list_changed_notification_t;

typedef struct mcp_prompt_list_changed_notification {
  char _placeholder; /* Empty notification */
} mcp_prompt_list_changed_notification_t;

typedef struct mcp_tool_list_changed_notification {
  char _placeholder; /* Empty notification */
} mcp_tool_list_changed_notification_t;

typedef struct mcp_roots_list_changed_notification {
  char _placeholder; /* Empty notification */
} mcp_roots_list_changed_notification_t;

/**
 * Initialized notification
 */
typedef struct mcp_initialized_notification {
  char _placeholder; /* Empty notification */
} mcp_initialized_notification_t;

/**
 * Ping request/response
 */
typedef struct mcp_ping_request {
  char _placeholder; /* Empty request */
} mcp_ping_request_t;

typedef struct mcp_ping_response {
  char _placeholder; /* Empty response */
} mcp_ping_response_t;

/* ============================================================================
 * Error Data Type Support
 * ============================================================================
 */

typedef enum mcp_error_data_type {
  MCP_ERROR_DATA_NULL,
  MCP_ERROR_DATA_BOOL,
  MCP_ERROR_DATA_INT,
  MCP_ERROR_DATA_DOUBLE,
  MCP_ERROR_DATA_STRING,
  MCP_ERROR_DATA_STRING_ARRAY,
  MCP_ERROR_DATA_STRING_MAP
} mcp_error_data_type_t;

typedef struct mcp_error_data {
  mcp_error_data_type_t type;
  union {
    mcp_bool_t bool_value;
    int64_t int_value;
    double double_value;
    mcp_string_t string_value;
    mcp_list_t string_array; /* List of strings */
    mcp_map_t string_map; /* Map of string to string */
  } value;
} mcp_error_data_t;

/* ============================================================================
 * Response Result Type Support
 * ============================================================================
 */

typedef enum mcp_response_result_type {
  MCP_RESPONSE_RESULT_NULL,
  MCP_RESPONSE_RESULT_BOOL,
  MCP_RESPONSE_RESULT_INT,
  MCP_RESPONSE_RESULT_DOUBLE,
  MCP_RESPONSE_RESULT_STRING,
  MCP_RESPONSE_RESULT_METADATA,
  MCP_RESPONSE_RESULT_CONTENT_BLOCKS,
  MCP_RESPONSE_RESULT_TOOLS,
  MCP_RESPONSE_RESULT_PROMPTS,
  MCP_RESPONSE_RESULT_RESOURCES
} mcp_response_result_type_t;

typedef struct mcp_response_result {
  mcp_response_result_type_t type;
  union {
    mcp_bool_t bool_value;
    int64_t int_value;
    double double_value;
    mcp_string_t string_value;
    mcp_json_value_t metadata;
    mcp_list_t content_blocks; /* List of ContentBlock */
    mcp_list_t tools; /* List of Tool */
    mcp_list_t prompts; /* List of Prompt */
    mcp_list_t resources; /* List of Resource */
  } value;
} mcp_response_result_t;

/* ============================================================================
 * Memory Management
 * ============================================================================
 */

/**
 * Custom allocator interface
 */
typedef struct mcp_allocator {
  void* (*alloc)(size_t size, void* context);
  void* (*realloc)(void* ptr, size_t size, void* context);
  void (*free)(void* ptr, void* context);
  void* context;
} mcp_allocator_t;

/* ============================================================================
 * Builder Functions (FFI-Friendly)
 * ============================================================================
 */

/* String builders */
mcp_string_t mcp_string_from_cstr(const char* str);
mcp_string_t mcp_string_from_data(const char* data, size_t length);
char* mcp_string_to_cstr(mcp_string_t str);
mcp_string_t mcp_string_copy(mcp_string_t str);

/* Request ID builders */
mcp_request_id_t mcp_request_id_from_string(const char* str);
mcp_request_id_t mcp_request_id_from_number(int64_t num);
mcp_request_id_t mcp_request_id_copy(mcp_request_id_t id);

/* Progress token builders */
mcp_progress_token_t mcp_progress_token_from_string(const char* str);
mcp_progress_token_t mcp_progress_token_from_number(int64_t num);

/* Content block builders */
mcp_content_block_t* mcp_text_content_create(const char* text);
mcp_content_block_t* mcp_text_content_with_annotations(
    const char* text, mcp_annotations_t* annotations);
mcp_content_block_t* mcp_image_content_create(const char* data,
                                              const char* mime_type);
mcp_content_block_t* mcp_audio_content_create(const char* data,
                                              const char* mime_type);
mcp_content_block_t* mcp_resource_content_create(
    const mcp_resource_t* resource);
mcp_content_block_t* mcp_resource_link_create(const char* uri,
                                              const char* name);
mcp_content_block_t* mcp_embedded_resource_create(
    const mcp_resource_t* resource);

/* Resource builders */
mcp_resource_t* mcp_resource_create(const char* uri, const char* name);
mcp_resource_t* mcp_resource_with_description(const char* uri,
                                              const char* name,
                                              const char* description);
mcp_resource_t* mcp_resource_with_mime_type(const char* uri,
                                            const char* name,
                                            const char* mime_type);

/* Tool & Prompt builders */
mcp_tool_t* mcp_tool_create(const char* name);
mcp_tool_t* mcp_tool_with_description(const char* name,
                                      const char* description);
mcp_tool_t* mcp_tool_with_schema(const char* name, mcp_json_value_t schema);
mcp_tool_t* mcp_tool_add_parameter(mcp_tool_t* tool,
                                   const char* param_name,
                                   const char* param_type,
                                   mcp_bool_t required);

mcp_prompt_t* mcp_prompt_create(const char* name);
mcp_prompt_t* mcp_prompt_with_description(const char* name,
                                          const char* description);
mcp_prompt_t* mcp_prompt_add_argument(mcp_prompt_t* prompt,
                                      const char* arg_name,
                                      const char* description,
                                      mcp_bool_t required);

/* Message builders */
mcp_message_t* mcp_message_create(mcp_role_t role,
                                  mcp_content_block_t* content);
mcp_message_t* mcp_user_message(const char* text);
mcp_message_t* mcp_assistant_message(const char* text);
mcp_prompt_message_t* mcp_prompt_message_create(mcp_role_t role,
                                                mcp_content_block_t* content);
mcp_sampling_message_t* mcp_sampling_message_create(
    mcp_role_t role, mcp_content_block_t* content);

/* Error builders */
mcp_jsonrpc_error_t* mcp_error_create(int32_t code, const char* message);
mcp_jsonrpc_error_t* mcp_error_with_data(int32_t code,
                                         const char* message,
                                         mcp_json_value_t data);
mcp_jsonrpc_error_t* mcp_error_parse(void);
mcp_jsonrpc_error_t* mcp_error_invalid_request(void);
mcp_jsonrpc_error_t* mcp_error_method_not_found(const char* method);
mcp_jsonrpc_error_t* mcp_error_invalid_params(const char* details);
mcp_jsonrpc_error_t* mcp_error_internal(const char* details);

/* Capability builders */
mcp_client_capabilities_t* mcp_client_capabilities_create(void);
mcp_client_capabilities_t* mcp_client_capabilities_with_experimental(
    mcp_json_value_t experimental);
mcp_client_capabilities_t* mcp_client_capabilities_with_sampling(
    mcp_sampling_params_t* params);
mcp_client_capabilities_t* mcp_client_capabilities_with_roots(
    mcp_roots_capability_t* roots);

mcp_server_capabilities_t* mcp_server_capabilities_create(void);
mcp_server_capabilities_t* mcp_server_capabilities_with_tools(mcp_bool_t enabled);
mcp_server_capabilities_t* mcp_server_capabilities_with_prompts(mcp_bool_t enabled);
mcp_server_capabilities_t* mcp_server_capabilities_with_resources(mcp_bool_t enabled);
mcp_server_capabilities_t* mcp_server_capabilities_with_resources_advanced(
    mcp_resources_capability_t* resources);
mcp_server_capabilities_t* mcp_server_capabilities_with_logging(mcp_bool_t enabled);

/* Request builders */
mcp_initialize_request_t* mcp_initialize_request_create(
    const char* protocol_version, mcp_client_capabilities_t* capabilities);
mcp_initialize_request_t* mcp_initialize_request_with_client_info(
    const char* protocol_version,
    mcp_client_capabilities_t* capabilities,
    const char* client_name,
    const char* client_version);

mcp_call_tool_request_t* mcp_call_tool_request_create(const char* name);
mcp_call_tool_request_t* mcp_call_tool_request_with_arguments(
    const char* name, mcp_json_value_t arguments);

mcp_get_prompt_request_t* mcp_get_prompt_request_create(const char* name);
mcp_get_prompt_request_t* mcp_get_prompt_request_with_arguments(
    const char* name, mcp_json_value_t arguments);

mcp_read_resource_request_t* mcp_read_resource_request_create(const char* uri);

mcp_set_level_request_t* mcp_set_level_request_create(
    mcp_logging_level_t level);

/* Result builders */
mcp_initialize_result_t* mcp_initialize_result_create(
    const char* protocol_version, mcp_server_capabilities_t* capabilities);
mcp_initialize_result_t* mcp_initialize_result_with_server_info(
    const char* protocol_version,
    mcp_server_capabilities_t* capabilities,
    const char* server_name,
    const char* server_version);
mcp_initialize_result_t* mcp_initialize_result_with_instructions(
    const char* protocol_version,
    mcp_server_capabilities_t* capabilities,
    const char* instructions);

mcp_call_tool_result_t* mcp_call_tool_result_create(void);
mcp_call_tool_result_t* mcp_call_tool_result_add_content(
    mcp_call_tool_result_t* result, mcp_content_block_t* content);
mcp_call_tool_result_t* mcp_call_tool_result_add_text(
    mcp_call_tool_result_t* result, const char* text);
mcp_call_tool_result_t* mcp_call_tool_result_set_error(
    mcp_call_tool_result_t* result, mcp_bool_t is_error);

/* Notification builders */
mcp_progress_notification_t* mcp_progress_notification_create(
    mcp_progress_token_t token, double progress);
mcp_progress_notification_t* mcp_progress_notification_with_total(
    mcp_progress_token_t token, double progress, double total);

mcp_cancelled_notification_t* mcp_cancelled_notification_create(
    mcp_request_id_t request_id);
mcp_cancelled_notification_t* mcp_cancelled_notification_with_reason(
    mcp_request_id_t request_id, const char* reason);

mcp_logging_message_notification_t* mcp_logging_notification_create(
    mcp_logging_level_t level, const char* message);
mcp_logging_message_notification_t* mcp_logging_notification_with_logger(
    mcp_logging_level_t level, const char* logger, const char* message);

/* Schema builders */
mcp_primitive_schema_t* mcp_string_schema_create(void);
mcp_primitive_schema_t* mcp_string_schema_with_constraints(int min_length,
                                                           int max_length,
                                                           const char* pattern);
mcp_primitive_schema_t* mcp_number_schema_create(void);
mcp_primitive_schema_t* mcp_number_schema_with_range(double minimum,
                                                     double maximum);
mcp_primitive_schema_t* mcp_boolean_schema_create(void);
mcp_primitive_schema_t* mcp_enum_schema_create(const char** values,
                                               size_t count);

/* ============================================================================
 * JSON Serialization/Deserialization (FFI-Friendly)
 * ============================================================================
 */

/* Serialize to JSON */
mcp_json_value_t mcp_string_to_json(mcp_string_t str);
mcp_json_value_t mcp_request_id_to_json(const mcp_request_id_t* id);
mcp_json_value_t mcp_progress_token_to_json(const mcp_progress_token_t* token);
mcp_json_value_t mcp_role_to_json(mcp_role_t role);
mcp_json_value_t mcp_logging_level_to_json(mcp_logging_level_t level);
mcp_json_value_t mcp_content_block_to_json(const mcp_content_block_t* block);
mcp_json_value_t mcp_resource_to_json(const mcp_resource_t* resource);
mcp_json_value_t mcp_tool_to_json(const mcp_tool_t* tool);
mcp_json_value_t mcp_prompt_to_json(const mcp_prompt_t* prompt);
mcp_json_value_t mcp_message_to_json(const mcp_message_t* message);
mcp_json_value_t mcp_error_to_json(const mcp_jsonrpc_error_t* error);
mcp_json_value_t mcp_client_capabilities_to_json(
    const mcp_client_capabilities_t* caps);
mcp_json_value_t mcp_server_capabilities_to_json(
    const mcp_server_capabilities_t* caps);
mcp_json_value_t mcp_implementation_to_json(const mcp_implementation_t* impl);
mcp_json_value_t mcp_initialize_request_to_json(
    const mcp_initialize_request_t* req);
mcp_json_value_t mcp_initialize_result_to_json(
    const mcp_initialize_result_t* result);
mcp_json_value_t mcp_call_tool_request_to_json(
    const mcp_call_tool_request_t* req);
mcp_json_value_t mcp_call_tool_result_to_json(
    const mcp_call_tool_result_t* result);
mcp_json_value_t mcp_jsonrpc_request_to_json(const mcp_jsonrpc_request_t* req);
mcp_json_value_t mcp_jsonrpc_response_to_json(
    const mcp_jsonrpc_response_t* resp);
mcp_json_value_t mcp_jsonrpc_notification_to_json(
    const mcp_jsonrpc_notification_t* notif);

/* Deserialize from JSON */
mcp_string_t mcp_string_from_json(mcp_json_value_t json);
mcp_request_id_t* mcp_request_id_from_json(mcp_json_value_t json);
mcp_progress_token_t* mcp_progress_token_from_json(mcp_json_value_t json);
mcp_role_t mcp_role_from_json(mcp_json_value_t json);
mcp_logging_level_t mcp_logging_level_from_json(mcp_json_value_t json);
mcp_content_block_t* mcp_content_block_from_json(mcp_json_value_t json);
mcp_resource_t* mcp_resource_from_json(mcp_json_value_t json);
mcp_tool_t* mcp_tool_from_json(mcp_json_value_t json);
mcp_prompt_t* mcp_prompt_from_json(mcp_json_value_t json);
mcp_message_t* mcp_message_from_json(mcp_json_value_t json);
mcp_jsonrpc_error_t* mcp_error_from_json(mcp_json_value_t json);
mcp_client_capabilities_t* mcp_client_capabilities_from_json(
    mcp_json_value_t json);
mcp_server_capabilities_t* mcp_server_capabilities_from_json(
    mcp_json_value_t json);
mcp_implementation_t* mcp_implementation_from_json(mcp_json_value_t json);
mcp_initialize_request_t* mcp_initialize_request_from_json(
    mcp_json_value_t json);
mcp_initialize_result_t* mcp_initialize_result_from_json(mcp_json_value_t json);
mcp_call_tool_request_t* mcp_call_tool_request_from_json(mcp_json_value_t json);
mcp_call_tool_result_t* mcp_call_tool_result_from_json(mcp_json_value_t json);
mcp_jsonrpc_request_t* mcp_jsonrpc_request_from_json(mcp_json_value_t json);
mcp_jsonrpc_response_t* mcp_jsonrpc_response_from_json(mcp_json_value_t json);
mcp_jsonrpc_notification_t* mcp_jsonrpc_notification_from_json(
    mcp_json_value_t json);

/* ============================================================================
 * Utility Functions (FFI-Friendly)
 * ============================================================================
 */

/* Type checking utilities */
mcp_bool_t mcp_request_id_is_string(const mcp_request_id_t* id);
mcp_bool_t mcp_request_id_is_number(const mcp_request_id_t* id);
mcp_bool_t mcp_progress_token_is_string(const mcp_progress_token_t* token);
mcp_bool_t mcp_progress_token_is_number(const mcp_progress_token_t* token);
mcp_bool_t mcp_content_block_is_text(const mcp_content_block_t* block);
mcp_bool_t mcp_content_block_is_image(const mcp_content_block_t* block);
mcp_bool_t mcp_content_block_is_audio(const mcp_content_block_t* block);
mcp_bool_t mcp_content_block_is_resource(const mcp_content_block_t* block);

/* Comparison utilities */
mcp_bool_t mcp_string_equals(mcp_string_t a, mcp_string_t b);
mcp_bool_t mcp_string_equals_cstr(mcp_string_t str, const char* cstr);
mcp_bool_t mcp_request_id_equals(const mcp_request_id_t* a,
                           const mcp_request_id_t* b);
int mcp_string_compare(mcp_string_t a, mcp_string_t b);

/* Conversion utilities */
const char* mcp_role_to_string(mcp_role_t role);
mcp_role_t mcp_role_from_string(const char* str);
const char* mcp_logging_level_to_string(mcp_logging_level_t level);
mcp_logging_level_t mcp_logging_level_from_string(const char* str);
const char* mcp_content_block_type_to_string(mcp_content_block_type_t type);

/* List operations */
mcp_list_t* mcp_list_create(size_t initial_capacity);
mcp_result_t mcp_list_append(mcp_list_t* list, void* item);
mcp_result_t mcp_list_insert(mcp_list_t* list, size_t index, void* item);
void* mcp_list_get(const mcp_list_t* list, size_t index);
mcp_result_t mcp_list_remove(mcp_list_t* list, size_t index);
void mcp_list_clear(mcp_list_t* list);
void mcp_list_free(mcp_list_t* list);
size_t mcp_list_size(const mcp_list_t* list);
mcp_bool_t mcp_list_is_empty(const mcp_list_t* list);

/* Map operations */
mcp_map_t* mcp_map_create(size_t initial_capacity);
mcp_result_t mcp_map_set(mcp_map_t* map, const char* key, void* value);
void* mcp_map_get(const mcp_map_t* map, const char* key);
mcp_bool_t mcp_map_has(const mcp_map_t* map, const char* key);
mcp_result_t mcp_map_remove(mcp_map_t* map, const char* key);
void mcp_map_clear(mcp_map_t* map);
void mcp_map_free(mcp_map_t* map);
size_t mcp_map_size(const mcp_map_t* map);
mcp_bool_t mcp_map_is_empty(const mcp_map_t* map);
mcp_list_t* mcp_map_keys(const mcp_map_t* map);
mcp_list_t* mcp_map_values(const mcp_map_t* map);

/* Optional operations */
mcp_optional_t* mcp_optional_create(void* value);
mcp_optional_t* mcp_optional_empty(void);
mcp_bool_t mcp_optional_has_value(const mcp_optional_t* opt);
void* mcp_optional_get_value(const mcp_optional_t* opt);
void mcp_optional_set_value(mcp_optional_t* opt, void* value);
void mcp_optional_clear(mcp_optional_t* opt);
void mcp_optional_free(mcp_optional_t* opt);

/* ============================================================================
 * Memory Management Functions (FFI-Friendly)
 * ============================================================================
 */

/* Free functions for all types */
void mcp_string_free(mcp_string_t* str);
void mcp_request_id_free(mcp_request_id_t* id);
void mcp_progress_token_free(mcp_progress_token_t* token);
void mcp_content_block_free(mcp_content_block_t* block);
void mcp_resource_free(mcp_resource_t* resource);
void mcp_tool_free(mcp_tool_t* tool);
void mcp_prompt_free(mcp_prompt_t* prompt);
void mcp_message_free(mcp_message_t* message);
void mcp_error_free(mcp_jsonrpc_error_t* error);
void mcp_client_capabilities_free(mcp_client_capabilities_t* caps);
void mcp_server_capabilities_free(mcp_server_capabilities_t* caps);
void mcp_implementation_free(mcp_implementation_t* impl);
void mcp_initialize_request_free(mcp_initialize_request_t* req);
void mcp_initialize_result_free(mcp_initialize_result_t* result);
void mcp_call_tool_request_free(mcp_call_tool_request_t* req);
void mcp_call_tool_result_free(mcp_call_tool_result_t* result);
void mcp_jsonrpc_request_free(mcp_jsonrpc_request_t* req);
void mcp_jsonrpc_response_free(mcp_jsonrpc_response_t* resp);
void mcp_jsonrpc_notification_free(mcp_jsonrpc_notification_t* notif);
void mcp_primitive_schema_free(mcp_primitive_schema_t* schema);
void mcp_sampling_params_free(mcp_sampling_params_t* params);
void mcp_model_preferences_free(mcp_model_preferences_t* prefs);
void mcp_prompt_argument_free(mcp_prompt_argument_t* arg);
void mcp_progress_notification_free(mcp_progress_notification_t* notif);
void mcp_cancelled_notification_free(mcp_cancelled_notification_t* notif);
void mcp_logging_message_notification_free(mcp_logging_message_notification_t* notif);
void mcp_resource_updated_notification_free(mcp_resource_updated_notification_t* notif);
void mcp_resource_list_changed_notification_free(mcp_resource_list_changed_notification_t* notif);
void mcp_prompt_list_changed_notification_free(mcp_prompt_list_changed_notification_t* notif);
void mcp_tool_list_changed_notification_free(mcp_tool_list_changed_notification_t* notif);
void mcp_roots_list_changed_notification_free(mcp_roots_list_changed_notification_t* notif);
void mcp_initialized_notification_free(mcp_initialized_notification_t* notif);
void mcp_ping_request_free(mcp_ping_request_t* req);
void mcp_ping_response_free(mcp_ping_response_t* resp);
void mcp_error_data_free(mcp_error_data_t* data);
void mcp_response_result_free(mcp_response_result_t* result);
void mcp_create_message_request_free(mcp_create_message_request_t* req);
void mcp_create_message_result_free(mcp_create_message_result_t* result);
void mcp_elicit_request_free(mcp_elicit_request_t* req);
void mcp_elicit_result_free(mcp_elicit_result_t* result);
void mcp_complete_request_free(mcp_complete_request_t* req);
void mcp_complete_result_free(mcp_complete_result_t* result);
void mcp_list_resources_request_free(mcp_list_resources_request_t* req);
void mcp_list_resource_templates_request_free(mcp_list_resource_templates_request_t* req);
void mcp_list_prompts_request_free(mcp_list_prompts_request_t* req);
void mcp_list_tools_request_free(mcp_list_tools_request_t* req);
void mcp_list_roots_request_free(mcp_list_roots_request_t* req);
void mcp_subscribe_request_free(mcp_subscribe_request_t* req);
void mcp_unsubscribe_request_free(mcp_unsubscribe_request_t* req);
void mcp_get_prompt_request_free(mcp_get_prompt_request_t* req);
void mcp_read_resource_request_free(mcp_read_resource_request_t* req);
void mcp_set_level_request_free(mcp_set_level_request_t* req);
void mcp_list_resources_result_free(mcp_list_resources_result_t* result);
void mcp_list_resource_templates_result_free(mcp_list_resource_templates_result_t* result);
void mcp_read_resource_result_free(mcp_read_resource_result_t* result);
void mcp_list_prompts_result_free(mcp_list_prompts_result_t* result);
void mcp_get_prompt_result_free(mcp_get_prompt_result_t* result);
void mcp_list_tools_result_free(mcp_list_tools_result_t* result);
void mcp_list_roots_result_free(mcp_list_roots_result_t* result);
void mcp_prompt_message_free(mcp_prompt_message_t* msg);
void mcp_sampling_message_free(mcp_sampling_message_t* msg);
void mcp_annotations_free(mcp_annotations_t* annotations);
void mcp_tool_annotations_free(mcp_tool_annotations_t* annotations);
void mcp_audio_content_free(mcp_audio_content_t* content);
void mcp_resource_link_free(mcp_resource_link_t* link);
void mcp_embedded_resource_free(mcp_embedded_resource_t* resource);
void mcp_resource_contents_free(mcp_resource_contents_t* contents);
void mcp_text_resource_contents_free(mcp_text_resource_contents_t* contents);
void mcp_blob_resource_contents_free(mcp_blob_resource_contents_t* contents);
void mcp_root_free(mcp_root_t* root);
void mcp_model_hint_free(mcp_model_hint_t* hint);
void mcp_string_schema_free(mcp_string_schema_t* schema);
void mcp_number_schema_free(mcp_number_schema_t* schema);
void mcp_boolean_schema_free(mcp_boolean_schema_t* schema);
void mcp_enum_schema_free(mcp_enum_schema_t* schema);
void mcp_resource_template_reference_free(mcp_resource_template_reference_t* ref);
void mcp_prompt_reference_free(mcp_prompt_reference_t* ref);
void mcp_empty_capability_free(mcp_empty_capability_t* cap);
void mcp_resources_capability_free(mcp_resources_capability_t* cap);
void mcp_prompts_capability_free(mcp_prompts_capability_t* cap);
void mcp_roots_capability_free(mcp_roots_capability_t* cap);
void mcp_empty_result_free(mcp_empty_result_t* result);
void mcp_completion_free(mcp_completion_t* completion);
void mcp_paginated_request_free(mcp_paginated_request_t* req);
void mcp_paginated_result_free(mcp_paginated_result_t* result);
void mcp_resource_template_free(mcp_resource_template_t* tmpl);
void mcp_tool_parameter_free(mcp_tool_parameter_t* param);

/* Deep copy functions */
mcp_content_block_t* mcp_content_block_copy(const mcp_content_block_t* block);
mcp_resource_t* mcp_resource_copy(const mcp_resource_t* resource);
mcp_tool_t* mcp_tool_copy(const mcp_tool_t* tool);
mcp_prompt_t* mcp_prompt_copy(const mcp_prompt_t* prompt);
mcp_message_t* mcp_message_copy(const mcp_message_t* message);
mcp_client_capabilities_t* mcp_client_capabilities_copy(
    const mcp_client_capabilities_t* caps);
mcp_server_capabilities_t* mcp_server_capabilities_copy(
    const mcp_server_capabilities_t* caps);

/* Memory pool support for efficient allocation */
typedef struct mcp_memory_pool mcp_memory_pool_t;
mcp_memory_pool_t* mcp_memory_pool_create(size_t initial_size);
void* mcp_memory_pool_alloc(mcp_memory_pool_t* pool, size_t size);
void mcp_memory_pool_reset(mcp_memory_pool_t* pool);
void mcp_memory_pool_destroy(mcp_memory_pool_t* pool);

/* ============================================================================
 * FFI Safety and ABI Stability Functions
 * ============================================================================
 */

/**
 * ABI version info for compatibility checking
 */
#define MCP_C_API_VERSION_MAJOR 1
#define MCP_C_API_VERSION_MINOR 0
#define MCP_C_API_VERSION_PATCH 0

typedef struct mcp_abi_version {
  uint32_t major;
  uint32_t minor;
  uint32_t patch;
  uint32_t reserved;
} mcp_abi_version_t;

MCP_EXPORT mcp_abi_version_t mcp_get_abi_version(void);
MCP_EXPORT mcp_bool_t mcp_check_abi_compatibility(uint32_t major, uint32_t minor);

/**
 * Thread-safe error handling for FFI
 */
MCP_EXPORT const char* mcp_get_last_error(void);
MCP_EXPORT void mcp_set_last_error(const char* error);
MCP_EXPORT void mcp_clear_last_error(void);

/**
 * Safe memory allocation with error checking
 */
MCP_EXPORT void* mcp_malloc_safe(size_t size);
MCP_EXPORT void* mcp_calloc_safe(size_t count, size_t size);
MCP_EXPORT void* mcp_realloc_safe(void* ptr, size_t new_size);
MCP_EXPORT void mcp_free_safe(void* ptr);
MCP_EXPORT char* mcp_strdup_safe(const char* str);
MCP_EXPORT char* mcp_strndup_safe(const char* str, size_t max_len);

/**
 * FFI initialization and cleanup
 */
MCP_EXPORT mcp_result_t mcp_ffi_initialize(void);
MCP_EXPORT void mcp_ffi_cleanup(void);
MCP_EXPORT mcp_bool_t mcp_ffi_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* MCP_C_TYPES_H */