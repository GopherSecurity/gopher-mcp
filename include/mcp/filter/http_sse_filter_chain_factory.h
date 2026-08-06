#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "mcp/event/event_loop.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/http_security_filter.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/network/connection.h"
#include "mcp/network/filter.h"
#include "mcp/transport/exchange_registry.h"
#include "mcp/transport/streamable_http_config.h"

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
  bool use_sse_;          // True for SSE mode, false for Streamable HTTP
  std::string sse_path_;  // Server-side SSE endpoint path (e.g., "/sse")
  std::string rpc_path_;  // Server-side JSON-RPC endpoint path (e.g., "/mcp")
  std::string external_url_;  // External URL for absolute SSE callback URLs
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
