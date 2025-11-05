/**
 * @file unified_filter_chain.h
 * @brief Unified filter chain wrapper for C API implementation
 * @note This is NOT part of the public API - internal use only
 */

#ifndef MCP_C_API_UNIFIED_FILTER_CHAIN_H
#define MCP_C_API_UNIFIED_FILTER_CHAIN_H

#include <memory>
#include <string>
#include <variant>

#include "mcp/c_api/mcp_c_filter_chain.h"
#include "mcp/c_api/mcp_c_filter_api.h"
#include "mcp/filter/filter_service_types.h"
#include "mcp/filter/filter_chain_event_hub.h"

// Forward declarations to avoid circular dependencies
namespace mcp {
namespace filter_chain {
class AdvancedFilterChain;
}
namespace filter_api {
class FilterChain;
}
namespace filter {
class FilterChainCallbacks;
class FilterChainEventHub;
}
}

namespace mcp {
namespace c_api_internal {

/**
 * Unified wrapper for different filter chain implementations.
 * This allows both AdvancedFilterChain (from mcp_c_filter_chain.cc) and 
 * FilterChain (from mcp_c_filter_api.cc) to share the same handle space.
 */
class UnifiedFilterChain {
public:
  enum class ChainType {
    Simple,    // FilterChain from mcp_c_filter_api.cc
    Advanced   // AdvancedFilterChain from mcp_c_filter_chain.cc
  };

  // Constructor for simple chain
  explicit UnifiedFilterChain(std::shared_ptr<mcp::filter_api::FilterChain> simple_chain)
      : type_(ChainType::Simple), simple_chain_(simple_chain) {}

  // Constructor for advanced chain
  explicit UnifiedFilterChain(std::shared_ptr<mcp::filter_chain::AdvancedFilterChain> advanced_chain)
      : type_(ChainType::Advanced), advanced_chain_(advanced_chain) {}

  ChainType getType() const { return type_; }

  // Get the simple chain if this wraps one, nullptr otherwise
  std::shared_ptr<mcp::filter_api::FilterChain> getSimpleChain() const {
    return type_ == ChainType::Simple ? simple_chain_ : nullptr;
  }

  // Get the advanced chain if this wraps one, nullptr otherwise
  std::shared_ptr<mcp::filter_chain::AdvancedFilterChain> getAdvancedChain() const {
    return type_ == ChainType::Advanced ? advanced_chain_ : nullptr;
  }

  // Common operations that can be performed on both types
  // These will be implemented in the .cc file to avoid circular dependencies
  
  /**
   * Pause the chain if supported (only Advanced chains support this)
   * Returns true if the operation was performed, false if not supported
   */
  bool pause();

  /**
   * Resume the chain if supported (only Advanced chains support this)
   * Returns true if the operation was performed, false if not supported
   */
  bool resume();

  /**
   * Get the state of the chain (only Advanced chains have state)
   * Returns MCP_CHAIN_STATE_ERROR for Simple chains
   */
  mcp_chain_state_t getState() const;

  /**
   * Dump the chain configuration as a string
   * Format can be "text", "json", or "dot"
   */
  std::string dump(const std::string& format) const;

  /** Set chain-level event callbacks (advanced chains only). */
  bool setEventCallback(
      std::shared_ptr<mcp::filter::FilterChainCallbacks> callbacks);

  /** Clear chain-level event callbacks (advanced chains only). */
  bool clearEventCallback();

  /** Check if chain-level event callbacks are currently registered. */
  bool hasEventCallback() const;

private:
  ChainType type_;
  std::shared_ptr<mcp::filter_api::FilterChain> simple_chain_;
  std::shared_ptr<mcp::filter_chain::AdvancedFilterChain> advanced_chain_;

  /**
   * Observer handle for chain-level event callbacks.
   * OWNERSHIP: RAII - automatically unregisters observer on destruction
   * LIFETIME: Valid while callbacks should remain registered
   * THREAD SAFETY: Not thread-safe, caller must synchronize
   *
   * This handle MUST be retained to keep the callback registered.
   * When destroyed (or reset), the observer is automatically unregistered.
   */
  mcp::filter::FilterChainEventHub::ObserverHandle observer_handle_;
};

}  // namespace c_api_internal
}  // namespace mcp

#endif  // MCP_C_API_UNIFIED_FILTER_CHAIN_H
