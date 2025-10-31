/**
 * @file mcp_c_metrics_callbacks.h
 * @brief C API bridge for metrics filter callbacks
 *
 * Provides C-compatible callback definitions for receiving metrics updates
 * and threshold alerts from the C++ MetricsFilter.
 */

#pragma once

#include <stdint.h>

#include "mcp_c_filter_chain.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Aggregated connection metrics reported by the metrics filter.
 *
 * All counters are cumulative for the current connection lifetime unless
 * otherwise noted.
 */
typedef struct mcp_connection_metrics {
  uint64_t bytes_received;
  uint64_t bytes_sent;
  uint64_t messages_received;
  uint64_t messages_sent;
  uint64_t requests_received;
  uint64_t requests_sent;
  uint64_t responses_received;
  uint64_t responses_sent;
  uint64_t notifications_received;
  uint64_t notifications_sent;
  uint64_t errors_received;
  uint64_t errors_sent;
  uint64_t protocol_errors;
  uint64_t total_latency_ms;
  uint64_t min_latency_ms;
  uint64_t max_latency_ms;
  uint64_t latency_samples;
  double current_receive_rate_bps;
  double current_send_rate_bps;
  double peak_receive_rate_bps;
  double peak_send_rate_bps;
  uint64_t connection_uptime_ms;
  uint64_t idle_time_ms;
} mcp_connection_metrics_t;

/**
 * @brief Callback invoked for periodic metrics updates.
 *
 * @param metrics Pointer to metrics snapshot (valid for duration of call)
 * @param user_data Opaque pointer provided during registration
 */
typedef void (*mcp_metrics_update_cb)(const mcp_connection_metrics_t* metrics,
                                      void* user_data);

/**
 * @brief Callback invoked when a configured threshold is exceeded.
 *
 * @param metric_name Name of the metric that triggered the alert
 * @param value Current value
 * @param threshold Configured threshold
 * @param user_data Opaque pointer provided during registration
 */
typedef void (*mcp_metrics_threshold_cb)(const char* metric_name,
                                         uint64_t value,
                                         uint64_t threshold,
                                         void* user_data);

/**
 * @brief Collection of metrics callbacks.
 *
 * All callbacks are optional. If a callback pointer is NULL it will not
 * be invoked.
 */
typedef struct mcp_metrics_callbacks {
  mcp_metrics_update_cb on_metrics_update;
  mcp_metrics_threshold_cb on_threshold_exceeded;
  void* user_data;
} mcp_metrics_callbacks_t;

/**
 * @brief Register metrics callbacks for a filter chain.
 *
 * @param chain Filter chain handle
 * @param callbacks Callback structure (copied internally)
 * @return 0 on success, negative error code on failure:
 *         -1: Invalid chain handle
 *         -2: Metrics filter not present in chain
 *         -3: Internal error
 */
int mcp_filter_chain_set_metrics_callbacks(
    mcp_filter_chain_t chain,
    const mcp_metrics_callbacks_t* callbacks);

/**
 * @brief Clear previously registered metrics callbacks.
 *
 * @param chain Filter chain handle
 * @return 0 on success, negative error code on failure
 */
int mcp_filter_chain_clear_metrics_callbacks(mcp_filter_chain_t chain);

/**
 * @brief Check if metrics callbacks are registered.
 *
 * @param chain Filter chain handle
 * @return 1 if callbacks are registered, 0 if not, negative on error
 */
int mcp_filter_chain_has_metrics_callbacks(mcp_filter_chain_t chain);

#ifdef __cplusplus
}  // extern "C"
#endif
