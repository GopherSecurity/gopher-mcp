/**
 * @file circuit_breaker_factory.cc
 * @brief Factory implementation for circuit breaker filter
 */

#include "mcp/filter/filter_registry.h"
#include "mcp/filter/circuit_breaker_filter.h"
#include "mcp/filter/filter_context.h"
#include "mcp/filter/filter_event_emitter.h"
#include "mcp/filter/filter_chain_event_hub.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"

#define GOPHER_LOG_COMPONENT "filter.factory.circuit_breaker"

namespace mcp {
namespace filter {

/**
 * Context-aware factory for creating CircuitBreakerFilter instances
 *
 * Configuration schema:
 * {
 *   "failure_threshold": number,          // Consecutive failures to open (default: 5)
 *   "error_rate_threshold": number,       // Error rate to open (0.0-1.0, default: 0.5)
 *   "min_requests": number,               // Min requests before checking error rate (default: 10)
 *   "timeout_ms": number,                 // Time before trying half-open (default: 30000)
 *   "window_size_ms": number,             // Sliding window for metrics (default: 60000)
 *   "half_open_max_requests": number,     // Max requests in half-open (default: 3)
 *   "half_open_success_threshold": number, // Successes to close circuit (default: 2)
 *   "track_timeouts": boolean,            // Track timeouts as failures (default: true)
 *   "track_errors": boolean,              // Track errors as failures (default: true)
 *   "track_4xx_as_errors": boolean        // Count client errors as failures (default: false)
 * }
 */
network::FilterSharedPtr createCircuitBreakerFilter(
    const FilterCreationContext& context,
    const json::JsonValue& config) {

  GOPHER_LOG(Info, "Creating CircuitBreakerFilter instance");

  // Extract configuration values with defaults
  CircuitBreakerConfig cb_config;

  if (config.isObject()) {
    if (config.contains("failure_threshold")) {
      cb_config.failure_threshold = static_cast<size_t>(
          config["failure_threshold"].getInt(5));
    }

    if (config.contains("error_rate_threshold")) {
      cb_config.error_rate_threshold =
          config["error_rate_threshold"].getFloat(0.5);
    }

    if (config.contains("min_requests")) {
      cb_config.min_requests = static_cast<size_t>(
          config["min_requests"].getInt(10));
    }

    if (config.contains("timeout_ms")) {
      cb_config.timeout = std::chrono::milliseconds(
          config["timeout_ms"].getInt(30000));
    }

    if (config.contains("window_size_ms")) {
      cb_config.window_size = std::chrono::milliseconds(
          config["window_size_ms"].getInt(60000));
    }

    if (config.contains("half_open_max_requests")) {
      cb_config.half_open_max_requests = static_cast<size_t>(
          config["half_open_max_requests"].getInt(3));
    }

    if (config.contains("half_open_success_threshold")) {
      cb_config.half_open_success_threshold = static_cast<size_t>(
          config["half_open_success_threshold"].getInt(2));
    }

    if (config.contains("track_timeouts")) {
      cb_config.track_timeouts = config["track_timeouts"].getBool(true);
    }

    if (config.contains("track_errors")) {
      cb_config.track_errors = config["track_errors"].getBool(true);
    }

    if (config.contains("track_4xx_as_errors")) {
      cb_config.track_4xx_as_errors =
          config["track_4xx_as_errors"].getBool(false);
    }
  }

  // Log configuration
  GOPHER_LOG(Debug, "CircuitBreakerFilter config: failure_threshold=%zu error_rate=%.2f "
             "min_requests=%zu timeout=%ldms window=%ldms half_open_max=%zu half_open_success=%zu",
             cb_config.failure_threshold, cb_config.error_rate_threshold,
             cb_config.min_requests,
             static_cast<long>(cb_config.timeout.count()),
             static_cast<long>(cb_config.window_size.count()),
             cb_config.half_open_max_requests, cb_config.half_open_success_threshold);

  // Validate logical consistency
  if (cb_config.half_open_success_threshold > cb_config.half_open_max_requests) {
    GOPHER_LOG(Warning, "half_open_success_threshold (%zu) > half_open_max_requests (%zu) - circuit may never close",
               cb_config.half_open_success_threshold, cb_config.half_open_max_requests);
  }

  // Get callbacks from context, or use default stub callbacks
  // Create event emitter from event_hub if available
  std::shared_ptr<FilterEventEmitter> emitter_ptr;

  if (context.event_hub) {
    // Cast event_hub from void* to FilterChainEventHub
    auto hub = std::static_pointer_cast<FilterChainEventHub>(context.event_hub);
    if (hub) {
      GOPHER_LOG(Info, "Creating circuit breaker event emitter from event hub");
      emitter_ptr = std::make_shared<FilterEventEmitter>(
          hub, "circuit_breaker", "", "");
    }
  }

  if (!emitter_ptr) {
    GOPHER_LOG(Info, "No event hub available - circuit breaker events will not be emitted");
  }

  auto filter = std::make_shared<CircuitBreakerFilter>(emitter_ptr, cb_config);

  GOPHER_LOG(Info, "Created circuit_breaker filter successfully");

  return filter;
}


/**
 * Register circuit breaker filter factory
 */
void registerCircuitBreakerFilterFactory() {
  auto& registry = FilterRegistry::instance();

  // Define metadata
  BasicFilterMetadata metadata;
  metadata.name = "circuit_breaker";
  metadata.version = "1.0.0";
  metadata.description =
      "Circuit breaker filter for cascading failure protection";

  // Define default configuration
  json::JsonObjectBuilder defaults;
  defaults.add("failure_threshold", 5)
      .add("error_rate_threshold", 0.5)
      .add("min_requests", 10)
      .add("timeout_ms", 30000)
      .add("window_size_ms", 60000)
      .add("half_open_max_requests", 3)
      .add("half_open_success_threshold", 2)
      .add("track_timeouts", true)
      .add("track_errors", true)
      .add("track_4xx_as_errors", false);
  metadata.default_config = defaults.build();

  // Register context-aware factory
  registry.registerContextFactory("circuit_breaker",
                                  createCircuitBreakerFilter, metadata);

  GOPHER_LOG(Info, "Registered circuit_breaker filter factory (context-aware)");
}

namespace {
struct CircuitBreakerRegistrar {
  CircuitBreakerRegistrar() { registerCircuitBreakerFilterFactory(); }
};

static CircuitBreakerRegistrar circuit_breaker_registrar;
}  // namespace

// Export for static linking - using magic number as sentinel value
extern "C" {
void* circuit_breaker_filter_registrar_ref =
    reinterpret_cast<void*>(0xDEADBEEF);
}

}  // namespace filter
}  // namespace mcp
