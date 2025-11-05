/**
 * @file mcp_c_filter_events.h
 * @brief C API for unified chain-level filter events
 *
 * This header provides FFI-safe C API for registering chain-level callbacks
 * that receive events from all filters in a filter chain.
 */

#ifndef MCP_C_FILTER_EVENTS_H
#define MCP_C_FILTER_EVENTS_H

#include "mcp_c_filter_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Event Type and Severity Enumerations
 * ============================================================================
 */

/**
 * @brief Filter event types
 *
 * Matches the C++ FilterEventType enum values.
 */
typedef enum {
  // Circuit breaker events
  MCP_FILTER_EVENT_CIRCUIT_STATE_CHANGE = 0,
  MCP_FILTER_EVENT_CIRCUIT_REQUEST_BLOCKED = 1,
  MCP_FILTER_EVENT_CIRCUIT_HEALTH_UPDATE = 2,

  // Rate limiter events
  MCP_FILTER_EVENT_RATE_LIMIT_EXCEEDED = 3,
  MCP_FILTER_EVENT_RATE_LIMIT_RESET = 4,
  MCP_FILTER_EVENT_RATE_LIMIT_SAMPLE = 5,
  MCP_FILTER_EVENT_RATE_LIMIT_WINDOW_RESET = 6,
  MCP_FILTER_EVENT_RATE_LIMIT_CONFIGURATION_SYNCED = 7,

  // Metrics events
  MCP_FILTER_EVENT_METRIC_UPDATE = 50,
  MCP_FILTER_EVENT_METRIC_FLUSH = 51,

  // Request logger events
  MCP_FILTER_EVENT_REQUEST_LOGGED = 60,
  MCP_FILTER_EVENT_RESPONSE_LOGGED = 61,

  // Protocol events
  MCP_FILTER_EVENT_PROTOCOL_ERROR = 70,
  MCP_FILTER_EVENT_PROTOCOL_UPGRADE = 71,

  // Generic filter events
  MCP_FILTER_EVENT_FILTER_INITIALIZED = 80,
  MCP_FILTER_EVENT_FILTER_DESTROYED = 81,
  MCP_FILTER_EVENT_FILTER_ERROR = 100,
  MCP_FILTER_EVENT_FILTER_WARNING = 101,
  MCP_FILTER_EVENT_FILTER_INFO = 102
} mcp_filter_event_type_t;

/**
 * @brief Event severity levels
 */
typedef enum {
  MCP_FILTER_EVENT_SEVERITY_TRACE = 0,
  MCP_FILTER_EVENT_SEVERITY_DEBUG = 1,
  MCP_FILTER_EVENT_SEVERITY_INFO = 2,
  MCP_FILTER_EVENT_SEVERITY_WARN = 3,
  MCP_FILTER_EVENT_SEVERITY_ERROR = 4,
  MCP_FILTER_EVENT_SEVERITY_CRITICAL = 5
} mcp_filter_event_severity_t;

/* ============================================================================
 * Event Context and Event Structures
 * ============================================================================
 */

/**
 * @brief Event context metadata
 */
typedef struct mcp_filter_event_context {
  const char* chain_id;          ///< Chain identifier (may be NULL)
  const char* stream_id;         ///< Stream/session identifier (may be NULL)
  const char* correlation_id;    ///< Correlation identifier (may be NULL)
} mcp_filter_event_context_t;

/* ============================================================================
 * Callback Type
 * ============================================================================
 */

/**
 * @brief Chain-level filter event callback
 *
 * This callback receives all events from all filters in the chain.
 *
 * @param filter_name Name of the filter that emitted the event (null-terminated)
 * @param filter_instance_id Instance ID of the filter (may be NULL)
 * @param event_type Type of the event
 * @param severity Severity level of the event
 * @param event_data_json Event-specific data as JSON string (null-terminated)
 * @param context Event context metadata (may be NULL)
 * @param timestamp_ms Unix timestamp in milliseconds
 * @param user_data User-provided context passed during registration
 *
 * @note This callback is invoked synchronously in the dispatcher thread.
 *       Implementations must not block or perform long-running operations.
 */
typedef void (*mcp_filter_event_callback_t)(
    const char* filter_name,
    const char* filter_instance_id,
    mcp_filter_event_type_t event_type,
    mcp_filter_event_severity_t severity,
    const char* event_data_json,
    const mcp_filter_event_context_t* context,
    int64_t timestamp_ms,
    void* user_data);

/* ============================================================================
 * Chain Event Callback Management
 * ============================================================================
 */

/**
 * @brief Set chain-level event callback
 *
 * Registers a unified callback that receives events from all filters
 * in the chain. Replaces any previously registered callback.
 *
 * @param chain Filter chain handle
 * @param callback Event callback function
 * @param user_data User context passed to callback
 * @return 0 on success, negative error code on failure
 *         -1: Invalid chain handle
 *         -2: Callback registration failed
 */
MCP_API int mcp_filter_chain_set_event_callback(
    mcp_filter_chain_t chain,
    mcp_filter_event_callback_t callback,
    void* user_data) MCP_NOEXCEPT;

/**
 * @brief Clear chain-level event callback
 *
 * Removes the currently registered event callback.
 *
 * @param chain Filter chain handle
 * @return 0 on success, negative error code on failure
 *         -1: Invalid chain handle
 */
MCP_API int mcp_filter_chain_clear_event_callback(
    mcp_filter_chain_t chain) MCP_NOEXCEPT;

/* ============================================================================
 * Utility Functions
 * ============================================================================
 */

/**
 * @brief Get string representation of event type
 *
 * @param event_type Event type enum value
 * @return String name of the event type (statically allocated, do not free)
 */
MCP_API const char* mcp_filter_event_type_to_string(
    mcp_filter_event_type_t event_type) MCP_NOEXCEPT;

/**
 * @brief Get string representation of event severity
 *
 * @param severity Severity enum value
 * @return String name of the severity (statically allocated, do not free)
 */
MCP_API const char* mcp_filter_event_severity_to_string(
    mcp_filter_event_severity_t severity) MCP_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* MCP_C_FILTER_EVENTS_H */
