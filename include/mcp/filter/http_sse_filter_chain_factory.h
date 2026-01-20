#pragma once

#include <memory>
#include <string>

#include "mcp/event/event_loop.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/network/connection.h"
#include "mcp/network/filter.h"

// Forward declarations
namespace mcp {
class McpProtocolCallbacks;

namespace filter {
class HttpRoutingFilter;
class MetricsFilter;
}  // namespace filter
}  // namespace mcp

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
class HttpSseFilterChainFactory : public network::FilterChainFactory {
 public:
  /**
   * Constructor
   * @param dispatcher Event dispatcher for async operations
   * @param message_callbacks MCP message callbacks for handling requests
   * @param is_server True for server mode, false for client mode
   * @param http_path HTTP request path for client mode (e.g., "/sse")
   * @param http_host HTTP Host header value for client mode
   * @param use_sse True for SSE mode (GET /sse first), false for Streamable HTTP (direct POST)
   */
  HttpSseFilterChainFactory(event::Dispatcher& dispatcher,
                            McpProtocolCallbacks& message_callbacks,
                            bool is_server = true,
                            const std::string& http_path = "/rpc",
                            const std::string& http_host = "localhost",
                            bool use_sse = true)
      : dispatcher_(dispatcher),
        message_callbacks_(message_callbacks),
        is_server_(is_server),
        http_path_(http_path),
        http_host_(http_host),
        use_sse_(use_sse) {}

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
   * Send a response through the connection's filter chain
   * Following production pattern: connection context flows through
   * @param response The JSON-RPC response to send
   * @param connection The connection to send the response on
   */
  static void sendHttpResponse(const jsonrpc::Response& response,
                               network::Connection& connection);

 private:
  event::Dispatcher& dispatcher_;
  McpProtocolCallbacks& message_callbacks_;
  bool is_server_;
  std::string http_path_;  // HTTP request path for client mode
  std::string http_host_;  // HTTP Host header for client mode
  bool use_sse_;           // True for SSE mode, false for Streamable HTTP
  mutable bool enable_metrics_ = true;  // Enable metrics by default

  // Store filters for lifetime management
  mutable std::vector<network::FilterSharedPtr> filters_;
};

}  // namespace filter
}  // namespace mcp