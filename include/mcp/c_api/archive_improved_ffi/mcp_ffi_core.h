/**
 * @file mcp_ffi_core.h
 * @brief Core FFI-safe types and utilities for Gopher MCP C API
 *
 * This header provides the foundational FFI-safe types and patterns that ensure
 * cross-language compatibility and RAII integration. All types follow strict
 * FFI best practices for use with Python ctypes, Go CGO, Rust FFI, etc.
 *
 * Design principles:
 * - Fixed-size primitive types only
 * - No unions in public API (use tagged opaque types)
 * - Clear ownership semantics
 * - Thread-safe reference counting
 * - Comprehensive error handling
 * - RAII-friendly patterns
 */

#ifndef MCP_FFI_CORE_H
#define MCP_FFI_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Platform and Compiler Configuration
 * ============================================================================
 */

/* Export/Import macros for shared library support */
#if defined(_MSC_VER)
#define MCP_API_EXPORT __declspec(dllexport)
#define MCP_API_IMPORT __declspec(dllimport)
#define MCP_CALLBACK __stdcall
#define MCP_NOEXCEPT
#elif defined(__GNUC__) || defined(__clang__)
#define MCP_API_EXPORT __attribute__((visibility("default")))
#define MCP_API_IMPORT
#define MCP_CALLBACK
#define MCP_NOEXCEPT __attribute__((nothrow))
#else
#define MCP_API_EXPORT
#define MCP_API_IMPORT
#define MCP_CALLBACK
#define MCP_NOEXCEPT
#endif

/* API decoration based on build configuration */
#ifdef MCP_BUILD_SHARED
#ifdef MCP_BUILD_LIBRARY
#define MCP_API MCP_API_EXPORT
#else
#define MCP_API MCP_API_IMPORT
#endif
#else
#define MCP_API
#endif

/* Structure packing for network protocols */
#if defined(_MSC_VER)
#define MCP_PACK_BEGIN __pragma(pack(push, 1))
#define MCP_PACK_END __pragma(pack(pop))
#define MCP_PACKED
#elif defined(__GNUC__) || defined(__clang__)
#define MCP_PACK_BEGIN _Pragma("pack(push, 1)")
#define MCP_PACK_END _Pragma("pack(pop)")
#define MCP_PACKED __attribute__((packed))
#else
#define MCP_PACK_BEGIN
#define MCP_PACK_END
#define MCP_PACKED
#endif

/* Alignment macros for cache optimization */
#if defined(__GNUC__) || defined(__clang__)
#define MCP_ALIGN(n) __attribute__((aligned(n)))
#elif defined(_MSC_VER)
#define MCP_ALIGN(n) __declspec(align(n))
#else
#define MCP_ALIGN(n)
#endif

/* ============================================================================
 * FFI-Safe Primitive Types
 * ============================================================================
 */

/**
 * FFI-safe boolean type (guaranteed 1 byte)
 * Uses uint8_t to ensure consistent size across all platforms and languages
 */
typedef uint8_t mcp_bool_t;
#define MCP_TRUE ((mcp_bool_t)1)
#define MCP_FALSE ((mcp_bool_t)0)

/**
 * Result codes for all API operations
 * Negative values indicate errors, non-negative indicate success
 */
typedef enum mcp_result {
  MCP_OK = 0,      /* Operation successful */
  MCP_PENDING = 1, /* Operation pending (async) */

  /* General errors (-1 to -99) */
  MCP_ERROR = -1,                   /* General error */
  MCP_ERROR_INVALID_ARGUMENT = -2,  /* Invalid argument provided */
  MCP_ERROR_NULL_POINTER = -3,      /* Null pointer where not allowed */
  MCP_ERROR_OUT_OF_MEMORY = -4,     /* Memory allocation failed */
  MCP_ERROR_NOT_FOUND = -5,         /* Resource not found */
  MCP_ERROR_ALREADY_EXISTS = -6,    /* Resource already exists */
  MCP_ERROR_PERMISSION_DENIED = -7, /* Permission denied */
  MCP_ERROR_NOT_SUPPORTED = -8,     /* Operation not supported */
  MCP_ERROR_TIMEOUT = -9,           /* Operation timed out */
  MCP_ERROR_CANCELLED = -10,        /* Operation was cancelled */

  /* State errors (-100 to -199) */
  MCP_ERROR_INVALID_STATE = -100,       /* Invalid state for operation */
  MCP_ERROR_NOT_INITIALIZED = -101,     /* Library not initialized */
  MCP_ERROR_ALREADY_INITIALIZED = -102, /* Already initialized */
  MCP_ERROR_IN_USE = -103,              /* Resource is in use */
  MCP_ERROR_NOT_CONNECTED = -104,       /* Not connected */
  MCP_ERROR_ALREADY_CONNECTED = -105,   /* Already connected */

  /* I/O errors (-200 to -299) */
  MCP_ERROR_IO = -200,                /* General I/O error */
  MCP_ERROR_WOULD_BLOCK = -201,       /* Operation would block */
  MCP_ERROR_CONNECTION_CLOSED = -202, /* Connection closed */
  MCP_ERROR_CONNECTION_RESET = -203,  /* Connection reset by peer */
  MCP_ERROR_BROKEN_PIPE = -204,       /* Broken pipe */

  /* Protocol errors (-300 to -399) */
  MCP_ERROR_PROTOCOL = -300,            /* Protocol error */
  MCP_ERROR_INVALID_MESSAGE = -301,     /* Invalid message format */
  MCP_ERROR_MESSAGE_TOO_LARGE = -302,   /* Message exceeds size limit */
  MCP_ERROR_UNSUPPORTED_VERSION = -303, /* Unsupported protocol version */

  /* JSON-RPC errors (-32000 to -32999) */
  MCP_ERROR_JSONRPC_PARSE_ERROR = -32700,
  MCP_ERROR_JSONRPC_INVALID_REQUEST = -32600,
  MCP_ERROR_JSONRPC_METHOD_NOT_FOUND = -32601,
  MCP_ERROR_JSONRPC_INVALID_PARAMS = -32602,
  MCP_ERROR_JSONRPC_INTERNAL_ERROR = -32603
} mcp_result_t;

/* ============================================================================
 * String Types with Clear Ownership
 * ============================================================================
 */

/**
 * String view (borrowed reference - does not own memory)
 * Used for passing strings into API functions
 */
typedef struct mcp_string_view {
  const char* data; /* Pointer to string data (not null-terminated) */
  size_t length;    /* Length of string in bytes */
} mcp_string_view_t;

/**
 * Owned string (owns memory - must be freed)
 * Used for returning strings from API functions
 */
typedef struct mcp_string_owned {
  char* data;      /* Owned string data (null-terminated) */
  size_t length;   /* Length of string (excluding null terminator) */
  size_t capacity; /* Allocated capacity */
} mcp_string_owned_t;

/**
 * String reference with ownership flag
 * Can represent either borrowed or owned strings
 */
typedef struct mcp_string_ref {
  const char* data; /* String data */
  size_t length;    /* Length in bytes */
  mcp_bool_t owned; /* MCP_TRUE if owned, MCP_FALSE if borrowed */
} mcp_string_ref_t;

/* ============================================================================
 * Opaque Handle Types
 * ============================================================================
 */

/* Forward declarations of opaque types */
typedef struct mcp_handle_impl* mcp_handle_t;
typedef struct mcp_refcounted_impl* mcp_refcounted_t;
typedef struct mcp_mempool_impl* mcp_mempool_t;
typedef struct mcp_error_context_impl* mcp_error_context_t;

/* ============================================================================
 * Reference Counting Support
 * ============================================================================
 */

/**
 * Reference-counted handle with atomic operations
 * Enables safe sharing across threads and languages
 */
typedef struct mcp_ref_handle {
  mcp_handle_t handle; /* Opaque handle to object */
  int32_t* ref_count;  /* Pointer to atomic reference count */
} mcp_ref_handle_t;

/* ============================================================================
 * Error Context for Detailed Diagnostics
 * ============================================================================
 */

/**
 * Extended error information
 * Provides detailed context for debugging
 */
typedef struct mcp_error_info {
  mcp_result_t code;  /* Error code */
  char message[256];  /* Human-readable error message */
  char file[128];     /* Source file where error occurred */
  int32_t line;       /* Line number where error occurred */
  uint64_t timestamp; /* Timestamp when error occurred */
  uint32_t thread_id; /* Thread ID where error occurred */
} mcp_error_info_t;

/* ============================================================================
 * Memory Management
 * ============================================================================
 */

/**
 * Custom allocator interface for memory management
 * Allows integration with application memory systems
 */
typedef struct mcp_allocator {
  void* (*alloc)(size_t size, void* user_data);
  void* (*realloc)(void* ptr, size_t new_size, void* user_data);
  void (*free)(void* ptr, void* user_data);
  void* user_data;
} mcp_allocator_t;

/**
 * Memory statistics for monitoring
 */
typedef struct mcp_memory_stats {
  uint64_t total_allocated;  /* Total bytes allocated */
  uint64_t total_freed;      /* Total bytes freed */
  uint64_t current_usage;    /* Current memory usage */
  uint64_t peak_usage;       /* Peak memory usage */
  uint32_t allocation_count; /* Number of allocations */
  uint32_t free_count;       /* Number of frees */
} mcp_memory_stats_t;

/* ============================================================================
 * Validation and Safety
 * ============================================================================
 */

/**
 * Magic number for handle validation
 * Used to detect use-after-free and corruption
 */
#define MCP_HANDLE_MAGIC 0x4D435048 /* "MCPH" in hex */

/**
 * Handle validation structure
 */
typedef struct mcp_handle_header {
  uint32_t magic;   /* Magic number for validation */
  uint32_t type_id; /* Type identifier */
  uint32_t flags;   /* Handle flags */
  uint32_t version; /* Structure version */
} mcp_handle_header_t;

/* Handle flags */
#define MCP_HANDLE_FLAG_VALID 0x01       /* Handle is valid */
#define MCP_HANDLE_FLAG_OWNED 0x02       /* Handle owns resources */
#define MCP_HANDLE_FLAG_SHARED 0x04      /* Handle is shared (ref-counted) */
#define MCP_HANDLE_FLAG_READONLY 0x08    /* Handle is read-only */
#define MCP_HANDLE_FLAG_THREAD_SAFE 0x10 /* Handle is thread-safe */

/* ============================================================================
 * Core API Functions
 * ============================================================================
 */

/* Library initialization and cleanup */
MCP_API mcp_result_t mcp_ffi_initialize(const mcp_allocator_t* allocator)
    MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_ffi_shutdown(void) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_ffi_is_initialized(void) MCP_NOEXCEPT;

/* Version information */
MCP_API const char* mcp_ffi_get_version(void) MCP_NOEXCEPT;
MCP_API uint32_t mcp_ffi_get_version_major(void) MCP_NOEXCEPT;
MCP_API uint32_t mcp_ffi_get_version_minor(void) MCP_NOEXCEPT;
MCP_API uint32_t mcp_ffi_get_version_patch(void) MCP_NOEXCEPT;

/* Error handling */
MCP_API const mcp_error_info_t* mcp_get_last_error(void) MCP_NOEXCEPT;
MCP_API void mcp_clear_last_error(void) MCP_NOEXCEPT;
MCP_API const char* mcp_result_to_string(mcp_result_t result) MCP_NOEXCEPT;

/* String management */
MCP_API mcp_string_owned_t* mcp_string_create(const char* data,
                                              size_t length) MCP_NOEXCEPT;
MCP_API mcp_string_owned_t* mcp_string_create_from_cstr(const char* cstr)
    MCP_NOEXCEPT;
MCP_API void mcp_string_free(mcp_string_owned_t* str) MCP_NOEXCEPT;
MCP_API mcp_string_owned_t* mcp_string_duplicate(const mcp_string_view_t* view)
    MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_string_append(
    mcp_string_owned_t* str, const mcp_string_view_t* suffix) MCP_NOEXCEPT;
MCP_API void mcp_string_clear(mcp_string_owned_t* str) MCP_NOEXCEPT;
MCP_API void mcp_string_reset(mcp_string_owned_t* str) MCP_NOEXCEPT;

/* Reference counting */
MCP_API void mcp_ref_acquire(mcp_ref_handle_t* handle) MCP_NOEXCEPT;
MCP_API void mcp_ref_release(mcp_ref_handle_t* handle) MCP_NOEXCEPT;
MCP_API int32_t mcp_ref_count(const mcp_ref_handle_t* handle) MCP_NOEXCEPT;
MCP_API mcp_ref_handle_t* mcp_ref_create(mcp_handle_t handle) MCP_NOEXCEPT;

/* Handle validation */
MCP_API mcp_bool_t mcp_handle_is_valid(mcp_handle_t handle) MCP_NOEXCEPT;
MCP_API uint32_t mcp_handle_get_type(mcp_handle_t handle) MCP_NOEXCEPT;
MCP_API uint32_t mcp_handle_get_flags(mcp_handle_t handle) MCP_NOEXCEPT;

/* Memory pool management */
MCP_API mcp_mempool_t mcp_mempool_create(size_t initial_size) MCP_NOEXCEPT;
MCP_API void mcp_mempool_destroy(mcp_mempool_t pool) MCP_NOEXCEPT;
MCP_API void* mcp_mempool_alloc(mcp_mempool_t pool, size_t size) MCP_NOEXCEPT;
MCP_API void mcp_mempool_reset(mcp_mempool_t pool) MCP_NOEXCEPT;
MCP_API mcp_memory_stats_t mcp_mempool_get_stats(mcp_mempool_t pool)
    MCP_NOEXCEPT;

/* Batch operations for efficiency */
MCP_API void mcp_batch_free(void** objects,
                            size_t count,
                            void (*free_fn)(void*)) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_batch_validate(mcp_handle_t* handles,
                                        size_t count,
                                        mcp_bool_t* results) MCP_NOEXCEPT;

/* Thread-local storage for error context */
MCP_API void mcp_tls_set_error(const mcp_error_info_t* error) MCP_NOEXCEPT;
MCP_API const mcp_error_info_t* mcp_tls_get_error(void) MCP_NOEXCEPT;
MCP_API void mcp_tls_clear_error(void) MCP_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* MCP_FFI_CORE_H */