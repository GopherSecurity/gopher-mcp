/**
 * @file circuit_breaker_callbacks.h
 * @brief Shared circuit breaker callback interface and state definitions.
 */

#pragma once

#include <cstdint>
#include <string>

namespace mcp {
namespace filter {

/**
 * Circuit breaker states used across callbacks and filters.
 */
enum class CircuitState {
  CLOSED,
  OPEN,
  HALF_OPEN
};

/**
 * Callback interface for circuit breaker events.
 */
class CircuitBreakerCallbacks {
 public:
  virtual ~CircuitBreakerCallbacks() = default;

  virtual void onStateChange(CircuitState old_state,
                             CircuitState new_state,
                             const std::string& reason) = 0;

  virtual void onRequestBlocked(const std::string& method) = 0;

  virtual void onHealthUpdate(double success_rate,
                              uint64_t latency_ms) = 0;
};

}  // namespace filter
}  // namespace mcp

