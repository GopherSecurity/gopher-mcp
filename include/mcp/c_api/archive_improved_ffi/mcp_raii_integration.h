/**
 * @file mcp_raii_integration.h
 * @brief Integration layer between FFI-safe C API and existing RAII utilities
 *
 * This header provides the bridge between our improved FFI-safe C API types
 * and the existing mcp_raii.h utilities, enabling safe and efficient resource
 * management with full RAII support.
 *
 * Features:
 * - Specialized deleters for all MCP C API types
 * - Integration with ResourceGuard and AllocationTransaction
 * - Thread-safe reference counting using RAII patterns
 * - Memory pool integration for batch operations
 * - Production debugging with ResourceTracker integration
 */

#ifndef MCP_RAII_INTEGRATION_H
#define MCP_RAII_INTEGRATION_H

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "mcp_c_types_improved.h"
#include "mcp_ffi_core.h"
#include "mcp_raii.h"

namespace mcp {
namespace raii {

/* ============================================================================
 * Specialized Deleters for MCP C API Types
 * ============================================================================
 */

/**
 * Generic deleter template for MCP types
 * Specializations provide type-specific cleanup
 */
template <typename T>
struct mcp_deleter {
  void operator()(T* ptr) const noexcept;
};

/* Specializations for each MCP type */
template <>
struct mcp_deleter<mcp_string_owned_t> {
  void operator()(mcp_string_owned_t* str) const noexcept {
    if (str) {
      MCP_RAII_UNTRACK_RESOURCE(str);
      mcp_string_free(str);
    }
  }
};

template <>
struct mcp_deleter<std::remove_pointer_t<mcp_list_t>> {
  void operator()(std::remove_pointer_t<mcp_list_t>* list) const noexcept {
    if (list) {
      MCP_RAII_UNTRACK_RESOURCE(list);
      mcp_list_free(list);
    }
  }
};

template <>
struct mcp_deleter<std::remove_pointer_t<mcp_map_t>> {
  void operator()(std::remove_pointer_t<mcp_map_t>* map) const noexcept {
    if (map) {
      MCP_RAII_UNTRACK_RESOURCE(map);
      mcp_map_free(map);
    }
  }
};

template <>
struct mcp_deleter<std::remove_pointer_t<mcp_json_t>> {
  void operator()(std::remove_pointer_t<mcp_json_t>* json) const noexcept {
    if (json) {
      MCP_RAII_UNTRACK_RESOURCE(json);
      mcp_json_free(json);
    }
  }
};

template <>
struct mcp_deleter<std::remove_pointer_t<mcp_request_t>> {
  void operator()(std::remove_pointer_t<mcp_request_t>* req) const noexcept {
    if (req) {
      MCP_RAII_UNTRACK_RESOURCE(req);
      mcp_request_free(req);
    }
  }
};

template <>
struct mcp_deleter<std::remove_pointer_t<mcp_response_t>> {
  void operator()(std::remove_pointer_t<mcp_response_t>* resp) const noexcept {
    if (resp) {
      MCP_RAII_UNTRACK_RESOURCE(resp);
      mcp_response_free(resp);
    }
  }
};

template <>
struct mcp_deleter<std::remove_pointer_t<mcp_notification_t>> {
  void operator()(
      std::remove_pointer_t<mcp_notification_t>* notif) const noexcept {
    if (notif) {
      MCP_RAII_UNTRACK_RESOURCE(notif);
      mcp_notification_free(notif);
    }
  }
};

template <>
struct mcp_deleter<std::remove_pointer_t<mcp_error_t>> {
  void operator()(std::remove_pointer_t<mcp_error_t>* err) const noexcept {
    if (err) {
      MCP_RAII_UNTRACK_RESOURCE(err);
      mcp_error_free(err);
    }
  }
};

/* ============================================================================
 * Reference Counting with RAII
 * ============================================================================
 */

/**
 * Thread-safe reference-counted wrapper for MCP types
 * Integrates with ResourceGuard for automatic cleanup
 */
template <typename T>
class RefCounted {
 public:
  using element_type = T;
  using deleter_type = mcp_deleter<T>;

  RefCounted() noexcept : ptr_(nullptr), ref_count_(nullptr) {}

  explicit RefCounted(T* ptr) noexcept
      : ptr_(ptr), ref_count_(ptr ? new std::atomic<int32_t>(1) : nullptr) {
    if (ptr_) {
      MCP_RAII_TRACK_RESOURCE(ptr_, typeid(T).name());
    }
  }

  RefCounted(const RefCounted& other) noexcept
      : ptr_(other.ptr_), ref_count_(other.ref_count_) {
    if (ref_count_) {
      ref_count_->fetch_add(1, std::memory_order_relaxed);
    }
  }

  RefCounted(RefCounted&& other) noexcept
      : ptr_(other.ptr_), ref_count_(other.ref_count_) {
    other.ptr_ = nullptr;
    other.ref_count_ = nullptr;
  }

  RefCounted& operator=(const RefCounted& other) noexcept {
    if (this != &other) {
      release();
      ptr_ = other.ptr_;
      ref_count_ = other.ref_count_;
      if (ref_count_) {
        ref_count_->fetch_add(1, std::memory_order_relaxed);
      }
    }
    return *this;
  }

  RefCounted& operator=(RefCounted&& other) noexcept {
    if (this != &other) {
      release();
      ptr_ = other.ptr_;
      ref_count_ = other.ref_count_;
      other.ptr_ = nullptr;
      other.ref_count_ = nullptr;
    }
    return *this;
  }

  ~RefCounted() noexcept { release(); }

  T* get() const noexcept { return ptr_; }
  T* operator->() const noexcept { return ptr_; }
  T& operator*() const noexcept { return *ptr_; }
  explicit operator bool() const noexcept { return ptr_ != nullptr; }

  int32_t use_count() const noexcept {
    return ref_count_ ? ref_count_->load(std::memory_order_relaxed) : 0;
  }

  void reset(T* ptr = nullptr) noexcept {
    release();
    if (ptr) {
      ptr_ = ptr;
      ref_count_ = new std::atomic<int32_t>(1);
      MCP_RAII_TRACK_RESOURCE(ptr_, typeid(T).name());
    }
  }

 private:
  void release() noexcept {
    if (ref_count_) {
      if (ref_count_->fetch_sub(1, std::memory_order_acq_rel) == 1) {
        deleter_type()(ptr_);
        delete ref_count_;
      }
      ptr_ = nullptr;
      ref_count_ = nullptr;
    }
  }

  T* ptr_;
  std::atomic<int32_t>* ref_count_;
};

/* ============================================================================
 * Integration with ResourceGuard
 * ============================================================================
 */

/**
 * Factory functions for creating ResourceGuard with MCP types
 */
template <typename T>
inline auto make_mcp_guard(T* resource) {
  return make_resource_guard(resource, mcp_deleter<T>());
}

inline auto make_string_guard(const char* data, size_t length) {
  auto* str = mcp_string_create(data, length);
  if (str) {
    MCP_RAII_TRACK_RESOURCE(str, "mcp_string_owned_t");
  }
  return make_resource_guard(str, mcp_deleter<mcp_string_owned_t>());
}

inline auto make_list_guard(mcp_type_id_t element_type) {
  auto list = mcp_list_create(element_type);
  if (list) {
    MCP_RAII_TRACK_RESOURCE(list, "mcp_list_t");
  }
  return make_resource_guard(list, [](mcp_list_t l) {
    MCP_RAII_UNTRACK_RESOURCE(l);
    mcp_list_free(l);
  });
}

inline auto make_map_guard(mcp_type_id_t value_type) {
  auto map = mcp_map_create(value_type);
  if (map) {
    MCP_RAII_TRACK_RESOURCE(map, "mcp_map_t");
  }
  return make_resource_guard(map, [](mcp_map_t m) {
    MCP_RAII_UNTRACK_RESOURCE(m);
    mcp_map_free(m);
  });
}

inline auto make_json_guard(mcp_json_t json) {
  if (json) {
    MCP_RAII_TRACK_RESOURCE(json, "mcp_json_t");
  }
  return make_resource_guard(json, [](mcp_json_t j) {
    MCP_RAII_UNTRACK_RESOURCE(j);
    mcp_json_free(j);
  });
}

/* ============================================================================
 * Integration with AllocationTransaction
 * ============================================================================
 */

/**
 * Extended AllocationTransaction for MCP types
 */
class MCPAllocationTransaction : public AllocationTransaction {
 public:
  MCPAllocationTransaction() = default;

  /* Track MCP-specific resources */
  void track_string(mcp_string_owned_t* str) {
    if (str) {
      MCP_RAII_TRACK_RESOURCE(str, "mcp_string_owned_t");
      track(str, [](void* p) {
        MCP_RAII_UNTRACK_RESOURCE(p);
        mcp_string_free(static_cast<mcp_string_owned_t*>(p));
      });
    }
  }

  void track_list(mcp_list_t list) {
    if (list) {
      MCP_RAII_TRACK_RESOURCE(list, "mcp_list_t");
      track(list, [](void* p) {
        MCP_RAII_UNTRACK_RESOURCE(p);
        mcp_list_free(static_cast<mcp_list_t>(p));
      });
    }
  }

  void track_map(mcp_map_t map) {
    if (map) {
      MCP_RAII_TRACK_RESOURCE(map, "mcp_map_t");
      track(map, [](void* p) {
        MCP_RAII_UNTRACK_RESOURCE(p);
        mcp_map_free(static_cast<mcp_map_t>(p));
      });
    }
  }

  void track_json(mcp_json_t json) {
    if (json) {
      MCP_RAII_TRACK_RESOURCE(json, "mcp_json_t");
      track(json, [](void* p) {
        MCP_RAII_UNTRACK_RESOURCE(p);
        mcp_json_free(static_cast<mcp_json_t>(p));
      });
    }
  }

  void track_request(mcp_request_t request) {
    if (request) {
      MCP_RAII_TRACK_RESOURCE(request, "mcp_request_t");
      track(request, [](void* p) {
        MCP_RAII_UNTRACK_RESOURCE(p);
        mcp_request_free(static_cast<mcp_request_t>(p));
      });
    }
  }

  void track_response(mcp_response_t response) {
    if (response) {
      MCP_RAII_TRACK_RESOURCE(response, "mcp_response_t");
      track(response, [](void* p) {
        MCP_RAII_UNTRACK_RESOURCE(p);
        mcp_response_free(static_cast<mcp_response_t>(p));
      });
    }
  }
};

/* ============================================================================
 * Memory Pool Integration with RAII
 * ============================================================================
 */

/**
 * RAII wrapper for memory pool
 * Automatically resets pool on destruction
 */
class MemoryPoolGuard {
 public:
  explicit MemoryPoolGuard(size_t initial_size)
      : pool_(mcp_mempool_create(initial_size)) {
    if (pool_) {
      MCP_RAII_TRACK_RESOURCE(pool_, "mcp_mempool_t");
    }
  }

  MemoryPoolGuard(const MemoryPoolGuard&) = delete;
  MemoryPoolGuard& operator=(const MemoryPoolGuard&) = delete;

  MemoryPoolGuard(MemoryPoolGuard&& other) noexcept : pool_(other.pool_) {
    other.pool_ = nullptr;
  }

  MemoryPoolGuard& operator=(MemoryPoolGuard&& other) noexcept {
    if (this != &other) {
      reset();
      pool_ = other.pool_;
      other.pool_ = nullptr;
    }
    return *this;
  }

  ~MemoryPoolGuard() noexcept { reset(); }

  mcp_mempool_t get() const noexcept { return pool_; }

  void* allocate(size_t size) {
    return pool_ ? mcp_mempool_alloc(pool_, size) : nullptr;
  }

  void reset_pool() noexcept {
    if (pool_) {
      mcp_mempool_reset(pool_);
    }
  }

  mcp_memory_stats_t get_stats() const noexcept {
    return pool_ ? mcp_mempool_get_stats(pool_) : mcp_memory_stats_t{};
  }

 private:
  void reset() noexcept {
    if (pool_) {
      MCP_RAII_UNTRACK_RESOURCE(pool_);
      mcp_mempool_destroy(pool_);
      pool_ = nullptr;
    }
  }

  mcp_mempool_t pool_;
};

/* ============================================================================
 * Scoped Lock Integration
 * ============================================================================
 */

/**
 * RAII wrapper for scoped locks
 */
class ScopedLockGuard {
 public:
  explicit ScopedLockGuard(const char* resource_name)
      : lock_(mcp_scoped_lock_acquire(resource_name)) {
    if (lock_) {
      MCP_RAII_TRACK_RESOURCE(lock_, "mcp_scoped_lock_t");
    }
  }

  ScopedLockGuard(const ScopedLockGuard&) = delete;
  ScopedLockGuard& operator=(const ScopedLockGuard&) = delete;

  ScopedLockGuard(ScopedLockGuard&& other) noexcept : lock_(other.lock_) {
    other.lock_ = nullptr;
  }

  ~ScopedLockGuard() noexcept {
    if (lock_) {
      MCP_RAII_UNTRACK_RESOURCE(lock_);
      mcp_scoped_lock_release(lock_);
    }
  }

 private:
  mcp_scoped_lock_t lock_;
};

/* ============================================================================
 * Batch Operations with RAII
 * ============================================================================
 */

/**
 * RAII wrapper for batch operations
 * Automatically executes cleanup on destruction
 */
class BatchOperationGuard {
 public:
  BatchOperationGuard() = default;

  template <typename T>
  void add(T* resource) {
    if (resource) {
      MCP_RAII_TRACK_RESOURCE(resource, typeid(T).name());
      operations_.push_back({resource, [](void* p) {
                               MCP_RAII_UNTRACK_RESOURCE(p);
                               mcp_deleter<T>()(static_cast<T*>(p));
                             }});
    }
  }

  void add_custom(void* resource, void (*deleter)(void*)) {
    if (resource && deleter) {
      operations_.push_back({resource, deleter});
    }
  }

  void execute_and_clear() noexcept {
    if (!operations_.empty()) {
      std::vector<mcp_batch_operation_t> c_ops;
      c_ops.reserve(operations_.size());

      for (const auto& op : operations_) {
        c_ops.push_back({op.resource, op.deleter});
      }

      mcp_batch_execute(c_ops.data(), c_ops.size());
      operations_.clear();
    }
  }

  void clear() noexcept { operations_.clear(); }

  ~BatchOperationGuard() noexcept { execute_and_clear(); }

 private:
  struct Operation {
    void* resource;
    void (*deleter)(void*);
  };

  std::vector<Operation> operations_;
};

/* ============================================================================
 * Error Context Integration
 * ============================================================================
 */

/**
 * RAII wrapper for error context
 * Automatically clears error on destruction
 */
class ErrorContextGuard {
 public:
  ErrorContextGuard() = default;

  ~ErrorContextGuard() noexcept { mcp_clear_last_error(); }

  const mcp_error_info_t* get() const noexcept { return mcp_get_last_error(); }

  bool has_error() const noexcept {
    auto* error = get();
    return error && error->code != MCP_OK;
  }

  std::string get_message() const {
    auto* error = get();
    return error ? std::string(error->message) : std::string();
  }
};

/* ============================================================================
 * Utility Functions
 * ============================================================================
 */

/**
 * Create a ref-counted wrapper from a raw MCP resource
 */
template <typename T>
inline RefCounted<T> make_ref_counted(T* resource) {
  return RefCounted<T>(resource);
}

/**
 * Create a unique_ptr with MCP deleter
 */
template <typename T>
using mcp_unique_ptr = std::unique_ptr<T, mcp_deleter<T>>;

template <typename T>
inline mcp_unique_ptr<T> make_mcp_unique(T* resource) {
  return mcp_unique_ptr<T>(resource);
}

/**
 * Validate and wrap a resource in RAII guard
 */
template <typename T>
inline auto validate_and_guard(T* resource, mcp_type_id_t expected_type) {
  if (resource && mcp_validate_handle(reinterpret_cast<mcp_handle_t>(resource),
                                      expected_type)) {
    return make_mcp_guard(resource);
  }
  return decltype(make_mcp_guard(resource))(nullptr, mcp_deleter<T>());
}

}  // namespace raii
}  // namespace mcp

#endif /* MCP_RAII_INTEGRATION_H */