/**
 * @file filter_event_emitter.h
 * @brief Helper for filters to emit events with context enrichment
 *
 * The FilterEventEmitter provides a convenient interface for filters to
 * emit events. It automatically enriches events with FilterEventContext
 * (chain ID, stream ID, correlation ID) and forwards them to the event hub.
 */

#pragma once

#include <memory>
#include <string>

#include "filter_chain_event_hub.h"
#include "filter_event.h"

namespace mcp {
namespace filter {

/**
 * @brief Event emitter helper for filters
 *
 * Filters receive an instance of this emitter via FilterCreationContext.
 * The emitter simplifies event emission by:
 * - Automatically enriching events with context (chain ID, stream ID, etc.)
 * - Validating event payloads (if enabled)
 * - Forwarding to the chain's event hub
 *
 * Thread Safety: This class is not thread-safe on its own, but is designed
 * to be used within a single filter instance which operates in a single
 * dispatcher thread.
 */
class FilterEventEmitter {
 public:
  /**
   * @brief Constructor
   *
   * @param hub Event hub to forward events to
   * @param filter_name Name of the filter using this emitter
   * @param filter_instance_id Optional instance ID for the filter
   * @param chain_id Chain identifier for context
   */
  FilterEventEmitter(std::shared_ptr<FilterChainEventHub> hub,
                     const std::string& filter_name,
                     const std::string& filter_instance_id = "",
                     const std::string& chain_id = "");

  /**
   * @brief Destructor
   */
  ~FilterEventEmitter() = default;

  // Non-copyable, movable
  FilterEventEmitter(const FilterEventEmitter&) = delete;
  FilterEventEmitter& operator=(const FilterEventEmitter&) = delete;
  FilterEventEmitter(FilterEventEmitter&&) = default;
  FilterEventEmitter& operator=(FilterEventEmitter&&) = default;

  /**
   * @brief Emit an event with automatic context enrichment
   *
   * Creates a FilterEvent with the provided type, severity, and data,
   * enriches it with the filter's context information, and forwards it
   * to the event hub.
   *
   * @param event_type Type of event
   * @param severity Event severity level
   * @param event_data Event-specific data (filter-defined schema)
   */
  void emit(FilterEventType event_type,
            FilterEventSeverity severity,
            const json::JsonValue& event_data);

  /**
   * @brief Emit an event with custom stream/correlation IDs
   *
   * Allows overriding the stream ID and correlation ID for specific events
   * while still using the filter's name and chain ID.
   *
   * @param event_type Type of event
   * @param severity Event severity level
   * @param event_data Event-specific data
   * @param stream_id Custom stream identifier
   * @param correlation_id Custom correlation identifier
   */
  void emit(FilterEventType event_type,
            FilterEventSeverity severity,
            const json::JsonValue& event_data,
            const std::string& stream_id,
            const std::string& correlation_id = "");

  /**
   * @brief Emit a fully constructed FilterEvent
   *
   * For advanced use cases where the filter wants full control over
   * the event structure. The emitter will still enrich context fields
   * if they are empty.
   *
   * @param event The event to emit
   */
  void emitEvent(FilterEvent event);

  /**
   * @brief Set the stream ID for subsequent events
   *
   * Useful for connection-scoped or stream-scoped contexts.
   *
   * @param stream_id Stream/session identifier
   */
  void setStreamId(const std::string& stream_id);

  /**
   * @brief Set the correlation ID for subsequent events
   *
   * Useful for request-scoped tracing across filters.
   *
   * @param correlation_id Correlation identifier
   */
  void setCorrelationId(const std::string& correlation_id);

  /**
   * @brief Get the filter name
   */
  const std::string& getFilterName() const { return filter_name_; }

  /**
   * @brief Get the filter instance ID
   */
  const std::string& getFilterInstanceId() const {
    return filter_instance_id_;
  }

  /**
   * @brief Get the chain ID
   */
  const std::string& getChainId() const { return chain_id_; }

  /**
   * @brief Check if emitter is connected to a hub
   */
  bool isConnected() const { return hub_ != nullptr; }

 private:
  std::shared_ptr<FilterChainEventHub> hub_;
  std::string filter_name_;
  std::string filter_instance_id_;
  std::string chain_id_;
  std::string stream_id_;
  std::string correlation_id_;
};

}  // namespace filter
}  // namespace mcp
