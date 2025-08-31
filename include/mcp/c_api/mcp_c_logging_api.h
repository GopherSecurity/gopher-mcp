/**
 * @file mcp_c_logging_api.h
 * @brief FFI-safe C API for MCP Logging Framework with RAII enforcement
 * 
 * This header provides a complete C API for the MCP logging framework with:
 * - Automatic resource management through RAII
 * - Thread-safe handle-based logger access
 * - Zero-copy string passing
 * - Structured logging support
 * - Custom sink integration
 * - Safe cleanup guarantees
 * 
 * All resources are automatically cleaned up when handles are released,
 * preventing memory leaks across FFI boundaries.
 */

#ifndef MCP_LOGGING_API_H
#define MCP_LOGGING_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Core Types and Enumerations
 * ============================================================================ */

/** 
 * Opaque handle types with type safety for RAII management
 * Each handle type is distinct to prevent mixing handle types
 */
typedef struct mcp_logger_handle { uint64_t id; } mcp_logger_handle_t;
typedef struct mcp_sink_handle { uint64_t id; } mcp_sink_handle_t;
typedef struct mcp_log_context_handle { uint64_t id; } mcp_log_context_handle_t;
typedef struct mcp_formatter_handle { uint64_t id; } mcp_formatter_handle_t;

/** Invalid handle constants */
#ifdef __cplusplus
  /* C++ doesn't support compound literals, use constexpr */
  constexpr mcp_logger_handle_t MCP_INVALID_LOGGER_HANDLE = {0};
  constexpr mcp_sink_handle_t MCP_INVALID_SINK_HANDLE = {0};
  constexpr mcp_log_context_handle_t MCP_INVALID_CONTEXT_HANDLE = {0};
  constexpr mcp_formatter_handle_t MCP_INVALID_FORMATTER_HANDLE = {0};
#else
  /* C supports compound literals */
  #define MCP_INVALID_LOGGER_HANDLE ((mcp_logger_handle_t){0})
  #define MCP_INVALID_SINK_HANDLE ((mcp_sink_handle_t){0})
  #define MCP_INVALID_CONTEXT_HANDLE ((mcp_log_context_handle_t){0})
  #define MCP_INVALID_FORMATTER_HANDLE ((mcp_formatter_handle_t){0})
#endif

/** Handle validation macros */
#define MCP_IS_VALID_LOGGER(h) ((h).id != 0)
#define MCP_IS_VALID_SINK(h) ((h).id != 0)
#define MCP_IS_VALID_CONTEXT(h) ((h).id != 0)
#define MCP_IS_VALID_FORMATTER(h) ((h).id != 0)

/** Result codes for logging operations */
typedef enum {
    MCP_LOG_OK = 0,
    MCP_LOG_ERROR_INVALID_HANDLE = -1,
    MCP_LOG_ERROR_NULL_POINTER = -2,
    MCP_LOG_ERROR_OUT_OF_MEMORY = -3,
    MCP_LOG_ERROR_INVALID_LEVEL = -4,
    MCP_LOG_ERROR_SINK_FAILED = -5,
    MCP_LOG_ERROR_ALREADY_EXISTS = -6,
    MCP_LOG_ERROR_NOT_FOUND = -7,
    MCP_LOG_ERROR_INVALID_PATTERN = -8,
    MCP_LOG_ERROR_IO_ERROR = -9,
    MCP_LOG_ERROR_NOT_INITIALIZED = -10
} mcp_log_result_t;

/** Log levels matching RFC-5424 */
typedef enum {
    MCP_LOG_LEVEL_DEBUG = 0,
    MCP_LOG_LEVEL_INFO = 1,
    MCP_LOG_LEVEL_NOTICE = 2,
    MCP_LOG_LEVEL_WARNING = 3,
    MCP_LOG_LEVEL_ERROR = 4,
    MCP_LOG_LEVEL_CRITICAL = 5,
    MCP_LOG_LEVEL_ALERT = 6,
    MCP_LOG_LEVEL_EMERGENCY = 7,
    MCP_LOG_LEVEL_OFF = 8
} mcp_log_level_t;

/** Logging modes */
typedef enum {
    MCP_LOG_MODE_SYNC = 0,
    MCP_LOG_MODE_ASYNC = 1,
    MCP_LOG_MODE_NOOP = 2
} mcp_log_mode_t;

/** Component identifiers */
typedef enum {
    MCP_LOG_COMPONENT_ROOT = 0,
    MCP_LOG_COMPONENT_SERVER = 1,
    MCP_LOG_COMPONENT_CLIENT = 2,
    MCP_LOG_COMPONENT_NETWORK = 3,
    MCP_LOG_COMPONENT_FILTER = 4,
    MCP_LOG_COMPONENT_TRANSPORT = 5,
    MCP_LOG_COMPONENT_PROTOCOL = 6,
    MCP_LOG_COMPONENT_EVENT = 7,
    MCP_LOG_COMPONENT_HTTP = 8,
    MCP_LOG_COMPONENT_JSON = 9,
    MCP_LOG_COMPONENT_MEMORY = 10,
    MCP_LOG_COMPONENT_COUNT = 11
} mcp_log_component_t;

/** Sink types */
typedef enum {
    MCP_SINK_TYPE_FILE = 0,
    MCP_SINK_TYPE_STDIO = 1,
    MCP_SINK_TYPE_NULL = 2,
    MCP_SINK_TYPE_EXTERNAL = 3,
    MCP_SINK_TYPE_MCP = 4
} mcp_sink_type_t;

/** Formatter types */
typedef enum {
    MCP_FORMATTER_DEFAULT = 0,
    MCP_FORMATTER_JSON = 1,
    MCP_FORMATTER_CUSTOM = 2
} mcp_formatter_type_t;

/* ============================================================================
 * FFI-Safe String Type
 * ============================================================================ */

/** 
 * Zero-copy string reference for efficient string passing
 * IMPORTANT: The caller retains ownership of the string data.
 * The string data must remain valid for the duration of the API call.
 */
typedef struct mcp_string_view {
    const char* data;  /* Pointer to string data (not null-terminated) */
    size_t length;     /* Length of string in bytes */
} mcp_string_view_t;

/** Helper macro to create string view from literal */
#define MCP_STRING_VIEW(str) \
    (mcp_string_view_t){.data = (str), .length = sizeof(str) - 1}

/** Helper macro to create string view from C string */
#define MCP_STRING_VIEW_C(str) \
    (mcp_string_view_t){.data = (str), .length = strlen(str)}

/** Helper macro for empty string view */
#define MCP_EMPTY_STRING_VIEW \
    (mcp_string_view_t){.data = NULL, .length = 0}

/* ============================================================================
 * Log Message Structure
 * ============================================================================ */

/** Structured log message for detailed logging */
typedef struct mcp_log_message {
    mcp_log_level_t level;
    mcp_string_view_t message;
    mcp_log_component_t component;
    mcp_string_view_t component_name;
    
    /** Source location (optional) */
    const char* file;
    int line;
    const char* function;
    
    /** Correlation IDs (optional) */
    mcp_string_view_t trace_id;
    mcp_string_view_t span_id;
    mcp_string_view_t request_id;
    
    /** Performance metrics (optional) */
    double duration_ms;
    uint64_t bytes_sent;
    uint64_t bytes_received;
} mcp_log_message_t;

/* ============================================================================
 * Callback Types for External Integration
 * ============================================================================ */

/** 
 * External sink callback for custom log handling
 * IMPORTANT: This callback is invoked synchronously during logging.
 * The callback must not call back into the logging API to avoid deadlock.
 * The formatted_message is only valid during the callback execution.
 */
typedef void (*mcp_log_sink_callback_t)(
    mcp_log_level_t level,
    const char* logger_name,        /* Null-terminated logger name */
    const char* formatted_message,   /* Null-terminated formatted message */
    void* user_data                  /* User-provided context */
);

/** 
 * Sink cleanup callback for releasing user data
 * Called when the sink is destroyed
 */
typedef void (*mcp_sink_cleanup_callback_t)(void* user_data);

/** Log filter callback for custom filtering */
typedef int (*mcp_log_filter_callback_t)(
    mcp_log_level_t level,
    const char* logger_name,
    void* user_data
);

/* ============================================================================
 * Initialization and Cleanup
 * ============================================================================ */

/**
 * Initialize the logging system with default configuration
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logging_initialize(void);

/**
 * Shutdown the logging system and release all resources
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logging_shutdown(void);

/**
 * Check if logging system is initialized
 * @return 1 if initialized, 0 otherwise
 */
int mcp_logging_is_initialized(void);

/* ============================================================================
 * Logger Management with RAII
 * ============================================================================ */

/**
 * Get or create a logger with automatic resource management
 * The returned handle must be released with mcp_logger_release()
 * 
 * @param name Logger name (hierarchical with dots)
 * @param handle Output handle for the logger
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logger_get_or_create(
    mcp_string_view_t name,
    mcp_logger_handle_t* handle
);

/**
 * Get the default logger
 * The returned handle must be released with mcp_logger_release()
 * 
 * @param handle Output handle for the default logger
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logger_get_default(mcp_logger_handle_t* handle);

/**
 * Release a logger handle (RAII cleanup)
 * 
 * @param handle Logger handle to release
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logger_release(mcp_logger_handle_t handle);

/**
 * Set logger level
 * 
 * @param handle Logger handle
 * @param level New log level
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logger_set_level(
    mcp_logger_handle_t handle,
    mcp_log_level_t level
);

/**
 * Get logger level
 * 
 * @param handle Logger handle
 * @param level Output log level
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logger_get_level(
    mcp_logger_handle_t handle,
    mcp_log_level_t* level
);

/**
 * Set logger mode (sync/async/noop)
 * 
 * @param handle Logger handle
 * @param mode Logging mode
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logger_set_mode(
    mcp_logger_handle_t handle,
    mcp_log_mode_t mode
);

/* ============================================================================
 * Basic Logging Functions
 * ============================================================================ */

/**
 * Log a simple message
 * 
 * @param handle Logger handle
 * @param level Log level
 * @param message Message to log
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logger_log(
    mcp_logger_handle_t handle,
    mcp_log_level_t level,
    mcp_string_view_t message
);

/**
 * Log with source location
 * 
 * @param handle Logger handle
 * @param level Log level
 * @param file Source file
 * @param line Source line
 * @param function Source function
 * @param message Message to log
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logger_log_with_location(
    mcp_logger_handle_t handle,
    mcp_log_level_t level,
    const char* file,
    int line,
    const char* function,
    mcp_string_view_t message
);

/**
 * Log with component information
 * 
 * @param handle Logger handle
 * @param level Log level
 * @param component Component identifier
 * @param message Message to log
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logger_log_with_component(
    mcp_logger_handle_t handle,
    mcp_log_level_t level,
    mcp_log_component_t component,
    mcp_string_view_t message
);

/**
 * Log a structured message
 * 
 * @param handle Logger handle
 * @param message Structured log message
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logger_log_structured(
    mcp_logger_handle_t handle,
    const mcp_log_message_t* message
);

/**
 * Flush logger buffers
 * 
 * @param handle Logger handle
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logger_flush(mcp_logger_handle_t handle);

/* ============================================================================
 * Convenience Macros for Logging
 * ============================================================================ */

#define MCP_LOG_DEBUG(handle, msg) \
    mcp_logger_log(handle, MCP_LOG_LEVEL_DEBUG, MCP_STRING_VIEW(msg))

#define MCP_LOG_INFO(handle, msg) \
    mcp_logger_log(handle, MCP_LOG_LEVEL_INFO, MCP_STRING_VIEW(msg))

#define MCP_LOG_WARNING(handle, msg) \
    mcp_logger_log(handle, MCP_LOG_LEVEL_WARNING, MCP_STRING_VIEW(msg))

#define MCP_LOG_ERROR(handle, msg) \
    mcp_logger_log(handle, MCP_LOG_LEVEL_ERROR, MCP_STRING_VIEW(msg))

/* ============================================================================
 * Sink Management with RAII
 * ============================================================================ */

/**
 * Create a file sink with rotation support
 * The returned handle must be released with mcp_sink_release()
 * 
 * @param filename Base filename for log file
 * @param max_file_size Maximum file size before rotation (0 for no limit)
 * @param max_files Maximum number of rotated files to keep
 * @param handle Output handle for the sink
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_sink_create_file(
    mcp_string_view_t filename,
    size_t max_file_size,
    size_t max_files,
    mcp_sink_handle_t* handle
);

/**
 * Create a stdio sink (stdout/stderr)
 * The returned handle must be released with mcp_sink_release()
 * 
 * @param use_stderr 1 for stderr, 0 for stdout
 * @param handle Output handle for the sink
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_sink_create_stdio(
    int use_stderr,
    mcp_sink_handle_t* handle
);

/**
 * Create an external sink with custom callback
 * The returned handle must be released with mcp_sink_release()
 * 
 * @param callback Callback function for log handling
 * @param user_data User data passed to callback
 * @param handle Output handle for the sink
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_sink_create_external(
    mcp_log_sink_callback_t callback,
    void* user_data,
    mcp_sink_cleanup_callback_t cleanup,  /* Optional cleanup for user_data */
    mcp_sink_handle_t* handle
);

/**
 * Release a sink handle (RAII cleanup)
 * 
 * @param handle Sink handle to release
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_sink_release(mcp_sink_handle_t handle);

/**
 * Attach a sink to a logger
 * 
 * @param logger_handle Logger handle
 * @param sink_handle Sink handle
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logger_set_sink(
    mcp_logger_handle_t logger_handle,
    mcp_sink_handle_t sink_handle
);

/* ============================================================================
 * Log Context Management with RAII
 * ============================================================================ */

/**
 * Create a log context for structured logging
 * The returned handle must be released with mcp_context_release()
 * 
 * @param handle Output handle for the context
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_context_create(mcp_log_context_handle_t* handle);

/**
 * Set trace ID in context
 * 
 * @param handle Context handle
 * @param trace_id Trace identifier
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_context_set_trace_id(
    mcp_log_context_handle_t handle,
    mcp_string_view_t trace_id
);

/**
 * Set request ID in context
 * 
 * @param handle Context handle
 * @param request_id Request identifier
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_context_set_request_id(
    mcp_log_context_handle_t handle,
    mcp_string_view_t request_id
);

/**
 * Add metadata to context
 * 
 * @param handle Context handle
 * @param key Metadata key
 * @param value Metadata value
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_context_add_metadata(
    mcp_log_context_handle_t handle,
    mcp_string_view_t key,
    mcp_string_view_t value
);

/**
 * Log with context
 * 
 * @param logger_handle Logger handle
 * @param context_handle Context handle
 * @param level Log level
 * @param message Message to log
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logger_log_with_context(
    mcp_logger_handle_t logger_handle,
    mcp_log_context_handle_t context_handle,
    mcp_log_level_t level,
    mcp_string_view_t message
);

/**
 * Release a context handle (RAII cleanup)
 * 
 * @param handle Context handle to release
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_context_release(mcp_log_context_handle_t handle);

/* ============================================================================
 * Global Configuration
 * ============================================================================ */

/**
 * Set global log level for all loggers
 * 
 * @param level Global log level
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logging_set_global_level(mcp_log_level_t level);

/**
 * Set log level for a specific component
 * 
 * @param component Component identifier
 * @param level Log level for the component
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logging_set_component_level(
    mcp_log_component_t component,
    mcp_log_level_t level
);

/**
 * Set log level by pattern (glob-style)
 * 
 * @param pattern Glob pattern for logger names
 * @param level Log level for matching loggers
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logging_set_pattern_level(
    mcp_string_view_t pattern,
    mcp_log_level_t level
);

/**
 * Enable/disable bloom filter optimization
 * 
 * @param enabled 1 to enable, 0 to disable
 * @param size Bloom filter size (0 for default)
 * @param num_hashes Number of hash functions (0 for default)
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logging_set_bloom_filter(
    int enabled,
    size_t size,
    size_t num_hashes
);

/* ============================================================================
 * Statistics and Monitoring
 * ============================================================================ */

/** Logging statistics structure */
typedef struct mcp_logging_stats {
    uint64_t messages_logged;
    uint64_t messages_dropped;
    uint64_t loggers_created;
    uint64_t sinks_created;
    uint64_t contexts_created;
    uint64_t bytes_written;
    uint64_t active_loggers;
    uint64_t active_sinks;
    uint64_t active_contexts;
} mcp_logging_stats_t;

/**
 * Get logging statistics
 * 
 * @param stats Output statistics structure
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logging_get_stats(mcp_logging_stats_t* stats);

/**
 * Reset logging statistics
 * 
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logging_reset_stats(void);

/* ============================================================================
 * Error Handling
 * ============================================================================ */

/**
 * Get last error message (thread-local)
 * 
 * @param buffer Buffer to store error message
 * @param buffer_size Size of buffer
 * @return MCP_LOG_OK on success, error code otherwise
 */
mcp_log_result_t mcp_logging_get_last_error(char* buffer, size_t buffer_size);

/**
 * Convert result code to string
 * 
 * @param result Result code
 * @return String representation of result (static string, do not free)
 */
const char* mcp_logging_result_to_string(mcp_log_result_t result);

/* ============================================================================
 * RAII Guards for Automatic Resource Management
 * ============================================================================ */

#ifdef __cplusplus

namespace mcp {
namespace logging {

/**
 * RAII guard for automatic logger handle release
 */
class CLoggerGuard {
public:
    explicit CLoggerGuard(mcp_logger_handle_t h = MCP_INVALID_LOGGER_HANDLE) 
        : handle_(h) {}
    
    ~CLoggerGuard() {
        if (MCP_IS_VALID_LOGGER(handle_)) {
            mcp_logger_release(handle_);
        }
    }
    
    // Move semantics
    CLoggerGuard(CLoggerGuard&& other) noexcept 
        : handle_(other.handle_) {
        other.handle_ = MCP_INVALID_LOGGER_HANDLE;
    }
    
    CLoggerGuard& operator=(CLoggerGuard&& other) noexcept {
        if (this != &other) {
            if (MCP_IS_VALID_LOGGER(handle_)) {
                mcp_logger_release(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = MCP_INVALID_LOGGER_HANDLE;
        }
        return *this;
    }
    
    // Delete copy operations
    CLoggerGuard(const CLoggerGuard&) = delete;
    CLoggerGuard& operator=(const CLoggerGuard&) = delete;
    
    // Access
    mcp_logger_handle_t get() const { return handle_; }
    mcp_logger_handle_t* addressof() { return &handle_; }
    operator mcp_logger_handle_t() const { return handle_; }
    
    // Release ownership
    mcp_logger_handle_t release() {
        auto h = handle_;
        handle_ = MCP_INVALID_LOGGER_HANDLE;
        return h;
    }
    
private:
    mcp_logger_handle_t handle_;
};

/**
 * RAII guard for automatic sink handle release
 */
class CSinkGuard {
public:
    explicit CSinkGuard(mcp_sink_handle_t h = MCP_INVALID_SINK_HANDLE) 
        : handle_(h) {}
    
    ~CSinkGuard() {
        if (MCP_IS_VALID_SINK(handle_)) {
            mcp_sink_release(handle_);
        }
    }
    
    // Move semantics
    CSinkGuard(CSinkGuard&& other) noexcept 
        : handle_(other.handle_) {
        other.handle_ = MCP_INVALID_SINK_HANDLE;
    }
    
    CSinkGuard& operator=(CSinkGuard&& other) noexcept {
        if (this != &other) {
            if (MCP_IS_VALID_SINK(handle_)) {
                mcp_sink_release(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = MCP_INVALID_SINK_HANDLE;
        }
        return *this;
    }
    
    // Delete copy operations
    CSinkGuard(const CSinkGuard&) = delete;
    CSinkGuard& operator=(const CSinkGuard&) = delete;
    
    // Access
    mcp_sink_handle_t get() const { return handle_; }
    mcp_sink_handle_t* addressof() { return &handle_; }
    operator mcp_sink_handle_t() const { return handle_; }
    
    // Release ownership
    mcp_sink_handle_t release() {
        auto h = handle_;
        handle_ = MCP_INVALID_SINK_HANDLE;
        return h;
    }
    
private:
    mcp_sink_handle_t handle_;
};

/**
 * RAII guard for automatic context handle release
 */
class CContextGuard {
public:
    explicit CContextGuard(mcp_log_context_handle_t h = MCP_INVALID_CONTEXT_HANDLE) 
        : handle_(h) {}
    
    ~CContextGuard() {
        if (MCP_IS_VALID_CONTEXT(handle_)) {
            mcp_context_release(handle_);
        }
    }
    
    // Move semantics
    CContextGuard(CContextGuard&& other) noexcept 
        : handle_(other.handle_) {
        other.handle_ = MCP_INVALID_CONTEXT_HANDLE;
    }
    
    CContextGuard& operator=(CContextGuard&& other) noexcept {
        if (this != &other) {
            if (MCP_IS_VALID_CONTEXT(handle_)) {
                mcp_context_release(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = MCP_INVALID_CONTEXT_HANDLE;
        }
        return *this;
    }
    
    // Delete copy operations
    CContextGuard(const CContextGuard&) = delete;
    CContextGuard& operator=(const CContextGuard&) = delete;
    
    // Access
    mcp_log_context_handle_t get() const { return handle_; }
    mcp_log_context_handle_t* addressof() { return &handle_; }
    operator mcp_log_context_handle_t() const { return handle_; }
    
    // Release ownership
    mcp_log_context_handle_t release() {
        auto h = handle_;
        handle_ = MCP_INVALID_CONTEXT_HANDLE;
        return h;
    }
    
private:
    mcp_log_context_handle_t handle_;
};

} // namespace logging
} // namespace mcp

#endif // __cplusplus

#ifdef __cplusplus
}
#endif

#endif /* MCP_LOGGING_API_H */