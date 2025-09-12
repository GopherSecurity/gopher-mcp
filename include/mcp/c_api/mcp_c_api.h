/**
 * @file mcp_c_api.h
 * @brief FFI-friendly C API for MCP C++ SDK with RAII enforcement
 *
 * This header provides the complete C API for the MCP C++ SDK.
 * It follows an event-driven, thread-confined architecture while
 * ensuring FFI-safety and automatic resource management through RAII.
 *
 * Architecture:
 * - All operations happen in dispatcher thread context
 * - Callbacks are invoked in dispatcher thread
 * - RAII guards ensure automatic cleanup
 * - FFI-safe types for cross-language bindings
 * - Follows Create → Configure → Connect → Operate → Close lifecycle
 *
 * Memory Management:
 * - All handles are reference-counted internally
 * - Automatic cleanup through RAII guards
 * - Optional manual resource management for FFI
 * - Thread-safe resource tracking in debug mode
 */

#ifndef MCP_C_API_H
#define MCP_C_API_H

/* Include FFI-safe type definitions */
#include "mcp_c_collections.h"
#include "mcp_c_memory.h"
#include "mcp_c_types.h"
#include "mcp_c_types_api.h"
#include "mcp_c_api_json.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Library Version and Initialization
 * ============================================================================
 */

/**
 * Initialize the MCP library
 * Must be called before any other API functions
 * @param allocator Custom allocator (NULL for default)
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_init(const mcp_allocator_t* allocator) MCP_NOEXCEPT;

/**
 * Shutdown the MCP library
 * Cleans up all resources and checks for leaks
 */
MCP_API void mcp_shutdown(void) MCP_NOEXCEPT;

/**
 * Check if library is initialized
 * @return MCP_TRUE if initialized
 */
MCP_API mcp_bool_t mcp_is_initialized(void) MCP_NOEXCEPT;

/**
 * Get library version information
 * @return Version string (do not free)
 */
MCP_API const char* mcp_get_version(void) MCP_NOEXCEPT;

/* ============================================================================
 * RAII Guard Functions with Enhanced Safety
 * ============================================================================
 */

/**
 * RAII guard handle for automatic resource cleanup
 * Guards are automatically destroyed when going out of scope in C++
 * For C users, explicit destruction is required
 */
typedef struct mcp_guard_impl* mcp_guard_t;

/**
 * Guard cleanup callback for custom resources
 */
typedef void (*mcp_guard_cleanup_fn)(void* resource) MCP_NOEXCEPT;

/**
 * Create a RAII guard for a handle with automatic cleanup
 * @param handle Handle to guard (takes ownership)
 * @param type Type of handle for validation
 * @return Guard handle or NULL on error
 */
MCP_API mcp_guard_t mcp_guard_create(void* handle,
                                     mcp_type_id_t type) MCP_NOEXCEPT;

/**
 * Create a RAII guard with custom cleanup function
 * @param handle Handle to guard (takes ownership)
 * @param type Type of handle for validation
 * @param cleanup Custom cleanup function
 * @return Guard handle or NULL on error
 */
MCP_API mcp_guard_t mcp_guard_create_custom(void* handle,
                                            mcp_type_id_t type,
                                            mcp_guard_cleanup_fn cleanup)
    MCP_NOEXCEPT;

/**
 * Release resource from guard (prevents automatic cleanup)
 * @param guard Guard handle (will be nullified)
 * @return Original handle (caller takes ownership)
 */
MCP_API void* mcp_guard_release(mcp_guard_t* guard) MCP_NOEXCEPT;

/**
 * Destroy guard and cleanup resource
 * @param guard Guard handle (will be nullified)
 */
MCP_API void mcp_guard_destroy(mcp_guard_t* guard) MCP_NOEXCEPT;

/**
 * Check if guard is valid and holds a resource
 * @param guard Guard handle
 * @return MCP_TRUE if valid
 */
MCP_API mcp_bool_t mcp_guard_is_valid(mcp_guard_t guard) MCP_NOEXCEPT;

/**
 * Get the guarded resource without releasing ownership
 * @param guard Guard handle
 * @return Guarded resource or NULL
 */
MCP_API void* mcp_guard_get(mcp_guard_t guard) MCP_NOEXCEPT;

/* ============================================================================
 * Transaction Management for Multiple Resources
 * ============================================================================
 */

/**
 * Transaction handle for atomic multi-resource operations
 * Ensures all-or-nothing semantics for resource operations
 */
typedef struct mcp_transaction_impl* mcp_transaction_t;

/**
 * Transaction options for fine-grained control
 */
typedef struct mcp_transaction_opts {
  mcp_bool_t auto_rollback;   /* Auto-rollback on error */
  mcp_bool_t strict_ordering; /* Enforce strict cleanup order */
  uint32_t max_resources;     /* Maximum resources (0 = unlimited) */
} mcp_transaction_opts_t;

/**
 * Create a new transaction with default options
 * @return Transaction handle or NULL on error
 */
MCP_API mcp_transaction_t mcp_transaction_create(void) MCP_NOEXCEPT;

/**
 * Create a new transaction with custom options
 * @param opts Transaction options
 * @return Transaction handle or NULL on error
 */
MCP_API mcp_transaction_t
mcp_transaction_create_ex(const mcp_transaction_opts_t* opts) MCP_NOEXCEPT;

/**
 * Add resource to transaction with automatic cleanup
 * @param txn Transaction handle
 * @param handle Resource handle (ownership transferred)
 * @param type Resource type for validation
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_transaction_add(mcp_transaction_t txn,
                                         void* handle,
                                         mcp_type_id_t type) MCP_NOEXCEPT;

/**
 * Add resource with custom cleanup function
 * @param txn Transaction handle
 * @param handle Resource handle (ownership transferred)
 * @param type Resource type for validation
 * @param cleanup Custom cleanup function
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_transaction_add_custom(mcp_transaction_t txn,
                                                void* handle,
                                                mcp_type_id_t type,
                                                mcp_guard_cleanup_fn cleanup)
    MCP_NOEXCEPT;

/**
 * Get number of resources in transaction
 * @param txn Transaction handle
 * @return Number of resources
 */
MCP_API size_t mcp_transaction_size(mcp_transaction_t txn) MCP_NOEXCEPT;

/**
 * Commit transaction (prevent cleanup)
 * @param txn Transaction handle (will be nullified)
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_transaction_commit(mcp_transaction_t* txn)
    MCP_NOEXCEPT;

/**
 * Rollback transaction (cleanup all resources)
 * @param txn Transaction handle (will be nullified)
 */
MCP_API void mcp_transaction_rollback(mcp_transaction_t* txn) MCP_NOEXCEPT;

/**
 * Destroy transaction (auto-rollback if not committed)
 * @param txn Transaction handle (will be nullified)
 */
MCP_API void mcp_transaction_destroy(mcp_transaction_t* txn) MCP_NOEXCEPT;

/**
 * Check if transaction is valid
 * @param txn Transaction handle
 * @return MCP_TRUE if valid
 */
MCP_API mcp_bool_t mcp_transaction_is_valid(mcp_transaction_t txn) MCP_NOEXCEPT;

/* ============================================================================
 * Event Loop & Dispatcher
 * ============================================================================
 */

/**
 * Create a new dispatcher (event loop)
 * @return Dispatcher handle or NULL on error
 */
MCP_API mcp_dispatcher_t mcp_dispatcher_create(void) MCP_NOEXCEPT;

/**
 * Create dispatcher with RAII guard
 * @param guard Output: RAII guard for automatic cleanup
 * @return Dispatcher handle or NULL on error
 */
MCP_API mcp_dispatcher_t mcp_dispatcher_create_guarded(mcp_guard_t* guard)
    MCP_NOEXCEPT;

/**
 * Run the dispatcher (blocks until stopped)
 * @param dispatcher Dispatcher handle
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_dispatcher_run(mcp_dispatcher_t dispatcher)
    MCP_NOEXCEPT;

/**
 * Run dispatcher for specified duration
 * @param dispatcher Dispatcher handle
 * @param timeout_ms Maximum time to run in milliseconds
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_dispatcher_run_timeout(
    mcp_dispatcher_t dispatcher, uint32_t timeout_ms) MCP_NOEXCEPT;

/**
 * Stop the dispatcher
 * @param dispatcher Dispatcher handle
 */
MCP_API void mcp_dispatcher_stop(mcp_dispatcher_t dispatcher) MCP_NOEXCEPT;

/**
 * Post a callback to dispatcher thread
 * @param dispatcher Dispatcher handle
 * @param callback Callback function
 * @param user_data User data for callback
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_dispatcher_post(mcp_dispatcher_t dispatcher,
                                         mcp_callback_t callback,
                                         void* user_data) MCP_NOEXCEPT;

/**
 * Check if current thread is dispatcher thread
 * @param dispatcher Dispatcher handle
 * @return MCP_TRUE if in dispatcher thread
 */
MCP_API mcp_bool_t mcp_dispatcher_is_thread(mcp_dispatcher_t dispatcher)
    MCP_NOEXCEPT;

/**
 * Create a timer
 * @param dispatcher Dispatcher handle
 * @param callback Timer callback
 * @param user_data User data for callback
 * @return Timer ID or 0 on error
 */
MCP_API uint64_t mcp_dispatcher_create_timer(mcp_dispatcher_t dispatcher,
                                             mcp_timer_callback_t callback,
                                             void* user_data) MCP_NOEXCEPT;

/**
 * Enable/arm a timer
 * @param dispatcher Dispatcher handle
 * @param timer_id Timer ID
 * @param timeout_ms Timeout in milliseconds
 * @param repeat Whether to repeat
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_dispatcher_enable_timer(mcp_dispatcher_t dispatcher,
                                                 uint64_t timer_id,
                                                 uint32_t timeout_ms,
                                                 mcp_bool_t repeat)
    MCP_NOEXCEPT;

/**
 * Disable a timer
 * @param dispatcher Dispatcher handle
 * @param timer_id Timer ID
 */
MCP_API void mcp_dispatcher_disable_timer(mcp_dispatcher_t dispatcher,
                                          uint64_t timer_id) MCP_NOEXCEPT;

/**
 * Destroy a timer
 * @param dispatcher Dispatcher handle
 * @param timer_id Timer ID
 */
MCP_API void mcp_dispatcher_destroy_timer(mcp_dispatcher_t dispatcher,
                                          uint64_t timer_id) MCP_NOEXCEPT;

/**
 * Destroy dispatcher
 * @param dispatcher Dispatcher handle
 */
MCP_API void mcp_dispatcher_destroy(mcp_dispatcher_t dispatcher) MCP_NOEXCEPT;

/* ============================================================================
 * Connection Management with RAII
 * ============================================================================
 */

/**
 * Extended transport configuration for MCP connections
 */
typedef struct mcp_transport_config {
  mcp_transport_type_t type;

  /* Transport-specific configuration */
  union {
    struct {
      mcp_bool_t use_tls;
      const char* alpn_protocols; /* Comma-separated ALPN protocols */
      const char* sni_hostname;   /* SNI hostname for TLS */
    } tcp;

    struct {
      int stdin_fd;  /* -1 for default stdin */
      int stdout_fd; /* -1 for default stdout */
      int stderr_fd; /* -1 for default stderr */
    } stdio;

    struct {
      const char* http_headers; /* Additional HTTP headers for MCP */
      uint32_t retry_delay_ms;
      uint32_t max_retries;
      mcp_bool_t use_compression;
    } http_sse;
  } config;

  /* Common options */
  uint32_t connect_timeout_ms;
  uint32_t idle_timeout_ms;
  mcp_bool_t enable_keepalive;
  uint32_t keepalive_interval_ms;
} mcp_transport_config_t;

/**
 * Create a client connection with transport configuration
 * @param dispatcher Event dispatcher handle
 * @param transport_config Transport configuration (if NULL, uses defaults)
 * @return Connection handle or NULL on error
 */
MCP_API mcp_connection_t mcp_connection_create_client_ex(
    mcp_dispatcher_t dispatcher,
    const mcp_transport_config_t* transport_config) MCP_NOEXCEPT;

/**
 * Create client connection with RAII guard
 * @param dispatcher Event dispatcher handle
 * @param transport_config Transport configuration
 * @param guard Output: RAII guard for automatic cleanup
 * @return Connection handle or NULL on error
 */
MCP_API mcp_connection_t mcp_connection_create_client_guarded(
    mcp_dispatcher_t dispatcher,
    const mcp_transport_config_t* transport_config,
    mcp_guard_t* guard) MCP_NOEXCEPT;

/**
 * Create a client connection (simplified)
 * @param dispatcher Dispatcher handle
 * @param transport Transport type
 * @return Connection handle or NULL on error
 */
MCP_API mcp_connection_t mcp_connection_create_client(
    mcp_dispatcher_t dispatcher, mcp_transport_type_t transport) MCP_NOEXCEPT;

/**
 * Configure connection
 * @param connection Connection handle
 * @param address Target address (NULL for stdio)
 * @param options Socket options (NULL for defaults)
 * @param ssl_config SSL configuration (NULL for non-SSL)
 * @return MCP_OK on success
 */
MCP_API mcp_result_t
mcp_connection_configure(mcp_connection_t connection,
                         const mcp_address_t* address,
                         const mcp_socket_options_t* options,
                         const mcp_ssl_config_t* ssl_config) MCP_NOEXCEPT;

/**
 * Set connection callbacks
 * @param connection Connection handle
 * @param state_cb State change callback
 * @param data_cb Data received callback
 * @param error_cb Error callback
 * @param user_data User data for callbacks
 * @return MCP_OK on success
 */
MCP_API mcp_result_t
mcp_connection_set_callbacks(mcp_connection_t connection,
                             mcp_connection_state_callback_t state_cb,
                             mcp_data_callback_t data_cb,
                             mcp_error_callback_t error_cb,
                             void* user_data) MCP_NOEXCEPT;

/**
 * Set watermarks for flow control
 * @param connection Connection handle
 * @param config Watermark configuration
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_connection_set_watermarks(
    mcp_connection_t connection,
    const mcp_watermark_config_t* config) MCP_NOEXCEPT;

/**
 * Connect (async)
 * @param connection Connection handle
 * @return MCP_OK if connection started
 */
MCP_API mcp_result_t mcp_connection_connect(mcp_connection_t connection)
    MCP_NOEXCEPT;

/**
 * Write data (async)
 * @param connection Connection handle
 * @param data Data to write
 * @param length Data length
 * @param callback Write complete callback (optional)
 * @param user_data User data for callback
 * @return MCP_OK if write queued
 */
MCP_API mcp_result_t mcp_connection_write(mcp_connection_t connection,
                                          const uint8_t* data,
                                          size_t length,
                                          mcp_write_callback_t callback,
                                          void* user_data) MCP_NOEXCEPT;

/**
 * Close connection
 * @param connection Connection handle
 * @param flush Whether to flush pending writes
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_connection_close(mcp_connection_t connection,
                                          mcp_bool_t flush) MCP_NOEXCEPT;

/**
 * Get connection state
 * @param connection Connection handle
 * @return Current connection state
 */
MCP_API mcp_connection_state_t
mcp_connection_get_state(mcp_connection_t connection) MCP_NOEXCEPT;

/**
 * Get connection statistics
 * @param connection Connection handle
 * @param bytes_read Output: total bytes read
 * @param bytes_written Output: total bytes written
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_connection_get_stats(mcp_connection_t connection,
                                              uint64_t* bytes_read,
                                              uint64_t* bytes_written)
    MCP_NOEXCEPT;

/**
 * Destroy connection
 * @param connection Connection handle
 */
MCP_API void mcp_connection_destroy(mcp_connection_t connection) MCP_NOEXCEPT;

/* ============================================================================
 * Server & Listener with RAII
 * ============================================================================
 */

/**
 * Create a listener
 * @param dispatcher Dispatcher handle
 * @param transport Transport type
 * @return Listener handle or NULL on error
 */
MCP_API mcp_listener_t mcp_listener_create(
    mcp_dispatcher_t dispatcher, mcp_transport_type_t transport) MCP_NOEXCEPT;

/**
 * Create listener with RAII guard
 * @param dispatcher Dispatcher handle
 * @param transport Transport type
 * @param guard Output: RAII guard for automatic cleanup
 * @return Listener handle or NULL on error
 */
MCP_API mcp_listener_t
mcp_listener_create_guarded(mcp_dispatcher_t dispatcher,
                            mcp_transport_type_t transport,
                            mcp_guard_t* guard) MCP_NOEXCEPT;

/**
 * Configure listener
 * @param listener Listener handle
 * @param address Bind address (NULL for stdio)
 * @param options Socket options (NULL for defaults)
 * @param ssl_config SSL configuration (NULL for non-SSL)
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_listener_configure(mcp_listener_t listener,
                                            const mcp_address_t* address,
                                            const mcp_socket_options_t* options,
                                            const mcp_ssl_config_t* ssl_config)
    MCP_NOEXCEPT;

/**
 * Set accept callback
 * @param listener Listener handle
 * @param callback Accept callback
 * @param user_data User data for callback
 * @return MCP_OK on success
 */
MCP_API mcp_result_t
mcp_listener_set_accept_callback(mcp_listener_t listener,
                                 mcp_accept_callback_t callback,
                                 void* user_data) MCP_NOEXCEPT;

/**
 * Start listening
 * @param listener Listener handle
 * @param backlog Connection backlog
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_listener_start(mcp_listener_t listener,
                                        int backlog) MCP_NOEXCEPT;

/**
 * Stop listening
 * @param listener Listener handle
 */
MCP_API void mcp_listener_stop(mcp_listener_t listener) MCP_NOEXCEPT;

/**
 * Destroy listener
 * @param listener Listener handle
 */
MCP_API void mcp_listener_destroy(mcp_listener_t listener) MCP_NOEXCEPT;

/* ============================================================================
 * MCP Client with RAII
 * ============================================================================
 */

/**
 * Create MCP client
 * @param dispatcher Dispatcher handle
 * @param config Client configuration
 * @return Client handle or NULL on error
 */
MCP_API mcp_client_t mcp_client_create(mcp_dispatcher_t dispatcher,
                                       const mcp_client_config_t* config)
    MCP_NOEXCEPT;

/**
 * Create MCP client with RAII guard
 * @param dispatcher Dispatcher handle
 * @param config Client configuration
 * @param guard Output: RAII guard for automatic cleanup
 * @return Client handle or NULL on error
 */
MCP_API mcp_client_t
mcp_client_create_guarded(mcp_dispatcher_t dispatcher,
                          const mcp_client_config_t* config,
                          mcp_guard_t* guard) MCP_NOEXCEPT;

/**
 * Set MCP message callbacks
 * @param client Client handle
 * @param request_cb Request callback
 * @param response_cb Response callback
 * @param notification_cb Notification callback
 * @param user_data User data for callbacks
 * @return MCP_OK on success
 */
MCP_API mcp_result_t
mcp_client_set_callbacks(mcp_client_t client,
                         mcp_request_callback_t request_cb,
                         mcp_response_callback_t response_cb,
                         mcp_notification_callback_t notification_cb,
                         void* user_data) MCP_NOEXCEPT;

/**
 * Connect client
 * @param client Client handle
 * @return MCP_OK if connection started
 */
MCP_API mcp_result_t mcp_client_connect(mcp_client_t client) MCP_NOEXCEPT;

/**
 * Initialize protocol handshake
 * @param client Client handle
 * @return Request ID for tracking
 */
MCP_API mcp_request_id_t mcp_client_initialize(mcp_client_t client)
    MCP_NOEXCEPT;

/**
 * Send request
 * @param client Client handle
 * @param method Method name
 * @param params Parameters (JSON value)
 * @return Request ID for tracking
 */
MCP_API mcp_request_id_t mcp_client_send_request(mcp_client_t client,
                                                 mcp_string_t method,
                                                 mcp_json_value_t params)
    MCP_NOEXCEPT;

/**
 * Send notification
 * @param client Client handle
 * @param method Method name
 * @param params Parameters (JSON value)
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_client_send_notification(mcp_client_t client,
                                                  mcp_string_t method,
                                                  mcp_json_value_t params)
    MCP_NOEXCEPT;

/**
 * List available tools
 * @param client Client handle
 * @return Request ID for tracking
 */
MCP_API mcp_request_id_t mcp_client_list_tools(mcp_client_t client)
    MCP_NOEXCEPT;

/**
 * Call a tool
 * @param client Client handle
 * @param name Tool name
 * @param arguments Tool arguments (JSON value)
 * @return Request ID for tracking
 */
MCP_API mcp_request_id_t mcp_client_call_tool(mcp_client_t client,
                                              mcp_string_t name,
                                              mcp_json_value_t arguments)
    MCP_NOEXCEPT;

/**
 * List resources
 * @param client Client handle
 * @return Request ID for tracking
 */
MCP_API mcp_request_id_t mcp_client_list_resources(mcp_client_t client)
    MCP_NOEXCEPT;

/**
 * Read resource
 * @param client Client handle
 * @param uri Resource URI
 * @return Request ID for tracking
 */
MCP_API mcp_request_id_t
mcp_client_read_resource(mcp_client_t client, mcp_string_t uri) MCP_NOEXCEPT;

/**
 * List prompts
 * @param client Client handle
 * @return Request ID for tracking
 */
MCP_API mcp_request_id_t mcp_client_list_prompts(mcp_client_t client)
    MCP_NOEXCEPT;

/**
 * Get prompt
 * @param client Client handle
 * @param name Prompt name
 * @param arguments Prompt arguments (map of string to string)
 * @return Request ID for tracking
 */
MCP_API mcp_request_id_t mcp_client_get_prompt(
    mcp_client_t client, mcp_string_t name, mcp_map_t arguments) MCP_NOEXCEPT;

/**
 * Disconnect client
 * @param client Client handle
 */
MCP_API void mcp_client_disconnect(mcp_client_t client) MCP_NOEXCEPT;

/**
 * Destroy client
 * @param client Client handle
 */
MCP_API void mcp_client_destroy(mcp_client_t client) MCP_NOEXCEPT;

/* ============================================================================
 * MCP Server with RAII
 * ============================================================================
 */

/**
 * Create MCP server
 * @param dispatcher Dispatcher handle
 * @param config Server configuration
 * @return Server handle or NULL on error
 */
MCP_API mcp_server_t mcp_server_create(mcp_dispatcher_t dispatcher,
                                       const mcp_server_config_t* config)
    MCP_NOEXCEPT;

/**
 * Create MCP server with RAII guard
 * @param dispatcher Dispatcher handle
 * @param config Server configuration
 * @param guard Output: RAII guard for automatic cleanup
 * @return Server handle or NULL on error
 */
MCP_API mcp_server_t
mcp_server_create_guarded(mcp_dispatcher_t dispatcher,
                          const mcp_server_config_t* config,
                          mcp_guard_t* guard) MCP_NOEXCEPT;

/**
 * Set MCP message callbacks
 * @param server Server handle
 * @param request_cb Request callback
 * @param notification_cb Notification callback
 * @param user_data User data for callbacks
 * @return MCP_OK on success
 */
MCP_API mcp_result_t
mcp_server_set_callbacks(mcp_server_t server,
                         mcp_request_callback_t request_cb,
                         mcp_notification_callback_t notification_cb,
                         void* user_data) MCP_NOEXCEPT;

/**
 * Register tool
 * @param server Server handle
 * @param tool Tool definition
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_server_register_tool(
    mcp_server_t server, const mcp_tool_t* tool) MCP_NOEXCEPT;

/**
 * Register resource template
 * @param server Server handle
 * @param resource Resource template
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_server_register_resource(
    mcp_server_t server, const mcp_resource_template_t* resource) MCP_NOEXCEPT;

/**
 * Register prompt
 * @param server Server handle
 * @param prompt Prompt definition
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_server_register_prompt(
    mcp_server_t server, const mcp_prompt_t* prompt) MCP_NOEXCEPT;

/**
 * Start server
 * @param server Server handle
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_server_start(mcp_server_t server) MCP_NOEXCEPT;

/**
 * Send response
 * @param server Server handle
 * @param request_id Request ID to respond to
 * @param result Result (JSON value)
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_server_send_response(mcp_server_t server,
                                              mcp_request_id_t request_id,
                                              mcp_json_value_t result)
    MCP_NOEXCEPT;

/**
 * Send error response
 * @param server Server handle
 * @param request_id Request ID to respond to
 * @param error Error details
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_server_send_error(mcp_server_t server,
                                           mcp_request_id_t request_id,
                                           const mcp_jsonrpc_error_t* error)
    MCP_NOEXCEPT;

/**
 * Send notification
 * @param server Server handle
 * @param method Method name
 * @param params Parameters (JSON value)
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_server_send_notification(mcp_server_t server,
                                                  mcp_string_t method,
                                                  mcp_json_value_t params)
    MCP_NOEXCEPT;

/**
 * Stop server
 * @param server Server handle
 */
MCP_API void mcp_server_stop(mcp_server_t server) MCP_NOEXCEPT;

/**
 * Destroy server
 * @param server Server handle
 */
MCP_API void mcp_server_destroy(mcp_server_t server) MCP_NOEXCEPT;

/* ============================================================================
 * JSON Value Management with RAII
 * ============================================================================
 */

/* JSON Compatibility Wrappers - canonical functions are in mcp_c_api_json.h */

/**
 * Parse JSON from mcp_string_t (compatibility wrapper)
 * @param json JSON string as mcp_string_t
 * @return JSON value handle or NULL on error
 */
MCP_API mcp_json_value_t mcp_json_parse_mcp_string(mcp_string_t json) MCP_NOEXCEPT;

/**
 * Serialize JSON to string buffer (compatibility wrapper)
 * @param value JSON value
 * @param pretty Whether to pretty-print (TODO: not yet implemented)
 * @return Serialized string buffer (must be freed with mcp_string_buffer_free)
 */
MCP_API mcp_string_buffer_t* mcp_json_stringify_buffer(mcp_json_value_t value,
                                                       mcp_bool_t pretty) MCP_NOEXCEPT;

/**
 * Clone JSON value
 * @param value JSON value
 * @return Cloned value
 */
MCP_API mcp_json_value_t mcp_json_clone(mcp_json_value_t value) MCP_NOEXCEPT;

/**
 * Release JSON value (deprecated, use mcp_json_free from mcp_c_collections.h)
 * @param value JSON value
 * @deprecated Use mcp_json_free instead
 */
static inline void mcp_json_release(mcp_json_value_t value) MCP_NOEXCEPT {
  mcp_json_free(value);
}

/* ============================================================================
 * Utility Functions
 * ============================================================================
 */

/**
 * Create string from C string
 * @param str C string (can be NULL)
 * @return String structure
 */
MCP_API mcp_string_t mcp_string_from_cstr(const char* str) MCP_NOEXCEPT;

/**
 * Create string with length
 * @param data String data
 * @param length String length
 * @return String structure
 */
MCP_API mcp_string_t mcp_string_from_data(const char* data,
                                          size_t length) MCP_NOEXCEPT;

/**
 * Duplicate string
 * @param str String to duplicate
 * @return Duplicated string buffer (must be freed)
 */
MCP_API mcp_string_buffer_t* mcp_string_dup(mcp_string_t str) MCP_NOEXCEPT;

/**
 * Free string buffer
 * @param buffer String buffer
 */
MCP_API void mcp_string_buffer_free(mcp_string_buffer_t* buffer) MCP_NOEXCEPT;

/**
 * Create buffer
 * @param capacity Initial capacity
 * @return Buffer or NULL
 */
MCP_API mcp_buffer_t* mcp_buffer_create(size_t capacity) MCP_NOEXCEPT;

/**
 * Append to buffer
 * @param buffer Buffer
 * @param data Data to append
 * @param length Data length
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_append(mcp_buffer_t* buffer,
                                       const uint8_t* data,
                                       size_t length) MCP_NOEXCEPT;

/**
 * Get buffer data
 * @param buffer Buffer
 * @param out_data Output: buffer data
 * @param out_length Output: data length
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_get_data(mcp_buffer_t* buffer,
                                         const uint8_t** out_data,
                                         size_t* out_length) MCP_NOEXCEPT;

/**
 * Free buffer
 * @param buffer Buffer
 */
MCP_API void mcp_buffer_free(mcp_buffer_t* buffer) MCP_NOEXCEPT;

/* ============================================================================
 * Resource Statistics and Debugging
 * ============================================================================
 */

/**
 * Get resource statistics
 * @param active_count Output: number of active resources
 * @param total_created Output: total resources created
 * @param total_destroyed Output: total resources destroyed
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_get_resource_stats(size_t* active_count,
                                            size_t* total_created,
                                            size_t* total_destroyed)
    MCP_NOEXCEPT;

/**
 * Check for resource leaks
 * @return Number of leaked resources
 */
MCP_API size_t mcp_check_resource_leaks(void) MCP_NOEXCEPT;

/**
 * Print resource leak report
 */
MCP_API void mcp_print_leak_report(void) MCP_NOEXCEPT;

/* ============================================================================
 * RAII Helper Macros for C++ Users
 * ============================================================================
 */

#ifdef __cplusplus

/* Automatic cleanup guard for any MCP handle */
#define MCP_AUTO_GUARD(handle, type)                                   \
  std::unique_ptr<void, std::function<void(void*)>> _guard_##__LINE__( \
      handle, [](void* h) {                                            \
        if (h) {                                                       \
          auto guard = mcp_guard_create(h, type);                      \
          mcp_guard_destroy(&guard);                                   \
        }                                                              \
      })

/* Scoped transaction with automatic rollback */
#define MCP_SCOPED_TRANSACTION(name)                          \
  struct _TxnGuard_##__LINE__ {                               \
    mcp_transaction_t txn;                                    \
    bool committed = false;                                   \
    _TxnGuard_##__LINE__() : txn(mcp_transaction_create()) {} \
    ~_TxnGuard_##__LINE__() {                                 \
      if (txn && !committed)                                  \
        mcp_transaction_rollback(&txn);                       \
      else if (txn)                                           \
        mcp_transaction_destroy(&txn);                        \
    }                                                         \
    void commit() {                                           \
      if (txn) {                                              \
        mcp_transaction_commit(&txn);                         \
        committed = true;                                     \
      }                                                       \
    }                                                         \
  } name

/* Automatic resource cleanup on scope exit */
#define MCP_SCOPE_EXIT(code) \
  auto _scope_exit_##__LINE__ = mcp::raii::ScopeExit([&]() { code; })

/* Ensure resource is freed even on exception */
#define MCP_ENSURE_CLEANUP(resource, cleanup_fn)             \
  std::unique_ptr<std::remove_pointer_t<decltype(resource)>, \
                  decltype(cleanup_fn)>                      \
      _cleanup_##__LINE__(resource, cleanup_fn)

#endif /* __cplusplus */

/* ============================================================================
 * RAII Patterns for C Users
 * ============================================================================
 */

/* Guard creation with cleanup registration */
#define MCP_GUARD_CREATE(handle, type) mcp_guard_create(handle, type)

/* Transaction with resource tracking */
#define MCP_TXN_ADD(txn, handle, type) mcp_transaction_add(txn, handle, type)

/* Safe resource release */
#define MCP_SAFE_RELEASE(guard_ptr)  \
  do {                               \
    if (guard_ptr && *(guard_ptr)) { \
      mcp_guard_destroy(guard_ptr);  \
    }                                \
  } while (0)

/* Safe transaction cleanup */
#define MCP_SAFE_TXN_CLEANUP(txn_ptr)    \
  do {                                   \
    if (txn_ptr && *(txn_ptr)) {         \
      mcp_transaction_rollback(txn_ptr); \
    }                                    \
  } while (0)

#ifdef __cplusplus
}
#endif

#endif /* MCP_C_API_H */