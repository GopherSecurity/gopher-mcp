/**
 * @file filter_chain_callbacks.h
 * @brief Unified chain-level callback interface for filter events
 *
 * This file defines the callback interface used by observers to receive
 * events from all filters in a chain through a single callback handler.
 * This replaces per-filter callback interfaces, providing a unified
 * observability surface for logging, metrics, and monitoring.
 */

#pragma once

#include "filter_event.h"

namespace mcp {
namespace filter {

/**
 * @brief Chain-level callback interface
 *
 * Observers implement this interface to receive events from all filters
 * in a filter chain. Events are delivered synchronously in the dispatcher
 * thread context.
 *
 * Thread Safety: All callback methods are invoked in the dispatcher thread.
 * Implementations must not block or perform long-running operations.
 */
class FilterChainCallbacks {
 public:
  virtual ~FilterChainCallbacks() = default;

  /**
   * @brief Called when any filter in the chain emits an event
   *
   * This method receives all events from all filters in the chain,
   * enabling unified observability, correlation, and monitoring.
   *
   * @param event The filter event with full context and payload
   *
   * @note This callback is invoked synchronously in the dispatcher thread.
   *       Implementations should process events quickly and avoid blocking.
   *       For expensive operations (I/O, network calls), queue the event
   *       for asynchronous processing.
   */
  virtual void onFilterEvent(const FilterEvent& event) = 0;
};

}  // namespace filter
}  // namespace mcp
