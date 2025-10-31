/**
 * @file metrics_filter.h
 * @brief Enhanced metrics collection filter for MCP connections
 *
 * EQUAL USE: BOTH CLIENT AND SERVER - Essential for observability
 *
 * This filter collects detailed metrics about connection performance,
 * request/response patterns, and protocol-specific statistics.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include "../network/filter.h"
#include "../types.h"
#include "json_rpc_protocol_filter.h"

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
 * Enhanced metrics collection filter supporting both Scenario 1 (native stack)
 * and Scenario 2 (SDK hybrid) deployments. The core class focuses on
 * protocol-level observations; an optional NetworkAdapter bridges byte-level
 * counters when the full transport stack is available.
 */
class MetricsFilter : public JsonRpcProtocolFilter::MessageHandler,
                      public std::enable_shared_from_this<MetricsFilter> {
 public:
  class NetworkAdapter;

  /** Callbacks for metrics events */
  class MetricsCallbacks {
   public:
    virtual ~MetricsCallbacks() = default;

    /** Called periodically with current metrics snapshot */
    virtual void onMetricsUpdate(const ConnectionMetrics& metrics) = 0;

    /** Called when a threshold is exceeded */
    virtual void onThresholdExceeded(const std::string& metric_name,
                                     uint64_t value,
                                     uint64_t threshold) = 0;
  };

  /** Configuration for metrics collection */
  struct Config {
    std::chrono::seconds rate_update_interval{1};
    std::chrono::seconds report_interval{10};
    uint64_t max_latency_threshold_ms = 5000;
    uint64_t error_rate_threshold = 10;            // errors per minute
    uint64_t bytes_threshold = 100 * 1024 * 1024;  // 100MB
    bool track_methods = true;
    bool enable_histograms = false;
  };

  MetricsFilter(MetricsCallbacks& callbacks, const Config& config);
  MetricsFilter(std::shared_ptr<MetricsCallbacks> callbacks,
                const Config& config);

  void setCallbacks(MetricsCallbacks& callbacks);
  void setCallbacks(std::shared_ptr<MetricsCallbacks> callbacks);

  /**
   * Create a transport-layer adapter that feeds byte-level statistics into the
   * collector. In Scenario 1 this adapter is inserted into the network filter
   * chain; Scenario 2 can skip it entirely and rely on protocol events only.
   */
  std::shared_ptr<NetworkAdapter> createNetworkAdapter();

  /** Called when a new connection is established */
  void onConnectionOpened();

  /** Called when a connection is torn down */
  void onConnectionClosed();

  /** Record bytes received from the network or SDK transport */
  void recordIncomingBytes(size_t bytes);

  /** Record bytes sent to the network or SDK transport */
  void recordOutgoingBytes(size_t bytes);

  /** JsonRpcProtocolFilter::MessageHandler overrides */
  void onRequest(const jsonrpc::Request& request) override;
  void onNotification(const jsonrpc::Notification& notification) override;
  void onResponse(const jsonrpc::Response& response) override;
  void onProtocolError(const Error& error) override;

  /** Chain to the next message handler */
  void setNextCallbacks(JsonRpcProtocolFilter::MessageHandler* callbacks);

  /** Populate a snapshot with the current metrics */
  void getMetrics(ConnectionMetrics& snapshot) const;

 private:
 friend class NetworkAdapter;

  void initializeMetricsState();
  void resetConnectionMetrics();
  void updateReceiveRate(size_t bytes);
  void updateSendRate(size_t bytes);
  void updateLatencyMetrics(uint64_t latency_ms);
  void checkErrorThreshold();
  void startReportingTimer();
  std::string requestIdToString(const RequestId& id) const;

  void recordActivity();

  std::shared_ptr<MetricsCallbacks> callbacks_holder_;
  std::shared_ptr<MetricsCallbacks> default_callbacks_holder_;
  MetricsCallbacks* callbacks_{nullptr};
  MetricsCallbacks* default_callbacks_{nullptr};
  Config config_;
  JsonRpcProtocolFilter::MessageHandler* next_callbacks_{nullptr};

  ConnectionMetrics metrics_;

  // Rate calculation state
  std::chrono::steady_clock::time_point last_receive_rate_update_{};
  std::chrono::steady_clock::time_point last_send_rate_update_{};
  std::atomic<size_t> bytes_since_last_receive_update_{0};
  std::atomic<size_t> bytes_since_last_send_update_{0};

  // Request tracking for latency (using string key to avoid variant comparison)
  std::map<std::string, std::chrono::steady_clock::time_point>
      pending_requests_;
  mutable std::mutex request_mutex_;

  // Method metrics mutex
  mutable std::mutex method_mutex_;
};

/**
 * Optional transport adapter that feeds network-level events into the
 * MetricsFilter. Owns a shared reference so the collector stays alive
 * whenever the adapter is installed in a filter chain.
 */
class MetricsFilter::NetworkAdapter : public network::NetworkFilterBase {
 public:
  explicit NetworkAdapter(std::shared_ptr<MetricsFilter> owner);

  network::FilterStatus onData(Buffer& data, bool end_stream) override;
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override;
  network::FilterStatus onNewConnection() override;

  std::shared_ptr<MetricsFilter> getMetricsFilter() const;

 private:
  std::shared_ptr<MetricsFilter> owner_;
};

}  // namespace filter
}  // namespace mcp

// === Inline Implementations ===

namespace mcp {
namespace filter {

inline MetricsFilter::MetricsFilter(MetricsCallbacks& callbacks,
                                    const Config& config)
    : callbacks_holder_(nullptr),
      default_callbacks_holder_(nullptr),
      callbacks_(&callbacks),
      default_callbacks_(&callbacks),
      config_(config) {
  initializeMetricsState();
}

inline MetricsFilter::MetricsFilter(
    std::shared_ptr<MetricsCallbacks> callbacks, const Config& config)
    : callbacks_holder_(std::move(callbacks)),
      default_callbacks_holder_(callbacks_holder_),
      callbacks_(callbacks_holder_.get()),
      default_callbacks_(callbacks_),
      config_(config) {
  if (!callbacks_holder_) {
    throw std::invalid_argument("Metrics callbacks must not be null");
  }
  initializeMetricsState();
}

inline void MetricsFilter::initializeMetricsState() {
  if (!callbacks_) {
    throw std::invalid_argument("Metrics callbacks must not be null");
  }
  auto now = std::chrono::steady_clock::now();
  metrics_.connection_start = now;
  metrics_.last_activity = now;
  last_receive_rate_update_ = now;
  last_send_rate_update_ = now;

  if (config_.report_interval.count() > 0) {
    startReportingTimer();
  }
}

inline std::shared_ptr<MetricsFilter::NetworkAdapter>
MetricsFilter::createNetworkAdapter() {
  return std::make_shared<NetworkAdapter>(shared_from_this());
}

inline void MetricsFilter::setCallbacks(MetricsCallbacks& callbacks) {
  callbacks_holder_.reset();
  callbacks_ = &callbacks;
  default_callbacks_holder_.reset();
  default_callbacks_ = callbacks_;
  if (!callbacks_) {
    throw std::invalid_argument("Metrics callbacks must not be null");
  }
}

inline void MetricsFilter::setCallbacks(
    std::shared_ptr<MetricsCallbacks> callbacks) {
  if (callbacks) {
    callbacks_holder_ = std::move(callbacks);
    callbacks_ = callbacks_holder_.get();
  } else {
    callbacks_holder_ = default_callbacks_holder_;
    if (!callbacks_holder_) {
      callbacks_holder_.reset();
    }
    callbacks_ = default_callbacks_;
  }
  if (!callbacks_) {
    throw std::invalid_argument("Metrics callbacks must not be null");
  }
}

inline void MetricsFilter::onConnectionOpened() {
  resetConnectionMetrics();
}

inline void MetricsFilter::onConnectionClosed() {
  metrics_.last_activity = std::chrono::steady_clock::now();
}

inline void MetricsFilter::recordIncomingBytes(size_t bytes) {
  metrics_.bytes_received += bytes;
  metrics_.messages_received++;
  recordActivity();
  updateReceiveRate(bytes);

  if (metrics_.bytes_received > config_.bytes_threshold) {
    callbacks_->onThresholdExceeded("bytes_received",
                                    metrics_.bytes_received,
                                    config_.bytes_threshold);
  }
}

inline void MetricsFilter::recordOutgoingBytes(size_t bytes) {
  metrics_.bytes_sent += bytes;
  metrics_.messages_sent++;
  recordActivity();
  updateSendRate(bytes);
}

inline void MetricsFilter::onRequest(const jsonrpc::Request& request) {
  metrics_.requests_received++;
  recordActivity();

  if (config_.track_methods) {
    std::lock_guard<std::mutex> lock(method_mutex_);
    metrics_.method_counts[request.method]++;
  }

  if (holds_alternative<int>(request.id) ||
      holds_alternative<std::string>(request.id)) {
    std::lock_guard<std::mutex> lock(request_mutex_);
    pending_requests_[requestIdToString(request.id)] =
        std::chrono::steady_clock::now();
  }

  if (next_callbacks_) {
    next_callbacks_->onRequest(request);
  }
}

inline void MetricsFilter::onNotification(
    const jsonrpc::Notification& notification) {
  metrics_.notifications_received++;
  recordActivity();

  if (config_.track_methods) {
    std::lock_guard<std::mutex> lock(method_mutex_);
    metrics_.method_counts[notification.method]++;
  }

  if (next_callbacks_) {
    next_callbacks_->onNotification(notification);
  }
}

inline void MetricsFilter::onResponse(const jsonrpc::Response& response) {
  metrics_.responses_received++;
  recordActivity();

  {
    std::lock_guard<std::mutex> lock(request_mutex_);
    auto it = pending_requests_.find(requestIdToString(response.id));
    if (it != pending_requests_.end()) {
      auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - it->second)
                         .count();
      updateLatencyMetrics(static_cast<uint64_t>(latency));
      pending_requests_.erase(it);
    }
  }

  if (response.error.has_value()) {
    metrics_.errors_received++;
    checkErrorThreshold();
  }

  if (next_callbacks_) {
    next_callbacks_->onResponse(response);
  }
}

inline void MetricsFilter::onProtocolError(const Error& error) {
  (void)error;
  metrics_.protocol_errors++;
  recordActivity();
  checkErrorThreshold();

  if (next_callbacks_) {
    next_callbacks_->onProtocolError(error);
  }
}

inline void MetricsFilter::setNextCallbacks(
    JsonRpcProtocolFilter::MessageHandler* callbacks) {
  next_callbacks_ = callbacks;
}

inline void MetricsFilter::getMetrics(ConnectionMetrics& snapshot) const {
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

inline void MetricsFilter::resetConnectionMetrics() {
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
  last_receive_rate_update_ = metrics_.connection_start;
  last_send_rate_update_ = metrics_.connection_start;
  bytes_since_last_receive_update_ = 0;
  bytes_since_last_send_update_ = 0;

  std::lock_guard<std::mutex> lock(request_mutex_);
  pending_requests_.clear();
}

inline void MetricsFilter::updateReceiveRate(size_t bytes) {
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      now - last_receive_rate_update_);

  if (elapsed >= config_.rate_update_interval && elapsed.count() > 0) {
    double rate = (bytes_since_last_receive_update_ * 8.0) / elapsed.count();
    metrics_.current_receive_rate_bps = rate;
    if (rate > metrics_.peak_receive_rate_bps) {
      metrics_.peak_receive_rate_bps = rate;
    }
    bytes_since_last_receive_update_ = 0;
    last_receive_rate_update_ = now;
  } else {
    bytes_since_last_receive_update_ += bytes;
  }
}

inline void MetricsFilter::updateSendRate(size_t bytes) {
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      now - last_send_rate_update_);

  if (elapsed >= config_.rate_update_interval && elapsed.count() > 0) {
    double rate = (bytes_since_last_send_update_ * 8.0) / elapsed.count();
    metrics_.current_send_rate_bps = rate;
    if (rate > metrics_.peak_send_rate_bps) {
      metrics_.peak_send_rate_bps = rate;
    }
    bytes_since_last_send_update_ = 0;
    last_send_rate_update_ = now;
  } else {
    bytes_since_last_send_update_ += bytes;
  }
}

inline void MetricsFilter::updateLatencyMetrics(uint64_t latency_ms) {
  metrics_.total_latency_ms += latency_ms;
  metrics_.latency_samples++;

  auto current_min = metrics_.min_latency_ms.load();
  if (latency_ms < current_min) {
    metrics_.min_latency_ms = latency_ms;
  }

  auto current_max = metrics_.max_latency_ms.load();
  if (latency_ms > current_max) {
    metrics_.max_latency_ms = latency_ms;
    if (latency_ms > config_.max_latency_threshold_ms) {
      callbacks_->onThresholdExceeded("latency_ms", latency_ms,
                                      config_.max_latency_threshold_ms);
    }
  }
}

inline void MetricsFilter::checkErrorThreshold() {
  auto now = std::chrono::steady_clock::now();
  auto age = std::chrono::duration_cast<std::chrono::minutes>(
      now - metrics_.connection_start);

  if (age.count() > 0) {
    uint64_t error_rate =
        (metrics_.errors_received + metrics_.protocol_errors) /
        static_cast<uint64_t>(age.count());
    if (error_rate > config_.error_rate_threshold) {
      callbacks_->onThresholdExceeded("error_rate", error_rate,
                                      config_.error_rate_threshold);
    }
  }
}

inline void MetricsFilter::startReportingTimer() {
  // TODO: Implement periodic reporting using dispatcher timer
  // This would call callbacks_->onMetricsUpdate(metrics_) periodically
}

inline std::string MetricsFilter::requestIdToString(
    const RequestId& id) const {
  return visit(make_overload([](const std::string& s) { return s; },
                             [](int i) { return std::to_string(i); }),
               id);
}

inline void MetricsFilter::recordActivity() {
  metrics_.last_activity = std::chrono::steady_clock::now();
}

inline MetricsFilter::NetworkAdapter::NetworkAdapter(
    std::shared_ptr<MetricsFilter> owner)
    : owner_(std::move(owner)) {}

inline network::FilterStatus MetricsFilter::NetworkAdapter::onData(
    Buffer& data, bool /*end_stream*/) {
  owner_->recordIncomingBytes(data.length());
  return network::FilterStatus::Continue;
}

inline network::FilterStatus MetricsFilter::NetworkAdapter::onWrite(
    Buffer& data, bool /*end_stream*/) {
  owner_->recordOutgoingBytes(data.length());
  return network::FilterStatus::Continue;
}

inline network::FilterStatus MetricsFilter::NetworkAdapter::onNewConnection() {
  owner_->onConnectionOpened();
  return network::FilterStatus::Continue;
}

inline std::shared_ptr<MetricsFilter>
MetricsFilter::NetworkAdapter::getMetricsFilter() const {
  return owner_;
}

}  // namespace filter
}  // namespace mcp
