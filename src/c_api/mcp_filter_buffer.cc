/**
 * @file mcp_filter_buffer.cc
 * @brief Implementation of zero-copy buffer interface for MCP Filter API
 */

#include "mcp/c_api/mcp_filter_buffer.h"
#include "mcp/c_api/mcp_filter_api.h"
#include "mcp/c_api/mcp_raii.h"
#include "mcp/buffer.h"
#include "handle_manager.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace mcp {
namespace filter_buffer {

// ============================================================================
// Handle Management (reuse from filter_api)
// ============================================================================

// Import the g_buffer_manager from filter_api namespace
using c_api_internal::HandleManager;

} // namespace filter_buffer

// Declare extern reference to g_buffer_manager defined in mcp_filter_api.cc
namespace filter_api {
    extern c_api_internal::HandleManager<Buffer> g_buffer_manager;
}

namespace filter_buffer {
using mcp::filter_api::g_buffer_manager;

// ============================================================================
// Buffer Fragment Wrapper
// ============================================================================

class ExternalBufferFragment : public BufferFragment {
public:
    ExternalBufferFragment(const mcp_buffer_fragment_t& fragment)
        : fragment_(fragment) {}
    
    const void* data() const override {
        return fragment_.data;
    }
    
    size_t size() const override {
        return fragment_.size;
    }
    
    void done() override {
        if (fragment_.release_callback) {
            fragment_.release_callback(
                const_cast<void*>(fragment_.data),
                fragment_.size,
                fragment_.user_data);
        }
    }
    
private:
    mcp_buffer_fragment_t fragment_;
};

// ============================================================================
// Buffer Reservation Manager
// ============================================================================

class ReservationManager {
public:
    struct Reservation {
        std::shared_ptr<Buffer> buffer;
        RawSlice slice;
        size_t reserved_size;
        bool committed;
    };
    
    uint64_t createReservation(std::shared_ptr<Buffer> buffer, size_t size) {
        RawSlice slice;
        void* mem = buffer->reserveSingleSlice(size, slice);
        if (!mem) return 0;
        
        uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
        
        std::lock_guard<std::mutex> lock(mutex_);
        reservations_[id] = {buffer, slice, size, false};
        return id;
    }
    
    Reservation* getReservation(uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = reservations_.find(id);
        if (it != reservations_.end() && !it->second.committed) {
            return &it->second;
        }
        return nullptr;
    }
    
    void commitReservation(uint64_t id, size_t bytes_written) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = reservations_.find(id);
        if (it != reservations_.end()) {
            it->second.buffer->commit(it->second.slice, bytes_written);
            it->second.committed = true;
            reservations_.erase(it);
        }
    }
    
    void cancelReservation(uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        reservations_.erase(id);
    }
    
private:
    std::mutex mutex_;
    std::atomic<uint64_t> next_id_{1};
    std::unordered_map<uint64_t, Reservation> reservations_;
};

static ReservationManager g_reservation_manager;

// ============================================================================
// Drain Tracker Implementation
// ============================================================================

class DrainTrackerImpl : public DrainTracker {
public:
    DrainTrackerImpl(mcp_drain_tracker_cb callback, void* user_data)
        : callback_(callback), user_data_(user_data) {}
    
    void onDrain(size_t bytes_drained) override {
        if (callback_) {
            callback_(bytes_drained, user_data_);
        }
    }
    
private:
    mcp_drain_tracker_cb callback_;
    void* user_data_;
};

// ============================================================================
// Advanced Buffer Pool
// ============================================================================

struct BufferPoolImpl {
    BufferPoolImpl(const mcp_buffer_pool_config_t& config)
        : buffer_size_(config.buffer_size),
          max_buffers_(config.max_buffers),
          thread_local_(config.use_thread_local),
          zero_on_alloc_(config.zero_on_alloc) {
        
        // Preallocate buffers
        for (size_t i = 0; i < config.prealloc_count && i < max_buffers_; ++i) {
            auto buffer = std::make_shared<OwnedBuffer>();
            // Pre-allocate space
            mcp::RawSlice slice;
            buffer->reserveSingleSlice(buffer_size_, slice);
            free_buffers_.push_back(buffer);
        }
        total_allocated_ = config.prealloc_count * buffer_size_;
    }
    
    std::shared_ptr<Buffer> acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!free_buffers_.empty()) {
            auto buffer = free_buffers_.back();
            free_buffers_.pop_back();
            used_count_++;
            
            if (zero_on_alloc_) {
                // Clear buffer content
                buffer->drain(buffer->length());
            }
            return buffer;
        }
        
        // Allocate new buffer if under limit
        if (used_count_ < max_buffers_) {
            auto buffer = std::make_shared<OwnedBuffer>();
            // Pre-allocate space
            mcp::RawSlice slice;
            buffer->reserveSingleSlice(buffer_size_, slice);
            used_count_++;
            total_allocated_ += buffer_size_;
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
            used_count_--;
        }
    }
    
    void getStats(size_t* free_count, size_t* used, size_t* total_alloc) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (free_count) *free_count = free_buffers_.size();
        if (used) *used = used_count_;
        if (total_alloc) *total_alloc = total_allocated_;
    }
    
    void trim(size_t target_free) {
        std::lock_guard<std::mutex> lock(mutex_);
        while (free_buffers_.size() > target_free) {
            free_buffers_.pop_back();
            total_allocated_ -= buffer_size_;
        }
    }
    
private:
    size_t buffer_size_;
    size_t max_buffers_;
    bool thread_local_;
    bool zero_on_alloc_;
    
    std::mutex mutex_;
    std::vector<std::shared_ptr<Buffer>> free_buffers_;
    size_t used_count_{0};
    size_t total_allocated_{0};
};

} // namespace filter_buffer
} // namespace mcp

// ============================================================================
// C API Implementation
// ============================================================================

using namespace mcp::filter_buffer;

extern "C" {

// Buffer Creation and Management

MCP_API mcp_buffer_handle_t mcp_buffer_create_owned(
    size_t initial_capacity,
    mcp_buffer_ownership_t ownership) MCP_NOEXCEPT {
    
    try {
        auto buffer = std::make_shared<mcp::OwnedBuffer>();
        if (initial_capacity > 0) {
            // Pre-allocate space
            mcp::RawSlice slice;
            buffer->reserveSingleSlice(initial_capacity, slice);
        }
        return g_buffer_manager.store(buffer);
    } catch (...) {
        return 0;
    }
}

MCP_API mcp_buffer_handle_t mcp_buffer_create_view(
    const void* data,
    size_t length) MCP_NOEXCEPT {
    
    if (!data || length == 0) return 0;
    
    try {
        auto buffer = std::make_shared<mcp::OwnedBuffer>();
        // Create a view without copying
        // Note: This is a simplification - real implementation would need
        // a proper view buffer that doesn't copy
        buffer->add(data, length);
        return g_buffer_manager.store(buffer);
    } catch (...) {
        return 0;
    }
}

MCP_API mcp_buffer_handle_t mcp_buffer_create_from_fragment(
    const mcp_buffer_fragment_t* fragment) MCP_NOEXCEPT {
    
    if (!fragment || !fragment->data) return 0;
    
    try {
        auto buffer = std::make_shared<mcp::OwnedBuffer>();
        auto frag = std::make_unique<ExternalBufferFragment>(*fragment);
        buffer->addBufferFragment(std::move(frag));
        return g_buffer_manager.store(buffer);
    } catch (...) {
        return 0;
    }
}

MCP_API mcp_buffer_handle_t mcp_buffer_clone(
    mcp_buffer_handle_t buffer) MCP_NOEXCEPT {
    
    auto src = g_buffer_manager.get(buffer);
    if (!src) return 0;
    
    try {
        auto clone = std::make_shared<mcp::OwnedBuffer>();
        clone->add(*src);
        return g_buffer_manager.store(clone);
    } catch (...) {
        return 0;
    }
}

MCP_API mcp_buffer_handle_t mcp_buffer_create_cow(
    mcp_buffer_handle_t buffer) MCP_NOEXCEPT {
    
    // For now, just clone - real COW would track modifications
    return mcp_buffer_clone(buffer);
}

// Buffer Data Operations

MCP_API mcp_result_t mcp_buffer_add(
    mcp_buffer_handle_t buffer,
    const void* data,
    size_t length) MCP_NOEXCEPT {
    
    if (!data || length == 0) return MCP_OK;
    
    auto buf = g_buffer_manager.get(buffer);
    if (!buf) return MCP_ERROR_NOT_FOUND;
    
    try {
        buf->add(data, length);
        return MCP_OK;
    } catch (...) {
        return MCP_ERROR_OUT_OF_MEMORY;
    }
}

MCP_API mcp_result_t mcp_buffer_add_string(
    mcp_buffer_handle_t buffer,
    const char* str) MCP_NOEXCEPT {
    
    if (!str) return MCP_ERROR_INVALID_ARGUMENT;
    
    return mcp_buffer_add(buffer, str, strlen(str));
}

MCP_API mcp_result_t mcp_buffer_add_buffer(
    mcp_buffer_handle_t buffer,
    mcp_buffer_handle_t source) MCP_NOEXCEPT {
    
    auto dest = g_buffer_manager.get(buffer);
    auto src = g_buffer_manager.get(source);
    if (!dest || !src) return MCP_ERROR_NOT_FOUND;
    
    try {
        dest->add(*src);
        return MCP_OK;
    } catch (...) {
        return MCP_ERROR_OUT_OF_MEMORY;
    }
}

MCP_API mcp_result_t mcp_buffer_add_fragment(
    mcp_buffer_handle_t buffer,
    const mcp_buffer_fragment_t* fragment) MCP_NOEXCEPT {
    
    if (!fragment || !fragment->data) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto buf = g_buffer_manager.get(buffer);
    if (!buf) return MCP_ERROR_NOT_FOUND;
    
    try {
        auto frag = std::make_unique<ExternalBufferFragment>(*fragment);
        buf->addBufferFragment(std::move(frag));
        return MCP_OK;
    } catch (...) {
        return MCP_ERROR_OUT_OF_MEMORY;
    }
}

MCP_API mcp_result_t mcp_buffer_prepend(
    mcp_buffer_handle_t buffer,
    const void* data,
    size_t length) MCP_NOEXCEPT {
    
    if (!data || length == 0) return MCP_OK;
    
    auto buf = g_buffer_manager.get(buffer);
    if (!buf) return MCP_ERROR_NOT_FOUND;
    
    try {
        buf->prepend(data, length);
        return MCP_OK;
    } catch (...) {
        return MCP_ERROR_OUT_OF_MEMORY;
    }
}

// Buffer Consumption

MCP_API mcp_result_t mcp_buffer_drain(
    mcp_buffer_handle_t buffer,
    size_t size) MCP_NOEXCEPT {
    
    auto buf = g_buffer_manager.get(buffer);
    if (!buf) return MCP_ERROR_NOT_FOUND;
    
    if (size > buf->length()) {
        return MCP_ERROR_INVALID_ARGUMENT;
    }
    
    buf->drain(size);
    return MCP_OK;
}

MCP_API mcp_result_t mcp_buffer_move(
    mcp_buffer_handle_t source,
    mcp_buffer_handle_t destination,
    size_t length) MCP_NOEXCEPT {
    
    auto src = g_buffer_manager.get(source);
    auto dest = g_buffer_manager.get(destination);
    if (!src || !dest) return MCP_ERROR_NOT_FOUND;
    
    try {
        if (length == 0) {
            src->move(*dest);
        } else {
            src->move(*dest, length);
        }
        return MCP_OK;
    } catch (...) {
        return MCP_ERROR_OUT_OF_MEMORY;
    }
}

MCP_API mcp_result_t mcp_buffer_set_drain_tracker(
    mcp_buffer_handle_t buffer,
    const mcp_drain_tracker_t* tracker) MCP_NOEXCEPT {
    
    if (!tracker) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto buf = g_buffer_manager.get(buffer);
    if (!buf) return MCP_ERROR_NOT_FOUND;
    
    try {
        auto tracker_impl = std::make_shared<DrainTrackerImpl>(
            tracker->callback, tracker->user_data);
        buf->attachDrainTracker(tracker_impl);
        return MCP_OK;
    } catch (...) {
        return MCP_ERROR_OUT_OF_MEMORY;
    }
}

// Buffer Reservation

MCP_API mcp_result_t mcp_buffer_reserve(
    mcp_buffer_handle_t buffer,
    size_t min_size,
    mcp_buffer_reservation_t* reservation) MCP_NOEXCEPT {
    
    if (!reservation) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto buf = g_buffer_manager.get(buffer);
    if (!buf) return MCP_ERROR_NOT_FOUND;
    
    uint64_t res_id = g_reservation_manager.createReservation(buf, min_size);
    if (res_id == 0) return MCP_ERROR_RESOURCE_EXHAUSTED;
    
    auto res = g_reservation_manager.getReservation(res_id);
    if (!res) return MCP_ERROR_INVALID_STATE;
    
    reservation->data = res->slice.mem_;
    reservation->capacity = res->slice.len_;
    reservation->buffer = buffer;
    reservation->reservation_id = res_id;
    
    return MCP_OK;
}

MCP_API mcp_result_t mcp_buffer_reserve_iovec(
    mcp_buffer_handle_t buffer,
    void* iovecs,
    size_t iovec_count,
    size_t* reserved) MCP_NOEXCEPT {
    
    // TODO: Implement vectored I/O reservation
    return MCP_ERROR_NOT_IMPLEMENTED;
}

MCP_API mcp_result_t mcp_buffer_commit_reservation(
    mcp_buffer_reservation_t* reservation,
    size_t bytes_written) MCP_NOEXCEPT {
    
    if (!reservation) return MCP_ERROR_INVALID_ARGUMENT;
    
    if (bytes_written > reservation->capacity) {
        return MCP_ERROR_INVALID_ARGUMENT;
    }
    
    g_reservation_manager.commitReservation(
        reservation->reservation_id, bytes_written);
    
    reservation->reservation_id = 0;
    return MCP_OK;
}

MCP_API mcp_result_t mcp_buffer_cancel_reservation(
    mcp_buffer_reservation_t* reservation) MCP_NOEXCEPT {
    
    if (!reservation) return MCP_ERROR_INVALID_ARGUMENT;
    
    g_reservation_manager.cancelReservation(reservation->reservation_id);
    reservation->reservation_id = 0;
    
    return MCP_OK;
}

// Buffer Access

MCP_API mcp_result_t mcp_buffer_get_contiguous(
    mcp_buffer_handle_t buffer,
    size_t offset,
    size_t length,
    const void** data,
    size_t* actual_length) MCP_NOEXCEPT {
    
    if (!data || !actual_length) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto buf = g_buffer_manager.get(buffer);
    if (!buf) return MCP_ERROR_NOT_FOUND;
    
    if (offset >= buf->length()) {
        *data = nullptr;
        *actual_length = 0;
        return MCP_OK;
    }
    
    // Get the first slice
    auto slice = buf->frontSlice();
    if (offset < slice.len_) {
        *data = static_cast<const uint8_t*>(slice.mem_) + offset;
        *actual_length = std::min(length, slice.len_ - offset);
    } else {
        *data = nullptr;
        *actual_length = 0;
    }
    
    return MCP_OK;
}

MCP_API mcp_result_t mcp_buffer_linearize(
    mcp_buffer_handle_t buffer,
    size_t size,
    void** data) MCP_NOEXCEPT {
    
    if (!data) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto buf = g_buffer_manager.get(buffer);
    if (!buf) return MCP_ERROR_NOT_FOUND;
    
    if (size > buf->length()) {
        return MCP_ERROR_INVALID_ARGUMENT;
    }
    
    *data = buf->linearize(size);
    return (*data) ? MCP_OK : MCP_ERROR_OUT_OF_MEMORY;
}

MCP_API mcp_result_t mcp_buffer_peek(
    mcp_buffer_handle_t buffer,
    size_t offset,
    void* data,
    size_t length) MCP_NOEXCEPT {
    
    if (!data || length == 0) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto buf = g_buffer_manager.get(buffer);
    if (!buf) return MCP_ERROR_NOT_FOUND;
    
    if (offset + length > buf->length()) {
        return MCP_ERROR_INVALID_ARGUMENT;
    }
    
    // Copy data without consuming
    buf->copyOut(offset, length, data);
    return MCP_OK;
}

// Type-Safe I/O

MCP_API mcp_result_t mcp_buffer_write_le_int(
    mcp_buffer_handle_t buffer,
    uint64_t value,
    size_t size) MCP_NOEXCEPT {
    
    auto buf = g_buffer_manager.get(buffer);
    if (!buf) return MCP_ERROR_NOT_FOUND;
    
    try {
        switch (size) {
            case 1: buf->writeLEInt<uint8_t>(value); break;
            case 2: buf->writeLEInt<uint16_t>(value); break;
            case 4: buf->writeLEInt<uint32_t>(value); break;
            case 8: buf->writeLEInt<uint64_t>(value); break;
            default: return MCP_ERROR_INVALID_ARGUMENT;
        }
        return MCP_OK;
    } catch (...) {
        return MCP_ERROR_OUT_OF_MEMORY;
    }
}

MCP_API mcp_result_t mcp_buffer_write_be_int(
    mcp_buffer_handle_t buffer,
    uint64_t value,
    size_t size) MCP_NOEXCEPT {
    
    auto buf = g_buffer_manager.get(buffer);
    if (!buf) return MCP_ERROR_NOT_FOUND;
    
    try {
        switch (size) {
            case 1: buf->writeBEInt<uint8_t>(value); break;
            case 2: buf->writeBEInt<uint16_t>(value); break;
            case 4: buf->writeBEInt<uint32_t>(value); break;
            case 8: buf->writeBEInt<uint64_t>(value); break;
            default: return MCP_ERROR_INVALID_ARGUMENT;
        }
        return MCP_OK;
    } catch (...) {
        return MCP_ERROR_OUT_OF_MEMORY;
    }
}

MCP_API mcp_result_t mcp_buffer_read_le_int(
    mcp_buffer_handle_t buffer,
    size_t size,
    uint64_t* value) MCP_NOEXCEPT {
    
    if (!value) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto buf = g_buffer_manager.get(buffer);
    if (!buf) return MCP_ERROR_NOT_FOUND;
    
    if (size > buf->length()) return MCP_ERROR_INVALID_ARGUMENT;
    
    try {
        switch (size) {
            case 1: *value = buf->readLEInt<uint8_t>(); break;
            case 2: *value = buf->readLEInt<uint16_t>(); break;
            case 4: *value = buf->readLEInt<uint32_t>(); break;
            case 8: *value = buf->readLEInt<uint64_t>(); break;
            default: return MCP_ERROR_INVALID_ARGUMENT;
        }
        return MCP_OK;
    } catch (...) {
        return MCP_ERROR_INVALID_STATE;
    }
}

MCP_API mcp_result_t mcp_buffer_read_be_int(
    mcp_buffer_handle_t buffer,
    size_t size,
    uint64_t* value) MCP_NOEXCEPT {
    
    if (!value) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto buf = g_buffer_manager.get(buffer);
    if (!buf) return MCP_ERROR_NOT_FOUND;
    
    if (size > buf->length()) return MCP_ERROR_INVALID_ARGUMENT;
    
    try {
        switch (size) {
            case 1: *value = buf->readBEInt<uint8_t>(); break;
            case 2: *value = buf->readBEInt<uint16_t>(); break;
            case 4: *value = buf->readBEInt<uint32_t>(); break;
            case 8: *value = buf->readBEInt<uint64_t>(); break;
            default: return MCP_ERROR_INVALID_ARGUMENT;
        }
        return MCP_OK;
    } catch (...) {
        return MCP_ERROR_INVALID_STATE;
    }
}

// Buffer Search

MCP_API mcp_result_t mcp_buffer_search(
    mcp_buffer_handle_t buffer,
    const void* pattern,
    size_t pattern_size,
    size_t start_position,
    size_t* position) MCP_NOEXCEPT {
    
    if (!pattern || !position) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto buf = g_buffer_manager.get(buffer);
    if (!buf) return MCP_ERROR_NOT_FOUND;
    
    auto result = buf->search(pattern, pattern_size, start_position);
    if (result.found_) {
        *position = result.position_;
        return MCP_OK;
    }
    
    return MCP_ERROR_NOT_FOUND;
}

MCP_API mcp_result_t mcp_buffer_find_byte(
    mcp_buffer_handle_t buffer,
    uint8_t delimiter,
    size_t* position) MCP_NOEXCEPT {
    
    return mcp_buffer_search(buffer, &delimiter, 1, 0, position);
}

// Buffer Information

MCP_API size_t mcp_buffer_length(
    mcp_buffer_handle_t buffer) MCP_NOEXCEPT {
    
    auto buf = g_buffer_manager.get(buffer);
    return buf ? buf->length() : 0;
}

MCP_API size_t mcp_buffer_capacity(
    mcp_buffer_handle_t buffer) MCP_NOEXCEPT {
    
    auto buf = g_buffer_manager.get(buffer);
    // Buffer doesn't expose capacity directly, return length
    return buf ? buf->length() : 0;
}

MCP_API mcp_bool_t mcp_buffer_is_empty(
    mcp_buffer_handle_t buffer) MCP_NOEXCEPT {
    
    return mcp_buffer_length(buffer) == 0 ? MCP_TRUE : MCP_FALSE;
}

MCP_API mcp_result_t mcp_buffer_get_stats(
    mcp_buffer_handle_t buffer,
    mcp_buffer_stats_t* stats) MCP_NOEXCEPT {
    
    if (!stats) return MCP_ERROR_INVALID_ARGUMENT;
    
    auto buf = g_buffer_manager.get(buffer);
    if (!buf) return MCP_ERROR_NOT_FOUND;
    
    stats->total_bytes = buf->length();
    stats->used_bytes = buf->length();
    
    // Count slices
    mcp::RawSlice slices[32];
    stats->slice_count = buf->getRawSlices(slices, 32);
    
    stats->fragment_count = 0;  // Not tracked
    stats->read_operations = 0;  // Not tracked
    stats->write_operations = 0;  // Not tracked
    
    return MCP_OK;
}

// Buffer Watermarks

MCP_API mcp_result_t mcp_buffer_set_watermarks(
    mcp_buffer_handle_t buffer,
    size_t low_watermark,
    size_t high_watermark,
    size_t overflow_watermark) MCP_NOEXCEPT {
    
    auto buf = g_buffer_manager.get(buffer);
    if (!buf) return MCP_ERROR_NOT_FOUND;
    
    // WatermarkBuffer would handle this - simplified for now
    return MCP_OK;
}

MCP_API mcp_bool_t mcp_buffer_above_high_watermark(
    mcp_buffer_handle_t buffer) MCP_NOEXCEPT {
    
    // Simplified - would check actual watermark
    return MCP_FALSE;
}

MCP_API mcp_bool_t mcp_buffer_below_low_watermark(
    mcp_buffer_handle_t buffer) MCP_NOEXCEPT {
    
    // Simplified - would check actual watermark
    return MCP_TRUE;
}

// Advanced Buffer Pool

MCP_API mcp_buffer_pool_t mcp_buffer_pool_create_ex(
    const mcp_buffer_pool_config_t* config) MCP_NOEXCEPT {
    
    if (!config) return nullptr;
    
    try {
        return reinterpret_cast<mcp_buffer_pool_t>(new BufferPoolImpl(*config));
    } catch (...) {
        return nullptr;
    }
}

MCP_API mcp_result_t mcp_buffer_pool_get_stats(
    mcp_buffer_pool_t pool,
    size_t* free_count,
    size_t* used_count,
    size_t* total_allocated) MCP_NOEXCEPT {
    
    if (!pool) return MCP_ERROR_INVALID_ARGUMENT;
    
    reinterpret_cast<BufferPoolImpl*>(pool)->getStats(free_count, used_count, total_allocated);
    return MCP_OK;
}

MCP_API mcp_result_t mcp_buffer_pool_trim(
    mcp_buffer_pool_t pool,
    size_t target_free) MCP_NOEXCEPT {
    
    if (!pool) return MCP_ERROR_INVALID_ARGUMENT;
    
    reinterpret_cast<BufferPoolImpl*>(pool)->trim(target_free);
    return MCP_OK;
}

} // extern "C"