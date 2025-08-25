/**
 * @file mcp_c_memory.h
 * @brief Memory management and error handling for MCP C API
 *
 * This header provides memory management utilities, error handling,
 * and resource tracking for the MCP C API.
 */

#ifndef MCP_C_MEMORY_H
#define MCP_C_MEMORY_H

#include "mcp_c_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Library Initialization
 * ============================================================================ */

/**
 * Initialize the FFI system with custom allocator
 * @param allocator Custom memory allocator (NULL for default)
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_ffi_initialize(const mcp_allocator_t* allocator) MCP_NOEXCEPT;

/**
 * Shutdown the FFI system and release all resources
 */
MCP_API void mcp_ffi_shutdown(void) MCP_NOEXCEPT;

/**
 * Check if FFI system is initialized
 * @return MCP_TRUE if initialized
 */
MCP_API mcp_bool_t mcp_ffi_is_initialized(void) MCP_NOEXCEPT;

/* ============================================================================
 * Error Handling
 * ============================================================================ */

/**
 * Get last error information (thread-local)
 * @return Error info or NULL if no error
 */
MCP_API const mcp_error_info_t* mcp_get_last_error(void) MCP_NOEXCEPT;

/**
 * Clear last error for current thread
 */
MCP_API void mcp_clear_last_error(void) MCP_NOEXCEPT;

/**
 * Set custom error handler
 * @param handler Error handler function
 * @param user_data User data passed to handler
 */
typedef void (*mcp_error_handler_t)(const mcp_error_info_t* error, void* user_data);
MCP_API void mcp_set_error_handler(mcp_error_handler_t handler, void* user_data) MCP_NOEXCEPT;

/* ============================================================================
 * Memory Pool Management
 * ============================================================================ */

typedef struct mcp_memory_pool_impl* mcp_memory_pool_t;

/**
 * Create a memory pool for batch operations
 * @param initial_size Initial pool size in bytes
 * @return Memory pool handle or NULL on error
 */
MCP_API mcp_memory_pool_t mcp_memory_pool_create(size_t initial_size) MCP_NOEXCEPT;

/**
 * Destroy memory pool and free all allocations
 * @param pool Memory pool to destroy
 */
MCP_API void mcp_memory_pool_destroy(mcp_memory_pool_t pool) MCP_NOEXCEPT;

/**
 * Allocate memory from pool
 * @param pool Memory pool
 * @param size Size to allocate
 * @return Allocated memory or NULL
 */
MCP_API void* mcp_memory_pool_alloc(mcp_memory_pool_t pool, size_t size) MCP_NOEXCEPT;

/**
 * Reset pool (free all allocations but keep pool)
 * @param pool Memory pool
 */
MCP_API void mcp_memory_pool_reset(mcp_memory_pool_t pool) MCP_NOEXCEPT;

/**
 * Get pool statistics
 * @param pool Memory pool
 * @param used_bytes Output: bytes currently used
 * @param total_bytes Output: total pool size
 * @param allocation_count Output: number of allocations
 */
MCP_API void mcp_memory_pool_stats(mcp_memory_pool_t pool,
                                   size_t* used_bytes,
                                   size_t* total_bytes,
                                   size_t* allocation_count) MCP_NOEXCEPT;

/* ============================================================================
 * Batch Operations
 * ============================================================================ */

typedef enum {
    MCP_BATCH_OP_CREATE,
    MCP_BATCH_OP_FREE,
    MCP_BATCH_OP_SET,
    MCP_BATCH_OP_GET
} mcp_batch_op_type_t;

typedef struct {
    mcp_batch_op_type_t type;
    mcp_type_id_t target_type;
    void* target;
    void* param1;
    void* param2;
    mcp_result_t result;
} mcp_batch_operation_t;

/**
 * Execute batch operations atomically
 * @param operations Array of operations
 * @param count Number of operations
 * @return MCP_OK if all operations succeed
 */
MCP_API mcp_result_t mcp_batch_execute(const mcp_batch_operation_t* operations, size_t count) MCP_NOEXCEPT;

/* ============================================================================
 * Resource Tracking (Debug Mode)
 * ============================================================================ */

#ifdef MCP_DEBUG

/**
 * Enable resource tracking for leak detection
 * @param enable MCP_TRUE to enable
 */
MCP_API void mcp_enable_resource_tracking(mcp_bool_t enable) MCP_NOEXCEPT;

/**
 * Get current resource count by type
 * @param type Resource type
 * @return Number of active resources
 */
MCP_API size_t mcp_get_resource_count(mcp_type_id_t type) MCP_NOEXCEPT;

/**
 * Print resource tracking report
 */
MCP_API void mcp_print_resource_report(void) MCP_NOEXCEPT;

/**
 * Check for resource leaks
 * @return MCP_TRUE if leaks detected
 */
MCP_API mcp_bool_t mcp_check_leaks(void) MCP_NOEXCEPT;

#endif /* MCP_DEBUG */

/* ============================================================================
 * Memory Utilities
 * ============================================================================ */

/**
 * Duplicate a string using MCP allocator
 * @param str String to duplicate
 * @return Duplicated string (must be freed with mcp_string_free)
 */
MCP_API char* mcp_strdup(const char* str) MCP_NOEXCEPT;

/**
 * Free a string allocated by MCP
 * @param str String to free
 */
MCP_API void mcp_string_free(char* str) MCP_NOEXCEPT;

/**
 * Allocate memory using MCP allocator
 * @param size Size to allocate
 * @return Allocated memory or NULL
 */
MCP_API void* mcp_malloc(size_t size) MCP_NOEXCEPT;

/**
 * Reallocate memory using MCP allocator
 * @param ptr Pointer to reallocate
 * @param new_size New size
 * @return Reallocated memory or NULL
 */
MCP_API void* mcp_realloc(void* ptr, size_t new_size) MCP_NOEXCEPT;

/**
 * Free memory allocated by MCP
 * @param ptr Pointer to free
 */
MCP_API void mcp_free(void* ptr) MCP_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* MCP_C_MEMORY_H */