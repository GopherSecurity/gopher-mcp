/**
 * @file circuit_breaker_filter.h
 * @brief Circuit breaker filter for cascading failure protection
 *
 * PRIMARY USE: CLIENT - Protects against repeatedly calling a failing server
 * SECONDARY USE: SERVER - Optional, only if server has downstream dependencies
 *
 * This filter implements the circuit breaker pattern to prevent cascading
 * failures by temporarily blocking requests when error rates exceed thresholds.
 *
 * Client Usage:
 * - Essential for MCP clients to handle server failures gracefully
 * - Prevents hammering a failing server with requests
 * - Automatically recovers when server becomes healthy again
 *
 * Server Usage:
 * - Only needed if server makes outbound calls (e.g., to tools, databases)
 * - Protects server's downstream dependencies
 * - Not needed for pure request/response servers
 */

#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>

#include "../network/filter.h"
#include "../types.h"
#include "filter_event_emitter.h"
#include "json_rpc_protocol_filter.h"

// Temporary debug logging for circuit breaker validation
#define GOPHER_LOG_COMPONENT "filter.circuit_breaker"
#include "mcp/logging/log_macros.h"

namespace mcp {
namespace filter {

/**
 * Circuit breaker states
 */
enum class CircuitState {
  CLOSED = 0,    // Normal operation
  OPEN = 1,      // Circuit is open, blocking requests
  HALF_OPEN = 2  // Testing recovery
};

/**
 * Circuit breaker configuration
 */
struct CircuitBreakerConfig {
  // Failure thresholds
  size_t failure_threshold = 5;       // Consecutive failures to open circuit
  double error_rate_threshold = 0.5;  // Error rate to open circuit (50%)
  size_t min_requests = 10;  // Minimum requests before checking error rate

  // Timing
  std::chrono::milliseconds timeout =
      std::chrono::seconds(30);  // Time before trying half-open
  std::chrono::milliseconds window_size =
      std::chrono::seconds(60);  // Sliding window for metrics

  // Half-open testing
  size_t half_open_max_requests = 3;       // Max requests in half-open state
  size_t half_open_success_threshold = 2;  // Successes needed to close circuit

  // Request types to track
  bool track_timeouts = true;
  bool track_errors = true;
  bool track_4xx_as_errors = false;  // Don't count client errors as failures
};

/**
 * Circuit breaker filter
 *
 * Monitors request/response patterns and opens circuit when:
 * - Consecutive failures exceed threshold
 * - Error rate exceeds threshold
 * - Timeouts become excessive
 *
 * Circuit states:
 * - CLOSED: Normal operation
 * - OPEN: All requests fail fast
 * - HALF_OPEN: Limited requests to test recovery
 */
class CircuitBreakerFilter : public network::NetworkFilterBase,
                             public JsonRpcProtocolFilter::MessageHandler {
 public:
  /**
   * Constructor
   * @param emitter Event emitter for filter events (can be nullptr to disable
   * events)
   * @param config Circuit breaker configuration
   */
  CircuitBreakerFilter(
      std::shared_ptr<FilterEventEmitter> emitter,
      const CircuitBreakerConfig& config = CircuitBreakerConfig())
      : emitter_(emitter),
        config_(config),
        state_(CircuitState::CLOSED),
        consecutive_failures_(0),
        half_open_requests_(0),
        half_open_successes_(0) {
    last_state_change_ = std::chrono::steady_clock::now();
    std::cout << "[CIRCUIT_BREAKER] 🔧 Filter created with "
              << (emitter_ ? "EVENT EMITTER" : "NO EMITTER")
              << " failure_threshold=" << config_.failure_threshold
              << std::endl;
  }

  // Filter interface implementation
  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    // Data passes through - circuit breaker operates at message level
    return network::FilterStatus::Continue;
  }

  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    return network::FilterStatus::Continue;
  }

  network::FilterStatus onNewConnection() override {
    // Could reset per-connection circuit state if needed
    return network::FilterStatus::Continue;
  }

  // JsonRpcProtocolFilter::MessageHandler implementation
  void onRequest(const jsonrpc::Request& request) override {
    std::lock_guard<std::mutex> lock(mutex_);

    // TEMPORARY DEBUG: Verify filter is being called
    std::cout << "[CIRCUIT_BREAKER] onRequest called: method=" << request.method
              << " state=" << stateToString(state_) << std::endl;

    GOPHER_LOG(Debug, "onRequest: method=%s id=%s circuit_state=%s",
               request.method.c_str(), requestIdToString(request.id).c_str(),
               stateToString(state_));

    // Check circuit state
    if (!allowRequest(request.method)) {
      // Circuit is open - fail fast
      std::cout << "[CIRCUIT_BREAKER] ⛔ REQUEST BLOCKED: " << request.method
                << std::endl;
      if (emitter_) {
        std::cout
            << "[CIRCUIT_BREAKER] 📡 Emitting CIRCUIT_REQUEST_BLOCKED event"
            << std::endl;
        json::JsonObjectBuilder event_data;
        event_data.add("method", request.method);
        event_data.add("state", stateToString(state_));
        emitter_->emit(FilterEventType::CIRCUIT_REQUEST_BLOCKED,
                       FilterEventSeverity::WARN, event_data.build());
      }

      // Send error response
      jsonrpc::Response error_response;
      error_response.jsonrpc = "2.0";
      error_response.id = request.id;
      Error error(jsonrpc::INTERNAL_ERROR, "Circuit breaker is open");
      error.data = mcp::make_optional(ErrorData(
          std::map<std::string, std::string>{{"circuit_state", "open"}}));
      error_response.error = mcp::make_optional(error);

      // Would need write callbacks to send this
      // For now, just block the request
      return;
    }

    // Track request start time
    pending_requests_[requestIdToString(request.id)] = {
        request.method, std::chrono::steady_clock::now()};

    // Forward request
    if (next_callbacks_) {
      next_callbacks_->onRequest(request);
    }
  }

  void onResponse(const jsonrpc::Response& response) override {
    std::lock_guard<std::mutex> lock(mutex_);

    // TEMPORARY DEBUG: Verify filter is being called
    bool has_error = response.error.has_value();
    std::cout << "[CIRCUIT_BREAKER] onResponse called: has_error=" << has_error
              << " state=" << stateToString(state_) << std::endl;

    // Find corresponding request
    auto it = pending_requests_.find(requestIdToString(response.id));
    if (it != pending_requests_.end()) {
      auto& req_info = it->second;
      auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - req_info.start_time)
                         .count();

      // Record result
      bool is_error = response.error.has_value();
      bool is_client_error = false;

      GOPHER_LOG(Debug, "onResponse: id=%s has_error=%d latency=%lums",
                 requestIdToString(response.id).c_str(), is_error,
                 static_cast<unsigned long>(latency));

      if (is_error && response.error.has_value()) {
        int code = response.error->code;
        // JSON-RPC error codes: -32700 to -32000 are protocol errors
        // Application errors are typically positive
        is_client_error =
            (code >= -32099 && code <= -32000) || (code >= 400 && code < 500);
      }

      if (is_error && (config_.track_errors && !is_client_error)) {
        GOPHER_LOG(
            Info,
            "onResponse: Recording FAILURE for id=%s method=%s error_code=%d",
            requestIdToString(response.id).c_str(), req_info.method.c_str(),
            response.error->code);
        recordFailure(req_info.method);
      } else {
        GOPHER_LOG(
            Debug,
            "onResponse: Recording SUCCESS for id=%s method=%s latency=%lums",
            requestIdToString(response.id).c_str(), req_info.method.c_str(),
            static_cast<unsigned long>(latency));
        recordSuccess(req_info.method, latency);
      }

      pending_requests_.erase(it);
    }

    // Forward response
    if (next_callbacks_) {
      next_callbacks_->onResponse(response);
    }
  }

  void onNotification(const jsonrpc::Notification& notification) override {
    // Notifications don't affect circuit breaker
    if (next_callbacks_) {
      next_callbacks_->onNotification(notification);
    }
  }

  void onProtocolError(const Error& error) override {
    std::lock_guard<std::mutex> lock(mutex_);

    if (config_.track_errors) {
      recordFailure("protocol_error");
    }

    if (next_callbacks_) {
      next_callbacks_->onProtocolError(error);
    }
  }

  /**
   * Set the next callbacks in the chain
   */
  void setNextCallbacks(JsonRpcProtocolFilter::MessageHandler* callbacks) {
    next_callbacks_ = callbacks;
  }

  /**
   * Get current circuit state
   */
  CircuitState getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
  }

  /**
   * Get circuit health metrics
   */
  void getHealthMetrics(double& success_rate, uint64_t& avg_latency_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    getHealthMetricsNoLock(success_rate, avg_latency_ms);
  }

 private:
  struct RequestInfo {
    std::string method;
    std::chrono::steady_clock::time_point start_time;
  };

  struct RequestOutcome {
    std::chrono::steady_clock::time_point timestamp;
    bool success;
    uint64_t latency_ms;
  };

  bool allowRequest(const std::string& method) {
    auto now = std::chrono::steady_clock::now();

    switch (state_) {
      case CircuitState::CLOSED:
        GOPHER_LOG(Debug,
                   "allowRequest: CLOSED - allowing request for method=%s",
                   method.c_str());
        return true;

      case CircuitState::OPEN:
        // Check if timeout has passed
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_state_change_) >= config_.timeout) {
          // Transition to half-open
          GOPHER_LOG(
              Info,
              "allowRequest: Transitioning OPEN -> HALF_OPEN after timeout");
          transitionState(CircuitState::HALF_OPEN,
                          "Timeout expired, testing recovery");
          half_open_requests_ = 0;
          half_open_successes_ = 0;
          return true;
        }
        GOPHER_LOG(Warning,
                   "allowRequest: OPEN - blocking request for method=%s",
                   method.c_str());
        return false;

      case CircuitState::HALF_OPEN:
        // Allow limited requests for testing
        if (half_open_requests_ < config_.half_open_max_requests) {
          half_open_requests_++;
          GOPHER_LOG(Debug,
                     "allowRequest: HALF_OPEN - allowing test request %zu/%zu "
                     "for method=%s",
                     static_cast<size_t>(half_open_requests_),
                     config_.half_open_max_requests, method.c_str());
          return true;
        }
        GOPHER_LOG(Warning,
                   "allowRequest: HALF_OPEN - max requests reached, blocking "
                   "method=%s",
                   method.c_str());
        return false;
    }

    return false;
  }

  void recordSuccess(const std::string& method, uint64_t latency_ms) {
    auto now = std::chrono::steady_clock::now();

    // Clean old entries
    cleanOldMetrics(now);

    // Record success
    request_outcomes_.push_back({now, true, latency_ms});
    consecutive_failures_ = 0;

    GOPHER_LOG(
        Debug,
        "recordSuccess: method=%s state=%s consecutive_failures reset to 0",
        method.c_str(), stateToString(state_));

    // Handle half-open state
    if (state_ == CircuitState::HALF_OPEN) {
      half_open_successes_++;
      GOPHER_LOG(Info, "recordSuccess: HALF_OPEN successes=%zu/%zu",
                 static_cast<size_t>(half_open_successes_),
                 config_.half_open_success_threshold);
      if (half_open_successes_ >= config_.half_open_success_threshold) {
        transitionState(CircuitState::CLOSED, "Recovery successful");
      }
    }

    // Update health metrics
    updateHealthMetrics();
  }

  void recordFailure(const std::string& method) {
    auto now = std::chrono::steady_clock::now();

    // Clean old entries
    cleanOldMetrics(now);

    // Record failure
    request_outcomes_.push_back({now, false, 0});
    consecutive_failures_++;

    GOPHER_LOG(Warning,
               "recordFailure: method=%s consecutive_failures=%zu/%zu state=%s",
               method.c_str(), static_cast<size_t>(consecutive_failures_),
               config_.failure_threshold, stateToString(state_));

    // Check failure conditions
    if (state_ == CircuitState::CLOSED) {
      // Check consecutive failures
      if (consecutive_failures_ >= config_.failure_threshold) {
        GOPHER_LOG(Error,
                   "recordFailure: CONSECUTIVE threshold exceeded (%zu >= %zu) "
                   "- OPENING circuit",
                   static_cast<size_t>(consecutive_failures_),
                   config_.failure_threshold);
        transitionState(CircuitState::OPEN,
                        "Consecutive failures exceeded threshold: " +
                            std::to_string(consecutive_failures_));
        return;
      }

      // Check error rate
      if (request_outcomes_.size() >= config_.min_requests) {
        double error_rate = calculateErrorRate();
        if (error_rate > config_.error_rate_threshold) {
          GOPHER_LOG(Error,
                     "recordFailure: ERROR RATE threshold exceeded (%.2f%% > "
                     "%.2f%%) - OPENING circuit",
                     error_rate * 100, config_.error_rate_threshold * 100);
          transitionState(CircuitState::OPEN,
                          "Error rate exceeded threshold: " +
                              std::to_string(error_rate * 100) + "%");
        }
      }
    } else if (state_ == CircuitState::HALF_OPEN) {
      // Any failure in half-open returns to open
      GOPHER_LOG(
          Error,
          "recordFailure: Failure during HALF_OPEN test - reopening circuit");
      transitionState(CircuitState::OPEN, "Failure during recovery test");
    }

    updateHealthMetrics();
  }

  void transitionState(CircuitState new_state, const std::string& reason) {
    CircuitState old_state = state_;
    state_ = new_state;
    last_state_change_ = std::chrono::steady_clock::now();

    GOPHER_LOG(Info, "Circuit state transition: %s -> %s (reason: %s)",
               stateToString(old_state), stateToString(new_state),
               reason.c_str());

    std::cout << "[CIRCUIT_BREAKER] ⚡ STATE CHANGE: "
              << stateToString(old_state) << " → " << stateToString(new_state)
              << " (reason: " << reason << ")" << std::endl;

    if (emitter_) {
      std::cout << "[CIRCUIT_BREAKER] 📡 Emitting CIRCUIT_STATE_CHANGE event"
                << std::endl;
      json::JsonObjectBuilder event_data;
      event_data.add("old_state", stateToString(old_state));
      event_data.add("new_state", stateToString(new_state));
      event_data.add("reason", reason);
      emitter_->emit(FilterEventType::CIRCUIT_STATE_CHANGE,
                     FilterEventSeverity::INFO, event_data.build());
    }
  }

  void cleanOldMetrics(std::chrono::steady_clock::time_point now) {
    // Remove entries outside the sliding window
    while (!request_outcomes_.empty()) {
      auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
          now - request_outcomes_.front().timestamp);
      if (age > config_.window_size) {
        request_outcomes_.pop_front();
      } else {
        break;
      }
    }
  }

  double calculateErrorRate() const {
    if (request_outcomes_.empty()) {
      return 0.0;
    }

    size_t failures = 0;
    for (const auto& outcome : request_outcomes_) {
      if (!outcome.success) {
        failures++;
      }
    }

    return static_cast<double>(failures) / request_outcomes_.size();
  }

  void updateHealthMetrics() {
    double success_rate;
    uint64_t avg_latency;
    // Use non-locking version since we're already holding the mutex
    getHealthMetricsNoLock(success_rate, avg_latency);
    if (emitter_) {
      json::JsonObjectBuilder event_data;
      event_data.add("success_rate", success_rate);
      event_data.add("avg_latency_ms", static_cast<double>(avg_latency));
      event_data.add("state", stateToString(state_));
      event_data.add("total_requests",
                     static_cast<double>(request_outcomes_.size()));
      emitter_->emit(FilterEventType::CIRCUIT_HEALTH_UPDATE,
                     FilterEventSeverity::DEBUG, event_data.build());
    }
  }

  // Internal version of getHealthMetrics that doesn't acquire the lock
  // Must be called with mutex_ already held
  void getHealthMetricsNoLock(double& success_rate,
                              uint64_t& avg_latency_ms) const {
    if (request_outcomes_.empty()) {
      success_rate = 1.0;
      avg_latency_ms = 0;
      return;
    }

    size_t successes = 0;
    uint64_t total_latency = 0;
    size_t latency_count = 0;

    for (const auto& outcome : request_outcomes_) {
      if (outcome.success) {
        successes++;
        if (outcome.latency_ms > 0) {
          total_latency += outcome.latency_ms;
          latency_count++;
        }
      }
    }

    success_rate = static_cast<double>(successes) / request_outcomes_.size();
    avg_latency_ms = latency_count > 0 ? total_latency / latency_count : 0;
  }

  // Helper to convert circuit state to string for logging
  static const char* stateToString(CircuitState state) {
    switch (state) {
      case CircuitState::CLOSED:
        return "CLOSED";
      case CircuitState::OPEN:
        return "OPEN";
      case CircuitState::HALF_OPEN:
        return "HALF_OPEN";
      default:
        return "UNKNOWN";
    }
  }

  // Helper to convert request ID to string for logging
  static std::string requestIdToString(const json::JsonValue& id) {
    // Just use toString() which works for all types
    return id.toString();
  }

  std::shared_ptr<FilterEventEmitter> emitter_;
  CircuitBreakerConfig config_;
  JsonRpcProtocolFilter::MessageHandler* next_callbacks_ = nullptr;

  // Circuit state
  mutable std::mutex mutex_;
  CircuitState state_;
  std::chrono::steady_clock::time_point last_state_change_;

  // Metrics
  std::deque<RequestOutcome> request_outcomes_;
  std::atomic<size_t> consecutive_failures_;

  // Half-open state tracking
  std::atomic<size_t> half_open_requests_;
  std::atomic<size_t> half_open_successes_;

  // Request tracking (using string key to avoid variant comparison issues)
  std::map<std::string, RequestInfo> pending_requests_;

  // Helper to convert RequestId to string key
  std::string requestIdToString(const RequestId& id) const {
    return visit(make_overload([](const std::string& s) { return s; },
                               [](int i) { return std::to_string(i); }),
                 id);
  }
};

}  // namespace filter
}  // namespace mcp
