/**
 * @file mcp_filter_buffer.h
 * @brief Zero-copy buffer interface for MCP Filter API
 *
 * This header provides advanced buffer management capabilities for filters,
 * including zero-copy operations, scatter-gather I/O, and memory pooling.
 *
 * Features:
 * - Direct memory access without copying
 * - Scatter-gather I/O for fragmented buffers
 * - Buffer pooling for efficient allocation
 * - Copy-on-write semantics
 * - External memory integration
 */

#ifndef MCP_FILTER_BUFFER_H
#define MCP_FILTER_BUFFER_H

#include "mcp_c_memory.h"
#include "mcp_c_types.h"
#include "mcp_filter_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Buffer Types and Enumerations
 * ============================================================================
 */

// Buffer ownership model
typedef enum {
  MCP_BUFFER_OWNERSHIP_NONE = 0,       // No ownership (view only)
  MCP_BUFFER_OWNERSHIP_SHARED = 1,     // Shared ownership (ref counted)
  MCP_BUFFER_OWNERSHIP_EXCLUSIVE = 2,  // Exclusive ownership
  MCP_BUFFER_OWNERSHIP_EXTERNAL = 3    // External ownership (callback)
} mcp_buffer_ownership_t;

// Buffer fragment for external memory
typedef struct mcp_buffer_fragment {
  const void* data;
  size_t size;
  void (*release_callback)(void* data, size_t size, void* user_data);
  void* user_data;
} mcp_buffer_fragment_t;

// Buffer reservation for writing
typedef struct mcp_buffer_reservation {
  void* data;
  size_t capacity;
  mcp_buffer_handle_t buffer;
  uint64_t reservation_id;
} mcp_buffer_reservation_t;

// Buffer statistics
typedef struct mcp_buffer_stats {
  size_t total_bytes;
  size_t used_bytes;
  size_t slice_count;
  size_t fragment_count;
  uint64_t read_operations;
  uint64_t write_operations;
} mcp_buffer_stats_t;

// Drain tracker for monitoring buffer consumption
typedef void (*mcp_drain_tracker_cb)(size_t bytes_drained, void* user_data);

typedef struct mcp_drain_tracker {
  mcp_drain_tracker_cb callback;
  void* user_data;
} mcp_drain_tracker_t;

/* ============================================================================
 * Buffer Creation and Management
 * ============================================================================
 */

/**
 * Create a new buffer
 * @param initial_capacity Initial buffer capacity
 * @param ownership Ownership model
 * @return Buffer handle or 0 on error
 */
MCP_API mcp_buffer_handle_t mcp_buffer_create_owned(
    size_t initial_capacity, mcp_buffer_ownership_t ownership) MCP_NOEXCEPT;

/**
 * Create a buffer view (zero-copy reference)
 * @param data Data pointer
 * @param length Data length
 * @return Buffer handle or 0 on error
 */
MCP_API mcp_buffer_handle_t mcp_buffer_create_view(const void* data,
                                                   size_t length) MCP_NOEXCEPT;

/**
 * Create buffer from external fragment
 * @param fragment External memory fragment
 * @return Buffer handle or 0 on error
 */
MCP_API mcp_buffer_handle_t mcp_buffer_create_from_fragment(
    const mcp_buffer_fragment_t* fragment) MCP_NOEXCEPT;

/**
 * Clone a buffer (deep copy)
 * @param buffer Source buffer
 * @return Cloned buffer handle or 0 on error
 */
MCP_API mcp_buffer_handle_t mcp_buffer_clone(mcp_buffer_handle_t buffer)
    MCP_NOEXCEPT;

/**
 * Create copy-on-write buffer
 * @param buffer Source buffer
 * @return COW buffer handle or 0 on error
 */
MCP_API mcp_buffer_handle_t mcp_buffer_create_cow(mcp_buffer_handle_t buffer)
    MCP_NOEXCEPT;

/* ============================================================================
 * Buffer Data Operations
 * ============================================================================
 */

/**
 * Add data to buffer
 * @param buffer Buffer handle
 * @param data Data to add
 * @param length Data length
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_add(mcp_buffer_handle_t buffer,
                                    const void* data,
                                    size_t length) MCP_NOEXCEPT;

/**
 * Add string to buffer
 * @param buffer Buffer handle
 * @param str String to add
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_add_string(mcp_buffer_handle_t buffer,
                                           const char* str) MCP_NOEXCEPT;

/**
 * Add another buffer to buffer
 * @param buffer Destination buffer
 * @param source Source buffer
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_add_buffer(
    mcp_buffer_handle_t buffer, mcp_buffer_handle_t source) MCP_NOEXCEPT;

/**
 * Add buffer fragment (zero-copy)
 * @param buffer Buffer handle
 * @param fragment Fragment to add
 * @return MCP_OK on success
 */
MCP_API mcp_result_t
mcp_buffer_add_fragment(mcp_buffer_handle_t buffer,
                        const mcp_buffer_fragment_t* fragment) MCP_NOEXCEPT;

/**
 * Prepend data to buffer
 * @param buffer Buffer handle
 * @param data Data to prepend
 * @param length Data length
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_prepend(mcp_buffer_handle_t buffer,
                                        const void* data,
                                        size_t length) MCP_NOEXCEPT;

/* ============================================================================
 * Buffer Consumption
 * ============================================================================
 */

/**
 * Drain bytes from front of buffer
 * @param buffer Buffer handle
 * @param size Number of bytes to drain
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_drain(mcp_buffer_handle_t buffer,
                                      size_t size) MCP_NOEXCEPT;

/**
 * Move data from one buffer to another
 * @param source Source buffer
 * @param destination Destination buffer
 * @param length Bytes to move (0 for all)
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_move(mcp_buffer_handle_t source,
                                     mcp_buffer_handle_t destination,
                                     size_t length) MCP_NOEXCEPT;

/**
 * Set drain tracker for buffer
 * @param buffer Buffer handle
 * @param tracker Drain tracker
 * @return MCP_OK on success
 */
MCP_API mcp_result_t
mcp_buffer_set_drain_tracker(mcp_buffer_handle_t buffer,
                             const mcp_drain_tracker_t* tracker) MCP_NOEXCEPT;

/* ============================================================================
 * Buffer Reservation (Zero-Copy Writing)
 * ============================================================================
 */

/**
 * Reserve space for writing
 * @param buffer Buffer handle
 * @param min_size Minimum size to reserve
 * @param reservation Output reservation
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_reserve(mcp_buffer_handle_t buffer,
                                        size_t min_size,
                                        mcp_buffer_reservation_t* reservation)
    MCP_NOEXCEPT;

/**
 * Reserve for vectored I/O
 * @param buffer Buffer handle
 * @param iovecs Array of iovec structures
 * @param iovec_count Number of iovecs
 * @param reserved Output: bytes reserved
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_reserve_iovec(mcp_buffer_handle_t buffer,
                                              void* iovecs,
                                              size_t iovec_count,
                                              size_t* reserved) MCP_NOEXCEPT;

/**
 * Commit reserved space
 * @param reservation Reservation to commit
 * @param bytes_written Actual bytes written
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_commit_reservation(
    mcp_buffer_reservation_t* reservation, size_t bytes_written) MCP_NOEXCEPT;

/**
 * Cancel reservation
 * @param reservation Reservation to cancel
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_cancel_reservation(
    mcp_buffer_reservation_t* reservation) MCP_NOEXCEPT;

/* ============================================================================
 * Buffer Access (Zero-Copy Reading)
 * ============================================================================
 */

/**
 * Get contiguous memory view
 * @param buffer Buffer handle
 * @param offset Offset in buffer
 * @param length Requested length
 * @param data Output: data pointer
 * @param actual_length Output: actual length available
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_get_contiguous(mcp_buffer_handle_t buffer,
                                               size_t offset,
                                               size_t length,
                                               const void** data,
                                               size_t* actual_length)
    MCP_NOEXCEPT;

/**
 * Linearize buffer (ensure contiguous memory)
 * @param buffer Buffer handle
 * @param size Size to linearize
 * @param data Output: linearized data pointer
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_linearize(mcp_buffer_handle_t buffer,
                                          size_t size,
                                          void** data) MCP_NOEXCEPT;

/**
 * Peek at buffer data without consuming
 * @param buffer Buffer handle
 * @param offset Offset to peek at
 * @param data Output buffer
 * @param length Length to peek
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_peek(mcp_buffer_handle_t buffer,
                                     size_t offset,
                                     void* data,
                                     size_t length) MCP_NOEXCEPT;

/* ============================================================================
 * Type-Safe I/O Operations
 * ============================================================================
 */

/**
 * Write integer with little-endian byte order
 * @param buffer Buffer handle
 * @param value Value to write
 * @param size Size in bytes (1, 2, 4, 8)
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_write_le_int(mcp_buffer_handle_t buffer,
                                             uint64_t value,
                                             size_t size) MCP_NOEXCEPT;

/**
 * Write integer with big-endian byte order
 * @param buffer Buffer handle
 * @param value Value to write
 * @param size Size in bytes (1, 2, 4, 8)
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_write_be_int(mcp_buffer_handle_t buffer,
                                             uint64_t value,
                                             size_t size) MCP_NOEXCEPT;

/**
 * Read integer with little-endian byte order
 * @param buffer Buffer handle
 * @param size Size in bytes (1, 2, 4, 8)
 * @param value Output: read value
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_read_le_int(mcp_buffer_handle_t buffer,
                                            size_t size,
                                            uint64_t* value) MCP_NOEXCEPT;

/**
 * Read integer with big-endian byte order
 * @param buffer Buffer handle
 * @param size Size in bytes (1, 2, 4, 8)
 * @param value Output: read value
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_read_be_int(mcp_buffer_handle_t buffer,
                                            size_t size,
                                            uint64_t* value) MCP_NOEXCEPT;

/* ============================================================================
 * Buffer Search Operations
 * ============================================================================
 */

/**
 * Search for pattern in buffer
 * @param buffer Buffer handle
 * @param pattern Pattern to search for
 * @param pattern_size Pattern size
 * @param start_position Start position for search
 * @param position Output: position where found
 * @return MCP_OK if found, MCP_ERROR_NOT_FOUND if not
 */
MCP_API mcp_result_t mcp_buffer_search(mcp_buffer_handle_t buffer,
                                       const void* pattern,
                                       size_t pattern_size,
                                       size_t start_position,
                                       size_t* position) MCP_NOEXCEPT;

/**
 * Find delimiter in buffer
 * @param buffer Buffer handle
 * @param delimiter Delimiter character
 * @param position Output: position where found
 * @return MCP_OK if found, MCP_ERROR_NOT_FOUND if not
 */
MCP_API mcp_result_t mcp_buffer_find_byte(mcp_buffer_handle_t buffer,
                                          uint8_t delimiter,
                                          size_t* position) MCP_NOEXCEPT;

/* ============================================================================
 * Buffer Information
 * ============================================================================
 */

/**
 * Get buffer length
 * @param buffer Buffer handle
 * @return Buffer length in bytes
 */
MCP_API size_t mcp_buffer_length(mcp_buffer_handle_t buffer) MCP_NOEXCEPT;

/**
 * Get buffer capacity
 * @param buffer Buffer handle
 * @return Buffer capacity in bytes
 */
MCP_API size_t mcp_buffer_capacity(mcp_buffer_handle_t buffer) MCP_NOEXCEPT;

/**
 * Check if buffer is empty
 * @param buffer Buffer handle
 * @return MCP_TRUE if empty
 */
MCP_API mcp_bool_t mcp_buffer_is_empty(mcp_buffer_handle_t buffer) MCP_NOEXCEPT;

/**
 * Get buffer statistics
 * @param buffer Buffer handle
 * @param stats Output statistics
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_get_stats(
    mcp_buffer_handle_t buffer, mcp_buffer_stats_t* stats) MCP_NOEXCEPT;

/* ============================================================================
 * Buffer Watermarks
 * ============================================================================
 */

/**
 * Set buffer watermarks for flow control
 * @param buffer Buffer handle
 * @param low_watermark Low watermark bytes
 * @param high_watermark High watermark bytes
 * @param overflow_watermark Overflow watermark bytes
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_set_watermarks(mcp_buffer_handle_t buffer,
                                               size_t low_watermark,
                                               size_t high_watermark,
                                               size_t overflow_watermark)
    MCP_NOEXCEPT;

/**
 * Check if buffer is above high watermark
 * @param buffer Buffer handle
 * @return MCP_TRUE if above high watermark
 */
MCP_API mcp_bool_t mcp_buffer_above_high_watermark(mcp_buffer_handle_t buffer)
    MCP_NOEXCEPT;

/**
 * Check if buffer is below low watermark
 * @param buffer Buffer handle
 * @return MCP_TRUE if below low watermark
 */
MCP_API mcp_bool_t mcp_buffer_below_low_watermark(mcp_buffer_handle_t buffer)
    MCP_NOEXCEPT;

/* ============================================================================
 * Advanced Buffer Pool
 * ============================================================================
 */

typedef struct mcp_buffer_pool_config {
  size_t buffer_size;           // Size of each buffer
  size_t max_buffers;           // Maximum buffers in pool
  size_t prealloc_count;        // Number to preallocate
  mcp_bool_t use_thread_local;  // Use thread-local caching
  mcp_bool_t zero_on_alloc;     // Zero memory on allocation
} mcp_buffer_pool_config_t;

/**
 * Create buffer pool with configuration
 * @param config Pool configuration
 * @return Buffer pool handle or NULL on error
 */
MCP_API mcp_buffer_pool_t
mcp_buffer_pool_create_ex(const mcp_buffer_pool_config_t* config) MCP_NOEXCEPT;

/**
 * Get pool statistics
 * @param pool Buffer pool
 * @param free_count Output: free buffers
 * @param used_count Output: used buffers
 * @param total_allocated Output: total bytes allocated
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_pool_get_stats(mcp_buffer_pool_t pool,
                                               size_t* free_count,
                                               size_t* used_count,
                                               size_t* total_allocated)
    MCP_NOEXCEPT;

/**
 * Trim pool to reduce memory usage
 * @param pool Buffer pool
 * @param target_free Target number of free buffers
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_buffer_pool_trim(mcp_buffer_pool_t pool,
                                          size_t target_free) MCP_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* MCP_FILTER_BUFFER_H */