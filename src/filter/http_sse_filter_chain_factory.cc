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

#include <cassert>
#include <cstdint>
#include <ctime>
#include <map>
#include <sstream>
#include <utility>

#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/http_routing_filter.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/filter/metrics_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/filter/sse_session_registry.h"
#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/stream_info/stream_info.h"

namespace mcp {
namespace filter {

// SseSessionRegistry is defined in mcp/filter/sse_session_registry.h so
// unit tests can exercise it directly without going through the full
// filter chain.

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

  HttpSseJsonRpcProtocolFilter(
      event::Dispatcher& dispatcher,
      McpProtocolCallbacks& mcp_callbacks,
      bool is_server,
      const std::string& http_path = "/rpc",
      const std::string& http_host = "localhost",
      bool use_sse = true,
      const HttpRouteRegistrationCallback& route_callback = nullptr,
      const std::string& configured_sse_path = "/sse",
      const std::string& configured_rpc_path = "/mcp",
      const std::string& configured_external_url = "",
      SseSessionRegistry* sse_registry = nullptr)
      : dispatcher_(dispatcher),
        mcp_callbacks_(mcp_callbacks),
        is_server_(is_server),
        http_path_(http_path),
        http_host_(http_host),
        use_sse_(use_sse),
        configured_sse_path_(configured_sse_path),
        configured_rpc_path_(configured_rpc_path),
        configured_external_url_(configured_external_url),
        sse_registry_(sse_registry),
        route_registration_callback_(route_callback) {
    // Following production pattern: all operations for this filter
    // happen in the single dispatcher thread
    // Create routing filter first (it will receive HTTP callbacks)
    routing_filter_ = std::make_shared<HttpRoutingFilter>(
        this,     // We are the next callbacks layer after routing
        nullptr,  // Will be set after HTTP filter is created
        is_server_);

    // Create the protocol filters
    // Single HTTP codec that sends callbacks to routing filter first
    GOPHER_LOG_DEBUG(
        "HttpSseJsonRpcProtocolFilter: Creating HttpCodecFilter with "
        "is_server={}",
        is_server_);
    http_filter_ = std::make_shared<HttpCodecFilter>(*routing_filter_,
                                                     dispatcher_, is_server_);

    // Set client endpoint for HTTP requests
    if (!is_server) {
      http_filter_->setClientEndpoint(http_path, http_host);
      // Only enable SSE GET mode if use_sse is true
      // For Streamable HTTP, we send POST requests directly
      if (use_sse) {
        http_filter_->setUseSseGet(true);
      }
    }

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

  ~HttpSseJsonRpcProtocolFilter() {
    // SSE stream connection is closing — drop this session from the
    // registry so a POST /callback/{id} that arrives between close and
    // destructor doesn't route a response into a dead connection. Runs
    // on the dispatcher thread (filters destruct on-thread by contract).
    if (sse_registry_ && !sse_session_id_.empty()) {
      sse_registry_->removeSession(sse_session_id_);
      sse_session_id_.clear();
    }
  }

  // ===== Network Filter Interface =====

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

    // For client mode with SSE, mark that we need to send GET request
    // Don't send here - connection is not ready yet (SSL handshake pending)
    // The GET will be sent on first onWrite() call after connection is
    // established For Streamable HTTP mode (use_sse_ = false), skip the SSE
    // endpoint waiting
    if (!is_server_ && use_sse_) {
      waiting_for_sse_endpoint_ = true;
    }

    return network::FilterStatus::Continue;
  }

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
    // SSE handshake bypass: we're inside a connection().write() call
    // made from our own onHeaders (writing "HTTP/1.1 200 OK ... event:
    // endpoint" for GET /sse, or "HTTP/1.1 202 Accepted" for POST
    // /callback). The bytes are already fully-formed HTTP — skip all
    // downstream JSON-RPC / HTTP-codec framing.
    if (sse_writing_handshake_) {
      return network::FilterStatus::Continue;
    }

    // SSE callback routing: the JSON-RPC filter is emitting the response
    // for a POST /callback/{id}. We already sent 202 Accepted on that
    // POST connection, so we must NOT let these bytes continue down the
    // write chain to be framed as HTTP and written back to the POST
    // connection. Instead, pull the JSON body out and hand it to the
    // registry, which writes it through the matching SSE stream.
    if (is_server_ && !sse_callback_session_id_.empty() && data.length() > 0) {
      const size_t len = data.length();
      std::string json_data(static_cast<const char*>(data.linearize(len)), len);
      data.drain(len);

      if (sse_registry_) {
        sse_registry_->sendResponse(sse_callback_session_id_, json_data);
      } else {
        GOPHER_LOG_WARN(
            "SSE callback response dropped: no registry available (session={})",
            sse_callback_session_id_);
      }

      // Stop iteration so ConnectionImpl doesn't flush an empty buffer
      // as HTTP bytes on the POST connection.
      return network::FilterStatus::StopIteration;
    }

    GOPHER_LOG_DEBUG(
        "HttpSseJsonRpcProtocolFilter: onWrite called, data_len={}, "
        "is_server={}, is_sse_mode={}, waiting_for_endpoint={}, "
        "sse_get_sent={}",
        data.length(), is_server_, is_sse_mode_, waiting_for_sse_endpoint_,
        http_filter_->hasSentSseGetRequest());

    // Client mode: handle SSE GET initialization
    if (!is_server_ && waiting_for_sse_endpoint_) {
      // First write after connection - send SSE GET request first
      if (!http_filter_->hasSentSseGetRequest()) {
        GOPHER_LOG_DEBUG(
            "HttpSseJsonRpcProtocolFilter: Sending SSE GET request first");

        // Send empty buffer to trigger SSE GET in http_filter_
        OwnedBuffer get_buffer;
        GOPHER_LOG_DEBUG(
            "HttpSseJsonRpcProtocolFilter: Calling http_filter_->onWrite() for "
            "GET");
        auto result = http_filter_->onWrite(get_buffer, false);
        GOPHER_LOG_DEBUG(
            "HttpSseJsonRpcProtocolFilter: http_filter_->onWrite() returned, "
            "get_buffer.length()={}",
            get_buffer.length());

        // The GET request is now in get_buffer - we need to send it
        // AND queue the current message to send after endpoint is received
        if (data.length() > 0) {
          GOPHER_LOG_DEBUG(
              "HttpSseJsonRpcProtocolFilter: Queuing message while waiting for "
              "SSE endpoint");
          OwnedBuffer msg_copy;
          size_t len = data.length();
          msg_copy.add(static_cast<const char*>(data.linearize(len)), len);
          pending_messages_.push_back(std::move(msg_copy));
          data.drain(len);
        }

        // Replace buffer contents with the GET request
        if (get_buffer.length() > 0) {
          size_t get_len = get_buffer.length();
          data.add(static_cast<const char*>(get_buffer.linearize(get_len)),
                   get_len);
        }

        // Return Continue so the GET request is written to socket
        return network::FilterStatus::Continue;
      }

      // GET already sent, but still waiting for endpoint - queue the message
      if (data.length() > 0) {
        GOPHER_LOG_DEBUG(
            "HttpSseJsonRpcProtocolFilter: Queuing message - waiting for SSE "
            "endpoint");
        OwnedBuffer msg_copy;
        size_t len = data.length();
        msg_copy.add(static_cast<const char*>(data.linearize(len)), len);
        pending_messages_.push_back(std::move(msg_copy));
        data.drain(len);  // Consume the data so it doesn't get written yet
        return network::FilterStatus::StopIteration;
      }
    }

    // Client mode with SSE active: send via separate POST connection
    // The SSE connection is for receiving only - POSTs must go separately
    if (!is_server_ && is_sse_mode_ && !waiting_for_sse_endpoint_ &&
        http_filter_->hasMessageEndpoint() && data.length() > 0) {
      GOPHER_LOG_DEBUG(
          "HttpSseJsonRpcProtocolFilter: Client SSE mode - sending via POST "
          "connection");
      size_t len = data.length();
      std::string json_body(static_cast<const char*>(data.linearize(len)), len);
      data.drain(len);  // Consume the data

      // Send via separate POST connection
      if (!mcp_callbacks_.sendHttpPost(json_body)) {
        GOPHER_LOG_ERROR(
            "HttpSseJsonRpcProtocolFilter: sendHttpPost failed for: {}",
            json_body.substr(0, std::min(len, (size_t)100)));
      }
      // Return StopIteration - we've handled the data via POST, don't write to
      // SSE
      return network::FilterStatus::StopIteration;
    }

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
    GOPHER_LOG_DEBUG(
        "HttpSseJsonRpcProtocolFilter::onWrite - data_len={} is_server={}",
        data.length(), is_server_);
    auto status = jsonrpc_filter_->onWrite(data, end_stream);
    if (status == network::FilterStatus::StopIteration) {
      return status;
    }

    GOPHER_LOG_DEBUG(
        "HttpSseJsonRpcProtocolFilter::onWrite - calling http_filter");
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
      // Pull :method and :path out of the pseudo-headers so we can branch
      // on the two SSE transport endpoints: GET {sse_path} (open stream)
      // and POST /callback/{session_id} (route response via stream).
      std::string method = "GET";
      auto method_it = headers.find(":method");
      if (method_it != headers.end()) {
        method = method_it->second;
      }

      std::string path = "/";
      auto path_it = headers.find(":path");
      if (path_it != headers.end()) {
        path = path_it->second;
      } else {
        // Some codecs surface the request target as "url" rather than the
        // HTTP/2-style :path pseudo-header. Accept either.
        auto url_it = headers.find("url");
        if (url_it != headers.end()) {
          path = url_it->second;
        }
      }
      // Trim query string before matching — the SSE transport doesn't
      // use query params on either endpoint and we want /sse?foo=bar to
      // still open the stream.
      size_t qpos = path.find('?');
      if (qpos != std::string::npos) {
        path = path.substr(0, qpos);
      }

      // ── GET {configured_sse_path_} → open an SSE stream.
      if (method == "GET" && path == configured_sse_path_ && sse_registry_ &&
          write_callbacks_) {
        client_accepts_sse_ = true;
        sse_session_id_ =
            sse_registry_->registerSession(&write_callbacks_->connection());

        // Build the callback URL the client will POST future requests
        // to. If an external URL is configured (reverse-proxy case) we
        // announce an absolute URL so the client doesn't try to guess.
        // Otherwise emit a relative path — it resolves relative to the
        // SSE URL on the client side and keeps us agnostic to scheme or
        // host.
        std::string callback_url;
        if (!configured_external_url_.empty()) {
          std::string base = configured_external_url_;
          if (!base.empty() && base.back() == '/') {
            base.pop_back();
          }
          callback_url = base + "/callback/" + sse_session_id_;
        } else {
          callback_url = "callback/" + sse_session_id_;
        }

        // Write the SSE response prelude + endpoint event straight onto
        // the wire. We set sse_writing_handshake_ so our own onWrite
        // passes these bytes through untouched — otherwise the write
        // filter chain would try to re-frame the raw HTTP bytes as an
        // SSE event and corrupt the stream.
        std::ostringstream sse_response;
        sse_response << "HTTP/1.1 200 OK\r\n";
        sse_response << "Content-Type: text/event-stream\r\n";
        sse_response << "Cache-Control: no-cache\r\n";
        sse_response << "Access-Control-Allow-Origin: *\r\n";
        sse_response << "\r\n";
        sse_response << "event: endpoint\n";
        sse_response << "data: " << callback_url << "\n\n";

        const std::string response_str = sse_response.str();
        OwnedBuffer response_buffer;
        response_buffer.add(response_str.c_str(), response_str.length());

        sse_writing_handshake_ = true;
        write_callbacks_->connection().write(response_buffer, false);
        sse_writing_handshake_ = false;

        sse_server_mode_ = true;
        is_sse_mode_ = true;
        sse_headers_written_ = true;

        GOPHER_LOG_INFO("SSE stream opened: session={} callback_url={}",
                        sse_session_id_, callback_url);
        return;
      }

      // ── POST .../callback/{session_id} → route the JSON-RPC body
      // through the SSE stream registered under {session_id}. We send
      // 202 Accepted on this POST connection right away and let the
      // body keep flowing into the JSON-RPC filter normally; onWrite
      // then intercepts the response before it gets written back to
      // this POST connection and redirects it to the SSE stream.
      //
      // Use rfind to accept a path prefix so deployments behind a
      // reverse proxy still match. If external_url announces a
      // callback at /v1/mcp/gateways/xyz/callback/client_1 and the
      // proxy passes that full path through, we still want to strip
      // everything up to and including /callback/ and take the
      // session ID from the tail.
      const std::string callback_prefix = "/callback/";
      const auto cb_pos = path.rfind(callback_prefix);
      if (method == "POST" && cb_pos != std::string::npos) {
        sse_callback_session_id_ = path.substr(cb_pos + callback_prefix.size());
        GOPHER_LOG_DEBUG("SSE callback POST: session={}",
                         sse_callback_session_id_);

        if (write_callbacks_) {
          const std::string http_202 =
              "HTTP/1.1 202 Accepted\r\n"
              "Content-Length: 0\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "\r\n";
          OwnedBuffer resp_buf;
          resp_buf.add(http_202.c_str(), http_202.length());
          sse_writing_handshake_ = true;
          write_callbacks_->connection().write(resp_buf, false);
          sse_writing_handshake_ = false;
        }
        // Keep is_sse_mode_ off on this connection — the body is plain
        // JSON-RPC, not an SSE stream. Response routing happens in
        // onWrite via sse_callback_session_id_.
        is_sse_mode_ = false;
        return;
      }

      // Non-SSE-transport request (POST /mcp Streamable HTTP, /health,
      // /info, etc.). Preserve the existing behavior: check Accept for
      // SSE capability but keep plain HTTP mode for request/response.
      auto accept = headers.find("accept");
      if (accept != headers.end() &&
          accept->second.find("text/event-stream") != std::string::npos) {
        client_accepts_sse_ = true;
        GOPHER_LOG_DEBUG("HttpSseJsonRpcProtocolFilter: client accepts SSE");
      }
      is_sse_mode_ = false;
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
    // The long-lived GET /sse request has no request body — but if the
    // codec surfaces any trailing bytes we don't want to push them down
    // into the JSON-RPC parser. Ignore bodies on the SSE stream
    // connection entirely.
    if (is_server_ && sse_server_mode_) {
      return;
    }
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
        // In Streamable HTTP mode, body contains JSON-RPC response
        // Process each chunk immediately - the HTTP codec may call onBody
        // multiple times
        OwnedBuffer temp_buffer;
        temp_buffer.add(data);
        // Add newline for JSON-RPC parsing (expects newline-delimited messages)
        if (!data.empty() && data.back() != '\n') {
          temp_buffer.add("\n", 1);
        }
        jsonrpc_filter_->onData(temp_buffer, end_stream);
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
    GOPHER_LOG_DEBUG(
        "HttpSseJsonRpcProtocolFilter: onEvent: event={}, data_len={}", event,
        data.size());

    (void)id;  // Event ID not currently used

    // Handle special MCP SSE events
    if (event == "endpoint") {
      // Server is telling us the endpoint URL for POST requests
      GOPHER_LOG_DEBUG(
          "HttpSseJsonRpcProtocolFilter: Received endpoint event: {}", data);
      http_filter_->setMessageEndpoint(data);
      waiting_for_sse_endpoint_ = false;

      // Notify McpConnectionManager about the message endpoint
      // This allows it to set up separate POST connections
      mcp_callbacks_.onMessageEndpoint(data);

      // Process any queued messages now that we have the endpoint
      // Use dispatcher to defer the write to avoid re-entrancy issues
      // (we're currently inside an onData callback)
      dispatcher_.post([this]() {
        GOPHER_LOG_DEBUG(
            "HttpSseJsonRpcProtocolFilter: Deferred: processing pending "
            "messages");
        processPendingMessages();
      });
      return;
    }

    if (event == "message") {
      // SSE message event contains JSON-RPC message
      // Forward to JSON-RPC filter
      auto buffer = std::make_unique<OwnedBuffer>();
      buffer->add(data);
      // Add trailing newline if missing for newline-delimited parsing
      if (!data.empty() && data.back() != '\n') {
        buffer->add("\n", 1);
      }
      jsonrpc_filter_->onData(*buffer, false);
      return;
    }

    // Default: treat data as JSON-RPC message (for backwards compatibility)
    if (!data.empty()) {
      auto buffer = std::make_unique<OwnedBuffer>();
      buffer->add(data);
      // CRITICAL FIX: JSON-RPC filter expects newline-delimited messages.
      // Add trailing newline if missing, otherwise the message will stay
      // in the partial buffer waiting for more data indefinitely.
      if (data.back() != '\n') {
        buffer->add("\n", 1);
      }
      jsonrpc_filter_->onData(*buffer, false);
    }
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
    GOPHER_LOG_DEBUG("HttpSseFilter::onRequest for method: {}", request.method);
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

    // For HTTP transport, send HTTP 202 Accepted response
    // JSON-RPC notifications don't have responses, but HTTP requires one
    if (is_server_ && write_callbacks_) {
      // Build minimal HTTP 202 response
      std::string http_response =
          "HTTP/1.1 202 Accepted\r\n"
          "Content-Length: 0\r\n"
          "Access-Control-Allow-Origin: *\r\n"
          "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
          "Access-Control-Allow-Headers: Content-Type, Authorization, Accept, "
          "Mcp-Session-Id, Mcp-Protocol-Version\r\n"
          "Connection: keep-alive\r\n"
          "\r\n";

      OwnedBuffer response_buffer;
      response_buffer.add(http_response);
      write_callbacks_->connection().write(response_buffer, false);
      GOPHER_LOG_DEBUG(
          "HttpSseJsonRpcProtocolFilter: Sent HTTP 202 for notification");
    }
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
      GOPHER_LOG_ERROR(
          "No stream found for response ID {} - dropping response "
          "(possible duplicate or late response)",
          id_key);
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
    GOPHER_LOG_WARN("sendResponseThroughFilter called - this is dead code!");
  }

 private:
  /**
   * Process pending messages after receiving endpoint event
   * Called when we get the "endpoint" SSE event from server
   */
  void processPendingMessages() {
    GOPHER_LOG_DEBUG(
        "HttpSseJsonRpcProtocolFilter: Processing {} pending messages",
        pending_messages_.size());

    if (pending_messages_.empty()) {
      return;
    }

    // Send all pending messages via POST connection
    for (auto& msg_buffer : pending_messages_) {
      size_t len = msg_buffer.length();
      if (len > 0) {
        std::string json_body(
            static_cast<const char*>(msg_buffer.linearize(len)), len);

        // Send via separate POST connection
        if (!mcp_callbacks_.sendHttpPost(json_body)) {
          GOPHER_LOG_ERROR(
              "HttpSseJsonRpcProtocolFilter: sendHttpPost failed for queued "
              "message: {}",
              json_body.substr(0, std::min(len, (size_t)100)));
        } else {
          GOPHER_LOG_DEBUG(
              "HttpSseJsonRpcProtocolFilter: Successfully sent queued message");
        }
      }
    }

    // Clear the queue
    pending_messages_.clear();
    GOPHER_LOG_DEBUG(
        "HttpSseJsonRpcProtocolFilter: Finished processing pending messages");
  }

  void setupRoutingHandlers() {
    // Register CORS preflight handler for all paths
    // Browser-based clients (like MCP Inspector) send OPTIONS before POST
    auto corsHandler = [](const HttpRoutingFilter::RequestContext& req) {
      HttpRoutingFilter::Response resp;
      resp.status_code = 204;  // No Content
      resp.headers["Access-Control-Allow-Origin"] = "*";
      resp.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS";
      resp.headers["Access-Control-Allow-Headers"] =
          "Content-Type, Authorization, Accept, Mcp-Session-Id, "
          "Mcp-Protocol-Version";
      resp.headers["Access-Control-Max-Age"] = "86400";  // Cache for 24 hours
      resp.headers["Content-Length"] = "0";
      return resp;
    };

    // Register OPTIONS for common MCP paths
    routing_filter_->registerHandler("OPTIONS", "/mcp", corsHandler);
    routing_filter_->registerHandler("OPTIONS", "/mcp/events", corsHandler);
    routing_filter_->registerHandler("OPTIONS", "/rpc", corsHandler);
    routing_filter_->registerHandler("OPTIONS", "/health", corsHandler);
    routing_filter_->registerHandler("OPTIONS", "/info", corsHandler);

    // Register health endpoint
    routing_filter_->registerHandler(
        "GET", "/health", [](const HttpRoutingFilter::RequestContext& req) {
          HttpRoutingFilter::Response resp;
          resp.status_code = 200;
          resp.headers["content-type"] = "application/json";
          resp.headers["cache-control"] = "no-cache";
          resp.headers["Access-Control-Allow-Origin"] = "*";

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
          resp.headers["Access-Control-Allow-Origin"] = "*";

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

    // Default handler - handle OPTIONS for CORS preflight on any path,
    // pass through other methods to MCP protocol handling
    routing_filter_->registerDefaultHandler(
        [](const HttpRoutingFilter::RequestContext& req) {
          // Handle OPTIONS for CORS preflight on any path
          if (req.method == "OPTIONS") {
            HttpRoutingFilter::Response resp;
            resp.status_code = 204;  // No Content
            resp.headers["Access-Control-Allow-Origin"] = "*";
            resp.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS";
            resp.headers["Access-Control-Allow-Headers"] =
                "Content-Type, Authorization, Accept, Mcp-Session-Id, "
                "Mcp-Protocol-Version";
            resp.headers["Access-Control-Max-Age"] = "86400";
            resp.headers["Content-Length"] = "0";
            return resp;
          }
          // Return status 0 to indicate pass-through for MCP endpoints
          HttpRoutingFilter::Response resp;
          resp.status_code = 0;
          return resp;
        });

    // Call custom route registration callback if provided
    // This allows users to register additional endpoints like OAuth discovery
    if (route_registration_callback_) {
      route_registration_callback_(routing_filter_.get());
    }
  }

  event::Dispatcher& dispatcher_;
  McpProtocolCallbacks& mcp_callbacks_;
  bool is_server_;
  bool is_sse_mode_{false};
  bool client_accepts_sse_{
      false};  // Track if client supports SSE (Accept header)
  bool sse_headers_written_{
      false};  // Track if HTTP headers sent for SSE stream

  // SSE client endpoint configuration
  std::string http_path_{"/rpc"};       // Default HTTP path for requests
  std::string http_host_{"localhost"};  // Default HTTP host for requests
  bool use_sse_{true};  // True for SSE mode, false for Streamable HTTP

  // SSE server transport (only meaningful when is_server_ == true).
  std::string configured_sse_path_{"/sse"};
  std::string configured_rpc_path_{"/mcp"};
  std::string configured_external_url_;
  // Registry of live SSE session IDs → their stream connections. Shared
  // with sibling filter instances on the same factory; null in client
  // mode and in server mode when the factory wasn't built with SSE
  // server transport (back-compat default constructors).
  SseSessionRegistry* sse_registry_{nullptr};
  // This connection serves the long-lived GET /sse stream.
  bool sse_server_mode_{false};
  // Pass-through guard: true while we're inside onHeaders writing raw
  // HTTP bytes (handshake or 202 Accepted) via connection().write(), so
  // our own onWrite doesn't try to re-frame those bytes.
  bool sse_writing_handshake_{false};
  // Session ID this connection's SSE stream is registered under.
  // Populated when we handle GET /sse, cleared in the destructor.
  std::string sse_session_id_;
  // Session ID parsed from an incoming POST /callback/{id}. Non-empty
  // means onWrite should redirect the response through the SSE stream
  // registered under this ID instead of writing back to the POST
  // connection.
  std::string sse_callback_session_id_;

  // SSE endpoint negotiation (client mode only)
  bool waiting_for_sse_endpoint_{false};  // Waiting for "endpoint" SSE event
  std::vector<OwnedBuffer>
      pending_messages_;  // Messages queued until endpoint received

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

  // Custom route registration callback
  HttpRouteRegistrationCallback route_registration_callback_;
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
  GOPHER_LOG_WARN(
      "HttpSseFilterChainFactory::sendHttpResponse called but filter "
      "access not available");
  GOPHER_LOG_WARN("Response ID {} dropped - no direct filter access",
                  requestIdToString(response.id));

  // Following production pattern: without direct filter access, we cannot route
  // responses The proper solution is for the connection manager to maintain
  // filter references and provide a proper API for response routing. For now,
  // responses must be sent through the connection manager's own mechanisms.
}

// ===== Factory Implementation =====

HttpSseFilterChainFactory::HttpSseFilterChainFactory(
    event::Dispatcher& dispatcher,
    McpProtocolCallbacks& message_callbacks,
    bool is_server,
    const std::string& http_path,
    const std::string& http_host,
    bool use_sse,
    const std::string& sse_path,
    const std::string& rpc_path,
    const std::string& external_url)
    : dispatcher_(dispatcher),
      message_callbacks_(message_callbacks),
      is_server_(is_server),
      http_path_(http_path),
      http_host_(http_host),
      use_sse_(use_sse),
      sse_path_(sse_path),
      rpc_path_(rpc_path),
      external_url_(external_url) {}

// Out-of-line destructor so the unique_ptr<SseSessionRegistry> member
// can see the complete type from this translation unit.
HttpSseFilterChainFactory::~HttpSseFilterChainFactory() = default;

bool HttpSseFilterChainFactory::createFilterChain(
    network::FilterManager& filter_manager) const {
  // Following production pattern: create filters in order
  // 1. Pre-filters (authentication, logging, etc.) - added by user
  // 2. Metrics Filter (collects statistics)
  // 3. Combined Protocol Filter (HTTP/SSE/JSON-RPC)

  // Invoke user-provided filter factories first (e.g., auth filters)
  // These filters run before protocol filters and can intercept/reject requests
  // Following the existing FilterFactoryCb pattern from FilterChainFactoryImpl
  for (const auto& factory : filter_factories_) {
    if (factory) {
      auto filter = factory();
      if (filter) {
        filter_manager.addReadFilter(filter);
        filter_manager.addWriteFilter(filter);
        filters_.push_back(filter);
      }
    }
  }

  // Create metrics filter if enabled
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

    auto metrics_filter = std::make_shared<filter::MetricsFilter>(
        metrics_callbacks, metrics_config);
    auto metrics_adapter = metrics_filter->createNetworkAdapter();
    filter_manager.addReadFilter(metrics_adapter);
    filter_manager.addWriteFilter(metrics_adapter);
    filters_.push_back(metrics_adapter);
  }

  // Routing is now integrated into the combined filter
  // No separate routing filter needed

  // Lazily construct the SSE session registry on the first server-side
  // filter chain creation. Client-mode factories never touch it, and
  // back-compat server-mode factories that don't use the SSE transport
  // still carry a registry — it just sits empty because registerSession
  // is only called from GET {configured_sse_path_}.
  if (is_server_ && !sse_registry_) {
    sse_registry_.reset(new SseSessionRegistry(dispatcher_));
  }

  // Create the combined protocol filter
  // Pass the route registration callback so custom HTTP routes can be
  // registered
  auto combined_filter = std::make_shared<HttpSseJsonRpcProtocolFilter>(
      dispatcher_, message_callbacks_, is_server_, http_path_, http_host_,
      use_sse_, route_registration_callback_, sse_path_, rpc_path_,
      external_url_, sse_registry_.get());

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
