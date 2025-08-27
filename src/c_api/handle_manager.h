/**
 * @file handle_manager.h
 * @brief Internal handle management template for C API implementation
 * @note This is NOT part of the public API - internal use only
 */

#ifndef MCP_C_API_HANDLE_MANAGER_H
#define MCP_C_API_HANDLE_MANAGER_H

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace mcp {
namespace c_api_internal {

/**
 * Thread-safe handle manager for converting between C handles (uint64_t)
 * and C++ shared pointers. Used internally by the C API implementation.
 */
template <typename T>
class HandleManager {
 public:
  using SharedPtr = std::shared_ptr<T>;

  HandleManager() : next_handle_(1) {}

  uint64_t store(SharedPtr obj) {
    if (!obj)
      return 0;

    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t handle = next_handle_.fetch_add(1, std::memory_order_relaxed);
    handles_[handle] = obj;
    return handle;
  }

  SharedPtr get(uint64_t handle) {
    if (handle == 0)
      return nullptr;

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handles_.find(handle);
    if (it != handles_.end()) {
      return it->second;
    }
    return nullptr;
  }

  void retain(uint64_t handle) {
    // Reference counting is handled by shared_ptr
    // This is a no-op but kept for API consistency
  }

  void release(uint64_t handle) {
    if (handle == 0)
      return;

    std::lock_guard<std::mutex> lock(mutex_);
    handles_.erase(handle);
  }

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    handles_.clear();
  }

 private:
  std::mutex mutex_;
  std::atomic<uint64_t> next_handle_;
  std::unordered_map<uint64_t, SharedPtr> handles_;
};

}  // namespace c_api_internal
}  // namespace mcp

#endif  // MCP_C_API_HANDLE_MANAGER_H