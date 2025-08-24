/**
 * MCP Stdio Filter Chain Factory Implementation
 *
 * Simple filter chain for direct transports without protocol stacks
 */

#include "mcp/filter/stdio_filter_chain_factory.h"

#include "mcp/filter/json_rpc_protocol_filter.h"

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
 * Wrapper filter that owns the callbacks adapter
 * This ensures the callbacks outlive the filter factory
 */
class StdioJsonRpcFilterWrapper : public network::NetworkFilterBase {
 public:
  StdioJsonRpcFilterWrapper(event::Dispatcher& dispatcher,
                            McpProtocolCallbacks& message_callbacks,
                            bool is_server,
                            bool use_framing)
      : callbacks_adapter_(
            std::make_shared<DirectJsonRpcCallbacks>(message_callbacks)),
        jsonrpc_filter_(std::make_shared<JsonRpcProtocolFilter>(
            *callbacks_adapter_, dispatcher, is_server)) {
    jsonrpc_filter_->setUseFraming(use_framing);
  }

  // Network filter interface
  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    return jsonrpc_filter_->onData(data, end_stream);
  }

  network::FilterStatus onNewConnection() override {
    return jsonrpc_filter_->onNewConnection();
  }

  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    return jsonrpc_filter_->onWrite(data, end_stream);
  }

  // Filter initialization
  void initializeReadFilterCallbacks(
      network::ReadFilterCallbacks& callbacks) override {
    jsonrpc_filter_->initializeReadFilterCallbacks(callbacks);
  }

  void initializeWriteFilterCallbacks(
      network::WriteFilterCallbacks& callbacks) override {
    jsonrpc_filter_->initializeWriteFilterCallbacks(callbacks);
  }

 private:
  // Own the callbacks adapter to ensure it outlives the filter
  std::shared_ptr<DirectJsonRpcCallbacks> callbacks_adapter_;
  std::shared_ptr<JsonRpcProtocolFilter> jsonrpc_filter_;
};

bool StdioFilterChainFactory::createFilterChain(
    network::FilterManager& filter_manager) const {
  // Create wrapper filter that owns its callbacks
  // This ensures callbacks outlive the filter factory
  auto wrapper_filter = std::make_shared<StdioJsonRpcFilterWrapper>(
      dispatcher_, message_callbacks_, is_server_, use_framing_);

  // Add as both read and write filter
  filter_manager.addReadFilter(wrapper_filter);
  filter_manager.addWriteFilter(wrapper_filter);

  return true;
}

bool StdioFilterChainFactory::createNetworkFilterChain(
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

}  // namespace filter
}  // namespace mcp