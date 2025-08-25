/**
 * @file mcp_ffi_core.cc
 * @brief Implementation of core FFI-safe utilities and primitives
 */

#include "mcp/c_api/mcp_ffi_core.h"
#include "mcp/c_api/mcp_raii.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <cstring>
#include <cstdlib>
#include <unordered_map>

namespace {
    // Global initialization state
    std::atomic<bool> g_initialized{false};
    std::mutex g_init_mutex;
    
    // Custom allocator
    mcp_allocator_t g_allocator = {
        [](size_t size, void*) -> void* { return std::malloc(size); },
        [](void* ptr, size_t size, void*) -> void* { return std::realloc(ptr, size); },
        [](void* ptr, void*) { std::free(ptr); },
        nullptr
    };
    
    // Thread-local error storage
    thread_local mcp_error_info_t g_last_error = {};
    thread_local bool g_has_error = false;
    
    // Memory statistics
    std::atomic<uint64_t> g_total_allocated{0};
    std::atomic<uint64_t> g_total_freed{0};
    std::atomic<uint32_t> g_allocation_count{0};
    std::atomic<uint32_t> g_free_count{0};
    
    // Version information
    constexpr uint32_t VERSION_MAJOR = 1;
    constexpr uint32_t VERSION_MINOR = 0;
    constexpr uint32_t VERSION_PATCH = 0;
    constexpr const char* VERSION_STRING = "1.0.0";
    
    // Error message lookup
    const char* get_error_message(mcp_result_t result) {
        switch (result) {
            case MCP_OK: return "Success";
            case MCP_PENDING: return "Operation pending";
            case MCP_ERROR: return "General error";
            case MCP_ERROR_INVALID_ARGUMENT: return "Invalid argument";
            case MCP_ERROR_NULL_POINTER: return "Null pointer";
            case MCP_ERROR_OUT_OF_MEMORY: return "Out of memory";
            case MCP_ERROR_NOT_FOUND: return "Resource not found";
            case MCP_ERROR_ALREADY_EXISTS: return "Resource already exists";
            case MCP_ERROR_PERMISSION_DENIED: return "Permission denied";
            case MCP_ERROR_NOT_SUPPORTED: return "Operation not supported";
            case MCP_ERROR_TIMEOUT: return "Operation timed out";
            case MCP_ERROR_CANCELLED: return "Operation cancelled";
            case MCP_ERROR_INVALID_STATE: return "Invalid state";
            case MCP_ERROR_NOT_INITIALIZED: return "Not initialized";
            case MCP_ERROR_ALREADY_INITIALIZED: return "Already initialized";
            case MCP_ERROR_IN_USE: return "Resource in use";
            case MCP_ERROR_NOT_CONNECTED: return "Not connected";
            case MCP_ERROR_ALREADY_CONNECTED: return "Already connected";
            case MCP_ERROR_IO: return "I/O error";
            case MCP_ERROR_WOULD_BLOCK: return "Operation would block";
            case MCP_ERROR_CONNECTION_CLOSED: return "Connection closed";
            case MCP_ERROR_CONNECTION_RESET: return "Connection reset";
            case MCP_ERROR_BROKEN_PIPE: return "Broken pipe";
            case MCP_ERROR_PROTOCOL: return "Protocol error";
            case MCP_ERROR_INVALID_MESSAGE: return "Invalid message";
            case MCP_ERROR_MESSAGE_TOO_LARGE: return "Message too large";
            case MCP_ERROR_UNSUPPORTED_VERSION: return "Unsupported version";
            case MCP_ERROR_JSONRPC_PARSE_ERROR: return "JSON-RPC parse error";
            case MCP_ERROR_JSONRPC_INVALID_REQUEST: return "JSON-RPC invalid request";
            case MCP_ERROR_JSONRPC_METHOD_NOT_FOUND: return "JSON-RPC method not found";
            case MCP_ERROR_JSONRPC_INVALID_PARAMS: return "JSON-RPC invalid params";
            case MCP_ERROR_JSONRPC_INTERNAL_ERROR: return "JSON-RPC internal error";
            default: return "Unknown error";
        }
    }
    
    void set_error(mcp_result_t code, const char* message, const char* file, int line) {
        g_last_error.code = code;
        if (message) {
            std::strncpy(g_last_error.message, message, sizeof(g_last_error.message) - 1);
            g_last_error.message[sizeof(g_last_error.message) - 1] = '\0';
        } else {
            std::strncpy(g_last_error.message, get_error_message(code), sizeof(g_last_error.message) - 1);
        }
        if (file) {
            std::strncpy(g_last_error.file, file, sizeof(g_last_error.file) - 1);
            g_last_error.file[sizeof(g_last_error.file) - 1] = '\0';
        }
        g_last_error.line = line;
        g_last_error.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        g_last_error.thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
        g_has_error = true;
    }
}

extern "C" {

/* ============================================================================
 * Library Initialization
 * ============================================================================ */

MCP_API mcp_result_t mcp_ffi_initialize(const mcp_allocator_t* allocator) MCP_NOEXCEPT {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    
    if (g_initialized.load(std::memory_order_acquire)) {
        set_error(MCP_ERROR_ALREADY_INITIALIZED, nullptr, __FILE__, __LINE__);
        return MCP_ERROR_ALREADY_INITIALIZED;
    }
    
    if (allocator) {
        g_allocator = *allocator;
    }
    
    // Reset statistics
    g_total_allocated.store(0);
    g_total_freed.store(0);
    g_allocation_count.store(0);
    g_free_count.store(0);
    
    g_initialized.store(true, std::memory_order_release);
    return MCP_OK;
}

MCP_API mcp_result_t mcp_ffi_shutdown(void) MCP_NOEXCEPT {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    
    if (!g_initialized.load(std::memory_order_acquire)) {
        set_error(MCP_ERROR_NOT_INITIALIZED, nullptr, __FILE__, __LINE__);
        return MCP_ERROR_NOT_INITIALIZED;
    }
    
    // Check for memory leaks
    uint64_t allocated = g_total_allocated.load();
    uint64_t freed = g_total_freed.load();
    if (allocated != freed) {
        char msg[256];
        std::snprintf(msg, sizeof(msg), "Memory leak detected: %llu bytes leaked", 
                     (unsigned long long)(allocated - freed));
        set_error(MCP_ERROR, msg, __FILE__, __LINE__);
    }
    
    g_initialized.store(false, std::memory_order_release);
    return MCP_OK;
}

MCP_API mcp_bool_t mcp_ffi_is_initialized(void) MCP_NOEXCEPT {
    return g_initialized.load(std::memory_order_acquire) ? MCP_TRUE : MCP_FALSE;
}

/* ============================================================================
 * Version Information
 * ============================================================================ */

MCP_API const char* mcp_ffi_get_version(void) MCP_NOEXCEPT {
    return VERSION_STRING;
}

MCP_API uint32_t mcp_ffi_get_version_major(void) MCP_NOEXCEPT {
    return VERSION_MAJOR;
}

MCP_API uint32_t mcp_ffi_get_version_minor(void) MCP_NOEXCEPT {
    return VERSION_MINOR;
}

MCP_API uint32_t mcp_ffi_get_version_patch(void) MCP_NOEXCEPT {
    return VERSION_PATCH;
}

/* ============================================================================
 * Error Handling
 * ============================================================================ */

MCP_API const mcp_error_info_t* mcp_get_last_error(void) MCP_NOEXCEPT {
    return g_has_error ? &g_last_error : nullptr;
}

MCP_API void mcp_clear_last_error(void) MCP_NOEXCEPT {
    g_has_error = false;
    std::memset(&g_last_error, 0, sizeof(g_last_error));
}

MCP_API const char* mcp_result_to_string(mcp_result_t result) MCP_NOEXCEPT {
    return get_error_message(result);
}

MCP_API void mcp_tls_set_error(const mcp_error_info_t* error) MCP_NOEXCEPT {
    if (error) {
        g_last_error = *error;
        g_has_error = true;
    }
}

MCP_API const mcp_error_info_t* mcp_tls_get_error(void) MCP_NOEXCEPT {
    return mcp_get_last_error();
}

MCP_API void mcp_tls_clear_error(void) MCP_NOEXCEPT {
    mcp_clear_last_error();
}

/* ============================================================================
 * String Management
 * ============================================================================ */

MCP_API mcp_string_owned_t* mcp_string_create(const char* data, size_t length) MCP_NOEXCEPT {
    if (!g_initialized.load(std::memory_order_acquire)) {
        set_error(MCP_ERROR_NOT_INITIALIZED, nullptr, __FILE__, __LINE__);
        return nullptr;
    }
    
    auto* str = static_cast<mcp_string_owned_t*>(
        g_allocator.alloc(sizeof(mcp_string_owned_t), g_allocator.user_data));
    if (!str) {
        set_error(MCP_ERROR_OUT_OF_MEMORY, nullptr, __FILE__, __LINE__);
        return nullptr;
    }
    
    str->capacity = length + 1;
    str->data = static_cast<char*>(
        g_allocator.alloc(str->capacity, g_allocator.user_data));
    if (!str->data) {
        g_allocator.free(str, g_allocator.user_data);
        set_error(MCP_ERROR_OUT_OF_MEMORY, nullptr, __FILE__, __LINE__);
        return nullptr;
    }
    
    if (data) {
        std::memcpy(str->data, data, length);
    }
    str->data[length] = '\0';
    str->length = length;
    
    g_total_allocated.fetch_add(sizeof(mcp_string_owned_t) + str->capacity);
    g_allocation_count.fetch_add(2);
    
    return str;
}

MCP_API mcp_string_owned_t* mcp_string_create_from_cstr(const char* cstr) MCP_NOEXCEPT {
    if (!cstr) {
        set_error(MCP_ERROR_NULL_POINTER, nullptr, __FILE__, __LINE__);
        return nullptr;
    }
    return mcp_string_create(cstr, std::strlen(cstr));
}

MCP_API void mcp_string_free(mcp_string_owned_t* str) MCP_NOEXCEPT {
    if (!str) return;
    
    if (str->data) {
        g_allocator.free(str->data, g_allocator.user_data);
        g_total_freed.fetch_add(str->capacity);
        g_free_count.fetch_add(1);
    }
    
    g_allocator.free(str, g_allocator.user_data);
    g_total_freed.fetch_add(sizeof(mcp_string_owned_t));
    g_free_count.fetch_add(1);
}

MCP_API mcp_string_owned_t* mcp_string_duplicate(const mcp_string_view_t* view) MCP_NOEXCEPT {
    if (!view) {
        set_error(MCP_ERROR_NULL_POINTER, nullptr, __FILE__, __LINE__);
        return nullptr;
    }
    return mcp_string_create(view->data, view->length);
}

MCP_API mcp_result_t mcp_string_append(mcp_string_owned_t* str, const mcp_string_view_t* suffix) MCP_NOEXCEPT {
    if (!str || !suffix) {
        set_error(MCP_ERROR_NULL_POINTER, nullptr, __FILE__, __LINE__);
        return MCP_ERROR_NULL_POINTER;
    }
    
    size_t new_length = str->length + suffix->length;
    if (new_length + 1 > str->capacity) {
        size_t new_capacity = (new_length + 1) * 2;
        char* new_data = static_cast<char*>(
            g_allocator.realloc(str->data, new_capacity, g_allocator.user_data));
        if (!new_data) {
            set_error(MCP_ERROR_OUT_OF_MEMORY, nullptr, __FILE__, __LINE__);
            return MCP_ERROR_OUT_OF_MEMORY;
        }
        
        g_total_allocated.fetch_add(new_capacity - str->capacity);
        str->data = new_data;
        str->capacity = new_capacity;
    }
    
    std::memcpy(str->data + str->length, suffix->data, suffix->length);
    str->length = new_length;
    str->data[str->length] = '\0';
    
    return MCP_OK;
}

MCP_API void mcp_string_clear(mcp_string_owned_t* str) MCP_NOEXCEPT {
    if (str && str->data) {
        str->data[0] = '\0';
        str->length = 0;
    }
}

MCP_API void mcp_string_reset(mcp_string_owned_t* str) MCP_NOEXCEPT {
    if (str) {
        if (str->data) {
            g_allocator.free(str->data, g_allocator.user_data);
            g_total_freed.fetch_add(str->capacity);
            g_free_count.fetch_add(1);
        }
        str->data = nullptr;
        str->length = 0;
        str->capacity = 0;
    }
}

/* ============================================================================
 * Reference Counting
 * ============================================================================ */

struct mcp_ref_handle_impl {
    mcp_handle_t handle;
    std::atomic<int32_t>* ref_count;
};

MCP_API mcp_ref_handle_t* mcp_ref_create(mcp_handle_t handle) MCP_NOEXCEPT {
    if (!handle) {
        set_error(MCP_ERROR_NULL_POINTER, nullptr, __FILE__, __LINE__);
        return nullptr;
    }
    
    auto* ref = static_cast<mcp_ref_handle_impl*>(
        g_allocator.alloc(sizeof(mcp_ref_handle_impl), g_allocator.user_data));
    if (!ref) {
        set_error(MCP_ERROR_OUT_OF_MEMORY, nullptr, __FILE__, __LINE__);
        return nullptr;
    }
    
    ref->handle = handle;
    ref->ref_count = new std::atomic<int32_t>(1);
    
    g_total_allocated.fetch_add(sizeof(mcp_ref_handle_impl) + sizeof(std::atomic<int32_t>));
    g_allocation_count.fetch_add(2);
    
    return reinterpret_cast<mcp_ref_handle_t*>(ref);
}

MCP_API void mcp_ref_acquire(mcp_ref_handle_t* handle) MCP_NOEXCEPT {
    if (handle) {
        auto* impl = reinterpret_cast<mcp_ref_handle_impl*>(handle);
        if (impl->ref_count) {
            impl->ref_count->fetch_add(1, std::memory_order_relaxed);
        }
    }
}

MCP_API void mcp_ref_release(mcp_ref_handle_t* handle) MCP_NOEXCEPT {
    if (!handle) return;
    
    auto* impl = reinterpret_cast<mcp_ref_handle_impl*>(handle);
    if (impl->ref_count) {
        if (impl->ref_count->fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete impl->ref_count;
            g_allocator.free(impl, g_allocator.user_data);
            g_total_freed.fetch_add(sizeof(mcp_ref_handle_impl) + sizeof(std::atomic<int32_t>));
            g_free_count.fetch_add(2);
        }
    }
}

MCP_API int32_t mcp_ref_count(const mcp_ref_handle_t* handle) MCP_NOEXCEPT {
    if (!handle) return 0;
    
    auto* impl = reinterpret_cast<const mcp_ref_handle_impl*>(handle);
    return impl->ref_count ? impl->ref_count->load(std::memory_order_relaxed) : 0;
}

/* ============================================================================
 * Handle Validation
 * ============================================================================ */

MCP_API mcp_bool_t mcp_handle_is_valid(mcp_handle_t handle) MCP_NOEXCEPT {
    if (!handle) return MCP_FALSE;
    
    // Check magic number
    auto* header = reinterpret_cast<mcp_handle_header_t*>(handle);
    return (header->magic == MCP_HANDLE_MAGIC) ? MCP_TRUE : MCP_FALSE;
}

MCP_API uint32_t mcp_handle_get_type(mcp_handle_t handle) MCP_NOEXCEPT {
    if (!handle) return 0;
    
    auto* header = reinterpret_cast<mcp_handle_header_t*>(handle);
    return (header->magic == MCP_HANDLE_MAGIC) ? header->type_id : 0;
}

MCP_API uint32_t mcp_handle_get_flags(mcp_handle_t handle) MCP_NOEXCEPT {
    if (!handle) return 0;
    
    auto* header = reinterpret_cast<mcp_handle_header_t*>(handle);
    return (header->magic == MCP_HANDLE_MAGIC) ? header->flags : 0;
}

/* ============================================================================
 * Memory Pool Management
 * ============================================================================ */

struct mcp_mempool_impl {
    uint8_t* memory;
    size_t size;
    size_t used;
    mcp_memory_stats_t stats;
    std::mutex mutex;
};

MCP_API mcp_mempool_t mcp_mempool_create(size_t initial_size) MCP_NOEXCEPT {
    auto* pool = new mcp_mempool_impl;
    pool->size = initial_size;
    pool->memory = static_cast<uint8_t*>(
        g_allocator.alloc(initial_size, g_allocator.user_data));
    if (!pool->memory) {
        delete pool;
        set_error(MCP_ERROR_OUT_OF_MEMORY, nullptr, __FILE__, __LINE__);
        return nullptr;
    }
    
    pool->used = 0;
    pool->stats = {};
    pool->stats.total_allocated = initial_size;
    
    g_total_allocated.fetch_add(sizeof(mcp_mempool_impl) + initial_size);
    g_allocation_count.fetch_add(2);
    
    return pool;
}

MCP_API void mcp_mempool_destroy(mcp_mempool_t pool) MCP_NOEXCEPT {
    if (!pool) return;
    
    if (pool->memory) {
        g_allocator.free(pool->memory, g_allocator.user_data);
        g_total_freed.fetch_add(pool->size);
        g_free_count.fetch_add(1);
    }
    
    g_total_freed.fetch_add(sizeof(mcp_mempool_impl));
    g_free_count.fetch_add(1);
    delete pool;
}

MCP_API void* mcp_mempool_alloc(mcp_mempool_t pool, size_t size) MCP_NOEXCEPT {
    if (!pool) {
        set_error(MCP_ERROR_NULL_POINTER, nullptr, __FILE__, __LINE__);
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(pool->mutex);
    
    // Align to 8 bytes
    size = (size + 7) & ~7;
    
    if (pool->used + size > pool->size) {
        set_error(MCP_ERROR_OUT_OF_MEMORY, "Pool exhausted", __FILE__, __LINE__);
        return nullptr;
    }
    
    void* ptr = pool->memory + pool->used;
    pool->used += size;
    
    pool->stats.current_usage = pool->used;
    if (pool->used > pool->stats.peak_usage) {
        pool->stats.peak_usage = pool->used;
    }
    pool->stats.allocation_count++;
    
    return ptr;
}

MCP_API void mcp_mempool_reset(mcp_mempool_t pool) MCP_NOEXCEPT {
    if (!pool) return;
    
    std::lock_guard<std::mutex> lock(pool->mutex);
    pool->used = 0;
    pool->stats.current_usage = 0;
    pool->stats.free_count = pool->stats.allocation_count;
}

MCP_API mcp_memory_stats_t mcp_mempool_get_stats(mcp_mempool_t pool) MCP_NOEXCEPT {
    if (!pool) return {};
    
    std::lock_guard<std::mutex> lock(pool->mutex);
    return pool->stats;
}

/* ============================================================================
 * Batch Operations
 * ============================================================================ */

MCP_API void mcp_batch_free(void** objects, size_t count, void (*free_fn)(void*)) MCP_NOEXCEPT {
    if (!objects || !free_fn) return;
    
    for (size_t i = 0; i < count; ++i) {
        if (objects[i]) {
            free_fn(objects[i]);
        }
    }
}

MCP_API mcp_result_t mcp_batch_validate(mcp_handle_t* handles, size_t count, mcp_bool_t* results) MCP_NOEXCEPT {
    if (!handles || !results) {
        set_error(MCP_ERROR_NULL_POINTER, nullptr, __FILE__, __LINE__);
        return MCP_ERROR_NULL_POINTER;
    }
    
    for (size_t i = 0; i < count; ++i) {
        results[i] = mcp_handle_is_valid(handles[i]);
    }
    
    return MCP_OK;
}

} // extern "C"