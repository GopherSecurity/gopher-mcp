/**
 * @file callback_bridges.h
 * @brief Callback bridge classes for filter chain data flow
 *
 * This file provides bridge classes that connect filter-specific callback
 * interfaces to the central McpProtocolCallbacks, enabling proper data flow
 * through the HTTP → SSE → JSON-RPC filter pipeline.
 */

#pragma once

#include <map>
#include <memory>
#include <string>

#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/filter.h"

namespace mcp {
namespace filter {

/**
 * Bridge that connects HttpCodecFilter::MessageCallbacks to network filter chain
 *
 * This bridge receives HTTP messages from the HTTP codec and forwards them
 * through the standard network filter interface to the next filter in the chain.
 * The data flows through FilterManager to the next filter.
 */
class HttpToFilterChainBridge : public HttpCodecFilter::MessageCallbacks {
public:
  /**
   * Constructor
   * @param filter_callbacks The filter callbacks for injecting data into the chain
   */
  explicit HttpToFilterChainBridge(network::FilterCallbacks& filter_callbacks);

  // HttpCodecFilter::MessageCallbacks implementation
  void onHeaders(const std::map<std::string, std::string>& headers,
                 bool keep_alive) override;
  void onBody(const std::string& data, bool end_stream) override;
  void onMessageComplete() override;
  void onError(const std::string& error) override;

private:
  network::FilterCallbacks& filter_callbacks_;
  std::string accumulated_body_;
};

/**
 * Bridge that connects SseCodecFilter::EventCallbacks to network filter chain
 *
 * This bridge receives SSE events from the SSE codec and forwards them
 * through the standard network filter interface to the next filter in the chain.
 * The data flows through FilterManager to the next filter.
 */
class SseToFilterChainBridge : public SseCodecFilter::EventCallbacks {
public:
  /**
   * Constructor
   * @param filter_callbacks The filter callbacks for injecting data into the chain
   */
  explicit SseToFilterChainBridge(network::FilterCallbacks& filter_callbacks);

  // SseCodecFilter::EventCallbacks implementation
  void onEvent(const std::string& event,
               const std::string& data,
               const optional<std::string>& id) override;
  void onComment(const std::string& comment) override;
  void onError(const std::string& error) override;

private:
  network::FilterCallbacks& filter_callbacks_;
};

/**
 * Bridge that connects JsonRpcProtocolFilter::MessageHandler to McpProtocolCallbacks
 *
 * This bridge receives parsed JSON-RPC messages from the JSON-RPC protocol filter
 * and forwards them to the final application callbacks. This is the final bridge
 * in the HTTP → SSE → JSON-RPC pipeline.
 */
class JsonRpcToProtocolBridge : public JsonRpcProtocolFilter::MessageHandler {
public:
  /**
   * Constructor
   * @param callbacks The final MCP protocol callbacks
   */
  explicit JsonRpcToProtocolBridge(McpProtocolCallbacks& callbacks);

  // JsonRpcProtocolFilter::MessageHandler implementation
  void onRequest(const jsonrpc::Request& request) override;
  void onNotification(const jsonrpc::Notification& notification) override;
  void onResponse(const jsonrpc::Response& response) override;
  void onProtocolError(const Error& error) override;

private:
  McpProtocolCallbacks& callbacks_;
};

/**
 * Factory class for creating callback bridges for filter chain data flow
 *
 * This factory creates the appropriate bridge instances for connecting
 * filter-specific callbacks to the network filter chain and final protocol callbacks.
 */
class FilterBridgeFactory {
public:
  /**
   * Create an HTTP codec callback bridge
   *
   * @param filter_callbacks The filter callbacks for injecting data into the chain
   * @return Shared pointer to the created bridge
   */
  static std::shared_ptr<HttpToFilterChainBridge> createHttpBridge(
      network::FilterCallbacks& filter_callbacks);

  /**
   * Create an SSE codec callback bridge
   *
   * @param filter_callbacks The filter callbacks for injecting data into the chain
   * @return Shared pointer to the created bridge
   */
  static std::shared_ptr<SseToFilterChainBridge> createSseBridge(
      network::FilterCallbacks& filter_callbacks);

  /**
   * Create a JSON-RPC to protocol callback bridge
   *
   * @param final_callbacks The final protocol callbacks
   * @return Shared pointer to the created bridge
   */
  static std::shared_ptr<JsonRpcToProtocolBridge> createJsonRpcBridge(
      McpProtocolCallbacks& final_callbacks);
};

}  // namespace filter
}  // namespace mcp