/**
 * MCP Protocol Detection Filter Chain Factory Implementation
 */

#include "mcp/filter/mcp_protocol_detection_filter_chain_factory.h"
#include "mcp/filter/mcp_http_filter_chain_factory.h"
#include "mcp/filter/mcp_stdio_filter_chain_factory.h"
#include "mcp/mcp_connection_manager.h"  // For McpProtocolCallbacks

namespace mcp {
namespace filter {

// Adapter for McpProtocolCallbacks to JsonRpcProtocolFilter::Callbacks
class ProtocolDetectionJsonRpcCallbacks : public JsonRpcProtocolFilter::Callbacks {
public:
  ProtocolDetectionJsonRpcCallbacks(McpProtocolCallbacks& mcp_callbacks)
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
  McpProtocolCallbacks& mcp_callbacks_;
};

McpProtocolDetectionFilterChainFactory::McpProtocolDetectionFilterChainFactory(
    event::Dispatcher& dispatcher,
    McpProtocolCallbacks& callbacks,
    bool is_server,
    bool enable_http,
    bool enable_native_mcp)
    : dispatcher_(dispatcher),
      callbacks_(callbacks),
      is_server_(is_server),
      enable_http_(enable_http),
      enable_native_mcp_(enable_native_mcp) {}

bool McpProtocolDetectionFilterChainFactory::createFilterChain(
    network::FilterManager& filter_manager) const {
  
  // For now, we'll create a simplified approach:
  // 1. Add protocol detection filter that buffers initial data
  // 2. Based on transport config, add appropriate filters
  // 
  // In a production system, the protocol detection filter would
  // dynamically install filters, but that requires FilterManager changes
  
  // For client mode, we typically know the protocol from the URI
  // But we'll add detection for flexibility
  
  if (!is_server_) {
    // Client mode - add protocol detection followed by both filter types
    // The detection filter will enable the appropriate path
    
    // Add protocol detection filter
    auto detection_filter = std::make_shared<ProtocolDetectionFilter>(
        [this](DetectedProtocol protocol, const ProtocolDetectionResult& result) {
          // Log detection result
          // In production, this would trigger dynamic filter installation
        });
    filter_manager.addReadFilter(detection_filter);
    filter_manager.addWriteFilter(detection_filter);
  }
  
  // For now, add the JSON-RPC filter as the default
  // In production, this would be added dynamically after detection
  // Create callbacks adapter that outlives the filter
  auto adapter = std::make_shared<ProtocolDetectionJsonRpcCallbacks>(callbacks_);
  auto jsonrpc_filter = std::make_shared<JsonRpcProtocolFilter>(
      *adapter, dispatcher_, is_server_);
  
  // Keep the adapter alive with the filter
  filter_manager.addReadFilter(jsonrpc_filter);
  filter_manager.addWriteFilter(jsonrpc_filter);
  
  return true;
}

network::FilterSharedPtr 
McpProtocolDetectionFilterChainFactory::createProtocolDetectionFilter() const {
  // Create protocol detection filter
  auto filter = std::make_shared<ProtocolDetectionFilter>(
      [this](DetectedProtocol protocol, const ProtocolDetectionResult& result) {
        // Log protocol detection
        // This demonstrates where protocol-specific handling would occur
      });
  
  return filter;
}

bool McpProtocolDetectionFilterChainFactory::createNetworkFilterChain(
    network::FilterManager& filter_manager,
    const std::vector<network::FilterFactoryCb>& factories) const {
  // Apply any additional filter factories first
  for (const auto& factory : factories) {
    auto filter = factory();
    if (filter) {
      filter_manager.addReadFilter(filter);
      filter_manager.addWriteFilter(filter);
    }
  }
  
  // Then create our filter chain
  return createFilterChain(filter_manager);
}

bool McpProtocolDetectionFilterChainFactory::createListenerFilterChain(
    network::FilterManager& filter_manager) const {
  // Server-side listener filter chain
  // For now, just delegate to createFilterChain
  return createFilterChain(filter_manager);
}

void McpProtocolDetectionFilterChainFactory::onProtocolDetected(
    network::FilterManager& filter_manager,
    DetectedProtocol protocol,
    const ProtocolDetectionResult& result) const {
  
  // Based on detected protocol, create appropriate filter chain
  switch (protocol) {
    case DetectedProtocol::HTTP:
    case DetectedProtocol::SSE: {
      if (enable_http_) {
        // Create HTTP/SSE filter chain
        auto http_factory = std::make_shared<McpHttpFilterChainFactory>(
            dispatcher_, callbacks_, is_server_);
        
        // Add HTTP filters after detection filter
        // Note: We would need to enhance FilterManager to support
        // dynamic filter addition after initialization
        // For now, this demonstrates the architecture
      }
      break;
    }
    
    case DetectedProtocol::JsonRpc: {
      if (enable_native_mcp_) {
        // Create native MCP filter chain
        auto stdio_factory = std::make_shared<McpStdioFilterChainFactory>(
            dispatcher_, callbacks_, is_server_, true);
        
        // Add MCP filters after detection filter
        // Note: Similar to above, would need FilterManager enhancement
      }
      break;
    }
    
    default:
      // Unknown protocol, connection will be closed
      break;
  }
}

} // namespace filter
} // namespace mcp