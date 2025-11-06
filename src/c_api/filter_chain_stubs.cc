/**
 * @file filter_chain_stubs.cc
 * @brief Chain-level event callback implementations
 *
 * NOTE: This file was originally created as stubs for Phase 1.
 * It has now been updated with real implementations for Phase 2.
 *
 * IMPORTANT: This file needs to include the full AdvancedFilterChain definition
 * to access the public event_hub_ member. However, that would create circular
 * dependencies, so instead we forward declare and access via an extern getter.
 */

#include <memory>

// Include the event system headers
#include "mcp/filter/filter_chain_callbacks.h"
#include "mcp/filter/filter_chain_event_hub.h"

namespace mcp {
namespace filter_chain {

// Forward declare AdvancedFilterChain
// The actual class definition is in mcp_c_filter_chain.cc
class AdvancedFilterChain;

// Helper functions to access event_hub_ member
// Implemented in mcp_c_filter_chain.cc where AdvancedFilterChain is defined
namespace internal {
std::shared_ptr<filter::FilterChainEventHub> getEventHub(AdvancedFilterChain& chain);
std::shared_ptr<filter::FilterChainEventHub> getEventHub(const AdvancedFilterChain& chain);
}  // namespace internal

// Real implementations for Phase 2
mcp::filter::FilterChainEventHub::ObserverHandle
advanced_chain_set_event_callback(
    AdvancedFilterChain& chain,
    std::shared_ptr<mcp::filter::FilterChainCallbacks> callbacks) {
  auto hub = internal::getEventHub(chain);
  if (hub && callbacks) {
    // Register the callback observer and return the handle
    // The caller MUST store this handle to keep the callback registered
    return hub->registerObserver(callbacks);
  }
  // Return empty handle if registration failed
  return mcp::filter::FilterChainEventHub::ObserverHandle();
}

void advanced_chain_unregister_observer(AdvancedFilterChain& chain,
                                         size_t observer_id) {
  auto hub = internal::getEventHub(chain);
  if (hub) {
    // Unregister only the specific observer by ID
    hub->unregisterObserver(observer_id);
  }
}

bool advanced_chain_has_event_callback(const AdvancedFilterChain& chain) {
  auto hub = internal::getEventHub(chain);
  // Check if hub exists and has registered observers
  return hub && hub->getObserverCount() > 0;
}

}  // namespace filter_chain
}  // namespace mcp
