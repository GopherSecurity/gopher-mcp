/**
 * MCP Stdio Filter Chain Factory Implementation
 * 
 * Simple filter chain for direct transports without protocol stacks
 */

#include "mcp/filter/mcp_stdio_filter_chain_factory.h"
#include "mcp/filter/mcp_jsonrpc_filter.h"

namespace mcp {
namespace filter {

/**
 * Direct callbacks adapter for JSON-RPC to MCP
 * Simply forwards JSON-RPC messages to MCP callbacks
 */
class DirectJsonRpcCallbacks : public McpJsonRpcFilter::Callbacks {
public:
  DirectJsonRpcCallbacks(McpMessageCallbacks& mcp_callbacks)
      : mcp_callbacks_(mcp_callbacks) {}
  
  void onRequest(const jsonrpc::Request& request) override {
    mcp_callbacks_.onRequest(request);
  }
  
  void onNotification(const jsonrpc::Notification& notification) override {
    mcp_callbacks_.onNotification(notification);
  }
  
  void onResponse(const jsonrpc::Response& response) override {
    mcp_callbacks_.onResponse(response);
  }
  
  void onProtocolError(const Error& error) override {
    mcp_callbacks_.onError(error);
  }
  
private:
  McpMessageCallbacks& mcp_callbacks_;
};

bool McpStdioFilterChainFactory::createFilterChain(
    network::FilterManager& filter_manager) const {
  
  // Create callbacks adapter
  auto callbacks = std::make_shared<DirectJsonRpcCallbacks>(message_callbacks_);
  
  // Create JSON-RPC filter (reuse the same filter used in HTTP+SSE stack)
  // This ensures consistent JSON-RPC handling across all transports
  auto jsonrpc_filter = std::make_shared<McpJsonRpcFilter>(
      *callbacks, 
      dispatcher_, 
      false);  // Client mode for stdio (TODO: make configurable)
  
  jsonrpc_filter->setUseFraming(use_framing_);
  
  // Add as both read and write filter
  filter_manager.addReadFilter(jsonrpc_filter);
  filter_manager.addWriteFilter(jsonrpc_filter);
  
  // Store for lifetime management
  filters_.push_back(jsonrpc_filter);
  callbacks_.push_back(callbacks);
  
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