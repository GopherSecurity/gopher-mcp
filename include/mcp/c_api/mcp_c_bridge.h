/**
 * @file mcp_c_bridge.h
 * @brief Internal C++ to C bridge with RAII enforcement
 *
 * This header provides the internal bridge between C++ and C APIs with
 * comprehensive RAII support, FFI-safe type conversions, and automatic
 * resource management. It ensures thread-safe operations and prevents
 * resource leaks through systematic RAII enforcement.
 *
 * Architecture:
 * - RAII wrappers for all C++ resources
 * - Thread-safe handle management with reference counting
 * - Automatic cleanup through scope guards and transactions
 * - FFI-safe type conversions with validation
 * - Comprehensive error handling with recovery
 *
 * This file is NOT part of the public API and should only be included
 * by implementation files.
 */

#ifndef MCP_C_BRIDGE_H
#define MCP_C_BRIDGE_H

/* Include C API headers */
#include "mcp_c_api.h"
#include "mcp_c_collections.h"
#include "mcp_c_memory.h"
#include "mcp_c_types.h"
#include "mcp_c_types_api.h"
#include "mcp_raii.h"

/* C++ standard library headers */
#include <atomic>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/* MCP C++ headers */
#include "mcp/buffer.h"
#include "mcp/client/mcp_client.h"
#include "mcp/event/event_loop.h"
#include "mcp/json/json_bridge.h"
#include "mcp/network/address_impl.h"
#include "mcp/network/connection.h"
#include "mcp/network/listener.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace c_api {

/* ============================================================================
 * RAII-Enabled Handle Base
 * ============================================================================
 */

/**
 * Base class for all handle implementations with RAII support
 * Provides reference counting, thread-safe destruction, and resource tracking
 */
class HandleBase {
 public:
  HandleBase() : ref_count_(1), type_id_(MCP_TYPE_UNKNOWN) {
    RegisterHandle(this);
  }

  explicit HandleBase(mcp_type_id_t type) : ref_count_(1), type_id_(type) {
    RegisterHandle(this);
  }

  virtual ~HandleBase() { UnregisterHandle(this); }

  void AddRef() { ref_count_.fetch_add(1, std::memory_order_relaxed); }

  void Release() {
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }

  int GetRefCount() const { return ref_count_.load(std::memory_order_relaxed); }

  mcp_type_id_t GetType() const { return type_id_; }

  /* Virtual methods for resource cleanup */
  virtual void Cleanup() {}
  virtual bool IsValid() const { return true; }

 private:
  std::atomic<int> ref_count_;
  mcp_type_id_t type_id_;

  /* Global handle registry for leak detection */
  static void RegisterHandle(HandleBase* handle);
  static void UnregisterHandle(HandleBase* handle);
};

/* ============================================================================
 * RAII Guard Implementation
 * ============================================================================
 */

/**
 * RAII guard for automatic resource cleanup with enhanced safety
 * Provides strong exception safety and automatic reference counting
 */
struct mcp_guard_impl {
  using Deleter = std::function<void(void*)>;

  mcp_guard_impl(void* handle, mcp_type_id_t type, Deleter deleter)
      : handle_(handle),
        type_(type),
        deleter_(std::move(deleter)),
        ref_count_(1) {
    if (handle_) {
      if (auto* base = static_cast<HandleBase*>(handle_)) {
        base->AddRef();
      }
    }
  }

  ~mcp_guard_impl() noexcept {
    try {
      if (handle_ && !released_) {
        Release();
      }
    } catch (...) {
      /* Suppress exceptions in destructor */
    }
  }

  /* Disable copy to enforce single ownership */
  mcp_guard_impl(const mcp_guard_impl&) = delete;
  mcp_guard_impl& operator=(const mcp_guard_impl&) = delete;

  /* Enable move semantics for transfer of ownership */
  mcp_guard_impl(mcp_guard_impl&& other) noexcept
      : handle_(other.handle_),
        type_(other.type_),
        deleter_(std::move(other.deleter_)),
        released_(other.released_.load()),
        ref_count_(other.ref_count_.load()) {
    other.handle_ = nullptr;
    other.released_ = true;
  }

  void* Release() {
    void* h = handle_;
    handle_ = nullptr;
    released_ = true;

    if (h && deleter_ && ref_count_.fetch_sub(1) == 1) {
      try {
        deleter_(h);
      } catch (const std::exception& e) {
        /* Log cleanup failure - ErrorManager defined later */
        /* ErrorManager::SetError(MCP_ERROR_UNKNOWN,
                              std::string("Guard cleanup failed: ") + e.what());
         */
      }
    }
    return h;
  }

  void* Get() const noexcept { return released_ ? nullptr : handle_; }
  mcp_type_id_t GetType() const noexcept { return type_; }
  bool IsValid() const noexcept { return !released_ && handle_ != nullptr; }

  void AddRef() { ref_count_.fetch_add(1); }

 private:
  void* handle_;
  mcp_type_id_t type_;
  Deleter deleter_;
  std::atomic<bool> released_{false};
  std::atomic<int> ref_count_;
};

/* ============================================================================
 * Transaction Implementation
 * ============================================================================
 */

/**
 * Transaction for atomic multi-resource operations with RAII
 * Ensures all-or-nothing semantics with automatic rollback
 */
struct mcp_transaction_impl {
  struct Resource {
    void* handle;
    mcp_type_id_t type;
    std::function<void(void*)> deleter;
    std::string description; /* For debugging */

    Resource(void* h,
             mcp_type_id_t t,
             std::function<void(void*)> d,
             const std::string& desc = "")
        : handle(h), type(t), deleter(std::move(d)), description(desc) {}
  };

  explicit mcp_transaction_impl(const mcp_transaction_opts_t* opts = nullptr)
      : committed_(false), rolled_back_(false) {
    if (opts) {
      auto_rollback_ = opts->auto_rollback;
      strict_ordering_ = opts->strict_ordering;
      max_resources_ = opts->max_resources;
    }

    resources_.reserve(max_resources_ > 0 ? max_resources_ : 16);
  }

  ~mcp_transaction_impl() noexcept {
    if (!committed_ && !rolled_back_ && auto_rollback_) {
      try {
        Rollback();
      } catch (...) {
        /* Suppress exceptions in destructor */
      }
    }
  }

  /* Disable copy to ensure single ownership */
  mcp_transaction_impl(const mcp_transaction_impl&) = delete;
  mcp_transaction_impl& operator=(const mcp_transaction_impl&) = delete;

  mcp_result_t AddResource(void* handle,
                           mcp_type_id_t type,
                           std::function<void(void*)> deleter = nullptr,
                           const std::string& desc = "") {
    if (!handle)
      return MCP_ERROR_INVALID_ARGUMENT;
    if (committed_ || rolled_back_)
      return MCP_ERROR_INVALID_STATE;

    std::lock_guard<std::mutex> lock(mutex_);

    if (max_resources_ > 0 && resources_.size() >= max_resources_) {
      return MCP_ERROR_RESOURCE_LIMIT;
    }

    if (!deleter) {
      deleter = [type](void* h) { DestroyHandle(h, type); };
    }

    try {
      resources_.emplace_back(handle, type, std::move(deleter), desc);
      return MCP_OK;
    } catch (const std::exception&) {
      return MCP_ERROR_NO_MEMORY;
    }
  }

  mcp_result_t Commit() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (committed_ || rolled_back_) {
      return MCP_ERROR_INVALID_STATE;
    }

    committed_ = true;
    resources_.clear();
    return MCP_OK;
  }

  void Rollback() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (committed_ || rolled_back_) {
      return;
    }

    /* Cleanup in reverse order (LIFO) for proper dependency handling */
    if (strict_ordering_) {
      for (auto it = resources_.rbegin(); it != resources_.rend(); ++it) {
        SafeCleanup(*it);
      }
    } else {
      /* Parallel cleanup for independent resources */
      std::vector<std::future<void>> futures;
      for (auto& resource : resources_) {
        futures.push_back(std::async(std::launch::async, [this, &resource]() {
          SafeCleanup(resource);
        }));
      }
      for (auto& f : futures) {
        f.wait();
      }
    }

    resources_.clear();
    rolled_back_ = true;
  }

  size_t Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return resources_.size();
  }

  bool IsValid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !committed_ && !rolled_back_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<Resource> resources_;
  std::atomic<bool> committed_;
  std::atomic<bool> rolled_back_;
  bool auto_rollback_ = true;
  bool strict_ordering_ = true;
  size_t max_resources_ = 0;

  void SafeCleanup(const Resource& resource) {
    try {
      if (resource.deleter && resource.handle) {
        resource.deleter(resource.handle);
      }
    } catch (const std::exception& e) {
      /* Log error but continue cleanup */
      /* Log cleanup failure - ErrorManager defined later */
      /* ErrorManager::SetError(MCP_ERROR_CLEANUP_FAILED,
          std::string("Resource cleanup failed: ") + resource.description +
          " - " + e.what()); */
    }
  }

  static void DestroyHandle(void* handle, mcp_type_id_t type);
};

/* ============================================================================
 * Dispatcher Implementation with RAII
 * ============================================================================
 */

struct mcp_dispatcher_impl : public HandleBase {
  mcp_dispatcher_impl() : HandleBase(MCP_TYPE_UNKNOWN) {
    /* Dispatcher will be created by mcp_dispatcher_create() */
  }

  ~mcp_dispatcher_impl() override { Cleanup(); }

  void Cleanup() override {
    if (running) {
      dispatcher->exit();
      running = false;
    }
    /* Clean up all timers */
    timers.clear();
  }

  bool IsValid() const override { return dispatcher != nullptr; }

  /* Dispatcher instance */
  std::unique_ptr<mcp::event::Dispatcher> dispatcher;
  std::thread::id dispatcher_thread_id;
  std::atomic<bool> running{false};

  /* Timer management with RAII */
  struct TimerInfo {
    std::unique_ptr<mcp::event::Timer> timer;
    mcp_timer_callback_t callback;
    void* user_data;

    TimerInfo(std::unique_ptr<mcp::event::Timer> t,
              mcp_timer_callback_t cb,
              void* ud)
        : timer(std::move(t)), callback(cb), user_data(ud) {}
  };

  std::mutex timers_mutex;
  std::unordered_map<uint64_t, std::unique_ptr<TimerInfo>> timers;
  std::atomic<uint64_t> next_timer_id{1};

  /* RAII helper for timer creation */
  uint64_t CreateTimer(mcp_timer_callback_t callback, void* user_data) {
    std::lock_guard<std::mutex> lock(timers_mutex);

    uint64_t id = next_timer_id.fetch_add(1, std::memory_order_relaxed);
    auto timer = dispatcher->createTimer([callback, user_data]() {
      if (callback)
        callback(user_data);
    });

    timers[id] =
        std::make_unique<TimerInfo>(std::move(timer), callback, user_data);
    return id;
  }

  void DestroyTimer(uint64_t id) {
    std::lock_guard<std::mutex> lock(timers_mutex);
    timers.erase(id);
  }
};

/* ============================================================================
 * Connection Implementation with RAII
 * ============================================================================
 */

struct mcp_connection_impl : public HandleBase {
  mcp_connection_impl(mcp_dispatcher_impl* disp)
      : HandleBase(MCP_TYPE_UNKNOWN), dispatcher(disp) {
    if (dispatcher) {
      dispatcher->AddRef();
    }
  }

  ~mcp_connection_impl() override { Cleanup(); }

  void Cleanup() override {
    if (connection) {
      connection->close(mcp::network::ConnectionCloseType::FlushWrite);
    }
    if (dispatcher) {
      dispatcher->Release();
      dispatcher = nullptr;
    }
  }

  bool IsValid() const override {
    return connection != nullptr && dispatcher != nullptr;
  }

  /* Connection instance with RAII */
  std::shared_ptr<mcp::network::Connection> connection;
  mcp_dispatcher_impl* dispatcher;

  /* Callbacks */
  mcp_connection_state_callback_t state_callback = nullptr;
  mcp_data_callback_t data_callback = nullptr;
  mcp_error_callback_t error_callback = nullptr;
  void* callback_user_data = nullptr;

  /* State tracking */
  std::atomic<mcp_connection_state_t> current_state{
      MCP_CONNECTION_STATE_DISCONNECTED};

  /* Statistics */
  std::atomic<uint64_t> bytes_read{0};
  std::atomic<uint64_t> bytes_written{0};

  /* Configuration */
  mcp::network::Address::InstanceConstSharedPtr remote_address;

  /* Callback bridge with RAII */
  class CallbackBridge;
  std::unique_ptr<CallbackBridge> callback_bridge;
};

/* ============================================================================
 * Listener Implementation with RAII
 * ============================================================================
 */

struct mcp_listener_impl : public HandleBase {
  mcp_listener_impl(mcp_dispatcher_impl* disp)
      : HandleBase(MCP_TYPE_UNKNOWN), dispatcher(disp) {
    if (dispatcher) {
      dispatcher->AddRef();
    }
  }

  ~mcp_listener_impl() override { Cleanup(); }

  void Cleanup() override {
    if (listener) {
      /* Stop listening */
      listener.reset();
    }
    if (dispatcher) {
      dispatcher->Release();
      dispatcher = nullptr;
    }
  }

  bool IsValid() const override {
    return listener != nullptr && dispatcher != nullptr;
  }

  std::unique_ptr<mcp::network::Listener> listener;
  mcp_dispatcher_impl* dispatcher;

  /* Callbacks */
  mcp_accept_callback_t accept_callback = nullptr;
  void* callback_user_data = nullptr;

  /* Accepted connections tracking for cleanup */
  std::mutex connections_mutex;
  std::vector<mcp_connection_impl*> accepted_connections;
};

/* ============================================================================
 * MCP Client Implementation with RAII
 * ============================================================================
 */

struct mcp_client_impl : public HandleBase {
  mcp_client_impl(mcp_dispatcher_impl* disp)
      : HandleBase(MCP_TYPE_UNKNOWN), dispatcher(disp) {
    if (dispatcher) {
      dispatcher->AddRef();
    }
  }

  ~mcp_client_impl() override { Cleanup(); }

  void Cleanup() override {
    if (connection) {
      connection->Release();
      connection = nullptr;
    }
    if (dispatcher) {
      dispatcher->Release();
      dispatcher = nullptr;
    }
  }

  bool IsValid() const override { return dispatcher != nullptr; }

  /* Client instance (placeholder - replace with actual MCPClient) */
  void* client = nullptr;
  mcp_dispatcher_impl* dispatcher;
  mcp_connection_impl* connection = nullptr;

  /* Callbacks */
  mcp_request_callback_t request_callback = nullptr;
  mcp_response_callback_t response_callback = nullptr;
  mcp_notification_callback_t notification_callback = nullptr;
  void* callback_user_data = nullptr;

  /* Request tracking with RAII */
  std::mutex request_mutex;
  std::unordered_map<uint64_t, mcp_request_id_t> request_map;
  std::atomic<uint64_t> next_request_id{1};
};

/* ============================================================================
 * MCP Server Implementation with RAII
 * ============================================================================
 */

struct mcp_server_impl : public HandleBase {
  mcp_server_impl(mcp_dispatcher_impl* disp)
      : HandleBase(MCP_TYPE_UNKNOWN), dispatcher(disp) {
    if (dispatcher) {
      dispatcher->AddRef();
    }
  }

  ~mcp_server_impl() override { Cleanup(); }

  void Cleanup() override {
    if (listener) {
      listener->Release();
      listener = nullptr;
    }
    if (dispatcher) {
      dispatcher->Release();
      dispatcher = nullptr;
    }
    /* Clear registered capabilities */
    tools.clear();
    resources.clear();
    prompts.clear();
  }

  bool IsValid() const override { return dispatcher != nullptr; }

  /* Server instance (placeholder - replace with actual MCPServer) */
  void* server = nullptr;
  mcp_dispatcher_impl* dispatcher;
  mcp_listener_impl* listener = nullptr;

  /* Callbacks */
  mcp_request_callback_t request_callback = nullptr;
  mcp_notification_callback_t notification_callback = nullptr;
  void* callback_user_data = nullptr;

  /* Registered capabilities with RAII */
  std::vector<mcp::Tool> tools;
  std::vector<mcp::ResourceTemplate> resources;
  std::vector<mcp::Prompt> prompts;
};

/* ============================================================================
 * JSON Value Implementation with RAII
 * ============================================================================
 */

struct mcp_json_value_impl : public HandleBase {
  mcp_json_value_impl() : HandleBase(MCP_TYPE_JSON) {}

  explicit mcp_json_value_impl(mcp::json::JsonValue v)
      : HandleBase(MCP_TYPE_JSON), value(std::move(v)) {}

  ~mcp_json_value_impl() override = default;

  bool IsValid() const override {
    return true; /* JSON values are always valid */
  }

  mcp::json::JsonValue value;
};

/* ============================================================================
 * Type Conversion Utilities with Validation
 * ============================================================================
 */

/**
 * Safe string conversion with null checks
 */
inline std::string to_cpp_string_safe(mcp_string_t str) {
  if (!str.data) {
    return std::string();
  }
  return std::string(str.data, str.length);
}

/**
 * Temporary C string creation with lifetime management
 */
class TempCString {
 public:
  explicit TempCString(const std::string& str)
      : data_(str.c_str()), length_(str.length()) {}

  mcp_string_t get() const { return {data_, length_}; }

 private:
  const char* data_;
  size_t length_;
};

/**
 * Convert RequestId with validation
 */
inline mcp_request_id_t to_c_request_id_safe(const mcp::RequestId& id) {
  if (mcp::holds_alternative<std::string>(id)) {
    const auto& str = mcp::get<std::string>(id);
    return mcp_request_id_create_string(str.c_str());
  } else {
    return mcp_request_id_create_number(mcp::get<int>(id));
  }
}

inline mcp::RequestId to_cpp_request_id_safe(mcp_request_id_t id) {
  if (!id) {
    return mcp::RequestId(0);
  }

  if (mcp_request_id_is_string(id)) {
    const char* str = mcp_request_id_get_string(id);
    return mcp::RequestId(str ? std::string(str) : std::string());
  } else {
    return mcp::RequestId(static_cast<int>(mcp_request_id_get_number(id)));
  }
}

/**
 * Convert address with validation
 */
inline mcp::network::Address::InstanceConstSharedPtr to_cpp_address_safe(
    const mcp_address_t* addr) {
  if (!addr)
    return nullptr;

  switch (addr->family) {
    case mcp_address::MCP_AF_INET:
      return std::make_shared<mcp::network::Address::Ipv4Instance>(
          std::string(addr->addr.inet.host), addr->addr.inet.port);

    case mcp_address::MCP_AF_INET6:
      return std::make_shared<mcp::network::Address::Ipv6Instance>(
          std::string(addr->addr.inet.host), addr->addr.inet.port);

    case mcp_address::MCP_AF_UNIX:
      return std::make_shared<mcp::network::Address::PipeInstance>(
          std::string(addr->addr.unix.path));

    default:
      return nullptr;
  }
}

/* ============================================================================
 * Callback Bridges with RAII
 * ============================================================================
 */

/**
 * Connection callbacks bridge with automatic cleanup
 */
class mcp_connection_impl::CallbackBridge
    : public mcp::network::ConnectionCallbacks {
 public:
  explicit CallbackBridge(mcp_connection_impl* impl) : impl_(impl) {
    if (impl_) {
      impl_->AddRef();
    }
  }

  ~CallbackBridge() {
    if (impl_) {
      impl_->Release();
    }
  }

  void onEvent(mcp::network::ConnectionEvent event) override {
    if (!impl_)
      return;

    /* Map event to state change */
    mcp_connection_state_t new_state = impl_->current_state.load();

    switch (event) {
      case mcp::network::ConnectionEvent::Connected:
      case mcp::network::ConnectionEvent::ConnectedZeroRtt:
        new_state = MCP_CONNECTION_STATE_CONNECTED;
        break;

      case mcp::network::ConnectionEvent::RemoteClose:
      case mcp::network::ConnectionEvent::LocalClose:
        new_state = MCP_CONNECTION_STATE_DISCONNECTED;
        break;

      default:
        break;
    }

    /* Notify state change */
    if (new_state != impl_->current_state && impl_->state_callback) {
      impl_->current_state = new_state;
      impl_->state_callback(reinterpret_cast<mcp_connection_t>(impl_),
                            static_cast<int>(new_state),
                            impl_->callback_user_data);
    }
  }

  void onAboveWriteBufferHighWatermark() override {
    /* Could notify about backpressure */
  }

  void onBelowWriteBufferLowWatermark() override {
    /* Could notify about flow control */
  }

 private:
  mcp_connection_impl* impl_;
};

/* ============================================================================
 * Memory Management with RAII
 * ============================================================================
 */

/**
 * Global allocator with RAII cleanup
 */
class GlobalAllocator {
 public:
  static GlobalAllocator& Instance() {
    static GlobalAllocator instance;
    return instance;
  }

  void SetAllocator(const mcp_allocator_t* allocator) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (allocator) {
      allocator_ = *allocator;
      has_custom_ = true;
    } else {
      has_custom_ = false;
    }
  }

  void* Alloc(size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);

    void* ptr = nullptr;
    if (has_custom_ && allocator_.alloc) {
      ptr = allocator_.alloc(size, allocator_.user_data);
    } else {
      ptr = std::malloc(size);
    }

    if (ptr) {
      TrackAllocation(ptr, size);
    }
    return ptr;
  }

  void* Realloc(void* ptr, size_t new_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    void* new_ptr = nullptr;
    if (has_custom_ && allocator_.realloc) {
      new_ptr = allocator_.realloc(ptr, new_size, allocator_.user_data);
    } else {
      new_ptr = std::realloc(ptr, new_size);
    }

    if (new_ptr) {
      UntrackAllocation(ptr);
      TrackAllocation(new_ptr, new_size);
    }
    return new_ptr;
  }

  void Free(void* ptr) {
    if (!ptr)
      return;

    std::lock_guard<std::mutex> lock(mutex_);

    UntrackAllocation(ptr);

    if (has_custom_ && allocator_.free) {
      allocator_.free(ptr, allocator_.user_data);
    } else {
      std::free(ptr);
    }
  }

  /* Memory tracking for leak detection */
  size_t GetActiveAllocations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return allocations_.size();
  }

  void ReportLeaks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!allocations_.empty()) {
      /* Log leak information */
      for (const auto& [ptr, size] : allocations_) {
        /* Report leak: ptr with size bytes */
      }
    }
  }

 private:
  mutable std::mutex mutex_;
  mcp_allocator_t allocator_;
  bool has_custom_ = false;

  /* Allocation tracking */
  std::unordered_map<void*, size_t> allocations_;

  void TrackAllocation(void* ptr, size_t size) { allocations_[ptr] = size; }

  void UntrackAllocation(void* ptr) { allocations_.erase(ptr); }
};

/* ============================================================================
 * Error Handling with RAII
 * ============================================================================
 */

/**
 * Thread-local error management with automatic cleanup
 */
class ErrorManager {
 public:
  static void SetError(mcp_result_t code,
                       const std::string& message,
                       const char* file = __FILE__,
                       int line = __LINE__) {
    thread_local_error_.code = code;
    strncpy(thread_local_error_.message, message.c_str(),
            sizeof(thread_local_error_.message) - 1);
    strncpy(thread_local_error_.file, file,
            sizeof(thread_local_error_.file) - 1);
    thread_local_error_.line = line;
  }

  static const mcp_error_info_t* GetError() {
    if (thread_local_error_.code != MCP_OK) {
      return &thread_local_error_;
    }
    return nullptr;
  }

  static void ClearError() {
    thread_local_error_.code = MCP_OK;
    thread_local_error_.message[0] = '\0';
  }

  /* RAII error scope for automatic cleanup */
  class ErrorScope {
   public:
    ErrorScope() { ClearError(); }
    ~ErrorScope() { /* Error persists after scope */
    }
  };

 private:
  static thread_local mcp_error_info_t thread_local_error_;
};

/* ============================================================================
 * Handle Registry for Global Resource Management
 * ============================================================================
 */

/**
 * Global handle registry for leak detection and validation
 */
class HandleRegistry {
 public:
  static HandleRegistry& Instance() {
    static HandleRegistry instance;
    return instance;
  }

  void Register(HandleBase* handle) {
    if (!handle)
      return;

    std::lock_guard<std::mutex> lock(mutex_);
    handles_.insert(handle);
    stats_.total_created++;
  }

  void Unregister(HandleBase* handle) {
    if (!handle)
      return;

    std::lock_guard<std::mutex> lock(mutex_);
    handles_.erase(handle);
    stats_.total_destroyed++;
  }

  bool IsValid(void* handle) const {
    if (!handle)
      return false;

    std::lock_guard<std::mutex> lock(mutex_);
    return handles_.find(static_cast<HandleBase*>(handle)) != handles_.end();
  }

  struct Stats {
    size_t total_created{0};
    size_t total_destroyed{0};
  };

  Stats GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
  }

  size_t GetActiveCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return handles_.size();
  }

  void ReportLeaks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!handles_.empty()) {
      /* Report each leaked handle */
      for (auto* handle : handles_) {
        /* Log: Leaked handle of type handle->GetType() */
      }
    }
  }

 private:
  mutable std::mutex mutex_;
  std::unordered_set<HandleBase*> handles_;
  Stats stats_;
};

/* ============================================================================
 * Helper Macros with RAII
 * ============================================================================
 */

#define CHECK_HANDLE_VALID(handle)                                         \
  do {                                                                     \
    if (!HandleRegistry::Instance().IsValid(handle)) {                     \
      ErrorManager::SetError(MCP_ERROR_INVALID_ARGUMENT, "Invalid handle", \
                             __FILE__, __LINE__);                          \
      return MCP_ERROR_INVALID_ARGUMENT;                                   \
    }                                                                      \
  } while (0)

#define CHECK_HANDLE_VALID_NULL(handle)                                    \
  do {                                                                     \
    if (!HandleRegistry::Instance().IsValid(handle)) {                     \
      ErrorManager::SetError(MCP_ERROR_INVALID_ARGUMENT, "Invalid handle", \
                             __FILE__, __LINE__);                          \
      return nullptr;                                                      \
    }                                                                      \
  } while (0)

#define RAII_GUARD(handle)                                  \
  mcp::raii::ResourceGuard<HandleBase> guard_(              \
      static_cast<HandleBase*>(handle), [](HandleBase* h) { \
        if (h)                                              \
          h->Release();                                     \
      })

#define RAII_TRANSACTION() mcp::raii::AllocationTransaction transaction_

#define TRY_WITH_RAII(code)                                                  \
  try {                                                                      \
    ErrorManager::ErrorScope error_scope_;                                   \
    code                                                                     \
  } catch (const std::exception& e) {                                        \
    ErrorManager::SetError(MCP_ERROR_UNKNOWN, e.what(), __FILE__, __LINE__); \
    return MCP_ERROR_UNKNOWN;                                                \
  } catch (...) {                                                            \
    ErrorManager::SetError(MCP_ERROR_UNKNOWN, "Unknown error", __FILE__,     \
                           __LINE__);                                        \
    return MCP_ERROR_UNKNOWN;                                                \
  }

#define TRY_WITH_RAII_NULL(code)                                             \
  try {                                                                      \
    ErrorManager::ErrorScope error_scope_;                                   \
    code                                                                     \
  } catch (const std::exception& e) {                                        \
    ErrorManager::SetError(MCP_ERROR_UNKNOWN, e.what(), __FILE__, __LINE__); \
    return nullptr;                                                          \
  } catch (...) {                                                            \
    ErrorManager::SetError(MCP_ERROR_UNKNOWN, "Unknown error", __FILE__,     \
                           __LINE__);                                        \
    return nullptr;                                                          \
  }

/* ============================================================================
 * Inline Implementations
 * ============================================================================
 */

inline void HandleBase::RegisterHandle(HandleBase* handle) {
  HandleRegistry::Instance().Register(handle);
}

inline void HandleBase::UnregisterHandle(HandleBase* handle) {
  HandleRegistry::Instance().Unregister(handle);
}

inline void mcp_transaction_impl::DestroyHandle(void* handle,
                                                mcp_type_id_t type) {
  if (!handle)
    return;

  /* Type-specific destruction */
  switch (type) {
    case MCP_TYPE_STRING:
      mcp_string_buffer_free(static_cast<mcp_string_buffer_t*>(handle));
      break;

    case MCP_TYPE_JSON:
      mcp_json_free(static_cast<mcp_json_value_t>(handle));
      break;

    default:
      /* Generic handle destruction */
      if (auto* base = static_cast<HandleBase*>(handle)) {
        base->Release();
      }
      break;
  }
}

/* Thread-local storage definition is in implementation file */

}  // namespace c_api
}  // namespace mcp

#endif /* MCP_C_BRIDGE_H */