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
    return hub->registerObserver(callbacks);
  }
  return mcp::filter::FilterChainEventHub::ObserverHandle();
}

}  // namespace filter_chain
}  // namespace mcp
