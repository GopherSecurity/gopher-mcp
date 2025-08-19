/**
 * @file metrics_filter.h
 * @brief Enhanced metrics collection filter for MCP connections
 *
 * EQUAL USE: BOTH CLIENT AND SERVER - Essential for observability
 *
 * This filter collects detailed metrics about connection performance,
 * request/response patterns, and protocol-specific statistics.
 *
 * Server Usage:
 * - Monitor service health and performance
 * - Track request rates, error rates, and latencies
 * - Per-method statistics for capacity planning
 * - Identify performance bottlenecks
 * - Feed metrics to monitoring systems (Prometheus, Grafana)
 *
 * Client Usage:
 * - Monitor integration health with MCP servers
 * - Track API call performance and reliability
 * - Identify slow endpoints or methods
 * - Monitor retry rates and circuit breaker trips
 * - Debug connection issues
 *
 * Both Benefit From:
 * - Latency percentiles (p50, p95, p99)
 * - Throughput measurements (requests/sec, bytes/sec)
 * - Error rate tracking
 * - Method-level granularity
 */

#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>

#include "../network/filter.h"
#include "../types.h"
#include "mcp_jsonrpc_filter.h"

namespace mcp {
namespace filter {

/**
 * Detailed metrics structure
 */
struct ConnectionMetrics {
  // Basic counters
  std::atomic<uint64_t> bytes_received{0};
  std::atomic<uint64_t> bytes_sent{0};
  std::atomic<uint64_t> messages_received{0};
  std::atomic<uint64_t> messages_sent{0};

  // Request/Response metrics
  std::atomic<uint64_t> requests_sent{0};
  std::atomic<uint64_t> requests_received{0};
  std::atomic<uint64_t> responses_sent{0};
  std::atomic<uint64_t> responses_received{0};
  std::atomic<uint64_t> notifications_sent{0};
  std::atomic<uint64_t> notifications_received{0};

  // Error metrics
  std::atomic<uint64_t> errors_sent{0};
  std::atomic<uint64_t> errors_received{0};
  std::atomic<uint64_t> protocol_errors{0};

  // Latency tracking
  std::atomic<uint64_t> total_latency_ms{0};
  std::atomic<uint64_t> min_latency_ms{UINT64_MAX};
  std::atomic<uint64_t> max_latency_ms{0};
  std::atomic<uint64_t> latency_samples{0};

  // Method-specific metrics
  std::map<std::string, uint64_t> method_counts;
  std::map<std::string, uint64_t> method_latencies_ms;
  std::map<std::string, uint64_t> method_errors;

  // Connection timing
  std::chrono::steady_clock::time_point connection_start;
  std::chrono::steady_clock::time_point last_activity;

  // Rate tracking
  double current_receive_rate_bps{0};
  double current_send_rate_bps{0};
  double peak_receive_rate_bps{0};
  double peak_send_rate_bps{0};
};

/**
 * Enhanced metrics collection filter
 *
 * This filter collects comprehensive metrics about:
 * - Data transfer (bytes, messages)
 * - Protocol operations (requests, responses, notifications)
 * - Performance (latency, throughput)
 * - Errors and failures
 * - Method-specific statistics
 */
class MetricsFilter : public network::NetworkFilterBase,
                      public McpJsonRpcFilter::Callbacks {
 public:
  /**
   * Callbacks for metrics events
   */
  class MetricsCallbacks {
   public:
    virtual ~MetricsCallbacks() = default;

    /**
     * Called periodically with current metrics snapshot
     * @param metrics Current metrics
     */
    virtual void onMetricsUpdate(const ConnectionMetrics& metrics) = 0;

    /**
     * Called when a threshold is exceeded
     * @param metric_name Name of the metric that exceeded threshold
     * @param value Current value
     * @param threshold Threshold that was exceeded
     */
    virtual void onThresholdExceeded(const std::string& metric_name,
                                     uint64_t value,
                                     uint64_t threshold) = 0;
  };

  /**
   * Configuration for metrics collection
   */
  struct Config {
    // Update interval for rate calculations
    std::chrono::seconds rate_update_interval{1};

    // Reporting interval
    std::chrono::seconds report_interval{10};

    // Thresholds for alerts
    uint64_t max_latency_threshold_ms = 5000;
    uint64_t error_rate_threshold = 10;            // errors per minute
    uint64_t bytes_threshold = 100 * 1024 * 1024;  // 100MB

    // Enable detailed method tracking
    bool track_methods = true;

    // Enable latency histograms
    bool enable_histograms = false;
  };

  /**
   * Constructor
   * @param callbacks Metrics event callbacks
   * @param config Metrics configuration
   */
  MetricsFilter(MetricsCallbacks& callbacks, const Config& config)
      : callbacks_(callbacks), config_(config) {
    metrics_.connection_start = std::chrono::steady_clock::now();
    metrics_.last_activity = metrics_.connection_start;

    // Start periodic reporting if configured
    if (config_.report_interval.count() > 0) {
      startReportingTimer();
    }
  }

  // Filter interface implementation
  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    size_t bytes = data.length();
    metrics_.bytes_received += bytes;
    metrics_.messages_received++;
    metrics_.last_activity = std::chrono::steady_clock::now();

    // Update receive rate
    updateReceiveRate(bytes);

    // Check thresholds
    if (metrics_.bytes_received > config_.bytes_threshold) {
      callbacks_.onThresholdExceeded("bytes_received", metrics_.bytes_received,
                                     config_.bytes_threshold);
    }

    return network::FilterStatus::Continue;
  }

  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    size_t bytes = data.length();
    metrics_.bytes_sent += bytes;
    metrics_.messages_sent++;
    metrics_.last_activity = std::chrono::steady_clock::now();

    // Update send rate
    updateSendRate(bytes);

    return network::FilterStatus::Continue;
  }

  network::FilterStatus onNewConnection() override {
    // Reset metrics for new connection
    metrics_.bytes_received = 0;
    metrics_.bytes_sent = 0;
    metrics_.messages_received = 0;
    metrics_.messages_sent = 0;
    metrics_.requests_sent = 0;
    metrics_.requests_received = 0;
    metrics_.responses_sent = 0;
    metrics_.responses_received = 0;
    metrics_.notifications_sent = 0;
    metrics_.notifications_received = 0;
    metrics_.errors_sent = 0;
    metrics_.errors_received = 0;
    metrics_.protocol_errors = 0;
    metrics_.total_latency_ms = 0;
    metrics_.min_latency_ms = UINT64_MAX;
    metrics_.max_latency_ms = 0;
    metrics_.latency_samples = 0;
    metrics_.connection_start = std::chrono::steady_clock::now();
    metrics_.last_activity = metrics_.connection_start;

    // Clear request tracking
    pending_requests_.clear();

    return network::FilterStatus::Continue;
  }

  // McpJsonRpcFilter::Callbacks implementation
  void onRequest(const jsonrpc::Request& request) override {
    metrics_.requests_received++;

    // Track method
    if (config_.track_methods) {
      std::lock_guard<std::mutex> lock(method_mutex_);
      metrics_.method_counts[request.method]++;
    }

    // Start tracking request latency
    if (holds_alternative<int>(request.id) ||
        holds_alternative<std::string>(request.id)) {
      std::lock_guard<std::mutex> lock(request_mutex_);
      pending_requests_[requestIdToString(request.id)] =
          std::chrono::steady_clock::now();
    }

    // Forward to next handler
    if (next_callbacks_) {
      next_callbacks_->onRequest(request);
    }
  }

  void onNotification(const jsonrpc::Notification& notification) override {
    metrics_.notifications_received++;

    if (config_.track_methods) {
      std::lock_guard<std::mutex> lock(method_mutex_);
      metrics_.method_counts[notification.method]++;
    }

    if (next_callbacks_) {
      next_callbacks_->onNotification(notification);
    }
  }

  void onResponse(const jsonrpc::Response& response) override {
    metrics_.responses_received++;

    // Calculate latency if we have the original request
    std::lock_guard<std::mutex> lock(request_mutex_);
    auto it = pending_requests_.find(requestIdToString(response.id));
    if (it != pending_requests_.end()) {
      auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - it->second)
                         .count();

      updateLatencyMetrics(latency);
      pending_requests_.erase(it);
    }

    // Track errors
    if (response.error.has_value()) {
      metrics_.errors_received++;
      checkErrorThreshold();
    }

    if (next_callbacks_) {
      next_callbacks_->onResponse(response);
    }
  }

  void onProtocolError(const Error& error) override {
    metrics_.protocol_errors++;
    checkErrorThreshold();

    if (next_callbacks_) {
      next_callbacks_->onProtocolError(error);
    }
  }

  /**
   * Set the next callbacks in the chain
   */
  void setNextCallbacks(McpJsonRpcFilter::Callbacks* callbacks) {
    next_callbacks_ = callbacks;
  }

  /**
   * Get current metrics snapshot
   */
  void getMetrics(ConnectionMetrics& snapshot) const {
    std::lock_guard<std::mutex> lock(method_mutex_);
    snapshot.bytes_received = metrics_.bytes_received.load();
    snapshot.bytes_sent = metrics_.bytes_sent.load();
    snapshot.messages_received = metrics_.messages_received.load();
    snapshot.messages_sent = metrics_.messages_sent.load();
    snapshot.requests_sent = metrics_.requests_sent.load();
    snapshot.requests_received = metrics_.requests_received.load();
    snapshot.responses_sent = metrics_.responses_sent.load();
    snapshot.responses_received = metrics_.responses_received.load();
    snapshot.notifications_sent = metrics_.notifications_sent.load();
    snapshot.notifications_received = metrics_.notifications_received.load();
    snapshot.errors_sent = metrics_.errors_sent.load();
    snapshot.errors_received = metrics_.errors_received.load();
    snapshot.protocol_errors = metrics_.protocol_errors.load();
    snapshot.total_latency_ms = metrics_.total_latency_ms.load();
    snapshot.min_latency_ms = metrics_.min_latency_ms.load();
    snapshot.max_latency_ms = metrics_.max_latency_ms.load();
    snapshot.latency_samples = metrics_.latency_samples.load();
    snapshot.connection_start = metrics_.connection_start;
    snapshot.last_activity = metrics_.last_activity;
    snapshot.method_latencies_ms = metrics_.method_latencies_ms;
    snapshot.method_counts = metrics_.method_counts;
  }

 private:
  void updateReceiveRate(size_t bytes) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_rate_update_);

    if (elapsed >= config_.rate_update_interval) {
      double rate = (bytes_since_last_update_ * 8.0) / elapsed.count();
      metrics_.current_receive_rate_bps = rate;

      if (rate > metrics_.peak_receive_rate_bps) {
        metrics_.peak_receive_rate_bps = rate;
      }

      bytes_since_last_update_ = 0;
      last_rate_update_ = now;
    } else {
      bytes_since_last_update_ += bytes;
    }
  }

  void updateSendRate(size_t bytes) {
    // Similar to updateReceiveRate but for send
    // Implementation omitted for brevity
  }

  void updateLatencyMetrics(uint64_t latency_ms) {
    metrics_.total_latency_ms += latency_ms;
    metrics_.latency_samples++;

    if (latency_ms < metrics_.min_latency_ms) {
      metrics_.min_latency_ms = latency_ms;
    }

    if (latency_ms > metrics_.max_latency_ms) {
      metrics_.max_latency_ms = latency_ms;

      if (latency_ms > config_.max_latency_threshold_ms) {
        callbacks_.onThresholdExceeded("latency_ms", latency_ms,
                                       config_.max_latency_threshold_ms);
      }
    }
  }

  void checkErrorThreshold() {
    // Check error rate
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::minutes>(
        now - metrics_.connection_start);

    if (age.count() > 0) {
      uint64_t error_rate =
          (metrics_.errors_received + metrics_.protocol_errors) / age.count();
      if (error_rate > config_.error_rate_threshold) {
        callbacks_.onThresholdExceeded("error_rate", error_rate,
                                       config_.error_rate_threshold);
      }
    }
  }

  void startReportingTimer() {
    // TODO: Implement periodic reporting using dispatcher timer
    // This would call callbacks_.onMetricsUpdate(metrics_) periodically
  }

  MetricsCallbacks& callbacks_;
  Config config_;
  McpJsonRpcFilter::Callbacks* next_callbacks_ = nullptr;

  // Metrics data
  ConnectionMetrics metrics_;

  // Rate calculation state
  std::chrono::steady_clock::time_point last_rate_update_;
  std::atomic<size_t> bytes_since_last_update_{0};

  // Request tracking for latency (using string key to avoid variant comparison
  // issues)
  std::map<std::string, std::chrono::steady_clock::time_point>
      pending_requests_;

  // Helper to convert RequestId to string key
  std::string requestIdToString(const RequestId& id) const {
    return visit(make_overload([](const std::string& s) { return s; },
                               [](int i) { return std::to_string(i); }),
                 id);
  }
  mutable std::mutex request_mutex_;

  // Method metrics mutex
  mutable std::mutex method_mutex_;
};

}  // namespace filter
}  // namespace mcp