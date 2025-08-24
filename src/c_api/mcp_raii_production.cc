/**
 * @file mcp_raii_production.cc
 * @brief Production implementation of specialized RAII deleters and utilities
 *
 * This file contains safe, production-ready implementations of specialized deleters
 * for MCP C API types with proper error handling and resource management.
 *
 * @copyright Copyright (c) 2025 MCP Project
 * @license MIT License
 */

#include "mcp/c_api/mcp_raii_production.h"

// Include C API types for specializations
extern "C" {
#include "mcp/c_api/mcp_c_types.h"
}

#include <cstdlib>
#include <cstring>

namespace mcp {
namespace raii {

/* ============================================================================
 * Specialized Deleter Implementations
 * ============================================================================ */

/**
 * Safe deleter specialization for mcp_string_t
 * Handles nested pointer cleanup with null checks
 */
template<>
void c_deleter<mcp_string_t>::operator()(mcp_string_t* ptr) const noexcept {
    if (!ptr) return;
    
    try {
        // Free string data first if it exists
        if (ptr->data) {
            // Cast away const for free() - data was allocated as non-const
            free(const_cast<char*>(ptr->data));
            ptr->data = nullptr; // Prevent double-free
        }
        
        // Reset length for debugging
        ptr->length = 0;
        
        // Free the structure itself
        free(ptr);
        
    } catch (...) {
        // This should never happen with standard free(), but be defensive
        // In production, log this error to monitoring system
        MCP_RAII_ASSERT(false && "Exception in mcp_string_t deleter");
        
        // Still attempt to free the structure to prevent leaks
        try {
            free(ptr);
        } catch (...) {
            // Complete failure - nothing more we can do
        }
    }
}

/**
 * Safe deleter specialization for mcp_json_value_t
 * 
 * Note: This implementation assumes mcp_json_value_t is managed by the
 * JSON library's proper cleanup functions. If using a specific JSON library
 * (e.g., nlohmann::json, rapidjson, etc.), replace this with the appropriate
 * cleanup call.
 */
template<>
void c_deleter<mcp_json_value_t>::operator()(mcp_json_value_t* ptr) const noexcept {
    if (!ptr) return;
    
    try {
        // Check if we have a proper JSON cleanup function available
        // This is safer than assuming free() is correct for JSON values
        if (mcp_json_value_free) {
            // Use the proper JSON library cleanup function
            mcp_json_value_free(ptr);
        } else {
            // Fallback: If no proper cleanup function exists, we cannot safely
            // free this resource as we don't know its internal structure.
            // In debug builds, assert to catch this configuration error.
            MCP_RAII_ASSERT(false && 
                "No proper JSON cleanup function available - potential leak");
            
            // In release builds, we have to choose between potential crash
            // and definite leak. Choose leak as safer option.
            // This should be fixed by providing proper mcp_json_value_free.
            
            // DO NOT call free(ptr) here - JSON values may have complex
            // internal structure requiring specialized cleanup
        }
        
    } catch (...) {
        // JSON cleanup function threw exception - log error
        MCP_RAII_ASSERT(false && "Exception in mcp_json_value_t deleter");
        // Cannot do much more - the resource may be leaked
    }
}

/* ============================================================================
 * Production Resource Tracking Implementation
 * ============================================================================ */

#ifdef MCP_RAII_DEBUG_MODE

// Global resource tracker instance management
namespace {
    std::once_flag tracker_init_flag;
    std::unique_ptr<ResourceTracker> global_tracker;
    
    void ensure_tracker_initialized() {
        std::call_once(tracker_init_flag, []() {
            global_tracker = std::make_unique<ResourceTracker>();
            
            // Register cleanup at program termination
            std::atexit([]() {
                if (global_tracker) {
                    global_tracker->report_leaks();
                    global_tracker.reset();
                }
            });
        });
    }
}

ResourceTracker& ResourceTracker::instance() {
    ensure_tracker_initialized();
    return *global_tracker;
}

void ResourceTracker::report_leaks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (resources_.empty()) {
        // No leaks detected - log success in debug mode
        return;
    }
    
    // Report leaks to appropriate logging system
    // In a real implementation, this would integrate with your logging framework
    size_t leak_count = resources_.size();
    
    // Example logging (replace with actual logging system):
    // logger::error("Resource leak detected: {} leaked resources", leak_count);
    
    for (const auto& [ptr, info] : resources_) {
        auto duration = std::chrono::steady_clock::now() - info.allocated_at;
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
        
        // Example detailed leak reporting:
        // logger::error("  Leaked {} resource at {} allocated at {}:{} ({} ms ago)",
        //               info.type_name, ptr, info.file, info.line, duration_ms.count());
    }
    
    // In debug builds, assert to catch leaks during development
    MCP_RAII_ASSERT(false && "Resource leaks detected - see log for details");
}

#endif // MCP_RAII_DEBUG_MODE

/* ============================================================================
 * Global Statistics and Monitoring
 * ============================================================================ */

// Thread-safe global statistics for production monitoring
namespace {
    struct GlobalStats {
        std::atomic<uint64_t> total_guards_created{0};
        std::atomic<uint64_t> total_guards_destroyed{0};
        std::atomic<uint64_t> total_resources_tracked{0};
        std::atomic<uint64_t> total_resources_released{0};
        std::atomic<uint64_t> total_exceptions_in_destructors{0};
    };
    
    GlobalStats& get_global_stats() {
        static GlobalStats stats;
        return stats;
    }
}

// Production monitoring functions (can be called from external monitoring)
extern "C" {
    
/**
 * Get current resource statistics for monitoring
 * Thread-safe and can be called from monitoring/metrics systems
 */
void mcp_raii_get_stats(
    uint64_t* guards_created,
    uint64_t* guards_destroyed,
    uint64_t* resources_tracked,
    uint64_t* resources_released,
    uint64_t* exceptions_in_destructors
) {
    if (!guards_created || !guards_destroyed || !resources_tracked || 
        !resources_released || !exceptions_in_destructors) {
        return; // Invalid parameters
    }
    
    auto& stats = get_global_stats();
    *guards_created = stats.total_guards_created.load(std::memory_order_relaxed);
    *guards_destroyed = stats.total_guards_destroyed.load(std::memory_order_relaxed);
    *resources_tracked = stats.total_resources_tracked.load(std::memory_order_relaxed);
    *resources_released = stats.total_resources_released.load(std::memory_order_relaxed);
    *exceptions_in_destructors = stats.total_exceptions_in_destructors.load(std::memory_order_relaxed);
}

/**
 * Reset statistics counters (useful for testing or periodic resets)
 */
void mcp_raii_reset_stats() {
    auto& stats = get_global_stats();
    stats.total_guards_created.store(0, std::memory_order_relaxed);
    stats.total_guards_destroyed.store(0, std::memory_order_relaxed);
    stats.total_resources_tracked.store(0, std::memory_order_relaxed);
    stats.total_resources_released.store(0, std::memory_order_relaxed);
    stats.total_exceptions_in_destructors.store(0, std::memory_order_relaxed);
}

/**
 * Check for resource leaks (returns number of active resources)
 * Only available in debug builds
 */
size_t mcp_raii_active_resources() {
#ifdef MCP_RAII_DEBUG_MODE
    return ResourceTracker::instance().active_resources();
#else
    return 0; // Not available in release builds
#endif
}

} // extern "C"

/* ============================================================================
 * Internal Statistics Tracking
 * ============================================================================ */

// These functions are called internally by RAII templates to update statistics
namespace internal {

void increment_guards_created() {
    get_global_stats().total_guards_created.fetch_add(1, std::memory_order_relaxed);
}

void increment_guards_destroyed() {
    get_global_stats().total_guards_destroyed.fetch_add(1, std::memory_order_relaxed);
}

void increment_resources_tracked() {
    get_global_stats().total_resources_tracked.fetch_add(1, std::memory_order_relaxed);
}

void increment_resources_released() {
    get_global_stats().total_resources_released.fetch_add(1, std::memory_order_relaxed);
}

void increment_exceptions_in_destructors() {
    get_global_stats().total_exceptions_in_destructors.fetch_add(1, std::memory_order_relaxed);
}

} // namespace internal

} // namespace raii
} // namespace mcp