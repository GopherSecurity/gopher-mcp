package com.gopher.mcp.filter

import com.gopher.mcp.filter.type.*
import com.gopher.mcp.filter.type.buffer.BufferSlice
import com.gopher.mcp.jna.McpFilterLibrary
import com.gopher.mcp.jna.type.filter.*
import com.gopher.mcp.util.BufferUtils
import com.sun.jna.NativeLong
import com.sun.jna.Pointer
import com.sun.jna.ptr.IntByReference
import java.nio.ByteBuffer

/**
 * Kotlin wrapper for the MCP Filter API. Provides a high-level interface to the native MCP filter
 * library through JNA.
 *
 * Features: - Handle-based RAII system with automatic cleanup - Zero-copy buffer operations
 * where possible - Thread-safe dispatcher-based execution - Protocol-agnostic support for OSI
 * layers 3-7 - Reusable filter chains across languages
 */
class McpFilter : AutoCloseable {

    private val lib: McpFilterLibrary
    private var filterHandle: Long? = null

    // ============================================================================
    // Constructors
    // ============================================================================

    constructor() {
        this.lib = McpFilterLibrary.INSTANCE
    }

    protected constructor(library: McpFilterLibrary) {
        this.lib = library
    }

    // ============================================================================
    // Filter Lifecycle Management
    // ============================================================================

    /**
     * Create a new filter
     *
     * @param dispatcher Event dispatcher handle
     * @param config Filter configuration
     * @return Filter handle or 0 on error
     */
    fun create(dispatcher: Long, config: FilterConfig): Long {
        val nativeConfig = McpFilterConfig.ByReference().apply {
            this.name = config.name
            this.filter_type = config.filterType
            this.config_json = if (config.configJson != null) Pointer.NULL else null // TODO: Convert JSON
            this.layer = config.layer
        }

        val handle = lib.mcp_filter_create(Pointer(dispatcher), nativeConfig)
        if (filterHandle == null && handle != 0L) {
            filterHandle = handle
        }
        return handle
    }

    /**
     * Create a built-in filter
     *
     * @param dispatcher Event dispatcher handle
     * @param type Built-in filter type
     * @param configJson JSON configuration string (can be null)
     * @return Filter handle or 0 on error
     */
    fun createBuiltin(dispatcher: Long, type: Int, configJson: String?): Long {
        val jsonPtr = if (configJson != null) Pointer.NULL else null // TODO: Convert JSON string
        val handle = lib.mcp_filter_create_builtin(Pointer(dispatcher), type, jsonPtr)
        if (filterHandle == null && handle != 0L) {
            filterHandle = handle
        }
        return handle
    }

    /**
     * Retain filter (increment reference count)
     *
     * @param filter Filter handle
     */
    fun retain(filter: Long) {
        lib.mcp_filter_retain(filter)
    }

    /**
     * Release filter (decrement reference count)
     *
     * @param filter Filter handle
     */
    fun release(filter: Long) {
        lib.mcp_filter_release(filter)
    }

    /**
     * Set filter callbacks
     *
     * @param filter Filter handle
     * @param onData Data callback
     * @param onWrite Write callback
     * @param onEvent Event callback
     * @param onMetadata Metadata callback
     * @param onTrailers Trailers callback
     * @param userData User data object
     * @return MCP_OK on success
     */
    fun setCallbacks(
        filter: Long,
        onData: FilterDataCallback?,
        onWrite: FilterWriteCallback?,
        onEvent: FilterEventCallback?,
        onMetadata: FilterMetadataCallback?,
        onTrailers: FilterTrailersCallback?,
        userData: Any?
    ): Int {
        val callbacks = McpFilterCallbacks.ByReference()

        // Convert Kotlin callbacks to JNA callbacks
        if (onData != null) {
            callbacks.on_data = Pointer.NULL // TODO: Create JNA callback wrapper
        }
        if (onWrite != null) {
            callbacks.on_write = Pointer.NULL // TODO: Create JNA callback wrapper
        }
        if (onEvent != null) {
            callbacks.on_event = Pointer.NULL // TODO: Create JNA callback wrapper
        }
        if (onMetadata != null) {
            callbacks.on_metadata = Pointer.NULL // TODO: Create JNA callback wrapper
        }
        if (onTrailers != null) {
            callbacks.on_trailers = Pointer.NULL // TODO: Create JNA callback wrapper
        }
        callbacks.user_data = Pointer.NULL // TODO: Store user data

        return lib.mcp_filter_set_callbacks(filter, callbacks)
    }

    /**
     * Set protocol metadata for filter
     *
     * @param filter Filter handle
     * @param metadata Protocol metadata
     * @return MCP_OK on success
     */
    fun setProtocolMetadata(filter: Long, metadata: ProtocolMetadata): Int {
        val nativeMeta = McpProtocolMetadata.ByReference().apply {
            this.layer = metadata.layer
            // TODO: Convert union data based on layer
        }
        return lib.mcp_filter_set_protocol_metadata(filter, nativeMeta)
    }

    /**
     * Get protocol metadata from filter
     *
     * @param filter Filter handle
     * @return Protocol metadata or null on error
     */
    fun getProtocolMetadata(filter: Long): ProtocolMetadata? {
        val nativeMeta = McpProtocolMetadata.ByReference()
        val result = lib.mcp_filter_get_protocol_metadata(filter, nativeMeta)
        return if (result == ResultCode.OK.value) {
            // TODO: Convert native metadata to Kotlin object
            ProtocolMetadata()
        } else {
            null
        }
    }

    // ============================================================================
    // Filter Chain Management
    // ============================================================================

    /**
     * Create filter chain builder
     *
     * @param dispatcher Event dispatcher handle
     * @return Builder handle or 0 on error
     */
    fun chainBuilderCreate(dispatcher: Long): Long {
        val builder = lib.mcp_filter_chain_builder_create(Pointer(dispatcher))
        return Pointer.nativeValue(builder)
    }

    /**
     * Add filter to chain builder
     *
     * @param builder Chain builder handle
     * @param filter Filter to add
     * @param position Position in chain
     * @param referenceFilter Reference filter for BEFORE/AFTER positions
     * @return MCP_OK on success
     */
    fun chainAddFilter(builder: Long, filter: Long, position: Int, referenceFilter: Long): Int {
        return lib.mcp_filter_chain_add_filter(Pointer(builder), filter, position, referenceFilter)
    }

    /**
     * Build filter chain
     *
     * @param builder Chain builder handle
     * @return Filter chain handle or 0 on error
     */
    fun chainBuild(builder: Long): Long {
        return lib.mcp_filter_chain_build(Pointer(builder))
    }

    /**
     * Destroy filter chain builder
     *
     * @param builder Chain builder handle
     */
    fun chainBuilderDestroy(builder: Long) {
        lib.mcp_filter_chain_builder_destroy(Pointer(builder))
    }

    /**
     * Retain filter chain
     *
     * @param chain Filter chain handle
     */
    fun chainRetain(chain: Long) {
        lib.mcp_filter_chain_retain(chain)
    }

    /**
     * Release filter chain
     *
     * @param chain Filter chain handle
     */
    fun chainRelease(chain: Long) {
        lib.mcp_filter_chain_release(chain)
    }

    // ============================================================================
    // Filter Manager
    // ============================================================================

    /**
     * Create filter manager
     *
     * @param connection Connection handle
     * @param dispatcher Event dispatcher handle
     * @return Filter manager handle or 0 on error
     */
    fun managerCreate(connection: Long, dispatcher: Long): Long {
        return lib.mcp_filter_manager_create(Pointer(connection), Pointer(dispatcher))
    }

    /**
     * Add filter to manager
     *
     * @param manager Filter manager handle
     * @param filter Filter to add
     * @return MCP_OK on success
     */
    fun managerAddFilter(manager: Long, filter: Long): Int {
        return lib.mcp_filter_manager_add_filter(manager, filter)
    }

    /**
     * Add filter chain to manager
     *
     * @param manager Filter manager handle
     * @param chain Filter chain to add
     * @return MCP_OK on success
     */
    fun managerAddChain(manager: Long, chain: Long): Int {
        return lib.mcp_filter_manager_add_chain(manager, chain)
    }

    /**
     * Initialize filter manager
     *
     * @param manager Filter manager handle
     * @return MCP_OK on success
     */
    fun managerInitialize(manager: Long): Int {
        return lib.mcp_filter_manager_initialize(manager)
    }

    /**
     * Release filter manager
     *
     * @param manager Filter manager handle
     */
    fun managerRelease(manager: Long) {
        lib.mcp_filter_manager_release(manager)
    }

    // ============================================================================
    // Zero-Copy Buffer Operations
    // ============================================================================

    /**
     * Get buffer slices for zero-copy access
     *
     * @param buffer Buffer handle
     * @param maxSlices Maximum number of slices to retrieve
     * @return Array of buffer slices or null on error
     */
    fun getBufferSlices(buffer: Long, maxSlices: Int): Array<BufferSlice>? {
        if (maxSlices <= 0) {
            return null
        }
        
        // For JNA, we need to create a contiguous array using toArray
        val firstSlice = McpBufferSlice()
        val nativeSlices = firstSlice.toArray(maxSlices) as Array<McpBufferSlice>
        val sliceCount = IntByReference(maxSlices)

        val result = lib.mcp_filter_get_buffer_slices(buffer, nativeSlices, sliceCount)
        return if (result == ResultCode.OK.value) {
            val actualCount = sliceCount.value
            Array(actualCount) { i ->
                // Convert Pointer to ByteBuffer using the utility class
                val dataBuffer = BufferUtils.toByteBuffer(nativeSlices[i].data, nativeSlices[i].size)
                BufferSlice(dataBuffer, nativeSlices[i].size, nativeSlices[i].flags)
            }
        } else {
            null
        }
    }

    /**
     * Reserve buffer space for writing
     *
     * @param buffer Buffer handle
     * @param size Size to reserve
     * @return Buffer slice with reserved memory or null on error
     */
    fun reserveBuffer(buffer: Long, size: Long): BufferSlice? {
        val slice = McpBufferSlice.ByReference()
        val result = lib.mcp_filter_reserve_buffer(buffer, NativeLong(size), slice)
        return if (result == ResultCode.OK.value) {
            // Convert Pointer to ByteBuffer using the utility class
            val dataBuffer = BufferUtils.toByteBuffer(slice.data, slice.size)
            BufferSlice(dataBuffer, slice.size, slice.flags)
        } else {
            null
        }
    }

    /**
     * Commit written data to buffer
     *
     * @param buffer Buffer handle
     * @param bytesWritten Actual bytes written
     * @return MCP_OK on success
     */
    fun commitBuffer(buffer: Long, bytesWritten: Long): Int {
        return lib.mcp_filter_commit_buffer(buffer, NativeLong(bytesWritten))
    }

    /**
     * Create buffer handle from data
     *
     * @param data Data bytes
     * @param flags Buffer flags
     * @return Buffer handle or 0 on error
     */
    fun bufferCreate(data: ByteArray, flags: Int): Long {
        return lib.mcp_filter_buffer_create(data, NativeLong(data.size.toLong()), flags)
    }

    /**
     * Release buffer handle
     *
     * @param buffer Buffer handle
     */
    fun bufferRelease(buffer: Long) {
        lib.mcp_filter_buffer_release(buffer)
    }

    /**
     * Get buffer length
     *
     * @param buffer Buffer handle
     * @return Buffer length in bytes
     */
    fun bufferLength(buffer: Long): Long {
        val length = lib.mcp_filter_buffer_length(buffer)
        return length.toLong()
    }

    // ============================================================================
    // Client/Server Integration
    // ============================================================================

    /**
     * Send client request through filters
     *
     * @param context Client filter context
     * @param data Request data
     * @param callback Completion callback
     * @param userData User data for callback
     * @return Request ID or 0 on error
     */
    fun clientSendFiltered(
        context: FilterClientContext,
        data: ByteArray,
        callback: FilterCompletionCallback?,
        userData: Any?
    ): Long {
        val nativeContext = McpFilterClientContext.ByReference()
        // TODO: Convert context to native

        val nativeCallback: McpFilterLibrary.MCP_FILTER_COMPLETION_CB? = if (callback != null) {
            object : McpFilterLibrary.MCP_FILTER_COMPLETION_CB {
                override fun invoke(result: Int, user_data: Pointer?) {
                    callback.onComplete(result)
                }
            }
        } else {
            null
        }

        return lib.mcp_client_send_filtered(
            nativeContext, data, NativeLong(data.size.toLong()), nativeCallback, Pointer.NULL
        )
    }

    /**
     * Process server request through filters
     *
     * @param context Server filter context
     * @param requestId Request ID
     * @param requestBuffer Request buffer handle
     * @param callback Request callback
     * @param userData User data for callback
     * @return MCP_OK on success
     */
    fun serverProcessFiltered(
        context: FilterServerContext,
        requestId: Long,
        requestBuffer: Long,
        callback: FilterRequestCallback?,
        userData: Any?
    ): Int {
        val nativeContext = McpFilterServerContext.ByReference()
        // TODO: Convert context to native

        val nativeCallback: McpFilterLibrary.MCP_FILTER_REQUEST_CB? = if (callback != null) {
            object : McpFilterLibrary.MCP_FILTER_REQUEST_CB {
                override fun invoke(response_buffer: Long, result: Int, user_data: Pointer?) {
                    callback.onRequest(response_buffer, result)
                }
            }
        } else {
            null
        }

        return lib.mcp_server_process_filtered(
            nativeContext, requestId, requestBuffer, nativeCallback, Pointer.NULL
        )
    }

    // ============================================================================
    // Thread-Safe Operations
    // ============================================================================

    /**
     * Post data to filter from any thread
     *
     * @param filter Filter handle
     * @param data Data to post
     * @param callback Completion callback
     * @param userData User data for callback
     * @return MCP_OK on success
     */
    fun postData(
        filter: Long,
        data: ByteArray,
        callback: FilterPostCompletionCallback?,
        userData: Any?
    ): Int {
        val nativeCallback: McpFilterLibrary.MCP_POST_COMPLETION_CB? = if (callback != null) {
            object : McpFilterLibrary.MCP_POST_COMPLETION_CB {
                override fun invoke(result: Int, user_data: Pointer?) {
                    callback.onPostComplete(result)
                }
            }
        } else {
            null
        }

        return lib.mcp_filter_post_data(
            filter, data, NativeLong(data.size.toLong()), nativeCallback, Pointer.NULL
        )
    }

    // ============================================================================
    // Memory Management
    // ============================================================================

    /**
     * Create filter resource guard
     *
     * @param dispatcher Event dispatcher handle
     * @return Resource guard handle or 0 on error
     */
    fun guardCreate(dispatcher: Long): Long {
        val guard = lib.mcp_filter_guard_create(Pointer(dispatcher))
        return Pointer.nativeValue(guard)
    }

    /**
     * Add filter to resource guard
     *
     * @param guard Resource guard handle
     * @param filter Filter to track
     * @return MCP_OK on success
     */
    fun guardAddFilter(guard: Long, filter: Long): Int {
        return lib.mcp_filter_guard_add_filter(Pointer(guard), filter)
    }

    /**
     * Release resource guard (cleanup all tracked resources)
     *
     * @param guard Resource guard handle
     */
    fun guardRelease(guard: Long) {
        lib.mcp_filter_guard_release(Pointer(guard))
    }

    // ============================================================================
    // Buffer Pool Management
    // ============================================================================

    /**
     * Create buffer pool
     *
     * @param bufferSize Size of each buffer
     * @param maxBuffers Maximum buffers in pool
     * @return Buffer pool handle or 0 on error
     */
    fun bufferPoolCreate(bufferSize: Long, maxBuffers: Long): Long {
        val pool = lib.mcp_buffer_pool_create(NativeLong(bufferSize), NativeLong(maxBuffers))
        return Pointer.nativeValue(pool)
    }

    /**
     * Acquire buffer from pool
     *
     * @param pool Buffer pool handle
     * @return Buffer handle or 0 if pool exhausted
     */
    fun bufferPoolAcquire(pool: Long): Long {
        return lib.mcp_buffer_pool_acquire(Pointer(pool))
    }

    /**
     * Release buffer back to pool
     *
     * @param pool Buffer pool handle
     * @param buffer Buffer to release
     */
    fun bufferPoolRelease(pool: Long, buffer: Long) {
        lib.mcp_buffer_pool_release(Pointer(pool), buffer)
    }

    /**
     * Destroy buffer pool
     *
     * @param pool Buffer pool handle
     */
    fun bufferPoolDestroy(pool: Long) {
        lib.mcp_buffer_pool_destroy(Pointer(pool))
    }

    // ============================================================================
    // Statistics and Monitoring
    // ============================================================================

    /**
     * Get filter statistics
     *
     * @param filter Filter handle
     * @return Filter statistics or null on error
     */
    fun getStats(filter: Long): FilterStats? {
        val nativeStats = McpFilterStats.ByReference()
        val result = lib.mcp_filter_get_stats(filter, nativeStats)
        return if (result == ResultCode.OK.value) {
            FilterStats(
                bytesProcessed = nativeStats.bytes_processed,
                packetsProcessed = nativeStats.packets_processed,
                errors = nativeStats.errors,
                processingTimeUs = nativeStats.processing_time_us,
                throughputMbps = nativeStats.throughput_mbps
            )
        } else {
            null
        }
    }

    /**
     * Reset filter statistics
     *
     * @param filter Filter handle
     * @return MCP_OK on success
     */
    fun resetStats(filter: Long): Int {
        return lib.mcp_filter_reset_stats(filter)
    }

    // ============================================================================
    // Callback Interfaces
    // ============================================================================

    fun interface FilterDataCallback {
        fun onData(buffer: Long, endStream: Boolean): Int
    }

    fun interface FilterWriteCallback {
        fun onWrite(buffer: Long, endStream: Boolean): Int
    }

    fun interface FilterEventCallback {
        fun onEvent(state: Int): Int
    }

    fun interface FilterMetadataCallback {
        fun onMetadata(filter: Long)
    }

    fun interface FilterTrailersCallback {
        fun onTrailers(filter: Long)
    }

    fun interface FilterErrorCallback {
        fun onError(filter: Long, errorCode: Int, message: String?)
    }

    fun interface FilterCompletionCallback {
        fun onComplete(result: Int)
    }

    fun interface FilterPostCompletionCallback {
        fun onPostComplete(result: Int)
    }

    fun interface FilterRequestCallback {
        fun onRequest(responseBuffer: Long, result: Int)
    }

    // ============================================================================
    // AutoCloseable Implementation
    // ============================================================================

    override fun close() {
        filterHandle?.let { handle ->
            if (handle != 0L) {
                release(handle)
                filterHandle = null
            }
        }
    }

    // ============================================================================
    // Utility Methods
    // ============================================================================

    /**
     * Get the current filter handle
     *
     * @return Current filter handle or null if not created
     */
    fun getFilterHandle(): Long? {
        return filterHandle
    }

    /**
     * Check if filter is valid
     *
     * @return true if filter handle is valid
     */
    fun isValid(): Boolean {
        return filterHandle != null && filterHandle != 0L
    }
}