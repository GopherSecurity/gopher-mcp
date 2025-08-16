/**
 * @file mcp_c_api_core.cc
 * @brief Core C API implementation - Library initialization and dispatcher
 */

#include "mcp/c_api/mcp_c_bridge.h"
#include "mcp/event/libevent_dispatcher.h"
#include <atomic>
#include <mutex>

namespace mcp {
namespace c_api {

// Thread-local error storage implementation
thread_local std::string ErrorManager::thread_local_error_;

// Global library state
static std::atomic<bool> g_initialized{false};
static std::mutex g_init_mutex;

} // namespace c_api
} // namespace mcp

using namespace mcp::c_api;

/* ============================================================================
 * Library Initialization & Cleanup
 * ============================================================================ */

extern "C" {

mcp_result_t mcp_init(const mcp_allocator_t* allocator) {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    
    if (g_initialized.load()) {
        return MCP_OK; // Already initialized
    }
    
    try {
        // Set custom allocator if provided
        if (allocator) {
            GlobalAllocator::instance().set_allocator(allocator);
        }
        
        // Initialize any global resources here
        // For now, we just mark as initialized
        g_initialized.store(true);
        
        return MCP_OK;
    } catch (const std::exception& e) {
        ErrorManager::set_error(e.what());
        return MCP_ERROR;
    }
}

void mcp_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    
    if (!g_initialized.load()) {
        return; // Not initialized
    }
    
    // Clean up global resources
    g_initialized.store(false);
}

const char* mcp_get_version(void) {
    return "1.0.0"; // TODO: Use actual version from build system
}

const char* mcp_get_last_error(void) {
    return ErrorManager::get_error();
}

/* ============================================================================
 * Event Loop & Dispatcher
 * ============================================================================ */

mcp_dispatcher_t mcp_dispatcher_create(void) {
    if (!g_initialized.load()) {
        ErrorManager::set_error("Library not initialized");
        return nullptr;
    }
    
    TRY_CATCH_NULL({
        auto impl = new mcp::c_api::mcp_dispatcher_impl();
        
        // Create libevent dispatcher
        impl->dispatcher = std::make_unique<mcp::event::LibeventDispatcher>("mcp_c_api");
        impl->dispatcher_thread_id = std::this_thread::get_id();
        
        return reinterpret_cast<mcp_dispatcher_t>(impl);
    });
}

mcp_result_t mcp_dispatcher_run(mcp_dispatcher_t dispatcher) {
    CHECK_HANDLE(dispatcher);
    
    TRY_CATCH({
        auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
        
        if (impl->running) {
            ErrorManager::set_error("Dispatcher already running");
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

mcp_result_t mcp_dispatcher_run_timeout(mcp_dispatcher_t dispatcher, uint32_t timeout_ms) {
    CHECK_HANDLE(dispatcher);
    
    TRY_CATCH({
        auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
        
        if (impl->running) {
            ErrorManager::set_error("Dispatcher already running");
            return MCP_ERROR_INVALID_STATE;
        }
        
        impl->running = true;
        impl->dispatcher_thread_id = std::this_thread::get_id();
        
        // Create a timer to stop the dispatcher after timeout
        auto timer = impl->dispatcher->createTimer(
            [impl]() {
                impl->dispatcher->exit();
            }
        );
        timer->enableTimer(std::chrono::milliseconds(timeout_ms));
        
        // Run the event loop
        impl->dispatcher->run(mcp::event::RunType::Block);
        
        impl->running = false;
        return MCP_OK;
    });
}

void mcp_dispatcher_stop(mcp_dispatcher_t dispatcher) {
    if (!dispatcher) return;
    
    auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
    if (impl->running) {
        impl->dispatcher->exit();
    }
}

mcp_result_t mcp_dispatcher_post(
    mcp_dispatcher_t dispatcher,
    mcp_callback_t callback,
    void* user_data
) {
    CHECK_HANDLE(dispatcher);
    
    TRY_CATCH({
        auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
        
        impl->dispatcher->post([callback, user_data]() {
            if (callback) {
                callback(user_data);
            }
        });
        
        return MCP_OK;
    });
}

bool mcp_dispatcher_is_thread(mcp_dispatcher_t dispatcher) {
    if (!dispatcher) return false;
    
    auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
    return std::this_thread::get_id() == impl->dispatcher_thread_id;
}

uint64_t mcp_dispatcher_create_timer(
    mcp_dispatcher_t dispatcher,
    mcp_timer_callback_t callback,
    void* user_data
) {
    if (!dispatcher || !callback) {
        ErrorManager::set_error("Invalid parameters");
        return 0;
    }
    
    try {
        auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
        
        // Create timer with callback wrapper
        auto timer = impl->dispatcher->createTimer(
            [callback, user_data]() {
                callback(user_data);
            }
        );
        
        // Store timer info
        uint64_t timer_id = impl->next_timer_id++;
        impl->timers[timer_id] = {
            std::move(timer),
            callback,
            user_data
        };
        
        return timer_id;
    } catch (const std::exception& e) {
        ErrorManager::set_error(e.what());
        return 0;
    }
}

mcp_result_t mcp_dispatcher_enable_timer(
    mcp_dispatcher_t dispatcher,
    uint64_t timer_id,
    uint32_t timeout_ms,
    bool repeat
) {
    CHECK_HANDLE(dispatcher);
    
    TRY_CATCH({
        auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
        
        auto it = impl->timers.find(timer_id);
        if (it == impl->timers.end()) {
            ErrorManager::set_error("Timer not found");
            return MCP_ERROR_NOT_FOUND;
        }
        
        // TODO: Repeating timers not supported in current API
        // For now, just use one-shot timer
        it->second.timer->enableTimer(std::chrono::milliseconds(timeout_ms));
        
        return MCP_OK;
    });
}

void mcp_dispatcher_disable_timer(mcp_dispatcher_t dispatcher, uint64_t timer_id) {
    if (!dispatcher) return;
    
    auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
    auto it = impl->timers.find(timer_id);
    if (it != impl->timers.end()) {
        it->second.timer->disableTimer();
    }
}

void mcp_dispatcher_destroy_timer(mcp_dispatcher_t dispatcher, uint64_t timer_id) {
    if (!dispatcher) return;
    
    auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
    impl->timers.erase(timer_id);
}

void mcp_dispatcher_destroy(mcp_dispatcher_t dispatcher) {
    if (!dispatcher) return;
    
    auto impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
    
    // Stop if running
    if (impl->running) {
        impl->dispatcher->exit();
    }
    
    // Clear all timers
    impl->timers.clear();
    
    // Release the handle
    impl->release();
}

} // extern "C"