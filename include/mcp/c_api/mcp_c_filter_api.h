/**
 * @file mcp_c_filter_api.h
 * @brief C API for MCP C++ Filter Architecture
 *
 * This header provides FFI-safe C bindings for the MCP filter architecture,
 * enabling any language to integrate with C++ filters through a clean
 * interface.
 *
 * Architecture:
 * - Handle-based RAII system with automatic cleanup
 * - Zero-copy buffer operations where possible
 * - Thread-safe dispatcher-based execution
 * - Protocol-agnostic support for OSI layers 3-7
 * - Reusable filter chains across languages
 *
 * Thread Safety:
 * - All operations execute in dispatcher thread context
 * - Callbacks are invoked in dispatcher thread
 * - No manual synchronization needed
 */

#ifndef MCP_FILTER_API_H
#define MCP_FILTER_API_H

#include "mcp_c_collections.h"
#include "mcp_c_memory.h"
#include "mcp_c_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Core Types and Enumerations
 * ============================================================================
 */

// Opaque handles for FFI safety
// Note: mcp_filter_t is already defined in mcp_c_types.h
typedef uint64_t mcp_filter_chain_t;
typedef uint64_t mcp_filter_manager_t;
typedef uint64_t mcp_buffer_handle_t;
typedef uint64_t mcp_filter_factory_t;
typedef struct mcp_filter_chain_builder* mcp_filter_chain_builder_t;

// Filter status for processing control
typedef enum {
  MCP_FILTER_CONTINUE = 0,       // Continue filter chain processing
  MCP_FILTER_STOP_ITERATION = 1  // Stop filter chain processing
} mcp_filter_status_t;

// Filter position in chain
typedef enum {
  MCP_FILTER_POSITION_FIRST = 0,
  MCP_FILTER_POSITION_LAST = 1,
  MCP_FILTER_POSITION_BEFORE = 2,  // Requires reference filter
  MCP_FILTER_POSITION_AFTER = 3    // Requires reference filter
} mcp_filter_position_t;

// Protocol layers (OSI model)
typedef enum {
  MCP_PROTOCOL_LAYER_3_NETWORK = 3,       // IP level
  MCP_PROTOCOL_LAYER_4_TRANSPORT = 4,     // TCP/UDP
  MCP_PROTOCOL_LAYER_5_SESSION = 5,       // Session management
  MCP_PROTOCOL_LAYER_6_PRESENTATION = 6,  // Encoding/Encryption
  MCP_PROTOCOL_LAYER_7_APPLICATION = 7    // HTTP/gRPC/WebSocket
} mcp_protocol_layer_t;

// Transport protocols for L4
typedef enum {
  MCP_TRANSPORT_PROTOCOL_TCP = 0,
  MCP_TRANSPORT_PROTOCOL_UDP = 1,
  MCP_TRANSPORT_PROTOCOL_QUIC = 2,
  MCP_TRANSPORT_PROTOCOL_SCTP = 3
} mcp_transport_protocol_t;

// Application protocols for L7
typedef enum {
  MCP_APP_PROTOCOL_HTTP = 0,
  MCP_APP_PROTOCOL_HTTPS = 1,
  MCP_APP_PROTOCOL_HTTP2 = 2,
  MCP_APP_PROTOCOL_HTTP3 = 3,
  MCP_APP_PROTOCOL_GRPC = 4,
  MCP_APP_PROTOCOL_WEBSOCKET = 5,
  MCP_APP_PROTOCOL_JSONRPC = 6,
  MCP_APP_PROTOCOL_CUSTOM = 99
} mcp_app_protocol_t;

// Built-in filter types
typedef enum {
  // Network filters
  MCP_FILTER_TCP_PROXY = 0,
  MCP_FILTER_UDP_PROXY = 1,

  // HTTP filters
  MCP_FILTER_HTTP_CODEC = 10,
  MCP_FILTER_HTTP_ROUTER = 11,
  MCP_FILTER_HTTP_COMPRESSION = 12,

  // Security filters
  MCP_FILTER_TLS_TERMINATION = 20,
  MCP_FILTER_AUTHENTICATION = 21,
  MCP_FILTER_AUTHORIZATION = 22,

  // Observability
  MCP_FILTER_ACCESS_LOG = 30,
  MCP_FILTER_METRICS = 31,
  MCP_FILTER_TRACING = 32,

  // Traffic management
  MCP_FILTER_RATE_LIMIT = 40,
  MCP_FILTER_CIRCUIT_BREAKER = 41,
  MCP_FILTER_RETRY = 42,
  MCP_FILTER_LOAD_BALANCER = 43,

  // Custom filter
  MCP_FILTER_CUSTOM = 100
} mcp_builtin_filter_type_t;

// Filter error codes
typedef enum {
  MCP_FILTER_ERROR_NONE = 0,
  MCP_FILTER_ERROR_INVALID_CONFIG = -1000,
  MCP_FILTER_ERROR_INITIALIZATION_FAILED = -1001,
  MCP_FILTER_ERROR_BUFFER_OVERFLOW = -1002,
  MCP_FILTER_ERROR_PROTOCOL_VIOLATION = -1003,
  MCP_FILTER_ERROR_UPSTREAM_TIMEOUT = -1004,
  MCP_FILTER_ERROR_CIRCUIT_OPEN = -1005,
  MCP_FILTER_ERROR_RESOURCE_EXHAUSTED = -1006,
  MCP_FILTER_ERROR_INVALID_STATE = -1007
} mcp_filter_error_t;

// Buffer flags
#define MCP_BUFFER_FLAG_READONLY 0x01
#define MCP_BUFFER_FLAG_OWNED 0x02
#define MCP_BUFFER_FLAG_EXTERNAL 0x04
#define MCP_BUFFER_FLAG_ZERO_COPY 0x08

/* ============================================================================
 * Data Structures
 * ============================================================================
 */

// Filter configuration
typedef struct mcp_filter_config {
  const char* name;                // Filter name
  mcp_builtin_filter_type_t type;  // Filter type
  mcp_json_value_t settings;       // JSON configuration
  mcp_protocol_layer_t layer;      // OSI layer
  mcp_memory_pool_t memory_pool;   // Optional memory pool
} mcp_filter_config_t;

// Buffer slice for zero-copy access
typedef struct mcp_buffer_slice {
  const uint8_t* data;  // Direct pointer to buffer memory
  size_t length;        // Length of this slice
  uint32_t flags;       // Buffer flags
} mcp_buffer_slice_t;

// Protocol metadata for different layers
typedef struct mcp_protocol_metadata {
  mcp_protocol_layer_t layer;

  union {
    // L3 - Network layer
    struct {
      uint32_t src_ip;
      uint32_t dst_ip;
      uint8_t protocol;
      uint8_t ttl;
    } l3;

    // L4 - Transport layer
    struct {
      uint16_t src_port;
      uint16_t dst_port;
      mcp_transport_protocol_t protocol;
      uint32_t sequence_num;
    } l4;

    // L5 - Session layer
    struct {
      mcp_bool_t is_tls;
      const char* alpn;
      const char* sni;
      uint32_t session_id;
    } l5;

    // L7 - Application layer
    struct {
      mcp_app_protocol_t protocol;
      mcp_map_t headers;
      const char* method;
      const char* path;
      uint32_t status_code;
    } l7;
  } data;
} mcp_protocol_metadata_t;

/* ============================================================================
 * Callback Types
 * ============================================================================
 */

// Filter data callback (onData from ReadFilter)
typedef mcp_filter_status_t (*mcp_filter_data_cb)(mcp_buffer_handle_t buffer,
                                                  mcp_bool_t end_stream,
                                                  void* user_data);

// Filter write callback (onWrite from WriteFilter)
typedef mcp_filter_status_t (*mcp_filter_write_cb)(mcp_buffer_handle_t buffer,
                                                   mcp_bool_t end_stream,
                                                   void* user_data);

// Connection event callback
typedef mcp_filter_status_t (*mcp_filter_event_cb)(mcp_connection_state_t state,
                                                   void* user_data);

// Watermark callbacks
typedef void (*mcp_filter_watermark_cb)(mcp_filter_t filter, void* user_data);

// Error callback
typedef void (*mcp_filter_error_cb)(mcp_filter_t filter,
                                    mcp_filter_error_t error,
                                    const char* message,
                                    void* user_data);

// Completion callback for async operations
typedef void (*mcp_filter_completion_cb)(mcp_result_t result, void* user_data);

// Post completion callback
typedef void (*mcp_post_completion_cb)(mcp_result_t result, void* user_data);

// Request callback for server
typedef void (*mcp_filter_request_cb)(mcp_buffer_handle_t response_buffer,
                                      mcp_result_t result,
                                      void* user_data);

// Filter callbacks structure
typedef struct mcp_filter_callbacks {
  // Data callbacks (executed in dispatcher thread)
  mcp_filter_data_cb on_data;
  mcp_filter_write_cb on_write;
  mcp_filter_event_cb on_new_connection;

  // Watermark callbacks
  mcp_filter_watermark_cb on_high_watermark;
  mcp_filter_watermark_cb on_low_watermark;

  // Error handling
  mcp_filter_error_cb on_error;

  void* user_data;
} mcp_filter_callbacks_t;

/* ============================================================================
 * Filter Lifecycle Management
 * ============================================================================
 */

/**
 * Create a new filter
 * @param dispatcher Event dispatcher handle
 * @param config Filter configuration
 * @return Filter handle or 0 on error
 */
MCP_API mcp_filter_t mcp_filter_create(mcp_dispatcher_t dispatcher,
                                       const mcp_filter_config_t* config)
    MCP_NOEXCEPT;

/**
 * Create a built-in filter
 * @param dispatcher Event dispatcher handle
 * @param type Built-in filter type
 * @param config JSON configuration
 * @return Filter handle or 0 on error
 */
MCP_API mcp_filter_t mcp_filter_create_builtin(mcp_dispatcher_t dispatcher,
                                               mcp_builtin_filter_type_t type,
                                               mcp_json_value_t config)
    MCP_NOEXCEPT;

/**
 * Retain filter (increment reference count)
 * @param filter Filter handle
 */
MCP_API void mcp_filter_retain(mcp_filter_t filter) MCP_NOEXCEPT;

/**
 * Release filter (decrement reference count)
 * @param filter Filter handle
 */
MCP_API void mcp_filter_release(mcp_filter_t filter) MCP_NOEXCEPT;

/**
 * Set filter callbacks
 * @param filter Filter handle
 * @param callbacks Callback structure
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_filter_set_callbacks(
    mcp_filter_t filter, const mcp_filter_callbacks_t* callbacks) MCP_NOEXCEPT;

/**
 * Set protocol metadata for filter
 * @param filter Filter handle
 * @param metadata Protocol metadata
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_filter_set_protocol_metadata(
    mcp_filter_t filter, const mcp_protocol_metadata_t* metadata) MCP_NOEXCEPT;

/**
 * Get protocol metadata from filter
 * @param filter Filter handle
 * @param metadata Output metadata structure
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_filter_get_protocol_metadata(
    mcp_filter_t filter, mcp_protocol_metadata_t* metadata) MCP_NOEXCEPT;

/* ============================================================================
 * Filter Chain Management
 * ============================================================================
 */

/**
 * Create filter chain builder
 * @param dispatcher Event dispatcher
 * @return Builder handle or NULL on error
 */
MCP_API mcp_filter_chain_builder_t
mcp_filter_chain_builder_create(mcp_dispatcher_t dispatcher) MCP_NOEXCEPT;

/**
 * Add filter to chain builder
 * @param builder Chain builder handle
 * @param filter Filter to add
 * @param position Position in chain
 * @param reference_filter Reference filter for BEFORE/AFTER positions
 * @return MCP_OK on success
 */
MCP_API mcp_result_t
mcp_filter_chain_add_filter(mcp_filter_chain_builder_t builder,
                            mcp_filter_t filter,
                            mcp_filter_position_t position,
                            mcp_filter_t reference_filter) MCP_NOEXCEPT;

/**
 * Build filter chain
 * @param builder Chain builder handle
 * @return Filter chain handle or 0 on error
 */
MCP_API mcp_filter_chain_t
mcp_filter_chain_build(mcp_filter_chain_builder_t builder) MCP_NOEXCEPT;

/**
 * Destroy filter chain builder
 * @param builder Chain builder handle
 */
MCP_API void mcp_filter_chain_builder_destroy(
    mcp_filter_chain_builder_t builder) MCP_NOEXCEPT;

/**
 * Retain filter chain
 * @param chain Filter chain handle
 */
MCP_API void mcp_filter_chain_retain(mcp_filter_chain_t chain) MCP_NOEXCEPT;

/**
 * Release filter chain
 * @param chain Filter chain handle
 */
MCP_API void mcp_filter_chain_release(mcp_filter_chain_t chain) MCP_NOEXCEPT;

/* ============================================================================
 * Filter Manager
 * ============================================================================
 */

/**
 * Create filter manager
 * @param connection Connection handle
 * @param dispatcher Event dispatcher
 * @return Filter manager handle or 0 on error
 */
MCP_API mcp_filter_manager_t mcp_filter_manager_create(
    mcp_connection_t connection, mcp_dispatcher_t dispatcher) MCP_NOEXCEPT;

/**
 * Add filter to manager
 * @param manager Filter manager handle
 * @param filter Filter to add
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_filter_manager_add_filter(
    mcp_filter_manager_t manager, mcp_filter_t filter) MCP_NOEXCEPT;

/**
 * Add filter chain to manager
 * @param manager Filter manager handle
 * @param chain Filter chain to add
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_filter_manager_add_chain(
    mcp_filter_manager_t manager, mcp_filter_chain_t chain) MCP_NOEXCEPT;

/**
 * Initialize filter manager
 * @param manager Filter manager handle
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_filter_manager_initialize(mcp_filter_manager_t manager)
    MCP_NOEXCEPT;

/**
 * Release filter manager
 * @param manager Filter manager handle
 */
MCP_API void mcp_filter_manager_release(mcp_filter_manager_t manager)
    MCP_NOEXCEPT;

/* ============================================================================
 * Zero-Copy Buffer Operations
 * ============================================================================
 */

/**
 * Get buffer slices for zero-copy access
 * @param buffer Buffer handle
 * @param slices Output array of slices
 * @param slice_count Input: max slices, Output: actual slices
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_filter_get_buffer_slices(mcp_buffer_handle_t buffer,
                                                  mcp_buffer_slice_t* slices,
                                                  size_t* slice_count)
    MCP_NOEXCEPT;

/**
 * Reserve buffer space for writing
 * @param buffer Buffer handle
 * @param size Size to reserve
 * @param slice Output slice with reserved memory
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_filter_reserve_buffer(mcp_buffer_handle_t buffer,
                                               size_t size,
                                               mcp_buffer_slice_t* slice)
    MCP_NOEXCEPT;

/**
 * Commit written data to buffer
 * @param buffer Buffer handle
 * @param bytes_written Actual bytes written
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_filter_commit_buffer(
    mcp_buffer_handle_t buffer, size_t bytes_written) MCP_NOEXCEPT;

/**
 * Create buffer handle from data
 * @param data Data pointer
 * @param length Data length
 * @param flags Buffer flags
 * @return Buffer handle or 0 on error
 */
MCP_API mcp_buffer_handle_t mcp_filter_buffer_create(
    const uint8_t* data, size_t length, uint32_t flags) MCP_NOEXCEPT;

/**
 * Release buffer handle
 * @param buffer Buffer handle
 */
MCP_API void mcp_filter_buffer_release(mcp_buffer_handle_t buffer) MCP_NOEXCEPT;

/**
 * Get buffer length
 * @param buffer Buffer handle
 * @return Buffer length in bytes
 */
MCP_API size_t mcp_filter_buffer_length(mcp_buffer_handle_t buffer)
    MCP_NOEXCEPT;

/* ============================================================================
 * Client/Server Integration
 * ============================================================================
 */

// Client context for filtered operations
typedef struct mcp_filter_client_context {
  mcp_client_t client;
  mcp_filter_chain_t request_filters;
  mcp_filter_chain_t response_filters;
} mcp_filter_client_context_t;

/**
 * Send client request through filters
 * @param context Client filter context
 * @param data Request data
 * @param length Data length
 * @param callback Completion callback
 * @param user_data User data for callback
 * @return Request ID or 0 on error
 */
MCP_API mcp_request_id_t
mcp_client_send_filtered(mcp_filter_client_context_t* context,
                         const uint8_t* data,
                         size_t length,
                         mcp_filter_completion_cb callback,
                         void* user_data) MCP_NOEXCEPT;

// Server context for filtered operations
typedef struct mcp_filter_server_context {
  mcp_server_t server;
  mcp_filter_chain_t request_filters;
  mcp_filter_chain_t response_filters;
} mcp_filter_server_context_t;

/**
 * Process server request through filters
 * @param context Server filter context
 * @param request_id Request ID
 * @param request_buffer Request buffer
 * @param callback Request callback
 * @param user_data User data for callback
 * @return MCP_OK on success
 */
MCP_API mcp_result_t
mcp_server_process_filtered(mcp_filter_server_context_t* context,
                            mcp_request_id_t request_id,
                            mcp_buffer_handle_t request_buffer,
                            mcp_filter_request_cb callback,
                            void* user_data) MCP_NOEXCEPT;

/* ============================================================================
 * Thread-Safe Operations
 * ============================================================================
 */

/**
 * Post data to filter from any thread
 * @param filter Filter handle
 * @param data Data to post
 * @param length Data length
 * @param callback Completion callback
 * @param user_data User data for callback
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_filter_post_data(mcp_filter_t filter,
                                          const uint8_t* data,
                                          size_t length,
                                          mcp_post_completion_cb callback,
                                          void* user_data) MCP_NOEXCEPT;

/* ============================================================================
 * Memory Management
 * ============================================================================
 */

// Filter resource guard for RAII
typedef struct mcp_filter_resource_guard mcp_filter_resource_guard_t;

/**
 * Create filter resource guard
 * @param dispatcher Event dispatcher
 * @return Resource guard or NULL on error
 */
MCP_API mcp_filter_resource_guard_t* mcp_filter_guard_create(
    mcp_dispatcher_t dispatcher) MCP_NOEXCEPT;

/**
 * Add filter to resource guard
 * @param guard Resource guard
 * @param filter Filter to track
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_filter_guard_add_filter(
    mcp_filter_resource_guard_t* guard, mcp_filter_t filter) MCP_NOEXCEPT;

/**
 * Release resource guard (cleanup all tracked resources)
 * @param guard Resource guard
 */
MCP_API void mcp_filter_guard_release(mcp_filter_resource_guard_t* guard)
    MCP_NOEXCEPT;

/* ============================================================================
 * Buffer Pool Management
 * ============================================================================
 */

typedef struct mcp_buffer_pool* mcp_buffer_pool_t;

/**
 * Create buffer pool
 * @param buffer_size Size of each buffer
 * @param max_buffers Maximum buffers in pool
 * @return Buffer pool handle or NULL on error
 */
MCP_API mcp_buffer_pool_t
mcp_buffer_pool_create(size_t buffer_size, size_t max_buffers) MCP_NOEXCEPT;

/**
 * Acquire buffer from pool
 * @param pool Buffer pool
 * @return Buffer handle or 0 if pool exhausted
 */
MCP_API mcp_buffer_handle_t mcp_buffer_pool_acquire(mcp_buffer_pool_t pool)
    MCP_NOEXCEPT;

/**
 * Release buffer back to pool
 * @param pool Buffer pool
 * @param buffer Buffer to release
 */
MCP_API void mcp_buffer_pool_release(mcp_buffer_pool_t pool,
                                     mcp_buffer_handle_t buffer) MCP_NOEXCEPT;

/**
 * Destroy buffer pool
 * @param pool Buffer pool
 */
MCP_API void mcp_buffer_pool_destroy(mcp_buffer_pool_t pool) MCP_NOEXCEPT;

/* ============================================================================
 * Statistics and Monitoring
 * ============================================================================
 */

typedef struct mcp_filter_stats {
  uint64_t bytes_processed;
  uint64_t packets_processed;
  uint64_t errors;
  uint64_t processing_time_us;
  double throughput_mbps;
} mcp_filter_stats_t;

/**
 * Get filter statistics
 * @param filter Filter handle
 * @param stats Output statistics
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_filter_get_stats(
    mcp_filter_t filter, mcp_filter_stats_t* stats) MCP_NOEXCEPT;

/**
 * Reset filter statistics
 * @param filter Filter handle
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_filter_reset_stats(mcp_filter_t filter) MCP_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* MCP_FILTER_API_H */