#pragma once

#include "mcp/event/event_loop.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/network/connection.h"
#include "mcp/network/filter.h"

// Forward declarations
namespace mcp {
class McpMessageCallbacks;
}

namespace mcp {
namespace filter {

/**
 * MCP HTTP Filter Chain Factory
 *
 * Following production FilterChainFactory pattern exactly:
 * - Creates the complete filter chain for a connection
 * - Filters handle ALL protocol processing
 * - Transport socket is pure I/O only
 *
 * Based on:
 * source/common/listener_manager/filter_chain_factory_context_callback.h
 */
class McpHttpFilterChainFactory : public network::FilterChainFactory {
 public:
  /**
   * Constructor
   * @param dispatcher Event dispatcher for async operations
   * @param message_callbacks MCP message callbacks for handling requests
   */
  McpHttpFilterChainFactory(event::Dispatcher& dispatcher,
                            McpMessageCallbacks& message_callbacks)
      : dispatcher_(dispatcher), message_callbacks_(message_callbacks) {}

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

 private:
  /**
   * Bridge implementation that connects HTTP/SSE filters to MCP callbacks
   * This implements the protocol adaptation layer
   */
  class McpProtocolBridge : public HttpCodecFilter::MessageCallbacks,
                            public SseCodecFilter::EventCallbacks {
   public:
    McpProtocolBridge(McpMessageCallbacks& callbacks)
        : message_callbacks_(callbacks) {}

    void setFilters(HttpCodecFilter* http_filter, SseCodecFilter* sse_filter) {
      http_filter_ = http_filter;
      sse_filter_ = sse_filter;
    }

    // HttpCodecFilter::MessageCallbacks
    void onHeaders(const std::map<std::string, std::string>& headers,
                   bool keep_alive) override;
    void onBody(const std::string& data, bool end_stream) override;
    void onMessageComplete() override;

    // SseCodecFilter::EventCallbacks
    void onEvent(const std::string& event,
                 const std::string& data,
                 const optional<std::string>& id) override;
    void onComment(const std::string& comment) override;

    // Shared error handler for both interfaces
    void onError(const std::string& error) override;

    /**
     * Send MCP response
     * Routes to either HTTP response or SSE event based on mode
     */
    void sendResponse(const std::string& response);

   private:
    McpMessageCallbacks& message_callbacks_;
    HttpCodecFilter* http_filter_{nullptr};
    SseCodecFilter* sse_filter_{nullptr};

    // Request state
    enum class RequestMode {
      UNKNOWN,
      HTTP_RPC,   // Single HTTP request/response
      SSE_STREAM  // SSE event stream
    };
    RequestMode mode_{RequestMode::UNKNOWN};
    std::string current_request_body_;
  };

  event::Dispatcher& dispatcher_;
  McpMessageCallbacks& message_callbacks_;

  // Store bridges for lifetime management
  std::vector<std::unique_ptr<McpProtocolBridge>> bridges_;
};

}  // namespace filter
}  // namespace mcp