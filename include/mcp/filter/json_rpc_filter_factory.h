#pragma once

#include "mcp/mcp_connection_manager.h"
#include "mcp/network/filter.h"

namespace mcp {
namespace filter {

/**
 * Factory for creating JSON-RPC filter chains
 * 
 * This is a reusable component that creates filter chains for processing
 * JSON-RPC messages. It can be used by both client and server connections.
 * 
 * Flow: Transport delivers data → Filter parses JSON-RPC → Callbacks handle messages
 */
class JsonRpcFilterChainFactory : public network::FilterChainFactory {
 public:
  /**
   * Constructor
   * @param callbacks Message callbacks for handling parsed JSON-RPC messages
   * @param use_framing Whether to use message framing (false for HTTP transport)
   */
  JsonRpcFilterChainFactory(McpMessageCallbacks& callbacks, bool use_framing = true)
      : callbacks_(callbacks), use_framing_(use_framing) {}

  /**
   * Create the filter chain
   * Adds JsonRpcMessageFilter for both read and write paths
   */
  bool createFilterChain(network::FilterManager& manager) const override {
    // Create the JSON-RPC message filter
    // This filter parses incoming JSON-RPC and frames outgoing messages
    auto filter = std::make_shared<JsonRpcMessageFilter>(callbacks_);
    filter->setUseFraming(use_framing_);
    
    // Add to both read and write filter chains
    // Read: Parse incoming JSON-RPC messages
    // Write: Frame outgoing JSON-RPC responses (if framing enabled)
    manager.addReadFilter(filter);
    manager.addWriteFilter(filter);
    
    return true;
  }
  
  /**
   * Create network filter chain (not used for JSON-RPC)
   */
  bool createNetworkFilterChain(
      network::FilterManager& filter_manager,
      const std::vector<network::FilterFactoryCb>& factories) const override {
    // Not used for JSON-RPC processing
    return true;
  }
  
  /**
   * Create listener filter chain (not used for JSON-RPC)
   */
  bool createListenerFilterChain(network::FilterManager& filter_manager) const override {
    // Not used for JSON-RPC processing
    return true;
  }

 private:
  McpMessageCallbacks& callbacks_;
  bool use_framing_;
};

}  // namespace filter
}  // namespace mcp