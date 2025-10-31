#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include "circuit_breaker_callbacks.h"

namespace mcp {

// Forward declarations
namespace event {
class Dispatcher;
}
class McpProtocolCallbacks;

namespace filter {

// Forward declarations
class CircuitBreakerFilter;
class CircuitBreakerCallbacks;

// ============================================================================
// CONCRETE SERVICE TYPE DEFINITIONS
// ============================================================================

/**
 * Dispatcher service pointer
 *
 * OWNERSHIP: BORROWED
 * - Owned by AdvancedFilterChain
 * - Valid from injectDependencies() until chain destruction
 * - Filters MUST NOT delete this pointer
 * - Filters MUST NOT access after chain destroyed
 */
using DispatcherService = event::Dispatcher*;

/**
 * Protocol callbacks service pointer
 *
 * OWNERSHIP: BORROWED
 * - Owned by application/caller
 * - Caller MUST ensure callbacks outlive filter chain
 * - Filters MUST NOT delete this pointer
 * - May be nullptr if NullProtocolCallbacks used
 */
using CallbacksService = McpProtocolCallbacks*;

// ============================================================================
// METRICS SINK INTERFACE
// ============================================================================

/**
 * Abstract metrics sink interface
 *
 * OWNERSHIP: SHARED (std::shared_ptr)
 * - Reference counted, automatically managed
 * - Filters can safely hold shared_ptr<MetricsSink>
 * - Lifetime managed by refcount
 */
class MetricsSink {
 public:
  virtual ~MetricsSink() = default;

  /**
   * Increment a counter metric
   * Thread-safe
   */
  virtual void incrementCounter(const std::string& name, uint64_t value = 1) = 0;

  /**
   * Record a gauge value
   * Thread-safe
   */
  virtual void recordGauge(const std::string& name, double value) = 0;

  /**
   * Record a histogram sample
   * Thread-safe
   */
  virtual void recordHistogram(const std::string& name, double value) = 0;

  /**
   * Record timing in microseconds
   * Thread-safe
   */
  virtual void recordTiming(const std::string& name,
                           std::chrono::microseconds duration) = 0;
};

using MetricsService = std::shared_ptr<MetricsSink>;

// ============================================================================
// CIRCUIT BREAKER STATE
// ============================================================================

/**
 * Shared circuit breaker state
 *
 * OWNERSHIP: SHARED (std::shared_ptr)
 * - Reference counted, automatically managed
 * - Thread-safe via atomics
 * - Multiple filters can share same circuit breaker state
 */
struct CircuitBreakerState {
  /// Is circuit currently open (tripped)?
  std::atomic<bool> circuit_open{false};

  /// Current consecutive failure count
  std::atomic<uint64_t> failure_count{0};

  /// Timestamp of last failure
  std::chrono::steady_clock::time_point last_failure;

  /// Timestamp when circuit was opened
  std::chrono::steady_clock::time_point circuit_opened_at;

  /// Total requests processed
  std::atomic<uint64_t> total_requests{0};

  /// Total failures
  std::atomic<uint64_t> total_failures{0};
};

using CircuitBreakerService = std::shared_ptr<CircuitBreakerState>;

// ============================================================================
// RUNTIME SERVICES CONTAINER
// ============================================================================

/**
 * Container for all runtime services
 *
 * Used with FilterCreationContext::shared_services
 * Provides type-safe access to all injectable services
 */
struct RuntimeServices {
  /// BORROWED: Dispatcher pointer (see DispatcherService docs)
  DispatcherService dispatcher = nullptr;

  /// BORROWED: Callbacks pointer (see CallbacksService docs)
  CallbacksService callbacks = nullptr;

  /// SHARED: Metrics sink (see MetricsService docs)
  MetricsService metrics;

  /// SHARED: Circuit breaker state (see CircuitBreakerService docs)
  CircuitBreakerService circuit_breaker;

  /// SHARED: Circuit breaker callbacks for state change notifications
  /// - Reference counted, automatically managed
  /// - Used by CircuitBreakerFilter to notify application of events
  /// - Can be nullptr if no callbacks registered
  std::shared_ptr<CircuitBreakerCallbacks> circuit_breaker_callbacks;

  /**
   * Helper: Cast shared_services from FilterCreationContext
   *
   * Usage:
   *   auto services = RuntimeServices::fromSharedServices(context.shared_services);
   *   if (services && services->dispatcher) { ... }
   */
  static std::shared_ptr<RuntimeServices> fromSharedServices(
      const std::shared_ptr<void>& shared_services) {
    return std::static_pointer_cast<RuntimeServices>(shared_services);
  }
};

}  // namespace filter
}  // namespace mcp
