/**
 * MCP HTTP+SSE Filter Chain Factory Implementation
 * 
 * Following production architecture strictly:
 * - Clean separation between protocol layers
 * - Each filter handles exactly one concern
 * - No mixing of protocol responsibilities
 */

#include "mcp/filter/mcp_http_filter_chain_factory.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/mcp_jsonrpc_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/connection.h"

namespace mcp {
namespace filter {

/**
 * HTTP Protocol Adapter
 * Bridges HTTP codec filter to SSE filter for protocol flow
 */
class HttpToSseAdapter : public HttpCodecFilter::MessageCallbacks {
public:
  HttpToSseAdapter(SseCodecFilter& sse_filter, bool is_server)
      : sse_filter_(sse_filter), is_server_(is_server) {}
  
  void onHeaders(const std::map<std::string, std::string>& headers,
                 bool keep_alive) override {
    // Determine if this is an SSE request/response
    auto content_type = headers.find("content-type");
    auto accept = headers.find("accept");
    
    if (is_server_) {
      // Server mode: Check if client wants SSE
      if (accept != headers.end() && 
          accept->second.find("text/event-stream") != std::string::npos) {
        // Start SSE stream
        is_sse_mode_ = true;
        sse_filter_.startEventStream();
      }
    } else {
      // Client mode: Check if server is sending SSE
      if (content_type != headers.end() &&
          content_type->second.find("text/event-stream") != std::string::npos) {
        is_sse_mode_ = true;
        // SSE filter will parse events from body
      }
    }
    
    // Store headers for potential use
    current_headers_ = headers;
    keep_alive_ = keep_alive;
  }
  
  void onBody(const std::string& data, bool end_stream) override {
    if (is_sse_mode_) {
      // Forward to SSE filter for event parsing
      auto buffer = std::make_unique<OwnedBuffer>();
      buffer->add(data);
      sse_filter_.onData(*buffer, end_stream);
    } else {
      // Regular HTTP body - accumulate
      current_body_ += data;
      if (end_stream) {
        // Complete HTTP message received
        onMessageComplete();
      }
    }
  }
  
  void onMessageComplete() override {
    // HTTP message complete (non-SSE mode)
    // The JSON-RPC filter will handle the body content
    // Reset for next message
    current_headers_.clear();
    current_body_.clear();
  }
  
  void onError(const std::string& error) override {
    // HTTP protocol error
    // This should be propagated to application layer
    (void)error;
  }
  
private:
  SseCodecFilter& sse_filter_;
  bool is_server_;
  bool is_sse_mode_{false};
  bool keep_alive_{true};
  std::map<std::string, std::string> current_headers_;
  std::string current_body_;
};

/**
 * SSE to JSON-RPC Adapter
 * Bridges SSE events to JSON-RPC filter
 */
class SseToJsonRpcAdapter : public SseCodecFilter::EventCallbacks {
public:
  SseToJsonRpcAdapter(McpJsonRpcFilter& jsonrpc_filter)
      : jsonrpc_filter_(jsonrpc_filter) {}
  
  void onEvent(const std::string& event,
               const std::string& data,
               const optional<std::string>& id) override {
    (void)event;
    (void)id;
    
    // SSE event contains JSON-RPC message
    // Forward to JSON-RPC filter for parsing
    auto buffer = std::make_unique<OwnedBuffer>();
    buffer->add(data);
    jsonrpc_filter_.onData(*buffer, false);
  }
  
  void onComment(const std::string& comment) override {
    // SSE comments are used for keep-alive, ignore
    (void)comment;
  }
  
  void onError(const std::string& error) override {
    // SSE protocol error
    (void)error;
  }
  
private:
  McpJsonRpcFilter& jsonrpc_filter_;
};

/**
 * JSON-RPC to Application Adapter
 * Bridges JSON-RPC filter to MCP application callbacks
 */
class JsonRpcToMcpAdapter : public McpJsonRpcFilter::Callbacks {
public:
  JsonRpcToMcpAdapter(McpMessageCallbacks& mcp_callbacks)
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
  McpMessageCallbacks& mcp_callbacks_;
};

// Main factory implementation

bool McpHttpFilterChainFactory::createFilterChain(
    network::FilterManager& filter_manager) const {
  
  // Create adapters to bridge between protocol layers
  // Following production pattern: adapters provide clean interfaces between layers
  
  // 1. Create JSON-RPC to MCP adapter (top layer)
  auto jsonrpc_adapter = std::make_unique<JsonRpcToMcpAdapter>(message_callbacks_);
  
  // 2. Create JSON-RPC filter (handles JSON-RPC protocol)
  auto jsonrpc_filter = std::make_shared<McpJsonRpcFilter>(
      *jsonrpc_adapter, dispatcher_, is_server_);
  
  // 3. Create SSE to JSON-RPC adapter
  auto sse_adapter = std::make_unique<SseToJsonRpcAdapter>(*jsonrpc_filter);
  
  // 4. Create SSE codec filter (handles SSE protocol)
  auto sse_filter = std::make_shared<SseCodecFilter>(
      *sse_adapter, dispatcher_, is_server_);
  
  // 5. Create HTTP to SSE adapter
  auto http_adapter = std::make_unique<HttpToSseAdapter>(*sse_filter, is_server_);
  
  // 6. Create HTTP codec filter (handles HTTP protocol)
  auto http_filter = std::make_shared<HttpCodecFilter>(
      *http_adapter, dispatcher_, is_server_);
  
  // Add filters to filter manager in correct order
  // Data flows through read filters in order, write filters in reverse
  
  // Layer 1: HTTP protocol
  filter_manager.addReadFilter(http_filter);
  filter_manager.addWriteFilter(http_filter);
  
  // Layer 2: SSE protocol (optional, based on headers)
  filter_manager.addReadFilter(sse_filter);
  filter_manager.addWriteFilter(sse_filter);
  
  // Layer 3: JSON-RPC protocol
  filter_manager.addReadFilter(jsonrpc_filter);
  filter_manager.addWriteFilter(jsonrpc_filter);
  
  // Store adapters for lifetime management
  // NOTE: In production, these would be managed by a context object
  bridges_.push_back(std::shared_ptr<void>(std::move(jsonrpc_adapter)));
  bridges_.push_back(std::shared_ptr<void>(std::move(sse_adapter)));
  bridges_.push_back(std::shared_ptr<void>(std::move(http_adapter)));
  
  return true;
}

bool McpHttpFilterChainFactory::createNetworkFilterChain(
    network::FilterManager& filter_manager,
    const std::vector<network::FilterFactoryCb>& filter_factories) const {
  
  // First apply any additional filter factories
  for (const auto& factory : filter_factories) {
    auto filter = factory();
    if (filter) {
      filter_manager.addReadFilter(filter);
      filter_manager.addWriteFilter(filter);
    }
  }
  
  // Then create our standard filter chain
  return createFilterChain(filter_manager);
}

} // namespace filter
} // namespace mcp