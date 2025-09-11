package com.gopher.mcp.jna

import com.gopher.mcp.jna.type.filter.*
import com.sun.jna.Callback
import com.sun.jna.Library
import com.sun.jna.NativeLong
import com.sun.jna.Pointer
import com.sun.jna.ptr.IntByReference

/**
 * JNA interface for the MCP Filter API (mcp_c_filter_api.h).
 * This interface provides FFI-safe C bindings for the MCP filter architecture,
 * enabling Kotlin to integrate with C++ filters through a clean interface.
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
interface McpFilterLibrary : Library {

    companion object {
        // Load the native library
        @JvmStatic
        val INSTANCE: McpFilterLibrary = NativeLibraryLoader.loadLibrary(McpFilterLibrary::class.java)

        /* ============================================================================
         * Core Types and Constants (from mcp_c_filter_api.h lines 47-138)
         * ============================================================================
         */

        // Filter status for processing control
        const val MCP_FILTER_CONTINUE = 0
        const val MCP_FILTER_STOP_ITERATION = 1

        // Filter position in chain
        const val MCP_FILTER_POSITION_FIRST = 0
        const val MCP_FILTER_POSITION_LAST = 1
        const val MCP_FILTER_POSITION_BEFORE = 2
        const val MCP_FILTER_POSITION_AFTER = 3

        // Protocol layers (OSI model)
        const val MCP_PROTOCOL_LAYER_3_NETWORK = 3
        const val MCP_PROTOCOL_LAYER_4_TRANSPORT = 4
        const val MCP_PROTOCOL_LAYER_5_SESSION = 5
        const val MCP_PROTOCOL_LAYER_6_PRESENTATION = 6
        const val MCP_PROTOCOL_LAYER_7_APPLICATION = 7

        // Transport protocols for L4
        const val MCP_TRANSPORT_PROTOCOL_TCP = 0
        const val MCP_TRANSPORT_PROTOCOL_UDP = 1
        const val MCP_TRANSPORT_PROTOCOL_QUIC = 2
        const val MCP_TRANSPORT_PROTOCOL_SCTP = 3

        // Application protocols for L7
        const val MCP_APP_PROTOCOL_HTTP = 0
        const val MCP_APP_PROTOCOL_HTTPS = 1
        const val MCP_APP_PROTOCOL_HTTP2 = 2
        const val MCP_APP_PROTOCOL_HTTP3 = 3
        const val MCP_APP_PROTOCOL_GRPC = 4
        const val MCP_APP_PROTOCOL_WEBSOCKET = 5
        const val MCP_APP_PROTOCOL_JSONRPC = 6
        const val MCP_APP_PROTOCOL_CUSTOM = 99

        // Built-in filter types
        const val MCP_FILTER_TCP_PROXY = 0
        const val MCP_FILTER_UDP_PROXY = 1
        const val MCP_FILTER_HTTP_CODEC = 10
        const val MCP_FILTER_HTTP_ROUTER = 11
        const val MCP_FILTER_HTTP_COMPRESSION = 12
        const val MCP_FILTER_TLS_TERMINATION = 20
        const val MCP_FILTER_AUTHENTICATION = 21
        const val MCP_FILTER_AUTHORIZATION = 22
        const val MCP_FILTER_ACCESS_LOG = 30
        const val MCP_FILTER_METRICS = 31
        const val MCP_FILTER_TRACING = 32
        const val MCP_FILTER_RATE_LIMIT = 40
        const val MCP_FILTER_CIRCUIT_BREAKER = 41
        const val MCP_FILTER_RETRY = 42
        const val MCP_FILTER_LOAD_BALANCER = 43
        const val MCP_FILTER_CUSTOM = 100

        // Filter error codes
        const val MCP_FILTER_ERROR_NONE = 0
        const val MCP_FILTER_ERROR_INVALID_CONFIG = -1000
        const val MCP_FILTER_ERROR_INITIALIZATION_FAILED = -1001
        const val MCP_FILTER_ERROR_BUFFER_OVERFLOW = -1002
        const val MCP_FILTER_ERROR_PROTOCOL_VIOLATION = -1003
        const val MCP_FILTER_ERROR_UPSTREAM_TIMEOUT = -1004
        const val MCP_FILTER_ERROR_CIRCUIT_OPEN = -1005
        const val MCP_FILTER_ERROR_RESOURCE_EXHAUSTED = -1006
        const val MCP_FILTER_ERROR_INVALID_STATE = -1007

        // Buffer flags
        const val MCP_BUFFER_FLAG_READONLY = 0x01
        const val MCP_BUFFER_FLAG_OWNED = 0x02
        const val MCP_BUFFER_FLAG_EXTERNAL = 0x04
        const val MCP_BUFFER_FLAG_ZERO_COPY = 0x08

        // Result codes
        const val MCP_OK = 0
        const val MCP_ERROR = -1
    }

    /* ============================================================================
     * Callback Types (from mcp_c_filter_api.h lines 204-233)
     * ============================================================================
     */

    // Filter data callback (onData from ReadFilter)
    interface MCP_FILTER_DATA_CB : Callback {
        fun invoke(buffer: Long, end_stream: Byte, user_data: Pointer?): Int
    }

    // Filter write callback (onWrite from WriteFilter)
    interface MCP_FILTER_WRITE_CB : Callback {
        fun invoke(buffer: Long, end_stream: Byte, user_data: Pointer?): Int
    }

    // Connection event callback
    interface MCP_FILTER_EVENT_CB : Callback {
        fun invoke(state: Int, user_data: Pointer?): Int
    }

    // Watermark callbacks
    interface MCP_FILTER_WATERMARK_CB : Callback {
        fun invoke(filter: Long, user_data: Pointer?)
    }

    // Error callback
    interface MCP_FILTER_ERROR_CB : Callback {
        fun invoke(filter: Long, error: Int, message: String?, user_data: Pointer?)
    }

    // Completion callback for async operations
    interface MCP_FILTER_COMPLETION_CB : Callback {
        fun invoke(result: Int, user_data: Pointer?)
    }

    // Post completion callback
    interface MCP_POST_COMPLETION_CB : Callback {
        fun invoke(result: Int, user_data: Pointer?)
    }

    // Request callback for server
    interface MCP_FILTER_REQUEST_CB : Callback {
        fun invoke(response_buffer: Long, result: Int, user_data: Pointer?)
    }

    /* ============================================================================
     * Filter Lifecycle Management (lines 260-321)
     * ============================================================================
     */

    /**
     * Create a new filter (line 267)
     *
     * @param dispatcher Event dispatcher handle
     * @param config Filter configuration
     * @return Filter handle or 0 on error
     */
    fun mcp_filter_create(dispatcher: Pointer?, config: McpFilterConfig.ByReference?): Long

    /**
     * Create a built-in filter (line 278)
     *
     * @param dispatcher Event dispatcher handle
     * @param type Built-in filter type
     * @param config JSON configuration
     * @return Filter handle or 0 on error
     */
    fun mcp_filter_create_builtin(dispatcher: Pointer?, type: Int, config: Pointer?): Long

    /**
     * Retain filter (increment reference count) (line 287)
     *
     * @param filter Filter handle
     */
    fun mcp_filter_retain(filter: Long)

    /**
     * Release filter (decrement reference count) (line 293)
     *
     * @param filter Filter handle
     */
    fun mcp_filter_release(filter: Long)

    /**
     * Set filter callbacks (line 301)
     *
     * @param filter Filter handle
     * @param callbacks Callback structure
     * @return MCP_OK on success
     */
    fun mcp_filter_set_callbacks(filter: Long, callbacks: McpFilterCallbacks.ByReference?): Int

    /**
     * Set protocol metadata for filter (line 310)
     *
     * @param filter Filter handle
     * @param metadata Protocol metadata
     * @return MCP_OK on success
     */
    fun mcp_filter_set_protocol_metadata(filter: Long, metadata: McpProtocolMetadata.ByReference?): Int

    /**
     * Get protocol metadata from filter (line 319)
     *
     * @param filter Filter handle
     * @param metadata Output metadata structure
     * @return MCP_OK on success
     */
    fun mcp_filter_get_protocol_metadata(filter: Long, metadata: McpProtocolMetadata.ByReference?): Int

    /* ============================================================================
     * Filter Chain Management (lines 323-375)
     * ============================================================================
     */

    /**
     * Create filter chain builder (line 332)
     *
     * @param dispatcher Event dispatcher
     * @return Builder handle or NULL on error
     */
    fun mcp_filter_chain_builder_create(dispatcher: Pointer?): Pointer?

    /**
     * Add filter to chain builder (line 343)
     *
     * @param builder Chain builder handle
     * @param filter Filter to add
     * @param position Position in chain
     * @param reference_filter Reference filter for BEFORE/AFTER positions
     * @return MCP_OK on success
     */
    fun mcp_filter_chain_add_filter(
        builder: Pointer?,
        filter: Long,
        position: Int,
        reference_filter: Long
    ): Int

    /**
     * Build filter chain (line 354)
     *
     * @param builder Chain builder handle
     * @return Filter chain handle or 0 on error
     */
    fun mcp_filter_chain_build(builder: Pointer?): Long

    /**
     * Destroy filter chain builder (line 361)
     *
     * @param builder Chain builder handle
     */
    fun mcp_filter_chain_builder_destroy(builder: Pointer?)

    /**
     * Retain filter chain (line 368)
     *
     * @param chain Filter chain handle
     */
    fun mcp_filter_chain_retain(chain: Long)

    /**
     * Release filter chain (line 374)
     *
     * @param chain Filter chain handle
     */
    fun mcp_filter_chain_release(chain: Long)

    /* ============================================================================
     * Filter Manager (lines 377-422)
     * ============================================================================
     */

    /**
     * Create filter manager (line 387)
     *
     * @param connection Connection handle
     * @param dispatcher Event dispatcher
     * @return Filter manager handle or 0 on error
     */
    fun mcp_filter_manager_create(connection: Pointer?, dispatcher: Pointer?): Long

    /**
     * Add filter to manager (line 396)
     *
     * @param manager Filter manager handle
     * @param filter Filter to add
     * @return MCP_OK on success
     */
    fun mcp_filter_manager_add_filter(manager: Long, filter: Long): Int

    /**
     * Add filter chain to manager (line 405)
     *
     * @param manager Filter manager handle
     * @param chain Filter chain to add
     * @return MCP_OK on success
     */
    fun mcp_filter_manager_add_chain(manager: Long, chain: Long): Int

    /**
     * Initialize filter manager (line 413)
     *
     * @param manager Filter manager handle
     * @return MCP_OK on success
     */
    fun mcp_filter_manager_initialize(manager: Long): Int

    /**
     * Release filter manager (line 420)
     *
     * @param manager Filter manager handle
     */
    fun mcp_filter_manager_release(manager: Long)

    /* ============================================================================
     * Zero-Copy Buffer Operations (lines 424-485)
     * ============================================================================
     */

    /**
     * Get buffer slices for zero-copy access (line 435)
     *
     * @param buffer Buffer handle
     * @param slices Output array of slices
     * @param slice_count Input: max slices, Output: actual slices
     * @return MCP_OK on success
     */
    fun mcp_filter_get_buffer_slices(
        buffer: Long,
        slices: Array<McpBufferSlice>?,
        slice_count: IntByReference?
    ): Int

    /**
     * Reserve buffer space for writing (line 447)
     *
     * @param buffer Buffer handle
     * @param size Size to reserve
     * @param slice Output slice with reserved memory
     * @return MCP_OK on success
     */
    fun mcp_filter_reserve_buffer(buffer: Long, size: NativeLong, slice: McpBufferSlice.ByReference?): Int

    /**
     * Commit written data to buffer (line 458)
     *
     * @param buffer Buffer handle
     * @param bytes_written Actual bytes written
     * @return MCP_OK on success
     */
    fun mcp_filter_commit_buffer(buffer: Long, bytes_written: NativeLong): Int

    /**
     * Create buffer handle from data (line 468)
     *
     * @param data Data pointer
     * @param length Data length
     * @param flags Buffer flags
     * @return Buffer handle or 0 on error
     */
    fun mcp_filter_buffer_create(data: ByteArray?, length: NativeLong, flags: Int): Long

    /**
     * Release buffer handle (line 475)
     *
     * @param buffer Buffer handle
     */
    fun mcp_filter_buffer_release(buffer: Long)

    /**
     * Get buffer length (line 482)
     *
     * @param buffer Buffer handle
     * @return Buffer length in bytes
     */
    fun mcp_filter_buffer_length(buffer: Long): NativeLong

    /* ============================================================================
     * Client/Server Integration (lines 487-535)
     * ============================================================================
     */

    /**
     * Send client request through filters (line 506)
     *
     * @param context Client filter context
     * @param data Request data
     * @param length Data length
     * @param callback Completion callback
     * @param user_data User data for callback
     * @return Request ID or 0 on error
     */
    fun mcp_client_send_filtered(
        context: McpFilterClientContext.ByReference?,
        data: ByteArray?,
        length: NativeLong,
        callback: MCP_FILTER_COMPLETION_CB?,
        user_data: Pointer?
    ): Long

    /**
     * Process server request through filters (line 529)
     *
     * @param context Server filter context
     * @param request_id Request ID
     * @param request_buffer Request buffer
     * @param callback Request callback
     * @param user_data User data for callback
     * @return MCP_OK on success
     */
    fun mcp_server_process_filtered(
        context: McpFilterServerContext.ByReference?,
        request_id: Long,
        request_buffer: Long,
        callback: MCP_FILTER_REQUEST_CB?,
        user_data: Pointer?
    ): Int

    /* ============================================================================
     * Thread-Safe Operations (lines 537-555)
     * ============================================================================
     */

    /**
     * Post data to filter from any thread (line 550)
     *
     * @param filter Filter handle
     * @param data Data to post
     * @param length Data length
     * @param callback Completion callback
     * @param user_data User data for callback
     * @return MCP_OK on success
     */
    fun mcp_filter_post_data(
        filter: Long,
        data: ByteArray?,
        length: NativeLong,
        callback: MCP_POST_COMPLETION_CB?,
        user_data: Pointer?
    ): Int

    /* ============================================================================
     * Memory Management (lines 557-587)
     * ============================================================================
     */

    /**
     * Create filter resource guard (line 569)
     *
     * @param dispatcher Event dispatcher
     * @return Resource guard or NULL on error
     */
    fun mcp_filter_guard_create(dispatcher: Pointer?): Pointer?

    /**
     * Add filter to resource guard (line 578)
     *
     * @param guard Resource guard
     * @param filter Filter to track
     * @return MCP_OK on success
     */
    fun mcp_filter_guard_add_filter(guard: Pointer?, filter: Long): Int

    /**
     * Release resource guard (cleanup all tracked resources) (line 585)
     *
     * @param guard Resource guard
     */
    fun mcp_filter_guard_release(guard: Pointer?)

    /* ============================================================================
     * Buffer Pool Management (lines 589-625)
     * ============================================================================
     */

    /**
     * Create buffer pool (line 601)
     *
     * @param buffer_size Size of each buffer
     * @param max_buffers Maximum buffers in pool
     * @return Buffer pool handle or NULL on error
     */
    fun mcp_buffer_pool_create(buffer_size: NativeLong, max_buffers: NativeLong): Pointer?

    /**
     * Acquire buffer from pool (line 609)
     *
     * @param pool Buffer pool
     * @return Buffer handle or 0 if pool exhausted
     */
    fun mcp_buffer_pool_acquire(pool: Pointer?): Long

    /**
     * Release buffer back to pool (line 617)
     *
     * @param pool Buffer pool
     * @param buffer Buffer to release
     */
    fun mcp_buffer_pool_release(pool: Pointer?, buffer: Long)

    /**
     * Destroy buffer pool (line 624)
     *
     * @param pool Buffer pool
     */
    fun mcp_buffer_pool_destroy(pool: Pointer?)

    /* ============================================================================
     * Statistics and Monitoring (lines 627-654)
     * ============================================================================
     */

    /**
     * Get filter statistics (line 645)
     *
     * @param filter Filter handle
     * @param stats Output statistics
     * @return MCP_OK on success
     */
    fun mcp_filter_get_stats(filter: Long, stats: McpFilterStats.ByReference?): Int

    /**
     * Reset filter statistics (line 653)
     *
     * @param filter Filter handle
     * @return MCP_OK on success
     */
    fun mcp_filter_reset_stats(filter: Long): Int
}