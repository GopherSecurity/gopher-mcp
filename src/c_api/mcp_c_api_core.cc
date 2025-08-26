/**
 * @file mcp_c_api_core.cc
 * @brief Core C API implementation - Library initialization and dispatcher
 * Uses RAII for memory safety.
 */

#include <atomic>
#include <mutex>

#include "mcp/c_api/mcp_c_api.h"
#include "mcp/c_api/mcp_c_bridge.h"
#include "mcp/c_api/mcp_raii.h"
#include "mcp/event/libevent_dispatcher.h"

namespace mcp {
namespace c_api {

// Thread-local error storage implementation
thread_local mcp_error_info_t ErrorManager::thread_local_error_ = {};

}  // namespace c_api
}  // namespace mcp

using namespace mcp::c_api;

/* ============================================================================
 * Library Initialization & Cleanup
 * ============================================================================
 */

extern "C" {

// Library initialization is now implemented in mcp_c_memory_impl.cc
// The functions mcp_init, mcp_shutdown, and mcp_is_initialized are
// provided there for centralized memory and error management

const char* mcp_get_version(void) {
  return "1.0.0";  // TODO: Use actual version from build system
}

// Removed - now implemented in mcp_c_memory_impl.cc with mcp_error_info_t*
// const char* mcp_get_last_error(void) { return ErrorManager::get_error(); }

/* ============================================================================
 * Event Loop & Dispatcher
 * ============================================================================
 */

mcp_dispatcher_t mcp_dispatcher_create(void) MCP_NOEXCEPT {
  if (!mcp_is_initialized()) {
    ErrorManager::SetError(MCP_ERROR_NOT_INITIALIZED, "Library not initialized");
    return nullptr;
  }

  TRY_WITH_RAII_NULL({
    auto impl = new mcp::c_api::mcp_dispatcher_impl();

    // Create libevent dispatcher
    impl->dispatcher =
        std::make_unique<mcp::event::LibeventDispatcher>("mcp_c_api");
    impl->dispatcher_thread_id = std::this_thread::get_id();

    return reinterpret_cast<mcp_dispatcher_t>(impl);
  });
}

mcp_result_t mcp_dispatcher_run(mcp_dispatcher_t dispatcher) MCP_NOEXCEPT {
  CHECK_HANDLE_VALID(dispatcher);

  TRY_WITH_RAII({
    auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);

    if (impl->running) {
      ErrorManager::SetError(MCP_ERROR_INVALID_STATE, "Dispatcher already running");
      return MCP_ERROR_INVALID_STATE;
    }

    impl->running = true;
    impl->dispatcher_thread_id = std::this_thread::get_id();

    // Run the event loop (blocks)
    impl->dispatcher->run(mcp::event::RunType::Block);

    impl->running = false;
    return MCP_OK;
  });
}

mcp_result_t mcp_dispatcher_run_timeout(mcp_dispatcher_t dispatcher,
                                        uint32_t timeout_ms) MCP_NOEXCEPT {
  CHECK_HANDLE_VALID(dispatcher);

  TRY_WITH_RAII({
    auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);

    if (impl->running) {
      ErrorManager::SetError(MCP_ERROR_INVALID_STATE, "Dispatcher already running");
      return MCP_ERROR_INVALID_STATE;
    }

    impl->running = true;
    impl->dispatcher_thread_id = std::this_thread::get_id();

    // Create a timer to stop the dispatcher after timeout
    auto timer =
        impl->dispatcher->createTimer([impl]() { impl->dispatcher->exit(); });
    timer->enableTimer(std::chrono::milliseconds(timeout_ms));

    // Run the event loop
    impl->dispatcher->run(mcp::event::RunType::Block);

    impl->running = false;
    return MCP_OK;
  });
}

void mcp_dispatcher_stop(mcp_dispatcher_t dispatcher) MCP_NOEXCEPT {
  if (!dispatcher)
    return;

  auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
  if (impl->running) {
    impl->dispatcher->exit();
  }
}

mcp_result_t mcp_dispatcher_post(mcp_dispatcher_t dispatcher,
                                 mcp_callback_t callback,
                                 void* user_data) MCP_NOEXCEPT {
  CHECK_HANDLE_VALID(dispatcher);

  TRY_WITH_RAII({
    auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);

    impl->dispatcher->post([callback, user_data]() {
      if (callback) {
        callback(user_data);
      }
    });

    return MCP_OK;
  });
}

mcp_bool_t mcp_dispatcher_is_thread(mcp_dispatcher_t dispatcher) MCP_NOEXCEPT {
  if (!dispatcher)
    return MCP_FALSE;

  auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
  return (std::this_thread::get_id() == impl->dispatcher_thread_id) ? MCP_TRUE : MCP_FALSE;
}

uint64_t mcp_dispatcher_create_timer(mcp_dispatcher_t dispatcher,
                                     mcp_timer_callback_t callback,
                                     void* user_data) MCP_NOEXCEPT {
  if (!dispatcher || !callback) {
    ErrorManager::SetError(MCP_ERROR_INVALID_ARGUMENT, "Invalid parameters");
    return 0;
  }

  try {
    auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);

    // Create timer with callback wrapper
    auto timer = impl->dispatcher->createTimer(
        [callback, user_data]() { callback(user_data); });

    // Store timer info
    uint64_t timer_id = impl->next_timer_id++;
    impl->timers[timer_id] = std::make_unique<mcp::c_api::mcp_dispatcher_impl::TimerInfo>(
        std::move(timer), callback, user_data);

    return timer_id;
  } catch (const std::exception& e) {
    ErrorManager::SetError(MCP_ERROR_UNKNOWN, e.what());
    return 0;
  }
}

mcp_result_t mcp_dispatcher_enable_timer(mcp_dispatcher_t dispatcher,
                                         uint64_t timer_id,
                                         uint32_t timeout_ms,
                                         mcp_bool_t repeat) MCP_NOEXCEPT {
  CHECK_HANDLE_VALID(dispatcher);

  TRY_WITH_RAII({
    auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);

    auto it = impl->timers.find(timer_id);
    if (it == impl->timers.end()) {
      ErrorManager::SetError(MCP_ERROR_NOT_FOUND, "Timer not found");
      return MCP_ERROR_NOT_FOUND;
    }

    // TODO: Repeating timers not supported in current API
    // For now, just use one-shot timer
    it->second->timer->enableTimer(std::chrono::milliseconds(timeout_ms));

    return MCP_OK;
  });
}

void mcp_dispatcher_disable_timer(mcp_dispatcher_t dispatcher,
                                  uint64_t timer_id) MCP_NOEXCEPT {
  if (!dispatcher)
    return;

  auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
  auto it = impl->timers.find(timer_id);
  if (it != impl->timers.end()) {
    it->second->timer->disableTimer();
  }
}

void mcp_dispatcher_destroy_timer(mcp_dispatcher_t dispatcher,
                                  uint64_t timer_id) MCP_NOEXCEPT {
  if (!dispatcher)
    return;

  auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
  impl->timers.erase(timer_id);
}

void mcp_dispatcher_destroy(mcp_dispatcher_t dispatcher) MCP_NOEXCEPT {
  if (!dispatcher)
    return;

  auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);

  // Stop if running
  if (impl->running) {
    impl->dispatcher->exit();
  }

  // Clear all timers
  impl->timers.clear();

  // Release the handle
  impl->Release();
}

/* ============================================================================
 * Buffer Operations - implementations use existing RAII infrastructure
 * ============================================================================
 */

// Buffer implementation using existing RAII patterns
struct mcp_buffer_impl {
    size_t capacity_;
    size_t size_;
    std::unique_ptr<char[]> data_;
    
    explicit mcp_buffer_impl(size_t capacity) 
        : capacity_(capacity), size_(0) {
        if (capacity > 0) {
            data_ = std::make_unique<char[]>(capacity);
        }
    }
    
    ~mcp_buffer_impl() = default;
};

mcp_buffer_t* mcp_buffer_create(size_t capacity) MCP_NOEXCEPT {
    if (!mcp_is_initialized()) {
        ErrorManager::SetError(MCP_ERROR_NOT_INITIALIZED, "MCP library not initialized");
        return nullptr;
    }
    
    try {
        auto buffer = std::make_unique<mcp_buffer_impl>(capacity);
        auto result = new mcp_buffer_t(buffer.release());
        return result;
    } catch (...) {
        ErrorManager::SetError(MCP_ERROR_NO_MEMORY, "Failed to create buffer");
        return nullptr;
    }
}

void mcp_buffer_free(mcp_buffer_t* buffer) MCP_NOEXCEPT {
    if (!buffer || !*buffer) 
        return;
    
    auto impl = reinterpret_cast<mcp_buffer_impl*>(*buffer);
    delete impl;
    *buffer = nullptr;
}

}  // extern "C"