/**
 * MCP Stdio Filter Chain Factory Implementation
 * 
 * Simple filter chain for direct transports without protocol stacks
 */

#include "mcp/filter/mcp_stdio_filter_chain_factory.h"
#include "mcp/mcp_connection_manager.h"

namespace mcp {
namespace filter {

bool McpStdioFilterChainFactory::createFilterChain(
    network::FilterManager& filter_manager) const {
  
  // Create simple JSON-RPC filter
  // No protocol stack needed for direct transports
  auto jsonrpc_filter = std::make_shared<JsonRpcMessageFilter>(message_callbacks_);
  jsonrpc_filter->setUseFraming(use_framing_);
  
  // Add as both read and write filter
  filter_manager.addReadFilter(jsonrpc_filter);
  filter_manager.addWriteFilter(jsonrpc_filter);
  
  // Store for lifetime management
  filters_.push_back(jsonrpc_filter);
  
  return true;
}

bool McpStdioFilterChainFactory::createNetworkFilterChain(
    network::FilterManager& filter_manager,
    const std::vector<network::FilterFactoryCb>& filter_factories) const {
  
  // Apply any additional filter factories first
  for (const auto& factory : filter_factories) {
    auto filter = factory();
    if (filter) {
      filter_manager.addReadFilter(filter);
      filter_manager.addWriteFilter(filter);
    }
  }
  
  // Then create our filter
  return createFilterChain(filter_manager);
}

} // namespace filter
} // namespace mcp