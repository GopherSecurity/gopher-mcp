/**
 * @file mcp_c_filter_callbacks.cc
 * @brief Implementation of circuit breaker filter callbacks C API
 */

#include "mcp/c_api/mcp_c_filter_callbacks.h"
#include "mcp/c_api/mcp_c_filter_chain.h"

#include <memory>
#include <string>

#include "mcp/filter/circuit_breaker_callbacks.h"
#include "mcp/filter/filter_context.h"
#include "mcp/logging/log_macros.h"

#include "handle_manager.h"
#include "unified_filter_chain.h"

#undef GOPHER_LOG_COMPONENT
#define GOPHER_LOG_COMPONENT "capi.callbacks"

namespace mcp {
namespace c_api_internal {

// Forward declare the handle manager from mcp_c_filter_chain.cc
extern HandleManager<UnifiedFilterChain> g_unified_chain_manager;

}  // namespace c_api_internal

namespace filter_chain {

// Alias for convenience
using c_api_internal::g_unified_chain_manager;
#define g_chain_manager g_unified_chain_manager

// Forward declare the AdvancedFilterChain class
class AdvancedFilterChain;

// Forward declare helper functions from mcp_c_filter_chain.cc
extern void advancedChainSetCircuitBreakerCallbacks(
    AdvancedFilterChain* chain,
    std::shared_ptr<void> callbacks);

extern void advancedChainClearCircuitBreakerCallbacks(
    AdvancedFilterChain* chain);

extern std::shared_ptr<filter::RuntimeServices> advancedChainGetRuntimeServices(
    AdvancedFilterChain* chain);

}  // namespace filter_chain
}  // namespace mcp

namespace {

using namespace mcp;
using namespace mcp::filter;
using namespace mcp::filter_chain;

/**
 * @brief C API callback bridge to C++ CircuitBreakerFilter::Callbacks
 *
 * This class bridges C function pointers to C++ virtual interface.
 */
class CircuitBreakerCallbackBridge : public filter::CircuitBreakerCallbacks {
 public:
  explicit CircuitBreakerCallbackBridge(const mcp_circuit_breaker_callbacks_t& callbacks)
      : callbacks_(callbacks) {
    GOPHER_LOG(Debug, "Created CircuitBreakerCallbackBridge");
  }

  ~CircuitBreakerCallbackBridge() override {
    GOPHER_LOG(Debug, "Destroyed CircuitBreakerCallbackBridge");
  }

  void onStateChange(CircuitState old_state, CircuitState new_state,
                     const std::string& reason) override {
    if (callbacks_.on_state_change) {
      try {
        callbacks_.on_state_change(
            static_cast<mcp_circuit_state_t>(old_state),
            static_cast<mcp_circuit_state_t>(new_state),
            reason.c_str(),
            callbacks_.user_data);
      } catch (const std::exception& e) {
        GOPHER_LOG(Error, "Exception in circuit breaker state change callback: %s",
                   e.what());
      } catch (...) {
        GOPHER_LOG(Error, "Unknown exception in circuit breaker state change callback");
      }
    }
  }

  void onRequestBlocked(const std::string& method) override {
    if (callbacks_.on_request_blocked) {
      try {
        callbacks_.on_request_blocked(method.c_str(), callbacks_.user_data);
      } catch (const std::exception& e) {
        GOPHER_LOG(Error, "Exception in circuit breaker request blocked callback: %s",
                   e.what());
      } catch (...) {
        GOPHER_LOG(Error, "Unknown exception in circuit breaker request blocked callback");
      }
    }
  }

  void onHealthUpdate(double success_rate, uint64_t latency_ms) override {
    if (callbacks_.on_health_update) {
      try {
        callbacks_.on_health_update(success_rate, latency_ms, callbacks_.user_data);
      } catch (const std::exception& e) {
        GOPHER_LOG(Error, "Exception in circuit breaker health update callback: %s",
                   e.what());
      } catch (...) {
        GOPHER_LOG(Error, "Unknown exception in circuit breaker health update callback");
      }
    }
  }

 private:
  mcp_circuit_breaker_callbacks_t callbacks_;
};

}  // anonymous namespace

extern "C" {

int mcp_filter_chain_set_circuit_breaker_callbacks(
    mcp_filter_chain_t chain,
    const mcp_circuit_breaker_callbacks_t* callbacks) {

  if (!chain) {
    GOPHER_LOG(Error, "Invalid chain handle (NULL)");
    return -1;
  }

  if (!callbacks) {
    GOPHER_LOG(Error, "Invalid callbacks pointer (NULL)");
    return -1;
  }

  // Get the unified chain from the handle manager
  auto unified_chain = g_chain_manager.get(chain);
  if (!unified_chain) {
    GOPHER_LOG(Error, "Invalid chain handle (not found in manager)");
    return -1;
  }

  // Only advanced chains support callback registration
  try {
    // Create callback bridge that wraps C callbacks as C++ interface
    auto callback_bridge = std::make_shared<CircuitBreakerCallbackBridge>(*callbacks);

    if (!unified_chain->setCircuitBreakerCallbacks(callback_bridge)) {
      GOPHER_LOG(Warning, "Chain does not support circuit breaker callbacks");
      return -3;
    }

    GOPHER_LOG(Info, "Circuit breaker callbacks registered successfully");
    return 0;

  } catch (const std::exception& e) {
    GOPHER_LOG(Error, "Failed to register circuit breaker callbacks: %s", e.what());
    return -3;
  }
}

int mcp_filter_chain_clear_circuit_breaker_callbacks(
    mcp_filter_chain_t chain) {

  if (!chain) {
    GOPHER_LOG(Error, "Invalid chain handle (NULL)");
    return -1;
  }

  auto unified_chain = g_chain_manager.get(chain);
  if (!unified_chain) {
    GOPHER_LOG(Error, "Invalid chain handle (not found in manager)");
    return -1;
  }

  try {
    if (!unified_chain->clearCircuitBreakerCallbacks()) {
      GOPHER_LOG(Warning, "Chain does not support circuit breaker callbacks");
      return -3;
    }

    GOPHER_LOG(Info, "Circuit breaker callbacks cleared successfully");
    return 0;

  } catch (const std::exception& e) {
    GOPHER_LOG(Error, "Failed to clear circuit breaker callbacks: %s", e.what());
    return -3;
  }
}

int mcp_filter_chain_has_circuit_breaker_callbacks(
    mcp_filter_chain_t chain) {

  if (!chain) {
    GOPHER_LOG(Error, "Invalid chain handle (NULL)");
    return -1;
  }

  auto unified_chain = g_chain_manager.get(chain);
  if (!unified_chain) {
    GOPHER_LOG(Error, "Invalid chain handle (not found in manager)");
    return -1;
  }

  return unified_chain->hasCircuitBreakerCallbacks() ? 1 : 0;
}

const char* mcp_circuit_state_to_string(mcp_circuit_state_t state) {
  switch (state) {
    case MCP_CIRCUIT_STATE_CLOSED:
      return "CLOSED";
    case MCP_CIRCUIT_STATE_OPEN:
      return "OPEN";
    case MCP_CIRCUIT_STATE_HALF_OPEN:
      return "HALF_OPEN";
    default:
      return "UNKNOWN";
  }
}

}  // extern "C"
