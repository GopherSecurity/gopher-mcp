/**
 * MCP Protocol Detection Filter Chain Factory
 * 
 * Creates a filter chain that automatically detects the protocol
 * (HTTP or native MCP JSON-RPC) and installs appropriate filters.
 */

#pragma once

#include <memory>

#include "mcp/network/filter.h"
#include "mcp/filter/protocol_detection_filter.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/event/event_loop.h"

namespace mcp {

// Forward declaration
class McpProtocolCallbacks;
namespace filter {

/**
 * Filter chain factory that uses protocol detection
 * 
 * This factory creates a filter chain that:
 * 1. Detects the protocol (HTTP vs native MCP)
 * 2. Installs appropriate filters based on detection
 * 3. Handles both protocols transparently
 */
class ProtocolDetectionFilterChainFactory : public network::FilterChainFactory {
public:
  /**
   * Constructor
   * @param dispatcher Event dispatcher for async operations
   * @param callbacks MCP message callbacks
   * @param is_server True for server mode, false for client
   * @param enable_http Enable HTTP protocol detection
   * @param enable_native_mcp Enable native MCP detection
   */
  ProtocolDetectionFilterChainFactory(event::Dispatcher& dispatcher,
                                         McpProtocolCallbacks& callbacks,
                                         bool is_server,
                                         bool enable_http = true,
                                         bool enable_native_mcp = true);
  
  ~ProtocolDetectionFilterChainFactory() override = default;
  
  // FilterChainFactory interface
  bool createFilterChain(network::FilterManager& filter_manager) const override;
  
  bool createNetworkFilterChain(
      network::FilterManager& filter_manager,
      const std::vector<network::FilterFactoryCb>& factories) const override;
  
  bool createListenerFilterChain(
      network::FilterManager& filter_manager) const override;
  
private:
  event::Dispatcher& dispatcher_;
  McpProtocolCallbacks& callbacks_;
  bool is_server_;
  bool enable_http_;
  bool enable_native_mcp_;
  
  /**
   * Create protocol detection filter
   */
  network::FilterSharedPtr createProtocolDetectionFilter() const;
  
  /**
   * Handle protocol detection result
   */
  void onProtocolDetected(network::FilterManager& filter_manager,
                          DetectedProtocol protocol,
                          const ProtocolDetectionResult& result) const;
};

} // namespace filter
} // namespace mcp