#pragma once

/**
 * @file mcp_raii.h
 * @brief Production-quality RAII utilities for C API resource management
 *
 * This header provides safe, efficient RAII (Resource Acquisition Is Initialization)
 * utilities for managing C API resources with proper exception safety and move semantics.
 * All utilities are designed for use in C FFI contexts with deterministic cleanup.
 *
 * Features:
 * - ResourceGuard: Generic RAII wrapper with custom deleters
 * - AllocationTransaction: Exception-safe multi-resource allocation
 * - Specialized deleters for common C types
 * - Thread-safe resource management
 * - C++14/17 compatibility with optional features
 *
 * Usage:
 *   auto guard = make_resource_guard(malloc(size), free);
 *   auto txn = AllocationTransaction();
 *   txn.track(resource1, deleter1);
 *   txn.commit(); // Success - prevent cleanup
 *
 * @copyright Copyright (c) 2025 MCP Project
 * @license MIT License
 */

#include <memory>
#include <utility>
#include <vector>
#include <functional>
#include <type_traits>
#include <atomic>
#include <mutex>

// Note: C API types are included by implementation file

namespace mcp {
namespace raii {

/* ============================================================================
 * Compatibility and Feature Detection
 * ============================================================================ */

// C++17 feature detection for std::exchange
#if __cplusplus >= 201703L
    using std::exchange;
    #define MCP_RAII_NODISCARD [[nodiscard]]
#else
    // C++14 implementation of std::exchange
    template<class T, class U = T>
    T exchange(T& obj, U&& new_value) {
        T old_value = std::move(obj);
        obj = std::forward<U>(new_value);
        return old_value;
    }
    #define MCP_RAII_NODISCARD
#endif

/* ============================================================================
 * Core RAII Deleter System
 * ============================================================================ */

/**
 * Generic deleter for C API resources
 * Provides safe cleanup with null pointer checks
 */
template<typename T>
struct c_deleter {
    void operator()(T* ptr) const noexcept {
        if (ptr) {
            free(ptr);
        }
    }
};

// Note: Specialized deleters are implemented in the implementation file
// They are specialized for mcp_string_t and mcp_json_value_t types

/**
 * Thread-safe deleter wrapper for multi-threaded environments
 * Ensures atomic cleanup operations
 */
template<typename T, typename Deleter = c_deleter<T>>
struct thread_safe_deleter {
    void operator()(T* ptr) const noexcept {
        static std::mutex cleanup_mutex;
        if (ptr) {
            std::lock_guard<std::mutex> lock(cleanup_mutex);
            Deleter{}(ptr);
        }
    }
};

/**
 * Safe unique_ptr alias for C API resources
 */
template<typename T>
using c_unique_ptr = std::unique_ptr<T, c_deleter<T>>;

/**
 * Thread-safe unique_ptr alias for concurrent usage
 */
template<typename T>
using c_unique_ptr_threadsafe = std::unique_ptr<T, thread_safe_deleter<T>>;

/* ============================================================================
 * Advanced RAII Resource Guard
 * ============================================================================ */

/**
 * Generic RAII resource guard with custom cleanup
 * 
 * Provides deterministic resource cleanup with move semantics.
 * Supports custom deleters and null-safe operations.
 * 
 * @tparam T Resource type
 */
template<typename T>
class ResourceGuard {
public:
    using resource_type = T;
    using deleter_type = std::function<void(T*)>;
    
    // Constructors
    ResourceGuard() noexcept : ptr_(nullptr) {}
    
    explicit ResourceGuard(T* ptr) noexcept 
        : ptr_(ptr), deleter_([](T* p) { c_deleter<T>{}(p); }) {}
    
    ResourceGuard(T* ptr, deleter_type deleter) noexcept
        : ptr_(ptr), deleter_(std::move(deleter)) {}
    
    // Destructor - automatic cleanup
    ~ResourceGuard() {
        reset();
    }
    
    // Move-only semantics for safety
    ResourceGuard(const ResourceGuard&) = delete;
    ResourceGuard& operator=(const ResourceGuard&) = delete;
    
    ResourceGuard(ResourceGuard&& other) noexcept
        : ptr_(exchange(other.ptr_, nullptr)),
          deleter_(std::move(other.deleter_)) {}
    
    ResourceGuard& operator=(ResourceGuard&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = exchange(other.ptr_, nullptr);
            deleter_ = std::move(other.deleter_);
        }
        return *this;
    }
    
    // Resource access
    MCP_RAII_NODISCARD T* get() const noexcept { return ptr_; }
    MCP_RAII_NODISCARD T* release() noexcept { return exchange(ptr_, nullptr); }
    MCP_RAII_NODISCARD explicit operator bool() const noexcept { return ptr_ != nullptr; }
    
    // Resource management
    void reset(T* new_ptr = nullptr) {
        if (ptr_ && deleter_) {
            deleter_(ptr_);
        }
        ptr_ = new_ptr;
    }
    
    void reset(T* new_ptr, deleter_type new_deleter) {
        reset(new_ptr);
        deleter_ = std::move(new_deleter);
    }
    
    // Swap operation
    void swap(ResourceGuard& other) noexcept {
        std::swap(ptr_, other.ptr_);
        std::swap(deleter_, other.deleter_);
    }
    
private:
    T* ptr_;
    deleter_type deleter_;
};

/**
 * Create a ResourceGuard with automatic type deduction
 */
template<typename T, typename Deleter>
MCP_RAII_NODISCARD ResourceGuard<T> make_resource_guard(T* ptr, Deleter deleter) {
    return ResourceGuard<T>(ptr, deleter);
}

template<typename T>
MCP_RAII_NODISCARD ResourceGuard<T> make_resource_guard(T* ptr) {
    return ResourceGuard<T>(ptr);
}

/* ============================================================================
 * Transaction-Based Resource Management
 * ============================================================================ */

/**
 * Exception-safe transaction manager for multi-resource allocation
 * 
 * Provides all-or-nothing semantics for resource allocation with
 * automatic rollback on destruction if not committed.
 * 
 * Thread-safe for concurrent usage.
 */
class AllocationTransaction {
public:
    using deleter_func = std::function<void(void*)>;
    
    AllocationTransaction() = default;
    
    ~AllocationTransaction() {
        if (!committed_.load(std::memory_order_acquire)) {
            rollback();
        }
    }
    
    // Non-copyable, movable
    AllocationTransaction(const AllocationTransaction&) = delete;
    AllocationTransaction& operator=(const AllocationTransaction&) = delete;
    
    AllocationTransaction(AllocationTransaction&& other) noexcept
        : resources_(std::move(other.resources_)),
          committed_(other.committed_.load(std::memory_order_acquire)) {
        other.committed_.store(true, std::memory_order_release);
    }
    
    AllocationTransaction& operator=(AllocationTransaction&& other) noexcept {
        if (this != &other) {
            if (!committed_.load(std::memory_order_acquire)) {
                rollback();
            }
            resources_ = std::move(other.resources_);
            committed_.store(other.committed_.load(std::memory_order_acquire), 
                           std::memory_order_release);
            other.committed_.store(true, std::memory_order_release);
        }
        return *this;
    }
    
    /**
     * Track a resource for cleanup
     * Thread-safe operation
     */
    void track(void* resource, deleter_func deleter) {
        if (resource && deleter) {
            std::lock_guard<std::mutex> lock(mutex_);
            resources_.emplace_back(resource, std::move(deleter));
        }
    }
    
    /**
     * Track a typed resource with automatic deleter
     */
    template<typename T>
    void track(T* resource) {
        track(resource, [](void* ptr) { 
            c_deleter<T>{}(static_cast<T*>(ptr)); 
        });
    }
    
    /**
     * Commit transaction - prevent automatic cleanup
     * Thread-safe operation
     */
    void commit() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        resources_.clear();
        committed_.store(true, std::memory_order_release);
    }
    
    /**
     * Manual rollback - cleanup all tracked resources
     * Thread-safe operation, can be called multiple times
     */
    void rollback() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!committed_.load(std::memory_order_acquire)) {
            // Cleanup in reverse order (LIFO)
            for (auto it = resources_.rbegin(); it != resources_.rend(); ++it) {
                try {
                    if (it->second) {
                        it->second(it->first);
                    }
                } catch (...) {
                    // Ignore exceptions during cleanup
                }
            }
            resources_.clear();
            committed_.store(true, std::memory_order_release);
        }
    }
    
    /**
     * Check if transaction is committed
     */
    MCP_RAII_NODISCARD bool is_committed() const noexcept {
        return committed_.load(std::memory_order_acquire);
    }
    
    /**
     * Get number of tracked resources
     */
    MCP_RAII_NODISCARD size_t resource_count() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return resources_.size();
    }
    
private:
    mutable std::mutex mutex_;
    std::vector<std::pair<void*, deleter_func>> resources_;
    std::atomic<bool> committed_{false};
};

/* ============================================================================
 * Scoped Resource Manager
 * ============================================================================ */

/**
 * Scoped resource manager for automatic cleanup
 * Executes cleanup function on scope exit
 */
class ScopedCleanup {
public:
    using cleanup_func = std::function<void()>;
    
    explicit ScopedCleanup(cleanup_func cleanup) 
        : cleanup_(std::move(cleanup)), active_(true) {}
    
    ~ScopedCleanup() {
        if (active_ && cleanup_) {
            try {
                cleanup_();
            } catch (...) {
                // Ignore exceptions during cleanup
            }
        }
    }
    
    // Non-copyable, movable
    ScopedCleanup(const ScopedCleanup&) = delete;
    ScopedCleanup& operator=(const ScopedCleanup&) = delete;
    
    ScopedCleanup(ScopedCleanup&& other) noexcept
        : cleanup_(std::move(other.cleanup_)), active_(other.active_) {
        other.active_ = false;
    }
    
    ScopedCleanup& operator=(ScopedCleanup&& other) noexcept {
        if (this != &other) {
            if (active_ && cleanup_) cleanup_();
            cleanup_ = std::move(other.cleanup_);
            active_ = other.active_;
            other.active_ = false;
        }
        return *this;
    }
    
    // Cancel cleanup
    void release() noexcept { active_ = false; }
    
    // Check if cleanup is active
    MCP_RAII_NODISCARD bool is_active() const noexcept { return active_; }
    
private:
    cleanup_func cleanup_;
    bool active_;
};

/**
 * Create a scoped cleanup with automatic type deduction
 */
template<typename Func>
MCP_RAII_NODISCARD ScopedCleanup make_scoped_cleanup(Func&& func) {
    return ScopedCleanup(std::forward<Func>(func));
}

/* ============================================================================
 * Utility Macros for Common Patterns
 * ============================================================================ */

/**
 * RAII_GUARD - Create a resource guard with automatic naming
 */
#define RAII_GUARD(var, resource, deleter) \
    auto var = mcp::raii::make_resource_guard(resource, deleter)

/**
 * RAII_TRANSACTION - Create a transaction with automatic rollback
 */
#define RAII_TRANSACTION() mcp::raii::AllocationTransaction{}

/**
 * RAII_CLEANUP - Create scoped cleanup with lambda
 */
#define RAII_CLEANUP(cleanup_code) \
    auto cleanup_guard = mcp::raii::make_scoped_cleanup([&]() { cleanup_code; })

} // namespace raii
} // namespace mcp

/* ============================================================================
 * Implementation Details
 * ============================================================================ */

// Include implementation file for specialized deleters
// This ensures the implementation is compiled separately
#ifdef MCP_RAII_IMPLEMENTATION
#include "mcp_raii_impl.h"
#endif