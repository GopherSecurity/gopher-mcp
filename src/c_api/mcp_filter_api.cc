/**
 * @file mcp_filter_api.cc
 * @brief Implementation of C API for MCP C++ Filter Architecture
 */

#include "mcp/c_api/mcp_filter_api.h"
#include "mcp/c_api/mcp_raii.h"
#include "mcp/network/filter.h"
#include "mcp/network/connection.h"
#include "mcp/buffer.h"
#include "mcp/event/event_loop.h"
#include "handle_manager.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <cstring>

namespace mcp {
namespace filter_api {

using c_api_internal::HandleManager;

// ============================================================================
// Handle Management System
// ============================================================================

// Global handle managers
static HandleManager<network::Filter> g_filter_manager;
HandleManager<Buffer> g_buffer_manager;  // Made non-static for external linkage
static HandleManager<event::Dispatcher> g_dispatcher_manager;

// ============================================================================
// Filter Chain Management
// ============================================================================

class FilterChain {
public:
    FilterChain() = default;
    
    void addFilter(std::shared_ptr<network::Filter> filter,
                   mcp_filter_position_t position,
                   std::shared_ptr<network::Filter> reference = nullptr) {
        switch (position) {
            case MCP_FILTER_POSITION_FIRST:
                filters_.insert(filters_.begin(), filter);
                break;
            case MCP_FILTER_POSITION_LAST:
                filters_.push_back(filter);
                break;
            case MCP_FILTER_POSITION_BEFORE:
                if (reference) {
                    auto it = std::find(filters_.begin(), filters_.end(), reference);
                    if (it != filters_.end()) {
                        filters_.insert(it, filter);
                    }
                }
                break;
            case MCP_FILTER_POSITION_AFTER:
                if (reference) {
                    auto it = std::find(filters_.begin(), filters_.end(), reference);
                    if (it != filters_.end()) {
                        filters_.insert(++it, filter);
                    }
                }
                break;
        }
    }
    
    const std::vector<std::shared_ptr<network::Filter>>& getFilters() const {
        return filters_;
    }
    
private:
    std::vector<std::shared_ptr<network::Filter>> filters_;
};

static HandleManager<FilterChain> g_filter_chain_manager;

// ============================================================================
// Filter Chain Builder
// ============================================================================

struct mcp_filter_chain_builder {
    mcp_dispatcher_t dispatcher;
    std::unique_ptr<FilterChain> chain;
    
    mcp_filter_chain_builder(mcp_dispatcher_t disp) 
        : dispatcher(disp), chain(std::make_unique<FilterChain>()) {}
};

// ============================================================================
// Custom C++ Filter Implementation
// ============================================================================

class CApiFilter : public network::NetworkFilterBase {
public:
    CApiFilter(const mcp_filter_callbacks_t& callbacks)
        : callbacks_(callbacks), handle_(0) {}
    
    void setHandle(mcp_filter_t h) { handle_ = h; }
    
    // ReadFilter interface
    network::FilterStatus onData(Buffer& data, bool end_stream) override {
        if (!callbacks_.on_data) {
            return network::FilterStatus::Continue;
        }
        
        // Store buffer and get handle
        auto buffer_handle = g_buffer_manager.store(
            std::shared_ptr<Buffer>(&data, [](Buffer*){}));  // Non-owning shared_ptr
        
        // Call callback in dispatcher thread
        mcp_filter_status_t status = callbacks_.on_data(
            buffer_handle, 
            end_stream ? MCP_TRUE : MCP_FALSE,
            callbacks_.user_data);
        
        // Release temporary handle
        g_buffer_manager.release(buffer_handle);
        
        return (status == MCP_FILTER_CONTINUE) 
            ? network::FilterStatus::Continue 
            : network::FilterStatus::StopIteration;
    }
    
    network::FilterStatus onNewConnection() override {
        if (!callbacks_.on_new_connection) {
            return network::FilterStatus::Continue;
        }
        
        mcp_filter_status_t status = callbacks_.on_new_connection(
            MCP_CONNECTION_STATE_CONNECTED,
            callbacks_.user_data);
        
        return (status == MCP_FILTER_CONTINUE) 
            ? network::FilterStatus::Continue 
            : network::FilterStatus::StopIteration;
    }
    
    // WriteFilter interface  
    network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
        if (!callbacks_.on_write) {
            return network::FilterStatus::Continue;
        }
        
        // Store buffer and get handle
        auto buffer_handle = g_buffer_manager.store(
            std::shared_ptr<Buffer>(&data, [](Buffer*){}));  // Non-owning shared_ptr
        
        mcp_filter_status_t status = callbacks_.on_write(
            buffer_handle,
            end_stream ? MCP_TRUE : MCP_FALSE,
            callbacks_.user_data);
        
        // Release temporary handle
        g_buffer_manager.release(buffer_handle);
        
        return (status == MCP_FILTER_CONTINUE) 
            ? network::FilterStatus::Continue 
            : network::FilterStatus::StopIteration;
    }
    
    void onError(mcp_filter_error_t error, const std::string& message) {
        if (callbacks_.on_error) {
            callbacks_.on_error(handle_, error, message.c_str(), callbacks_.user_data);
        }
    }
    
private:
    mcp_filter_callbacks_t callbacks_;
    mcp_filter_t handle_;
};

// ============================================================================
// Filter Manager Wrapper
// ============================================================================

class FilterManagerWrapper {
public:
    FilterManagerWrapper(network::Connection* conn, event::Dispatcher* disp)
        : connection_(conn), dispatcher_(disp) {
        if (conn) {
            filter_manager_ = std::make_unique<network::FilterManagerImpl>(*conn, *disp);
        }
    }
    
    void addFilter(std::shared_ptr<network::Filter> filter) {
        if (filter_manager_) {
            filter_manager_->addFilter(filter);
        }
    }
    
    void addChain(std::shared_ptr<FilterChain> chain) {
        if (filter_manager_) {
            for (const auto& filter : chain->getFilters()) {
                filter_manager_->addFilter(filter);
            }
        }
    }
    
    bool initialize() {
        if (filter_manager_) {
            return filter_manager_->initializeReadFilters();
        }
        return false;
    }
    
private:
    network::Connection* connection_;
    event::Dispatcher* dispatcher_;
    std::unique_ptr<network::FilterManagerImpl> filter_manager_;
};

static HandleManager<FilterManagerWrapper> g_filter_manager_manager;

// ============================================================================
// Buffer Pool Implementation
// ============================================================================

struct BufferPool {
    BufferPool(size_t buffer_size, size_t max_buffers)
        : buffer_size_(buffer_size), max_buffers_(max_buffers) {
        // Pre-allocate buffers
        for (size_t i = 0; i < max_buffers; ++i) {
            auto buffer = std::make_shared<OwnedBuffer>();
            // Buffers will be sized on demand
            free_buffers_.push_back(buffer);
        }
    }
    
    std::shared_ptr<Buffer> acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!free_buffers_.empty()) {
            auto buffer = free_buffers_.back();
            free_buffers_.pop_back();
            return buffer;
        }
        return nullptr;
    }
    
    void release(std::shared_ptr<Buffer> buffer) {
        if (!buffer) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        buffer->drain(buffer->length());  // Clear buffer
        if (free_buffers_.size() < max_buffers_) {
            free_buffers_.push_back(buffer);
        }
    }
    
private:
    size_t buffer_size_;
    size_t max_buffers_;
    std::mutex mutex_;
    std::vector<std::shared_ptr<Buffer>> free_buffers_;
};

// ============================================================================
// Resource Guard Implementation
// ============================================================================

struct mcp_filter_resource_guard {
    std::unique_ptr<raii::AllocationTransaction> transaction;
    mcp_memory_pool_t pool;
    std::vector<mcp_filter_t> tracked_filters;
    
    mcp_filter_resource_guard(mcp_dispatcher_t disp) 
        : transaction(std::make_unique<raii::AllocationTransaction>()),
          pool(mcp_memory_pool_create(1024 * 1024)) {  // 1MB pool
    }
    
    ~mcp_filter_resource_guard() {
        // Clean up tracked filters
        for (auto filter : tracked_filters) {
            mcp_filter_release(filter);
        }
        if (pool) {
            mcp_memory_pool_destroy(pool);
        }
    }
};

} // namespace filter_api
} // namespace mcp

// ============================================================================
// C API Implementation
// ============================================================================

using namespace mcp::filter_api;

extern "C" {

// Filter Lifecycle Management

MCP_API mcp_filter_t mcp_filter_create(
    mcp_dispatcher_t dispatcher,
    const mcp_filter_config_t* config) MCP_NOEXCEPT {
    
    if (!config) return 0;
    
    try {
        // Create a default filter with empty callbacks
        mcp_filter_callbacks_t callbacks = {};
        auto filter = std::make_shared<CApiFilter>(callbacks);
        
        // Store in handle manager
        mcp_filter_t handle = g_filter_manager.store(filter);
        filter->setHandle(handle);
        
        return handle;
    } catch (...) {
        return 0;
    }
}

MCP_API mcp_filter_t mcp_filter_create_builtin(
    mcp_dispatcher_t dispatcher,
    mcp_builtin_filter_type_t type,
    mcp_json_value_t config) MCP_NOEXCEPT {
    
    try {
        // Create filter based on type
        std::shared_ptr<mcp::network::Filter> filter;
        
        switch (type) {
            case MCP_FILTER_HTTP_CODEC:
                // TODO: Create actual HTTP codec filter
                filter = std::make_shared<CApiFilter>(mcp_filter_callbacks_t{});
                break;
                
            case MCP_FILTER_RATE_LIMIT:
                // TODO: Create actual rate limit filter
                filter = std::make_shared<CApiFilter>(mcp_filter_callbacks_t{});
                break;
                
            case MCP_FILTER_CIRCUIT_BREAKER:
                // TODO: Create actual circuit breaker filter
                filter = std::make_shared<CApiFilter>(mcp_filter_callbacks_t{});
                break;
                
            default:
                // Create generic filter for now
                filter = std::make_shared<CApiFilter>(mcp_filter_callbacks_t{});
                break;
        }
        
        return g_filter_manager.store(filter);
    } catch (...) {
        return 0;
    }
}

MCP_API void mcp_filter_retain(mcp_filter_t filter) MCP_NOEXCEPT {
    g_filter_manager.retain(filter);
}

MCP_API void mcp_filter_release(mcp_filter_t filter) MCP_NOEXCEPT {
    g_filter_manager.release(filter);
}

MCP_API mcp_result_t mcp_filter_set_callbacks(
    mcp_filter_t filter,
    const mcp_filter_callbacks_t* callbacks) MCP_NOEXCEPT {
    
    if (!callbacks) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto filter_ptr = g_filter_manager.get(filter);
    if (!filter_ptr) return MCP_ERROR_NOT_FOUND;
    
    // If it's a CApiFilter, update callbacks
    auto capi_filter = std::dynamic_pointer_cast<CApiFilter>(filter_ptr);
    if (capi_filter) {
        // We'd need to add a setter method for this
        // For now, return success
        return MCP_OK;
    }
    
    return MCP_ERROR_INVALID_STATE;
}

MCP_API mcp_result_t mcp_filter_set_protocol_metadata(
    mcp_filter_t filter,
    const mcp_protocol_metadata_t* metadata) MCP_NOEXCEPT {
    
    if (!metadata) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto filter_ptr = g_filter_manager.get(filter);
    if (!filter_ptr) return MCP_ERROR_NOT_FOUND;
    
    // TODO: Store metadata in filter
    return MCP_OK;
}

MCP_API mcp_result_t mcp_filter_get_protocol_metadata(
    mcp_filter_t filter,
    mcp_protocol_metadata_t* metadata) MCP_NOEXCEPT {
    
    if (!metadata) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto filter_ptr = g_filter_manager.get(filter);
    if (!filter_ptr) return MCP_ERROR_NOT_FOUND;
    
    // TODO: Retrieve metadata from filter
    return MCP_OK;
}

// Filter Chain Management

MCP_API mcp_filter_chain_builder_t mcp_filter_chain_builder_create(
    mcp_dispatcher_t dispatcher) MCP_NOEXCEPT {
    
    try {
        return reinterpret_cast<mcp_filter_chain_builder_t>(new mcp::filter_api::mcp_filter_chain_builder(dispatcher));
    } catch (...) {
        return nullptr;
    }
}

MCP_API mcp_result_t mcp_filter_chain_add_filter(
    mcp_filter_chain_builder_t builder,
    mcp_filter_t filter,
    mcp_filter_position_t position,
    mcp_filter_t reference_filter) MCP_NOEXCEPT {
    
    if (!builder) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto filter_ptr = g_filter_manager.get(filter);
    if (!filter_ptr) return MCP_ERROR_NOT_FOUND;
    
    std::shared_ptr<mcp::network::Filter> ref_ptr;
    if (reference_filter != 0) {
        ref_ptr = g_filter_manager.get(reference_filter);
    }
    
    reinterpret_cast<mcp::filter_api::mcp_filter_chain_builder*>(builder)->chain->addFilter(filter_ptr, position, ref_ptr);
    return MCP_OK;
}

MCP_API mcp_filter_chain_t mcp_filter_chain_build(
    mcp_filter_chain_builder_t builder) MCP_NOEXCEPT {
    
    if (!builder) return 0;
    
    auto chain = std::move(reinterpret_cast<mcp::filter_api::mcp_filter_chain_builder*>(builder)->chain);
    return g_filter_chain_manager.store(std::move(chain));
}

MCP_API void mcp_filter_chain_builder_destroy(
    mcp_filter_chain_builder_t builder) MCP_NOEXCEPT {
    
    delete reinterpret_cast<mcp::filter_api::mcp_filter_chain_builder*>(builder);
}

MCP_API void mcp_filter_chain_retain(mcp_filter_chain_t chain) MCP_NOEXCEPT {
    g_filter_chain_manager.retain(chain);
}

MCP_API void mcp_filter_chain_release(mcp_filter_chain_t chain) MCP_NOEXCEPT {
    g_filter_chain_manager.release(chain);
}

// Filter Manager

MCP_API mcp_filter_manager_t mcp_filter_manager_create(
    mcp_connection_t connection,
    mcp_dispatcher_t dispatcher) MCP_NOEXCEPT {
    
    try {
        // TODO: Get actual connection and dispatcher objects
        // For now, create with nullptrs
        auto wrapper = std::make_shared<FilterManagerWrapper>(nullptr, nullptr);
        return g_filter_manager_manager.store(wrapper);
    } catch (...) {
        return 0;
    }
}

MCP_API mcp_result_t mcp_filter_manager_add_filter(
    mcp_filter_manager_t manager,
    mcp_filter_t filter) MCP_NOEXCEPT {
    
    auto manager_ptr = g_filter_manager_manager.get(manager);
    if (!manager_ptr) return MCP_ERROR_NOT_FOUND;
    
    auto filter_ptr = g_filter_manager.get(filter);
    if (!filter_ptr) return MCP_ERROR_NOT_FOUND;
    
    manager_ptr->addFilter(filter_ptr);
    return MCP_OK;
}

MCP_API mcp_result_t mcp_filter_manager_add_chain(
    mcp_filter_manager_t manager,
    mcp_filter_chain_t chain) MCP_NOEXCEPT {
    
    auto manager_ptr = g_filter_manager_manager.get(manager);
    if (!manager_ptr) return MCP_ERROR_NOT_FOUND;
    
    auto chain_ptr = g_filter_chain_manager.get(chain);
    if (!chain_ptr) return MCP_ERROR_NOT_FOUND;
    
    manager_ptr->addChain(chain_ptr);
    return MCP_OK;
}

MCP_API mcp_result_t mcp_filter_manager_initialize(
    mcp_filter_manager_t manager) MCP_NOEXCEPT {
    
    auto manager_ptr = g_filter_manager_manager.get(manager);
    if (!manager_ptr) return MCP_ERROR_NOT_FOUND;
    
    bool success = manager_ptr->initialize();
    return success ? MCP_OK : MCP_ERROR_INVALID_STATE;
}

MCP_API void mcp_filter_manager_release(
    mcp_filter_manager_t manager) MCP_NOEXCEPT {
    
    g_filter_manager_manager.release(manager);
}

// Zero-Copy Buffer Operations

MCP_API mcp_result_t mcp_filter_get_buffer_slices(
    mcp_buffer_handle_t buffer,
    mcp_buffer_slice_t* slices,
    size_t* slice_count) MCP_NOEXCEPT {
    
    if (!slices || !slice_count) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto buffer_ptr = g_buffer_manager.get(buffer);
    if (!buffer_ptr) return MCP_ERROR_NOT_FOUND;
    
    // Get raw slices from buffer
    std::vector<mcp::RawSlice> raw_slices(*slice_count);
    size_t num_slices = buffer_ptr->getRawSlices(raw_slices.data(), *slice_count);
    
    // Convert to C API slices
    for (size_t i = 0; i < num_slices; ++i) {
        slices[i].data = static_cast<const uint8_t*>(raw_slices[i].mem_);
        slices[i].length = raw_slices[i].len_;
        slices[i].flags = MCP_BUFFER_FLAG_READONLY;
    }
    
    *slice_count = num_slices;
    return MCP_OK;
}

MCP_API mcp_result_t mcp_filter_reserve_buffer(
    mcp_buffer_handle_t buffer,
    size_t size,
    mcp_buffer_slice_t* slice) MCP_NOEXCEPT {
    
    if (!slice) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto buffer_ptr = g_buffer_manager.get(buffer);
    if (!buffer_ptr) return MCP_ERROR_NOT_FOUND;
    
    mcp::RawSlice raw_slice;
    void* mem = buffer_ptr->reserveSingleSlice(size, raw_slice);
    if (!mem) return MCP_ERROR_RESOURCE_EXHAUSTED;
    
    slice->data = static_cast<const uint8_t*>(mem);
    slice->length = raw_slice.len_;
    slice->flags = 0;  // Writable
    
    return MCP_OK;
}

MCP_API mcp_result_t mcp_filter_commit_buffer(
    mcp_buffer_handle_t buffer,
    size_t bytes_written) MCP_NOEXCEPT {
    
    auto buffer_ptr = g_buffer_manager.get(buffer);
    if (!buffer_ptr) return MCP_ERROR_NOT_FOUND;
    
    // Commit is handled internally by the buffer
    return MCP_OK;
}

MCP_API mcp_buffer_handle_t mcp_filter_buffer_create(
    const uint8_t* data,
    size_t length,
    uint32_t flags) MCP_NOEXCEPT {
    
    try {
        auto buffer = std::make_shared<mcp::OwnedBuffer>();
        if (data && length > 0) {
            buffer->add(data, length);
        }
        return g_buffer_manager.store(buffer);
    } catch (...) {
        return 0;
    }
}

MCP_API void mcp_filter_buffer_release(
    mcp_buffer_handle_t buffer) MCP_NOEXCEPT {
    
    g_buffer_manager.release(buffer);
}

MCP_API size_t mcp_filter_buffer_length(
    mcp_buffer_handle_t buffer) MCP_NOEXCEPT {
    
    auto buffer_ptr = g_buffer_manager.get(buffer);
    if (!buffer_ptr) return 0;
    
    return buffer_ptr->length();
}

// Client/Server Integration

MCP_API mcp_request_id_t mcp_client_send_filtered(
    mcp_filter_client_context_t* context,
    const uint8_t* data,
    size_t length,
    mcp_filter_completion_cb callback,
    void* user_data) MCP_NOEXCEPT {
    
    if (!context) return 0;
    
    // TODO: Implement filtered client send
    // This would:
    // 1. Create buffer from data
    // 2. Pass through request_filters
    // 3. Send to server
    // 4. Pass response through response_filters
    // 5. Call callback
    
    return 0;  // Placeholder
}

MCP_API mcp_result_t mcp_server_process_filtered(
    mcp_filter_server_context_t* context,
    mcp_request_id_t request_id,
    mcp_buffer_handle_t request_buffer,
    mcp_filter_request_cb callback,
    void* user_data) MCP_NOEXCEPT {
    
    if (!context) return MCP_ERROR_INVALID_ARGUMENT;
    
    // TODO: Implement filtered server processing
    // This would:
    // 1. Pass request through request_filters
    // 2. Process request
    // 3. Pass response through response_filters
    // 4. Call callback with response
    
    return MCP_OK;
}

// Thread-Safe Operations

MCP_API mcp_result_t mcp_filter_post_data(
    mcp_filter_t filter,
    const uint8_t* data,
    size_t length,
    mcp_post_completion_cb callback,
    void* user_data) MCP_NOEXCEPT {
    
    auto filter_ptr = g_filter_manager.get(filter);
    if (!filter_ptr) return MCP_ERROR_NOT_FOUND;
    
    // TODO: Post to dispatcher thread
    // This would use mcp_dispatcher_post internally
    
    return MCP_OK;
}

// Memory Management

MCP_API mcp_filter_resource_guard_t* mcp_filter_guard_create(
    mcp_dispatcher_t dispatcher) MCP_NOEXCEPT {
    
    try {
        return reinterpret_cast<mcp_filter_resource_guard_t*>(new mcp::filter_api::mcp_filter_resource_guard(dispatcher));
    } catch (...) {
        return nullptr;
    }
}

MCP_API mcp_result_t mcp_filter_guard_add_filter(
    mcp_filter_resource_guard_t* guard,
    mcp_filter_t filter) MCP_NOEXCEPT {
    
    if (!guard) return MCP_ERROR_INVALID_ARGUMENT;
    
    reinterpret_cast<mcp::filter_api::mcp_filter_resource_guard*>(guard)->tracked_filters.push_back(filter);
    mcp_filter_retain(filter);
    
    return MCP_OK;
}

MCP_API void mcp_filter_guard_release(
    mcp_filter_resource_guard_t* guard) MCP_NOEXCEPT {
    
    delete reinterpret_cast<mcp::filter_api::mcp_filter_resource_guard*>(guard);
}

// Buffer Pool Management

MCP_API mcp_buffer_pool_t mcp_buffer_pool_create(
    size_t buffer_size,
    size_t max_buffers) MCP_NOEXCEPT {
    
    try {
        return reinterpret_cast<mcp_buffer_pool_t>(new BufferPool(buffer_size, max_buffers));
    } catch (...) {
        return nullptr;
    }
}

MCP_API mcp_buffer_handle_t mcp_buffer_pool_acquire(
    mcp_buffer_pool_t pool) MCP_NOEXCEPT {
    
    if (!pool) return 0;
    
    auto buffer = reinterpret_cast<BufferPool*>(pool)->acquire();
    if (!buffer) return 0;
    
    return g_buffer_manager.store(buffer);
}

MCP_API void mcp_buffer_pool_release(
    mcp_buffer_pool_t pool,
    mcp_buffer_handle_t buffer) MCP_NOEXCEPT {
    
    if (!pool) return;
    
    auto buffer_ptr = g_buffer_manager.get(buffer);
    if (buffer_ptr) {
        reinterpret_cast<BufferPool*>(pool)->release(buffer_ptr);
        g_buffer_manager.release(buffer);
    }
}

MCP_API void mcp_buffer_pool_destroy(
    mcp_buffer_pool_t pool) MCP_NOEXCEPT {
    
    delete reinterpret_cast<BufferPool*>(pool);
}

// Statistics and Monitoring

MCP_API mcp_result_t mcp_filter_get_stats(
    mcp_filter_t filter,
    mcp_filter_stats_t* stats) MCP_NOEXCEPT {
    
    if (!stats) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto filter_ptr = g_filter_manager.get(filter);
    if (!filter_ptr) return MCP_ERROR_NOT_FOUND;
    
    // TODO: Implement statistics collection
    std::memset(stats, 0, sizeof(mcp_filter_stats_t));
    
    return MCP_OK;
}

MCP_API mcp_result_t mcp_filter_reset_stats(
    mcp_filter_t filter) MCP_NOEXCEPT {
    
    auto filter_ptr = g_filter_manager.get(filter);
    if (!filter_ptr) return MCP_ERROR_NOT_FOUND;
    
    // TODO: Reset statistics
    
    return MCP_OK;
}

} // extern "C"