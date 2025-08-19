/**
 * @file backpressure_filter.h
 * @brief Backpressure filter for flow control in MCP connections
 *
 * EQUAL USE: BOTH CLIENT AND SERVER - Essential for memory management
 *
 * This filter implements flow control to prevent overwhelming the system
 * with too much data, especially important for streaming protocols.
 *
 * Server Usage:
 * - Prevents memory exhaustion from slow clients
 * - Essential when handling large requests or multiple clients
 * - Critical for SSE (Server-Sent Events) streaming
 * - Pauses reading when buffers exceed limits
 *
 * Client Usage:
 * - Prevents memory exhaustion from large server responses
 * - Important when processing is slower than network receiving
 * - Essential for resource-constrained clients
 * - Controls flow when consuming streaming data
 *
 * Both Benefit From:
 * - High/low watermark buffer management
 * - Automatic pause/resume based on buffer levels
 * - Protection against unbounded memory growth
 */

#pragma once

#include <atomic>
#include <chrono>

#include "mcp/buffer.h"
#include "mcp/network/filter.h"

namespace mcp {
namespace filter {

/**
 * Configuration for backpressure filter
 */
struct BackpressureConfig {
  // Buffer watermarks
  size_t high_watermark = 1024 * 1024;  // 1MB default
  size_t low_watermark = 256 * 1024;    // 256KB default

  // Rate limiting
  size_t max_bytes_per_second = 0;  // 0 = unlimited

  // Pause/resume thresholds
  std::chrono::milliseconds pause_duration{100};
};

/**
 * Backpressure filter for flow control
 *
 * This filter monitors data flow and applies backpressure when:
 * - Buffer size exceeds high watermark
 * - Data rate exceeds configured limit
 * - Connection is overwhelmed
 */
class BackpressureFilter : public network::NetworkFilterBase {
 public:
  /**
   * Callbacks for backpressure events
   */
  class Callbacks {
   public:
    virtual ~Callbacks() = default;

    /**
     * Called when backpressure is applied
     */
    virtual void onBackpressureApplied() = 0;

    /**
     * Called when backpressure is released
     */
    virtual void onBackpressureReleased() = 0;

    /**
     * Called when data is dropped due to overflow
     * @param bytes Number of bytes dropped
     */
    virtual void onDataDropped(size_t bytes) = 0;
  };

  /**
   * Constructor
   * @param callbacks Backpressure event callbacks
   * @param config Backpressure configuration
   */
  BackpressureFilter(Callbacks& callbacks,
                     const BackpressureConfig& config = BackpressureConfig())
      : callbacks_(callbacks),
        config_(config),
        current_buffer_size_(0),
        is_paused_(false),
        bytes_this_second_(0),
        last_rate_check_(std::chrono::steady_clock::now()) {}

  // Filter interface implementation
  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    size_t data_size = data.length();

    // Check rate limiting
    if (config_.max_bytes_per_second > 0) {
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
          now - last_rate_check_);

      if (elapsed.count() >= 1) {
        // Reset counter every second
        bytes_this_second_ = 0;
        last_rate_check_ = now;
      }

      if (bytes_this_second_ + data_size > config_.max_bytes_per_second) {
        // Rate limit exceeded - apply backpressure
        if (!is_paused_) {
          is_paused_ = true;
          callbacks_.onBackpressureApplied();
        }
        return network::FilterStatus::StopIteration;
      }

      bytes_this_second_ += data_size;
    }

    // Check buffer watermarks
    current_buffer_size_ += data_size;

    if (current_buffer_size_ > config_.high_watermark) {
      // High watermark exceeded - pause reading
      if (!is_paused_) {
        is_paused_ = true;
        callbacks_.onBackpressureApplied();
      }

      // Optionally drop data if buffer is too full
      if (current_buffer_size_ > config_.high_watermark * 2) {
        callbacks_.onDataDropped(data_size);
        data.drain(data_size);
        return network::FilterStatus::StopIteration;
      }

      return network::FilterStatus::StopIteration;
    } else if (is_paused_ && current_buffer_size_ < config_.low_watermark) {
      // Below low watermark - resume reading
      is_paused_ = false;
      callbacks_.onBackpressureReleased();
    }

    return network::FilterStatus::Continue;
  }

  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    // Update buffer size on write
    size_t data_size = data.length();
    if (current_buffer_size_ >= data_size) {
      current_buffer_size_ -= data_size;
    } else {
      current_buffer_size_ = 0;
    }

    // Check if we can resume after write
    if (is_paused_ && current_buffer_size_ < config_.low_watermark) {
      is_paused_ = false;
      callbacks_.onBackpressureReleased();
    }

    return network::FilterStatus::Continue;
  }

  network::FilterStatus onNewConnection() override {
    // Reset state for new connection
    current_buffer_size_ = 0;
    is_paused_ = false;
    bytes_this_second_ = 0;
    last_rate_check_ = std::chrono::steady_clock::now();
    return network::FilterStatus::Continue;
  }

  // Getters for monitoring
  size_t getCurrentBufferSize() const { return current_buffer_size_; }
  bool isPaused() const { return is_paused_; }
  size_t getBytesThisSecond() const { return bytes_this_second_; }

 private:
  Callbacks& callbacks_;
  BackpressureConfig config_;

  std::atomic<size_t> current_buffer_size_;
  std::atomic<bool> is_paused_;
  std::atomic<size_t> bytes_this_second_;
  std::chrono::steady_clock::time_point last_rate_check_;
};

}  // namespace filter
}  // namespace mcp