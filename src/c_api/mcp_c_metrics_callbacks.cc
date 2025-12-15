/**
 * @file mcp_c_metrics_callbacks.cc
 * @brief Implementation of metrics filter callbacks C API bridge
 */

#include "mcp/c_api/mcp_c_metrics_callbacks.h"

#include <chrono>
#include <exception>
#include <memory>
#include <string>

#include "mcp/c_api/mcp_c_filter_chain.h"
#include "mcp/filter/metrics_filter.h"
#include "mcp/logging/log_macros.h"

#include "handle_manager.h"
#include "unified_filter_chain.h"

#undef GOPHER_LOG_COMPONENT
#define GOPHER_LOG_COMPONENT "capi.metrics"

namespace mcp {
namespace c_api_internal {

// Forward declared in mcp_c_filter_chain.cc
extern HandleManager<UnifiedFilterChain> g_unified_chain_manager;

}  // namespace c_api_internal

namespace filter_chain {

class AdvancedFilterChain;

bool advanced_chain_set_metrics_callbacks(
    AdvancedFilterChain& chain,
    std::shared_ptr<mcp::filter::MetricsFilter::MetricsCallbacks> callbacks);
void advanced_chain_clear_metrics_callbacks(AdvancedFilterChain& chain);
bool advanced_chain_has_metrics_callbacks(const AdvancedFilterChain& chain);

}  // namespace filter_chain
}  // namespace mcp

namespace {

using namespace mcp;
using namespace mcp::filter;
using namespace mcp::filter_chain;

mcp_connection_metrics_t to_c_metrics(const ConnectionMetrics& metrics) {
  mcp_connection_metrics_t snapshot{};

  snapshot.bytes_received = metrics.bytes_received.load();
  snapshot.bytes_sent = metrics.bytes_sent.load();
  snapshot.messages_received = metrics.messages_received.load();
  snapshot.messages_sent = metrics.messages_sent.load();
  snapshot.requests_received = metrics.requests_received.load();
  snapshot.requests_sent = metrics.requests_sent.load();
  snapshot.responses_received = metrics.responses_received.load();
  snapshot.responses_sent = metrics.responses_sent.load();
  snapshot.notifications_received = metrics.notifications_received.load();
  snapshot.notifications_sent = metrics.notifications_sent.load();
  snapshot.errors_received = metrics.errors_received.load();
  snapshot.errors_sent = metrics.errors_sent.load();
  snapshot.protocol_errors = metrics.protocol_errors.load();
  snapshot.total_latency_ms = metrics.total_latency_ms.load();
  snapshot.min_latency_ms = metrics.min_latency_ms.load();
  snapshot.max_latency_ms = metrics.max_latency_ms.load();
  snapshot.latency_samples = metrics.latency_samples.load();
  snapshot.current_receive_rate_bps = metrics.current_receive_rate_bps;
  snapshot.current_send_rate_bps = metrics.current_send_rate_bps;
  snapshot.peak_receive_rate_bps = metrics.peak_receive_rate_bps;
  snapshot.peak_send_rate_bps = metrics.peak_send_rate_bps;

  const auto now = std::chrono::steady_clock::now();
  snapshot.connection_uptime_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now - metrics.connection_start)
          .count());
  snapshot.idle_time_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now - metrics.last_activity)
          .count());

  return snapshot;
}

class MetricsCallbackBridge : public MetricsFilter::MetricsCallbacks {
 public:
  explicit MetricsCallbackBridge(const mcp_metrics_callbacks_t& callbacks)
      : callbacks_(callbacks) {}

  void onMetricsUpdate(const ConnectionMetrics& metrics) override {
    if (!callbacks_.on_metrics_update) {
      return;
    }

    try {
      auto snapshot = to_c_metrics(metrics);
      callbacks_.on_metrics_update(&snapshot, callbacks_.user_data);
    } catch (const std::exception& e) {
      GOPHER_LOG(Error, "Exception in metrics update callback: {}", e.what());
    } catch (...) {
      GOPHER_LOG(Error, "Unknown exception in metrics update callback");
    }
  }

  void onThresholdExceeded(const std::string& metric_name,
                           uint64_t value,
                           uint64_t threshold) override {
    if (!callbacks_.on_threshold_exceeded) {
      return;
    }

    try {
      callbacks_.on_threshold_exceeded(metric_name.c_str(), value, threshold,
                                       callbacks_.user_data);
    } catch (const std::exception& e) {
      GOPHER_LOG(Error, "Exception in metrics threshold callback ({}): {}",
                 metric_name, e.what());
    } catch (...) {
      GOPHER_LOG(Error, "Unknown exception in metrics threshold callback ({})",
                 metric_name);
    }
  }

 private:
  mcp_metrics_callbacks_t callbacks_;
};

}  // namespace

extern "C" {

int mcp_filter_chain_set_metrics_callbacks(
    mcp_filter_chain_t chain, const mcp_metrics_callbacks_t* callbacks) {
  if (!chain) {
    GOPHER_LOG(Error, "Invalid chain handle (NULL)");
    return -1;
  }

  if (!callbacks) {
    GOPHER_LOG(Error, "Invalid metrics callbacks pointer (NULL)");
    return -1;
  }

  auto unified_chain = c_api_internal::g_unified_chain_manager.get(chain);
  if (!unified_chain) {
    GOPHER_LOG(Error, "Chain handle not found in manager");
    return -1;
  }

  auto advanced_chain = unified_chain->getAdvancedChain();
  if (!advanced_chain) {
    GOPHER_LOG(Warning,
               "Metrics callbacks only supported on advanced filter chains");
    return -2;
  }

  try {
    auto callback_bridge = std::make_shared<MetricsCallbackBridge>(*callbacks);
    bool registered =
        advanced_chain_set_metrics_callbacks(*advanced_chain, callback_bridge);
    if (!registered) {
      GOPHER_LOG(Warning,
                 "Metrics callbacks registered but no metrics filter present "
                 "in chain");
      return -2;
    }
    return 0;
  } catch (const std::exception& e) {
    GOPHER_LOG(Error, "Failed to set metrics callbacks: {}", e.what());
    return -3;
  } catch (...) {
    GOPHER_LOG(Error, "Unknown error while setting metrics callbacks");
    return -3;
  }
}

int mcp_filter_chain_clear_metrics_callbacks(mcp_filter_chain_t chain) {
  if (!chain) {
    GOPHER_LOG(Error, "Invalid chain handle (NULL)");
    return -1;
  }

  auto unified_chain = c_api_internal::g_unified_chain_manager.get(chain);
  if (!unified_chain) {
    GOPHER_LOG(Error, "Chain handle not found in manager");
    return -1;
  }

  auto advanced_chain = unified_chain->getAdvancedChain();
  if (!advanced_chain) {
    return -2;
  }

  advanced_chain_clear_metrics_callbacks(*advanced_chain);
  return 0;
}

int mcp_filter_chain_has_metrics_callbacks(mcp_filter_chain_t chain) {
  if (!chain) {
    return -1;
  }

  auto unified_chain = c_api_internal::g_unified_chain_manager.get(chain);
  if (!unified_chain) {
    return -1;
  }

  auto advanced_chain = unified_chain->getAdvancedChain();
  if (!advanced_chain) {
    return -2;
  }

  return advanced_chain_has_metrics_callbacks(*advanced_chain) ? 1 : 0;
}

}  // extern "C"
