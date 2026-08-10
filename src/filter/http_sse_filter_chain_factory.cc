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
 *   - SSE mode: Frame each event through the connection's ResponseWriter
 *   - Normal mode: Pass through JsonRpcFilter → HttpCodecFilter
 * → ConnectionImpl::doWrite() → Socket
 *
 * SSE MODE DETECTION:
 * - Server: Accept header contains "text/event-stream"
 * - Client: Content-Type header contains "text/event-stream"
 *
 * SSE STREAMING:
 * - The stream opens in onHeaders: status line, SSE headers, the framing
 *   header, and the endpoint event, all through one ResponseWriter
 * - Later events go through that same writer so their framing matches the
 *   prelude it emitted
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

#include "mcp/filter/client_sse_state_machine.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/http_routing_filter.h"
#include "mcp/filter/http_security_filter.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/filter/metrics_filter.h"
#include "mcp/filter/server_connection_mode.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/filter/sse_session_registry.h"
#include "mcp/filter/streamable_http_filter.h"
#include "mcp/http/response_writer.h"
#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/protocol/protocol_versions.h"
#include "mcp/stream_info/stream_info.h"
#include "mcp/transport/exchange_registry.h"

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

class HttpSseJsonRpcProtocolFilter
    : public network::Filter,
      public HttpCodecFilter::MessageCallbacks,
      public HttpCodecFilter::GateCallbacks,
      public network::ConnectionCallbacks,
      public SseCodecFilter::EventCallbacks,
      public JsonRpcProtocolFilter::MessageHandler,
      public http::ResponseWriter::Observer,
      public StreamableHttpFilter::Host,
      public HttpSecurityFilter::Host {
 public:
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
      const std::map<std::string, std::string>& client_headers = {},
      const std::shared_ptr<std::map<std::string, std::string>>&
          client_header_source = nullptr,
      SseSessionRegistry* sse_registry = nullptr,
      StreamGatePolicy stream_gate_policy = StreamGatePolicy::Off,
      size_t gated_input_limit = 64 * 1024,
      transport::RetainedExchangeStore* retained_exchanges = nullptr,
      const HttpSecurityOptions& security_options = HttpSecurityOptions(),
      const StreamableHttpOptions& streamable_options = StreamableHttpOptions(),
      const transport::StreamableHttpClientSessionPtr& client_session = nullptr,
      ClientConnectionRole client_role = ClientConnectionRole::Requests,
      const std::shared_ptr<std::atomic<uint64_t>>& stream_activity = nullptr)
      : dispatcher_(dispatcher),
        mcp_callbacks_(mcp_callbacks),
        is_server_(is_server),
        http_path_(http_path),
        http_host_(http_host),
        client_headers_(client_headers),
        client_header_source_(client_header_source),
        configured_sse_path_(configured_sse_path),
        configured_rpc_path_(configured_rpc_path),
        configured_external_url_(configured_external_url),
        sse_registry_(sse_registry),
        retained_exchanges_(retained_exchanges),
        exchanges_(dispatcher),
        stream_gate_policy_(stream_gate_policy),
        streamable_options_(streamable_options),
        client_session_(client_session),
        role_(client_role),
        stream_activity_(stream_activity),
        route_registration_callback_(route_callback) {
    // Following production pattern: all operations for this filter
    // happen in the single dispatcher thread

    // The MCP endpoint is served by its own filter, which sits between
    // routing and this one and hands back everything it does not serve.
    // Built before the routing filter because that filter takes the layer
    // behind it as a constructor argument.
    HttpCodecFilter::MessageCallbacks* after_routing = this;
    if (is_server_) {
      streamable_filter_.reset(new StreamableHttpFilter(
          dispatcher_, mcp_callbacks_, *this, exchanges_, *this,
          configured_rpc_path_, streamable_options));
      after_routing = streamable_filter_.get();
    }

    // Create routing filter first (it will receive HTTP callbacks)
    routing_filter_ = std::make_shared<HttpRoutingFilter>(
        after_routing,
        nullptr,  // Will be set after HTTP filter is created
        is_server_);

    // Requests are judged before they are routed, so a refusal costs a
    // response and nothing else: no handler runs, no route is consulted,
    // and no protocol layer sees a body it might act on.
    HttpCodecFilter::MessageCallbacks* after_codec = routing_filter_.get();
    if (is_server_) {
      security_policy_.setAllowedOrigins(security_options.allowed_origins);
      security_policy_.setExtraAllowedHeaders(
          security_options.extra_allowed_headers);
      security_filter_.reset(new HttpSecurityFilter(
          *routing_filter_, security_policy_, security_, *this));
      if (security_options.auth) {
        security_filter_->setAuthCallback(security_options.auth);
      }
      after_codec = security_filter_.get();
    }

    // Create the protocol filters
    // Single HTTP codec that sends callbacks to routing filter first
    GOPHER_LOG_DEBUG(
        "HttpSseJsonRpcProtocolFilter: Creating HttpCodecFilter with "
        "is_server={}",
        is_server_);
    http_filter_ = std::make_shared<HttpCodecFilter>(*after_codec, dispatcher_,
                                                     is_server_);

    // Set client endpoint for HTTP requests
    if (!is_server) {
      http_filter_->setClientEndpoint(http_path, http_host);
      http_filter_->setClientHeaders(client_headers_);
      http_filter_->setClientHeaderSource(client_header_source_);
      // Only enable SSE GET mode if use_sse is true
      // For Streamable HTTP, we send POST requests directly
      if (use_sse) {
        http_filter_->setUseSseGet(true);
      }
    }

    // Now set the encoder in routing filter
    routing_filter_->setEncoder(&http_filter_->messageEncoder());

    // Every answer this connection frames says who may read it, and says
    // it in one place. Between them these two cover every response that
    // is not written by hand below: the codec frames protocol responses,
    // the route table frames everything it serves itself.
    if (is_server_) {
      http_filter_->setResponseHeaderProvider(corsSource(/*preflight=*/false));
      routing_filter_->setResponseHeaderProvider(
          corsSource(/*preflight=*/false));
    }

    // Wire the stream connection policy. With the gate Off the codec never
    // holds anything back, so this only takes effect once a deployment
    // opts in.
    response_writer_.setObserver(this);
    if (stream_gate_policy_ != StreamGatePolicy::Off) {
      http_filter_->setGateCallbacks(this);
      http_filter_->setGatedInputLimit(gated_input_limit);
    }

    // Configure routing filter with health endpoint
    setupRoutingHandlers();

    // SSE and JSON-RPC filters for protocol-specific handling
    sse_filter_ =
        std::make_shared<SseCodecFilter>(*this, dispatcher_, is_server_);
    jsonrpc_filter_ =
        std::make_shared<JsonRpcProtocolFilter>(*this, dispatcher_, is_server_);

    // Client-side SSE negotiation state machine. Replaces the ad-hoc
    // boolean flags (waiting_for_sse_endpoint_, use_sse_, is_sse_mode_
    // on the client path) with validated state transitions.
    if (!is_server_) {
      ClientSseStateMachineConfig sm_config;
      sm_config.negotiation_timeout = std::chrono::milliseconds(30000);

      // When the negotiation timeout fires (or any error transition
      // occurs), propagate the error to the application layer and
      // discard any messages that were queued during negotiation.
      // This fixes the silent-hang bug where the server never sends
      // the "endpoint" SSE event and the client waits forever.
      sm_config.error_callback = [this](const std::string& reason) {
        GOPHER_LOG_ERROR("Client SSE negotiation failed: {}", reason);
        Error mcp_error(jsonrpc::INTERNAL_ERROR, reason);
        mcp_callbacks_.onError(mcp_error);

        // Discard queued messages — they can never be delivered
        // because the POST endpoint was never received.
        if (!pending_messages_.empty()) {
          GOPHER_LOG_WARN(
              "Discarding {} pending messages due to SSE negotiation failure",
              pending_messages_.size());
          pending_messages_.clear();
        }
      };

      // Where in the session this connection starts. A client that
      // reconnects mid-conversation is not going to initialize again,
      // so a machine that began by waiting for a handshake would be
      // waiting for something that never arrives.
      client_sse_sm_ = std::make_unique<ClientSseStateMachine>(
          dispatcher_, sm_config, use_sse,
          client_session_ && client_session_->established());

      // Log every state transition for observability and debugging.
      client_sse_sm_->addStateChangeListener(
          [](const ClientSseTransitionContext& ctx) {
            GOPHER_LOG_DEBUG(
                "Client SSE state: {} -> {} ({})",
                ClientSseStateMachine::getStateName(ctx.from_state),
                ClientSseStateMachine::getStateName(ctx.to_state), ctx.reason);
          });
    }

    // Server-side connection mode state machine. Replaces the ad-hoc
    // boolean flags (sse_server_mode_, sse_writing_handshake_,
    // sse_headers_written_) with validated mode determination.
    if (is_server_) {
      ServerConnModeConfig srv_config;
      server_mode_ =
          std::make_unique<ServerConnectionMode>(dispatcher_, srv_config);

      // Log every mode transition for observability and debugging.
      server_mode_->addStateChangeListener(
          [](const ServerConnTransitionContext& ctx) {
            GOPHER_LOG_DEBUG("Server connection mode: {} -> {} ({})",
                             ServerConnectionMode::getModeName(ctx.from_mode),
                             ServerConnectionMode::getModeName(ctx.to_mode),
                             ctx.reason);
          });
    }
  }

  ~HttpSseJsonRpcProtocolFilter() {
    if (connection_callbacks_registered_ && read_callbacks_) {
      read_callbacks_->connection().removeConnectionCallbacks(*this);
      connection_callbacks_registered_ = false;
    }

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

    // For client mode with SSE, signal the state machine that the
    // connection is ready. The GET /sse request will be sent on the
    // first onWrite() call after the connection is fully established
    // (TCP/TLS handshake complete). For Streamable HTTP mode the state
    // machine stays in StreamableHttp and no GET is sent.
    if (client_sse_sm_ &&
        client_sse_sm_->currentState() == ClientSseState::Idle) {
      client_sse_sm_->handleEvent(ClientSseEvent::ConnectionReady);
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
    // Anything at all arriving is a stream that is still there —
    // including the keep-alive comments the parser consumes without ever
    // reporting an event for, which is why this counts bytes rather than
    // messages.
    if (stream_activity_ && data.length() > 0) {
      stream_activity_->fetch_add(1, std::memory_order_relaxed);
    }

    // Data flows through protocol layers in sequence
    // HTTP -> SSE -> JSON-RPC

    // First layer: HTTP codec processes the data
    auto status = http_filter_->onData(data, end_stream);
    if (status == network::FilterStatus::StopIteration) {
      return status;
    }

    // Second layer: SSE codec (if this connection is in SSE mode —
    // either server SseStream or client Active).
    if (isSseActive()) {
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
   *    Server SSE mode (connection is serving an event stream):
   *    - The prelude was already written when the stream opened, so every
   *      write here is another event on a response in flight
   *    - Framed by the connection's ResponseWriter to match that prelude
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
    // While this runs, the connection is inside a write() and is holding a
    // pointer to the buffer being written. An exchange that wrote now would
    // clobber it, so tell them all to hold off until this returns.
    transport::ExchangeRegistry::WriteGuard write_guard(exchanges_);

    // SSE handshake bypass: we're inside a connection().write() call
    // made from our own onHeaders (writing "HTTP/1.1 200 OK ... event:
    // endpoint" for GET /sse, or "HTTP/1.1 202 Accepted" for POST
    // /callback). The bytes are already fully-formed HTTP — skip all
    // downstream JSON-RPC / HTTP-codec framing.
    if (server_mode_ && server_mode_->isWritingHandshake()) {
      return network::FilterStatus::Continue;
    }

    // An exchange that is streaming frames its own bytes, prelude and
    // chunks alike. Everything below this point exists to frame an answer
    // that has not been framed yet, and applying it to a chunk would wrap
    // that chunk in a second complete response — which is what a client
    // would then read as the end of the first one.
    //
    // Nothing else can be writing here: while a response streams, the
    // decoder is gated and no further request on this connection is being
    // dispatched.
    if (is_server_ && exchanges_.hasActiveStream()) {
      return network::FilterStatus::Continue;
    }

    // SSE callback routing: the JSON-RPC filter is emitting the response
    // for a POST /callback/{id}. We already sent 202 Accepted on that
    // POST connection, so we must NOT let these bytes continue down the
    // write chain to be framed as HTTP and written back to the POST
    // connection. Instead, pull the JSON body out and hand it to the
    // registry, which writes it through the matching SSE stream.
    if (server_mode_ && server_mode_->isCallbackProxy() &&
        !sse_callback_session_id_.empty() && data.length() > 0) {
      const size_t len = data.length();
      std::string json_data(static_cast<const char*>(data.linearize(len)), len);
      data.drain(len);

      if (sse_registry_) {
        const network::Connection* writing =
            write_callbacks_ ? &write_callbacks_->connection() : nullptr;
        sse_registry_->sendResponse(sse_callback_session_id_, json_data,
                                    writing);
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
        "is_server={}, client_sse_state={}",
        data.length(), is_server_,
        client_sse_sm_ ? ClientSseStateMachine::getStateName(
                             client_sse_sm_->currentState())
                       : "N/A");

    // Client mode: handle SSE GET initialization.
    // The state machine replaces the boolean waiting_for_sse_endpoint_
    // and hasSentSseGetRequest() checks with explicit state queries.
    // We check for Idle OR negotiating because onWrite can arrive before
    // onNewConnection fires the ConnectionReady event (e.g. when the
    // connection is already established by the time the first write comes).
    if (client_sse_sm_ &&
        (client_sse_sm_->isNegotiating() ||
         client_sse_sm_->currentState() == ClientSseState::Idle)) {
      // Idle or WaitingForGetSent: the GET has not been sent yet.
      // Transition through the state machine and send the GET.
      auto sm_state = client_sse_sm_->currentState();
      if (sm_state == ClientSseState::Idle ||
          sm_state == ClientSseState::WaitingForGetSent) {
        // If still Idle, fire ConnectionReady to advance to WaitingForGetSent.
        if (sm_state == ClientSseState::Idle) {
          client_sse_sm_->handleEvent(ClientSseEvent::ConnectionReady);
        }
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

        // The GET has been sent — transition the state machine so it
        // tracks that we are now waiting for the server's endpoint event.
        if (client_sse_sm_) {
          client_sse_sm_->handleEvent(ClientSseEvent::GetSent);
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

    // Client mode with SSE active: send via separate POST connection.
    // The SSE connection is for receiving only — POSTs must go separately.
    // The state machine replaces the (is_sse_mode_ &&
    // !waiting_for_sse_endpoint_) boolean combination with a single
    // canSendPost() query.
    if (client_sse_sm_ && client_sse_sm_->canSendPost() &&
        !client_sse_sm_->isStreamableHttp() &&
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
    if (server_mode_ && server_mode_->isSseStream()) {
      // The stream was opened in onHeaders, prelude and all, so everything
      // arriving here is a further event on a response already in flight.
      // It goes through the same writer, which frames it to match the
      // prelude it emitted.
      if (data.length() > 0) {
        size_t data_len = data.length();
        std::string json_data(
            static_cast<const char*>(data.linearize(data_len)), data_len);
        data.drain(data_len);

        if (!response_writer_.writeEvent("", json_data)) {
          GOPHER_LOG_ERROR(
              "SSE event dropped: no open stream on this connection");
        }
        response_writer_.drainTo(data);
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

    // The peer going away is delivered as a connection event, not through
    // the read path — on end-of-file the connection closes rather than
    // handing the filters a final empty read. A response stream has to
    // learn about it from here.
    if (!connection_callbacks_registered_) {
      callbacks.connection().addConnectionCallbacks(*this);
      connection_callbacks_registered_ = true;
    }

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
        // Determine connection mode as SSE stream.
        if (server_mode_) {
          server_mode_->handleEvent(ServerConnEvent::SseGetDetected);
        }
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

        // Open the stream through the response writer so the body is
        // actually framed. An event stream with no Content-Length and no
        // Transfer-Encoding is indistinguishable from an empty body on a
        // persistent connection, which is how the stream used to go out.
        http::ResponseWriter::Options writer_options;
        writer_options.http_1_1 = http_filter_->currentRequestIsHttp11();
        // Under the single-use policy the response says up front that the
        // connection ends with it, which is the other wire-legal answer to
        // a request arriving behind an open stream.
        writer_options.keep_alive =
            stream_gate_policy_ != StreamGatePolicy::SingleUseClose;
        response_writer_ = http::ResponseWriter(writer_options);
        response_writer_.setObserver(this);

        const auto start = response_writer_.startSse(
            static_cast<int>(http::HttpStatusCode::OK), corsHeaderList());
        if (start == http::ResponseWriter::SseStart::Streaming) {
          response_writer_.writeEvent("endpoint", callback_url);
        } else {
          // The client cannot take a stream; the writer has already put a
          // complete answer in its place. Drop the session we just made so
          // nothing tries to route responses into a stream that never
          // opened.
          GOPHER_LOG_WARN("SSE stream refused for session {}", sse_session_id_);
          sse_registry_->removeSession(sse_session_id_);
          sse_session_id_.clear();
        }

        OwnedBuffer response_buffer;
        response_writer_.drainTo(response_buffer);

        // RAII guard ensures isWritingHandshake() is cleared even if
        // connection().write() throws or triggers a callback chain, so our
        // own onWrite passes these already-framed bytes through untouched.
        {
          HandshakeWriteGuard guard(*server_mode_);
          write_callbacks_->connection().write(response_buffer, false);
        }

        if (start != http::ResponseWriter::SseStart::Streaming) {
          return;
        }

        // Mark headers as written via the state machine. The mode was
        // already set to SseStream by handleEvent(SseGetDetected) above.
        server_mode_->handleEvent(ServerConnEvent::SseHeadersWritten);

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
        // Determine connection mode as callback proxy.
        if (server_mode_) {
          server_mode_->handleEvent(ServerConnEvent::CallbackPostDetected);
        }
        sse_callback_session_id_ = path.substr(cb_pos + callback_prefix.size());
        GOPHER_LOG_DEBUG("SSE callback POST: session={}",
                         sse_callback_session_id_);

        if (write_callbacks_) {
          http::ResponseWriter writer;
          writer.startUnary(static_cast<int>(http::HttpStatusCode::Accepted),
                            corsHeaderList());
          OwnedBuffer resp_buf;
          writer.drainTo(resp_buf);
          // RAII guard ensures isWritingHandshake() is cleared even
          // on early return or exception.
          {
            HandshakeWriteGuard guard(*server_mode_);
            write_callbacks_->connection().write(resp_buf, false);
          }
        }
        // CallbackProxy mode — the body is plain JSON-RPC, not an SSE
        // stream. Response routing happens in onWrite via
        // sse_callback_session_id_. isSseActive() returns false for
        // CallbackProxy so the SSE codec is not invoked.
        return;
      }

      // Non-SSE-transport request (POST /mcp Streamable HTTP, /health,
      // /info, etc.). Determine connection mode as plain HTTP.
      if (server_mode_ &&
          server_mode_->currentMode() == ServerConnMode::Undetermined) {
        server_mode_->handleEvent(ServerConnEvent::PlainHttpDetected);
      }
      streamable_http_session_id_.clear();
      auto session_it = headers.find("mcp-session-id");
      if (session_it != headers.end()) {
        streamable_http_session_id_ = session_it->second;
      }

      // The protocol version header only became required after a certain
      // revision, so a request without one identifies a peer speaking the
      // revision before it.
      auto version_it = headers.find("mcp-protocol-version");
      request_protocol_version_ = version_it != headers.end()
                                      ? version_it->second
                                      : protocol::kLegacyAssumedVersion;
      auto accept = headers.find("accept");
      if (accept != headers.end() &&
          accept->second.find("text/event-stream") != std::string::npos) {
        client_accepts_sse_ = true;
        GOPHER_LOG_DEBUG("HttpSseJsonRpcProtocolFilter: client accepts SSE");
      }
      // PlainHttp mode — isSseActive() returns false by default.
    } else {
      // Client: a session id on a response is the server naming the
      // conversation this connection is part of. It arrives on the
      // initialize response and only there, so taking it whenever one
      // is offered costs nothing and does not depend on this layer
      // knowing which request is being answered.
      if (client_session_) {
        auto session_it = headers.find("mcp-session-id");
        if (session_it != headers.end() && !session_it->second.empty()) {
          client_session_->setId(session_it->second);
          GOPHER_LOG_DEBUG("Streamable HTTP client joined session {}",
                           session_it->second);
        }
      }

      // What the server made of the request. A refusal says nothing in
      // the message layer — the body behind it carries no id — so the
      // status is what has to be remembered until the response is over
      // and the request it answered can be named. Read for every client,
      // because how a body is treated depends on it whether or not there
      // is a session to report it against.
      response_status_ = 0;
      auto status_it = headers.find(":status");
      if (status_it != headers.end()) {
        response_status_ = std::atoi(status_it->second.c_str());
      }
      refusal_body_.clear();

      // Client: whether this answer arrives as a stream is a fact about
      // this answer, not about the conversation. The same connection
      // will answer the next request whichever way suits it, so what
      // decides how a body is read is the response being read, and it
      // is forgotten when that response is over.
      auto content_type = headers.find("content-type");
      reading_event_stream_ =
          !isRefusal() && content_type != headers.end() &&
          content_type->second.find("text/event-stream") != std::string::npos;

      // A stream's own response never completes — that is what makes it
      // a stream — so whether the server agreed to hold one is settled
      // here or nowhere.
      if (role_ == ClientConnectionRole::ServerStream && client_session_ &&
          reading_event_stream_) {
        mcp_callbacks_.onClientStreamEvent(
            ClientStreamEvent::Opened, optional<RequestId>(), std::string());
      }

      if (reading_event_stream_) {
        // A second stream on this connection starts where it starts, not
        // where the last one left off: an event with no id of its own
        // would otherwise inherit a cursor into somebody else's stream.
        sse_filter_->resetStream();
        last_event_id_.clear();
        sse_filter_->startEventStream();
        // The older transport's negotiation is what StreamStarted is
        // for, and this is not that: a Streamable connection reading a
        // streamed answer is exactly where it was before.
        if (client_sse_sm_ && !client_sse_sm_->isStreamableHttp()) {
          client_sse_sm_->handleEvent(ClientSseEvent::StreamStarted);
        }
      }
    }
  }

  void onBody(const std::string& data, bool end_stream) override {
    GOPHER_LOG_FLOW_DEBUG(
        "HTTP/SSE server body received mode={} bytes={} end_stream={}",
        server_mode_
            ? ServerConnectionMode::getModeName(server_mode_->currentMode())
            : "<none>",
        data.size(), end_stream ? "true" : "false");

    // The long-lived GET /sse request has no request body — but if the
    // codec surfaces any trailing bytes we don't want to push them down
    // into the JSON-RPC parser. Ignore bodies on the SSE stream
    // connection entirely.
    if (server_mode_ && server_mode_->isSseStream()) {
      GOPHER_LOG_FLOW_DEBUG("HTTP/SSE server body ignored on SSE stream");
      return;
    }
    // Server receives JSON-RPC in request body regardless of SSE mode
    // SSE mode only affects the response format
    if (is_server_) {
      // Server always receives JSON-RPC in request body
      pending_json_data_.add(data);
      if (end_stream) {
        GOPHER_LOG_FLOW_DEBUG(
            "HTTP/SSE server forwarding JSON-RPC body bytes={} to parser",
            pending_json_data_.length());
        jsonrpc_filter_->onData(pending_json_data_, true);
        pending_json_data_.drain(pending_json_data_.length());
      }
    } else {
      // Client mode: route body data based on the state machine.
      // When the SSE stream is active (Content-Type: text/event-stream
      // was seen), the body carries SSE event-stream chunks. Otherwise
      // (Streamable HTTP or before SSE headers arrive) it carries
      // JSON-RPC responses. isSseActive() checks client_sse_sm_->isReady().
      // A refused request's body is an error the message layer cannot
      // place: the server had no id to answer under, so nothing in it
      // matches anything outstanding. Feeding it to the JSON-RPC filter
      // is how a 404 used to turn into an unmatched error while the
      // request that caused it waited out its deadline. Keep it for the
      // detail instead.
      if (isRefusal()) {
        refusal_body_.append(data);
        return;
      }

      if (isSseActive()) {
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
    GOPHER_LOG_FLOW_DEBUG(
        "HTTP/SSE message complete sse_active={} pending_json_bytes={}",
        isSseActive() ? "true" : "false", pending_json_data_.length());

    // One answer, one place given up. Every response takes one, not just
    // the refusals — a queue that only moved when something went wrong
    // would name the wrong request the moment anything went right.
    //
    // The stream connection is outside all of this: what arrives there
    // is not answering any request of ours, so it takes no place and
    // names none.
    if (!is_server_ && client_session_ && response_status_ != 0) {
      const int status = response_status_;
      const std::string detail = refusal_body_;
      response_status_ = 0;
      refusal_body_.clear();
      if (role_ == ClientConnectionRole::ServerStream) {
        // The one thing worth saying about a stream's own response is
        // whether the server will serve one at all.
        if (status ==
            static_cast<int>(http::HttpStatusCode::MethodNotAllowed)) {
          mcp_callbacks_.onClientStreamEvent(ClientStreamEvent::Refused,
                                             optional<RequestId>(), detail);
        }
      } else {
        mcp_callbacks_.onTransportStatus(
            status, client_session_->takeAnswered(), detail);
      }
    }

    // HTTP message complete — flush any remaining JSON-RPC data that
    // was not yet processed. In SSE mode the data flows through the
    // SSE codec instead, so we only flush for non-SSE connections.
    if (!isSseActive() && pending_json_data_.length() > 0) {
      // Process any remaining JSON-RPC data
      GOPHER_LOG_FLOW_DEBUG(
          "HTTP/SSE message complete forwarding pending JSON-RPC bytes={}",
          pending_json_data_.length());
      jsonrpc_filter_->onData(pending_json_data_, true);
      pending_json_data_.drain(pending_json_data_.length());
    }

    // The stream this response was, if it was one, is over. Read after
    // the flush above, which asks whether it was.
    reading_event_stream_ = false;
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

    // Where this stream has got to. Kept rather than discarded because
    // it is the only thing a client that loses the stream can say to be
    // given what it missed instead of the stream from the beginning.
    if (id.has_value() && !id.value().empty()) {
      last_event_id_ = id.value();
    }

    // Handle special MCP SSE events
    if (event == "endpoint") {
      // Server is telling us the endpoint URL for POST requests
      GOPHER_LOG_DEBUG(
          "HttpSseJsonRpcProtocolFilter: Received endpoint event: {}", data);
      http_filter_->setMessageEndpoint(data);

      // Transition the state machine: endpoint arrived. This replaces
      // the old waiting_for_sse_endpoint_ = false assignment.
      if (client_sse_sm_) {
        client_sse_sm_->handleEvent(ClientSseEvent::EndpointReceived);
      }

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
   * Per-message dispatch context for messages decoded on this composite chain.
   * Origin is the connection the message physically arrived on. The transport
   * session id is the durable client identity for transports where logical MCP
   * sessions span short-lived HTTP connections: the callback id parsed from
   * POST /callback/{id}, or Mcp-Session-Id from Streamable HTTP POST /mcp.
   *
   * The reply sink writes the bare JSON to the origin connection, exactly
   * the bytes the server used to write to its ambient current-connection
   * pointer: this composite's own onWrite then decides the wire form
   * (reroute through the SSE registry for a callback proxy, HTTP-frame for
   * plain HTTP). Same wire behavior, but the destination is the message's
   * own connection by construction.
   */
  /**
   * A view onto the exchange behind the message being dispatched.
   *
   * The view is callback-scoped, as it always was — it dies when dispatch
   * returns, which is what makes a stale reply path unrepresentable. The
   * exchange it points at is not: that is the object anything outliving the
   * callback belongs to.
   */
  class DispatchContext : public MessageDispatchContext {
   public:
    DispatchContext(HttpSseJsonRpcProtocolFilter& parent,
                    transport::RequestExchangePtr exchange = nullptr)
        : parent_(parent), exchange_(std::move(exchange)) {}

    /** The exchange behind this message, when there is one. */
    const transport::RequestExchangePtr& exchange() const { return exchange_; }

    network::Connection* originConnection() const override {
      return parent_.write_callbacks_ ? &parent_.write_callbacks_->connection()
                                      : nullptr;
    }

    const std::string& transportSessionId() const override {
      if (!parent_.sse_callback_session_id_.empty()) {
        return parent_.sse_callback_session_id_;
      }
      return parent_.streamable_http_session_id_;
    }

    VoidResult sendResponse(const jsonrpc::Response& response) override {
      if (exchange_) {
        // The exchange knows what it has already committed to, so it is the
        // thing that can refuse a second answer rather than writing two
        // contradictory responses onto one request.
        return exchange_->respondJson(response);
      }

      // No exchange behind this message: the legacy SSE transport paths,
      // which answer through their own machinery.
      //
      // Fail loudly when the reply path is gone instead of pretending the
      // response went out. The state check matters as much as the null
      // check: write_callbacks_ is never cleared, and a write to a
      // non-open connection is silently discarded by the connection.
      if (!parent_.write_callbacks_ ||
          parent_.write_callbacks_->connection().state() !=
              network::ConnectionState::Open) {
        Error err;
        err.code = jsonrpc::INTERNAL_ERROR;
        err.message = "response dropped: origin connection is gone";
        return makeVoidError(err);
      }
      auto json_val = json::to_json(response);
      std::string json_str = json_val.toString();
      OwnedBuffer buffer;
      buffer.add(json_str);
      parent_.write_callbacks_->connection().write(buffer, false);
      return makeVoidSuccess();
    }

   private:
    HttpSseJsonRpcProtocolFilter& parent_;
    transport::RequestExchangePtr exchange_;
  };

  /**
   * Build the exchange for an inbound request, when this connection is one
   * that has them.
   *
   * Only plain HTTP requests get one. The legacy SSE transport answers
   * through its own machinery — a long-lived stream on one connection and
   * one-shot callback POSTs on others — and has no use for a per-request
   * runtime. Requests to the MCP endpoint never arrive here at all; the
   * filter in front of this one keeps them and makes their exchange itself.
   */
  transport::RequestExchangePtr makeExchangeFor(
      const jsonrpc::Request& request) {
    if (!is_server_ || !write_callbacks_ || !server_mode_ ||
        !server_mode_->isPlainHttp()) {
      return nullptr;
    }

    std::unique_ptr<transport::ConnectionExchangeSink> sink(
        new transport::ConnectionExchangeSink(&write_callbacks_->connection()));
    auto exchange = transport::RequestExchange::create(
        dispatcher_, std::move(sink), optional<RequestId>(request.id));

    // Capture how to answer now rather than reading it when the response is
    // written: by then this connection may be handling a different request,
    // or none at all.
    exchange->setResponseOptions(http_filter_->currentRequestIsHttp11(),
                                 /*keep_alive=*/true);
    exchange->clientContext().protocol_version = request_protocol_version_;

    // params._meta arrives already serialized, because nested JSON is
    // stringified on the way in. Carry it as it came; whoever needs a field
    // out of it can parse it.
    if (request.params.has_value()) {
      const auto& params = request.params.value();
      auto meta_it = params.find("_meta");
      if (meta_it != params.end() &&
          holds_alternative<std::string>(meta_it->second)) {
        exchange->clientContext().raw_meta =
            mcp::make_optional(get<std::string>(meta_it->second));
      }
    }

    exchanges_.add(exchange);
    return exchange;
  }

  /**
   * Called by JsonRpcProtocolFilter when a complete JSON-RPC request is parsed
   * Creates a RequestStream to track this request-response pair
   * Server only - clients don't receive requests
   */
  void onRequest(const jsonrpc::Request& request) override {
    GOPHER_LOG_DEBUG("HttpSseFilter::onRequest for method: {}", request.method);
    // The context carries the transport session id (the SSE stream id from
    // a POST /callback/{id} path — the durable client identity) with the
    // message itself. Built fresh per message because dispatcher-thread
    // reads from different connections interleave; a previous message's
    // binding cannot leak because the previous context is already gone.
    DispatchContext context(*this, makeExchangeFor(request));
    mcp_callbacks_.onRequestWithContext(request, context);

    // Anything that finished during dispatch is no longer this connection's
    // concern. A handler that means to answer later keeps its own reference.
    exchanges_.reapCompleted();
  }

  /**
   * The JSON-RPC sub-filter dispatches through here with its own generic
   * context, but that context knows neither the SSE stream id nor this
   * composite's write semantics — replace it with the composite's own.
   */
  void onRequestWithContext(const jsonrpc::Request& request,
                            MessageDispatchContext& context) override {
    (void)context;
    onRequest(request);
  }

  void onNotificationWithContext(const jsonrpc::Notification& notification,
                                 MessageDispatchContext& context) override {
    (void)context;
    onNotification(notification);
  }

  void onNotification(const jsonrpc::Notification& notification) override {
    DispatchContext context(*this);
    mcp_callbacks_.onNotificationWithContext(notification, context);

    // For HTTP transport, send HTTP 202 Accepted response.
    // JSON-RPC notifications don't have responses, but HTTP requires one.
    //
    // NOT in callback-proxy mode, for two reasons: onHeaders already
    // answered the POST /callback with a guarded 202 at header time, and
    // this write is unguarded — onWrite's callback-proxy branch would
    // capture the raw status line and ship it down the client's SSE
    // stream as event data, corrupting the stream with
    // "data: HTTP/1.1 202 Accepted...".
    if (is_server_ && write_callbacks_ &&
        !(server_mode_ && server_mode_->isCallbackProxy())) {
      // Build minimal HTTP 202 response
      http::ResponseWriter writer;
      writer.startUnary(static_cast<int>(http::HttpStatusCode::Accepted),
                        corsHeaderList());

      OwnedBuffer response_buffer;
      writer.drainTo(response_buffer);
      write_callbacks_->connection().write(response_buffer, false);
      GOPHER_LOG_DEBUG(
          "HttpSseJsonRpcProtocolFilter: Sent HTTP 202 for notification");
    }
  }

  void onResponse(const jsonrpc::Response& response) override {
    mcp_callbacks_.onResponse(response);
  }

  // ===== StreamableHttpFilter::Host =====

  transport::ExchangeSinkPtr makeSink() override {
    // Null when the connection is not writable yet; the sink reports itself
    // dead in that case rather than pretending bytes went out.
    network::Connection* connection =
        write_callbacks_ ? &write_callbacks_->connection() : nullptr;
    return transport::ExchangeSinkPtr(
        new transport::ConnectionExchangeSink(connection));
  }

  network::Connection* connection() override {
    return write_callbacks_ ? &write_callbacks_->connection() : nullptr;
  }

  // Shared with HttpSecurityFilter::Host, which asks the same question for
  // the same reason.
  bool requestIsHttp11() const override {
    return http_filter_ && http_filter_->currentRequestIsHttp11();
  }

  const std::string& principal() const override { return security_.principal; }

  http::ResponseWriter::HeaderList framedResponseHeaders() const override {
    return corsHeaderList();
  }

  http::ResponseWriter::Observer* streamObserver() override { return this; }

  bool streamEndsConnection() const override {
    return stream_gate_policy_ == StreamGatePolicy::SingleUseClose;
  }

  void holdInput(bool hold) override {
    if (!http_filter_) {
      return;
    }
    // The same gate a streaming response uses, for the same reason: while
    // one request cannot be answered yet, nothing behind it may be.
    if (hold) {
      http_filter_->pauseRequestProcessing();
    } else {
      http_filter_->resumeRequestProcessing();
    }
  }

  // ===== HttpSecurityFilter::Host =====

  void writeResponse(Buffer& data, bool close_connection) override {
    if (!write_callbacks_) {
      return;
    }
    // Guarded because these bytes are already a complete HTTP response.
    // Unguarded, a connection that previously proxied a callback POST
    // would capture them and ship the status line down someone's event
    // stream as message data.
    {
      HandshakeWriteGuard guard(*server_mode_);
      write_callbacks_->connection().write(data, false);
    }
    if (close_connection) {
      closeConnectionSoon();
    }
  }

  // ===== http::ResponseWriter::Observer =====

  void onSseStreamStarted() override {
    if (stream_gate_policy_ == StreamGatePolicy::DecoderGate && http_filter_) {
      // Anything that arrives from here on cannot be answered until this
      // stream ends, so stop turning it into requests.
      http_filter_->pauseRequestProcessing();
    }
  }

  void onSseStreamFinished(bool close_connection) override {
    if (http_filter_) {
      http_filter_->resumeRequestProcessing();
    }
    if (close_connection) {
      closeConnectionSoon();
    }
  }

  // ===== HttpCodecFilter::GateCallbacks =====

  void onGatedInputOverflow() override {
    // The client sent more than will be held while its stream runs. No
    // status can be sent — a response body is already going out — so the
    // connection is all there is to take away.
    GOPHER_LOG_WARN("Closing connection: gated input exceeded its limit");
    closeConnectionSoon();
  }

  void onGatedEof() override {
    // The client is gone. Nothing more will be read from this stream, so
    // end it rather than keep writing events nobody will collect.
    GOPHER_LOG_DEBUG("Peer half-closed during a response stream");
    finishResponseStream();
  }

  // ===== network::ConnectionCallbacks =====

  void onEvent(network::ConnectionEvent event) override {
    if (event != network::ConnectionEvent::RemoteClose &&
        event != network::ConnectionEvent::LocalClose) {
      return;
    }

    if (!is_server_ && client_session_) {
      if (role_ == ClientConnectionRole::ServerStream) {
        // The stream is over, and nobody here decides whether that
        // matters — what is reported is where it had got to, which is
        // the only thing that makes asking for it back worth anything.
        GOPHER_LOG_DEBUG("Server stream closed at {}",
                         last_event_id_.empty() ? "<nowhere>" : last_event_id_);
        mcp_callbacks_.onClientStreamEvent(
            ClientStreamEvent::Closed, optional<RequestId>(), last_event_id_);
      } else if (reading_event_stream_) {
        // An answer was still arriving. It is neither delivered nor
        // refused, so the request it belongs to is still outstanding —
        // the queue was never popped for it, which is why the front of
        // the queue is the request to name.
        GOPHER_LOG_DEBUG("Answer stream cut off at {}",
                         last_event_id_.empty() ? "<nowhere>" : last_event_id_);
        mcp_callbacks_.onClientStreamEvent(ClientStreamEvent::AnswerSevered,
                                           client_session_->peekAnswered(),
                                           last_event_id_);
      }
    }
    // Reaching here promptly is the reason the gate never disables socket
    // reads: an unarmed read event would delay or hide this entirely, and
    // a response stream would go on being written to nobody.
    if (response_writer_.mode() == http::ResponseWriter::Mode::Sse) {
      GOPHER_LOG_DEBUG("Connection closed with a response stream open");
      // The socket is going away, so there is nothing to flush — just
      // settle the writer and let the gate go.
      response_writer_.finish();
      response_writer_.drainTo(discard_);
      discard_.drain(discard_.length());
    }

    // Exchanges with work left decide for themselves whether to carry on.
    // Those that do need an owner that outlives this connection, which is
    // about to be destroyed along with this filter.
    auto survivors = exchanges_.onConnectionGone();
    if (!survivors.empty()) {
      if (retained_exchanges_ != nullptr) {
        for (const auto& survivor : survivors) {
          retained_exchanges_->retain(survivor);
        }
      } else {
        GOPHER_LOG_WARN(
            "{} exchange(s) asked to outlive their connection with nowhere "
            "to be held; releasing them",
            survivors.size());
      }
    }
  }

  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

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

  /**
   * A source of CORS headers for the request being answered.
   *
   * Handed to the codec and the route table, both of which can outlive
   * this filter in a caller's hands, so it stops answering once this
   * filter is gone rather than reading freed state.
   */
  std::function<std::map<std::string, std::string>()> corsSource(
      bool preflight) {
    std::weak_ptr<LifetimeToken> lifetime = lifetime_token_;
    auto* self = this;
    return [self, lifetime, preflight]() {
      if (lifetime.expired()) {
        return std::map<std::string, std::string>();
      }
      return preflight
                 ? self->security_policy_.preflightHeaders(self->security_)
                 : self->security_policy_.responseHeaders(self->security_);
    };
  }

  /** The same headers, in the form the response writer takes. */
  http::ResponseWriter::HeaderList corsHeaderList() const {
    http::ResponseWriter::HeaderList list;
    for (const auto& header : security_policy_.responseHeaders(security_)) {
      list.emplace_back(header.first, header.second);
    }
    return list;
  }

  void setupRoutingHandlers() {
    // Register CORS preflight handler for all paths
    // Browser-based clients (like MCP Inspector) send OPTIONS before POST
    auto preflight = corsSource(/*preflight=*/true);
    auto corsHandler =
        [preflight](const HttpRoutingFilter::RequestContext& req) {
          HttpRoutingFilter::Response resp;
          resp.status_code = 204;  // No Content
          // What an actual request may use — methods, headers and how long the
          // answer may be cached — all decided from live configuration, since
          // the header list follows what the tools registered so far designate.
          for (const auto& header : preflight()) {
            resp.headers[header.first] = header.second;
          }
          resp.headers["Content-Length"] = "0";
          return resp;
        };

    auto healthHandler = [](const HttpRoutingFilter::RequestContext& req) {
      HttpRoutingFilter::Response resp;
      resp.status_code = 200;
      resp.headers["content-type"] = "application/json";
      resp.headers["cache-control"] = "no-cache";

      resp.body = R"({"status":"healthy","timestamp":)" +
                  std::to_string(std::time(nullptr)) + "}";

      resp.headers["content-length"] = std::to_string(resp.body.length());
      return resp;
    };

    // What this server is holding for clients that might come back, read
    // straight from where the streams report it. A bound nobody outside
    // the process can observe is a bound nobody can believe, and this is
    // the one number that says whether it is being kept.
    transport::StreamableSessionManager* sessions =
        streamable_options_.sessions;
    auto infoHandler = [sessions](
                           const HttpRoutingFilter::RequestContext& req) {
      HttpRoutingFilter::Response resp;
      resp.status_code = 200;
      resp.headers["content-type"] = "application/json";

      std::string retained = R"("sessions":0,"streams":0,)"
                             R"("retained_events":0,"retained_bytes":0)";
      if (sessions != nullptr) {
        const auto& accounting = sessions->accounting();
        retained = "\"sessions\":" + std::to_string(sessions->size()) +
                   ",\"streams\":" + std::to_string(sessions->streamCount()) +
                   ",\"retained_events\":" +
                   std::to_string(accounting ? accounting->events.load() : 0) +
                   ",\"retained_bytes\":" +
                   std::to_string(accounting ? accounting->bytes.load() : 0);
      }

      resp.body = R"({
        "server": "MCP Server",
        "protocols": ["http", "sse", "json-rpc"],
        "endpoints": {
          "health": "/health",
          "info": "/info",
          "json_rpc": "/rpc",
          "sse_events": "/events"
        },
        "streamable_http": {)" +
                  retained +
                  R"(},
        "version": "1.0.0"
      })";

      resp.headers["content-length"] = std::to_string(resp.body.length());
      return resp;
    };

    const std::string rpc_path = configured_rpc_path_;
    const std::string sse_path = configured_sse_path_;
    using Target = HttpRoutingFilter::RouteTarget;

    // MCP endpoint. GET and DELETE are placeholders until the standalone
    // event stream and session termination exist; they must answer 405
    // rather than reach a protocol layer that has no reply for them and
    // would leave the client waiting for its own timeout. The Allow
    // header is derived from this table, so it starts naming only the
    // methods below and widens on its own as routes are added.
    routing_filter_->addRoute("OPTIONS", rpc_path,
                              Target::handlerRoute(corsHandler));
    routing_filter_->addRoute("POST", rpc_path, Target::passThrough());
    // The standalone event stream, where everything the server says on its
    // own initiative goes. Sessions are the condition as much as the
    // setting is: the stream is a session's, and with no session to hang
    // it on there is nothing for a message to be routed to.
    if (rpc_path != sse_path) {
      if (streamable_options_.enable_get_stream &&
          streamable_options_.sessions != nullptr) {
        routing_filter_->addRoute("GET", rpc_path, Target::passThrough());
      } else {
        routing_filter_->addRoute("GET", rpc_path, Target::reject(405));
      }
    }
    // Serving DELETE is what advertises it: the Allow header is rendered
    // from this table, and a rejecting route is deliberately left out of
    // it. So a server that does not let clients end their own sessions
    // answers 405 and never claims otherwise — and neither does one that
    // keeps no sessions, since there would be nothing to end.
    if (streamable_options_.allow_client_termination &&
        streamable_options_.sessions != nullptr) {
      routing_filter_->addRoute("DELETE", rpc_path, Target::passThrough());
    } else {
      routing_filter_->addRoute("DELETE", rpc_path, Target::reject(405));
    }

    // Event stream and the historic transport aliases.
    routing_filter_->addRoute("OPTIONS", sse_path,
                              Target::handlerRoute(corsHandler));
    routing_filter_->addRoute("GET", sse_path, Target::passThrough());
    routing_filter_->addRoute("POST", "/rpc", Target::passThrough());
    routing_filter_->addRoute("GET", "/events", Target::passThrough());
    routing_filter_->addRoute("GET", "/mcp/events", Target::passThrough());

    // Preflight on the well-known literal paths, which stay reachable
    // whatever the endpoint paths are configured to.
    for (const char* preflight_path :
         {"/mcp", "/mcp/events", "/rpc", "/health", "/info"}) {
      routing_filter_->addRoute("OPTIONS", preflight_path,
                                Target::handlerRoute(corsHandler));
    }

    routing_filter_->addRoute("GET", "/health",
                              Target::handlerRoute(healthHandler));
    routing_filter_->addRoute("GET", "/info",
                              Target::handlerRoute(infoHandler));

    // Default handler - handle OPTIONS for CORS preflight on any path, pass
    // through the remaining transport paths to protocol handling, and return a
    // definitive 404 for everything else. Unknown paths must get an immediate
    // response; otherwise they fall through to a protocol layer that has no
    // request to answer and the connection can wait until client timeout.
    // The MCP endpoint is deliberately absent from the pass-through list: it
    // is fully described by the table above, so an unlisted method on it gets
    // a 404 here instead of falling through and hanging.
    routing_filter_->registerDefaultHandler(
        [sse_path, preflight](const HttpRoutingFilter::RequestContext& req) {
          // Handle OPTIONS for CORS preflight on any path
          if (req.method == "OPTIONS") {
            HttpRoutingFilter::Response resp;
            resp.status_code = 204;  // No Content
            for (const auto& header : preflight()) {
              resp.headers[header.first] = header.second;
            }
            resp.headers["Content-Length"] = "0";
            return resp;
          }

          std::string path = req.path;
          auto query_start = path.find('?');
          if (query_start != std::string::npos) {
            path = path.substr(0, query_start);
          }

          const bool is_transport_path =
              path == sse_path || path == "/rpc" || path == "/events" ||
              path == "/mcp/events" ||
              (req.method == "POST" &&
               path.find("/callback/") != std::string::npos);
          if (is_transport_path) {
            HttpRoutingFilter::Response resp;
            resp.status_code = 0;
            return resp;
          }

          HttpRoutingFilter::Response resp;
          resp.status_code = 404;
          resp.headers["content-type"] = "application/json";
          resp.body = R"({"error":"not_found"})";
          resp.headers["content-length"] = std::to_string(resp.body.length());
          return resp;
        });

    // Call custom route registration callback if provided
    // This allows users to register additional endpoints like OAuth discovery
    if (route_registration_callback_) {
      route_registration_callback_(routing_filter_.get());
    }
  }

  /**
   * Check if this connection is in SSE mode for data routing purposes.
   * Server: true when mode is SseStream.
   * Client: true when the state machine has reached Active (Content-Type:
   *         text/event-stream was seen). The state machine can reach
   *         Active from WaitingForEndpoint directly because Content-Type
   *         detection (in HTTP headers) precedes the endpoint SSE event
   *         (in the HTTP body).
   */
  bool isSseActive() const {
    if (server_mode_) {
      return server_mode_->isSseStream();
    }
    if (reading_event_stream_) {
      return true;
    }
    if (client_sse_sm_) {
      return client_sse_sm_->isReady();
    }
    return false;
  }

  /**
   * Client mode: the response being read is a refusal, so its body is
   * an explanation rather than an answer. Anything outside 2xx — a
   * client that asked wrongly, a session that is gone, a server that
   * broke — has nothing in it for the message layer.
   */
  bool isRefusal() const {
    return response_status_ != 0 &&
           (response_status_ < 200 || response_status_ >= 300);
  }

  event::Dispatcher& dispatcher_;
  McpProtocolCallbacks& mcp_callbacks_;
  bool is_server_;
  bool client_accepts_sse_{
      false};  // Track if client supports SSE (Accept header)
  // (sse_headers_written_ removed — tracked by
  // server_mode_->sseHeadersWritten())

  // SSE client endpoint configuration
  std::string http_path_{"/rpc"};       // Default HTTP path for requests
  std::string http_host_{"localhost"};  // Default HTTP host for requests
  std::map<std::string, std::string> client_headers_;
  std::shared_ptr<std::map<std::string, std::string>> client_header_source_;

  // SSE server transport (only meaningful when is_server_ == true).
  std::string configured_sse_path_{"/sse"};
  std::string configured_rpc_path_{"/mcp"};
  std::string configured_external_url_;
  // Registry of live SSE session IDs → their stream connections. Shared
  // with sibling filter instances on the same factory; null in client
  // mode and in server mode when the factory wasn't built with SSE
  // server transport (back-compat default constructors).
  SseSessionRegistry* sse_registry_{nullptr};
  // (sse_server_mode_ removed — tracked by server_mode_->isSseStream())
  // (sse_writing_handshake_ removed — tracked by
  // server_mode_->isWritingHandshake()) Session ID this connection's SSE stream
  // is registered under. Populated when we handle GET /sse, cleared in the
  // destructor.
  std::string sse_session_id_;
  // Session ID parsed from an incoming POST /callback/{id}. Non-empty
  // means onWrite should redirect the response through the SSE stream
  // registered under this ID instead of writing back to the POST
  // connection.
  std::string sse_callback_session_id_;
  // Session ID from Mcp-Session-Id on Streamable HTTP requests. This is the
  // durable request-session identity for POST /mcp clients that do not use the
  // SSE callback path.
  std::string streamable_http_session_id_;

  // Messages queued during SSE endpoint negotiation (client mode only).
  // Drained once the state machine reaches EndpointReceived.
  std::vector<OwnedBuffer> pending_messages_;

  // Client-side SSE negotiation state machine. Tracks the SSE endpoint
  // negotiation lifecycle with validated transitions, timeout handling,
  // and state history. Null for server-mode filters.
  std::unique_ptr<ClientSseStateMachine> client_sse_sm_;

  // Server-side connection mode state machine. Tracks the per-connection
  // mode (PlainHttp, SseStream, CallbackProxy) with validated transitions
  // and RAII handshake write guard. Null for client-mode filters.
  std::unique_ptr<ServerConnectionMode> server_mode_;

  /**
   * End the response stream this connection is serving, if one is open,
   * putting the terminating bytes on the wire.
   */
  void finishResponseStream() {
    if (response_writer_.mode() != http::ResponseWriter::Mode::Sse) {
      return;
    }
    // finish() notifies the observer, which is us: the gate reopens and a
    // close is scheduled if the exchange asked for one. Both are deferred
    // or harmless, so the terminating bytes below still go out first.
    response_writer_.finish();

    OwnedBuffer tail;
    response_writer_.drainTo(tail);
    if (tail.length() > 0 && write_callbacks_ && server_mode_) {
      HandshakeWriteGuard guard(*server_mode_);
      write_callbacks_->connection().write(tail, false);
    }
  }

  /**
   * Close the connection from a later dispatcher turn.
   *
   * Never inline: the stream lifecycle is driven from inside onWrite,
   * which itself runs inside a connection write. Closing there hands
   * control back to a write that is still holding a buffer, and the bytes
   * just serialized are the ones that get dropped.
   */
  void closeConnectionSoon() {
    if (!write_callbacks_) {
      return;
    }
    network::Connection* connection = &write_callbacks_->connection();
    std::weak_ptr<LifetimeToken> lifetime = lifetime_token_;
    dispatcher_.post([lifetime, connection]() {
      if (lifetime.expired()) {
        return;
      }
      connection->close(network::ConnectionCloseType::FlushWrite);
    });
  }

  struct LifetimeToken {};
  std::shared_ptr<LifetimeToken> lifetime_token_{
      std::make_shared<LifetimeToken>()};

  bool connection_callbacks_registered_{false};
  // Somewhere to put bytes that can no longer be sent.
  OwnedBuffer discard_;

  // The exchanges this connection currently has in flight, and where any
  // that outlive it are handed off to. The store is owned by the factory,
  // which outlives every connection it built.
  transport::RetainedExchangeStore* retained_exchanges_{nullptr};
  transport::ExchangeRegistry exchanges_;

  // Protocol revision the request being handled states it speaks.
  std::string request_protocol_version_{protocol::kLegacyAssumedVersion};

  // What this connection does with requests that arrive while a response
  // stream is open.
  StreamGatePolicy stream_gate_policy_{StreamGatePolicy::Off};
  // Read while the routes are laid out, since what this endpoint serves
  // decides which methods the table admits and therefore advertises.
  StreamableHttpOptions streamable_options_;

  // Client mode: the session this connection is one of possibly several
  // to serve. Shared rather than owned — it was here before this
  // connection and will be here after it.
  transport::StreamableHttpClientSessionPtr client_session_;

  // Client mode: what this connection is for. A stream's answers are
  // nobody's answers, so it must not take places in the queue that says
  // whose they are.
  ClientConnectionRole role_{ClientConnectionRole::Requests};

  // Client mode, stream connection only: bumped once per read, so that
  // whoever watches for a stream gone quiet can tell silence from a
  // keep-alive without a callback per read.
  std::shared_ptr<std::atomic<uint64_t>> stream_activity_;

  // Client mode: the status of the response being read, and the body of
  // it when that status is a refusal. Both are cleared as the response
  // ends, which is the only point at which the request it answered can
  // be named.
  int response_status_{0};
  std::string refusal_body_;

  // Client mode: this response is arriving as an event stream. Per
  // response rather than per connection, because the same connection
  // answers the next request whichever way suits it.
  bool reading_event_stream_{false};

  // Client mode: where the stream being read has got to. What a client
  // that lost this stream comes back saying, so it is given what it
  // missed rather than the stream from the top.
  std::string last_event_id_;

  // Frames the response for the event stream this connection is serving,
  // from the prelude through every event. One writer per stream, so the
  // framing it chose up front stays consistent for the life of the stream.
  http::ResponseWriter response_writer_;

  // Who this connection serves, and what it decided about the request it
  // is serving now. Server mode only: a client receives no requests, so
  // there is nothing to judge. The record is read by whoever frames the
  // answer, which by then is several layers away from the request.
  HttpSecurityPolicy security_policy_;
  RequestSecurity security_;
  std::unique_ptr<HttpSecurityFilter> security_filter_;

  // Serves the MCP endpoint. Server mode only; a client never receives a
  // request on it.
  std::unique_ptr<StreamableHttpFilter> streamable_filter_;

  // Protocol filters
  std::shared_ptr<HttpCodecFilter> http_filter_;
  std::shared_ptr<HttpRoutingFilter>
      routing_filter_;  // Routing filter (shared for lifetime management)
  std::shared_ptr<SseCodecFilter> sse_filter_;
  std::shared_ptr<JsonRpcProtocolFilter> jsonrpc_filter_;

  // Filter callbacks
  network::ReadFilterCallbacks* read_callbacks_{nullptr};
  network::WriteFilterCallbacks* write_callbacks_{nullptr};

  // Connection reference for response routing
  network::Connection* connection_{nullptr};

  // Buffered data
  OwnedBuffer pending_json_data_;
  OwnedBuffer pending_sse_data_;  // For accumulating SSE event stream data

  // Custom route registration callback
  HttpRouteRegistrationCallback route_registration_callback_;
};

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
    const std::string& external_url,
    const std::map<std::string, std::string>& client_headers,
    const std::shared_ptr<std::map<std::string, std::string>>&
        client_header_source)
    : dispatcher_(dispatcher),
      message_callbacks_(message_callbacks),
      is_server_(is_server),
      http_path_(http_path),
      http_host_(http_host),
      client_headers_(client_headers),
      client_header_source_(client_header_source),
      use_sse_(use_sse),
      sse_path_(sse_path),
      rpc_path_(rpc_path),
      external_url_(external_url) {}

// Out-of-line destructor so the unique_ptr<SseSessionRegistry> member
// can see the complete type from this translation unit.
HttpSseFilterChainFactory::~HttpSseFilterChainFactory() = default;

SseSessionRegistry& HttpSseFilterChainFactory::sseRegistry() {
  assert(is_server_ && "sseRegistry() is only meaningful in server mode");
  // Same lazy construction as createFilterChain, so whichever runs first
  // wins and both hand out the one instance.
  if (!sse_registry_) {
    sse_registry_.reset(new SseSessionRegistry(dispatcher_));
  }
  return *sse_registry_;
}

transport::RetainedExchangeStore& HttpSseFilterChainFactory::retainedExchanges()
    const {
  // Lazily built, like the session registry, and for the same reason: it
  // has to outlive every connection the factory builds, and the factory is
  // the nearest thing that does.
  if (!retained_exchanges_) {
    retained_exchanges_.reset(
        new transport::RetainedExchangeStore(dispatcher_));
    retained_exchanges_->setRetention(closed_stream_retention_);
  }
  return *retained_exchanges_;
}

transport::StreamableSessionManager* HttpSseFilterChainFactory::sessionManager()
    const {
  if (!sessions_enabled_) {
    // Stateless. Null is the whole of it: with nothing to keep sessions
    // in, no connection here can mint one or believe one.
    return nullptr;
  }
  if (shared_session_manager_ != nullptr) {
    return shared_session_manager_;
  }
  if (!session_manager_) {
    session_manager_.reset(
        new transport::StreamableSessionManager(dispatcher_));
    session_manager_->setTimeout(session_timeout_);
    session_manager_->setPendingLimit(pending_limit_);
    session_manager_->setClosedStreamRetention(closed_stream_retention_);
  }
  return session_manager_.get();
}

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
      external_url_, client_headers_, client_header_source_,
      sse_registry_.get(), stream_gate_policy_, gated_input_limit_,
      &retainedExchanges(), security_options_, streamableOptions(),
      client_session_, client_role_, stream_activity_);

  // Add as both read and write filter. The FilterManager owns the filter
  // for the connection's lifetime (per-connection filter ownership): when
  // the connection is destroyed its FilterManager drops these refs and the
  // filter destructs, which is what deregisters its SSE stream from the
  // registry. The factory deliberately keeps NO reference of its own — a
  // retained copy would keep every connection's filters (and their buffers)
  // alive for the whole life of the server, and leave the filter destructor
  // to run only at shutdown instead of at connection close.
  filter_manager.addReadFilter(combined_filter);
  filter_manager.addWriteFilter(combined_filter);

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
