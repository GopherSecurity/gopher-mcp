/**
 * @file mcp_guard_transaction_impl.cc
 * @brief FFI-safe RAII implementation of guard and transaction functions
 * Uses opaque handles (uintptr_t) for FFI safety and enforces RAII semantics
 */

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "mcp/c_api/mcp_c_api.h"
#include "mcp/c_api/mcp_raii.h"

#include "handle_manager.h"

// Forward declare the C API function
extern "C" {
void mcp_json_free(mcp_json_value_t);
}

namespace mcp {
namespace c_api {

// Guard implementation with shared_ptr for automatic cleanup
class GuardImpl {
 public:
  GuardImpl(void* res, mcp_type_id_t t, mcp_guard_cleanup_fn fn = nullptr)
      : resource(res), type(t), cleanup_fn(fn), released(false) {}

  ~GuardImpl() { cleanup(); }

  void* release() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (released)
      return nullptr;

    released = true;
    void* res = resource;
    resource = nullptr;
    return res;
  }

  void* get() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return released ? nullptr : resource;
  }

  bool is_valid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !released && resource != nullptr;
  }

 private:
  void cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!released && resource) {
      if (cleanup_fn) {
        cleanup_fn(resource);
      } else {
        cleanup_by_type();
      }
      resource = nullptr;
    }
  }

  void cleanup_by_type() {
    // Default cleanup for known types
    switch (type) {
      case MCP_TYPE_STRING:
        if (resource) {
          mcp_string_t* str = static_cast<mcp_string_t*>(resource);
          if (str->data) {
            free(const_cast<char*>(str->data));
          }
          free(str);
        }
        break;
      case MCP_TYPE_JSON:
        // Use the C API function
        if (resource) {
          mcp_json_free(static_cast<mcp_json_value_t>(resource));
        }
        break;
      default:
        // Generic free for unknown types
        free(resource);
        break;
    }
  }

  void* resource;
  mcp_type_id_t type;
  mcp_guard_cleanup_fn cleanup_fn;
  bool released;
  mutable std::mutex mutex_;
};

// Transaction implementation with RAII enforcement
class TransactionImpl {
 public:
  TransactionImpl() : committed(false) {
    // Default options
    options.auto_rollback = MCP_TRUE;
    options.strict_ordering = MCP_FALSE;
    options.max_resources = 0;  // unlimited
  }

  explicit TransactionImpl(const mcp_transaction_opts_t* opts)
      : committed(false) {
    if (opts) {
      options = *opts;
    } else {
      options.auto_rollback = MCP_TRUE;
      options.strict_ordering = MCP_FALSE;
      options.max_resources = 0;
    }
  }

  ~TransactionImpl() {
    if (!committed && options.auto_rollback) {
      rollback();
    }
  }

  mcp_result_t add(void* resource,
                   mcp_type_id_t type,
                   mcp_guard_cleanup_fn cleanup_fn = nullptr) {
    std::lock_guard<std::mutex> lock(mutex);

    if (committed) {
      return MCP_ERROR_INVALID_STATE;
    }

    if (options.max_resources > 0 &&
        resources.size() >= options.max_resources) {
      return MCP_ERROR_RESOURCE_EXHAUSTED;
    }

    resources.emplace_back(resource, type, cleanup_fn);
    return MCP_OK;
  }

  mcp_result_t commit() {
    std::lock_guard<std::mutex> lock(mutex);
    if (committed) {
      return MCP_ERROR_INVALID_STATE;
    }

    committed = true;
    // Resources are now owned by caller - clear without cleanup
    resources.clear();
    return MCP_OK;
  }

  void rollback() {
    std::lock_guard<std::mutex> lock(mutex);

    if (committed)
      return;

    // Clean up resources in reverse order if strict ordering
    if (options.strict_ordering) {
      for (auto it = resources.rbegin(); it != resources.rend(); ++it) {
        cleanup_resource(*it);
      }
    } else {
      for (auto& entry : resources) {
        cleanup_resource(entry);
      }
    }

    resources.clear();
    committed = true;  // Prevent double cleanup
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex));
    return resources.size();
  }

 private:
  struct ResourceEntry {
    void* resource;
    mcp_type_id_t type;
    mcp_guard_cleanup_fn cleanup_fn;

    ResourceEntry(void* res, mcp_type_id_t t, mcp_guard_cleanup_fn fn = nullptr)
        : resource(res), type(t), cleanup_fn(fn) {}
  };

  void cleanup_resource(const ResourceEntry& entry) {
    if (!entry.resource)
      return;

    if (entry.cleanup_fn) {
      entry.cleanup_fn(entry.resource);
    } else {
      // Use guard's cleanup logic
      GuardImpl guard(entry.resource, entry.type, nullptr);
      // Destructor will handle cleanup
    }
  }

  std::vector<ResourceEntry> resources;
  mcp_transaction_opts_t options;
  bool committed;
  std::mutex mutex;
};

// Use HandleManager for FFI-safe handle management
using c_api_internal::HandleManager;
static HandleManager<GuardImpl> g_guard_manager;
static HandleManager<TransactionImpl> g_transaction_manager;

}  // namespace c_api
}  // namespace mcp

extern "C" {

using namespace mcp::c_api;

// Guard functions - FFI-safe with automatic RAII cleanup
MCP_API mcp_guard_t mcp_guard_create(void* handle,
                                     mcp_type_id_t type) MCP_NOEXCEPT {
  if (!handle)
    return nullptr;

  try {
    auto guard = std::make_shared<GuardImpl>(handle, type);
    // Store in handle manager - gets uint64_t handle for FFI safety
    auto guard_handle = g_guard_manager.store(guard);
    // Return as opaque pointer for compatibility
    return reinterpret_cast<mcp_guard_t>(static_cast<uintptr_t>(guard_handle));
  } catch (...) {
    return nullptr;
  }
}

MCP_API mcp_guard_t mcp_guard_create_custom(void* handle,
                                            mcp_type_id_t type,
                                            mcp_guard_cleanup_fn cleanup)
    MCP_NOEXCEPT {
  if (!handle)
    return nullptr;

  try {
    auto guard = std::make_shared<GuardImpl>(handle, type, cleanup);
    auto guard_handle = g_guard_manager.store(guard);
    return reinterpret_cast<mcp_guard_t>(static_cast<uintptr_t>(guard_handle));
  } catch (...) {
    return nullptr;
  }
}

MCP_API void* mcp_guard_release(mcp_guard_t* guard) MCP_NOEXCEPT {
  if (!guard || !*guard)
    return nullptr;

  // Convert back to handle
  uint64_t handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(*guard));
  auto guard_ptr = g_guard_manager.get(handle);

  if (!guard_ptr)
    return nullptr;

  // Release ownership from guard
  void* resource = guard_ptr->release();

  // Remove from manager
  g_guard_manager.release(handle);
  *guard = nullptr;

  return resource;
}

MCP_API void mcp_guard_destroy(mcp_guard_t* guard) MCP_NOEXCEPT {
  if (!guard || !*guard)
    return;

  uint64_t handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(*guard));
  // Release from manager - shared_ptr destructor will clean up
  g_guard_manager.release(handle);
  *guard = nullptr;
}

MCP_API mcp_bool_t mcp_guard_is_valid(mcp_guard_t guard) MCP_NOEXCEPT {
  if (!guard)
    return MCP_FALSE;

  uint64_t handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(guard));
  auto guard_ptr = g_guard_manager.get(handle);

  return (guard_ptr && guard_ptr->is_valid()) ? MCP_TRUE : MCP_FALSE;
}

MCP_API void* mcp_guard_get(mcp_guard_t guard) MCP_NOEXCEPT {
  if (!guard)
    return nullptr;

  uint64_t handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(guard));
  auto guard_ptr = g_guard_manager.get(handle);

  return guard_ptr ? guard_ptr->get() : nullptr;
}

// Transaction functions - FFI-safe with automatic RAII cleanup
MCP_API mcp_transaction_t mcp_transaction_create(void) MCP_NOEXCEPT {
  try {
    auto txn = std::make_shared<TransactionImpl>();
    auto txn_handle = g_transaction_manager.store(txn);
    return reinterpret_cast<mcp_transaction_t>(
        static_cast<uintptr_t>(txn_handle));
  } catch (...) {
    return nullptr;
  }
}

MCP_API mcp_transaction_t
mcp_transaction_create_ex(const mcp_transaction_opts_t* opts) MCP_NOEXCEPT {
  try {
    auto txn = std::make_shared<TransactionImpl>(opts);
    auto txn_handle = g_transaction_manager.store(txn);
    return reinterpret_cast<mcp_transaction_t>(
        static_cast<uintptr_t>(txn_handle));
  } catch (...) {
    return nullptr;
  }
}

MCP_API void mcp_transaction_destroy(mcp_transaction_t* txn) MCP_NOEXCEPT {
  if (!txn || !*txn)
    return;

  uint64_t handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(*txn));
  // Release from manager - shared_ptr destructor will handle cleanup
  g_transaction_manager.release(handle);
  *txn = nullptr;
}

MCP_API mcp_result_t mcp_transaction_add(mcp_transaction_t txn,
                                         void* handle,
                                         mcp_type_id_t type) MCP_NOEXCEPT {
  if (!txn || !handle)
    return MCP_ERROR_INVALID_ARGUMENT;

  uint64_t txn_handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(txn));
  auto txn_ptr = g_transaction_manager.get(txn_handle);

  if (!txn_ptr)
    return MCP_ERROR_NOT_FOUND;

  return txn_ptr->add(handle, type);
}

MCP_API mcp_result_t mcp_transaction_add_custom(mcp_transaction_t txn,
                                                void* handle,
                                                mcp_type_id_t type,
                                                mcp_guard_cleanup_fn cleanup)
    MCP_NOEXCEPT {
  if (!txn || !handle)
    return MCP_ERROR_INVALID_ARGUMENT;

  uint64_t txn_handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(txn));
  auto txn_ptr = g_transaction_manager.get(txn_handle);

  if (!txn_ptr)
    return MCP_ERROR_NOT_FOUND;

  return txn_ptr->add(handle, type, cleanup);
}

MCP_API mcp_result_t mcp_transaction_commit(mcp_transaction_t* txn)
    MCP_NOEXCEPT {
  if (!txn || !*txn)
    return MCP_ERROR_INVALID_ARGUMENT;

  uint64_t handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(*txn));
  auto txn_ptr = g_transaction_manager.get(handle);

  if (!txn_ptr)
    return MCP_ERROR_NOT_FOUND;

  mcp_result_t result = txn_ptr->commit();

  // Release from manager after commit
  g_transaction_manager.release(handle);
  *txn = nullptr;

  return result;
}

MCP_API void mcp_transaction_rollback(mcp_transaction_t* txn) MCP_NOEXCEPT {
  if (!txn || !*txn)
    return;

  uint64_t handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(*txn));
  auto txn_ptr = g_transaction_manager.get(handle);

  if (txn_ptr) {
    txn_ptr->rollback();
  }

  // Release from manager after rollback
  g_transaction_manager.release(handle);
  *txn = nullptr;
}

MCP_API size_t mcp_transaction_size(mcp_transaction_t txn) MCP_NOEXCEPT {
  if (!txn)
    return 0;

  uint64_t handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(txn));
  auto txn_ptr = g_transaction_manager.get(handle);

  return txn_ptr ? txn_ptr->size() : 0;
}

}  // extern "C"