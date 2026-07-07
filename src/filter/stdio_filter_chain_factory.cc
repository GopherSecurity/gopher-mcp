/**
 * MCP Stdio Filter Chain Factory Implementation
 *
 * Simple filter chain for direct transports without protocol stacks
 */

#include "mcp/filter/stdio_filter_chain_factory.h"

// DirectJsonRpcCallbacks lives in json_rpc_filter_factory.h. This file
// used to define its own namespace-scope class with the SAME name — an
// ODR violation: two translation units emitted the same mangled class
// with (after the context hooks landed) different vtable layouts, which
// a linker is free to merge into misrouted virtual calls. Reuse the one
// public definition instead.
#include "mcp/filter/json_rpc_filter_factory.h"
#include "mcp/filter/json_rpc_protocol_filter.h"

namespace mcp {
namespace filter {

// Anonymous namespace: this wrapper is an implementation detail of the
// stdio factory; internal linkage keeps any future same-named class in
// another translation unit from colliding the way DirectJsonRpcCallbacks
// did.
namespace {

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

}  // namespace

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