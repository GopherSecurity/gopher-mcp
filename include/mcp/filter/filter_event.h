/**
 * @file filter_event.h
 * @brief Unified event model for chain-level filter callbacks
 *
 * This file defines the event types, severity levels, and event structures
 * used by the unified chain-level callback system. All filters emit events
 * through this common interface, allowing observers to receive events from
 * any filter in the chain through a single callback handler.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "mcp/json/json_bridge.h"

namespace mcp {
namespace filter {

/**
 * @brief Filter event types
 *
 * Comprehensive enumeration of all events that can be emitted by filters.
 * Events are categorized by filter type and purpose.
 */
enum class FilterEventType {
  // Circuit breaker events
  CIRCUIT_STATE_CHANGE = 0,        ///< Circuit breaker state transition
  CIRCUIT_REQUEST_BLOCKED = 1,     ///< Request blocked by open circuit
  CIRCUIT_HEALTH_UPDATE = 2,       ///< Health metrics update

  // Rate limiter events
  RATE_LIMIT_EXCEEDED = 3,         ///< Rate limit quota exceeded
  RATE_LIMIT_RESET = 4,            ///< Rate limit window reset (legacy)
  RATE_LIMIT_SAMPLE = 5,           ///< Rate limit token sample
  RATE_LIMIT_WINDOW_RESET = 6,     ///< Rate limit window reset event
  RATE_LIMIT_CONFIGURATION_SYNCED = 7,  ///< Rate limit config updated

  // Metrics events
  METRIC_UPDATE = 50,              ///< Metric value update
  METRIC_FLUSH = 51,               ///< Metrics flushed to backend

  // Request logger events
  REQUEST_LOGGED = 60,             ///< Request logged
  RESPONSE_LOGGED = 61,            ///< Response logged

  // Protocol events
  PROTOCOL_ERROR = 70,             ///< Protocol-level error
  PROTOCOL_UPGRADE = 71,           ///< Protocol upgrade (e.g., HTTP/2)

  // Generic filter events
  FILTER_INITIALIZED = 80,         ///< Filter initialized
  FILTER_DESTROYED = 81,           ///< Filter destroyed
  FILTER_ERROR = 100,              ///< Generic filter error
  FILTER_WARNING = 101,            ///< Generic filter warning
  FILTER_INFO = 102                ///< Generic filter info
};

/**
 * @brief Event severity levels
 *
 * Severity classification for filtering and prioritization of events.
 */
enum class FilterEventSeverity {
  TRACE = 0,      ///< Trace-level diagnostic information
  DEBUG = 1,      ///< Debug-level information
  INFO = 2,       ///< Informational messages
  WARN = 3,       ///< Warning conditions
  ERROR = 4,      ///< Error conditions
  CRITICAL = 5    ///< Critical conditions requiring immediate attention
};

/**
 * @brief Event context metadata
 *
 * Contextual information propagated with every event, enabling correlation
 * across filters and streams.
 */
struct FilterEventContext {
  /// Filter chain identifier (unique per chain instance)
  std::string chain_id;

  /// Stream or session identifier (for HTTP/2 streams, connection IDs, etc.)
  std::string stream_id;

  /// Correlation identifier for tracing requests across filters
  std::string correlation_id;

  /**
   * @brief Default constructor
   */
  FilterEventContext() = default;

  /**
   * @brief Constructor with all fields
   */
  FilterEventContext(const std::string& chain,
                     const std::string& stream = "",
                     const std::string& correlation = "")
      : chain_id(chain),
        stream_id(stream),
        correlation_id(correlation) {}

  /**
   * @brief Convert to JSON for serialization
   */
  json::JsonValue toJson() const {
    json::JsonObjectBuilder builder;
    if (!chain_id.empty()) {
      builder.add("chain_id", chain_id);
    }
    if (!stream_id.empty()) {
      builder.add("stream_id", stream_id);
    }
    if (!correlation_id.empty()) {
      builder.add("correlation_id", correlation_id);
    }
    return builder.build();
  }
};

/**
 * @brief Unified filter event
 *
 * Complete event structure emitted by filters and delivered to chain-level
 * observers. Contains event identity, context, and filter-specific payload.
 */
struct FilterEvent {
  /// Event context (chain ID, stream ID, correlation ID)
  FilterEventContext context;

  /// Filter name (e.g., "circuit_breaker", "rate_limiter")
  std::string filter_name;

  /// Filter instance ID (unique if multiple instances of same filter type)
  std::string filter_instance_id;

  /// Event type classification
  FilterEventType event_type;

  /// Event severity level
  FilterEventSeverity severity;

  /// Event-specific structured data (filter-defined schema)
  json::JsonValue event_data;

  /// Event timestamp
  std::chrono::system_clock::time_point timestamp;

  /**
   * @brief Default constructor
   */
  FilterEvent()
      : event_type(FilterEventType::FILTER_INFO),
        severity(FilterEventSeverity::INFO),
        event_data(json::JsonValue::object()),
        timestamp(std::chrono::system_clock::now()) {}

  /**
   * @brief Constructor with essential fields
   */
  FilterEvent(const std::string& filter,
              FilterEventType type,
              FilterEventSeverity sev = FilterEventSeverity::INFO)
      : filter_name(filter),
        event_type(type),
        severity(sev),
        event_data(json::JsonValue::object()),
        timestamp(std::chrono::system_clock::now()) {}

  /**
   * @brief Constructor with full fields
   */
  FilterEvent(const FilterEventContext& ctx,
              const std::string& filter,
              const std::string& instance_id,
              FilterEventType type,
              FilterEventSeverity sev,
              const json::JsonValue& data)
      : context(ctx),
        filter_name(filter),
        filter_instance_id(instance_id),
        event_type(type),
        severity(sev),
        event_data(data),
        timestamp(std::chrono::system_clock::now()) {}

  /**
   * @brief Convert to JSON for serialization
   */
  json::JsonValue toJson() const {
    json::JsonObjectBuilder builder;

    // Add context if present
    if (!context.chain_id.empty() || !context.stream_id.empty() ||
        !context.correlation_id.empty()) {
      builder.add("context", context.toJson());
    }

    // Add filter identification
    builder.add("filter_name", filter_name);
    if (!filter_instance_id.empty()) {
      builder.add("filter_instance_id", filter_instance_id);
    }

    // Add event type and severity
    builder.add("event_type", static_cast<int>(event_type));
    builder.add("severity", static_cast<int>(severity));

    // Add event data
    if (!event_data.isNull()) {
      builder.add("event_data", event_data);
    }

    // Add timestamp (milliseconds since epoch)
    auto epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        timestamp.time_since_epoch())
                        .count();
    builder.add("timestamp_ms", static_cast<double>(epoch_ms));

    return builder.build();
  }

  /**
   * @brief Get timestamp in milliseconds since epoch
   */
  int64_t getTimestampMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               timestamp.time_since_epoch())
        .count();
  }
};

/**
 * @brief Convert FilterEventType to string
 */
inline const char* toString(FilterEventType type) {
  switch (type) {
    case FilterEventType::CIRCUIT_STATE_CHANGE:
      return "CIRCUIT_STATE_CHANGE";
    case FilterEventType::CIRCUIT_REQUEST_BLOCKED:
      return "CIRCUIT_REQUEST_BLOCKED";
    case FilterEventType::CIRCUIT_HEALTH_UPDATE:
      return "CIRCUIT_HEALTH_UPDATE";
    case FilterEventType::RATE_LIMIT_EXCEEDED:
      return "RATE_LIMIT_EXCEEDED";
    case FilterEventType::RATE_LIMIT_RESET:
      return "RATE_LIMIT_RESET";
    case FilterEventType::RATE_LIMIT_SAMPLE:
      return "RATE_LIMIT_SAMPLE";
    case FilterEventType::RATE_LIMIT_WINDOW_RESET:
      return "RATE_LIMIT_WINDOW_RESET";
    case FilterEventType::RATE_LIMIT_CONFIGURATION_SYNCED:
      return "RATE_LIMIT_CONFIGURATION_SYNCED";
    case FilterEventType::METRIC_UPDATE:
      return "METRIC_UPDATE";
    case FilterEventType::METRIC_FLUSH:
      return "METRIC_FLUSH";
    case FilterEventType::REQUEST_LOGGED:
      return "REQUEST_LOGGED";
    case FilterEventType::RESPONSE_LOGGED:
      return "RESPONSE_LOGGED";
    case FilterEventType::PROTOCOL_ERROR:
      return "PROTOCOL_ERROR";
    case FilterEventType::PROTOCOL_UPGRADE:
      return "PROTOCOL_UPGRADE";
    case FilterEventType::FILTER_INITIALIZED:
      return "FILTER_INITIALIZED";
    case FilterEventType::FILTER_DESTROYED:
      return "FILTER_DESTROYED";
    case FilterEventType::FILTER_ERROR:
      return "FILTER_ERROR";
    case FilterEventType::FILTER_WARNING:
      return "FILTER_WARNING";
    case FilterEventType::FILTER_INFO:
      return "FILTER_INFO";
    default:
      return "UNKNOWN";
  }
}

/**
 * @brief Convert FilterEventSeverity to string
 */
inline const char* toString(FilterEventSeverity severity) {
  switch (severity) {
    case FilterEventSeverity::TRACE:
      return "TRACE";
    case FilterEventSeverity::DEBUG:
      return "DEBUG";
    case FilterEventSeverity::INFO:
      return "INFO";
    case FilterEventSeverity::WARN:
      return "WARN";
    case FilterEventSeverity::ERROR:
      return "ERROR";
    case FilterEventSeverity::CRITICAL:
      return "CRITICAL";
    default:
      return "UNKNOWN";
  }
}

}  // namespace filter
}  // namespace mcp
