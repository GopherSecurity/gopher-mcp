/**
 * @file mcp_server.cc
 * @brief Implementation of enterprise-grade MCP server using MCP abstraction
 * layer
 */

#include "mcp/server/mcp_server.h"

#include <algorithm>
#include <cassert>
#include <future>
#include <sstream>

#include "mcp/filter/filter_chain_assembler.h"
#include "mcp/filter/http_sse_filter_chain_factory.h"
#include "mcp/filter/json_rpc_filter_factory.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/filter/sse_session_registry.h"
#include "mcp/logging/log_macros.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/protocol/protocol_versions.h"
#include "mcp/transport/http_sse_transport_socket.h"
// NOTE: We'll implement connection handler directly in server for now
// to avoid conflicts with existing connection management in
// connection_manager.h
#include "mcp/config/listener_config.h"
#include "mcp/config/types.h"
#include "mcp/network/server_listener_impl.h"

namespace mcp {
namespace server {

namespace {

/**
 * The stream a deferred handler answers on, with the request's own
 * bookkeeping attached to it.
 *
 * A handler that answers by returning has an obvious end: the dispatch
 * that called it. One that answers later has none, so the thing that has
 * to happen when the request is finally over is hung on the only event
 * that means it is — the response going out.
 *
 * A handler that never answers leaves the request outstanding, exactly as
 * a handler that never returns would.
 */
class DeferredAnswer : public ResponseStream {
 public:
  DeferredAnswer(ResponseStreamPtr stream, std::function<void()> on_answered)
      : stream_(std::move(stream)), on_answered_(std::move(on_answered)) {}

  VoidResult sendNotification(
      const jsonrpc::Notification& notification) override {
    return stream_ ? stream_->sendNotification(notification) : noStream();
  }

  VoidResult sendRequest(const jsonrpc::Request& request) override {
    return stream_ ? stream_->sendRequest(request) : noStream();
  }

  VoidResult sendResponse(const jsonrpc::Response& response) override {
    auto sent = stream_ ? stream_->sendResponse(response) : noStream();
    // Told once, whether or not the write reached anyone: the request is
    // over either way, and a client that has gone — or a stream that was
    // never there — is not a reason to keep accounting for it.
    if (on_answered_) {
      auto finished = std::move(on_answered_);
      on_answered_ = nullptr;
      finished();
    }
    return sent;
  }

  bool alive() const override { return stream_ && stream_->alive(); }

 private:
  static VoidResult noStream() {
    return makeVoidError(
        Error(jsonrpc::INTERNAL_ERROR,
              "this request was dispatched with nowhere to answer it"));
  }

  ResponseStreamPtr stream_;
  std::function<void()> on_answered_;
};

/**
 * Split "scheme://host:port" into the host and port a listener binds.
 *
 * An address with no host binds the loopback interface, which is what a
 * locally installed server should reach and nothing else. Absent both, a
 * bare "http://" means the same as saying nothing at all.
 */
void parseHttpListenUrl(const std::string& url,
                        std::string* host,
                        uint16_t* port) {
  *host = "127.0.0.1";
  *port = 8080;

  const size_t authority = url.find("://");
  if (authority == std::string::npos) {
    return;
  }
  std::string rest = url.substr(authority + 3);
  const size_t path = rest.find('/');
  if (path != std::string::npos) {
    rest = rest.substr(0, path);
  }
  if (rest.empty()) {
    return;
  }

  // Only a colon after the closing bracket separates a port; the ones
  // inside an IPv6 literal belong to the address.
  const size_t search_from = rest[0] == '[' ? rest.find(']') : 0;
  const size_t colon = search_from == std::string::npos
                           ? std::string::npos
                           : rest.find(':', search_from);
  if (colon != std::string::npos) {
    try {
      *port = static_cast<uint16_t>(std::stoi(rest.substr(colon + 1)));
    } catch (...) {
      // Leave the default; an unreadable port is not worth failing over
      // when the address it belongs to is about to be checked anyway.
    }
    rest = rest.substr(0, colon);
  }
  if (!rest.empty()) {
    *host = rest;
  }
}

/** Strip the brackets an IPv6 literal wears in a URL. */
std::string unbracketed(const std::string& host) {
  if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
    return host.substr(1, host.size() - 2);
  }
  return host;
}

}  // namespace

VoidResult McpServer::resolveBindAddress(const std::string& url) {
  std::string host;
  uint16_t port = 0;
  parseHttpListenUrl(url, &host, &port);

  // Not an address, a name for one. The only name a server binds without
  // being told to reach the network is its own machine.
  if (host == "localhost") {
    host = "127.0.0.1";
  }

  auto address =
      network::Address::parseInternetAddressNoPort(unbracketed(host), port);
  if (!address || !address->ip()) {
    return makeVoidError(Error(
        jsonrpc::INVALID_PARAMS,
        "Cannot listen on '" + url + "': '" + host +
            "' is not an address this server can bind. Give an IP literal, "
            "or 'localhost'."));
  }

  const bool is_local = address->ip()->isLoopbackAddress();
  if (!is_local && !config_.streamable_http.allow_public_bind) {
    // Binding beyond the loopback interface puts the endpoint on the
    // network, where any host that can route to it can reach it. That is
    // a deployment decision, not a default.
    return makeVoidError(
        Error(jsonrpc::INVALID_PARAMS,
              "Refusing to listen on '" + address->asString() +
                  "': it is reachable from outside this machine. Set "
                  "streamable_http.allow_public_bind to allow it, or listen on "
                  "127.0.0.1."));
  }

  bind_address_ = address;
  return makeVoidSuccess();
}

// Constructor
McpServer::McpServer(const McpServerConfig& config)
    : ApplicationBase(config), config_(config), server_stats_() {
  // Initialize session manager for client connection tracking
  session_manager_ = std::make_unique<SessionManager>(config_, server_stats_);

  // Initialize resource manager for resource handling
  resource_manager_ = std::make_unique<ResourceManager>(server_stats_);

  // Release a session's resource subscriptions when the expiry sweep
  // removes it. Without this, a timed-out session's ids would linger in the
  // resource fan-out map forever (a slow leak, and a dead-id target for
  // every future update). Runs on the dispatcher thread (the only caller of
  // the sweep) with the SessionManager lock already released, so taking the
  // ResourceManager lock here is safe. Connection-close and SSE-stream-close
  // removals are handled separately by onConnectionLifecycleEvent and the
  // registry's session-closed callback.
  session_manager_->setSessionRemovedCallback(
      [this](const SessionManager::SessionPtr& session) {
        resource_manager_->releaseSession(*session);
      });

  // Initialize tool registry for tool management
  tool_registry_ = std::make_unique<ToolRegistry>(server_stats_);

  // Initialize prompt registry for prompt templates
  prompt_registry_ = std::make_unique<PromptRegistry>(server_stats_);

  // Create internal message callbacks instance
  // Following production pattern: separate callback interface from main class
  protocol_callbacks_ = std::make_unique<ServerProtocolCallbacks>(*this);

  // Register built-in request handlers
  registerBuiltinHandlers();
}

// Destructor
McpServer::~McpServer() {
  // Shutdown server gracefully
  shutdown();
}

// Start listening for connections
VoidResult McpServer::listen(const std::string& address) {
  // Store address for deferred listening in run()
  // Actual listening happens in dispatcher thread
  listen_address_ = address;

  // Checked here rather than where the bind happens: that runs on the
  // dispatcher thread, where a refusal can only be logged, and a server
  // that logs a refusal and carries on is a server nobody notices is
  // unreachable.
  bind_address_.reset();
  if (address.find("http://") == 0 || address.find("https://") == 0) {
    auto resolved = resolveBindAddress(address);
    if (!holds_alternative<std::nullptr_t>(resolved)) {
      GOPHER_LOG_ERROR("{}", get<Error>(resolved).message);
      return resolved;
    }
  }

  // Initialize the application if not already done
  if (!initialized_) {
    initialize();  // Create dispatchers and workers (including main dispatcher)

    // Verify main dispatcher was created
    if (!main_dispatcher_) {
      GOPHER_LOG_ERROR("Failed to create main dispatcher");
      return makeVoidError(
          Error(jsonrpc::INTERNAL_ERROR, "Dispatcher initialization failed"));
    }
  }

  // Just return success - actual listening will happen in run()
  return makeVoidSuccess();
}

// Internal method to perform actual listening
void McpServer::performListen() {
  // This is called from within the dispatcher thread
  // REFACTORED: Using improved listener and connection handler infrastructure
  // This provides better state management, connection lifecycle handling, and
  // robustness

  const std::string& address = listen_address_;

  try {
    // Transport selection flow (unchanged):
    // 1. Parse the address URL to determine transport type
    // 2. Create appropriate listener configuration
    // 3. Use listener management infrastructure for server connections

    // Determine transport type from address format:
    // - "stdio://" or no prefix -> stdio transport (uses stdin/stdout)
    // - "http://host:port" -> HTTP+SSE transport (TCP server)
    // - "https://host:port" -> HTTP+SSE with TLS (future)
    TransportType transport_type;
    if (address.find("stdio://") == 0 || address == "stdio") {
      transport_type = TransportType::Stdio;
    } else if (address.find("http://") == 0 || address.find("https://") == 0) {
      transport_type = TransportType::HttpSse;
    } else {
      // Default to stdio if no recognized prefix
      transport_type = TransportType::Stdio;
    }

    // IMPROVEMENT: Using TcpActiveListener for robust connection management
    // Following production patterns for better lifecycle handling
    // The listener manages connection acceptance, filter chains, and transport
    // sockets

    if (transport_type == TransportType::Stdio) {
      // Stdio transport: Use existing McpConnectionManager for now
      // TODO: Refactor stdio to use listener pattern
      McpConnectionConfig conn_config;
      conn_config.transport_type = TransportType::Stdio;
      conn_config.buffer_limit = config_.buffer_high_watermark;
      conn_config.connection_timeout = config_.request_processing_timeout;
      conn_config.stdio_config = config_.stdio_transport_config.has_value()
                                     ? config_.stdio_transport_config.value()
                                     : transport::StdioTransportSocketConfig();

      auto conn_manager = std::make_unique<McpConnectionManager>(
          *main_dispatcher_, *socket_interface_, conn_config);

      conn_manager->setProtocolCallbacks(*protocol_callbacks_);

      auto result = conn_manager->connect();
      if (holds_alternative<std::nullptr_t>(result)) {
        connection_managers_.push_back(std::move(conn_manager));
        GOPHER_LOG_DEBUG("Successfully started stdio transport");
      } else {
        auto error = get<Error>(result);
        GOPHER_LOG_ERROR("Failed to setup stdio transport: {}", error.message);
        main_dispatcher_->exit();
        return;
      }
    } else if (transport_type == TransportType::HttpSse) {
      // IMPROVEMENT: Use TcpListenerImpl and listener management for HTTP+SSE
      // This provides better connection lifecycle management and error handling

      // Resolved and checked by listen(), which is the only place that can
      // refuse an address and have anyone hear about it.
      if (!bind_address_) {
        GOPHER_LOG_ERROR("No bind address resolved for '{}'", address);
        main_dispatcher_->exit();
        return;
      }
      auto tcp_address = bind_address_;
      const uint32_t port = tcp_address->ip()->port();

      // Create TCP listener configuration following production patterns
      network::TcpListenerConfig tcp_config;
      tcp_config.name = "mcp_http_sse_listener";
      tcp_config.address = tcp_address;
      tcp_config.bind_to_port = true;
      tcp_config.enable_reuse_port = false;  // Single process listener

      // Following production architecture: Transport sockets handle ONLY I/O
      // Protocol processing happens in filters, not transport sockets
      // Flow: TCP Socket → RawBufferSocket (I/O) → HTTP Filter → SSE Filter →
      // App

      // Use RawBufferTransportSocketFactory for pure I/O
      // This follows production pattern where transport sockets don't know
      // protocols
      tcp_config.transport_socket_factory =
          std::make_shared<network::RawBufferTransportSocketFactory>();

      // Create filter chain factory that implements the protocol stack
      // Following production pattern: Filters handle ALL protocol logic
      // HTTP codec, SSE codec, and JSON-RPC are ALL filters

      // Use new config-driven constructor if filter chain config is provided
      std::unique_ptr<network::TcpActiveListener> tcp_listener;

      if (config_.filter_chain_config.has_value()) {
        GOPHER_LOG_INFO(
            "Using config-driven TCP listener with configurable filter chain");

        // Create a ListenerConfig from the filter chain configuration
        mcp::config::ListenerConfig listener_config;
        listener_config.name = "http_sse_listener";
        // The same checked address the built-in listener binds, so a
        // config-driven chain cannot quietly reach further than one.
        listener_config.address.socket_address.address =
            tcp_address->ip()->addressAsString();
        listener_config.address.socket_address.port_value = port;

        // Convert JsonValue filter chain config to FilterChainConfig
        const json::JsonValue& chain_config =
            config_.filter_chain_config.value();
        auto filter_chain_config =
            config::FilterChainConfig::fromJson(chain_config);
        listener_config.filter_chains.push_back(filter_chain_config);

        // Use new constructor that accepts mcp::config::ListenerConfig
        tcp_listener = std::make_unique<network::TcpActiveListener>(
            *main_dispatcher_, listener_config, *this);

        if (protocol_callbacks_) {
          tcp_listener->setProtocolCallbacks(*protocol_callbacks_);
        }

        // The config-driven chain assembles named filters and does not build
        // the SSE transport that owns the server->client push channel, so
        // http_sse_factory_ stays null on this path. Warn once at setup so an
        // operator relying on notifications/resources/updated (or any
        // server-initiated notification) is not left debugging silent
        // non-delivery — sendNotificationToSession reports the same reason
        // per call, but the listener is where the capability is decided.
        GOPHER_LOG_WARN(
            "Config-driven listener '{}' does not provide a server-initiated "
            "notification channel; resource-update and other server->client "
            "notifications will not be delivered on this listener. Use the "
            "built-in HTTP+SSE listener for server push.",
            listener_config.name);
      } else {
        // Fallback to old constructor with default HTTP/SSE factory for
        // backward compatibility
        GOPHER_LOG_INFO("Using default HTTP/SSE filter chain factory");

        // Pass the configured endpoint paths (and optional external_url
        // for reverse-proxy deployments) through to the filter chain so
        // the SSE server transport advertises callbacks under whatever
        // paths the operator picked in McpServerConfig. The trailing
        // defaults of the ctor cover use_sse and the client-mode
        // http_path/http_host — those are unused in server mode but
        // still positional in the signature.
        auto http_sse_factory =
            std::make_shared<filter::HttpSseFilterChainFactory>(
                *main_dispatcher_, *protocol_callbacks_,
                /*is_server=*/true,
                /*http_path=*/config_.http_rpc_path,
                /*http_host=*/"localhost",
                /*use_sse=*/true,
                /*sse_path=*/config_.http_sse_path,
                /*rpc_path=*/config_.http_rpc_path,
                /*external_url=*/config_.external_url);

        // Who this server serves. Without this the chain applies its own
        // defaults, which are the local machine only — safe, but not what
        // an operator who configured an origin list asked for.
        http_sse_factory->setSecurityConfig(config_.streamable_http);

        // A response on this chain can stream, so a request arriving behind
        // one has to wait rather than be answered out of order.
        http_sse_factory->setStreamConfig(config_.streamable_http);

        // Whether a client is given something to come back with, and how
        // long it stays worth coming back with.
        http_sse_factory->setSessionConfig(config_.streamable_http);
        // Where the endpoint asks which arguments a tool mirrors into
        // headers, so a call can be held to both halves of that rule.
        http_sse_factory->setDesignatedParams(&tool_designations_);

        // Tools can designate parameters to travel as request headers, and
        // a browser cannot send a header CORS preflight did not advertise.
        // Read per preflight rather than captured here: tools may be
        // registered long after the listener starts.
        http_sse_factory->setExtraAllowedHeaders([this]() {
          std::vector<std::string> names;
          // listTools() copies under the registry's own lock, which is
          // what makes this safe to call from a dispatcher thread while
          // another registers a tool.
          for (const auto& tool : tool_registry_->listTools().tools) {
            for (const auto& name :
                 filter::HttpSecurityPolicy::paramHeadersFor(tool)) {
              names.push_back(name);
            }
          }
          return names;
        });

        // Add filter factories from config (e.g., auth filters)
        // This follows the existing FilterFactoryCb pattern
        for (const auto& factory : config_.filter_factories) {
          if (factory) {
            http_sse_factory->addFilterFactory(factory);
            GOPHER_LOG_DEBUG("Added filter factory to HTTP/SSE filter chain");
          }
        }

        // Set route registration callback for custom HTTP endpoints
        if (config_.route_registration_callback) {
          http_sse_factory->setRouteRegistrationCallback(
              config_.route_registration_callback);
          GOPHER_LOG_DEBUG(
              "Set route registration callback for custom endpoints");
        }

        // Keep a handle on the factory: its SSE session registry is the
        // server's only route to a client's SSE stream outside a request
        // cycle (server-initiated notifications), and the teardown observer
        // below is what bounds the lifetime of transport-keyed sessions.
        http_sse_factory_ = http_sse_factory;

        // When a client's SSE stream closes, drop the MCP session keyed on
        // it and release its resource subscriptions — after this the
        // transport identity can never carry another message, so keeping
        // the session would only leak subscription state and let a later
        // stream accidentally inherit it if the registry reused the id.
        // Runs on the dispatcher thread (registry contract).
        http_sse_factory->sseRegistry().setSessionClosedCallback(
            [this](const std::string& transport_session_id) {
              auto session = session_manager_->removeSessionByTransportId(
                  transport_session_id);
              if (session) {
                resource_manager_->releaseSession(*session);
                GOPHER_LOG_DEBUG(
                    "Session {} released on SSE stream close (transport={})",
                    session->getId(), transport_session_id);
              }
            });

        // The same for a session that simply went quiet. Nothing else
        // bounds these: the application-layer sweep skips every session
        // keyed on a transport identity, on the understanding that the
        // transport is what ends them, and for this endpoint this is the
        // transport doing exactly that.
        if (auto* sessions = http_sse_factory->sessionManager()) {
          sessions->setSessionRemovedCallback(
              [this](const std::string& transport_session_id) {
                auto session = session_manager_->removeSessionByTransportId(
                    transport_session_id);
                if (session) {
                  resource_manager_->releaseSession(*session);
                  GOPHER_LOG_DEBUG(
                      "Session {} released with its transport session {}",
                      session->getId(), transport_session_id);
                }
              });
        }

        tcp_config.filter_chain_factory = http_sse_factory;

        tcp_config.backlog = 128;
        tcp_config.per_connection_buffer_limit = config_.buffer_high_watermark;
        tcp_config.max_connections_per_event =
            10;  // Process up to 10 accepts per event

        // Use old constructor that takes TcpListenerConfig
        tcp_listener = std::make_unique<network::TcpActiveListener>(
            *main_dispatcher_, std::move(tcp_config), *this);

        if (protocol_callbacks_) {
          tcp_listener->setProtocolCallbacks(*protocol_callbacks_);
        }
      }

      // Enable the listener to start accepting connections
      tcp_listener->enable();

      // Store the listener
      tcp_listeners_.push_back(std::move(tcp_listener));

      GOPHER_LOG_DEBUG("Successfully started HTTP+SSE listener on port {}",
                       port);
    } else {
      // Future: support for other transports
      auto net_address = network::Address::pipeAddress(address);

      network::TcpListenerConfig tcp_config;
      tcp_config.name = "mcp_pipe_listener";
      tcp_config.address = net_address;
      tcp_config.bind_to_port = true;
      tcp_config.per_connection_buffer_limit = config_.buffer_high_watermark;

      // Create listener
      auto tcp_listener = std::make_unique<network::TcpActiveListener>(
          *main_dispatcher_,
          std::move(tcp_config),  // Move config to avoid copying unique_ptrs
          *this);
      tcp_listener->enable();
      tcp_listeners_.push_back(std::move(tcp_listener));
    }

    server_running_ = true;

    // Start background tasks for session cleanup and resource updates
    startBackgroundTasks();

    // Successfully started listening
    GOPHER_LOG_INFO("Server started successfully!");
    GOPHER_LOG_INFO("Listening on {}", address);

  } catch (const std::exception& e) {
    GOPHER_LOG_ERROR("Failed to start transport: {}", e.what());
    // Exit the dispatcher on fatal error
    main_dispatcher_->exit();
  }
}

// Run the main event loop
// This should be called from the main thread after listen()
void McpServer::run() {
  // Check if we have either a listen address or pending listener configs
  if (listen_address_.empty() && pending_listener_configs_.empty()) {
    GOPHER_LOG_ERROR(
        "No listen address or listener configurations specified. "
        "Call listen() or createListenersFromConfig() first.");
    return;
  }

  if (!initialized_) {
    GOPHER_LOG_ERROR(
        "Server not initialized. Call listen() or "
        "createListenersFromConfig() first.");
    return;
  }

  // Set flag to perform listen on first run iteration
  need_perform_listen_ = !listen_address_.empty();
  bool need_start_listeners = !pending_listener_configs_.empty();

  // Override the base class run to handle listening in dispatcher thread
  GOPHER_LOG_INFO("Running main event loop");

  // Main dispatcher should already be created in initialize()
  if (!main_dispatcher_) {
    GOPHER_LOG_ERROR("Main dispatcher not initialized.");
    return;
  }

  // Run one iteration to establish thread ID
  main_dispatcher_->run(event::RunType::NonBlock);

  // Now we can safely perform listen in the dispatcher thread
  if (need_perform_listen_) {
    // Directly call performListen since we're now in the dispatcher thread
    performListen();
    need_perform_listen_ = false;
  }

  // Start listeners from configuration if provided
  if (need_start_listeners) {
    GOPHER_LOG_INFO("Starting listeners from configuration");
    for (const auto& listener_config : pending_listener_configs_) {
      startListener(listener_config);
    }
    pending_listener_configs_.clear();  // Clear after starting
    server_running_ = true;

    // Start background tasks for session cleanup and resource updates
    startBackgroundTasks();

    GOPHER_LOG_INFO("All listeners started successfully!");
  }

  // Following production pattern: use blocking dispatch loop
  // This properly waits for and processes file events
  GOPHER_LOG_INFO("Starting blocking dispatch loop");
  main_dispatcher_->run(event::RunType::Block);
  GOPHER_LOG_INFO("Main dispatch loop exited");
}

// Shutdown server
void McpServer::shutdown() {
  if (!server_running_) {
    return;
  }

  server_running_ = false;

  // Stop background tasks
  stopBackgroundTasks();

  // Close all connections in dispatcher context
  if (main_dispatcher_) {
    main_dispatcher_->post([this]() {
      // Every subscription is told it is ending rather than left to find
      // out from a connection that stopped answering. Before the
      // listeners go, while there is still something to say it on.
      subscriptions_.closeAll();

      // Stop listeners first so no new connections slip in while we drain.
      for (auto& listener : tcp_listeners_) {
        listener->disable();
      }
      tcp_listeners_.clear();

      // Drain active_connections_ on the dispatcher thread so each
      // ConnectionImpl destructor fires its closeSocket(LocalClose) here
      // rather than on whatever thread ends up running ~McpServer. That
      // routes LocalClose through the per-connection adapter back into
      // onConnectionLifecycleEvent, which asserts dispatcher-thread.
      //
      // server_running_ has already flipped to false above, so the
      // container-unwind branch inside onConnectionLifecycleEvent is
      // skipped — otherwise each destruction would try to erase its own
      // element from the vector we are currently clearing, which would
      // invalidate iterators and drop adapters referenced by
      // still-running callback chains.
      //
      // Order matters: connections must die before their lifecycle
      // adapters. The connection's callback list still references the
      // adapter during ~ConnectionImpl's closeSocket; destroying the
      // adapter first would be a use-after-free on that last callback.
      active_connections_.clear();
      lifecycle_callbacks_.clear();

      // Close legacy connection managers (for stdio).
      for (auto& conn_manager : connection_managers_) {
        conn_manager->close();
      }
      connection_managers_.clear();

      // Release the HTTP+SSE factory here, on the dispatcher thread, after
      // every connection is gone. The factory owns the SSE session registry
      // and keeps every created filter alive; if it instead died in
      // ~McpServer (caller thread), those filter destructors would call
      // registry.removeSession off the dispatcher thread and trip its
      // thread assert.
      http_sse_factory_.reset();

      // Exit the blocking dispatch loop.
      main_dispatcher_->exit();
    });
  }

  // Stop application base (stops workers and dispatchers)
  stop();
}

// Register request handler
void McpServer::registerRequestHandler(
    const std::string& method,
    std::function<jsonrpc::Response(const jsonrpc::Request&, SessionContext&)>
        handler) {
  std::lock_guard<std::mutex> lock(handlers_mutex_);
  request_handlers_[method] = handler;
  async_request_handlers_.erase(method);
  streaming_methods_.erase(method);
}

void McpServer::registerRequestHandler(
    const std::string& method,
    std::function<jsonrpc::Response(const jsonrpc::Request&, SessionContext&)>
        handler,
    StreamingMode streaming) {
  std::lock_guard<std::mutex> lock(handlers_mutex_);
  request_handlers_[method] = handler;
  async_request_handlers_.erase(method);
  if (streaming == StreamingMode::None) {
    streaming_methods_.erase(method);
  } else {
    streaming_methods_[method] = streaming;
  }
}

void McpServer::registerAsyncRequestHandler(const std::string& method,
                                            AsyncRequestHandler handler,
                                            StreamingMode streaming) {
  std::lock_guard<std::mutex> lock(handlers_mutex_);
  async_request_handlers_[method] = std::move(handler);
  request_handlers_.erase(method);
  // A handler that answers later needs somewhere to answer, and None is
  // the one mode that leaves it without one. Taken as Optional rather
  // than refused: what was meant is not in doubt.
  streaming_methods_[method] =
      streaming == StreamingMode::None ? StreamingMode::Optional : streaming;
}

void McpServer::forgetPendingRequest(const std::string& key) {
  std::lock_guard<std::mutex> lock(pending_requests_mutex_);
  pending_requests_.erase(key);
}

bool McpServer::ToolDesignations::paramsForTool(
    const std::string& tool_name,
    std::vector<protocol::modern::DesignatedParam>* out) const {
  if (!server_.tool_registry_ || out == nullptr) {
    return false;
  }
  return server_.tool_registry_->paramsForTool(tool_name, out);
}

bool McpServer::knowsMethod(const std::string& method) const {
  {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    if (request_handlers_.count(method) != 0 ||
        async_request_handlers_.count(method) != 0) {
      return true;
    }
  }

  // The built-ins, which onRequestWithContext answers directly. Listed
  // here rather than derived, because they are a list there too — and a
  // method that is served but not named here would be refused before it
  // ever reached the branch that serves it.
  static const char* const kBuiltIn[] = {
      "initialize",
      "ping",
      "resources/list",
      "resources/read",
      "resources/subscribe",
      "resources/unsubscribe",
      "tools/list",
      "tools/call",
      "prompts/list",
      "prompts/get",
      protocol::modern::kMethodServerDiscover};
  for (const char* known : kBuiltIn) {
    if (method == known) {
      return true;
    }
  }
  return false;
}

StreamingMode McpServer::streamingFor(const jsonrpc::Request& request) const {
  // Asked from the dispatcher thread while handlers may be registered from
  // another, so it reads under the same lock registration takes.
  std::lock_guard<std::mutex> lock(handlers_mutex_);
  auto it = streaming_methods_.find(request.method);
  return it == streaming_methods_.end() ? StreamingMode::None : it->second;
}

// Register notification handler
void McpServer::registerNotificationHandler(
    const std::string& method,
    std::function<void(const jsonrpc::Notification&, SessionContext&)>
        handler) {
  std::lock_guard<std::mutex> lock(handlers_mutex_);
  notification_handlers_[method] = handler;
}

// Deliver a notification to one session's client. Dispatcher thread only —
// every route below writes to a connection owned by that thread.
VoidResult McpServer::sendNotificationToSession(
    const SessionManager::SessionPtr& session,
    const jsonrpc::Notification& notification) {
  assert(main_dispatcher_ && main_dispatcher_->isThreadSafe() &&
         "sendNotificationToSession off dispatcher");

  // HTTP+SSE: the session is keyed on the client's SSE stream — route the
  // JSON through the registry, which writes it down the stream connection's
  // filter chain (the SSE codec frames it as a `data:` event). This is the
  // only channel that reaches an HTTP+SSE client outside a request cycle.
  if (!session->getTransportSessionId().empty()) {
    // No SSE registry means this listener has no server-initiated push
    // channel at all — the config-driven filter-chain path does not build
    // the SSE transport (see performListen). Distinguish that from a stream
    // that merely closed, so the failure is diagnosable rather than looking
    // like a transient disconnect.
    if (!http_sse_factory_) {
      return makeVoidError(
          Error(jsonrpc::INTERNAL_ERROR,
                "No server-initiated notification channel on this listener "
                "(server push requires the built-in HTTP+SSE listener)"));
    }
    auto json_val = json::to_json(notification);

    // Streamable HTTP first: its sessions are keyed on an id this server
    // minted, which the SSE registry has never heard of — asking it would
    // report a stream that never existed rather than the truth. A session
    // the manager does not know is an HTTP+SSE one, and falls through.
    if (auto* sessions = http_sse_factory_->sessionManager()) {
      const std::string& transport_id = session->getTransportSessionId();
      if (auto* streamable = sessions->find(transport_id)) {
        // Queued rather than sent when nothing is connected, which is a
        // delivery that has not happened yet rather than one that failed.
        sessions->routeUnsolicited(*streamable, json_val.toString());
        return makeVoidSuccess();
      }
    }

    if (http_sse_factory_->sseRegistry().sendResponse(
            session->getTransportSessionId(), json_val.toString())) {
      return makeVoidSuccess();
    }
    // Registry exists but the stream is gone (client disconnected between
    // lookup and send) — report failure so the caller doesn't assume
    // delivery.
    return makeVoidError(
        Error(jsonrpc::INTERNAL_ERROR,
              "SSE stream gone for session " + session->getId()));
  }

  // Connection-keyed session. Since the dispatch context supplies the
  // origin connection for every transport, stdio sessions are keyed on
  // the pipe connection too — and stdio DOES have a push channel: the
  // long-lived pipe, written through the connection manager that frames
  // for it. Route through the manager that owns the session's connection.
  // (ownsConnection only compares pointers, never dereferences, which is
  // safe even if the pointer dangles on the posted off-thread path.)
  if (session->getConnection()) {
    for (auto& conn_manager : connection_managers_) {
      if (conn_manager->isConnected() &&
          conn_manager->ownsConnection(session->getConnection())) {
        return conn_manager->sendNotification(notification);
      }
    }

    // No manager owns it: a plain-HTTP connection. HTTP/1.1 has no message
    // outside a request/response cycle, so writing raw JSON to the
    // connection would corrupt its framing. Such a session is also only
    // ever an alias of an HTTP+SSE client whose real push channel is the
    // SSE stream (the transport-keyed session above) — writing here too
    // would duplicate the delivery. Fail cleanly instead.
    return makeVoidError(
        Error(jsonrpc::INTERNAL_ERROR,
              "Session " + session->getId() +
                  " has no server-initiated notification channel"));
  }

  // No key at all: only reachable for sessions created by the context-free
  // legacy dispatch path. There is a single stdio client, reached through
  // its connection manager.
  for (auto& conn_manager : connection_managers_) {
    if (conn_manager->isConnected()) {
      return conn_manager->sendNotification(notification);
    }
  }
  return makeVoidError(
      Error(jsonrpc::INTERNAL_ERROR, "No active connection for session"));
}

// Push notifications/resources/updated to every subscriber of the URI
void McpServer::notifyResourceUpdate(const std::string& uri) {
  if (!main_dispatcher_) {
    return;
  }
  // Delivery writes to connections, which is dispatcher-thread work; hop
  // if the application called this from one of its own threads.
  if (!main_dispatcher_->isThreadSafe()) {
    main_dispatcher_->post([this, uri]() { notifyResourceUpdate(uri); });
    return;
  }

  Metadata params;
  params["uri"] = uri;
  jsonrpc::Notification notification("notifications/resources/updated", params);

  // Every subscription that named this resource. In the newest revision
  // this is the only channel such a notice has — there is no standalone
  // stream for it to arrive on.
  json::JsonValue update = json::JsonValue::object();
  update.set("uri", json::JsonValue(uri));
  subscriptions_.publish(protocol::modern::kNotificationResourcesUpdated,
                         update, uri);

  for (const auto& session_id : resource_manager_->getSubscribers(uri)) {
    auto session = session_manager_->getSession(session_id);
    if (!session) {
      // Subscriber's session expired between bookkeeping and fan-out;
      // nothing to deliver to.
      continue;
    }
    auto result = sendNotificationToSession(session, notification);
    if (holds_alternative<std::nullptr_t>(result)) {
      server_stats_.resource_updates_sent++;
    } else {
      GOPHER_LOG_DEBUG("Resource update for {} not delivered to session {}: {}",
                       uri, session_id, get<Error>(result).message);
    }
  }
}

// Send notification to specific session
VoidResult McpServer::sendNotification(
    const std::string& session_id, const jsonrpc::Notification& notification) {
  // Already on the dispatcher thread (e.g. called from a tool or request
  // handler): resolve and deliver inline, returning the real result. The
  // old post-and-wait here deadlocked the event loop — the future could
  // only be fulfilled by the very thread that was blocked on it.
  if (main_dispatcher_ && main_dispatcher_->isThreadSafe()) {
    auto session = session_manager_->getSession(session_id);
    if (!session) {
      return makeVoidError(Error(jsonrpc::INVALID_PARAMS, "Session not found"));
    }
    return sendNotificationToSession(session, notification);
  }

  if (!main_dispatcher_) {
    return makeVoidError(Error(jsonrpc::INTERNAL_ERROR, "Server not running"));
  }

  // Off-thread caller (application thread). Resolve the session now (the
  // lookup is mutex-guarded, so it is safe from any thread) purely to
  // report an unknown id synchronously — the common misuse. Then hand the
  // actual delivery to the dispatcher and return WITHOUT blocking on a
  // result future. The previous post-and-wait hung forever if the
  // dispatcher exited (shutdown) before running the task, since the promise
  // would then never be fulfilled. A notification is one-way, so a
  // fire-and-forget hand-off matching notifyResourceUpdate /
  // broadcastNotification is the right contract: success means "accepted
  // for delivery", not "delivered".
  //
  // The delivery task re-resolves the session by id rather than capturing
  // the SessionPtr, so it never pins (or acts on) a session whose
  // connection was freed between this call and the task running.
  if (!session_manager_->getSession(session_id)) {
    return makeVoidError(Error(jsonrpc::INVALID_PARAMS, "Session not found"));
  }
  main_dispatcher_->post([this, session_id, notification]() {
    auto session = session_manager_->getSession(session_id);
    if (session) {
      sendNotificationToSession(session, notification);
    }
  });
  return makeVoidSuccess();
}

// Broadcast notification to all sessions
void McpServer::broadcastNotification(
    const jsonrpc::Notification& notification) {
  // Same threading rule as sendNotification: inline when already on the
  // dispatcher thread, hop otherwise (fire-and-forget — broadcast has no
  // per-recipient result to report).
  if (!main_dispatcher_->isThreadSafe()) {
    main_dispatcher_->post(
        [this, notification]() { broadcastNotification(notification); });
    return;
  }

  // Deliver to each client exactly once through its real push channel.
  // Only two channels can carry a server-initiated message: an HTTP+SSE
  // client's SSE stream (its transport-keyed session) and the stdio
  // connection manager. Connection-keyed sessions are transient aliases of
  // HTTP connections (the SSE GET stream, each one-shot POST) with no push
  // channel of their own — enumerating them here is exactly what used to
  // double-deliver to SSE clients and inject raw JSON into POST exchanges,
  // so they are skipped.
  for (auto& session : session_manager_->getAllSessions()) {
    if (!session->getTransportSessionId().empty()) {
      sendNotificationToSession(session, notification);
    }
  }

  // stdio: a single client reached through its connection manager,
  // independent of whether it has created a session yet.
  for (auto& conn_manager : connection_managers_) {
    if (conn_manager->isConnected()) {
      conn_manager->sendNotification(notification);
    }
  }
}

void McpServer::dropSessionStream(const std::string& session_id,
                                  std::function<void(bool)> done) {
  auto report = [done](bool dropped) {
    if (done) {
      done(dropped);
    }
  };

  if (!http_sse_factory_) {
    // No endpoint of this kind on this listener, so no stream to drop.
    report(false);
    return;
  }
  auto sessions = http_sse_factory_->sessionManagerShared();
  if (!sessions) {
    report(false);
    return;
  }

  // The caller names the session this server knows; the manager knows it
  // under the id the client echoes, which is the one the stream hangs on.
  auto session = session_manager_->getSession(session_id);
  const std::string transport_id =
      session ? session->getTransportSessionId() : session_id;
  if (transport_id.empty()) {
    report(false);
    return;
  }

  if (!main_dispatcher_) {
    report(false);
    return;
  }

  auto dropped = std::make_shared<bool>(false);
  sessions->withSession(
      *main_dispatcher_, transport_id,
      [sessions, dropped](transport::SessionCtx& ctx) {
        auto* stream = transport::StreamableSessionManager::currentStream(ctx);
        if (stream == nullptr) {
          return;
        }
        // Finished on the wire and marked as having no client, together:
        // a stream ended without the second is one the retention window
        // never starts on, and it would hold its id and its replay buffer
        // for as long as the keep-alive connection under it lived.
        *dropped = sessions->endStream(ctx, *stream);
      },
      // Only once that has happened, so nothing said next is routed to a
      // stream that is on its way out rather than queued for the one the
      // client is about to open.
      [report, dropped](bool found) { report(found && *dropped); });
}

VoidResult McpServer::answerWithInput(
    const ResponseStreamPtr& stream,
    const jsonrpc::Request& request,
    SessionContext& session,
    const protocol::modern::NeedsInput& needed) {
  if (!stream) {
    return makeVoidError(
        Error(jsonrpc::INTERNAL_ERROR,
              "nothing to answer on: a handler that needs more input still "
              "has to have somewhere to say so"));
  }
  if (needed.empty()) {
    return makeVoidError(
        Error(jsonrpc::INTERNAL_ERROR,
              "asked for nothing and carried nothing, which tells the caller "
              "to come back with no reason and nothing to come back with"));
  }
  if (!protocol::modern::mayAskForInput(request.method)) {
    // Answering anything else this way would ask a client to retry a
    // request it has no reason to think is retriable.
    return makeVoidError(Error(
        jsonrpc::INTERNAL_ERROR,
        "'" + request.method + "' cannot be answered by asking for input"));
  }

  const std::string declared = protocol::modern::declaredCapabilitiesIn(
      session.getRequestMeta().has_value() ? session.getRequestMeta().value()
                                           : std::string());
  const auto missing =
      protocol::modern::capabilitiesMissingFor(needed.requests, declared);
  if (!missing.empty()) {
    std::string named;
    for (const auto& capability : missing) {
      if (!named.empty()) {
        named += ", ";
      }
      named += capability;
    }
    GOPHER_LOG_DEBUG(
        "refusing to ask a caller for something it did not declare it can "
        "do: {}",
        named);
    return stream->sendRefusal(
        static_cast<int>(http::HttpStatusCode::BadRequest),
        Error(protocol::modern::kMissingRequiredClientCapability,
              "This request needs a client that can " + named),
        protocol::modern::requiredCapabilitiesData(missing));
  }

  jsonrpc::Response response = jsonrpc::Response::success(
      request.id,
      jsonrpc::ResponseResult(protocol::modern::renderInputRequired(needed)));
  return stream->sendResponse(response);
}

VoidResult McpServer::askClient(
    const ResponseStreamPtr& stream,
    const jsonrpc::Request& request,
    std::function<void(const jsonrpc::Response&)> on_answer,
    std::chrono::milliseconds timeout) {
  assert((!main_dispatcher_ || main_dispatcher_->isThreadSafe()) &&
         "askClient off dispatcher");

  if (!stream) {
    return makeVoidError(Error(jsonrpc::INTERNAL_ERROR,
                               "cannot ask the client anything without a "
                               "stream to ask on"));
  }
  if (!on_answer) {
    return makeVoidError(
        Error(jsonrpc::INTERNAL_ERROR,
              "a question nobody is waiting for is not a question"));
  }

  const RequestIdKey key = requestIdKey(request.id);

  // Registered before the question goes out, not after: the answer may be
  // on its way back before this function returns, and an answer arriving
  // to nobody is dropped.
  client_requests_.expect(
      request.id, [this, key, on_answer](const jsonrpc::Response& response) {
        // Settled, so its deadline is no longer anybody's business. Safe to
        // drop here because this runs from the inbound dispatch, never from
        // inside the timer's own callback.
        client_request_deadlines_.erase(key);
        on_answer(response);
      });

  auto sent = stream->sendRequest(request);
  if (holds_alternative<Error>(sent)) {
    client_requests_.forget(request.id);
    return sent;
  }
  ++server_stats_.requests_to_clients;

  if (timeout.count() <= 0 || main_dispatcher_ == nullptr) {
    // No deadline asked for, or nowhere to hang one. Either way the
    // client now holds this request open for as long as it likes.
    return makeVoidSuccess();
  }

  auto timer = main_dispatcher_->createTimer([this, key, request, on_answer]() {
    // Whoever was waiting has to be told something, and told once. A
    // waiter that is no longer registered was already told by the answer
    // itself, and this has nothing left to do.
    if (!client_requests_.forget(request.id)) {
      return;
    }

    ++server_stats_.client_requests_timed_out;
    GOPHER_LOG_WARN("No answer from the client to {} in time",
                    requestIdKeyToString(key));
    on_answer(jsonrpc::Response::make_error(
        request.id,
        Error(jsonrpc::INTERNAL_ERROR, "the client did not answer in time")));

    // Not erased inline: this is running from inside that timer, and a
    // timer must outlive its own callback.
    main_dispatcher_->post(
        [this, key]() { client_request_deadlines_.erase(key); });
  });
  timer->enableTimer(timeout);
  client_request_deadlines_[key] = std::move(timer);
  return makeVoidSuccess();
}

// ApplicationBase overrides
void McpServer::initializeWorker(application::WorkerContext& worker) {
  // Initialize worker-specific resources
  // Each worker can handle requests independently

  worker.getDispatcher().post([this, &worker]() {
    // Worker is ready to handle requests
    // Can process requests from any session
  });
}

void McpServer::setupFilterChain(application::FilterChainBuilder& builder) {
  // Call base class to add standard filters (rate limiting, metrics)
  ApplicationBase::setupFilterChain(builder);

  // Add MCP-specific filters

  // Create JSON-RPC filter using the simplified helper
  // Servers typically use framing for all transports
  // TODO: Make framing configurable based on actual transport endpoints
  bool use_framing = true;
  // Use the dispatcher from the builder
  auto filter_bundle = createJsonRpcFilter(
      *protocol_callbacks_, builder.getDispatcher(), true, use_framing);

  // Add the filter instance
  builder.addFilterInstance(filter_bundle->filter);

  // Keep the bundle alive through a lambda capture
  builder.addFilter([filter_bundle]() -> network::FilterSharedPtr {
    // This lambda keeps filter_bundle alive for the connection lifetime
    return nullptr;
  });

  // Add metrics tracking filter for server statistics
  builder.addFilter([this]() -> network::FilterSharedPtr {
    // Use the base MetricsTrackingFilter for byte counting
    return std::make_shared<application::MetricsTrackingFilter>(
        server_stats_.bytes_received, server_stats_.bytes_sent);
  });

  // Add request validation filter
  if (config_.enable_request_validation) {
    builder.addFilter([this]() -> network::FilterSharedPtr {
      class RequestValidationFilter : public network::NetworkFilterBase {
       public:
        RequestValidationFilter(McpServer& server) : server_(server) {}

        network::FilterStatus onData(Buffer& data, bool end_stream) override {
          // Validate incoming requests
          // Check JSON-RPC format, required fields, etc.
          return network::FilterStatus::Continue;
        }

        network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
          return network::FilterStatus::Continue;
        }

        network::FilterStatus onNewConnection() override {
          return network::FilterStatus::Continue;
        }

       private:
        McpServer& server_;
      };

      return std::make_shared<RequestValidationFilter>(*this);
    });
  }
}

SessionManager::SessionPtr McpServer::getOrCreateSessionFor(
    const MessageDispatchContext& context) {
  // The context is constructed per message by the transport that parsed
  // it and dies when the dispatch returns, so a stale binding is
  // unrepresentable by construction — no consume-and-clear discipline
  // needed, unlike the ambient announce-then-dispatch scheme this
  // replaces.
  //
  // Transport session id wins: for HTTP+SSE each request arrives on a
  // one-shot POST connection, so keying the session on the connection
  // would hand every request a fresh session and silently drop state
  // such as resource subscriptions. The SSE stream id is the identity that
  // actually spans the client's requests — and it is also what the push
  // path needs to find the client's SSE stream.
  const std::string& transport_session_id = context.transportSessionId();
  if (!transport_session_id.empty()) {
    return session_manager_->getOrCreateSessionByTransportId(
        transport_session_id);
  }

  // No origin at all: a context-free legacy dispatch. A null-keyed
  // session can never be found again by getSessionByConnection, so
  // creating one per message would leak an unretrievable session per
  // message until max_sessions starves every transport. Keep exactly one
  // shared session for this path instead — which also preserves a legacy
  // client's initialize/subscription state across messages. Re-create it
  // only if the expiry sweep removed it.
  if (context.originConnection() == nullptr) {
    if (!legacy_session_ ||
        !session_manager_->getSession(legacy_session_->getId())) {
      legacy_session_ = session_manager_->createSession(nullptr);
    }
    return legacy_session_;
  }

  // Connection-keyed fallback for transports where the connection is
  // long-lived (stdio) or there is no transport session concept. The
  // origin comes from the message itself, so interleaved reads from
  // concurrent connections each land in their own session.
  auto session =
      session_manager_->getSessionByConnection(context.originConnection());
  if (!session) {
    session = session_manager_->createSession(context.originConnection());
  }
  return session;
}

// Reply path for messages that arrived through the context-free legacy
// hooks. The "no origin" half of the contract (null connection, empty
// transport session id) is inherited from NullMessageDispatchContext so
// it is defined in exactly one place; only the reply path differs: fall
// back to the first connected stdio manager — the historical degraded
// behavior — and fail loudly when there is none, instead of writing to
// an unrelated connection.
// ===== ClientRequestCorrelator =====

void ClientRequestCorrelator::expect(const RequestId& id, Waiter waiter) {
  if (!waiter) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  waiters_[requestIdKey(id)] = std::move(waiter);
}

bool ClientRequestCorrelator::deliver(const jsonrpc::Response& response) {
  Waiter waiter;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = waiters_.find(requestIdKey(response.id));
    if (it == waiters_.end()) {
      return false;
    }
    // Take the waiter out before running it: it is entitled to register
    // another request, and it must not be invoked twice for one answer.
    waiter = std::move(it->second);
    waiters_.erase(it);
  }
  waiter(response);
  return true;
}

bool ClientRequestCorrelator::forget(const RequestId& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return waiters_.erase(requestIdKey(id)) > 0;
}

size_t ClientRequestCorrelator::pending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return waiters_.size();
}

class McpServer::LegacyDispatchContext : public NullMessageDispatchContext {
 public:
  explicit LegacyDispatchContext(McpServer& server) : server_(server) {}

  VoidResult sendResponse(const jsonrpc::Response& response) override {
    for (auto& conn_manager : server_.connection_managers_) {
      if (conn_manager->isConnected()) {
        return conn_manager->sendResponse(response);
      }
    }
    Error err;
    err.code = jsonrpc::INTERNAL_ERROR;
    err.message = "no dispatch context and no connected transport";
    return makeVoidError(err);
  }

 private:
  McpServer& server_;
};

// McpProtocolCallbacks overrides
void McpServer::onRequest(const jsonrpc::Request& request) {
  // Context-free legacy entry: the producer did not say where the message
  // came from. Every in-tree transport dispatches through
  // onRequestWithContext; reaching this path means an external producer
  // has not been migrated, so route replies through the degraded legacy
  // fallback rather than guessing at a connection. Warn once per server —
  // a legacy producer hits this on every message, and repeating the same
  // warning per message is flooding, not signal.
  if (!legacy_dispatch_warned_) {
    legacy_dispatch_warned_ = true;
    GOPHER_LOG_WARN(
        "Request '{}' dispatched without origin context; session and reply "
        "routing degraded to the legacy transport fallback (warned once; "
        "further context-free dispatches log at debug)",
        request.method);
  } else {
    GOPHER_LOG_DEBUG("Context-free dispatch of request '{}'", request.method);
  }
  LegacyDispatchContext context(*this);
  onRequestWithContext(request, context);
}

void McpServer::onRequestWithContext(const jsonrpc::Request& request,
                                     MessageDispatchContext& context) {
  GOPHER_LOG_DEBUG("McpServer::onRequest called with method: {}",
                   request.method);
  GOPHER_LOG_FLOW_DEBUG(
      "MCP server dispatch request method={} transport_session={}",
      request.method,
      context.transportSessionId().empty() ? "<none>"
                                           : context.transportSessionId());

  // Handle request in dispatcher context - already in dispatcher
  server_stats_.requests_total++;

  // Track this request for potential cancellation
  auto pending_req = std::make_shared<PendingRequest>();
  pending_req->id = request.id;
  pending_req->start_time = std::chrono::steady_clock::now();

  {
    std::lock_guard<std::mutex> lock(pending_requests_mutex_);
    // Convert RequestId to string key
    std::string key = holds_alternative<std::string>(request.id)
                          ? get<std::string>(request.id)
                          : std::to_string(get<int64_t>(request.id));
    pending_requests_[key] = pending_req;
  }

  // Resolve the session for this request: transport session id first
  // (durable across HTTP+SSE POST connections), origin connection as
  // fallback.
  auto session = getOrCreateSessionFor(context);

  if (session) {
    pending_req->session_id = session->getId();
  }

  if (!session) {
    // Max sessions reached. Reply on the requester's own return path —
    // the context pins it to the connection the request arrived on.
    server_stats_.requests_failed++;
    auto response = jsonrpc::Response::make_error(
        request.id, Error(jsonrpc::INTERNAL_ERROR, "Max sessions reached"));

    auto send_result = context.sendResponse(response);
    if (holds_alternative<Error>(send_result)) {
      GOPHER_LOG_ERROR("Failed to send max-sessions error response: {}",
                       get<Error>(send_result).message);
    }
    return;
  }

  session->updateActivity();

  // What the request said about itself, for every method rather than for
  // tool calls alone: in the era with no introduction this is where a
  // caller declares what it can do, and a handler deciding whether it may
  // ask for anything has nowhere else to read it. Cleared when absent, so
  // one request's metadata never stands in for another's.
  if (request.params.has_value()) {
    const auto& params = request.params.value();
    auto meta = params.find("_meta");
    if (meta != params.end() && holds_alternative<std::string>(meta->second)) {
      session->setRequestMeta(
          mcp::make_optional(get<std::string>(meta->second)));
    } else {
      session->setRequestMeta(nullopt);
    }
  } else {
    session->setRequestMeta(nullopt);
  }

  // A handler registered as streaming gets somewhere to report progress on
  // its way to an answer. Attached to the session only for the length of
  // this dispatch: a stream belongs to the request that opened it, and one
  // left behind would collect the next request's output.
  ResponseStreamPtr stream;
  if (streamingFor(request) != StreamingMode::None) {
    stream = context.beginResponseStream();
    session->setResponseStream(stream);
  }

  // A handler that answers later takes the request off this thread's
  // hands entirely: nothing is sent on its behalf here, and the dispatch
  // ends with the client still waiting on a stream somebody else holds.
  AsyncRequestHandler async_handler;
  {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    auto it = async_request_handlers_.find(request.method);
    if (it != async_request_handlers_.end()) {
      async_handler = it->second;
    }
  }

  if (async_handler && !stream) {
    // Nothing here can hold an answer open. A transport that answers a
    // request with exactly one message has no handle that outlives this
    // dispatch, so a handler answering later would answer into nothing
    // and the client would wait for a reply that was never going to
    // come. Refused now, on the return path that is still good, rather
    // than accepted and abandoned.
    server_stats_.requests_failed++;
    GOPHER_LOG_ERROR(
        "'{}' is registered to answer later, and this transport cannot hold "
        "an answer open; refusing rather than leaving the caller waiting",
        request.method);
    auto refusal = jsonrpc::Response::make_error(
        request.id,
        Error(jsonrpc::INTERNAL_ERROR,
              "This method answers asynchronously, which this transport "
              "cannot carry"));
    auto sent = context.sendResponse(refusal);
    if (holds_alternative<Error>(sent)) {
      GOPHER_LOG_ERROR("Failed to refuse '{}': {}", request.method,
                       get<Error>(sent).message);
    }
    forgetPendingRequest(holds_alternative<std::string>(request.id)
                             ? get<std::string>(request.id)
                             : std::to_string(get<int64_t>(request.id)));
    return;
  }

  if (async_handler) {
    // The stream leaves the session for the same reason it was ever
    // attached: it belongs to this request, and this request is not over.
    session->setResponseStream(nullptr);

    const std::string pending_key =
        holds_alternative<std::string>(request.id)
            ? get<std::string>(request.id)
            : std::to_string(get<int64_t>(request.id));
    auto answer = std::make_shared<DeferredAnswer>(
        stream, [this, pending_key]() { forgetPendingRequest(pending_key); });

    try {
      async_handler(request, *session, answer);
      server_stats_.requests_success++;
    } catch (const std::exception& e) {
      server_stats_.requests_failed++;
      GOPHER_LOG_ERROR("Deferred handler for {} threw: {}", request.method,
                       e.what());
      answer->sendResponse(jsonrpc::Response::make_error(
          request.id, Error(jsonrpc::INTERNAL_ERROR, e.what())));
    }
    return;
  }

  // Route request to appropriate handler
  jsonrpc::Response response;

  {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    auto it = request_handlers_.find(request.method);
    if (it != request_handlers_.end()) {
      // Custom handler registered
      try {
        response = it->second(request, *session);
        server_stats_.requests_success++;
      } catch (const std::exception& e) {
        response = jsonrpc::Response::make_error(
            request.id, Error(jsonrpc::INTERNAL_ERROR, e.what()));
        server_stats_.requests_failed++;
      }
    } else {
      // Check built-in handlers
      if (request.method == "initialize") {
        response = handleInitialize(request, *session);
      } else if (request.method == "ping") {
        response = handlePing(request, *session);
      } else if (request.method == "resources/list") {
        response = handleListResources(request, *session);
      } else if (request.method == "resources/read") {
        response = handleReadResource(request, *session);
      } else if (request.method == "resources/subscribe") {
        response = handleSubscribe(request, *session);
      } else if (request.method == "resources/unsubscribe") {
        response = handleUnsubscribe(request, *session);
      } else if (request.method == "tools/list") {
        response = handleListTools(request, *session);
      } else if (request.method == "tools/call") {
        GOPHER_LOG_DEBUG("Calling handleCallTool for request");
        response = handleCallTool(request, *session);
        GOPHER_LOG_DEBUG("handleCallTool returned");
      } else if (request.method == "prompts/list") {
        response = handleListPrompts(request, *session);
      } else if (request.method == "prompts/get") {
        response = handleGetPrompt(request, *session);
      } else if (request.method == protocol::modern::kMethodServerDiscover) {
        response = handleDiscover(request, *session);
      } else {
        // Method not found
        response = jsonrpc::Response::make_error(
            request.id, Error(jsonrpc::METHOD_NOT_FOUND,
                              "Method not found: " + request.method));
        server_stats_.requests_invalid++;
      }
    }
  }

  // Send the response along the request's own return path. The context
  // pins the origin connection and its transport's framing, so concurrent
  // connections cannot cross-wire replies and a single dispatch can never
  // fan out to an unrelated transport. A failed send is surfaced instead
  // of silently dropped.
  GOPHER_LOG_DEBUG("Sending response for request id: {}",
                   holds_alternative<std::string>(request.id)
                       ? get<std::string>(request.id)
                       : std::to_string(get<int64_t>(request.id)));

  session->setResponseStream(nullptr);

  // Down the same stream the progress went, when there was one: the
  // response is the last thing on it, and a second path out would leave
  // the stream open with the client still waiting.
  auto send_result =
      stream ? stream->sendResponse(response) : context.sendResponse(response);
  if (holds_alternative<Error>(send_result)) {
    server_stats_.errors_total++;
    GOPHER_LOG_ERROR("Failed to send response for request id {}: {}",
                     holds_alternative<std::string>(request.id)
                         ? get<std::string>(request.id)
                         : std::to_string(get<int64_t>(request.id)),
                     get<Error>(send_result).message);
  }

  // Remove request from pending list
  {
    std::lock_guard<std::mutex> lock(pending_requests_mutex_);
    // Convert RequestId to string key
    std::string key = holds_alternative<std::string>(request.id)
                          ? get<std::string>(request.id)
                          : std::to_string(get<int64_t>(request.id));
    pending_requests_.erase(key);
  }
}

void McpServer::onNotification(const jsonrpc::Notification& notification) {
  // Context-free legacy entry; see onRequest for why this is degraded and
  // why the warning fires only once per server.
  if (!legacy_dispatch_warned_) {
    legacy_dispatch_warned_ = true;
    GOPHER_LOG_WARN(
        "Notification '{}' dispatched without origin context; session "
        "resolution degraded to the legacy transport fallback (warned once; "
        "further context-free dispatches log at debug)",
        notification.method);
  } else {
    GOPHER_LOG_DEBUG("Context-free dispatch of notification '{}'",
                     notification.method);
  }
  LegacyDispatchContext context(*this);
  onNotificationWithContext(notification, context);
}

void McpServer::onNotificationWithContext(
    const jsonrpc::Notification& notification,
    MessageDispatchContext& context) {
  GOPHER_LOG_FLOW_DEBUG(
      "MCP server dispatch notification method={} transport_session={}",
      notification.method,
      context.transportSessionId().empty() ? "<none>"
                                           : context.transportSessionId());

  // Handle notification in dispatcher context
  server_stats_.notifications_total++;

  // Resolve the session the same way requests do, so a notification sent
  // on a fresh POST connection still lands in the subscriber's session.
  auto session = getOrCreateSessionFor(context);
  if (!session) {
    return;  // Can't process notification without session
  }

  session->updateActivity();

  // Route notification to appropriate handler
  {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    auto it = notification_handlers_.find(notification.method);
    if (it != notification_handlers_.end()) {
      try {
        it->second(notification, *session);
      } catch (const std::exception& e) {
        // Log error but don't send response for notifications
        server_stats_.errors_total++;
      }
    }
  }

  // Handle built-in notifications
  if (notification.method == "initialized") {
    // Client has completed initialization
    // Mark session as initialized
  } else if (notification.method == "notifications/cancelled") {
    // Client cancelled a request
    // Extract the request ID that was cancelled
    if (notification.params.has_value()) {
      auto params = notification.params.value();
      auto req_id_it = params.find("requestId");
      if (req_id_it != params.end()) {
        // Mark the request as cancelled
        // The value in params is a MetadataValue which could be string or
        // int64_t
        std::string key_to_cancel;
        if (holds_alternative<std::string>(req_id_it->second)) {
          key_to_cancel = get<std::string>(req_id_it->second);
        } else if (holds_alternative<int64_t>(req_id_it->second)) {
          key_to_cancel = std::to_string(get<int64_t>(req_id_it->second));
        } else {
          // Not a valid request ID type
          return;
        }

        // Find and mark the request as cancelled
        {
          std::lock_guard<std::mutex> lock(pending_requests_mutex_);
          auto it = pending_requests_.find(key_to_cancel);
          if (it != pending_requests_.end()) {
            it->second->cancelled = true;
            GOPHER_LOG_INFO("Request marked as cancelled: {}", key_to_cancel);
          }
        }
      }
    }
  }
}

void McpServer::onResponse(const jsonrpc::Response& response) {
  // A response arriving here is the answer to something this server asked a
  // client. Hand it to whoever asked; if nobody did, say so rather than
  // letting it disappear — an unmatched answer means either a confused peer
  // or a waiter released too early, and both are worth seeing.
  if (client_requests_.deliver(response)) {
    return;
  }

  server_stats_.responses_unmatched++;
  GOPHER_LOG_WARN("Dropping a client response nothing was waiting for: id={}",
                  requestIdKeyToString(requestIdKey(response.id)));
}

void McpServer::onConnectionEvent(network::ConnectionEvent event) {
  // Stats-only path. Per-connection cleanup lives in
  // onConnectionLifecycleEvent, invoked by the ConnectionLifecycleCallbacks
  // adapter which carries the closing connection's identity. This function
  // still runs for stdio transports (reached via ServerProtocolCallbacks)
  // where there is no active_connections_ entry to unwind.
  switch (event) {
    case network::ConnectionEvent::Connected:
    case network::ConnectionEvent::ConnectedZeroRtt:
      server_stats_.connections_total++;
      server_stats_.connections_active++;
      break;
    case network::ConnectionEvent::RemoteClose:
    case network::ConnectionEvent::LocalClose:
      server_stats_.connections_active--;
      break;
  }
}

void McpServer::onConnectionLifecycleEvent(network::Connection* connection,
                                           network::ConnectionEvent event) {
  // Connection events fire from the connection's own callback loop, which is
  // the dispatcher thread. Enforcing this here catches any accidental
  // off-thread callers introduced later.
  assert(main_dispatcher_ && main_dispatcher_->isThreadSafe() &&
         "onConnectionLifecycleEvent off dispatcher");
  // Runs in dispatcher thread via the per-connection adapter.
  //
  // Only close events reach us here in practice. ConnectionImpl does not
  // raise Connected for server-accepted sockets (it is raised only from
  // the client-side connect-completion path), so a Connected case would
  // be dead code. The matching connections_total/connections_active
  // increment lives in onNewConnection, which is the server-side
  // equivalent checkpoint.
  switch (event) {
    case network::ConnectionEvent::Connected:
    case network::ConnectionEvent::ConnectedZeroRtt:
      return;
    case network::ConnectionEvent::RemoteClose:
    case network::ConnectionEvent::LocalClose:
      break;
  }

  server_stats_.connections_active--;

  // If the closing connection carried an SSE stream, drop its registry
  // entry NOW — the entry holds a raw Connection* and this is the last
  // moment it is guaranteed valid (the filter destructor, the other
  // removal path, can run long after because the factory keeps filters
  // alive). A stale entry would make the next push a write into freed
  // memory. Removal fires the registry's session-closed callback, which
  // releases the MCP session keyed on the stream and its subscriptions.
  if (http_sse_factory_) {
    http_sse_factory_->sseRegistry().removeConnection(connection);
  }

  // Tear down session state keyed by the actual closing connection.
  // Transport-keyed sessions (HTTP+SSE) are untouched here by design: they
  // are created with a null connection, live across many short POST
  // connections, and are released by the SSE registry's session-closed
  // callback when their stream ends.
  if (session_manager_) {
    auto session = session_manager_->removeSessionByConnection(connection);
    if (session && resource_manager_) {
      resource_manager_->releaseSession(*session);
    }
  }
  {
    std::lock_guard<std::mutex> lock(connection_sessions_mutex_);
    connection_sessions_.erase(connection);
  }

  // Hand both the ConnectionPtr and the lifecycle adapter to the dispatcher's
  // deferred-delete queue. We are currently inside the adapter's onEvent(),
  // and the connection is iterating its own callback list — destroying either
  // synchronously would be a use-after-free. deferredDelete drains after the
  // current callback stack unwinds.
  //
  // Skip container unwinding if we're past shutdown: during ~McpServer the
  // containers themselves are being destroyed, and mutating them from a
  // close-triggered callback would be UB. Shutdown already sets this flag
  // before the destructor starts tearing members down.
  if (server_running_) {
    for (auto it = active_connections_.begin(); it != active_connections_.end();
         ++it) {
      if (it->get() == connection) {
        main_dispatcher_->deferredDelete(std::move(*it));
        active_connections_.erase(it);
        break;
      }
    }
    auto cb_it = lifecycle_callbacks_.find(connection);
    if (cb_it != lifecycle_callbacks_.end()) {
      main_dispatcher_->deferredDelete(std::move(cb_it->second));
      lifecycle_callbacks_.erase(cb_it);
    }
  }
}

void McpServer::onError(const Error& error) {
  server_stats_.errors_total++;

  // Log error with context
  trackFailure(application::FailureReason(
      application::FailureReason::Type::ProtocolError, error.message));
}

// Built-in request handlers
void McpServer::registerBuiltinHandlers() {
  // Most built-ins are answered directly in onRequestWithContext. This
  // one cannot be: its answer is a stream that stays open, and the
  // response its request is waiting for arrives only when the
  // subscription ends. So it is registered as a handler that answers
  // later and, ordinarily, never.
  registerAsyncRequestHandler(
      protocol::modern::kMethodSubscriptionsListen,
      [this](const jsonrpc::Request& request, SessionContext& session,
             const ResponseStreamPtr& stream) {
        if (!stream) {
          return;
        }

        // The filter arrives through the flat map its params are held
        // in, so nested JSON comes back stringified; this rebuilds it.
        json::JsonValue params = json::JsonValue::object();
        if (request.params.has_value()) {
          params = json::metadataToJson(request.params.value());
        }

        const auto filter = NotificationFilter::parse(params);
        // Held under who asked as well as what they called it: the id a
        // subscription answers to is one its own client chose, and two
        // clients each numbering their requests from one is ordinary.
        if (!subscriptions_.open(session.getId(), request.id, stream, filter)) {
          stream->sendResponse(jsonrpc::Response::make_error(
              request.id, Error(jsonrpc::INVALID_REQUEST,
                                "this subscription could not be opened")));
        }
      },
      // A subscription is a stream. A client that cannot read one cannot
      // have a subscription, and is better told so than given a request
      // that never answers.
      StreamingMode::Required);
}

jsonrpc::Response McpServer::handleInitialize(const jsonrpc::Request& request,
                                              SessionContext& session) {
  // Parse initialize request
  // TODO: Deserialize InitializeRequest from request.params
  // TODO: Extract client info and store in session via session.setClientInfo()

  // Read the revision the client asked for. Anything missing or of the
  // wrong type leaves this empty, which negotiates to our newest.
  std::string requested_version;
  if (request.params.has_value()) {
    const auto& params = request.params.value();
    auto version_it = params.find("protocolVersion");
    if (version_it != params.end() &&
        holds_alternative<std::string>(version_it->second)) {
      requested_version = get<std::string>(version_it->second);
    }
  }

  // Negotiate against the revisions we can serve. An empty list means the
  // single configured version is all we speak.
  //
  // Never a modern one, whatever this server also serves: that era has no
  // handshake, so a client that introduced itself cannot speak it, and
  // answering an introduction that named no version with the newest thing
  // on the list would hand such a client a revision with no way to use
  // it.
  std::vector<std::string> supported = transport::handshakeProtocolVersions(
      transport::servedProtocolVersions(config_.streamable_http));
  if (supported.empty()) {
    supported.push_back(config_.protocol_version);
  }

  const std::string negotiated =
      protocol::negotiateProtocolVersion(requested_version, supported);

  // Remember what was agreed. Later decisions on this session turn on the
  // revision actually negotiated, not on the configured default or on
  // re-deriving it from a header.
  session.setProtocolVersion(negotiated);

  // Build initialize result as proper nested JSON structure
  // MCP protocol requires nested objects, not flattened dot notation
  json::JsonValue result_json;
  result_json["protocolVersion"] = negotiated;

  // Add serverInfo as nested object
  json::JsonValue server_info;
  server_info["name"] = config_.server_name;
  server_info["version"] = config_.server_version;
  result_json["serverInfo"] = std::move(server_info);

  // Add capabilities as nested object with empty objects for enabled caps
  json::JsonValue capabilities = json::JsonValue::object();
  if (config_.capabilities.resources.has_value()) {
    if (holds_alternative<bool>(config_.capabilities.resources.value())) {
      if (get<bool>(config_.capabilities.resources.value())) {
        capabilities["resources"] = json::JsonValue::object();
      }
    } else {
      json::JsonValue resources_cap = json::JsonValue::object();
      resources_cap["subscribe"] = true;
      resources_cap["listChanged"] = true;
      capabilities["resources"] = std::move(resources_cap);
    }
  }
  if (config_.capabilities.tools.has_value() &&
      config_.capabilities.tools.value()) {
    capabilities["tools"] = json::JsonValue::object();
  }
  if (config_.capabilities.prompts.has_value() &&
      config_.capabilities.prompts.value()) {
    capabilities["prompts"] = json::JsonValue::object();
  }
  if (config_.capabilities.logging.has_value() &&
      config_.capabilities.logging.value()) {
    capabilities["logging"] = json::JsonValue::object();
  }
  result_json["capabilities"] = std::move(capabilities);

  // Add instructions if present
  if (!config_.instructions.empty()) {
    result_json["instructions"] = config_.instructions;
  }

  // Return response with JSON result directly
  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(result_json));
}

jsonrpc::Response McpServer::handleDiscover(const jsonrpc::Request& request,
                                            SessionContext& session) {
  (void)session;

  // What a client would otherwise have learned from an introduction. In
  // the era that has none this is the only way to ask, which is why it is
  // served whichever era asked: a method being new does not make it
  // modern-only, and a classic client is entitled to the same answer.
  json::JsonValue result = json::JsonValue::object();

  json::JsonValue versions = json::JsonValue::array();
  for (const auto& version :
       transport::servedProtocolVersions(config_.streamable_http)) {
    versions.push_back(json::JsonValue(version));
  }
  if (versions.size() == 0) {
    versions.push_back(json::JsonValue(config_.protocol_version));
  }
  result.set("supportedVersions", versions);

  result.set("capabilities", json::to_json(config_.capabilities));

  if (!config_.instructions.empty()) {
    result.set("instructions", json::JsonValue(config_.instructions));
  }

  // Named under the metadata key rather than as a field of its own: with
  // no handshake, this is where a server says what it is.
  json::JsonValue server_info = json::JsonValue::object();
  server_info.set("name", json::JsonValue(config_.server_name));
  server_info.set("version", json::JsonValue(config_.server_version));
  json::JsonValue meta = json::JsonValue::object();
  meta.set(protocol::modern::kMetaServerInfo, server_info);
  result.set("_meta", meta);

  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(result));
}

jsonrpc::Response McpServer::handlePing(const jsonrpc::Request& request,
                                        SessionContext& session) {
  // Simple ping response
  return jsonrpc::Response::success(
      request.id,
      jsonrpc::ResponseResult(make<Metadata>().add("pong", true).build()));
}

jsonrpc::Response McpServer::handleListResources(
    const jsonrpc::Request& request, SessionContext& session) {
  // Extract cursor if provided
  optional<Cursor> cursor;
  if (request.params.has_value()) {
    auto params = request.params.value();
    auto cursor_it = params.find("cursor");
    if (cursor_it != params.end() &&
        holds_alternative<std::string>(cursor_it->second)) {
      cursor = mcp::make_optional(get<std::string>(cursor_it->second));
    }
  }

  // Get resources from resource manager and return directly
  // ResponseResult variant supports ListResourcesResult
  auto result = resource_manager_->listResources(cursor);
  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(result));
}

jsonrpc::Response McpServer::handleReadResource(const jsonrpc::Request& request,
                                                SessionContext& session) {
  // Extract URI
  if (!request.params.has_value()) {
    return jsonrpc::Response::make_error(
        request.id, Error(jsonrpc::INVALID_PARAMS, "Missing uri parameter"));
  }

  auto params = request.params.value();
  auto uri_it = params.find("uri");
  if (uri_it == params.end() ||
      !holds_alternative<std::string>(uri_it->second)) {
    return jsonrpc::Response::make_error(
        request.id, Error(jsonrpc::INVALID_PARAMS, "Invalid uri parameter"));
  }

  std::string uri = get<std::string>(uri_it->second);

  // Delegate to the registered read handler via ResourceManager.
  // The handler produces a ReadResourceResult with properly populated contents.
  try {
    auto result = resource_manager_->readResource(uri, session);
    auto result_json = json::to_json(result);
    return jsonrpc::Response::success(request.id,
                                      jsonrpc::ResponseResult(result_json));
  } catch (const std::exception& e) {
    return jsonrpc::Response::make_error(
        request.id, Error(jsonrpc::INVALID_PARAMS,
                          std::string("Resource read failed: ") + e.what()));
  }
}

jsonrpc::Response McpServer::handleSubscribe(const jsonrpc::Request& request,
                                             SessionContext& session) {
  // Extract URI
  if (!request.params.has_value()) {
    return jsonrpc::Response::make_error(
        request.id, Error(jsonrpc::INVALID_PARAMS, "Missing uri parameter"));
  }

  auto params = request.params.value();
  auto uri_it = params.find("uri");
  if (uri_it == params.end() ||
      !holds_alternative<std::string>(uri_it->second)) {
    return jsonrpc::Response::make_error(
        request.id, Error(jsonrpc::INVALID_PARAMS, "Invalid uri parameter"));
  }

  std::string uri = get<std::string>(uri_it->second);

  // Subscribe to resource
  resource_manager_->subscribe(uri, session);

  // Nothing to say, which on the wire is an empty object. A null result
  // is not an empty one: a peer validating against the schema cannot read
  // it as a response at all.
  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(Metadata()));
}

jsonrpc::Response McpServer::handleUnsubscribe(const jsonrpc::Request& request,
                                               SessionContext& session) {
  // Extract URI
  if (!request.params.has_value()) {
    return jsonrpc::Response::make_error(
        request.id, Error(jsonrpc::INVALID_PARAMS, "Missing uri parameter"));
  }

  auto params = request.params.value();
  auto uri_it = params.find("uri");
  if (uri_it == params.end() ||
      !holds_alternative<std::string>(uri_it->second)) {
    return jsonrpc::Response::make_error(
        request.id, Error(jsonrpc::INVALID_PARAMS, "Invalid uri parameter"));
  }

  std::string uri = get<std::string>(uri_it->second);

  // Unsubscribe from resource
  resource_manager_->unsubscribe(uri, session);

  // Empty, and an object rather than a null — see handleSubscribe.
  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(Metadata()));
}

jsonrpc::Response McpServer::handleListTools(const jsonrpc::Request& request,
                                             SessionContext& session) {
  // Get tools from tool registry
  auto result = tool_registry_->listTools();

  // Build response as JsonValue with "tools" key per MCP spec
  json::JsonValue tools_array = json::JsonValue::array();
  for (const auto& tool : result.tools) {
    tools_array.push_back(json::to_json(tool));
  }

  json::JsonValue response_obj = json::JsonValue::object();
  response_obj["tools"] = std::move(tools_array);

  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(response_obj));
}

jsonrpc::Response McpServer::handleCallTool(const jsonrpc::Request& request,
                                            SessionContext& session) {
  GOPHER_LOG_DEBUG("handleCallTool entered");
  // Extract tool name and arguments
  if (!request.params.has_value()) {
    return jsonrpc::Response::make_error(
        request.id, Error(jsonrpc::INVALID_PARAMS, "Missing parameters"));
  }

  auto params = request.params.value();
  auto name_it = params.find("name");
  if (name_it == params.end() ||
      !holds_alternative<std::string>(name_it->second)) {
    return jsonrpc::Response::make_error(
        request.id, Error(jsonrpc::INVALID_PARAMS, "Invalid name parameter"));
  }

  std::string name = get<std::string>(name_it->second);

  // Extract optional arguments
  // The MCP protocol expects arguments to be nested under "arguments" field
  // Since MetadataValue doesn't support nested maps, we need to handle this
  // specially
  optional<Metadata> arguments;
  auto args_it = params.find("arguments");

  if (args_it != params.end()) {
    // The arguments field contains a JSON string representation of the nested
    // object We need to parse it back to extract the actual arguments
    if (holds_alternative<std::string>(args_it->second)) {
      // The nested object was stringified during deserialization
      // Parse it back to get the actual arguments
      std::string args_json = get<std::string>(args_it->second);
      try {
        auto args_value = json::JsonValue::parse(args_json);
        arguments = mcp::make_optional(json::jsonToMetadata(args_value));
      } catch (const json::JsonException& e) {
        // If parsing fails, treat it as empty arguments
        arguments = mcp::make_optional(Metadata());
      }
    } else {
      // Fallback: if arguments is not a string, create empty metadata
      arguments = mcp::make_optional(Metadata());
    }
  }

  // Surface the request's params._meta (out-of-band metadata, e.g. correlation
  // ids) to the tool handler via the session it already receives. Carried as
  // its stringified-JSON form, like nested arguments above. Cleared when absent
  // so a prior request's _meta never aliases this one.
  auto meta_it = params.find("_meta");
  if (meta_it != params.end() &&
      holds_alternative<std::string>(meta_it->second)) {
    session.setRequestMeta(
        mcp::make_optional(get<std::string>(meta_it->second)));
  } else {
    session.setRequestMeta(nullopt);
  }

  // Call tool
  GOPHER_LOG_DEBUG("Calling tool_registry_->callTool for: {}", name);
  auto result = tool_registry_->callTool(name, arguments, session);
  GOPHER_LOG_DEBUG("tool_registry_->callTool returned for: {}", name);

  // Serialize CallToolResult to proper MCP format
  // The result must have content as an array of content blocks per MCP spec
  auto result_json = json::to_json(result);

  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(result_json));
}

jsonrpc::Response McpServer::handleListPrompts(const jsonrpc::Request& request,
                                               SessionContext& session) {
  // Extract cursor if provided
  optional<Cursor> cursor;
  if (request.params.has_value()) {
    auto params = request.params.value();
    auto cursor_it = params.find("cursor");
    if (cursor_it != params.end() &&
        holds_alternative<std::string>(cursor_it->second)) {
      cursor = mcp::make_optional(get<std::string>(cursor_it->second));
    }
  }

  // Get prompts from prompt registry
  auto result = prompt_registry_->listPrompts(cursor);

  // Build response as JsonValue with "prompts" key per MCP spec
  json::JsonValue prompts_array = json::JsonValue::array();
  for (const auto& prompt : result.prompts) {
    prompts_array.push_back(json::to_json(prompt));
  }

  json::JsonValue response_obj = json::JsonValue::object();
  response_obj["prompts"] = std::move(prompts_array);

  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(response_obj));
}

jsonrpc::Response McpServer::handleGetPrompt(const jsonrpc::Request& request,
                                             SessionContext& session) {
  // Extract prompt name and arguments
  if (!request.params.has_value()) {
    return jsonrpc::Response::make_error(
        request.id, Error(jsonrpc::INVALID_PARAMS, "Missing parameters"));
  }

  auto params = request.params.value();
  auto name_it = params.find("name");
  if (name_it == params.end() ||
      !holds_alternative<std::string>(name_it->second)) {
    return jsonrpc::Response::make_error(
        request.id, Error(jsonrpc::INVALID_PARAMS, "Invalid name parameter"));
  }

  std::string name = get<std::string>(name_it->second);

  // Extract optional arguments. Nested objects do not fit in Metadata, so
  // they arrive as the serialized form and are parsed back — the same way
  // a tool call's arguments are, and for the same reason.
  optional<Metadata> arguments;
  auto args_it = params.find("arguments");
  if (args_it != params.end()) {
    arguments = mcp::make_optional(Metadata());
    if (holds_alternative<std::string>(args_it->second)) {
      try {
        auto parsed = json::JsonValue::parse(get<std::string>(args_it->second));
        arguments = mcp::make_optional(json::jsonToMetadata(parsed));
      } catch (const json::JsonException&) {
        // Unparseable arguments are no arguments. A prompt that needs one
        // says so itself; refusing here would refuse a prompt that does
        // not care.
      }
    }
  }

  // Get prompt
  auto result = prompt_registry_->getPrompt(name, arguments, session);

  // Serialize GetPromptResult to JSON and convert to Metadata for response
  // The result contains description and messages from the prompt handler
  auto result_json = json::to_json(result);
  auto result_metadata = json::jsonToMetadata(result_json);

  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(result_metadata));
}

// Background task management using dispatcher timers.
// Timers are owned by McpServer so they outlive this function; on each fire
// the callback re-arms the same timer to implement a periodic task without
// rebuilding timer objects.
void McpServer::startBackgroundTasks() {
  background_threads_running_ = true;

  const auto cleanup_interval = std::chrono::seconds(30);

  session_cleanup_timer_ =
      main_dispatcher_->createTimer([this, cleanup_interval]() {
        if (!background_threads_running_) {
          return;
        }
        session_manager_->cleanupExpiredSessions();
        // Re-arm for the next tick. The timer is a member, so firing is safe.
        session_cleanup_timer_->enableTimer(cleanup_interval);
      });
  session_cleanup_timer_->enableTimer(cleanup_interval);

  const auto update_interval = config_.resource_update_debounce;

  resource_update_timer_ =
      main_dispatcher_->createTimer([this, update_interval]() {
        if (!background_threads_running_) {
          return;
        }
        // Resource-update dispatch hooks in here when enabled.
        resource_update_timer_->enableTimer(update_interval);
      });
  resource_update_timer_->enableTimer(update_interval);
}

void McpServer::stopBackgroundTasks() {
  background_threads_running_ = false;
  // Disable immediately so a pending fire doesn't run after shutdown starts.
  // The guard above also prevents re-arming from inside the callback.
  if (session_cleanup_timer_) {
    session_cleanup_timer_->disableTimer();
  }
  if (resource_update_timer_) {
    resource_update_timer_->disableTimer();
  }
}

// ListenerCallbacks implementation (production pattern)
// These callbacks follow production connection acceptance flow:
// 1. Listener accepts raw socket
// 2. onAccept() called for socket-level processing
// 3. Connection created with transport and filters
// 4. onNewConnection() called with fully initialized connection

void McpServer::onAccept(network::ConnectionSocketPtr&& socket) {
  // CALLBACK FLOW: TcpListenerImpl → TcpActiveListener → McpServer
  // This is called when a raw socket is accepted but before connection is
  // created Following production pattern: we can reject connections early or
  // apply socket options

  // In the future, we could:
  // - Apply per-connection socket options
  // - Perform early rejection based on IP/port
  // - Select appropriate filter chain based on SNI

  // For now, we don't need socket-level processing
  // The TcpActiveListener will create the connection with appropriate transport
}

void McpServer::onNewConnection(network::ConnectionPtr&& connection) {
  // CALLBACK FLOW: TcpActiveListener → McpServer
  // This is called when a connection is fully established with transport and
  // filters Following production pattern: connection is ready for protocol
  // processing

  // CRITICAL FIX: Take ownership of the connection
  // The connection is passed by rvalue reference, meaning we must take
  // ownership or it will be destroyed when this function returns, closing the
  // connection!
  network::Connection* conn_ptr = connection.get();

  // Create session for this connection
  auto session = session_manager_->createSession(conn_ptr);

  if (!session) {
    // Max sessions reached - close connection
    connection->close(network::ConnectionCloseType::NoFlush);
    return;
  }

  // Register a per-connection lifecycle adapter so the close event tells us
  // exactly which connection is dying. The adapter is held alive by the
  // lifecycle_callbacks_ map until the close path hands it to the dispatcher
  // for deferred deletion.
  auto lifecycle_cb =
      std::make_unique<ConnectionLifecycleCallbacks>(*this, conn_ptr);
  connection->addConnectionCallbacks(*lifecycle_cb);
  lifecycle_callbacks_[conn_ptr] = std::move(lifecycle_cb);

  // Store connection-to-session mapping
  // Following production pattern: listener owns connections
  {
    std::lock_guard<std::mutex> lock(connection_sessions_mutex_);
    connection_sessions_[conn_ptr] = session;
  }

  // Update connection count.
  //
  // We bump the public stats here rather than wait for a Connected event
  // via the per-connection lifecycle adapter, because ConnectionImpl
  // never raises Connected for server-accepted sockets -- the event is
  // only raised on the client-side connect-completion path. onNewConnection
  // is the equivalent checkpoint for server: the socket is already
  // accepted and the filter chain is built by the time we run here, and
  // this call is on the dispatcher thread, so the atomic increments land
  // on the same thread that the decrement in onConnectionLifecycleEvent
  // runs on.
  server_stats_.connections_total++;
  server_stats_.connections_active++;

  // Arm the per-connection idle-read timer if the operator configured one.
  // The setter treats zero as "disabled", so gating here only documents
  // intent and avoids touching the timer state for the default build.
  if (config_.idle_read_timeout.count() > 0) {
    conn_ptr->setIdleReadTimeout(config_.idle_read_timeout);
  }

  // Store the connection to keep it alive
  // Following production pattern: server owns connections in dispatcher thread
  // No mutex needed - all operations happen in dispatcher thread
  // Connection will be removed from list when it closes via callbacks
  active_connections_.push_back(std::move(connection));

  // The connection is now ready for message processing
  // Messages will flow through filter chain to our onRequest/onNotification
  // handlers

  GOPHER_LOG_DEBUG("New connection established, session: {}", session->getId());
}

// Listener configuration-based server startup methods

VoidResult McpServer::createListenersFromConfig(
    const std::vector<mcp::config::ListenerConfig>& listeners) {
  // Initialize the application if not already done
  if (!initialized_) {
    initialize();  // Create dispatchers and workers (including main dispatcher)

    // Verify main dispatcher was created
    if (!main_dispatcher_) {
      GOPHER_LOG_ERROR("Failed to create main dispatcher");
      return makeVoidError(
          Error(jsonrpc::INTERNAL_ERROR, "Dispatcher initialization failed"));
    }
  }

  // Validate listener configurations
  for (const auto& listener_config : listeners) {
    try {
      listener_config.validate();
    } catch (const std::exception& e) {
      GOPHER_LOG_ERROR("Invalid listener config for '{}': {}",
                       listener_config.name, e.what());
      return makeVoidError(
          Error(jsonrpc::INVALID_PARAMS,
                "Invalid listener configuration: " + std::string(e.what())));
    }
  }

  // Store listeners for deferred creation in run()
  pending_listener_configs_ = listeners;

  GOPHER_LOG_INFO("Configured {} listener(s) for startup", listeners.size());
  return makeVoidSuccess();
}

void McpServer::startListener(
    const mcp::config::ListenerConfig& listener_config) {
  try {
    GOPHER_LOG_INFO("Starting listener '{}' on {}:{}", listener_config.name,
                    listener_config.address.socket_address.address,
                    listener_config.address.socket_address.port_value);

    // Create TcpActiveListener with the new constructor
    auto tcp_listener = std::make_unique<network::TcpActiveListener>(
        *main_dispatcher_, listener_config,
        *this  // We implement ListenerCallbacks
    );

    if (protocol_callbacks_) {
      tcp_listener->setProtocolCallbacks(*protocol_callbacks_);
    }

    // Enable the listener to start accepting connections
    tcp_listener->enable();

    // Store the listener
    tcp_listeners_.push_back(std::move(tcp_listener));

    GOPHER_LOG_DEBUG("Successfully started listener '{}'",
                     listener_config.name);

  } catch (const std::exception& e) {
    GOPHER_LOG_ERROR("Failed to start listener '{}': {}", listener_config.name,
                     e.what());
    // Continue with other listeners rather than failing entirely
  }
}

}  // namespace server
}  // namespace mcp
