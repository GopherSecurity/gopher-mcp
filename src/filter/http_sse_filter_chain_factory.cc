/**
 * MCP HTTP+SSE Filter Chain Factory Implementation
 *
 * Following production architecture strictly:
 * - No separate adapter classes
 * - Filters implement callback interfaces directly
 * - Filter manager wires filters together
 * - Clean separation between protocol layers
 *
 * ============================================================================
 * DATA FLOW ARCHITECTURE
 * ============================================================================
 *
 * READING (Incoming Data):
 * Socket → ConnectionImpl::onReadReady() → doRead() → FilterManager::onData()
 * → HttpSseJsonRpcProtocolFilter::onData() → Protocol layers:
 *   1. HttpCodecFilter: Parse HTTP request/response
 *   2. SseCodecFilter: Parse SSE events (if SSE mode active)
 *   3. JsonRpcProtocolFilter: Parse JSON-RPC messages
 * → Callbacks to McpConnectionManager → Application
 *
 * WRITING (Outgoing Data):
 * Application → McpConnectionManager::sendRequest/Response() →
 * sendJsonMessage() → ConnectionImpl::write() → FilterManager::onWrite()
 * (REVERSE order) → HttpSseJsonRpcProtocolFilter::onWrite():
 *   - SSE mode: Format as SSE events with HTTP headers on first write
 *   - Normal mode: Pass through JsonRpcFilter → HttpCodecFilter
 * → ConnectionImpl::doWrite() → Socket
 *
 * SSE MODE DETECTION:
 * - Server: Accept header contains "text/event-stream"
 * - Client: Content-Type header contains "text/event-stream"
 *
 * SSE STREAMING:
 * - First response: HTTP/1.1 200 OK + SSE headers + first event
 * - Subsequent responses: SSE events only (data: {json}\n\n)
 * - Long-lived connection, multiple events over time
 *
 * THREAD SAFETY:
 * - Each connection has its own filter chain instance
 * - All operations for a connection happen in single dispatcher thread
 * - No locks needed, no race conditions
 *
 * CRITICAL RULES:
 * 1. Filters MUST modify buffer in-place in onWrite()
 * 2. NEVER call connection().write() from within onWrite() - infinite
 * recursion!
 * 3. Return FilterStatus::Continue to pass data to next filter/transport
 */

#include "mcp/filter/http_sse_filter_chain_factory.h"

#include <ctime>
#include <iostream>
#include <sstream>

#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/http_routing_filter.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/filter/metrics_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/json/json_serialization.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/stream_info/stream_info.h"

namespace mcp {
namespace filter {

// Forward declaration
class HttpSseJsonRpcProtocolFilter;

/**
 * Combined filter that implements all protocol layers
 * Following production pattern: one filter class can handle multiple protocols
 * by implementing the appropriate callback interfaces
 *
 * Threading model (following production pattern):
 * - Each connection is bound to a single dispatcher thread
 * - All operations for a connection happen in that thread
 * - No locks needed for stream management (single-threaded access)
 * - Responses are posted to dispatcher to ensure thread safety
 *
 * Now includes HTTP routing capability without double parsing
 */

// Note: Following production pattern, connections and filters are managed by
// the connection manager. The server/client should maintain the
// connection-to-filter mapping if direct access is needed. For now, we rely on
// the connection's filter chain for response routing.

// Utility function to convert RequestId to string for logging
static std::string requestIdToString(const RequestId& id) {
  if (holds_alternative<std::string>(id)) {
    return get<std::string>(id);
  } else if (holds_alternative<int64_t>(id)) {
    return std::to_string(get<int64_t>(id));
  }
  return "<unknown>";
}

// Following production architecture: Stream class for request/response pairs
// Each incoming request creates a new stream that tracks its own state
class RequestStream {
 public:
  RequestStream(RequestId id, HttpSseJsonRpcProtocolFilter* filter)
      : id_(id),
        filter_(filter),
        creation_time_(std::chrono::steady_clock::now()) {}

  RequestId id() const { return id_; }

  // Note: sendResponse implementation moved after HttpSseJsonRpcProtocolFilter
  // definition
  void sendResponse(const jsonrpc::Response& response);

  std::chrono::steady_clock::time_point creationTime() const {
    return creation_time_;
  }

 private:
  RequestId id_;
  HttpSseJsonRpcProtocolFilter* filter_;
  std::chrono::steady_clock::time_point creation_time_;
};

class HttpSseJsonRpcProtocolFilter
    : public network::Filter,
      public HttpCodecFilter::MessageCallbacks,
      public SseCodecFilter::EventCallbacks,
      public JsonRpcProtocolFilter::MessageHandler {
 public:
  // Make active_streams_ accessible for response routing
  friend void HttpSseFilterChainFactory::sendHttpResponse(
      const jsonrpc::Response&, network::Connection&);

  HttpSseJsonRpcProtocolFilter(event::Dispatcher& dispatcher,
                               McpProtocolCallbacks& mcp_callbacks,
                               bool is_server)
      : dispatcher_(dispatcher),
        mcp_callbacks_(mcp_callbacks),
        is_server_(is_server) {
    // Following production pattern: all operations for this filter
    // happen in the single dispatcher thread
    // Create routing filter first (it will receive HTTP callbacks)
    routing_filter_ = std::make_shared<HttpRoutingFilter>(
        this,     // We are the next callbacks layer after routing
        nullptr,  // Will be set after HTTP filter is created
        is_server_);

    // Create the protocol filters
    // Single HTTP codec that sends callbacks to routing filter first
    http_filter_ = std::make_shared<HttpCodecFilter>(*routing_filter_,
                                                     dispatcher_, is_server_);

    // Now set the encoder in routing filter
    routing_filter_->setEncoder(&http_filter_->messageEncoder());

    // Configure routing filter with health endpoint
    setupRoutingHandlers();

    // SSE and JSON-RPC filters for protocol-specific handling
    sse_filter_ =
        std::make_shared<SseCodecFilter>(*this, dispatcher_, is_server_);
    jsonrpc_filter_ =
        std::make_shared<JsonRpcProtocolFilter>(*this, dispatcher_, is_server_);
  }

  ~HttpSseJsonRpcProtocolFilter() = default;

  // ===== Network Filter Interface =====

  /**
   * READ DATA FLOW (Server receiving request, Client receiving response):
   *
   * 1. ConnectionImpl::onReadReady() - Socket has data available
   * 2. ConnectionImpl::doRead() - Read from socket into read_buffer_
   * 3. FilterManagerImpl::onData() - Pass data through read filter chain
   * 4. HttpSseJsonRpcProtocolFilter::onData() - This method, processes in
   * layers: a. HttpCodecFilter::onData() - Parse HTTP headers/body b.
   * SseCodecFilter::onData() - Parse SSE events (if SSE mode) c.
   * JsonRpcProtocolFilter::onData() - Parse JSON-RPC messages
   * 5. Callbacks propagate up to McpConnectionManager::onRequest/onResponse()
   *
   * Server flow: HTTP request → Extract JSON-RPC from body → Process request
   * Client flow: HTTP response → Parse SSE events (if SSE) → Extract JSON-RPC →
   * Process response
   */
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
    // Following production pattern: connection is bound to this thread
    // Store connection reference for response routing
    if (read_callbacks_) {
      connection_ = &read_callbacks_->connection();
    }

    // Initialize all protocol filters
    http_filter_->onNewConnection();
    sse_filter_->onNewConnection();
    jsonrpc_filter_->onNewConnection();
    return network::FilterStatus::Continue;
  }

  // filters should not call connection().write() from within onWrite() causing
  // infinite recursion. We need to write directly to the underlying socket
  // without going through the filter chain again. onWrite should modify the
  // buffer in-place and return Continue to let it flow to the next filter or
  // transport We shouldn't call connection().write() from within onWrite().
  /**
   * WRITE DATA FLOW (Server sending response, Client sending request):
   *
   * 1. McpConnectionManager::sendResponse/sendRequest() - Application initiates
   * write
   * 2. McpConnectionManager::sendJsonMessage() - Convert to JSON string
   * 3. ConnectionImpl::write() - Add to write_buffer_, trigger filter chain
   * 4. FilterManagerImpl::onWrite() - Pass through write filter chain in
   * REVERSE order
   * 5. HttpSseJsonRpcProtocolFilter::onWrite() - This method, processes based
   * on mode:
   *
   *    Server SSE mode (is_sse_mode_ == true):
   *    - First write: Add HTTP headers + format as SSE event
   *    - Subsequent writes: Format as SSE events only
   *    - Data flows directly to transport, bypassing other filters
   *
   *    Normal HTTP mode:
   *    a. JsonRpcProtocolFilter::onWrite() - Add JSON-RPC framing if configured
   *    b. HttpCodecFilter::onWrite() - Add HTTP headers/framing
   *
   * 6. ConnectionImpl::doWrite() - Write from write_buffer_ to socket
   *
   * Server flow: JSON-RPC response → SSE formatting (if SSE) → HTTP headers →
   * Socket Client flow: JSON-RPC request → HTTP POST formatting → Socket
   *
   * CRITICAL: Filters must modify buffer in-place and return Continue.
   * Never call connection().write() from within onWrite() - causes infinite
   * recursion!
   */
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    // Write flows through filters in reverse order
    // JSON-RPC -> SSE -> HTTP

    // Track SSE writes

    // In SSE mode for server, handle headers + data properly
    // Design: Use boolean flag to track if HTTP headers have been sent
    // This is safe because:
    // - Each connection has its own filter instance (connection-scoped)
    // - All operations happen in single dispatcher thread (no races)
    // - SSE connections are long-lived with one stream at a time
    if (is_server_ && is_sse_mode_) {
      // Check if we've written anything to this connection yet
      // If connection has no bytes sent, this is the first response
      bool is_first_write = false;
      if (write_callbacks_) {
        // Check connection's write buffer stats or bytes sent
        // For now, use a simple approach: if this is the first onWrite call
        // after entering SSE mode, we need headers
        // Better approach: query connection's bytes_sent metric
        is_first_write = !sse_headers_written_;
      }

      if (is_first_write) {
        // First SSE write - send headers

        // Build complete HTTP response with SSE headers and first event
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: text/event-stream\r\n";
        response << "Cache-Control: no-cache\r\n";
        response << "Connection: keep-alive\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "\r\n";  // End of headers

        // Format the JSON data as SSE event
        if (data.length() > 0) {
          size_t data_len = data.length();
          std::string json_data(
              static_cast<const char*>(data.linearize(data_len)), data_len);
          data.drain(data_len);

          response << "data: " << json_data << "\n\n";
        }

        // Replace buffer with complete response
        std::string response_str = response.str();
        data.add(response_str.c_str(), response_str.length());

        // Mark that headers have been written for this SSE connection
        // This is connection-scoped, not global
        sse_headers_written_ = true;
      } else {
        // Subsequent SSE events - just format as SSE without HTTP headers
        // Subsequent SSE write
        if (data.length() > 0) {
          size_t data_len = data.length();
          std::string json_data(
              static_cast<const char*>(data.linearize(data_len)), data_len);
          data.drain(data_len);

          // Format as SSE event
          std::ostringstream sse_event;
          sse_event << "data: " << json_data << "\n\n";
          std::string event_str = sse_event.str();

          // SSE event formatted

          // Replace buffer contents with SSE-formatted data
          data.add(event_str.c_str(), event_str.length());
        }
      }
      // Let the formatted data flow to transport
      return network::FilterStatus::Continue;
    }

    // Normal HTTP path (non-SSE responses)
    // JSON-RPC filter handles framing
    auto status = jsonrpc_filter_->onWrite(data, end_stream);
    if (status == network::FilterStatus::StopIteration) {
      return status;
    }

    // HTTP filter adds headers/framing for normal HTTP responses
    return http_filter_->onWrite(data, end_stream);
  }

  void initializeReadFilterCallbacks(
      network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    connection_ = &callbacks.connection();

    http_filter_->initializeReadFilterCallbacks(callbacks);
    sse_filter_->initializeReadFilterCallbacks(callbacks);
    jsonrpc_filter_->initializeReadFilterCallbacks(callbacks);
  }

  void initializeWriteFilterCallbacks(
      network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
    http_filter_->initializeWriteFilterCallbacks(callbacks);
    routing_filter_->setWriteCallbacks(
        &callbacks);  // Set callbacks for routing filter
    sse_filter_->initializeWriteFilterCallbacks(callbacks);
    jsonrpc_filter_->initializeWriteFilterCallbacks(callbacks);
  }

  // ===== HttpCodecFilter::MessageCallbacks =====

  /**
   * Called by HttpCodecFilter when HTTP headers are parsed
   * Server: Called when request headers received
   * Client: Called when response headers received
   *
   * This determines transport mode (SSE vs regular HTTP) based on headers
   */
  void onHeaders(const std::map<std::string, std::string>& headers,
                 bool keep_alive) override {
    // Process headers

    // Determine transport mode based on headers
    if (is_server_) {
      // Server: check Accept header for SSE
      auto accept = headers.find("accept");
      if (accept != headers.end() &&
          accept->second.find("text/event-stream") != std::string::npos) {
        is_sse_mode_ = true;
        // Server detected SSE mode from Accept header
        // Headers will be sent with first data in onWrite()
        // This avoids calling connection().write() from onHeaders()
        sse_filter_->startEventStream();
      } else {
        is_sse_mode_ = false;
        // Server using normal HTTP mode
      }
    } else {
      // Client: check Content-Type for SSE
      auto content_type = headers.find("content-type");
      is_sse_mode_ =
          content_type != headers.end() &&
          content_type->second.find("text/event-stream") != std::string::npos;

      if (is_sse_mode_) {
        // Client entering SSE mode - start event stream parser
        sse_filter_->startEventStream();
      }
    }
  }

  void onBody(const std::string& data, bool end_stream) override {
    // Server receives JSON-RPC in request body regardless of SSE mode
    // SSE mode only affects the response format
    if (is_server_) {
      // Server always receives JSON-RPC in request body
      pending_json_data_.add(data);
      if (end_stream) {
        jsonrpc_filter_->onData(pending_json_data_, true);
        pending_json_data_.drain(pending_json_data_.length());
      }
    } else {
      // Client mode: parse based on content type
      if (is_sse_mode_) {
        // In SSE mode, body contains event stream
        // SSE events can span multiple chunks, accumulate in buffer
        pending_sse_data_.add(data);
        // Parse SSE events - the parser will handle partial events
        sse_filter_->onData(pending_sse_data_, end_stream);
        // SSE filter drains what it consumes, keeping partial events
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

  // ===== JsonRpcProtocolFilter::MessageHandler =====

  /**
   * Called by JsonRpcProtocolFilter when a complete JSON-RPC request is parsed
   * Creates a RequestStream to track this request-response pair
   * Server only - clients don't receive requests
   */
  void onRequest(const jsonrpc::Request& request) override {
    // Following production pattern: create a new stream for each request
    // This supports HTTP pipelining with multiple concurrent requests
    // Executed in dispatcher thread - no synchronization needed
    auto stream = std::make_unique<RequestStream>(request.id, this);
    std::string id_key = requestIdToString(request.id);
    active_streams_[id_key] = std::move(stream);

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

  JsonRpcProtocolFilter::Encoder& jsonrpcEncoder() {
    return jsonrpc_filter_->encoder();
  }

  // Method to send response through this filter instance
  // Send response for a specific stream
  // Following production pattern: called only in dispatcher thread context
  void sendResponseForStream(const jsonrpc::Response& response,
                             RequestId stream_id) {
    std::string id_key = requestIdToString(stream_id);

    // No locks needed - single threaded per connection
    auto it = active_streams_.find(id_key);
    if (it != active_streams_.end()) {
      sendResponseThroughFilter(response);
      active_streams_.erase(it);
    } else {
      // Following production pattern: no stream means request was already
      // completed or never existed Drop the response - do NOT send it (would
      // violate HTTP protocol)
      std::cerr << "[ERROR] No stream found for response ID " << id_key
                << " - dropping response (possible duplicate or late response)"
                << std::endl;
      // NO FALLBACK - just return
    }
  }

  /*
  1. McpServer receives request and calls conn_manager->sendResponse(response)
  2. McpConnectionManager::sendResponse converts to JSON and calls
  sendJsonMessage
  3. sendJsonMessage calls active_connection_->write(*buffer, false)
  4. This goes through the filter chain's onWrite() methods
  5. The filter should format the data in onWrite(), not initiate writes
  */
  void sendResponseThroughFilter(const jsonrpc::Response& response) {
    // DEAD CODE - This method is never called!
    // RequestStream::sendResponse is never invoked by anyone.
    // The actual response flow is:
    // 1. McpServer calls current_connection_->write() directly
    // 2. This triggers onWrite() which formats the data
    // 3. The formatted data is written to the socket
    //
    // This entire RequestStream mechanism is unused and should be removed.
    std::cerr
        << "[WARNING] sendResponseThroughFilter called - this is dead code!"
        << std::endl;
  }

 private:
  void setupRoutingHandlers() {
    // Register health endpoint
    routing_filter_->registerHandler(
        "GET", "/health", [](const HttpRoutingFilter::RequestContext& req) {
          HttpRoutingFilter::Response resp;
          resp.status_code = 200;
          resp.headers["content-type"] = "application/json";
          resp.headers["cache-control"] = "no-cache";

          resp.body = R"({"status":"healthy","timestamp":)" +
                      std::to_string(std::time(nullptr)) + "}";

          resp.headers["content-length"] = std::to_string(resp.body.length());
          return resp;
        });

    // Don't register /rpc endpoint - it will pass through to this filter
    // Only register endpoints that should be handled by routing filter

    // Register info endpoint
    routing_filter_->registerHandler(
        "GET", "/info", [](const HttpRoutingFilter::RequestContext& req) {
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
  McpProtocolCallbacks& mcp_callbacks_;
  bool is_server_;
  bool is_sse_mode_{false};
  bool sse_headers_written_{
      false};  // Track if HTTP headers sent for SSE stream

  // Protocol filters
  std::shared_ptr<HttpCodecFilter> http_filter_;
  std::shared_ptr<HttpRoutingFilter>
      routing_filter_;  // Routing filter (shared for lifetime management)
  std::shared_ptr<SseCodecFilter> sse_filter_;
  std::shared_ptr<JsonRpcProtocolFilter> jsonrpc_filter_;

  // Filter callbacks
  network::ReadFilterCallbacks* read_callbacks_{nullptr};
  network::WriteFilterCallbacks* write_callbacks_{nullptr};

  // Stream management - following production pattern
  // Multiple concurrent requests per connection (HTTP pipelining support)
  // Using string key since RequestId is a variant without comparison operators
  // All access happens in the single dispatcher thread - no locks needed
  std::map<std::string, std::unique_ptr<RequestStream>> active_streams_;

  // Connection reference for response routing
  network::Connection* connection_{nullptr};

  // Buffered data
  OwnedBuffer pending_json_data_;
  OwnedBuffer pending_sse_data_;  // For accumulating SSE event stream data
};

// RequestStream method implementation (after HttpSseJsonRpcProtocolFilter
// definition)
void RequestStream::sendResponse(const jsonrpc::Response& response) {
  // Each stream knows how to send its own response
  if (filter_) {
    filter_->sendResponseForStream(response, id_);
  }
}

// Static method to send response through the connection's filter chain
// Following production pattern: ensure execution in the connection's dispatcher
// thread
void HttpSseFilterChainFactory::sendHttpResponse(
    const jsonrpc::Response& response, network::Connection& connection) {
  std::cerr << "[WARNING] HttpSseFilterChainFactory::sendHttpResponse called "
               "but filter access not available"
            << std::endl;
  std::cerr << "[WARNING] Response ID " << requestIdToString(response.id)
            << " dropped - no direct filter access" << std::endl;

  // Following production pattern: without direct filter access, we cannot route
  // responses The proper solution is for the connection manager to maintain
  // filter references and provide a proper API for response routing. For now,
  // responses must be sent through the connection manager's own mechanisms.
}

// ===== Factory Implementation =====

bool HttpSseFilterChainFactory::createFilterChain(
    network::FilterManager& filter_manager) const {
  // Following production pattern: create filters in order
  // 1. HTTP Routing Filter (handles arbitrary HTTP endpoints)
  // 2. Combined Protocol Filter (HTTP/SSE/JSON-RPC)
  // 3. Metrics Filter (collects statistics)

  // Create metrics filter if enabled
  std::shared_ptr<filter::MetricsFilter> metrics_filter;
  if (enable_metrics_) {
    // Create simple metrics callbacks
    class SimpleMetricsCallbacks
        : public filter::MetricsFilter::MetricsCallbacks {
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

    auto metrics_callbacks = std::make_shared<SimpleMetricsCallbacks>();
    filter::MetricsFilter::Config metrics_config;
    metrics_config.track_methods = true;

    metrics_filter = std::make_shared<filter::MetricsFilter>(metrics_callbacks,
                                                             metrics_config);
    filter_manager.addReadFilter(metrics_filter);
    filter_manager.addWriteFilter(metrics_filter);
    filters_.push_back(metrics_filter);
  }

  // Routing is now integrated into the combined filter
  // No separate routing filter needed

  // Create the combined protocol filter
  auto combined_filter = std::make_shared<HttpSseJsonRpcProtocolFilter>(
      dispatcher_, message_callbacks_, is_server_);

  // Add as both read and write filter
  filter_manager.addReadFilter(combined_filter);
  filter_manager.addWriteFilter(combined_filter);

  // Store for lifetime management
  filters_.push_back(combined_filter);

  return true;
}

// Removed createHttpRoutingFilter - routing is now integrated in the combined
// filter

bool HttpSseFilterChainFactory::createNetworkFilterChain(
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
