package com.gopher.mcp.jna

import com.gopher.mcp.jna.type.filter.buffer.*
import com.sun.jna.Library
import com.sun.jna.NativeLong
import com.sun.jna.Pointer
import com.sun.jna.ptr.PointerByReference

/**
 * JNA interface for the MCP Filter Buffer API (mcp_c_filter_buffer.h).
 * This interface provides zero-copy buffer management capabilities for filters,
 * including scatter-gather I/O, memory pooling, and copy-on-write semantics.
 *
 * Features:
 * - Direct memory access without copying
 * - Scatter-gather I/O for fragmented buffers
 * - Buffer pooling for efficient allocation
 * - Copy-on-write semantics
 * - External memory integration
 *
 * All methods are ordered exactly as they appear in mcp_c_filter_buffer.h
 */
interface McpFilterBufferLibrary : Library {

    companion object {
        // Load the native library
        @JvmStatic
        val INSTANCE: McpFilterBufferLibrary = NativeLibraryLoader.loadLibrary(McpFilterBufferLibrary::class.java)

        /* ============================================================================
         * Buffer Types and Enumerations (from mcp_c_filter_buffer.h lines 33-73)
         * ============================================================================
         */

        // Buffer ownership model
        const val MCP_BUFFER_OWNERSHIP_NONE = 0 // No ownership (view only)
        const val MCP_BUFFER_OWNERSHIP_SHARED = 1 // Shared ownership (ref counted)
        const val MCP_BUFFER_OWNERSHIP_EXCLUSIVE = 2 // Exclusive ownership
        const val MCP_BUFFER_OWNERSHIP_EXTERNAL = 3 // External ownership (callback)
    }

    /* ============================================================================
     * Buffer Creation and Management (lines 78-120)
     * ============================================================================
     */

    /**
     * Create a new buffer (line 85)
     *
     * @param initial_capacity Initial buffer capacity
     * @param ownership Ownership model
     * @return Buffer handle or 0 on error
     */
    fun mcp_buffer_create_owned(initial_capacity: NativeLong, ownership: Int): Long

    /**
     * Create a buffer view (zero-copy reference) (line 94)
     *
     * @param data Data pointer
     * @param length Data length
     * @return Buffer handle or 0 on error
     */
    fun mcp_buffer_create_view(data: Pointer?, length: NativeLong): Long

    /**
     * Create buffer from external fragment (line 102)
     *
     * @param fragment External memory fragment
     * @return Buffer handle or 0 on error
     */
    fun mcp_buffer_create_from_fragment(fragment: McpBufferFragment.ByReference?): Long

    /**
     * Clone a buffer (deep copy) (line 110)
     *
     * @param buffer Source buffer
     * @return Cloned buffer handle or 0 on error
     */
    fun mcp_buffer_clone(buffer: Long): Long

    /**
     * Create copy-on-write buffer (line 118)
     *
     * @param buffer Source buffer
     * @return COW buffer handle or 0 on error
     */
    fun mcp_buffer_create_cow(buffer: Long): Long

    /* ============================================================================
     * Buffer Data Operations (lines 124-175)
     * ============================================================================
     */

    /**
     * Add data to buffer (line 133)
     *
     * @param buffer Buffer handle
     * @param data Data to add
     * @param length Data length
     * @return MCP_OK on success
     */
    fun mcp_buffer_add(buffer: Long, data: Pointer?, length: NativeLong): Int

    /**
     * Add string to buffer (line 143)
     *
     * @param buffer Buffer handle
     * @param str String to add
     * @return MCP_OK on success
     */
    fun mcp_buffer_add_string(buffer: Long, str: String?): Int

    /**
     * Add another buffer to buffer (line 152)
     *
     * @param buffer Destination buffer
     * @param source Source buffer
     * @return MCP_OK on success
     */
    fun mcp_buffer_add_buffer(buffer: Long, source: Long): Int

    /**
     * Add buffer fragment (zero-copy) (line 161)
     *
     * @param buffer Buffer handle
     * @param fragment Fragment to add
     * @return MCP_OK on success
     */
    fun mcp_buffer_add_fragment(buffer: Long, fragment: McpBufferFragment.ByReference?): Int

    /**
     * Prepend data to buffer (line 172)
     *
     * @param buffer Buffer handle
     * @param data Data to prepend
     * @param length Data length
     * @return MCP_OK on success
     */
    fun mcp_buffer_prepend(buffer: Long, data: Pointer?, length: NativeLong): Int

    /* ============================================================================
     * Buffer Consumption (lines 179-210)
     * ============================================================================
     */

    /**
     * Drain bytes from front of buffer (line 187)
     *
     * @param buffer Buffer handle
     * @param size Number of bytes to drain
     * @return MCP_OK on success
     */
    fun mcp_buffer_drain(buffer: Long, size: NativeLong): Int

    /**
     * Move data from one buffer to another (line 197)
     *
     * @param source Source buffer
     * @param destination Destination buffer
     * @param length Bytes to move (0 for all)
     * @return MCP_OK on success
     */
    fun mcp_buffer_move(source: Long, destination: Long, length: NativeLong): Int

    /**
     * Set drain tracker for buffer (line 207)
     *
     * @param buffer Buffer handle
     * @param tracker Drain tracker
     * @return MCP_OK on success
     */
    fun mcp_buffer_set_drain_tracker(buffer: Long, tracker: McpDrainTracker.ByReference?): Int

    /* ============================================================================
     * Buffer Reservation (Zero-Copy Writing) (lines 214-257)
     * ============================================================================
     */

    /**
     * Reserve space for writing (line 223)
     *
     * @param buffer Buffer handle
     * @param min_size Minimum size to reserve
     * @param reservation Output reservation
     * @return MCP_OK on success
     */
    fun mcp_buffer_reserve(
        buffer: Long,
        min_size: NativeLong,
        reservation: McpBufferReservation.ByReference?
    ): Int

    /**
     * Reserve for vectored I/O (line 236)
     *
     * @param buffer Buffer handle
     * @param iovecs Array of iovec structures
     * @param iovec_count Number of iovecs
     * @param reserved Output: bytes reserved
     * @return MCP_OK on success
     */
    fun mcp_buffer_reserve_iovec(
        buffer: Long,
        iovecs: Pointer?,
        iovec_count: NativeLong,
        reserved: PointerByReference?
    ): Int

    /**
     * Commit reserved space (line 247)
     *
     * @param reservation Reservation to commit
     * @param bytes_written Actual bytes written
     * @return MCP_OK on success
     */
    fun mcp_buffer_commit_reservation(
        reservation: McpBufferReservation.ByReference?,
        bytes_written: NativeLong
    ): Int

    /**
     * Cancel reservation (line 255)
     *
     * @param reservation Reservation to cancel
     * @return MCP_OK on success
     */
    fun mcp_buffer_cancel_reservation(reservation: McpBufferReservation.ByReference?): Int

    /* ============================================================================
     * Buffer Access (Zero-Copy Reading) (lines 261-302)
     * ============================================================================
     */

    /**
     * Get contiguous memory view (line 272)
     *
     * @param buffer Buffer handle
     * @param offset Offset in buffer
     * @param length Requested length
     * @param data Output: data pointer
     * @param actual_length Output: actual length available
     * @return MCP_OK on success
     */
    fun mcp_buffer_get_contiguous(
        buffer: Long,
        offset: NativeLong,
        length: NativeLong,
        data: PointerByReference?,
        actual_length: PointerByReference?
    ): Int

    /**
     * Linearize buffer (ensure contiguous memory) (line 286)
     *
     * @param buffer Buffer handle
     * @param size Size to linearize
     * @param data Output: linearized data pointer
     * @return MCP_OK on success
     */
    fun mcp_buffer_linearize(buffer: Long, size: NativeLong, data: PointerByReference?): Int

    /**
     * Peek at buffer data without consuming (line 298)
     *
     * @param buffer Buffer handle
     * @param offset Offset to peek at
     * @param data Output buffer
     * @param length Length to peek
     * @return MCP_OK on success
     */
    fun mcp_buffer_peek(buffer: Long, offset: NativeLong, data: Pointer?, length: NativeLong): Int

    /* ============================================================================
     * Type-Safe I/O Operations (lines 306-351)
     * ============================================================================
     */

    /**
     * Write integer with little-endian byte order (line 315)
     *
     * @param buffer Buffer handle
     * @param value Value to write
     * @param size Size in bytes (1, 2, 4, 8)
     * @return MCP_OK on success
     */
    fun mcp_buffer_write_le_int(buffer: Long, value: Long, size: NativeLong): Int

    /**
     * Write integer with big-endian byte order (line 325)
     *
     * @param buffer Buffer handle
     * @param value Value to write
     * @param size Size in bytes (1, 2, 4, 8)
     * @return MCP_OK on success
     */
    fun mcp_buffer_write_be_int(buffer: Long, value: Long, size: NativeLong): Int

    /**
     * Read integer with little-endian byte order (line 337)
     *
     * @param buffer Buffer handle
     * @param size Size in bytes (1, 2, 4, 8)
     * @param value Output: read value
     * @return MCP_OK on success
     */
    fun mcp_buffer_read_le_int(buffer: Long, size: NativeLong, value: PointerByReference?): Int

    /**
     * Read integer with big-endian byte order (line 348)
     *
     * @param buffer Buffer handle
     * @param size Size in bytes (1, 2, 4, 8)
     * @param value Output: read value
     * @return MCP_OK on success
     */
    fun mcp_buffer_read_be_int(buffer: Long, size: NativeLong, value: PointerByReference?): Int

    /* ============================================================================
     * Buffer Search Operations (lines 355-382)
     * ============================================================================
     */

    /**
     * Search for pattern in buffer (line 366)
     *
     * @param buffer Buffer handle
     * @param pattern Pattern to search for
     * @param pattern_size Pattern size
     * @param start_position Start position for search
     * @param position Output: position where found
     * @return MCP_OK if found, MCP_ERROR_NOT_FOUND if not
     */
    fun mcp_buffer_search(
        buffer: Long,
        pattern: Pointer?,
        pattern_size: NativeLong,
        start_position: NativeLong,
        position: PointerByReference?
    ): Int

    /**
     * Find delimiter in buffer (line 379)
     *
     * @param buffer Buffer handle
     * @param delimiter Delimiter character
     * @param position Output: position where found
     * @return MCP_OK if found, MCP_ERROR_NOT_FOUND if not
     */
    fun mcp_buffer_find_byte(buffer: Long, delimiter: Byte, position: PointerByReference?): Int

    /* ============================================================================
     * Buffer Information (lines 386-417)
     * ============================================================================
     */

    /**
     * Get buffer length (line 393)
     *
     * @param buffer Buffer handle
     * @return Buffer length in bytes
     */
    fun mcp_buffer_length(buffer: Long): NativeLong

    /**
     * Get buffer capacity (line 400)
     *
     * @param buffer Buffer handle
     * @return Buffer capacity in bytes
     */
    fun mcp_buffer_capacity(buffer: Long): NativeLong

    /**
     * Check if buffer is empty (line 407)
     *
     * @param buffer Buffer handle
     * @return MCP_TRUE if empty
     */
    fun mcp_buffer_is_empty(buffer: Long): Byte

    /**
     * Get buffer statistics (line 415)
     *
     * @param buffer Buffer handle
     * @param stats Output statistics
     * @return MCP_OK on success
     */
    fun mcp_buffer_get_stats(buffer: Long, stats: McpBufferStats.ByReference?): Int

    /* ============================================================================
     * Buffer Watermarks (lines 421-452)
     * ============================================================================
     */

    /**
     * Set buffer watermarks for flow control (line 431)
     *
     * @param buffer Buffer handle
     * @param low_watermark Low watermark bytes
     * @param high_watermark High watermark bytes
     * @param overflow_watermark Overflow watermark bytes
     * @return MCP_OK on success
     */
    fun mcp_buffer_set_watermarks(
        buffer: Long,
        low_watermark: NativeLong,
        high_watermark: NativeLong,
        overflow_watermark: NativeLong
    ): Int

    /**
     * Check if buffer is above high watermark (line 442)
     *
     * @param buffer Buffer handle
     * @return MCP_TRUE if above high watermark
     */
    fun mcp_buffer_above_high_watermark(buffer: Long): Byte

    /**
     * Check if buffer is below low watermark (line 450)
     *
     * @param buffer Buffer handle
     * @return MCP_TRUE if below low watermark
     */
    fun mcp_buffer_below_low_watermark(buffer: Long): Byte

    /* ============================================================================
     * Advanced Buffer Pool (lines 457-496)
     * ============================================================================
     */

    /**
     * Create buffer pool with configuration (line 471)
     *
     * @param config Pool configuration
     * @return Buffer pool handle or NULL on error
     */
    fun mcp_buffer_pool_create_ex(config: McpBufferPoolConfig.ByReference?): Pointer?

    /**
     * Get pool statistics (line 482)
     *
     * @param pool Buffer pool
     * @param free_count Output: free buffers
     * @param used_count Output: used buffers
     * @param total_allocated Output: total bytes allocated
     * @return MCP_OK on success
     */
    fun mcp_buffer_pool_get_stats(
        pool: Pointer?,
        free_count: PointerByReference?,
        used_count: PointerByReference?,
        total_allocated: PointerByReference?
    ): Int

    /**
     * Trim pool to reduce memory usage (line 494)
     *
     * @param pool Buffer pool
     * @param target_free Target number of free buffers
     * @return MCP_OK on success
     */
    fun mcp_buffer_pool_trim(pool: Pointer?, target_free: NativeLong): Int
}