#pragma once

#include "mcp/event/event_loop.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/mcp_jsonrpc_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/network/connection.h"
#include "mcp/network/filter.h"

// Forward declarations
namespace mcp {
class McpMessageCallbacks;

namespace filter {
class HttpRoutingFilter;
class MetricsFilter;
}  // namespace filter
}

namespace mcp {
namespace filter {

/**
 * MCP HTTP+SSE Filter Chain Factory
 *
 * Following production FilterChainFactory pattern:
 * - Creates complete protocol stack for HTTP+SSE transport
 * - Each filter handles exactly one protocol layer
 * - Transport socket handles ONLY raw I/O
 *
 * Filter Chain Architecture:
 * ```
 * Client Mode:
 *   [TCP Socket] → [HTTP Codec] → [SSE Codec] → [JSON-RPC] → [Application]
 *   - HTTP Codec: Generates HTTP requests, parses HTTP responses
 *   - SSE Codec: Parses SSE events from response stream
 *   - JSON-RPC: Handles JSON-RPC protocol messages
 *
 * Server Mode:
 *   [TCP Socket] → [HTTP Codec] → [SSE Codec] → [JSON-RPC] → [Application]
 *   - HTTP Codec: Parses HTTP requests, generates HTTP responses
 *   - SSE Codec: Generates SSE events for response stream
 *   - JSON-RPC: Handles JSON-RPC protocol messages
 * ```
 *
 */
class McpHttpFilterChainFactory : public network::FilterChainFactory {
 public:
  /**
   * Constructor
   * @param dispatcher Event dispatcher for async operations
   * @param message_callbacks MCP message callbacks for handling requests
   * @param is_server True for server mode, false for client mode
   */
  McpHttpFilterChainFactory(event::Dispatcher& dispatcher,
                            McpMessageCallbacks& message_callbacks,
                            bool is_server = true)
      : dispatcher_(dispatcher),
        message_callbacks_(message_callbacks),
        is_server_(is_server) {}

  /**
   * Create filter chain for the connection
   * Following production pattern from FilterChainManager
   *
   * @param filter_manager The filter manager to add filters to
   * @return true if filter chain was created successfully
   */
  bool createFilterChain(network::FilterManager& filter_manager) const override;

  /**
   * Create network filter chain (alternative interface)
   * Following production pattern from FilterChainManager
   */
  bool createNetworkFilterChain(network::FilterManager& filter_manager,
                                const std::vector<network::FilterFactoryCb>&
                                    filter_factories) const override;

  /**
   * Create listener filter chain
   * Not used for this implementation
   */
  bool createListenerFilterChain(
      network::FilterManager& filter_manager) const override {
    return false;
  }

  /**
   * Enable metrics collection
   * When true, adds MetricsFilter to the chain
   */
  void enableMetrics(bool enable = true) { enable_metrics_ = enable; }
  
  /**
   * Send a response through the current request's filter chain
   * Following production pattern: maintain request-response context
   * This is used by the server to send responses for HTTP requests
   */
  static void sendHttpResponse(const jsonrpc::Response& response);

 private:
  event::Dispatcher& dispatcher_;
  McpMessageCallbacks& message_callbacks_;
  bool is_server_;
  mutable bool enable_metrics_ = true;  // Enable metrics by default

  // Store filters for lifetime management
  mutable std::vector<network::FilterSharedPtr> filters_;
};

}  // namespace filter
}  // namespace mcp