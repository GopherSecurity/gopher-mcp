package com.gopher.mcp.filter;

import com.gopher.mcp.util.BufferUtils;
import com.gopher.mcp.filter.type.buffer.BufferOwnership;
import com.gopher.mcp.filter.type.buffer.*;
import com.gopher.mcp.jna.McpFilterBufferLibrary;
import com.gopher.mcp.jna.type.filter.buffer.*;
import com.sun.jna.Native;
import com.sun.jna.NativeLong;
import com.sun.jna.Pointer;
import com.sun.jna.ptr.PointerByReference;

import java.nio.ByteBuffer;

/**
 * Java wrapper for the MCP Filter Buffer API.
 * Provides zero-copy buffer management capabilities for filters,
 * including scatter-gather I/O, memory pooling, and copy-on-write semantics.
 *
 * This class provides a clean Java API over the JNA bindings, handling all
 * conversions between Java types and native types.
 *
 * Features:
 * - Direct memory access without copying
 * - Scatter-gather I/O for fragmented buffers
 * - Buffer pooling for efficient allocation
 * - Copy-on-write semantics
 * - External memory integration
 * - TypeScript-style null/empty handling
 */
public class McpFilterBuffer implements AutoCloseable {

    private final McpFilterBufferLibrary lib;
    private Long bufferHandle;

    // ============================================================================
    // Constructors and Initialization
    // ============================================================================

    /**
     * Create a new McpFilterBuffer wrapper
     */
    public McpFilterBuffer() {
        this.lib = McpFilterBufferLibrary.INSTANCE;
    }

    /**
     * Create a wrapper for an existing buffer handle
     * @param bufferHandle Existing buffer handle
     */
    public McpFilterBuffer(long bufferHandle) {
        this.lib = McpFilterBufferLibrary.INSTANCE;
        this.bufferHandle = bufferHandle;
    }

    // ============================================================================
    // Buffer Creation and Management
    // ============================================================================

    /**
     * Create a new owned buffer
     * @param initialCapacity Initial buffer capacity
     * @param ownership Ownership model (use OWNERSHIP_* constants)
     * @return Buffer handle or 0 on error
     */
    public long createOwned(long initialCapacity, BufferOwnership ownership) {
        long handle = lib.mcp_buffer_create_owned(new NativeLong(initialCapacity), ownership.getValue());
        if (this.bufferHandle == null && handle != 0) {
            this.bufferHandle = handle;
        }
        return handle;
    }

    /**
     * Create a buffer view (zero-copy reference)
     * @param data Data bytes
     * @return Buffer handle or 0 on error
     */
    public long createView(byte[] data) {
        if (data == null || data.length == 0) {
            return createOwned(0, BufferOwnership.NONE);
        }

        Pointer dataPtr = BufferUtils.toPointer(data);
        long handle = lib.mcp_buffer_create_view(dataPtr, new NativeLong(data.length));

        if (this.bufferHandle == null && handle != 0) {
            this.bufferHandle = handle;
        }
        return handle;
    }

    /**
     * Create a buffer view from ByteBuffer
     * @param data ByteBuffer data
     * @return Buffer handle or 0 on error
     */
    public long createView(ByteBuffer data) {
        if (data == null || !data.hasRemaining()) {
            return createOwned(0, BufferOwnership.NONE);
        }

        Pointer dataPtr = BufferUtils.toPointer(data);
        long handle = lib.mcp_buffer_create_view(dataPtr, new NativeLong(data.remaining()));

        if (this.bufferHandle == null && handle != 0) {
            this.bufferHandle = handle;
        }
        return handle;
    }

    /**
     * Create buffer from external fragment
     * @param fragment External memory fragment
     * @return Buffer handle or 0 on error
     *
     * Note: The fragment's capacity field is not used by the native API.
     * Release callbacks are not currently supported - memory management is handled by Java GC.
     */
    public long createFromFragment(BufferFragment fragment) {
        if (fragment == null || fragment.getData() == null || fragment.getLength() <= 0) {
            return createOwned(0, BufferOwnership.NONE);
        }

        McpBufferFragment.ByReference nativeFragment = new McpBufferFragment.ByReference();
        nativeFragment.data = BufferUtils.toPointer(fragment.getData());
        nativeFragment.size = fragment.getLength();
        // Note: release_callback is null - memory lifecycle managed by Java GC
        nativeFragment.release_callback = null;
        // Store the data pointer as user_data to maintain reference and prevent GC
        // Note: fragment.getUserData() (Object) cannot be directly converted to Pointer
        nativeFragment.user_data = BufferUtils.toPointer(fragment.getData());

        long handle = lib.mcp_buffer_create_from_fragment(nativeFragment);

        if (this.bufferHandle == null && handle != 0) {
            this.bufferHandle = handle;
        }
        return handle;
    }

    /**
     * Clone a buffer (deep copy)
     * @param buffer Source buffer handle
     * @return Cloned buffer handle or 0 on error
     */
    public long clone(long buffer) {
        return lib.mcp_buffer_clone(buffer);
    }

    /**
     * Create copy-on-write buffer
     * @param buffer Source buffer handle
     * @return COW buffer handle or 0 on error
     */
    public long createCow(long buffer) {
        return lib.mcp_buffer_create_cow(buffer);
    }

    // ============================================================================
    // Buffer Data Operations
    // ============================================================================

    /**
     * Add data to buffer
     * @param buffer Buffer handle
     * @param data Data to add
     * @return 0 on success, error code on failure
     */
    public int add(long buffer, byte[] data) {
        if (data == null || data.length == 0) {
            return -1;
        }
        Pointer dataPtr = BufferUtils.toPointer(data);
        return lib.mcp_buffer_add(buffer, dataPtr, new NativeLong(data.length));
    }

    /**
     * Add data to buffer from ByteBuffer
     * @param buffer Buffer handle
     * @param data ByteBuffer data
     * @return 0 on success, error code on failure
     */
    public int add(long buffer, ByteBuffer data) {
        if (data == null || !data.hasRemaining()) {
            return -1;
        }
        Pointer dataPtr = BufferUtils.toPointer(data);
        return lib.mcp_buffer_add(buffer, dataPtr, new NativeLong(data.remaining()));
    }

    /**
     * Add string to buffer
     * @param buffer Buffer handle
     * @param str String to add
     * @return 0 on success, error code on failure
     */
    public int addString(long buffer, String str) {
        if (str == null || str.isEmpty()) {
            return -1;
        }
        return lib.mcp_buffer_add_string(buffer, str);
    }

    /**
     * Add another buffer to buffer
     * @param buffer Destination buffer
     * @param source Source buffer
     * @return 0 on success, error code on failure
     */
    public int addBuffer(long buffer, long source) {
        return lib.mcp_buffer_add_buffer(buffer, source);
    }

    /**
     * Add buffer fragment (zero-copy)
     * @param buffer Buffer handle
     * @param fragment Fragment to add
     * @return 0 on success, error code on failure
     *
     * Note: The fragment's capacity field is not used by the native API.
     * Release callbacks are not currently supported - memory management is handled by Java GC.
     */
    public int addFragment(long buffer, BufferFragment fragment) {
        if (fragment == null || fragment.getData() == null || fragment.getLength() <= 0) {
            return -1;
        }

        McpBufferFragment.ByReference nativeFragment = new McpBufferFragment.ByReference();
        nativeFragment.data = BufferUtils.toPointer(fragment.getData());
        nativeFragment.size = fragment.getLength();
        // Note: release_callback is null - memory lifecycle managed by Java GC
        nativeFragment.release_callback = null;
        // Store the data pointer as user_data to maintain reference and prevent GC
        // Note: fragment.getUserData() (Object) cannot be directly converted to Pointer
        nativeFragment.user_data = BufferUtils.toPointer(fragment.getData());

        return lib.mcp_buffer_add_fragment(buffer, nativeFragment);
    }

    /**
     * Prepend data to buffer
     * @param buffer Buffer handle
     * @param data Data to prepend
     * @return 0 on success, error code on failure
     */
    public int prepend(long buffer, byte[] data) {
        if (data == null || data.length == 0) {
            return -1;
        }
        Pointer dataPtr = BufferUtils.toPointer(data);
        return lib.mcp_buffer_prepend(buffer, dataPtr, new NativeLong(data.length));
    }

    // ============================================================================
    // Buffer Consumption
    // ============================================================================

    /**
     * Drain bytes from front of buffer
     * @param buffer Buffer handle
     * @param size Number of bytes to drain
     * @return 0 on success, error code on failure
     */
    public int drain(long buffer, long size) {
        if (size <= 0) {
            return 0;
        }
        return lib.mcp_buffer_drain(buffer, new NativeLong(size));
    }

    /**
     * Move data from one buffer to another
     * @param source Source buffer
     * @param destination Destination buffer
     * @param length Bytes to move (0 for all)
     * @return 0 on success, error code on failure
     */
    public int move(long source, long destination, long length) {
        return lib.mcp_buffer_move(source, destination, new NativeLong(length));
    }

    /**
     * Set drain tracker for buffer
     * @param buffer Buffer handle
     * @param tracker Drain tracker
     * @return 0 on success, error code on failure
     */
    public int setDrainTracker(long buffer, DrainTracker tracker) {
        if (tracker == null) {
            McpDrainTracker.ByReference nativeTracker = new McpDrainTracker.ByReference();
            nativeTracker.user_data = null;
            nativeTracker.callback = null;
            return lib.mcp_buffer_set_drain_tracker(buffer, nativeTracker);
        }

        // Note: Setting up callback requires additional JNA callback implementation
        // For now, just set up the structure without callback
        McpDrainTracker.ByReference nativeTracker = new McpDrainTracker.ByReference();
        nativeTracker.user_data = null;
        nativeTracker.callback = null; // Would need JNA Callback interface

        return lib.mcp_buffer_set_drain_tracker(buffer, nativeTracker);
    }

    // ============================================================================
    // Buffer Reservation (Zero-Copy Writing)
    // ============================================================================

    /**
     * Reserve space for writing
     * @param buffer Buffer handle
     * @param minSize Minimum size to reserve
     * @return BufferReservation or null on error
     */
    public BufferReservation reserve(long buffer, long minSize) {
        if (minSize <= 0) {
            return null;
        }

        McpBufferReservation.ByReference nativeReservation = new McpBufferReservation.ByReference();
        int result = lib.mcp_buffer_reserve(buffer, new NativeLong(minSize), nativeReservation);

        if (result != 0) {
            return null;
        }

        BufferReservation reservation = new BufferReservation();
        if (nativeReservation.data != null && nativeReservation.capacity > 0) {
            reservation.setData(BufferUtils.toByteBuffer(nativeReservation.data, nativeReservation.capacity));
            reservation.setCapacity(nativeReservation.capacity);
            reservation.setBuffer(nativeReservation.buffer);
            reservation.setReservationId(nativeReservation.reservation_id);
        }

        return reservation;
    }

    /**
     * Commit reserved space
     * @param reservation Reservation to commit
     * @param bytesWritten Actual bytes written
     * @return 0 on success, error code on failure
     */
    public int commitReservation(BufferReservation reservation, long bytesWritten) {
        if (reservation == null || bytesWritten < 0) {
            return -1;
        }

        McpBufferReservation.ByReference nativeRes = new McpBufferReservation.ByReference();
        nativeRes.data = BufferUtils.toPointer(reservation.getData());
        nativeRes.capacity = reservation.getCapacity();
        nativeRes.buffer = reservation.getBuffer();
        nativeRes.reservation_id = reservation.getReservationId();

        return lib.mcp_buffer_commit_reservation(nativeRes, new NativeLong(bytesWritten));
    }

    /**
     * Cancel reservation
     * @param reservation Reservation to cancel
     * @return 0 on success, error code on failure
     */
    public int cancelReservation(BufferReservation reservation) {
        if (reservation == null) {
            return -1;
        }

        McpBufferReservation.ByReference nativeRes = new McpBufferReservation.ByReference();
        nativeRes.data = BufferUtils.toPointer(reservation.getData());
        nativeRes.capacity = reservation.getCapacity();
        nativeRes.buffer = reservation.getBuffer();
        nativeRes.reservation_id = reservation.getReservationId();

        return lib.mcp_buffer_cancel_reservation(nativeRes);
    }

    // ============================================================================
    // Buffer Access (Zero-Copy Reading)
    // ============================================================================

    /**
     * Get contiguous memory view
     * @param buffer Buffer handle
     * @param offset Offset in buffer
     * @param length Requested length
     * @return ContiguousData or null on error
     */
    public ContiguousData getContiguous(long buffer, long offset, long length) {
        if (buffer == 0 || offset < 0 || length <= 0) {
            return null;
        }

        PointerByReference dataPtr = new PointerByReference();
        PointerByReference actualLengthPtr = new PointerByReference();

        int result = lib.mcp_buffer_get_contiguous(
            buffer,
            new NativeLong(offset),
            new NativeLong(length),
            dataPtr,
            actualLengthPtr
        );

        if (result != 0) {
            return null;
        }

        ContiguousData contData = new ContiguousData();
        Pointer data = dataPtr.getValue();
        long len = Pointer.nativeValue(actualLengthPtr.getValue());

        if (len > 0 && data != null) {
            contData.setData(BufferUtils.toByteBuffer(data, len));
            contData.setLength(len);
        } else {
            contData.setData(ByteBuffer.allocate(0));
            contData.setLength(0);
        }

        return contData;
    }

    /**
     * Linearize buffer (ensure contiguous memory)
     * @param buffer Buffer handle
     * @param size Size to linearize
     * @return Linearized ByteBuffer or null on error
     */
    public ByteBuffer linearize(long buffer, long size) {
        if (size <= 0) {
            return ByteBuffer.allocate(0);
        }

        PointerByReference dataPtr = new PointerByReference();
        int result = lib.mcp_buffer_linearize(buffer, new NativeLong(size), dataPtr);

        if (result != 0 || dataPtr.getValue() == null) {
            return null;
        }

        return BufferUtils.toByteBuffer(dataPtr.getValue(), size);
    }

    /**
     * Peek at buffer data without consuming
     * @param buffer Buffer handle
     * @param offset Offset to peek at
     * @param length Length to peek
     * @return Peeked data or null on error
     */
    public byte[] peek(long buffer, long offset, int length) {
        if (length <= 0) {
            return new byte[0];
        }

        byte[] data = new byte[length];
        ByteBuffer tempBuffer = ByteBuffer.allocateDirect(length);

        int result = lib.mcp_buffer_peek(
            buffer,
            new NativeLong(offset),
            Native.getDirectBufferPointer(tempBuffer),
            new NativeLong(length)
        );

        if (result != 0) {
            return null;
        }

        tempBuffer.get(data);
        return data;
    }

    // ============================================================================
    // Type-Safe I/O Operations
    // ============================================================================

    /**
     * Write integer with little-endian byte order
     * @param buffer Buffer handle
     * @param value Value to write
     * @param size Size in bytes (1, 2, 4, 8)
     * @return 0 on success, error code on failure
     */
    public int writeLeInt(long buffer, long value, int size) {
        return lib.mcp_buffer_write_le_int(buffer, value, new NativeLong(size));
    }

    /**
     * Write integer with big-endian byte order
     * @param buffer Buffer handle
     * @param value Value to write
     * @param size Size in bytes (1, 2, 4, 8)
     * @return 0 on success, error code on failure
     */
    public int writeBeInt(long buffer, long value, int size) {
        return lib.mcp_buffer_write_be_int(buffer, value, new NativeLong(size));
    }

    /**
     * Read integer with little-endian byte order
     * @param buffer Buffer handle
     * @param size Size in bytes (1, 2, 4, 8)
     * @return Read value or null on error
     */
    public Long readLeInt(long buffer, int size) {
        PointerByReference valuePtr = new PointerByReference();
        int result = lib.mcp_buffer_read_le_int(buffer, new NativeLong(size), valuePtr);

        if (result != 0) {
            return null;
        }

        return Pointer.nativeValue(valuePtr.getValue());
    }

    /**
     * Read integer with big-endian byte order
     * @param buffer Buffer handle
     * @param size Size in bytes (1, 2, 4, 8)
     * @return Read value or null on error
     */
    public Long readBeInt(long buffer, int size) {
        PointerByReference valuePtr = new PointerByReference();
        int result = lib.mcp_buffer_read_be_int(buffer, new NativeLong(size), valuePtr);

        if (result != 0) {
            return null;
        }

        return Pointer.nativeValue(valuePtr.getValue());
    }

    // ============================================================================
    // Buffer Search Operations
    // ============================================================================

    /**
     * Search for pattern in buffer
     * @param buffer Buffer handle
     * @param pattern Pattern to search for
     * @param startPosition Start position for search
     * @return Position where found or -1 if not found
     */
    public long search(long buffer, byte[] pattern, long startPosition) {
        if (pattern == null || pattern.length == 0) {
            return -1;
        }

        PointerByReference positionPtr = new PointerByReference();
        Pointer patternPtr = BufferUtils.toPointer(pattern);

        int result = lib.mcp_buffer_search(
            buffer,
            patternPtr,
            new NativeLong(pattern.length),
            new NativeLong(startPosition),
            positionPtr
        );

        if (result != 0) {
            return -1;
        }

        return Pointer.nativeValue(positionPtr.getValue());
    }

    /**
     * Find delimiter in buffer
     * @param buffer Buffer handle
     * @param delimiter Delimiter character
     * @return Position where found or -1 if not found
     */
    public long findByte(long buffer, byte delimiter) {
        PointerByReference positionPtr = new PointerByReference();
        int result = lib.mcp_buffer_find_byte(buffer, delimiter, positionPtr);

        if (result != 0) {
            return -1;
        }

        return Pointer.nativeValue(positionPtr.getValue());
    }

    // ============================================================================
    // Buffer Information
    // ============================================================================

    /**
     * Get buffer length
     * @param buffer Buffer handle
     * @return Buffer length in bytes
     */
    public long length(long buffer) {
        return lib.mcp_buffer_length(buffer).longValue();
    }

    /**
     * Get buffer capacity
     * @param buffer Buffer handle
     * @return Buffer capacity in bytes
     */
    public long capacity(long buffer) {
        return lib.mcp_buffer_capacity(buffer).longValue();
    }

    /**
     * Check if buffer is empty
     * @param buffer Buffer handle
     * @return true if empty
     */
    public boolean isEmpty(long buffer) {
        return lib.mcp_buffer_is_empty(buffer) != 0;
    }

    /**
     * Get buffer statistics
     * @param buffer Buffer handle
     * @return BufferStats or null on error
     */
    public BufferStats getStats(long buffer) {
        McpBufferStats.ByReference nativeStats = new McpBufferStats.ByReference();
        int result = lib.mcp_buffer_get_stats(buffer, nativeStats);

        if (result != 0) {
            return null;
        }

        BufferStats stats = new BufferStats();
        stats.setTotalBytes(nativeStats.total_bytes);
        stats.setUsedBytes(nativeStats.used_bytes);
        stats.setSliceCount(nativeStats.slice_count);
        stats.setFragmentCount(nativeStats.fragment_count);
        stats.setReadOperations(nativeStats.read_operations);
        stats.setWriteOperations(nativeStats.write_operations);

        return stats;
    }

    // ============================================================================
    // Buffer Watermarks
    // ============================================================================

    /**
     * Set buffer watermarks for flow control
     * @param buffer Buffer handle
     * @param lowWatermark Low watermark bytes
     * @param highWatermark High watermark bytes
     * @param overflowWatermark Overflow watermark bytes
     * @return 0 on success, error code on failure
     */
    public int setWatermarks(long buffer, long lowWatermark, long highWatermark, long overflowWatermark) {
        return lib.mcp_buffer_set_watermarks(
            buffer,
            new NativeLong(lowWatermark),
            new NativeLong(highWatermark),
            new NativeLong(overflowWatermark)
        );
    }

    /**
     * Check if buffer is above high watermark
     * @param buffer Buffer handle
     * @return true if above high watermark
     */
    public boolean aboveHighWatermark(long buffer) {
        return lib.mcp_buffer_above_high_watermark(buffer) != 0;
    }

    /**
     * Check if buffer is below low watermark
     * @param buffer Buffer handle
     * @return true if below low watermark
     */
    public boolean belowLowWatermark(long buffer) {
        return lib.mcp_buffer_below_low_watermark(buffer) != 0;
    }

    // ============================================================================
    // Advanced Buffer Pool
    // ============================================================================

    /**
     * Create buffer pool with configuration
     * @param config Pool configuration
     * @return Buffer pool handle or null on error
     */
    public Pointer createPoolEx(BufferPoolConfig config) {
        if (config == null) {
            return null;
        }

        McpBufferPoolConfig.ByReference nativeConfig = new McpBufferPoolConfig.ByReference();
        nativeConfig.buffer_size = config.getBufferSize();
        nativeConfig.max_buffers = config.getMaxCount();
        nativeConfig.prealloc_count = config.getInitialCount();
        nativeConfig.use_thread_local = (byte) 0;
        nativeConfig.zero_on_alloc = (byte) 0;

        return lib.mcp_buffer_pool_create_ex(nativeConfig);
    }

    /**
     * Get pool statistics
     * @param pool Buffer pool
     * @return PoolStats or null on error
     */
    public PoolStats getPoolStats(Pointer pool) {
        if (pool == null) {
            return null;
        }

        PointerByReference freeCountPtr = new PointerByReference();
        PointerByReference usedCountPtr = new PointerByReference();
        PointerByReference totalAllocatedPtr = new PointerByReference();

        int result = lib.mcp_buffer_pool_get_stats(pool, freeCountPtr, usedCountPtr, totalAllocatedPtr);

        if (result != 0) {
            return null;
        }

        PoolStats stats = new PoolStats();
        stats.setFreeCount(Pointer.nativeValue(freeCountPtr.getValue()));
        stats.setUsedCount(Pointer.nativeValue(usedCountPtr.getValue()));
        stats.setTotalAllocated(Pointer.nativeValue(totalAllocatedPtr.getValue()));

        return stats;
    }

    /**
     * Trim pool to reduce memory usage
     * @param pool Buffer pool
     * @param targetFree Target number of free buffers
     * @return 0 on success, error code on failure
     */
    public int trimPool(Pointer pool, long targetFree) {
        if (pool == null) {
            return -1;
        }
        return lib.mcp_buffer_pool_trim(pool, new NativeLong(targetFree));
    }

    // ============================================================================
    // Helper Methods
    // ============================================================================

    /**
     * Get the current buffer handle
     * @return Current buffer handle or null
     */
    public Long getBufferHandle() {
        return bufferHandle;
    }

    /**
     * Set the current buffer handle
     * @param bufferHandle Buffer handle to set
     */
    public void setBufferHandle(Long bufferHandle) {
        this.bufferHandle = bufferHandle;
    }

    /**
     * Close and release resources
     */
    @Override
    public void close() {
        // Note: Buffer cleanup would typically be done through a destroy method
        // which would need to be added to the native API
        bufferHandle = null;
    }

    // ============================================================================
    // Convenience Methods
    // ============================================================================

    /**
     * Create a buffer with data (TypeScript-style helper)
     * @param data Initial data
     * @param ownership Ownership model
     * @return Buffer handle or 0 on error
     */
    public long createWithData(byte[] data, BufferOwnership ownership) {
        if (data == null || data.length == 0) {
            return createOwned(0, BufferOwnership.NONE);
        }

        long handle = createOwned(data.length, ownership);
        if (handle != 0) {
            add(handle, data);
        }
        return handle;
    }

    /**
     * Create a buffer with string data
     * @param str Initial string
     * @param ownership Ownership model
     * @return Buffer handle or 0 on error
     */
    public long createWithString(String str, BufferOwnership ownership) {
        if (str == null || str.isEmpty()) {
            return createOwned(0, BufferOwnership.NONE);
        }

        long handle = createOwned(str.length(), ownership);
        if (handle != 0) {
            addString(handle, str);
        }
        return handle;
    }
}
