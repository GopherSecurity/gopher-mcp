/**
 * @file callback_bridges.cc
 * @brief Implementation of callback bridge classes for filter chain data flow
 */

#include "mcp/filter/callback_bridges.h"

#include <iostream>

#include "mcp/network/buffer.h"

namespace mcp {
namespace filter {

// HttpToFilterChainBridge Implementation

HttpToFilterChainBridge::HttpToFilterChainBridge(
    network::FilterCallbacks& filter_callbacks)
    : filter_callbacks_(filter_callbacks) {
  std::cerr << "[DEBUG] HttpToFilterChainBridge created" << std::endl;
}

void HttpToFilterChainBridge::onHeaders(
    const std::map<std::string, std::string>& headers, bool keep_alive) {
  std::cerr << "[DEBUG] HttpToFilterChainBridge::onHeaders called with "
            << headers.size() << " headers, keep_alive=" << keep_alive
            << std::endl;

  // For HTTP → SSE pipeline, we expect the body to contain SSE data
  // Headers are typically processed but the SSE content is in the body
  // We'll accumulate the body and pass it to the next filter when complete
}

void HttpToFilterChainBridge::onBody(const std::string& data, bool end_stream) {
  std::cerr << "[DEBUG] HttpToFilterChainBridge::onBody called with "
            << data.length() << " bytes, end_stream=" << end_stream
            << std::endl;

  // Accumulate body data for forwarding to next filter
  accumulated_body_ += data;

  // If this is the end of the stream, forward accumulated data through filter
  // chain
  if (end_stream) {
    // Create a buffer with the accumulated HTTP body (which should be SSE data)
    Buffer next_filter_buffer;
    next_filter_buffer.add(accumulated_body_);

    // Forward through filter chain to next filter
    filter_callbacks_.injectReadDataToFilterChain(next_filter_buffer,
                                                  end_stream);

    // Clear accumulated data for next message
    accumulated_body_.clear();
  }
}

void HttpToFilterChainBridge::onMessageComplete() {
  std::cerr << "[DEBUG] HttpToFilterChainBridge::onMessageComplete called"
            << std::endl;

  // HTTP message complete - if we have accumulated data, forward it through
  // chain
  if (!accumulated_body_.empty()) {
    Buffer next_filter_buffer;
    next_filter_buffer.add(accumulated_body_);

    // Forward complete message through filter chain
    filter_callbacks_.injectReadDataToFilterChain(next_filter_buffer, true);
    accumulated_body_.clear();
  }
}

void HttpToFilterChainBridge::onError(const std::string& error) {
  std::cerr << "[ERROR] HttpToFilterChainBridge::onError: " << error
            << std::endl;

  // Forward error through filter chain error handling
  // Note: We'll rely on the filter chain's standard error handling mechanisms
}

// SseToFilterChainBridge Implementation

SseToFilterChainBridge::SseToFilterChainBridge(
    network::FilterCallbacks& filter_callbacks)
    : filter_callbacks_(filter_callbacks) {
  std::cerr << "[DEBUG] SseToFilterChainBridge created" << std::endl;
}

void SseToFilterChainBridge::onEvent(const std::string& event,
                                     const std::string& data,
                                     const optional<std::string>& id) {
  std::cerr << "[DEBUG] SseToFilterChainBridge::onEvent called with event='"
            << event << "', data length=" << data.length();
  if (id.has_value()) {
    std::cerr << ", id='" << id.value() << "'";
  }
  std::cerr << std::endl;

  // Forward SSE event data through filter chain for JSON-RPC parsing
  if (!data.empty()) {
    // The SSE event data should contain JSON-RPC messages
    Buffer json_rpc_buffer;
    json_rpc_buffer.add(data);

    // Forward through filter chain to next filter
    filter_callbacks_.injectReadDataToFilterChain(json_rpc_buffer, false);
  }
}

void SseToFilterChainBridge::onComment(const std::string& comment) {
  std::cerr << "[DEBUG] SseToFilterChainBridge::onComment called with comment='"
            << comment << "'" << std::endl;

  // SSE comments are typically used for keep-alive, no need to forward
  // to JSON-RPC layer as they don't contain protocol data
}

void SseToFilterChainBridge::onError(const std::string& error) {
  std::cerr << "[ERROR] SseToFilterChainBridge::onError: " << error
            << std::endl;

  // Forward error through filter chain error handling
  // We'll rely on the filter chain's standard error handling mechanisms
}

// JsonRpcToProtocolBridge Implementation

JsonRpcToProtocolBridge::JsonRpcToProtocolBridge(
    McpProtocolCallbacks& callbacks)
    : callbacks_(callbacks) {
  std::cerr << "[DEBUG] JsonRpcToProtocolBridge created" << std::endl;
}

void JsonRpcToProtocolBridge::onRequest(const jsonrpc::Request& request) {
  std::cerr << "[DEBUG] JsonRpcToProtocolBridge::onRequest called with method='"
            << request.method << "'" << std::endl;

  // Forward parsed JSON-RPC request to final protocol callbacks
  callbacks_.onRequest(request);
}

void JsonRpcToProtocolBridge::onNotification(
    const jsonrpc::Notification& notification) {
  std::cerr
      << "[DEBUG] JsonRpcToProtocolBridge::onNotification called with method='"
      << notification.method << "'" << std::endl;

  // Forward parsed JSON-RPC notification to final protocol callbacks
  callbacks_.onNotification(notification);
}

void JsonRpcToProtocolBridge::onResponse(const jsonrpc::Response& response) {
  std::cerr << "[DEBUG] JsonRpcToProtocolBridge::onResponse called";
  if (response.id.has_value()) {
    std::cerr << " with id=" << response.id.value().toString();
  }
  std::cerr << std::endl;

  // Forward parsed JSON-RPC response to final protocol callbacks
  callbacks_.onResponse(response);
}

void JsonRpcToProtocolBridge::onProtocolError(const Error& error) {
  std::cerr << "[ERROR] JsonRpcToProtocolBridge::onProtocolError: "
            << error.message << std::endl;

  // Forward protocol error to final callbacks
  callbacks_.onError(error);
}

// FilterBridgeFactory Implementation

std::shared_ptr<HttpToFilterChainBridge> FilterBridgeFactory::createHttpBridge(
    network::FilterCallbacks& filter_callbacks) {
  std::cerr << "[DEBUG] FilterBridgeFactory::createHttpBridge called"
            << std::endl;

  auto bridge = std::make_shared<HttpToFilterChainBridge>(filter_callbacks);

  std::cerr << "[DEBUG] FilterBridgeFactory::createHttpBridge completed"
            << std::endl;

  return bridge;
}

std::shared_ptr<SseToFilterChainBridge> FilterBridgeFactory::createSseBridge(
    network::FilterCallbacks& filter_callbacks) {
  std::cerr << "[DEBUG] FilterBridgeFactory::createSseBridge called"
            << std::endl;

  auto bridge = std::make_shared<SseToFilterChainBridge>(filter_callbacks);

  std::cerr << "[DEBUG] FilterBridgeFactory::createSseBridge completed"
            << std::endl;

  return bridge;
}

std::shared_ptr<JsonRpcToProtocolBridge>
FilterBridgeFactory::createJsonRpcBridge(
    McpProtocolCallbacks& final_callbacks) {
  std::cerr << "[DEBUG] FilterBridgeFactory::createJsonRpcBridge called"
            << std::endl;

  auto bridge = std::make_shared<JsonRpcToProtocolBridge>(final_callbacks);

  std::cerr << "[DEBUG] FilterBridgeFactory::createJsonRpcBridge completed"
            << std::endl;

  return bridge;
}

}  // namespace filter
}  // namespace mcp