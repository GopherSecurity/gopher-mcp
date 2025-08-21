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
#include "mcp/filter/http_routing_filter.h"
#include "mcp/filter/metrics_filter.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/json/json_serialization.h"
#include <iostream>
#include <sstream>
#include <ctime>

namespace mcp {
namespace filter {

// Forward declaration
class McpHttpSseJsonRpcFilter;

/**
 * Combined filter that implements all protocol layers
 * Following production pattern: one filter class can handle multiple protocols
 * by implementing the appropriate callback interfaces
 * 
 * Now includes HTTP routing capability without double parsing
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
    // Create routing filter first (it will receive HTTP callbacks)
    routing_filter_ = std::make_shared<HttpRoutingFilter>(
        this,  // We are the next callbacks layer after routing
        nullptr,  // Will be set after HTTP filter is created
        is_server_);
    
    // Create the protocol filters
    // Single HTTP codec that sends callbacks to routing filter first
    http_filter_ = std::make_shared<HttpCodecFilter>(*routing_filter_, dispatcher_, is_server_);
    
    // Now set the encoder in routing filter
    routing_filter_->setEncoder(&http_filter_->messageEncoder());
    
    // Configure routing filter with health endpoint
    setupRoutingHandlers();
    
    // SSE and JSON-RPC filters for protocol-specific handling
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
    std::cerr << "[DEBUG] McpHttpSseJsonRpcFilter::onHeaders called, is_server=" 
              << is_server_ << std::endl;
    for (const auto& h : headers) {
      std::cerr << "[DEBUG]   " << h.first << ": " << h.second << std::endl;
    }
    
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
    std::cerr << "[DEBUG] McpHttpSseJsonRpcFilter::onBody called with " 
              << data.length() << " bytes, end_stream=" << end_stream 
              << ", is_sse_mode=" << is_sse_mode_ << std::endl;
    
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
      std::cerr << "[DEBUG] Accumulated " << pending_json_data_.length() 
                << " bytes of JSON-RPC data" << std::endl;
      if (end_stream) {
        std::cerr << "[DEBUG] End of stream, processing JSON-RPC data" << std::endl;
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
    // Store the current filter context for response routing
    // Following production pattern: maintain request-response context
    current_request_filter_ = this;
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
  
  // Method to send response through the current request's filter
  void sendResponseThroughFilter(const jsonrpc::Response& response) {
    std::cerr << "[DEBUG] Sending response through filter chain" << std::endl;
    
    // For HTTP, we need to send headers first, then body
    if (is_server_ && !is_sse_mode_) {
      // Convert response to JSON string first
      auto json_val = json::to_json(response);
      std::string json_str = json_val.toString();
      
      // Prepare HTTP headers with content length
      std::map<std::string, std::string> headers;
      headers["content-type"] = "application/json";
      headers["cache-control"] = "no-cache";
      headers["content-length"] = std::to_string(json_str.length());
      
      // Send HTTP headers (end_stream=false because body follows)
      http_filter_->messageEncoder().encodeHeaders("200 OK", headers, false);
      
      // Send JSON-RPC response as HTTP body
      OwnedBuffer body_buffer;
      body_buffer.add(json_str);
      http_filter_->messageEncoder().encodeData(body_buffer, true);
    } else {
      // For SSE or other modes, just encode the JSON-RPC response
      jsonrpcEncoder().encodeResponse(response);
    }
  }
  
private:
  void setupRoutingHandlers() {
    // Register health endpoint
    routing_filter_->registerHandler("GET", "/health", 
        [](const HttpRoutingFilter::RequestContext& req) {
      HttpRoutingFilter::Response resp;
      resp.status_code = 200;
      resp.headers["content-type"] = "application/json";
      resp.headers["cache-control"] = "no-cache";
      
      resp.body = R"({"status":"healthy","timestamp":)" + 
                  std::to_string(std::time(nullptr)) + "}";
      
      resp.headers["content-length"] = std::to_string(resp.body.length());
      return resp;
    });
    
    // Register info endpoint
    routing_filter_->registerHandler("GET", "/info",
        [](const HttpRoutingFilter::RequestContext& req) {
      HttpRoutingFilter::Response resp;
      resp.status_code = 200;
      resp.headers["content-type"] = "application/json";
      
      resp.body = R"({
        "server": "MCP Server",
        "protocols": ["http", "sse", "json-rpc"],
        "endpoints": {
          "health": "/health",
          "info": "/info",
          "json_rpc": "/rpc",
          "sse_events": "/events"
        },
        "version": "1.0.0"
      })";
      
      resp.headers["content-length"] = std::to_string(resp.body.length());
      return resp;
    });
    
    // Default handler passes through to MCP protocol handling
    routing_filter_->registerDefaultHandler(
        [](const HttpRoutingFilter::RequestContext& req) {
      // Return status 0 to indicate pass-through for MCP endpoints
      HttpRoutingFilter::Response resp;
      resp.status_code = 0;
      return resp;
    });
  }
  
  event::Dispatcher& dispatcher_;
  McpMessageCallbacks& mcp_callbacks_;
  bool is_server_;
  bool is_sse_mode_{false};
  
  // Protocol filters
  std::shared_ptr<HttpCodecFilter> http_filter_;
  std::shared_ptr<HttpRoutingFilter> routing_filter_;  // Routing filter (shared for lifetime management)
  std::shared_ptr<SseCodecFilter> sse_filter_;
  std::shared_ptr<McpJsonRpcFilter> jsonrpc_filter_;
  
  // Filter callbacks
  network::ReadFilterCallbacks* read_callbacks_{nullptr};
  network::WriteFilterCallbacks* write_callbacks_{nullptr};
  
  // Buffered data
  OwnedBuffer pending_json_data_;
  
  // Thread-local storage for current request filter
  // Following production pattern: maintain request context for response routing
  static thread_local McpHttpSseJsonRpcFilter* current_request_filter_;
};

// Define thread-local storage for request context
thread_local McpHttpSseJsonRpcFilter* McpHttpSseJsonRpcFilter::current_request_filter_ = nullptr;

// Static method to send response through the current request's filter chain
void McpHttpFilterChainFactory::sendHttpResponse(const jsonrpc::Response& response) {
  if (McpHttpSseJsonRpcFilter::current_request_filter_) {
    McpHttpSseJsonRpcFilter::current_request_filter_->sendResponseThroughFilter(response);
  } else {
    std::cerr << "[ERROR] No current request filter to send response!" << std::endl;
  }
}

// ===== Factory Implementation =====

bool McpHttpFilterChainFactory::createFilterChain(
    network::FilterManager& filter_manager) const {
  
  // Following production pattern: create filters in order
  // 1. HTTP Routing Filter (handles arbitrary HTTP endpoints)
  // 2. Combined Protocol Filter (HTTP/SSE/JSON-RPC)
  // 3. Metrics Filter (collects statistics)
  
  // Create metrics filter if enabled
  std::shared_ptr<filter::MetricsFilter> metrics_filter;
  if (enable_metrics_) {
    // Create simple metrics callbacks
    class SimpleMetricsCallbacks : public filter::MetricsFilter::MetricsCallbacks {
    public:
      void onMetricsUpdate(const filter::ConnectionMetrics& metrics) override {
        // Could log or expose metrics here
      }
      void onThresholdExceeded(const std::string& metric_name,
                               uint64_t value,
                               uint64_t threshold) override {
        // Could alert on threshold violations
      }
    };
    
    static SimpleMetricsCallbacks metrics_callbacks;
    filter::MetricsFilter::Config metrics_config;
    metrics_config.track_methods = true;
    
    metrics_filter = std::make_shared<filter::MetricsFilter>(
        metrics_callbacks, metrics_config);
    filter_manager.addReadFilter(metrics_filter);
    filter_manager.addWriteFilter(metrics_filter);
    filters_.push_back(metrics_filter);
  }
  
  // Routing is now integrated into the combined filter
  // No separate routing filter needed
  
  // Create the combined protocol filter
  auto combined_filter = std::make_shared<McpHttpSseJsonRpcFilter>(
      dispatcher_, message_callbacks_, is_server_);
  
  // Add as both read and write filter
  filter_manager.addReadFilter(combined_filter);
  filter_manager.addWriteFilter(combined_filter);
  
  // Store for lifetime management
  filters_.push_back(combined_filter);
  
  return true;
}

// Removed createHttpRoutingFilter - routing is now integrated in the combined filter

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