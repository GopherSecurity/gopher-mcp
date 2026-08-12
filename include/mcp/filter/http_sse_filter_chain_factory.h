#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "mcp/event/event_loop.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/http_security_filter.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/filter/streamable_http_filter.h"
#include "mcp/network/connection.h"
#include "mcp/network/filter.h"
#include "mcp/transport/exchange_registry.h"
#include "mcp/transport/streamable_http_client_session.h"
#include "mcp/transport/streamable_http_config.h"
#include "mcp/transport/streamable_session_manager.h"

// Forward declarations
namespace mcp {
class McpProtocolCallbacks;

namespace filter {
class HttpRoutingFilter;
class MetricsFilter;
class SseSessionRegistry;
}  // namespace filter
}  // namespace mcp

namespace mcp {
namespace filter {

/**
 * Callback type for registering custom HTTP routes
 * Called with the HttpRoutingFilter when setting up the filter chain.
 * Use this to register custom endpoints (e.g., OAuth discovery, health checks).
 */
using HttpRouteRegistrationCallback = std::function<void(HttpRoutingFilter*)>;

/**
 * What a connection does about further requests while it is streaming a
 * response.
 *
 * HTTP/1.1 delivers responses in request order, so a request arriving
 * behind an open stream cannot be answered until that stream ends. There
 * are only two wire-legal answers to that, and which one applies is a
 * deployment decision:
 *
 *   Off             Nothing is held back. Correct only while no response on
 *                   the connection actually streams.
 *   DecoderGate     Hold the request unparsed until the stream finishes,
 *                   then answer it in order. Keeps the connection reusable.
 *   SingleUseClose  Tell the client the connection ends with this response
 *                   and close when the stream does.
 *
 * Answering a queued request early with an error is not an option — that
 * is not something the protocol permits.
 */
enum class StreamGatePolicy { Off, DecoderGate, SingleUseClose };

/**
 * What a Streamable HTTP client connection is for.
 *
 * A session's requests and its stream cannot share one: a stream held
 * open occupies the connection, and every request would queue behind
 * it. So there are two, and they behave differently in one way that
 * matters — a response on the stream is not answering any request, and
 * must not take a place in the queue that says which answer belongs to
 * which request.
 */
enum class ClientConnectionRole { Requests, ServerStream };

/**
 * MCP HTTP+SSE Filter Chain Factory
 *
 * Following production FilterChainFactory pattern:
 * - Creates complete protocol stack for HTTP+SSE transport
 * - Each filter handles exactly one protocol layer
 * - Transport socket handles ONLY raw I/O
 *
 * Filter Chain Architecture:
 * ```
 * Client Mode:
 *   [TCP Socket] → [HTTP Codec] → [SSE Codec] → [JSON-RPC] → [Application]
 *   - HTTP Codec: Generates HTTP requests, parses HTTP responses
 *   - SSE Codec: Parses SSE events from response stream
 *   - JSON-RPC: Handles JSON-RPC protocol messages
 *
 * Server Mode:
 *   [TCP Socket] → [HTTP Codec] → [SSE Codec] → [JSON-RPC] → [Application]
 *   - HTTP Codec: Parses HTTP requests, generates HTTP responses
 *   - SSE Codec: Generates SSE events for response stream
 *   - JSON-RPC: Handles JSON-RPC protocol messages
 * ```
 *
 */
class HttpSseFilterChainFactory : public network::FilterChainFactory {
 public:
  /**
   * Constructor
   * @param dispatcher Event dispatcher for async operations
   * @param message_callbacks MCP message callbacks for handling requests
   * @param is_server True for server mode, false for client mode
   * @param http_path HTTP request path for client mode (e.g., "/sse")
   * @param http_host HTTP Host header value for client mode
   * @param use_sse True for SSE mode (GET /sse first), false for Streamable
   * HTTP (direct POST)
   * @param sse_path Server-side SSE endpoint path (e.g., "/sse"). Only
   *                 meaningful when is_server=true.
   * @param rpc_path Server-side JSON-RPC endpoint path (e.g., "/mcp"). Only
   *                 meaningful when is_server=true.
   * @param external_url Absolute URL the server is reachable at from the
   *                 client's perspective. Used to build the endpoint-event
   *                 callback URL advertised on GET /sse. Leave empty to
   *                 derive the URL from the incoming Host header.
   */
  HttpSseFilterChainFactory(
      event::Dispatcher& dispatcher,
      McpProtocolCallbacks& message_callbacks,
      bool is_server = true,
      const std::string& http_path = "/rpc",
      const std::string& http_host = "localhost",
      bool use_sse = true,
      const std::string& sse_path = "/sse",
      const std::string& rpc_path = "/mcp",
      const std::string& external_url = "",
      const std::map<std::string, std::string>& client_headers = {},
      const std::shared_ptr<std::map<std::string, std::string>>&
          client_header_source = nullptr);

  // Destructor defined out-of-line so the unique_ptr<SseSessionRegistry>
  // member can use the incomplete forward-declared type in this header.
  ~HttpSseFilterChainFactory();

  /**
   * Create filter chain for the connection
   * Following production pattern from FilterChainManager
   *
   * @param filter_manager The filter manager to add filters to
   * @return true if filter chain was created successfully
   */
  bool createFilterChain(network::FilterManager& filter_manager) const override;

  /**
   * Create network filter chain (alternative interface)
   * Following production pattern from FilterChainManager
   */
  bool createNetworkFilterChain(network::FilterManager& filter_manager,
                                const std::vector<network::FilterFactoryCb>&
                                    filter_factories) const override;

  /**
   * Create listener filter chain
   * Not used for this implementation
   */
  bool createListenerFilterChain(
      network::FilterManager& filter_manager) const override {
    return false;
  }

  /**
   * Enable metrics collection
   * When true, adds MetricsFilter to the chain
   */
  void enableMetrics(bool enable = true) { enable_metrics_ = enable; }

  /**
   * Add a filter factory that runs before protocol filters
   * Filter factories are invoked in order during chain creation.
   * The created filters process data before HTTP/SSE/JSON-RPC protocol filters.
   * Useful for authentication, logging, or other cross-cutting concerns.
   *
   * This follows the existing FilterFactoryCb pattern used by
   * FilterChainFactoryImpl and createNetworkFilterChain().
   *
   * @param factory Factory callback that creates a filter instance
   */
  void addFilterFactory(network::FilterFactoryCb factory) {
    filter_factories_.push_back(std::move(factory));
  }

  /**
   * Get the list of filter factories
   * @return Vector of filter factories
   */
  const std::vector<network::FilterFactoryCb>& getFilterFactories() const {
    return filter_factories_;
  }

  /**
   * Set callback for registering custom HTTP routes
   * The callback will be invoked when the filter chain is created,
   * allowing registration of custom endpoints like OAuth discovery.
   *
   * @param callback Function to call with the HttpRoutingFilter
   */
  void setRouteRegistrationCallback(HttpRouteRegistrationCallback callback) {
    route_registration_callback_ = std::move(callback);
  }

  /**
   * Get the route registration callback
   * @return The callback, or nullptr if not set
   */
  const HttpRouteRegistrationCallback& getRouteRegistrationCallback() const {
    return route_registration_callback_;
  }

  /**
   * Choose what connections do about requests that arrive while a response
   * is streaming. Defaults to Off, which is correct as long as no response
   * on this chain streams while another request could be in flight.
   */
  void setStreamGatePolicy(StreamGatePolicy policy) {
    stream_gate_policy_ = policy;
  }

  StreamGatePolicy streamGatePolicy() const { return stream_gate_policy_; }

  /** Cap on input held back while a stream is open. */
  void setGatedInputLimit(size_t bytes) { gated_input_limit_ = bytes; }

  /**
   * Which origins the connections this factory builds will serve.
   *
   * A setter rather than more constructor arguments: the constructor
   * already takes twelve, and none of its callers should have to change
   * to say nothing about security. Applies to chains built afterwards.
   */
  void setSecurityConfig(const transport::StreamableHttpConfig& config) {
    security_options_.allowed_origins = config.allowed_origins;
  }

  /**
   * What connections here do about a request that arrives while a response
   * is still streaming.
   *
   * Taken from configuration rather than left at the default, because a
   * chain that can stream and does not hold the next request will answer
   * it out of order, which HTTP/1.1 gives a client no way to read.
   */
  void setStreamConfig(const transport::StreamableHttpConfig& config) {
    stream_gate_policy_ = config.stream_conn_policy ==
                                  transport::StreamableHttpConfig::
                                      StreamConnPolicy::SingleUseClose
                              ? StreamGatePolicy::SingleUseClose
                              : StreamGatePolicy::DecoderGate;
    gated_input_limit_ = config.gated_input_buffer_bytes;
  }

  /**
   * Whether the connections here keep sessions, and for how long an idle
   * one is kept.
   *
   * Turning them off is stateless mode, and means more than never minting
   * one: an inbound session id is disregarded rather than believed. A
   * server that keeps no sessions has no way to tell whose id it was
   * handed, so passing one on would let a caller name any session it liked
   * and be given whatever sits under it.
   */
  void setSessionConfig(const transport::StreamableHttpConfig& config) {
    sessions_enabled_ = config.enable_sessions;
    session_timeout_ = config.session_timeout;
    streamable_options_.protocol_versions =
        transport::servedProtocolVersions(config);
    streamable_options_.enable_modern_era = config.enable_modern_era;
    streamable_options_.require_principal_match =
        config.require_principal_match;
    streamable_options_.allow_client_termination =
        config.allow_client_termination;
    streamable_options_.enable_get_stream = config.enable_get_stream;
    streamable_options_.max_get_streams_per_session =
        config.max_get_streams_per_session;
    streamable_options_.keepalive_interval = config.keepalive_interval;
    streamable_options_.enable_resumability = config.enable_resumability;
    streamable_options_.replay_buffer_events = config.replay_buffer_events;
    pending_limit_ = config.replay_buffer_events;
    closed_stream_retention_ = config.closed_stream_retention;
    if (session_manager_) {
      session_manager_->setTimeout(session_timeout_);
      session_manager_->setPendingLimit(pending_limit_);
      session_manager_->setClosedStreamRetention(closed_stream_retention_);
    }
    if (retained_exchanges_) {
      // The same window under one setting. A stream kept past the
      // exchange producing it would claim to be replayable with nothing
      // behind it, and an exchange kept past its stream would be held for
      // a client with no way left to ask.
      retained_exchanges_->setRetention(closed_stream_retention_);
    }
  }

  /**
   * The sessions the connections here serve, created on first use like the
   * other things that have to outlive any one connection. Null when this
   * factory builds stateless chains.
   */
  transport::StreamableSessionManager* sessionManager() const;

  /**
   * The same manager, as something a reference can be held to.
   *
   * Handed to another factory that is to serve the same sessions: a
   * borrowed raw pointer would leave that factory unable to say whether
   * the manager is still there when one of its hops comes back.
   */
  std::shared_ptr<transport::StreamableSessionManager> sessionManagerShared()
      const;

  /**
   * Serve sessions someone else keeps, rather than a set of this
   * factory's own.
   *
   * For a deployment with more than one listener: a session belongs to
   * one conversation, not to the socket it was created on, so a client
   * that reconnects elsewhere has to find it.
   *
   * Takes a shared reference rather than a borrowed pointer, and that
   * is a deliberate break for anyone calling this: a session visit that
   * crosses threads holds the manager for the length of the visit, and
   * it can only hold something shared. A caller passing a manager it
   * owns outright has no way to say whether that manager still exists
   * when the visit comes back. Use sessionManagerShared() to obtain one
   * from another factory.
   */
  void setSessionManager(
      const std::shared_ptr<transport::StreamableSessionManager>& manager) {
    shared_session_manager_ = manager;
  }

  /** Everything the MCP endpoint serves besides the requests themselves. */
  StreamableHttpOptions streamableOptions() const {
    StreamableHttpOptions options = streamable_options_;
    options.sessions = sessionManager();
    options.designated_params = designated_params_;
    return options;
  }

  /**
   * Where to ask which arguments a tool mirrors into headers.
   *
   * Borrowed, and expected to outlive this factory: it is the tool
   * registry of the server the endpoint belongs to. Null is a deployment
   * that designates none, and then none of the checking exists either.
   */
  void setDesignatedParams(
      const protocol::modern::DesignatedParamLookup* lookup) {
    designated_params_ = lookup;
  }

  /**
   * Client mode: the Streamable HTTP session the connections built here
   * take part in. Set rather than constructed because the session
   * outlives this factory's connections — it is the conversation, and
   * they are the sockets it happens over.
   */
  void setClientSession(
      const transport::StreamableHttpClientSessionPtr& session) {
    client_session_ = session;
  }

  /**
   * Client mode: what the connections built here are for.
   *
   * A session's requests and its stream go on different connections,
   * because a stream held open would have every request queued behind
   * it. They are told apart because a response on the stream is not
   * answering anything: taking a place in the queue of requests
   * awaiting answers would misname every answer after it.
   */
  void setClientRole(ClientConnectionRole role) { client_role_ = role; }

  /**
   * Client mode: bumped once per read on a stream connection, so that
   * whoever is watching for a stream that has gone quiet can tell
   * silence from a keep-alive. Counted rather than reported, because a
   * callback per read across a layer boundary would cost more than the
   * thing it is measuring.
   */
  void setStreamActivityCounter(
      const std::shared_ptr<std::atomic<uint64_t>>& counter) {
    stream_activity_ = counter;
  }

  /**
   * Client mode, older transport only: how long to wait for the server
   * to say where to post before giving up on it.
   *
   * The default is patient, because a client that has chosen this
   * transport is waiting for a server it expects to answer. A client
   * that has not chosen it — one asking whether this is what the server
   * speaks — sets it short, because the wait is the question.
   */
  void setNegotiationTimeout(std::chrono::milliseconds timeout) {
    negotiation_timeout_ = timeout;
  }

  /** Resolves who each request is from. Defaults to serving everyone. */
  void setAuthCallback(AuthCallback callback) {
    security_options_.auth = std::move(callback);
  }

  /**
   * Extra request header names to advertise in CORS preflight — the ones
   * registered tools designate for their parameters. Asked on every
   * preflight, since a tool may be registered at any time.
   */
  void setExtraAllowedHeaders(
      std::function<std::vector<std::string>()> source) {
    security_options_.extra_allowed_headers = std::move(source);
  }

  /**
   * Access the server-side SSE session registry, creating it on first use
   * (same lazy construction createFilterChain performs). This is the
   * server layer's handle for two things it cannot do from inside a
   * request cycle:
   *   - observing SSE stream teardown (setSessionClosedCallback), so the
   *     MCP session keyed on the stream can be released, and
   *   - pushing server-initiated messages (notifications) through a
   *     client's SSE stream.
   * Server-mode factories only; must be called on the dispatcher thread
   * the factory was built with, like every registry operation.
   */
  SseSessionRegistry& sseRegistry();

  /**
   * Exchanges that outlived the connection they were born on, so a client
   * that reconnects can be given what it missed. Held here because the
   * per-connection bookkeeping dies with its connection. Created on first
   * use, like the session registry.
   */
  transport::RetainedExchangeStore& retainedExchanges() const;

 private:
  event::Dispatcher& dispatcher_;
  McpProtocolCallbacks& message_callbacks_;
  bool is_server_;
  std::string http_path_;  // HTTP request path for client mode
  std::string http_host_;  // HTTP Host header for client mode
  std::map<std::string, std::string> client_headers_;
  std::shared_ptr<std::map<std::string, std::string>> client_header_source_;
  // Client mode: the Streamable HTTP session, or null in SSE mode and on
  // the server side.
  transport::StreamableHttpClientSessionPtr client_session_;
  ClientConnectionRole client_role_{ClientConnectionRole::Requests};
  std::shared_ptr<std::atomic<uint64_t>> stream_activity_;
  std::chrono::milliseconds negotiation_timeout_{30000};
  bool use_sse_;          // True for SSE mode, false for Streamable HTTP
  std::string sse_path_;  // Server-side SSE endpoint path (e.g., "/sse")
  std::string rpc_path_;  // Server-side JSON-RPC endpoint path (e.g., "/mcp")
  std::string external_url_;  // External URL for absolute SSE callback URLs
  // Where to ask which arguments a tool mirrors into headers. Borrowed;
  // null is a deployment that designates none.
  const protocol::modern::DesignatedParamLookup* designated_params_{nullptr};
  mutable bool enable_metrics_ = true;  // Enable metrics by default

  // SSE session registry — maps session IDs to the connections that are
  // streaming SSE to each client, so POST /callback/{id} handlers can
  // route a JSON-RPC response back through the correct stream. Lazily
  // constructed on the first server-side filter chain creation; stays
  // null for client-mode factories. Owned here (not a global singleton)
  // so each McpServer instance has an isolated registry and lifetime is
  // bounded by the factory, not process lifetime. Mutable because
  // createFilterChain is const on the base class.
  mutable std::unique_ptr<SseSessionRegistry> sse_registry_;

  // Exchanges handed over by connections that died with work still in
  // progress. Lives here rather than on a connection for the obvious
  // reason: the connection is what went away.
  mutable std::unique_ptr<transport::RetainedExchangeStore> retained_exchanges_;

  // The sessions these connections serve. Above the connection for the
  // same reason as the store: a session is what a client comes back to
  // after the connection it was created on has gone.
  // Shared rather than owned outright: a session visit that hops to
  // another thread holds the manager for the length of the visit, and
  // it can only do that if the manager is something a reference can be
  // held to.
  mutable std::shared_ptr<transport::StreamableSessionManager> session_manager_;
  // Someone else's, when a deployment keeps one set of sessions across
  // several listeners. Not owned, and used instead of the one above.
  std::shared_ptr<transport::StreamableSessionManager> shared_session_manager_;
  bool sessions_enabled_{true};
  std::chrono::milliseconds session_timeout_{300000};
  std::chrono::milliseconds closed_stream_retention_{60000};
  size_t pending_limit_{256};

  // What the MCP endpoint serves besides requests. Its session manager is
  // filled in per chain, since that is the part built lazily.
  StreamableHttpOptions streamable_options_;

  // NOTE: the factory intentionally does not retain the filters it creates.
  // Each connection's FilterManager owns its own filter-chain instance for
  // the connection's lifetime (per-connection ownership model), so a
  // factory-held copy would only leak — keeping every connection's filters
  // alive until the server shuts down and deferring the SSE-stream
  // deregistration done in the filter destructor to that same late moment.

  // Filter factories added by user (authentication, logging, etc.)
  // These are invoked during chain creation to add filters before protocol
  // filters Following the existing FilterFactoryCb pattern from
  // FilterChainFactoryImpl
  std::vector<network::FilterFactoryCb> filter_factories_;

  // Callback for registering custom HTTP routes
  HttpRouteRegistrationCallback route_registration_callback_;

  // How connections handle requests arriving behind an open response
  // stream. Off until a caller opts in, so existing chains are unchanged.
  StreamGatePolicy stream_gate_policy_{StreamGatePolicy::Off};
  size_t gated_input_limit_{64 * 1024};

  // Who the connections built here serve. Its defaults — the local
  // machine, everyone anonymous — are what a server gets when it says
  // nothing about security.
  HttpSecurityOptions security_options_;
};

}  // namespace filter
}  // namespace mcp
