/**
 * @file rate_limit_filter.h
 * @brief Rate limiting filter for MCP connections
 * 
 * PRIMARY USE: SERVER - Essential for protecting against abuse and DOS attacks
 * SECONDARY USE: CLIENT - Optional for self-throttling and being a good citizen
 * 
 * This filter implements various rate limiting strategies to prevent
 * abuse and ensure fair resource usage.
 * 
 * Server Usage:
 * - Critical for production servers to prevent abuse
 * - Enforces per-client request limits
 * - Protects server resources from being overwhelmed
 * - Implements fair usage across multiple clients
 * 
 * Client Usage:
 * - Optional self-throttling to avoid overwhelming servers
 * - Useful in batch processing or high-volume scenarios
 * - Helps maintain good relationship with API providers
 */

#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include "mcp/network/filter.h"
#include "mcp/buffer.h"

namespace mcp {
namespace filter {

/**
 * Rate limit strategy
 */
enum class RateLimitStrategy {
  TokenBucket,      // Classic token bucket algorithm
  SlidingWindow,    // Sliding window counter
  FixedWindow,      // Fixed window counter
  LeakyBucket      // Leaky bucket algorithm
};

/**
 * Rate limit configuration
 */
struct RateLimitConfig {
  // Strategy to use
  RateLimitStrategy strategy = RateLimitStrategy::TokenBucket;
  
  // Token bucket parameters
  size_t bucket_capacity = 100;        // Maximum tokens
  size_t refill_rate = 10;            // Tokens per second
  
  // Window parameters (for window-based strategies)
  std::chrono::seconds window_size{60};  // Window size
  size_t max_requests_per_window = 100;   // Max requests in window
  
  // Leaky bucket parameters
  size_t leak_rate = 10;               // Requests per second to process
  
  // Burst handling
  bool allow_burst = true;             // Allow burst traffic
  size_t burst_size = 20;              // Extra capacity for bursts
  
  // Per-client limiting (if client ID available)
  bool per_client_limiting = false;
  std::map<std::string, size_t> client_limits;
};

/**
 * Rate limiting filter
 * 
 * Implements multiple rate limiting algorithms:
 * - Token Bucket: Allows burst traffic up to bucket capacity
 * - Sliding Window: Smooth rate limiting over time window
 * - Fixed Window: Simple counter reset at window boundaries
 * - Leaky Bucket: Constant rate processing with queue
 */
class RateLimitFilter : public network::NetworkFilterBase {
public:
  /**
   * Callbacks for rate limit events
   */
  class Callbacks {
  public:
    virtual ~Callbacks() = default;
    
    /**
     * Called when a request is allowed
     */
    virtual void onRequestAllowed() = 0;
    
    /**
     * Called when a request is rate limited
     * @param retry_after Suggested retry time in milliseconds
     */
    virtual void onRequestLimited(std::chrono::milliseconds retry_after) = 0;
    
    /**
     * Called when rate limit is about to be exceeded (warning)
     * @param remaining Remaining capacity percentage (0-100)
     */
    virtual void onRateLimitWarning(int remaining) = 0;
  };
  
  /**
   * Constructor
   * @param callbacks Rate limit event callbacks
   * @param config Rate limit configuration
   */
  RateLimitFilter(Callbacks& callbacks,
                 const RateLimitConfig& config = RateLimitConfig())
      : callbacks_(callbacks),
        config_(config),
        tokens_(config.bucket_capacity),
        last_refill_(std::chrono::steady_clock::now()) {
    
    // Initialize based on strategy
    switch (config_.strategy) {
      case RateLimitStrategy::TokenBucket:
        tokens_ = config_.bucket_capacity;
        break;
      case RateLimitStrategy::SlidingWindow:
      case RateLimitStrategy::FixedWindow:
        window_start_ = std::chrono::steady_clock::now();
        break;
      case RateLimitStrategy::LeakyBucket:
        last_leak_ = std::chrono::steady_clock::now();
        break;
    }
  }
  
  // Filter interface implementation
  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    // Check rate limit before processing data
    if (!allowRequest()) {
      // Calculate retry time
      auto retry_after = calculateRetryAfter();
      callbacks_.onRequestLimited(retry_after);
      
      // Stop processing this data
      return network::FilterStatus::StopIteration;
    }
    
    // Request allowed
    callbacks_.onRequestAllowed();
    
    // Check if we're approaching limit (warning at 80% capacity)
    int remaining = getRemainingCapacityPercent();
    if (remaining < 20) {
      callbacks_.onRateLimitWarning(remaining);
    }
    
    return network::FilterStatus::Continue;
  }
  
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    // Outgoing data typically not rate limited
    return network::FilterStatus::Continue;
  }
  
  network::FilterStatus onNewConnection() override {
    // Could reset per-connection limits here
    return network::FilterStatus::Continue;
  }
  
private:
  bool allowRequest() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    switch (config_.strategy) {
      case RateLimitStrategy::TokenBucket:
        return allowRequestTokenBucket();
      case RateLimitStrategy::SlidingWindow:
        return allowRequestSlidingWindow();
      case RateLimitStrategy::FixedWindow:
        return allowRequestFixedWindow();
      case RateLimitStrategy::LeakyBucket:
        return allowRequestLeakyBucket();
    }
    
    return true;
  }
  
  bool allowRequestTokenBucket() {
    // Refill tokens based on time elapsed
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_refill_);
    
    if (elapsed.count() > 0) {
      size_t tokens_to_add = config_.refill_rate * elapsed.count();
      tokens_ = std::min(tokens_ + tokens_to_add, config_.bucket_capacity);
      last_refill_ = now;
    }
    
    // Check if we have tokens
    if (tokens_ > 0) {
      tokens_--;
      return true;
    }
    
    return false;
  }
  
  bool allowRequestSlidingWindow() {
    auto now = std::chrono::steady_clock::now();
    
    // Remove old entries outside the window
    while (!request_times_.empty()) {
      auto age = std::chrono::duration_cast<std::chrono::seconds>(
          now - request_times_.front());
      if (age > config_.window_size) {
        request_times_.pop_front();
      } else {
        break;
      }
    }
    
    // Check if we can add new request
    if (request_times_.size() < config_.max_requests_per_window) {
      request_times_.push_back(now);
      return true;
    }
    
    return false;
  }
  
  bool allowRequestFixedWindow() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - window_start_);
    
    // Reset window if needed
    if (elapsed >= config_.window_size) {
      window_requests_ = 0;
      window_start_ = now;
    }
    
    // Check if we can add request
    if (window_requests_ < config_.max_requests_per_window) {
      window_requests_++;
      return true;
    }
    
    return false;
  }
  
  bool allowRequestLeakyBucket() {
    auto now = std::chrono::steady_clock::now();
    
    // Process leaked requests
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_leak_);
    size_t leaked = (config_.leak_rate * elapsed.count()) / 1000;
    
    if (leaked > 0) {
      bucket_level_ = (bucket_level_ > leaked) ? bucket_level_ - leaked : 0;
      last_leak_ = now;
    }
    
    // Check if we can add to bucket
    if (bucket_level_ < config_.bucket_capacity) {
      bucket_level_++;
      return true;
    }
    
    return false;
  }
  
  std::chrono::milliseconds calculateRetryAfter() {
    switch (config_.strategy) {
      case RateLimitStrategy::TokenBucket:
        // Time until next token
        return std::chrono::milliseconds(1000 / config_.refill_rate);
        
      case RateLimitStrategy::SlidingWindow:
        // Time until oldest request expires
        if (!request_times_.empty()) {
          auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - request_times_.front());
          auto window_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
              config_.window_size);
          return window_ms - age;
        }
        break;
        
      case RateLimitStrategy::FixedWindow:
        // Time until window reset
        {
          auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - window_start_);
          auto window_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
              config_.window_size);
          return window_ms - elapsed;
        }
        
      case RateLimitStrategy::LeakyBucket:
        // Time until next leak
        return std::chrono::milliseconds(1000 / config_.leak_rate);
    }
    
    return std::chrono::milliseconds(1000);  // Default 1 second
  }
  
  int getRemainingCapacityPercent() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    switch (config_.strategy) {
      case RateLimitStrategy::TokenBucket:
        return (tokens_ * 100) / config_.bucket_capacity;
        
      case RateLimitStrategy::SlidingWindow:
      case RateLimitStrategy::FixedWindow:
        {
          size_t used = (config_.strategy == RateLimitStrategy::SlidingWindow) 
                        ? request_times_.size() : window_requests_;
          size_t remaining = config_.max_requests_per_window - used;
          return (remaining * 100) / config_.max_requests_per_window;
        }
        
      case RateLimitStrategy::LeakyBucket:
        {
          size_t remaining = config_.bucket_capacity - bucket_level_;
          return (remaining * 100) / config_.bucket_capacity;
        }
    }
    
    return 100;
  }
  
  Callbacks& callbacks_;
  RateLimitConfig config_;
  
  // Synchronization
  mutable std::mutex mutex_;
  
  // Token bucket state
  std::atomic<size_t> tokens_;
  std::chrono::steady_clock::time_point last_refill_;
  
  // Sliding window state
  std::deque<std::chrono::steady_clock::time_point> request_times_;
  
  // Fixed window state
  std::chrono::steady_clock::time_point window_start_;
  std::atomic<size_t> window_requests_{0};
  
  // Leaky bucket state
  std::atomic<size_t> bucket_level_{0};
  std::chrono::steady_clock::time_point last_leak_;
};

} // namespace filter
} // namespace mcp