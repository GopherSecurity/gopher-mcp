/**
 * MCP HTTP+SSE Filter Chain Factory Implementation
 * 
 * Following production architecture strictly:
 * - No separate adapter classes
 * - Filters implement callback interfaces directly
 * - Filter manager wires filters together
 * - Clean separation between protocol layers
 */

#include "mcp/filter/mcp_http_filter_chain_factory.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/mcp_jsonrpc_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/mcp_connection_manager.h"

namespace mcp {
namespace filter {

/**
 * Combined filter that implements all protocol layers
 * Following production pattern: one filter class can handle multiple protocols
 * by implementing the appropriate callback interfaces
 */
class McpHttpSseJsonRpcFilter : public network::Filter,
                                 public HttpCodecFilter::MessageCallbacks,
                                 public SseCodecFilter::EventCallbacks,
                                 public McpJsonRpcFilter::Callbacks {
public:
  McpHttpSseJsonRpcFilter(event::Dispatcher& dispatcher,
                          McpMessageCallbacks& mcp_callbacks,
                          bool is_server)
      : dispatcher_(dispatcher),
        mcp_callbacks_(mcp_callbacks),
        is_server_(is_server) {
    // Create the protocol filters
    // Each filter handles one protocol layer
    http_filter_ = std::make_shared<HttpCodecFilter>(*this, dispatcher_, is_server_);
    sse_filter_ = std::make_shared<SseCodecFilter>(*this, dispatcher_, is_server_);
    jsonrpc_filter_ = std::make_shared<McpJsonRpcFilter>(*this, dispatcher_, is_server_);
  }
  
  // ===== Network Filter Interface =====
  
  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    // Data flows through protocol layers in sequence
    // HTTP -> SSE -> JSON-RPC
    
    // First layer: HTTP codec processes the data
    auto status = http_filter_->onData(data, end_stream);
    if (status == network::FilterStatus::StopIteration) {
      return status;
    }
    
    // Second layer: SSE codec (if in SSE mode)
    if (is_sse_mode_) {
      status = sse_filter_->onData(data, end_stream);
      if (status == network::FilterStatus::StopIteration) {
        return status;
      }
    }
    
    // Third layer: JSON-RPC (processes accumulated data)
    if (pending_json_data_.length() > 0) {
      status = jsonrpc_filter_->onData(pending_json_data_, end_stream);
      pending_json_data_.drain(pending_json_data_.length());
    }
    
    return status;
  }
  
  network::FilterStatus onNewConnection() override {
    // Initialize all protocol filters
    http_filter_->onNewConnection();
    sse_filter_->onNewConnection();
    jsonrpc_filter_->onNewConnection();
    return network::FilterStatus::Continue;
  }
  
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    // Write flows through filters in reverse order
    // JSON-RPC -> SSE -> HTTP
    
    // JSON-RPC filter handles framing
    auto status = jsonrpc_filter_->onWrite(data, end_stream);
    if (status == network::FilterStatus::StopIteration) {
      return status;
    }
    
    // SSE filter formats events (if in SSE mode)
    if (is_sse_mode_) {
      status = sse_filter_->onWrite(data, end_stream);
      if (status == network::FilterStatus::StopIteration) {
        return status;
      }
    }
    
    // HTTP filter adds headers/framing
    return http_filter_->onWrite(data, end_stream);
  }
  
  void initializeReadFilterCallbacks(network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    http_filter_->initializeReadFilterCallbacks(callbacks);
    sse_filter_->initializeReadFilterCallbacks(callbacks);
    jsonrpc_filter_->initializeReadFilterCallbacks(callbacks);
  }
  
  void initializeWriteFilterCallbacks(network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
    http_filter_->initializeWriteFilterCallbacks(callbacks);
    sse_filter_->initializeWriteFilterCallbacks(callbacks);
    jsonrpc_filter_->initializeWriteFilterCallbacks(callbacks);
  }
  
  // ===== HttpCodecFilter::MessageCallbacks =====
  
  void onHeaders(const std::map<std::string, std::string>& headers,
                 bool keep_alive) override {
    // Determine transport mode based on headers
    if (is_server_) {
      // Server: check Accept header for SSE
      auto accept = headers.find("accept");
      if (accept != headers.end() && 
          accept->second.find("text/event-stream") != std::string::npos) {
        is_sse_mode_ = true;
        
        // Send SSE response headers
        std::map<std::string, std::string> response_headers = {
          {"content-type", "text/event-stream"},
          {"cache-control", "no-cache"},
          {"connection", keep_alive ? "keep-alive" : "close"},
          {"access-control-allow-origin", "*"}
        };
        
        http_filter_->messageEncoder().encodeHeaders("200", response_headers, false);
        sse_filter_->startEventStream();
      } else {
        is_sse_mode_ = false;
      }
    } else {
      // Client: check Content-Type for SSE
      auto content_type = headers.find("content-type");
      is_sse_mode_ = content_type != headers.end() &&
                     content_type->second.find("text/event-stream") != std::string::npos;
    }
  }
  
  void onBody(const std::string& data, bool end_stream) override {
    if (is_sse_mode_) {
      // In SSE mode, body contains event stream
      // Forward to SSE filter for parsing
      auto buffer = std::make_unique<OwnedBuffer>();
      buffer->add(data);
      sse_filter_->onData(*buffer, end_stream);
    } else {
      // In RPC mode, body contains JSON-RPC
      // Accumulate and forward to JSON-RPC filter
      pending_json_data_.add(data);
      if (end_stream) {
        jsonrpc_filter_->onData(pending_json_data_, true);
        pending_json_data_.drain(pending_json_data_.length());
      }
    }
  }
  
  void onMessageComplete() override {
    // HTTP message complete
    if (!is_sse_mode_ && pending_json_data_.length() > 0) {
      // Process any remaining JSON-RPC data
      jsonrpc_filter_->onData(pending_json_data_, true);
      pending_json_data_.drain(pending_json_data_.length());
    }
  }
  
  void onError(const std::string& error) override {
    // HTTP protocol error
    Error mcp_error(jsonrpc::INTERNAL_ERROR, "HTTP error: " + error);
    mcp_callbacks_.onError(mcp_error);
  }
  
  // ===== SseCodecFilter::EventCallbacks =====
  
  void onEvent(const std::string& event,
               const std::string& data,
               const optional<std::string>& id) override {
    (void)event;
    (void)id;
    
    // SSE event contains JSON-RPC message
    // Forward to JSON-RPC filter
    auto buffer = std::make_unique<OwnedBuffer>();
    buffer->add(data);
    jsonrpc_filter_->onData(*buffer, false);
  }
  
  void onComment(const std::string& comment) override {
    // SSE comments are used for keep-alive, ignore
    (void)comment;
  }
  
  // ===== McpJsonRpcFilter::Callbacks =====
  
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
  
  // ===== Encoder Access =====
  
  HttpCodecFilter::MessageEncoder& httpEncoder() {
    return http_filter_->messageEncoder();
  }
  
  SseCodecFilter::EventEncoder& sseEncoder() {
    return sse_filter_->eventEncoder();
  }
  
  McpJsonRpcFilter::Encoder& jsonrpcEncoder() {
    return jsonrpc_filter_->encoder();
  }
  
private:
  event::Dispatcher& dispatcher_;
  McpMessageCallbacks& mcp_callbacks_;
  bool is_server_;
  bool is_sse_mode_{false};
  
  // Protocol filters
  std::shared_ptr<HttpCodecFilter> http_filter_;
  std::shared_ptr<SseCodecFilter> sse_filter_;
  std::shared_ptr<McpJsonRpcFilter> jsonrpc_filter_;
  
  // Filter callbacks
  network::ReadFilterCallbacks* read_callbacks_{nullptr};
  network::WriteFilterCallbacks* write_callbacks_{nullptr};
  
  // Buffered data
  OwnedBuffer pending_json_data_;
};

// ===== Factory Implementation =====

bool McpHttpFilterChainFactory::createFilterChain(
    network::FilterManager& filter_manager) const {
  
  // Following production pattern: create a single combined filter
  // that implements all the callback interfaces
  auto combined_filter = std::make_shared<McpHttpSseJsonRpcFilter>(
      dispatcher_, message_callbacks_, is_server_);
  
  // Add as both read and write filter
  filter_manager.addReadFilter(combined_filter);
  filter_manager.addWriteFilter(combined_filter);
  
  // Store for lifetime management
  filters_.push_back(combined_filter);
  
  return true;
}

bool McpHttpFilterChainFactory::createNetworkFilterChain(
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

} // namespace filter
} // namespace mcp