/**
 * @file mcp_c_memory_impl.cc
 * @brief Implementation of memory management and error handling for MCP C API
 *
 * This file implements memory management utilities, error handling,
 * and resource tracking for the MCP C API.
 * Uses RAII for memory safety.
 */

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>
#include <vector>

#include "mcp/c_api/mcp_c_memory.h"
#include "mcp/c_api/mcp_c_raii.h"
#include "mcp/c_api/mcp_c_types.h"
#include "mcp/filter/core_filter_factories.h"

namespace {

/* ============================================================================
 * Global State
 * ============================================================================
 */

struct GlobalState {
  mcp_allocator_t allocator;
  bool custom_allocator = false;
  bool initialized = false;
  mcp_error_handler_t error_handler = nullptr;
  void* error_handler_data = nullptr;
  std::mutex mutex;

#ifdef MCP_DEBUG
  bool tracking_enabled = false;
  std::unordered_map<mcp_type_id_t, std::atomic<size_t>> resource_counts;
#endif
};

static GlobalState g_state;

/* ============================================================================
 * Thread-local Error State
 * ============================================================================
 */

thread_local mcp_error_info_t g_last_error = {};
thread_local bool g_has_error = false;

void set_error(mcp_result_t code,
               const char* message,
               const char* file = __FILE__,
               int line = __LINE__) {
  g_last_error.code = code;
  if (message) {
    strncpy(g_last_error.message, message, sizeof(g_last_error.message) - 1);
    g_last_error.message[sizeof(g_last_error.message) - 1] = '\0';
  }
  if (file) {
    strncpy(g_last_error.file, file, sizeof(g_last_error.file) - 1);
    g_last_error.file[sizeof(g_last_error.file) - 1] = '\0';
  }
  g_last_error.line = line;
  g_has_error = true;

  // Call error handler if set
  if (g_state.error_handler) {
    g_state.error_handler(&g_last_error, g_state.error_handler_data);
  }
}

}  // anonymous namespace

extern "C" {

/* ============================================================================
 * Library Initialization
 * ============================================================================
 */

MCP_API mcp_result_t mcp_init(const mcp_allocator_t* allocator) MCP_NOEXCEPT {
  fprintf(stderr, "[mcp_init] Entry\n");
  fflush(stderr);

  fprintf(stderr, "[mcp_init] About to acquire mutex lock\n");
  fflush(stderr);
  std::lock_guard<std::mutex> lock(g_state.mutex);
  fprintf(stderr, "[mcp_init] Mutex lock acquired\n");
  fflush(stderr);

  if (g_state.initialized) {
    fprintf(stderr, "[mcp_init] Already initialized, returning OK\n");
    fflush(stderr);
    return MCP_OK;  // Already initialized
  }

  fprintf(stderr, "[mcp_init] Setting up allocator\n");
  fflush(stderr);
  if (allocator) {
    g_state.allocator = *allocator;
    g_state.custom_allocator = true;
  } else {
    // Use default allocator
    g_state.allocator.alloc = [](size_t size, void*) -> void* {
      return std::malloc(size);
    };
    g_state.allocator.realloc = [](void* ptr, size_t size, void*) -> void* {
      return std::realloc(ptr, size);
    };
    g_state.allocator.free = [](void* ptr, void*) { std::free(ptr); };
    g_state.allocator.user_data = nullptr;
    g_state.custom_allocator = false;
  }

  // Register all core filters for static linking support
  fprintf(stderr, "[mcp_init] Registering core filters for static linking\n");
  fflush(stderr);
  {
    // Use std::call_once to ensure filters are only registered once
    static std::once_flag filter_init_flag;
    std::call_once(filter_init_flag, []() {
      try {
        // Register all core filters to ensure they're linked
        mcp::filter::registerAllCoreFilters();
        fprintf(stderr, "[mcp_init] Core filters registered successfully\n");
        fflush(stderr);
      } catch (const std::exception& e) {
        fprintf(stderr, "[mcp_init] WARNING: Failed to register filters: %s\n",
                e.what());
        fflush(stderr);
        // Continue anyway - filters might be registered via other means
      }
    });
  }

  fprintf(stderr, "[mcp_init] Setting initialized flag\n");
  fflush(stderr);
  g_state.initialized = true;
  fprintf(stderr, "[mcp_init] Exit - returning OK\n");
  fflush(stderr);
  return MCP_OK;
}

MCP_API void mcp_shutdown(void) MCP_NOEXCEPT {
  std::lock_guard<std::mutex> lock(g_state.mutex);

  if (!g_state.initialized) {
    return;
  }

#ifdef MCP_DEBUG
  if (g_state.tracking_enabled) {
    mcp_check_leaks();
  }
#endif

  g_state.initialized = false;
  g_state.custom_allocator = false;
  g_state.error_handler = nullptr;
  g_state.error_handler_data = nullptr;
}

MCP_API mcp_bool_t mcp_is_initialized(void) MCP_NOEXCEPT {
  std::lock_guard<std::mutex> lock(g_state.mutex);
  return g_state.initialized ? MCP_TRUE : MCP_FALSE;
}

/* ============================================================================
 * Error Handling
 * ============================================================================
 */

MCP_API const mcp_error_info_t* mcp_get_last_error(void) MCP_NOEXCEPT {
  return g_has_error ? &g_last_error : nullptr;
}

MCP_API void mcp_clear_last_error(void) MCP_NOEXCEPT {
  g_has_error = false;
  memset(&g_last_error, 0, sizeof(g_last_error));
}

MCP_API void mcp_set_error_handler(mcp_error_handler_t handler,
                                   void* user_data) MCP_NOEXCEPT {
  std::lock_guard<std::mutex> lock(g_state.mutex);
  g_state.error_handler = handler;
  g_state.error_handler_data = user_data;
}

/* ============================================================================
 * Memory Pool Management
 * ============================================================================
 */

struct mcp_memory_pool_impl {
  std::vector<uint8_t> buffer;
  size_t used = 0;
  size_t allocation_count = 0;
  std::vector<void*> allocations;
};

MCP_API mcp_memory_pool_t mcp_memory_pool_create(size_t initial_size)
    MCP_NOEXCEPT {
  try {
    auto pool = std::make_unique<mcp_memory_pool_impl>();
    pool->buffer.reserve(initial_size);
    return pool.release();
  } catch (...) {
    set_error(MCP_ERROR_OUT_OF_MEMORY, "Failed to create memory pool");
    return nullptr;
  }
}

MCP_API void mcp_memory_pool_destroy(mcp_memory_pool_t pool) MCP_NOEXCEPT {
  if (pool) {
    delete pool;
  }
}

MCP_API void* mcp_memory_pool_alloc(mcp_memory_pool_t pool,
                                    size_t size) MCP_NOEXCEPT {
  if (!pool || size == 0) {
    return nullptr;
  }

  // Align to 8 bytes
  size = (size + 7) & ~7;

  if (pool->used + size > pool->buffer.capacity()) {
    // Need to grow the pool
    try {
      pool->buffer.reserve(pool->buffer.capacity() * 2 + size);
    } catch (...) {
      set_error(MCP_ERROR_OUT_OF_MEMORY, "Memory pool exhausted");
      return nullptr;
    }
  }

  void* ptr = pool->buffer.data() + pool->used;
  pool->used += size;
  pool->allocation_count++;
  pool->allocations.push_back(ptr);

  return ptr;
}

MCP_API void mcp_memory_pool_reset(mcp_memory_pool_t pool) MCP_NOEXCEPT {
  if (pool) {
    pool->used = 0;
    pool->allocation_count = 0;
    pool->allocations.clear();
  }
}

MCP_API void mcp_memory_pool_stats(mcp_memory_pool_t pool,
                                   size_t* used_bytes,
                                   size_t* total_bytes,
                                   size_t* allocation_count) MCP_NOEXCEPT {
  if (!pool) {
    return;
  }

  if (used_bytes)
    *used_bytes = pool->used;
  if (total_bytes)
    *total_bytes = pool->buffer.capacity();
  if (allocation_count)
    *allocation_count = pool->allocation_count;
}

/* ============================================================================
 * Batch Operations
 * ============================================================================
 */

MCP_API mcp_result_t mcp_batch_execute(const mcp_batch_operation_t* operations,
                                       size_t count) MCP_NOEXCEPT {
  if (!operations || count == 0) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  // Execute all operations
  for (size_t i = 0; i < count; ++i) {
    // TODO: Implement batch operations based on type
    // This would dispatch to appropriate functions based on operation type
    const_cast<mcp_batch_operation_t&>(operations[i]).result = MCP_OK;
  }

  return MCP_OK;
}

/* ============================================================================
 * Resource Tracking (Debug Mode)
 * ============================================================================
 */

#ifdef MCP_DEBUG

MCP_API void mcp_enable_resource_tracking(mcp_bool_t enable) MCP_NOEXCEPT {
  std::lock_guard<std::mutex> lock(g_state.mutex);
  g_state.tracking_enabled = (enable == MCP_TRUE);
}

MCP_API size_t mcp_get_resource_count(mcp_type_id_t type) MCP_NOEXCEPT {
  std::lock_guard<std::mutex> lock(g_state.mutex);
  auto it = g_state.resource_counts.find(type);
  return (it != g_state.resource_counts.end()) ? it->second.load() : 0;
}

MCP_API void mcp_print_resource_report(void) MCP_NOEXCEPT {
  std::lock_guard<std::mutex> lock(g_state.mutex);

  printf("=== MCP Resource Report ===\n");
  for (const auto& [type, count] : g_state.resource_counts) {
    if (count > 0) {
      printf("  Type %d: %zu active\n", static_cast<int>(type), count.load());
    }
  }
  printf("===========================\n");
}

MCP_API mcp_bool_t mcp_check_leaks(void) MCP_NOEXCEPT {
  std::lock_guard<std::mutex> lock(g_state.mutex);

  for (const auto& [type, count] : g_state.resource_counts) {
    if (count > 0) {
      return MCP_TRUE;
    }
  }
  return MCP_FALSE;
}

#endif /* MCP_DEBUG */

/* ============================================================================
 * Memory Utilities
 * ============================================================================
 */

MCP_API char* mcp_strdup(const char* str) MCP_NOEXCEPT {
  if (!str) {
    return nullptr;
  }

  size_t len = strlen(str);
  char* result = static_cast<char*>(mcp_malloc(len + 1));
  if (result) {
    strcpy(result, str);
  }
  return result;
}

MCP_API void mcp_string_free(char* str) MCP_NOEXCEPT { mcp_free(str); }

/* ============================================================================
 * String Buffer Helpers (for MCP_TYPE_STRING handles)
 * ============================================================================
 */

struct mcp_string_buffer_impl {
  char* data{nullptr};
  size_t length{0};
};

MCP_API mcp_string_buffer_t* mcp_string_dup(mcp_string_t str) MCP_NOEXCEPT {
  // Allocate outer box that will be treated as the handle
  auto* box = static_cast<mcp_string_buffer_t*>(
      mcp_malloc(sizeof(mcp_string_buffer_t)));
  if (!box) {
    return nullptr;
  }

  auto* impl = new (std::nothrow) mcp_string_buffer_impl();
  if (!impl) {
    mcp_free(box);
    return nullptr;
  }

  // Duplicate string data
  if (str.data && str.length > 0) {
    impl->data = static_cast<char*>(mcp_malloc(str.length + 1));
    if (!impl->data) {
      delete impl;
      mcp_free(box);
      return nullptr;
    }
    std::memcpy(impl->data, str.data, str.length);
    impl->data[str.length] = '\0';
    impl->length = str.length;
  } else {
    // Empty string
    impl->data = nullptr;
    impl->length = 0;
  }

  *box = impl;
  return box;
}

MCP_API void mcp_string_buffer_free(mcp_string_buffer_t* buffer) MCP_NOEXCEPT {
  if (!buffer) {
    return;
  }
  mcp_string_buffer_t impl = *buffer;
  if (impl) {
    if (impl->data) {
      mcp_free(impl->data);
      impl->data = nullptr;
    }
    delete impl;
  }
  mcp_free(buffer);
}

MCP_API void* mcp_malloc(size_t size) MCP_NOEXCEPT {
  if (!g_state.initialized) {
    // Use default malloc if not initialized
    return std::malloc(size);
  }

  return g_state.allocator.alloc(size, g_state.allocator.user_data);
}

MCP_API void* mcp_realloc(void* ptr, size_t new_size) MCP_NOEXCEPT {
  if (!g_state.initialized) {
    // Use default realloc if not initialized
    return std::realloc(ptr, new_size);
  }

  return g_state.allocator.realloc(ptr, new_size, g_state.allocator.user_data);
}

MCP_API void mcp_free(void* ptr) MCP_NOEXCEPT {
  if (!ptr) {
    return;
  }

  if (!g_state.initialized) {
    // Use default free if not initialized
    std::free(ptr);
    return;
  }

  g_state.allocator.free(ptr, g_state.allocator.user_data);
}

}  // extern "C"
