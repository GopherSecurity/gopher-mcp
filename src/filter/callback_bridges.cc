/**
 * @file callback_bridges.cc
 * @brief Implementation of callback bridge classes for filter chain data flow
 */

#include "mcp/filter/callback_bridges.h"

#include "mcp/logging/log_macros.h"
#include "mcp/network/buffer.h"

namespace mcp {
namespace filter {

// HttpToFilterChainBridge Implementation

HttpToFilterChainBridge::HttpToFilterChainBridge(
    network::FilterCallbacks& filter_callbacks)
    : filter_callbacks_(filter_callbacks) {
  GOPHER_LOG_DEBUG("HttpToFilterChainBridge created");
}

void HttpToFilterChainBridge::onHeaders(
    const std::map<std::string, std::string>& headers, bool keep_alive) {
  GOPHER_LOG_DEBUG("HttpToFilterChainBridge::onHeaders called with {} headers, "
                   "keep_alive={}", headers.size(), keep_alive);

  // For HTTP → SSE pipeline, we expect the body to contain SSE data
  // Headers are typically processed but the SSE content is in the body
  // We'll accumulate the body and pass it to the next filter when complete
}

void HttpToFilterChainBridge::onBody(const std::string& data, bool end_stream) {
  GOPHER_LOG_DEBUG("HttpToFilterChainBridge::onBody called with {} bytes, "
                   "end_stream={}", data.length(), end_stream);

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
  GOPHER_LOG_DEBUG("HttpToFilterChainBridge::onMessageComplete called");

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
  GOPHER_LOG_ERROR("HttpToFilterChainBridge::onError: {}", error);

  // Forward error through filter chain error handling
  // Note: We'll rely on the filter chain's standard error handling mechanisms
}

// SseToFilterChainBridge Implementation

SseToFilterChainBridge::SseToFilterChainBridge(
    network::FilterCallbacks& filter_callbacks)
    : filter_callbacks_(filter_callbacks) {
  GOPHER_LOG_DEBUG("SseToFilterChainBridge created");
}

void SseToFilterChainBridge::onEvent(const std::string& event,
                                     const std::string& data,
                                     const optional<std::string>& id) {
  if (id.has_value()) {
    GOPHER_LOG_DEBUG("SseToFilterChainBridge::onEvent called with event='{}', "
                     "data length={}, id='{}'", event, data.length(), id.value());
  } else {
    GOPHER_LOG_DEBUG("SseToFilterChainBridge::onEvent called with event='{}', "
                     "data length={}", event, data.length());
  }

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
  GOPHER_LOG_DEBUG("SseToFilterChainBridge::onComment called with comment='{}'",
                   comment);

  // SSE comments are typically used for keep-alive, no need to forward
  // to JSON-RPC layer as they don't contain protocol data
}

void SseToFilterChainBridge::onError(const std::string& error) {
  GOPHER_LOG_ERROR("SseToFilterChainBridge::onError: {}", error);

  // Forward error through filter chain error handling
  // We'll rely on the filter chain's standard error handling mechanisms
}

// JsonRpcToProtocolBridge Implementation

JsonRpcToProtocolBridge::JsonRpcToProtocolBridge(
    McpProtocolCallbacks& callbacks)
    : callbacks_(callbacks) {
  GOPHER_LOG_DEBUG("JsonRpcToProtocolBridge created");
}

void JsonRpcToProtocolBridge::onRequest(const jsonrpc::Request& request) {
  GOPHER_LOG_DEBUG("JsonRpcToProtocolBridge::onRequest called with method='{}'",
                   request.method);

  // Forward parsed JSON-RPC request to final protocol callbacks
  callbacks_.onRequest(request);
}

void JsonRpcToProtocolBridge::onNotification(
    const jsonrpc::Notification& notification) {
  GOPHER_LOG_DEBUG("JsonRpcToProtocolBridge::onNotification called with method='{}'",
                   notification.method);

  // Forward parsed JSON-RPC notification to final protocol callbacks
  callbacks_.onNotification(notification);
}

void JsonRpcToProtocolBridge::onResponse(const jsonrpc::Response& response) {
  if (response.id.has_value()) {
    GOPHER_LOG_DEBUG("JsonRpcToProtocolBridge::onResponse called with id={}",
                     response.id.value().toString());
  } else {
    GOPHER_LOG_DEBUG("JsonRpcToProtocolBridge::onResponse called");
  }

  // Forward parsed JSON-RPC response to final protocol callbacks
  callbacks_.onResponse(response);
}

void JsonRpcToProtocolBridge::onProtocolError(const Error& error) {
  GOPHER_LOG_ERROR("JsonRpcToProtocolBridge::onProtocolError: {}", error.message);

  // Forward protocol error to final callbacks
  callbacks_.onError(error);
}

// FilterBridgeFactory Implementation

std::shared_ptr<HttpToFilterChainBridge> FilterBridgeFactory::createHttpBridge(
    network::FilterCallbacks& filter_callbacks) {
  GOPHER_LOG_DEBUG("FilterBridgeFactory::createHttpBridge called");

  auto bridge = std::make_shared<HttpToFilterChainBridge>(filter_callbacks);

  GOPHER_LOG_DEBUG("FilterBridgeFactory::createHttpBridge completed");

  return bridge;
}

std::shared_ptr<SseToFilterChainBridge> FilterBridgeFactory::createSseBridge(
    network::FilterCallbacks& filter_callbacks) {
  GOPHER_LOG_DEBUG("FilterBridgeFactory::createSseBridge called");

  auto bridge = std::make_shared<SseToFilterChainBridge>(filter_callbacks);

  GOPHER_LOG_DEBUG("FilterBridgeFactory::createSseBridge completed");

  return bridge;
}

std::shared_ptr<JsonRpcToProtocolBridge>
FilterBridgeFactory::createJsonRpcBridge(
    McpProtocolCallbacks& final_callbacks) {
  GOPHER_LOG_DEBUG("FilterBridgeFactory::createJsonRpcBridge called");

  auto bridge = std::make_shared<JsonRpcToProtocolBridge>(final_callbacks);

  GOPHER_LOG_DEBUG("FilterBridgeFactory::createJsonRpcBridge completed");

  return bridge;
}

}  // namespace filter
}  // namespace mcp