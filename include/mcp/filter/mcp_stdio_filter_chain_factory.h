#pragma once

#include "mcp/event/event_loop.h"
#include "mcp/network/connection.h"
#include "mcp/network/filter.h"

// Need full definition of McpMessageCallbacks
#include "mcp/mcp_connection_manager.h"

namespace mcp {
namespace filter {

/**
 * MCP Stdio Filter Chain Factory
 *
 * Creates filter chain for direct transports (stdio, websocket)
 * that don't require HTTP/SSE protocol layers.
 *
 * Filter Chain Architecture:
 * ```
 * [Transport Socket] → [JSON-RPC Filter] → [Application]
 * ```
 *
 * This is much simpler than HTTP+SSE as there's no protocol stack,
 * just direct JSON-RPC message handling.
 */
class McpStdioFilterChainFactory : public network::FilterChainFactory {
 public:
  /**
   * Constructor
   * @param dispatcher Event dispatcher for async operations
   * @param message_callbacks MCP message callbacks for handling messages
   * @param is_server True for server mode, false for client mode
   * @param use_framing Whether to use length-prefixed framing
   */
  McpStdioFilterChainFactory(event::Dispatcher& dispatcher,
                             McpMessageCallbacks& message_callbacks,
                             bool is_server,
                             bool use_framing = true)
      : dispatcher_(dispatcher),
        message_callbacks_(message_callbacks),
        is_server_(is_server),
        use_framing_(use_framing) {}

  /**
   * Create filter chain for the connection
   * @param filter_manager The filter manager to add filters to
   * @return true if filter chain was created successfully
   */
  bool createFilterChain(network::FilterManager& filter_manager) const override;

  /**
   * Create network filter chain with additional filters
   */
  bool createNetworkFilterChain(network::FilterManager& filter_manager,
                                const std::vector<network::FilterFactoryCb>&
                                    filter_factories) const override;

  /**
   * Create listener filter chain (not used for stdio)
   */
  bool createListenerFilterChain(
      network::FilterManager& filter_manager) const override {
    return false;
  }

 private:
  event::Dispatcher& dispatcher_;
  McpMessageCallbacks& message_callbacks_;
  bool is_server_;
  bool use_framing_;
};

}  // namespace filter
}  // namespace mcp