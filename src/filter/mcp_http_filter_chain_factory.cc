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
#include <sstream>
#include <ctime>

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
  
  // Create HTTP routing filter for arbitrary endpoints
  auto routing_filter = createHttpRoutingFilter(metrics_filter);
  if (routing_filter) {
    filter_manager.addReadFilter(routing_filter);
    filter_manager.addWriteFilter(routing_filter);
    filters_.push_back(routing_filter);
  }
  
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

std::shared_ptr<filter::HttpRoutingFilter> McpHttpFilterChainFactory::createHttpRoutingFilter(
    std::shared_ptr<filter::MetricsFilter> metrics_filter) const {
  
  auto routing_filter = std::make_shared<filter::HttpRoutingFilter>(dispatcher_, is_server_);
  
  // Register default handler that passes through unhandled requests
  // This allows JSON-RPC requests to /rpc and SSE connections to /events
  // to pass through to the appropriate protocol filters
  routing_filter->registerDefaultHandler(
      [](const filter::HttpRoutingFilter::RequestContext& req) {
    // For unhandled paths, return Continue to let the request
    // pass through the filter chain
    filter::HttpRoutingFilter::Response resp;
    
    // Special handling for JSON-RPC and SSE endpoints
    if (req.path == "/rpc" || req.path == "/events") {
      // Don't send a response - let it pass through to next filter
      resp.status_code = 0;  // Signal to continue processing
      return resp;
    }
    
    // For other paths, return 404
    resp.status_code = 404;
    resp.headers["content-type"] = "application/json";
    resp.body = "{\"error\":\"Not Found\",\"path\":\"" + req.path + "\"}";
    resp.headers["content-length"] = std::to_string(resp.body.length());
    return resp;
  });
  
  // Register health endpoint
  routing_filter->registerHandler("GET", "/health", 
      [](const filter::HttpRoutingFilter::RequestContext& req) {
    filter::HttpRoutingFilter::Response resp;
    resp.status_code = 200;
    resp.headers["content-type"] = "application/json";
    resp.headers["cache-control"] = "no-cache";
    
    resp.body = R"({
      "status": "healthy",
      "service": "mcp-server",
      "timestamp": )" + std::to_string(std::time(nullptr)) + R"(
    })";
    
    resp.headers["content-length"] = std::to_string(resp.body.length());
    return resp;
  });
  
  // Register metrics endpoint that queries the metrics filter
  if (metrics_filter) {
    routing_filter->registerHandler("GET", "/metrics",
        [metrics_filter](const filter::HttpRoutingFilter::RequestContext& req) {
      filter::HttpRoutingFilter::Response resp;
      resp.status_code = 200;
      resp.headers["content-type"] = "text/plain";
      
      // Get metrics from the metrics filter
      filter::ConnectionMetrics metrics;
      metrics_filter->getMetrics(metrics);
      
      // Format as Prometheus-style metrics
      std::stringstream ss;
      ss << "# HELP mcp_bytes_received_total Total bytes received\n";
      ss << "# TYPE mcp_bytes_received_total counter\n";
      ss << "mcp_bytes_received_total " << metrics.bytes_received << "\n\n";
      
      ss << "# HELP mcp_bytes_sent_total Total bytes sent\n";
      ss << "# TYPE mcp_bytes_sent_total counter\n";
      ss << "mcp_bytes_sent_total " << metrics.bytes_sent << "\n\n";
      
      ss << "# HELP mcp_messages_received_total Total messages received\n";
      ss << "# TYPE mcp_messages_received_total counter\n";
      ss << "mcp_messages_received_total " << metrics.messages_received << "\n\n";
      
      ss << "# HELP mcp_messages_sent_total Total messages sent\n";
      ss << "# TYPE mcp_messages_sent_total counter\n";
      ss << "mcp_messages_sent_total " << metrics.messages_sent << "\n\n";
      
      ss << "# HELP mcp_requests_received_total Total requests received\n";
      ss << "# TYPE mcp_requests_received_total counter\n";
      ss << "mcp_requests_received_total " << metrics.requests_received << "\n\n";
      
      ss << "# HELP mcp_responses_received_total Total responses received\n";
      ss << "# TYPE mcp_responses_received_total counter\n";
      ss << "mcp_responses_received_total " << metrics.responses_received << "\n\n";
      
      ss << "# HELP mcp_errors_total Total errors\n";
      ss << "# TYPE mcp_errors_total counter\n";
      ss << "mcp_errors_total " << (metrics.errors_received + metrics.protocol_errors) << "\n\n";
      
      // Latency metrics
      if (metrics.latency_samples > 0) {
        uint64_t avg_latency = metrics.total_latency_ms / metrics.latency_samples;
        ss << "# HELP mcp_latency_ms Request latency in milliseconds\n";
        ss << "# TYPE mcp_latency_ms summary\n";
        ss << "mcp_latency_ms{quantile=\"0\"} " << metrics.min_latency_ms << "\n";
        ss << "mcp_latency_ms{quantile=\"0.5\"} " << avg_latency << "\n";
        ss << "mcp_latency_ms{quantile=\"1\"} " << metrics.max_latency_ms << "\n";
        ss << "mcp_latency_ms_sum " << metrics.total_latency_ms << "\n";
        ss << "mcp_latency_ms_count " << metrics.latency_samples << "\n\n";
      }
      
      // Method-specific metrics
      if (!metrics.method_counts.empty()) {
        ss << "# HELP mcp_method_calls_total Total calls per method\n";
        ss << "# TYPE mcp_method_calls_total counter\n";
        for (const auto& pair : metrics.method_counts) {
          ss << "mcp_method_calls_total{method=\"" << pair.first << "\"} " << pair.second << "\n";
        }
        ss << "\n";
      }
      
      resp.body = ss.str();
      resp.headers["content-length"] = std::to_string(resp.body.length());
      return resp;
    });
  }
  
  // Register info endpoint
  routing_filter->registerHandler("GET", "/info",
      [](const filter::HttpRoutingFilter::RequestContext& req) {
    filter::HttpRoutingFilter::Response resp;
    resp.status_code = 200;
    resp.headers["content-type"] = "application/json";
    
    resp.body = R"({
      "server": "MCP Server",
      "protocols": ["http", "sse", "json-rpc"],
      "endpoints": {
        "health": "/health",
        "metrics": "/metrics",
        "info": "/info",
        "json_rpc": "/rpc",
        "sse_events": "/events"
      },
      "version": "1.0.0"
    })";
    
    resp.headers["content-length"] = std::to_string(resp.body.length());
    return resp;
  });
  
  // Register ready endpoint
  routing_filter->registerHandler("GET", "/ready",
      [](const filter::HttpRoutingFilter::RequestContext& req) {
    filter::HttpRoutingFilter::Response resp;
    resp.status_code = 200;
    resp.headers["content-type"] = "application/json";
    resp.body = "{\"ready\":true}";
    resp.headers["content-length"] = std::to_string(resp.body.length());
    return resp;
  });
  
  return routing_filter;
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