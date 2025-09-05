package com.gopher.mcp.jna;

import com.gopher.mcp.jna.type.filter.*;
import com.sun.jna.Callback;
import com.sun.jna.Library;
import com.sun.jna.NativeLong;
import com.sun.jna.Pointer;
import com.sun.jna.ptr.IntByReference;

/**
 * JNA interface for the MCP Filter API (mcp_c_filter_api.h).
 * This interface provides FFI-safe C bindings for the MCP filter architecture,
 * enabling Java to integrate with C++ filters through a clean interface.
 *
 * Architecture:
 * - Handle-based RAII system with automatic cleanup
 * - Zero-copy buffer operations where possible
 * - Thread-safe dispatcher-based execution
 * - Protocol-agnostic support for OSI layers 3-7
 * - Reusable filter chains across languages
 *
 * All methods are ordered exactly as they appear in mcp_c_filter_api.h
 */
public interface McpFilterLibrary extends Library {

    // Load the native library
    McpFilterLibrary INSTANCE = NativeLibraryLoader.loadLibrary(McpFilterLibrary.class);

    /* ============================================================================
     * Core Types and Constants (from mcp_c_filter_api.h lines 47-138)
     * ============================================================================
     */

    // Filter status for processing control
    int MCP_FILTER_CONTINUE = 0;
    int MCP_FILTER_STOP_ITERATION = 1;

    // Filter position in chain
    int MCP_FILTER_POSITION_FIRST = 0;
    int MCP_FILTER_POSITION_LAST = 1;
    int MCP_FILTER_POSITION_BEFORE = 2;
    int MCP_FILTER_POSITION_AFTER = 3;

    // Protocol layers (OSI model)
    int MCP_PROTOCOL_LAYER_3_NETWORK = 3;
    int MCP_PROTOCOL_LAYER_4_TRANSPORT = 4;
    int MCP_PROTOCOL_LAYER_5_SESSION = 5;
    int MCP_PROTOCOL_LAYER_6_PRESENTATION = 6;
    int MCP_PROTOCOL_LAYER_7_APPLICATION = 7;

    // Transport protocols for L4
    int MCP_TRANSPORT_PROTOCOL_TCP = 0;
    int MCP_TRANSPORT_PROTOCOL_UDP = 1;
    int MCP_TRANSPORT_PROTOCOL_QUIC = 2;
    int MCP_TRANSPORT_PROTOCOL_SCTP = 3;

    // Application protocols for L7
    int MCP_APP_PROTOCOL_HTTP = 0;
    int MCP_APP_PROTOCOL_HTTPS = 1;
    int MCP_APP_PROTOCOL_HTTP2 = 2;
    int MCP_APP_PROTOCOL_HTTP3 = 3;
    int MCP_APP_PROTOCOL_GRPC = 4;
    int MCP_APP_PROTOCOL_WEBSOCKET = 5;
    int MCP_APP_PROTOCOL_JSONRPC = 6;
    int MCP_APP_PROTOCOL_CUSTOM = 99;

    // Built-in filter types
    int MCP_FILTER_TCP_PROXY = 0;
    int MCP_FILTER_UDP_PROXY = 1;
    int MCP_FILTER_HTTP_CODEC = 10;
    int MCP_FILTER_HTTP_ROUTER = 11;
    int MCP_FILTER_HTTP_COMPRESSION = 12;
    int MCP_FILTER_TLS_TERMINATION = 20;
    int MCP_FILTER_AUTHENTICATION = 21;
    int MCP_FILTER_AUTHORIZATION = 22;
    int MCP_FILTER_ACCESS_LOG = 30;
    int MCP_FILTER_METRICS = 31;
    int MCP_FILTER_TRACING = 32;
    int MCP_FILTER_RATE_LIMIT = 40;
    int MCP_FILTER_CIRCUIT_BREAKER = 41;
    int MCP_FILTER_RETRY = 42;
    int MCP_FILTER_LOAD_BALANCER = 43;
    int MCP_FILTER_CUSTOM = 100;

    // Filter error codes
    int MCP_FILTER_ERROR_NONE = 0;
    int MCP_FILTER_ERROR_INVALID_CONFIG = -1000;
    int MCP_FILTER_ERROR_INITIALIZATION_FAILED = -1001;
    int MCP_FILTER_ERROR_BUFFER_OVERFLOW = -1002;
    int MCP_FILTER_ERROR_PROTOCOL_VIOLATION = -1003;
    int MCP_FILTER_ERROR_UPSTREAM_TIMEOUT = -1004;
    int MCP_FILTER_ERROR_CIRCUIT_OPEN = -1005;
    int MCP_FILTER_ERROR_RESOURCE_EXHAUSTED = -1006;
    int MCP_FILTER_ERROR_INVALID_STATE = -1007;

    // Buffer flags
    int MCP_BUFFER_FLAG_READONLY = 0x01;
    int MCP_BUFFER_FLAG_OWNED = 0x02;
    int MCP_BUFFER_FLAG_EXTERNAL = 0x04;
    int MCP_BUFFER_FLAG_ZERO_COPY = 0x08;

    // Result codes
    int MCP_OK = 0;
    int MCP_ERROR = -1;

    /* ============================================================================
     * Callback Types (from mcp_c_filter_api.h lines 204-233)
     * ============================================================================
     */

    // Filter data callback (onData from ReadFilter)
    interface MCP_FILTER_DATA_CB extends Callback {
        int invoke(long buffer, byte end_stream, Pointer user_data);
    }

    // Filter write callback (onWrite from WriteFilter)
    interface MCP_FILTER_WRITE_CB extends Callback {
        int invoke(long buffer, byte end_stream, Pointer user_data);
    }

    // Connection event callback
    interface MCP_FILTER_EVENT_CB extends Callback {
        int invoke(int state, Pointer user_data);
    }

    // Watermark callbacks
    interface MCP_FILTER_WATERMARK_CB extends Callback {
        void invoke(long filter, Pointer user_data);
    }

    // Error callback
    interface MCP_FILTER_ERROR_CB extends Callback {
        void invoke(long filter, int error, String message, Pointer user_data);
    }

    // Completion callback for async operations
    interface MCP_FILTER_COMPLETION_CB extends Callback {
        void invoke(int result, Pointer user_data);
    }

    // Post completion callback
    interface MCP_POST_COMPLETION_CB extends Callback {
        void invoke(int result, Pointer user_data);
    }

    // Request callback for server
    interface MCP_FILTER_REQUEST_CB extends Callback {
        void invoke(long response_buffer, int result, Pointer user_data);
    }

    /* ============================================================================
     * Filter Lifecycle Management (lines 260-321)
     * ============================================================================
     */

    /**
     * Create a new filter (line 267)
     * @param dispatcher Event dispatcher handle
     * @param config Filter configuration
     * @return Filter handle or 0 on error
     */
    long mcp_filter_create(Pointer dispatcher, McpFilterConfig.ByReference config);

    /**
     * Create a built-in filter (line 278)
     * @param dispatcher Event dispatcher handle
     * @param type Built-in filter type
     * @param config JSON configuration
     * @return Filter handle or 0 on error
     */
    long mcp_filter_create_builtin(Pointer dispatcher, int type, Pointer config);

    /**
     * Retain filter (increment reference count) (line 287)
     * @param filter Filter handle
     */
    void mcp_filter_retain(long filter);

    /**
     * Release filter (decrement reference count) (line 293)
     * @param filter Filter handle
     */
    void mcp_filter_release(long filter);

    /**
     * Set filter callbacks (line 301)
     * @param filter Filter handle
     * @param callbacks Callback structure
     * @return MCP_OK on success
     */
    int mcp_filter_set_callbacks(long filter, McpFilterCallbacks.ByReference callbacks);

    /**
     * Set protocol metadata for filter (line 310)
     * @param filter Filter handle
     * @param metadata Protocol metadata
     * @return MCP_OK on success
     */
    int mcp_filter_set_protocol_metadata(long filter, McpProtocolMetadata.ByReference metadata);

    /**
     * Get protocol metadata from filter (line 319)
     * @param filter Filter handle
     * @param metadata Output metadata structure
     * @return MCP_OK on success
     */
    int mcp_filter_get_protocol_metadata(long filter, McpProtocolMetadata.ByReference metadata);

    /* ============================================================================
     * Filter Chain Management (lines 323-375)
     * ============================================================================
     */

    /**
     * Create filter chain builder (line 332)
     * @param dispatcher Event dispatcher
     * @return Builder handle or NULL on error
     */
    Pointer mcp_filter_chain_builder_create(Pointer dispatcher);

    /**
     * Add filter to chain builder (line 343)
     * @param builder Chain builder handle
     * @param filter Filter to add
     * @param position Position in chain
     * @param reference_filter Reference filter for BEFORE/AFTER positions
     * @return MCP_OK on success
     */
    int mcp_filter_chain_add_filter(Pointer builder, long filter, int position, long reference_filter);

    /**
     * Build filter chain (line 354)
     * @param builder Chain builder handle
     * @return Filter chain handle or 0 on error
     */
    long mcp_filter_chain_build(Pointer builder);

    /**
     * Destroy filter chain builder (line 361)
     * @param builder Chain builder handle
     */
    void mcp_filter_chain_builder_destroy(Pointer builder);

    /**
     * Retain filter chain (line 368)
     * @param chain Filter chain handle
     */
    void mcp_filter_chain_retain(long chain);

    /**
     * Release filter chain (line 374)
     * @param chain Filter chain handle
     */
    void mcp_filter_chain_release(long chain);

    /* ============================================================================
     * Filter Manager (lines 377-422)
     * ============================================================================
     */

    /**
     * Create filter manager (line 387)
     * @param connection Connection handle
     * @param dispatcher Event dispatcher
     * @return Filter manager handle or 0 on error
     */
    long mcp_filter_manager_create(Pointer connection, Pointer dispatcher);

    /**
     * Add filter to manager (line 396)
     * @param manager Filter manager handle
     * @param filter Filter to add
     * @return MCP_OK on success
     */
    int mcp_filter_manager_add_filter(long manager, long filter);

    /**
     * Add filter chain to manager (line 405)
     * @param manager Filter manager handle
     * @param chain Filter chain to add
     * @return MCP_OK on success
     */
    int mcp_filter_manager_add_chain(long manager, long chain);

    /**
     * Initialize filter manager (line 413)
     * @param manager Filter manager handle
     * @return MCP_OK on success
     */
    int mcp_filter_manager_initialize(long manager);

    /**
     * Release filter manager (line 420)
     * @param manager Filter manager handle
     */
    void mcp_filter_manager_release(long manager);

    /* ============================================================================
     * Zero-Copy Buffer Operations (lines 424-485)
     * ============================================================================
     */

    /**
     * Get buffer slices for zero-copy access (line 435)
     * @param buffer Buffer handle
     * @param slices Output array of slices
     * @param slice_count Input: max slices, Output: actual slices
     * @return MCP_OK on success
     */
    int mcp_filter_get_buffer_slices(long buffer, McpBufferSlice[] slices, IntByReference slice_count);

    /**
     * Reserve buffer space for writing (line 447)
     * @param buffer Buffer handle
     * @param size Size to reserve
     * @param slice Output slice with reserved memory
     * @return MCP_OK on success
     */
    int mcp_filter_reserve_buffer(long buffer, NativeLong size, McpBufferSlice.ByReference slice);

    /**
     * Commit written data to buffer (line 458)
     * @param buffer Buffer handle
     * @param bytes_written Actual bytes written
     * @return MCP_OK on success
     */
    int mcp_filter_commit_buffer(long buffer, NativeLong bytes_written);

    /**
     * Create buffer handle from data (line 468)
     * @param data Data pointer
     * @param length Data length
     * @param flags Buffer flags
     * @return Buffer handle or 0 on error
     */
    long mcp_filter_buffer_create(byte[] data, NativeLong length, int flags);

    /**
     * Release buffer handle (line 475)
     * @param buffer Buffer handle
     */
    void mcp_filter_buffer_release(long buffer);

    /**
     * Get buffer length (line 482)
     * @param buffer Buffer handle
     * @return Buffer length in bytes
     */
    NativeLong mcp_filter_buffer_length(long buffer);

    /* ============================================================================
     * Client/Server Integration (lines 487-535)
     * ============================================================================
     */

    /**
     * Send client request through filters (line 506)
     * @param context Client filter context
     * @param data Request data
     * @param length Data length
     * @param callback Completion callback
     * @param user_data User data for callback
     * @return Request ID or 0 on error
     */
    long mcp_client_send_filtered(McpFilterClientContext.ByReference context,
                                   byte[] data, NativeLong length,
                                   MCP_FILTER_COMPLETION_CB callback, Pointer user_data);

    /**
     * Process server request through filters (line 529)
     * @param context Server filter context
     * @param request_id Request ID
     * @param request_buffer Request buffer
     * @param callback Request callback
     * @param user_data User data for callback
     * @return MCP_OK on success
     */
    int mcp_server_process_filtered(McpFilterServerContext.ByReference context,
                                     long request_id, long request_buffer,
                                     MCP_FILTER_REQUEST_CB callback, Pointer user_data);

    /* ============================================================================
     * Thread-Safe Operations (lines 537-555)
     * ============================================================================
     */

    /**
     * Post data to filter from any thread (line 550)
     * @param filter Filter handle
     * @param data Data to post
     * @param length Data length
     * @param callback Completion callback
     * @param user_data User data for callback
     * @return MCP_OK on success
     */
    int mcp_filter_post_data(long filter, byte[] data, NativeLong length,
                             MCP_POST_COMPLETION_CB callback, Pointer user_data);

    /* ============================================================================
     * Memory Management (lines 557-587)
     * ============================================================================
     */

    /**
     * Create filter resource guard (line 569)
     * @param dispatcher Event dispatcher
     * @return Resource guard or NULL on error
     */
    Pointer mcp_filter_guard_create(Pointer dispatcher);

    /**
     * Add filter to resource guard (line 578)
     * @param guard Resource guard
     * @param filter Filter to track
     * @return MCP_OK on success
     */
    int mcp_filter_guard_add_filter(Pointer guard, long filter);

    /**
     * Release resource guard (cleanup all tracked resources) (line 585)
     * @param guard Resource guard
     */
    void mcp_filter_guard_release(Pointer guard);

    /* ============================================================================
     * Buffer Pool Management (lines 589-625)
     * ============================================================================
     */

    /**
     * Create buffer pool (line 601)
     * @param buffer_size Size of each buffer
     * @param max_buffers Maximum buffers in pool
     * @return Buffer pool handle or NULL on error
     */
    Pointer mcp_buffer_pool_create(NativeLong buffer_size, NativeLong max_buffers);

    /**
     * Acquire buffer from pool (line 609)
     * @param pool Buffer pool
     * @return Buffer handle or 0 if pool exhausted
     */
    long mcp_buffer_pool_acquire(Pointer pool);

    /**
     * Release buffer back to pool (line 617)
     * @param pool Buffer pool
     * @param buffer Buffer to release
     */
    void mcp_buffer_pool_release(Pointer pool, long buffer);

    /**
     * Destroy buffer pool (line 624)
     * @param pool Buffer pool
     */
    void mcp_buffer_pool_destroy(Pointer pool);

    /* ============================================================================
     * Statistics and Monitoring (lines 627-654)
     * ============================================================================
     */

    /**
     * Get filter statistics (line 645)
     * @param filter Filter handle
     * @param stats Output statistics
     * @return MCP_OK on success
     */
    int mcp_filter_get_stats(long filter, McpFilterStats.ByReference stats);

    /**
     * Reset filter statistics (line 653)
     * @param filter Filter handle
     * @return MCP_OK on success
     */
    int mcp_filter_reset_stats(long filter);
}
