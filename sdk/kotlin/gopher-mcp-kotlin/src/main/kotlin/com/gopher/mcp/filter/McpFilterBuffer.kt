package com.gopher.mcp.filter

import com.gopher.mcp.filter.type.buffer.*
import com.gopher.mcp.jna.McpFilterBufferLibrary
import com.gopher.mcp.jna.type.filter.buffer.*
import com.gopher.mcp.util.BufferUtils
import com.sun.jna.Native
import com.sun.jna.NativeLong
import com.sun.jna.Pointer
import com.sun.jna.ptr.PointerByReference
import java.nio.ByteBuffer

/**
 * Kotlin wrapper for the MCP Filter Buffer API. Provides zero-copy buffer management capabilities for
 * filters, including scatter-gather I/O, memory pooling, and copy-on-write semantics.
 *
 * This class provides a clean Kotlin API over the JNA bindings, handling all conversions between
 * Kotlin types and native types.
 *
 * Features: - Direct memory access without copying - Scatter-gather I/O for fragmented buffers -
 * Buffer pooling for efficient allocation - Copy-on-write semantics - External memory integration -
 * TypeScript-style null/empty handling
 */
class McpFilterBuffer : AutoCloseable {

    private val lib: McpFilterBufferLibrary = McpFilterBufferLibrary.INSTANCE
    private var bufferHandle: Long? = null

    // ============================================================================
    // Constructors and Initialization
    // ============================================================================

    /** Create a new McpFilterBuffer wrapper */
    constructor()

    /**
     * Create a wrapper for an existing buffer handle
     *
     * @param bufferHandle Existing buffer handle
     */
    constructor(bufferHandle: Long) {
        this.bufferHandle = bufferHandle
    }

    // ============================================================================
    // Buffer Creation and Management
    // ============================================================================

    /**
     * Create a new owned buffer
     *
     * @param initialCapacity Initial buffer capacity
     * @param ownership Ownership model (use OWNERSHIP_* constants)
     * @return Buffer handle or 0 on error
     */
    fun createOwned(initialCapacity: Long, ownership: BufferOwnership): Long {
        val handle = lib.mcp_buffer_create_owned(NativeLong(initialCapacity), ownership.getValue())
        if (bufferHandle == null && handle != 0L) {
            bufferHandle = handle
        }
        return handle
    }

    /**
     * Create a buffer view (zero-copy reference)
     *
     * @param data Data bytes
     * @return Buffer handle or 0 on error
     */
    fun createView(data: ByteArray?): Long {
        if (data == null || data.isEmpty()) {
            return createOwned(0, BufferOwnership.NONE)
        }

        val dataPtr = BufferUtils.toPointer(data)
        val handle = lib.mcp_buffer_create_view(dataPtr, NativeLong(data.size.toLong()))

        if (bufferHandle == null && handle != 0L) {
            bufferHandle = handle
        }
        return handle
    }

    /**
     * Create a buffer view from ByteBuffer
     *
     * @param data ByteBuffer data
     * @return Buffer handle or 0 on error
     */
    fun createView(data: ByteBuffer?): Long {
        if (data == null || !data.hasRemaining()) {
            return createOwned(0, BufferOwnership.NONE)
        }

        val dataPtr = BufferUtils.toPointer(data)
        val handle = lib.mcp_buffer_create_view(dataPtr, NativeLong(data.remaining().toLong()))

        if (bufferHandle == null && handle != 0L) {
            bufferHandle = handle
        }
        return handle
    }

    /**
     * Create buffer from external fragment
     *
     * @param fragment External memory fragment
     * @return Buffer handle or 0 on error
     *
     * Note: The fragment's capacity field is not used by the native API. Release callbacks are
     * not currently supported - memory management is handled by Kotlin GC.
     */
    fun createFromFragment(fragment: BufferFragment?): Long {
        if (fragment?.data == null || fragment.length <= 0) {
            return createOwned(0, BufferOwnership.NONE)
        }

        val nativeFragment = McpBufferFragment.ByReference().apply {
            this.data = BufferUtils.toPointer(fragment.data)
            this.size = fragment.length
            // Note: release_callback is null - memory lifecycle managed by Kotlin GC
            this.release_callback = null
            // Store the data pointer as user_data to maintain reference and prevent GC
            // Note: fragment.userData (Any?) cannot be directly converted to Pointer
            this.user_data = BufferUtils.toPointer(fragment.data)
        }

        val handle = lib.mcp_buffer_create_from_fragment(nativeFragment)

        if (bufferHandle == null && handle != 0L) {
            bufferHandle = handle
        }
        return handle
    }

    /**
     * Clone a buffer (deep copy)
     *
     * @param buffer Source buffer handle
     * @return Cloned buffer handle or 0 on error
     */
    fun clone(buffer: Long): Long {
        return lib.mcp_buffer_clone(buffer)
    }

    /**
     * Create copy-on-write buffer
     *
     * @param buffer Source buffer handle
     * @return COW buffer handle or 0 on error
     */
    fun createCow(buffer: Long): Long {
        return lib.mcp_buffer_create_cow(buffer)
    }

    // ============================================================================
    // Buffer Data Operations
    // ============================================================================

    /**
     * Add data to buffer
     *
     * @param buffer Buffer handle
     * @param data Data to add
     * @return 0 on success, error code on failure
     */
    fun add(buffer: Long, data: ByteArray?): Int {
        if (data == null || data.isEmpty()) {
            return -1
        }
        val dataPtr = BufferUtils.toPointer(data)
        return lib.mcp_buffer_add(buffer, dataPtr, NativeLong(data.size.toLong()))
    }

    /**
     * Add data to buffer from ByteBuffer
     *
     * @param buffer Buffer handle
     * @param data ByteBuffer data
     * @return 0 on success, error code on failure
     */
    fun add(buffer: Long, data: ByteBuffer?): Int {
        if (data == null || !data.hasRemaining()) {
            return -1
        }
        val dataPtr = BufferUtils.toPointer(data)
        return lib.mcp_buffer_add(buffer, dataPtr, NativeLong(data.remaining().toLong()))
    }

    /**
     * Add string to buffer
     *
     * @param buffer Buffer handle
     * @param str String to add
     * @return 0 on success, error code on failure
     */
    fun addString(buffer: Long, str: String?): Int {
        if (str == null || str.isEmpty()) {
            return -1
        }
        return lib.mcp_buffer_add_string(buffer, str)
    }

    /**
     * Add another buffer to buffer
     *
     * @param buffer Destination buffer
     * @param source Source buffer
     * @return 0 on success, error code on failure
     */
    fun addBuffer(buffer: Long, source: Long): Int {
        return lib.mcp_buffer_add_buffer(buffer, source)
    }

    /**
     * Add buffer fragment (zero-copy)
     *
     * @param buffer Buffer handle
     * @param fragment Fragment to add
     * @return 0 on success, error code on failure
     *
     * Note: The fragment's capacity field is not used by the native API. Release callbacks are
     * not currently supported - memory management is handled by Kotlin GC.
     */
    fun addFragment(buffer: Long, fragment: BufferFragment?): Int {
        if (fragment?.data == null || fragment.length <= 0) {
            return -1
        }

        val nativeFragment = McpBufferFragment.ByReference().apply {
            this.data = BufferUtils.toPointer(fragment.data)
            this.size = fragment.length
            // Note: release_callback is null - memory lifecycle managed by Kotlin GC
            this.release_callback = null
            // Store the data pointer as user_data to maintain reference and prevent GC
            // Note: fragment.userData (Any?) cannot be directly converted to Pointer
            this.user_data = BufferUtils.toPointer(fragment.data)
        }

        return lib.mcp_buffer_add_fragment(buffer, nativeFragment)
    }

    /**
     * Prepend data to buffer
     *
     * @param buffer Buffer handle
     * @param data Data to prepend
     * @return 0 on success, error code on failure
     */
    fun prepend(buffer: Long, data: ByteArray?): Int {
        if (data == null || data.isEmpty()) {
            return -1
        }
        val dataPtr = BufferUtils.toPointer(data)
        return lib.mcp_buffer_prepend(buffer, dataPtr, NativeLong(data.size.toLong()))
    }

    // ============================================================================
    // Buffer Consumption
    // ============================================================================

    /**
     * Drain bytes from front of buffer
     *
     * @param buffer Buffer handle
     * @param size Number of bytes to drain
     * @return 0 on success, error code on failure
     */
    fun drain(buffer: Long, size: Long): Int {
        if (size <= 0) {
            return 0
        }
        return lib.mcp_buffer_drain(buffer, NativeLong(size))
    }

    /**
     * Move data from one buffer to another
     *
     * @param source Source buffer
     * @param destination Destination buffer
     * @param length Bytes to move (0 for all)
     * @return 0 on success, error code on failure
     */
    fun move(source: Long, destination: Long, length: Long): Int {
        return lib.mcp_buffer_move(source, destination, NativeLong(length))
    }

    /**
     * Set drain tracker for buffer
     *
     * @param buffer Buffer handle
     * @param tracker Drain tracker
     * @return 0 on success, error code on failure
     */
    fun setDrainTracker(buffer: Long, tracker: DrainTracker?): Int {
        val nativeTracker = McpDrainTracker.ByReference().apply {
            user_data = null
            callback = null
        }

        if (tracker != null) {
            // Note: Setting up callback requires additional JNA callback implementation
            // For now, just set up the structure without callback
            // Would need JNA Callback interface
        }

        return lib.mcp_buffer_set_drain_tracker(buffer, nativeTracker)
    }

    // ============================================================================
    // Buffer Reservation (Zero-Copy Writing)
    // ============================================================================

    /**
     * Reserve space for writing
     *
     * @param buffer Buffer handle
     * @param minSize Minimum size to reserve
     * @return BufferReservation or null on error
     */
    fun reserve(buffer: Long, minSize: Long): BufferReservation? {
        if (minSize <= 0) {
            return null
        }

        val nativeReservation = McpBufferReservation.ByReference()
        val result = lib.mcp_buffer_reserve(buffer, NativeLong(minSize), nativeReservation)

        if (result != 0) {
            return null
        }

        val reservation = BufferReservation()
        if (nativeReservation.data != null && nativeReservation.capacity > 0) {
            reservation.data = BufferUtils.toByteBuffer(nativeReservation.data, nativeReservation.capacity)
            reservation.capacity = nativeReservation.capacity
            reservation.buffer = nativeReservation.buffer
            reservation.reservationId = nativeReservation.reservation_id
        }

        return reservation
    }

    /**
     * Commit reserved space
     *
     * @param reservation Reservation to commit
     * @param bytesWritten Actual bytes written
     * @return 0 on success, error code on failure
     */
    fun commitReservation(reservation: BufferReservation?, bytesWritten: Long): Int {
        if (reservation == null || bytesWritten < 0) {
            return -1
        }

        val nativeRes = McpBufferReservation.ByReference().apply {
            this.data = BufferUtils.toPointer(reservation.data)
            this.capacity = reservation.capacity
            this.buffer = reservation.buffer
            this.reservation_id = reservation.reservationId
        }

        return lib.mcp_buffer_commit_reservation(nativeRes, NativeLong(bytesWritten))
    }

    /**
     * Cancel reservation
     *
     * @param reservation Reservation to cancel
     * @return 0 on success, error code on failure
     */
    fun cancelReservation(reservation: BufferReservation?): Int {
        if (reservation == null) {
            return -1
        }

        val nativeRes = McpBufferReservation.ByReference().apply {
            this.data = BufferUtils.toPointer(reservation.data)
            this.capacity = reservation.capacity
            this.buffer = reservation.buffer
            this.reservation_id = reservation.reservationId
        }

        return lib.mcp_buffer_cancel_reservation(nativeRes)
    }

    // ============================================================================
    // Buffer Access (Zero-Copy Reading)
    // ============================================================================

    /**
     * Get contiguous memory view
     *
     * @param buffer Buffer handle
     * @param offset Offset in buffer
     * @param length Requested length
     * @return ContiguousData or null on error
     */
    fun getContiguous(buffer: Long, offset: Long, length: Long): ContiguousData? {
        if (buffer == 0L || offset < 0 || length <= 0) {
            return null
        }

        val dataPtr = PointerByReference()
        val actualLengthPtr = PointerByReference()

        val result = lib.mcp_buffer_get_contiguous(
            buffer, NativeLong(offset), NativeLong(length), dataPtr, actualLengthPtr
        )

        if (result != 0) {
            return null
        }

        val contData = ContiguousData()
        val data = dataPtr.value
        val len = Pointer.nativeValue(actualLengthPtr.value)

        if (len > 0 && data != null) {
            contData.data = BufferUtils.toByteBuffer(data, len)
            contData.length = len
        } else {
            contData.data = ByteBuffer.allocate(0)
            contData.length = 0
        }

        return contData
    }

    /**
     * Linearize buffer (ensure contiguous memory)
     *
     * @param buffer Buffer handle
     * @param size Size to linearize
     * @return Linearized ByteBuffer or null on error
     */
    fun linearize(buffer: Long, size: Long): ByteBuffer? {
        if (size <= 0) {
            return ByteBuffer.allocate(0)
        }

        val dataPtr = PointerByReference()
        val result = lib.mcp_buffer_linearize(buffer, NativeLong(size), dataPtr)

        if (result != 0 || dataPtr.value == null) {
            return null
        }

        return BufferUtils.toByteBuffer(dataPtr.value, size)
    }

    /**
     * Peek at buffer data without consuming
     *
     * @param buffer Buffer handle
     * @param offset Offset to peek at
     * @param length Length to peek
     * @return Peeked data or null on error
     */
    fun peek(buffer: Long, offset: Long, length: Int): ByteArray? {
        if (length <= 0) {
            return ByteArray(0)
        }

        val data = ByteArray(length)
        val tempBuffer = ByteBuffer.allocateDirect(length)

        val result = lib.mcp_buffer_peek(
            buffer,
            NativeLong(offset),
            Native.getDirectBufferPointer(tempBuffer),
            NativeLong(length.toLong())
        )

        if (result != 0) {
            return null
        }

        tempBuffer.get(data)
        return data
    }

    // ============================================================================
    // Type-Safe I/O Operations
    // ============================================================================

    /**
     * Write integer with little-endian byte order
     *
     * @param buffer Buffer handle
     * @param value Value to write
     * @param size Size in bytes (1, 2, 4, 8)
     * @return 0 on success, error code on failure
     */
    fun writeLeInt(buffer: Long, value: Long, size: Int): Int {
        return lib.mcp_buffer_write_le_int(buffer, value, NativeLong(size.toLong()))
    }

    /**
     * Write integer with big-endian byte order
     *
     * @param buffer Buffer handle
     * @param value Value to write
     * @param size Size in bytes (1, 2, 4, 8)
     * @return 0 on success, error code on failure
     */
    fun writeBeInt(buffer: Long, value: Long, size: Int): Int {
        return lib.mcp_buffer_write_be_int(buffer, value, NativeLong(size.toLong()))
    }

    /**
     * Read integer with little-endian byte order
     *
     * @param buffer Buffer handle
     * @param size Size in bytes (1, 2, 4, 8)
     * @return Read value or null on error
     */
    fun readLeInt(buffer: Long, size: Int): Long? {
        val valuePtr = PointerByReference()
        val result = lib.mcp_buffer_read_le_int(buffer, NativeLong(size.toLong()), valuePtr)

        if (result != 0) {
            return null
        }

        return Pointer.nativeValue(valuePtr.value)
    }

    /**
     * Read integer with big-endian byte order
     *
     * @param buffer Buffer handle
     * @param size Size in bytes (1, 2, 4, 8)
     * @return Read value or null on error
     */
    fun readBeInt(buffer: Long, size: Int): Long? {
        val valuePtr = PointerByReference()
        val result = lib.mcp_buffer_read_be_int(buffer, NativeLong(size.toLong()), valuePtr)

        if (result != 0) {
            return null
        }

        return Pointer.nativeValue(valuePtr.value)
    }

    // ============================================================================
    // Buffer Search Operations
    // ============================================================================

    /**
     * Search for pattern in buffer
     *
     * @param buffer Buffer handle
     * @param pattern Pattern to search for
     * @param startPosition Start position for search
     * @return Position where found or -1 if not found
     */
    fun search(buffer: Long, pattern: ByteArray?, startPosition: Long): Long {
        if (pattern == null || pattern.isEmpty()) {
            return -1
        }

        val positionPtr = PointerByReference()
        val patternPtr = BufferUtils.toPointer(pattern)

        val result = lib.mcp_buffer_search(
            buffer,
            patternPtr,
            NativeLong(pattern.size.toLong()),
            NativeLong(startPosition),
            positionPtr
        )

        if (result != 0) {
            return -1
        }

        return Pointer.nativeValue(positionPtr.value)
    }

    /**
     * Find delimiter in buffer
     *
     * @param buffer Buffer handle
     * @param delimiter Delimiter character
     * @return Position where found or -1 if not found
     */
    fun findByte(buffer: Long, delimiter: Byte): Long {
        val positionPtr = PointerByReference()
        val result = lib.mcp_buffer_find_byte(buffer, delimiter, positionPtr)

        if (result != 0) {
            return -1
        }

        return Pointer.nativeValue(positionPtr.value)
    }

    // ============================================================================
    // Buffer Information
    // ============================================================================

    /**
     * Get buffer length
     *
     * @param buffer Buffer handle
     * @return Buffer length in bytes
     */
    fun length(buffer: Long): Long {
        return lib.mcp_buffer_length(buffer).toLong()
    }

    /**
     * Get buffer capacity
     *
     * @param buffer Buffer handle
     * @return Buffer capacity in bytes
     */
    fun capacity(buffer: Long): Long {
        return lib.mcp_buffer_capacity(buffer).toLong()
    }

    /**
     * Check if buffer is empty
     *
     * @param buffer Buffer handle
     * @return true if empty
     */
    fun isEmpty(buffer: Long): Boolean {
        return lib.mcp_buffer_is_empty(buffer).toInt() != 0
    }

    /**
     * Get buffer statistics
     *
     * @param buffer Buffer handle
     * @return BufferStats or null on error
     */
    fun getStats(buffer: Long): BufferStats? {
        val nativeStats = McpBufferStats.ByReference()
        val result = lib.mcp_buffer_get_stats(buffer, nativeStats)

        if (result != 0) {
            return null
        }

        return BufferStats().apply {
            totalBytes = nativeStats.total_bytes
            usedBytes = nativeStats.used_bytes
            sliceCount = nativeStats.slice_count
            fragmentCount = nativeStats.fragment_count
            readOperations = nativeStats.read_operations
            writeOperations = nativeStats.write_operations
        }
    }

    // ============================================================================
    // Buffer Watermarks
    // ============================================================================

    /**
     * Set buffer watermarks for flow control
     *
     * @param buffer Buffer handle
     * @param lowWatermark Low watermark bytes
     * @param highWatermark High watermark bytes
     * @param overflowWatermark Overflow watermark bytes
     * @return 0 on success, error code on failure
     */
    fun setWatermarks(
        buffer: Long,
        lowWatermark: Long,
        highWatermark: Long,
        overflowWatermark: Long
    ): Int {
        return lib.mcp_buffer_set_watermarks(
            buffer,
            NativeLong(lowWatermark),
            NativeLong(highWatermark),
            NativeLong(overflowWatermark)
        )
    }

    /**
     * Check if buffer is above high watermark
     *
     * @param buffer Buffer handle
     * @return true if above high watermark
     */
    fun aboveHighWatermark(buffer: Long): Boolean {
        return lib.mcp_buffer_above_high_watermark(buffer).toInt() != 0
    }

    /**
     * Check if buffer is below low watermark
     *
     * @param buffer Buffer handle
     * @return true if below low watermark
     */
    fun belowLowWatermark(buffer: Long): Boolean {
        return lib.mcp_buffer_below_low_watermark(buffer).toInt() != 0
    }

    // ============================================================================
    // Advanced Buffer Pool
    // ============================================================================

    /**
     * Create buffer pool with configuration
     *
     * @param config Pool configuration
     * @return Buffer pool handle or null on error
     */
    fun createPoolEx(config: BufferPoolConfig?): Pointer? {
        if (config == null) {
            return null
        }

        val nativeConfig = McpBufferPoolConfig.ByReference().apply {
            buffer_size = config.bufferSize
            max_buffers = config.maxCount
            prealloc_count = config.initialCount
            use_thread_local = 0.toByte()
            zero_on_alloc = 0.toByte()
        }

        return lib.mcp_buffer_pool_create_ex(nativeConfig)
    }

    /**
     * Get pool statistics
     *
     * @param pool Buffer pool
     * @return PoolStats or null on error
     */
    fun getPoolStats(pool: Pointer?): PoolStats? {
        if (pool == null) {
            return null
        }

        val freeCountPtr = PointerByReference()
        val usedCountPtr = PointerByReference()
        val totalAllocatedPtr = PointerByReference()

        val result = lib.mcp_buffer_pool_get_stats(pool, freeCountPtr, usedCountPtr, totalAllocatedPtr)

        if (result != 0) {
            return null
        }

        return PoolStats().apply {
            freeCount = Pointer.nativeValue(freeCountPtr.value)
            usedCount = Pointer.nativeValue(usedCountPtr.value)
            totalAllocated = Pointer.nativeValue(totalAllocatedPtr.value)
        }
    }

    /**
     * Trim pool to reduce memory usage
     *
     * @param pool Buffer pool
     * @param targetFree Target number of free buffers
     * @return 0 on success, error code on failure
     */
    fun trimPool(pool: Pointer?, targetFree: Long): Int {
        if (pool == null) {
            return -1
        }
        return lib.mcp_buffer_pool_trim(pool, NativeLong(targetFree))
    }

    // ============================================================================
    // Helper Methods
    // ============================================================================

    /**
     * Get the current buffer handle
     *
     * @return Current buffer handle or null
     */
    fun getBufferHandle(): Long? {
        return bufferHandle
    }

    /**
     * Set the current buffer handle
     *
     * @param bufferHandle Buffer handle to set
     */
    fun setBufferHandle(bufferHandle: Long?) {
        this.bufferHandle = bufferHandle
    }

    /** Close and release resources */
    override fun close() {
        // Note: Buffer cleanup would typically be done through a destroy method
        // which would need to be added to the native API
        bufferHandle = null
    }

    // ============================================================================
    // Convenience Methods
    // ============================================================================

    /**
     * Create a buffer with data (TypeScript-style helper)
     *
     * @param data Initial data
     * @param ownership Ownership model
     * @return Buffer handle or 0 on error
     */
    fun createWithData(data: ByteArray?, ownership: BufferOwnership): Long {
        if (data == null || data.isEmpty()) {
            return createOwned(0, BufferOwnership.NONE)
        }

        val handle = createOwned(data.size.toLong(), ownership)
        if (handle != 0L) {
            add(handle, data)
        }
        return handle
    }

    /**
     * Create a buffer with string data
     *
     * @param str Initial string
     * @param ownership Ownership model
     * @return Buffer handle or 0 on error
     */
    fun createWithString(str: String?, ownership: BufferOwnership): Long {
        if (str == null || str.isEmpty()) {
            return createOwned(0, BufferOwnership.NONE)
        }

        val handle = createOwned(str.length.toLong(), ownership)
        if (handle != 0L) {
            addString(handle, str)
        }
        return handle
    }
}