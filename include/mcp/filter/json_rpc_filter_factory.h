#pragma once

#include "mcp/event/event_loop.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/filter.h"

namespace mcp {
namespace filter {

/**
 * Direct callbacks adapter for JSON-RPC to MCP
 * Simply forwards JSON-RPC messages to MCP callbacks
 */
class DirectJsonRpcCallbacks : public JsonRpcProtocolFilter::MessageHandler {
 public:
  DirectJsonRpcCallbacks(McpProtocolCallbacks& mcp_callbacks)
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

/**
 * Factory for creating JSON-RPC filter chains
 *
 * This is a reusable component that creates filter chains for processing
 * JSON-RPC messages. It can be used by both client and server connections.
 *
 * Flow: Transport delivers data → Filter parses JSON-RPC → Callbacks handle
 * messages
 */
class JsonRpcFilterChainFactory : public network::FilterChainFactory {
 public:
  /**
   * Constructor
   * @param dispatcher Event dispatcher for async operations
   * @param callbacks Message callbacks for handling parsed JSON-RPC messages
   * @param use_framing Whether to use message framing (false for HTTP
   * transport)
   */
  JsonRpcFilterChainFactory(event::Dispatcher& dispatcher,
                            McpProtocolCallbacks& callbacks,
                            bool use_framing = true)
      : dispatcher_(dispatcher),
        callbacks_(callbacks),
        use_framing_(use_framing) {}

  /**
   * Create the filter chain
   * Adds JsonRpcProtocolFilter for both read and write paths
   */
  bool createFilterChain(network::FilterManager& manager) const override {
    // Create callbacks adapter
    json_callbacks_ = std::make_shared<DirectJsonRpcCallbacks>(callbacks_);

    // Create the JSON-RPC message filter
    // This filter parses incoming JSON-RPC and frames outgoing messages
    filter_ = std::make_shared<JsonRpcProtocolFilter>(
        *json_callbacks_, dispatcher_, false);  // false for client mode
    filter_->setUseFraming(use_framing_);

    // Add to both read and write filter chains
    // Read: Parse incoming JSON-RPC messages
    // Write: Frame outgoing JSON-RPC responses (if framing enabled)
    manager.addReadFilter(filter_);
    manager.addWriteFilter(filter_);

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
  bool createListenerFilterChain(
      network::FilterManager& filter_manager) const override {
    // Not used for JSON-RPC processing
    return true;
  }

 private:
  event::Dispatcher& dispatcher_;
  McpProtocolCallbacks& callbacks_;
  bool use_framing_;

  // Store for lifetime management
  mutable std::shared_ptr<DirectJsonRpcCallbacks> json_callbacks_;
  mutable std::shared_ptr<JsonRpcProtocolFilter> filter_;
};

}  // namespace filter
}  // namespace mcp