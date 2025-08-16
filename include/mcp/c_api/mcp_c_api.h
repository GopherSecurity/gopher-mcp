/**
 * @file mcp_c_api.h
 * @brief C API for MCP C++ SDK
 * 
 * This header provides the complete C API for the MCP C++ SDK.
 * It follows the event-driven, thread-confined architecture of the C++ SDK
 * while providing a C-compatible interface for FFI bindings.
 * 
 * Architecture:
 * - All operations happen in dispatcher thread context
 * - Callbacks are invoked in dispatcher thread
 * - No manual thread synchronization needed
 * - Follows Create → Configure → Connect → Operate → Close lifecycle
 */

#ifndef MCP_C_API_H
#define MCP_C_API_H

#include "mcp_c_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Library Initialization & Cleanup
 * ============================================================================ */

/**
 * Initialize the MCP library
 * Must be called before any other API functions
 * @param allocator Custom allocator (NULL for default)
 * @return MCP_OK on success
 */
mcp_result_t mcp_init(const mcp_allocator_t* allocator);

/**
 * Shutdown the MCP library
 * Cleans up all resources
 */
void mcp_shutdown(void);

/**
 * Get version string
 * @return Version string (do not free)
 */
const char* mcp_get_version(void);

/**
 * Get last error message for current thread
 * @return Error message (do not free)
 */
const char* mcp_get_last_error(void);

/* ============================================================================
 * Event Loop & Dispatcher
 * ============================================================================ */

/**
 * Create a new dispatcher (event loop)
 * @return Dispatcher handle or NULL on error
 */
mcp_dispatcher_t mcp_dispatcher_create(void);

/**
 * Run the dispatcher (blocks until stopped)
 * @param dispatcher Dispatcher handle
 * @return MCP_OK on success
 */
mcp_result_t mcp_dispatcher_run(mcp_dispatcher_t dispatcher);

/**
 * Run dispatcher for specified duration
 * @param dispatcher Dispatcher handle
 * @param timeout_ms Maximum time to run in milliseconds
 * @return MCP_OK on success
 */
mcp_result_t mcp_dispatcher_run_timeout(mcp_dispatcher_t dispatcher, uint32_t timeout_ms);

/**
 * Stop the dispatcher
 * @param dispatcher Dispatcher handle
 */
void mcp_dispatcher_stop(mcp_dispatcher_t dispatcher);

/**
 * Post a callback to dispatcher thread
 * @param dispatcher Dispatcher handle
 * @param callback Callback function
 * @param user_data User data for callback
 * @return MCP_OK on success
 */
mcp_result_t mcp_dispatcher_post(
    mcp_dispatcher_t dispatcher,
    mcp_callback_t callback,
    void* user_data
);

/**
 * Check if current thread is dispatcher thread
 * @param dispatcher Dispatcher handle
 * @return true if in dispatcher thread
 */
bool mcp_dispatcher_is_thread(mcp_dispatcher_t dispatcher);

/**
 * Create a timer
 * @param dispatcher Dispatcher handle
 * @param callback Timer callback
 * @param user_data User data for callback
 * @return Timer ID or 0 on error
 */
uint64_t mcp_dispatcher_create_timer(
    mcp_dispatcher_t dispatcher,
    mcp_timer_callback_t callback,
    void* user_data
);

/**
 * Enable/arm a timer
 * @param dispatcher Dispatcher handle
 * @param timer_id Timer ID
 * @param timeout_ms Timeout in milliseconds
 * @param repeat Whether to repeat
 * @return MCP_OK on success
 */
mcp_result_t mcp_dispatcher_enable_timer(
    mcp_dispatcher_t dispatcher,
    uint64_t timer_id,
    uint32_t timeout_ms,
    bool repeat
);

/**
 * Disable a timer
 * @param dispatcher Dispatcher handle
 * @param timer_id Timer ID
 */
void mcp_dispatcher_disable_timer(mcp_dispatcher_t dispatcher, uint64_t timer_id);

/**
 * Destroy a timer
 * @param dispatcher Dispatcher handle
 * @param timer_id Timer ID
 */
void mcp_dispatcher_destroy_timer(mcp_dispatcher_t dispatcher, uint64_t timer_id);

/**
 * Destroy dispatcher
 * @param dispatcher Dispatcher handle
 */
void mcp_dispatcher_destroy(mcp_dispatcher_t dispatcher);

/* ============================================================================
 * Connection Management
 * ============================================================================ */

/**
 * Create a client connection
 * @param dispatcher Dispatcher handle
 * @param transport Transport type
 * @return Connection handle or NULL on error
 */
mcp_connection_t mcp_connection_create_client(
    mcp_dispatcher_t dispatcher,
    mcp_transport_type_t transport
);

/**
 * Configure connection
 * @param connection Connection handle
 * @param address Target address (NULL for stdio)
 * @param options Socket options (NULL for defaults)
 * @param ssl_config SSL configuration (NULL for non-SSL)
 * @return MCP_OK on success
 */
mcp_result_t mcp_connection_configure(
    mcp_connection_t connection,
    const mcp_address_t* address,
    const mcp_socket_options_t* options,
    const mcp_ssl_config_t* ssl_config
);

/**
 * Set connection callbacks
 * @param connection Connection handle
 * @param state_cb State change callback
 * @param data_cb Data received callback
 * @param error_cb Error callback
 * @param user_data User data for callbacks
 * @return MCP_OK on success
 */
mcp_result_t mcp_connection_set_callbacks(
    mcp_connection_t connection,
    mcp_connection_state_callback_t state_cb,
    mcp_data_callback_t data_cb,
    mcp_error_callback_t error_cb,
    void* user_data
);

/**
 * Set watermarks for flow control
 * @param connection Connection handle
 * @param config Watermark configuration
 * @return MCP_OK on success
 */
mcp_result_t mcp_connection_set_watermarks(
    mcp_connection_t connection,
    const mcp_watermark_config_t* config
);

/**
 * Connect (async)
 * @param connection Connection handle
 * @return MCP_OK if connection started
 */
mcp_result_t mcp_connection_connect(mcp_connection_t connection);

/**
 * Write data (async)
 * @param connection Connection handle
 * @param data Data to write
 * @param length Data length
 * @param callback Write complete callback (optional)
 * @param user_data User data for callback
 * @return MCP_OK if write queued
 */
mcp_result_t mcp_connection_write(
    mcp_connection_t connection,
    const uint8_t* data,
    size_t length,
    mcp_write_callback_t callback,
    void* user_data
);

/**
 * Close connection
 * @param connection Connection handle
 * @param flush Whether to flush pending writes
 * @return MCP_OK on success
 */
mcp_result_t mcp_connection_close(mcp_connection_t connection, bool flush);

/**
 * Get connection state
 * @param connection Connection handle
 * @return Current connection state
 */
mcp_connection_state_t mcp_connection_get_state(mcp_connection_t connection);

/**
 * Get connection statistics
 * @param connection Connection handle
 * @param bytes_read Output: total bytes read
 * @param bytes_written Output: total bytes written
 * @return MCP_OK on success
 */
mcp_result_t mcp_connection_get_stats(
    mcp_connection_t connection,
    uint64_t* bytes_read,
    uint64_t* bytes_written
);

/**
 * Destroy connection
 * @param connection Connection handle
 */
void mcp_connection_destroy(mcp_connection_t connection);

/* ============================================================================
 * Server & Listener
 * ============================================================================ */

/**
 * Create a listener
 * @param dispatcher Dispatcher handle
 * @param transport Transport type
 * @return Listener handle or NULL on error
 */
mcp_listener_t mcp_listener_create(
    mcp_dispatcher_t dispatcher,
    mcp_transport_type_t transport
);

/**
 * Configure listener
 * @param listener Listener handle
 * @param address Bind address (NULL for stdio)
 * @param options Socket options (NULL for defaults)
 * @param ssl_config SSL configuration (NULL for non-SSL)
 * @return MCP_OK on success
 */
mcp_result_t mcp_listener_configure(
    mcp_listener_t listener,
    const mcp_address_t* address,
    const mcp_socket_options_t* options,
    const mcp_ssl_config_t* ssl_config
);

/**
 * Set accept callback
 * @param listener Listener handle
 * @param callback Accept callback
 * @param user_data User data for callback
 * @return MCP_OK on success
 */
mcp_result_t mcp_listener_set_accept_callback(
    mcp_listener_t listener,
    mcp_accept_callback_t callback,
    void* user_data
);

/**
 * Start listening
 * @param listener Listener handle
 * @param backlog Connection backlog
 * @return MCP_OK on success
 */
mcp_result_t mcp_listener_start(mcp_listener_t listener, int backlog);

/**
 * Stop listening
 * @param listener Listener handle
 */
void mcp_listener_stop(mcp_listener_t listener);

/**
 * Destroy listener
 * @param listener Listener handle
 */
void mcp_listener_destroy(mcp_listener_t listener);

/* ============================================================================
 * MCP Client
 * ============================================================================ */

/**
 * Create MCP client
 * @param dispatcher Dispatcher handle
 * @param config Client configuration
 * @return Client handle or NULL on error
 */
mcp_client_t mcp_client_create(
    mcp_dispatcher_t dispatcher,
    const mcp_client_config_t* config
);

/**
 * Set MCP message callbacks
 * @param client Client handle
 * @param request_cb Request callback
 * @param response_cb Response callback
 * @param notification_cb Notification callback
 * @param user_data User data for callbacks
 * @return MCP_OK on success
 */
mcp_result_t mcp_client_set_callbacks(
    mcp_client_t client,
    mcp_request_callback_t request_cb,
    mcp_response_callback_t response_cb,
    mcp_notification_callback_t notification_cb,
    void* user_data
);

/**
 * Connect client
 * @param client Client handle
 * @return MCP_OK if connection started
 */
mcp_result_t mcp_client_connect(mcp_client_t client);

/**
 * Initialize protocol handshake
 * @param client Client handle
 * @return Request ID for tracking
 */
mcp_request_id_t mcp_client_initialize(mcp_client_t client);

/**
 * Send request
 * @param client Client handle
 * @param method Method name
 * @param params Parameters (JSON value)
 * @return Request ID for tracking
 */
mcp_request_id_t mcp_client_send_request(
    mcp_client_t client,
    mcp_string_t method,
    mcp_json_value_t params
);

/**
 * Send notification
 * @param client Client handle
 * @param method Method name
 * @param params Parameters (JSON value)
 * @return MCP_OK on success
 */
mcp_result_t mcp_client_send_notification(
    mcp_client_t client,
    mcp_string_t method,
    mcp_json_value_t params
);

/**
 * List available tools
 * @param client Client handle
 * @return Request ID for tracking
 */
mcp_request_id_t mcp_client_list_tools(mcp_client_t client);

/**
 * Call a tool
 * @param client Client handle
 * @param name Tool name
 * @param arguments Tool arguments (JSON value)
 * @return Request ID for tracking
 */
mcp_request_id_t mcp_client_call_tool(
    mcp_client_t client,
    mcp_string_t name,
    mcp_json_value_t arguments
);

/**
 * List resources
 * @param client Client handle
 * @return Request ID for tracking
 */
mcp_request_id_t mcp_client_list_resources(mcp_client_t client);

/**
 * Read resource
 * @param client Client handle
 * @param uri Resource URI
 * @return Request ID for tracking
 */
mcp_request_id_t mcp_client_read_resource(
    mcp_client_t client,
    mcp_string_t uri
);

/**
 * List prompts
 * @param client Client handle
 * @return Request ID for tracking
 */
mcp_request_id_t mcp_client_list_prompts(mcp_client_t client);

/**
 * Get prompt
 * @param client Client handle
 * @param name Prompt name
 * @param arguments Prompt arguments (map of string to string)
 * @return Request ID for tracking
 */
mcp_request_id_t mcp_client_get_prompt(
    mcp_client_t client,
    mcp_string_t name,
    mcp_map_t arguments
);

/**
 * Disconnect client
 * @param client Client handle
 */
void mcp_client_disconnect(mcp_client_t client);

/**
 * Destroy client
 * @param client Client handle
 */
void mcp_client_destroy(mcp_client_t client);

/* ============================================================================
 * MCP Server
 * ============================================================================ */

/**
 * Create MCP server
 * @param dispatcher Dispatcher handle
 * @param config Server configuration
 * @return Server handle or NULL on error
 */
mcp_server_t mcp_server_create(
    mcp_dispatcher_t dispatcher,
    const mcp_server_config_t* config
);

/**
 * Set MCP message callbacks
 * @param server Server handle
 * @param request_cb Request callback
 * @param notification_cb Notification callback
 * @param user_data User data for callbacks
 * @return MCP_OK on success
 */
mcp_result_t mcp_server_set_callbacks(
    mcp_server_t server,
    mcp_request_callback_t request_cb,
    mcp_notification_callback_t notification_cb,
    void* user_data
);

/**
 * Register tool
 * @param server Server handle
 * @param tool Tool definition
 * @return MCP_OK on success
 */
mcp_result_t mcp_server_register_tool(
    mcp_server_t server,
    const mcp_tool_t* tool
);

/**
 * Register resource template
 * @param server Server handle
 * @param resource Resource template
 * @return MCP_OK on success
 */
mcp_result_t mcp_server_register_resource(
    mcp_server_t server,
    const mcp_resource_template_t* resource
);

/**
 * Register prompt
 * @param server Server handle
 * @param prompt Prompt definition
 * @return MCP_OK on success
 */
mcp_result_t mcp_server_register_prompt(
    mcp_server_t server,
    const mcp_prompt_t* prompt
);

/**
 * Start server
 * @param server Server handle
 * @return MCP_OK on success
 */
mcp_result_t mcp_server_start(mcp_server_t server);

/**
 * Send response
 * @param server Server handle
 * @param request_id Request ID to respond to
 * @param result Result (JSON value)
 * @return MCP_OK on success
 */
mcp_result_t mcp_server_send_response(
    mcp_server_t server,
    mcp_request_id_t request_id,
    mcp_json_value_t result
);

/**
 * Send error response
 * @param server Server handle
 * @param request_id Request ID to respond to
 * @param error Error details
 * @return MCP_OK on success
 */
mcp_result_t mcp_server_send_error(
    mcp_server_t server,
    mcp_request_id_t request_id,
    const mcp_jsonrpc_error_t* error
);

/**
 * Send notification
 * @param server Server handle
 * @param method Method name
 * @param params Parameters (JSON value)
 * @return MCP_OK on success
 */
mcp_result_t mcp_server_send_notification(
    mcp_server_t server,
    mcp_string_t method,
    mcp_json_value_t params
);

/**
 * Stop server
 * @param server Server handle
 */
void mcp_server_stop(mcp_server_t server);

/**
 * Destroy server
 * @param server Server handle
 */
void mcp_server_destroy(mcp_server_t server);

/* ============================================================================
 * JSON Value Management
 * ============================================================================ */

/**
 * Create JSON null value
 * @return JSON value handle
 */
mcp_json_value_t mcp_json_null(void);

/**
 * Create JSON boolean value
 * @param value Boolean value
 * @return JSON value handle
 */
mcp_json_value_t mcp_json_bool(bool value);

/**
 * Create JSON number value
 * @param value Number value
 * @return JSON value handle
 */
mcp_json_value_t mcp_json_number(double value);

/**
 * Create JSON string value
 * @param value String value
 * @return JSON value handle
 */
mcp_json_value_t mcp_json_string(mcp_string_t value);

/**
 * Create JSON array
 * @return JSON value handle
 */
mcp_json_value_t mcp_json_array(void);

/**
 * Add item to JSON array
 * @param array Array handle
 * @param value Value to add
 * @return MCP_OK on success
 */
mcp_result_t mcp_json_array_append(mcp_json_value_t array, mcp_json_value_t value);

/**
 * Create JSON object
 * @return JSON value handle
 */
mcp_json_value_t mcp_json_object(void);

/**
 * Set object property
 * @param object Object handle
 * @param key Property key
 * @param value Property value
 * @return MCP_OK on success
 */
mcp_result_t mcp_json_object_set(
    mcp_json_value_t object,
    mcp_string_t key,
    mcp_json_value_t value
);

/**
 * Parse JSON from string
 * @param json JSON string
 * @return JSON value handle or NULL on error
 */
mcp_json_value_t mcp_json_parse(mcp_string_t json);

/**
 * Serialize JSON to string
 * @param value JSON value
 * @param pretty Whether to pretty-print
 * @return Serialized string (must be freed)
 */
mcp_string_buffer_t* mcp_json_stringify(mcp_json_value_t value, bool pretty);

/**
 * Get JSON value type
 * @param value JSON value
 * @return Value type
 */
typedef enum {
    MCP_JSON_NULL,
    MCP_JSON_BOOL,
    MCP_JSON_NUMBER,
    MCP_JSON_STRING,
    MCP_JSON_ARRAY,
    MCP_JSON_OBJECT
} mcp_json_type_t;

mcp_json_type_t mcp_json_get_type(mcp_json_value_t value);

/**
 * Get boolean value
 * @param value JSON value
 * @param out Output value
 * @return MCP_OK if value is boolean
 */
mcp_result_t mcp_json_get_bool(mcp_json_value_t value, bool* out);

/**
 * Get number value
 * @param value JSON value
 * @param out Output value
 * @return MCP_OK if value is number
 */
mcp_result_t mcp_json_get_number(mcp_json_value_t value, double* out);

/**
 * Get string value
 * @param value JSON value
 * @return String value or empty string
 */
mcp_string_t mcp_json_get_string(mcp_json_value_t value);

/**
 * Get array size
 * @param value JSON array
 * @return Array size or 0
 */
size_t mcp_json_array_size(mcp_json_value_t value);

/**
 * Get array item
 * @param value JSON array
 * @param index Item index
 * @return Item value or NULL
 */
mcp_json_value_t mcp_json_array_get(mcp_json_value_t value, size_t index);

/**
 * Get object property
 * @param value JSON object
 * @param key Property key
 * @return Property value or NULL
 */
mcp_json_value_t mcp_json_object_get(mcp_json_value_t value, mcp_string_t key);

/**
 * Iterate object properties
 * @param value JSON object
 * @param callback Callback for each property
 * @param user_data User data for callback
 */
typedef void (*mcp_json_object_iterator_t)(
    mcp_string_t key,
    mcp_json_value_t value,
    void* user_data
);

void mcp_json_object_iterate(
    mcp_json_value_t value,
    mcp_json_object_iterator_t callback,
    void* user_data
);

/**
 * Clone JSON value
 * @param value JSON value
 * @return Cloned value
 */
mcp_json_value_t mcp_json_clone(mcp_json_value_t value);

/**
 * Release JSON value
 * @param value JSON value
 */
void mcp_json_release(mcp_json_value_t value);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Create string from C string
 * @param str C string (can be NULL)
 * @return String structure
 */
mcp_string_t mcp_string_from_cstr(const char* str);

/**
 * Create string with length
 * @param data String data
 * @param length String length
 * @return String structure
 */
mcp_string_t mcp_string_from_data(const char* data, size_t length);

/**
 * Duplicate string
 * @param str String to duplicate
 * @return Duplicated string buffer (must be freed)
 */
mcp_string_buffer_t* mcp_string_dup(mcp_string_t str);

/**
 * Free string buffer
 * @param buffer String buffer
 */
void mcp_string_buffer_free(mcp_string_buffer_t* buffer);

/**
 * Create buffer
 * @param capacity Initial capacity
 * @return Buffer or NULL
 */
mcp_buffer_t* mcp_buffer_create(size_t capacity);

/**
 * Append to buffer
 * @param buffer Buffer
 * @param data Data to append
 * @param length Data length
 * @return MCP_OK on success
 */
mcp_result_t mcp_buffer_append(mcp_buffer_t* buffer, const uint8_t* data, size_t length);

/**
 * Free buffer
 * @param buffer Buffer
 */
void mcp_buffer_free(mcp_buffer_t* buffer);

/**
 * Create list
 * @param capacity Initial capacity
 * @return List or NULL
 */
mcp_list_t* mcp_list_create(size_t capacity);

/**
 * Add item to list
 * @param list List
 * @param item Item to add
 * @return MCP_OK on success
 */
mcp_result_t mcp_list_append(mcp_list_t* list, void* item);

/**
 * Get list item
 * @param list List
 * @param index Item index
 * @return Item or NULL
 */
void* mcp_list_get(const mcp_list_t* list, size_t index);

/**
 * Free list (does not free items)
 * @param list List
 */
void mcp_list_free(mcp_list_t* list);

/**
 * Create map
 * @param capacity Initial capacity
 * @return Map or NULL
 */
mcp_map_t* mcp_map_create(size_t capacity);

/**
 * Set map entry
 * @param map Map
 * @param key Entry key
 * @param value Entry value
 * @return MCP_OK on success
 */
mcp_result_t mcp_map_set(mcp_map_t* map, mcp_string_t key, void* value);

/**
 * Get map entry
 * @param map Map
 * @param key Entry key
 * @return Entry value or NULL
 */
void* mcp_map_get(const mcp_map_t* map, mcp_string_t key);

/**
 * Free map (does not free values)
 * @param map Map
 */
void mcp_map_free(mcp_map_t* map);

#ifdef __cplusplus
}
#endif

#endif /* MCP_C_API_H */