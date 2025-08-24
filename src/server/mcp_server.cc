/**
 * @file mcp_server.cc
 * @brief Implementation of enterprise-grade MCP server using MCP abstraction
 * layer
 */

#include "mcp/server/mcp_server.h"

#include <algorithm>
#include <future>
#include <iostream>
#include <sstream>

#include "mcp/filter/http_sse_filter_chain_factory.h"
#include "mcp/filter/json_rpc_filter_factory.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/transport/http_sse_transport_socket.h"
// NOTE: We'll implement connection handler directly in server for now
// to avoid conflicts with existing connection management in
// connection_manager.h
#include "mcp/network/server_listener_impl.h"

namespace mcp {
namespace server {

// Constructor
McpServer::McpServer(const McpServerConfig& config)
    : ApplicationBase(config), config_(config), server_stats_() {
  // Initialize session manager for client connection tracking
  session_manager_ = std::make_unique<SessionManager>(config_, server_stats_);

  // Initialize resource manager for resource handling
  resource_manager_ = std::make_unique<ResourceManager>(server_stats_);

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

  // Initialize the application if not already done
  if (!initialized_) {
    initialize();  // Create dispatchers and workers (including main dispatcher)

    // Verify main dispatcher was created
    if (!main_dispatcher_) {
      std::cerr << "[ERROR] Failed to create main dispatcher" << std::endl;
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
      conn_config.stdio_config = transport::StdioTransportSocketConfig();

      auto conn_manager = std::make_unique<McpConnectionManager>(
          *main_dispatcher_, *socket_interface_, conn_config);

      conn_manager->setProtocolCallbacks(*protocol_callbacks_);

      auto result = conn_manager->connect();
      if (holds_alternative<std::nullptr_t>(result)) {
        connection_managers_.push_back(std::move(conn_manager));
        std::cerr << "[DEBUG] Successfully started stdio transport"
                  << std::endl;
      } else {
        auto error = get<Error>(result);
        std::cerr << "[ERROR] Failed to setup stdio transport: "
                  << error.message << std::endl;
        main_dispatcher_->exit();
        return;
      }
    } else if (transport_type == TransportType::HttpSse) {
      // IMPROVEMENT: Use TcpListenerImpl and listener management for HTTP+SSE
      // This provides better connection lifecycle management and error handling

      // Parse URL to extract listening port
      uint32_t port = 8080;  // Default HTTP port
      if (address.find("http://") == 0 || address.find("https://") == 0) {
        std::string url = address;
        size_t protocol_end = url.find("://") + 3;
        size_t port_start = url.find(':', protocol_end);

        if (port_start != std::string::npos) {
          size_t port_end = url.find('/', port_start + 1);
          std::string port_str =
              (port_end != std::string::npos)
                  ? url.substr(port_start + 1, port_end - port_start - 1)
                  : url.substr(port_start + 1);
          port = std::stoi(port_str);
        }
      }

      // Create TCP address for listening
      auto tcp_address =
          network::Address::anyAddress(network::Address::IpVersion::v4, port);

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
      tcp_config.filter_chain_factory =
          std::make_shared<filter::HttpSseFilterChainFactory>(
              *main_dispatcher_, *protocol_callbacks_);

      tcp_config.backlog = 128;
      tcp_config.per_connection_buffer_limit = config_.buffer_high_watermark;
      tcp_config.max_connections_per_event =
          10;  // Process up to 10 accepts per event

      // Create TcpActiveListener with this server as callbacks
      // Following production pattern: listener owns socket, manages accepts,
      // creates connections
      auto tcp_listener = std::make_unique<network::TcpActiveListener>(
          *main_dispatcher_,
          std::move(tcp_config),  // Move config to avoid copying unique_ptrs
          *this                   // We implement ListenerCallbacks
      );

      // Enable the listener to start accepting connections
      tcp_listener->enable();

      // Store the listener
      tcp_listeners_.push_back(std::move(tcp_listener));

      std::cerr << "[DEBUG] Successfully started HTTP+SSE listener on port "
                << port << std::endl;
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
    std::cerr << "[INFO] Server started successfully!" << std::endl;
    std::cerr << "[INFO] Listening on " << address << std::endl;

  } catch (const std::exception& e) {
    std::cerr << "[ERROR] Failed to start transport: " << e.what() << std::endl;
    // Exit the dispatcher on fatal error
    main_dispatcher_->exit();
  }
}

// Run the main event loop
// This should be called from the main thread after listen()
void McpServer::run() {
  if (listen_address_.empty()) {
    std::cerr << "[ERROR] No listen address specified. Call listen() first."
              << std::endl;
    return;
  }

  if (!initialized_) {
    std::cerr << "[ERROR] Server not initialized. Call listen() first."
              << std::endl;
    return;
  }

  // Set flag to perform listen on first run iteration
  need_perform_listen_ = true;

  // Override the base class run to handle listening in dispatcher thread
  std::cerr << "[INFO] Running main event loop" << std::endl;

  // Main dispatcher should already be created in initialize()
  if (!main_dispatcher_) {
    std::cerr << "[ERROR] Main dispatcher not initialized." << std::endl;
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

  // Following production pattern: use blocking dispatch loop
  // This properly waits for and processes file events
  std::cerr << "[INFO] Starting blocking dispatch loop" << std::endl;
  main_dispatcher_->run(event::RunType::Block);
  std::cerr << "[INFO] Main dispatch loop exited" << std::endl;
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
      // IMPROVEMENT: Stop listeners using TcpActiveListener (production
      // pattern) This provides coordinated shutdown with proper connection
      // draining
      for (auto& listener : tcp_listeners_) {
        listener->disable();
      }
      tcp_listeners_.clear();

      // Close legacy connection managers (for stdio)
      for (auto& conn_manager : connection_managers_) {
        conn_manager->close();
      }
      connection_managers_.clear();

      // Clear all sessions
      // Session cleanup will happen automatically

      // Exit the blocking dispatch loop
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
}

// Register notification handler
void McpServer::registerNotificationHandler(
    const std::string& method,
    std::function<void(const jsonrpc::Notification&, SessionContext&)>
        handler) {
  std::lock_guard<std::mutex> lock(handlers_mutex_);
  notification_handlers_[method] = handler;
}

// Send notification to specific session
VoidResult McpServer::sendNotification(
    const std::string& session_id, const jsonrpc::Notification& notification) {
  // Get session
  auto session = session_manager_->getSession(session_id);
  if (!session) {
    return makeVoidError(Error(jsonrpc::INVALID_PARAMS, "Session not found"));
  }

  // Send notification through session's connection
  // This needs to be done in dispatcher context
  auto send_promise = std::make_shared<std::promise<VoidResult>>();
  auto send_future = send_promise->get_future();

  main_dispatcher_->post([this, session, notification, send_promise]() {
    // Find the connection manager for this session's connection
    for (auto& conn_manager : connection_managers_) {
      if (conn_manager->isConnected()) {
        auto result = conn_manager->sendNotification(notification);
        send_promise->set_value(result);
        return;
      }
    }
    send_promise->set_value(makeVoidError(
        Error(jsonrpc::INTERNAL_ERROR, "No active connection for session")));
  });

  return send_future.get();
}

// Broadcast notification to all sessions
void McpServer::broadcastNotification(
    const jsonrpc::Notification& notification) {
  // Send to all active sessions in dispatcher context
  main_dispatcher_->post([this, notification]() {
    for (auto& conn_manager : connection_managers_) {
      if (conn_manager->isConnected()) {
        conn_manager->sendNotification(notification);
      }
    }
  });
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

// McpProtocolCallbacks overrides
void McpServer::onRequest(const jsonrpc::Request& request) {
  std::cerr << "[DEBUG] McpServer::onRequest called with method: "
            << request.method << std::endl;

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
                          : std::to_string(get<int>(request.id));
    pending_requests_[key] = pending_req;
  }

  // Get or create session for this connection
  // Try to get existing session first
  auto session = session_manager_->getSessionByConnection(current_connection_);
  if (!session) {
    // Create new session for this connection
    session = session_manager_->createSession(current_connection_);
  }

  if (session) {
    pending_req->session_id = session->getId();
  }

  if (!session) {
    // Max sessions reached
    server_stats_.requests_failed++;
    auto response = jsonrpc::Response::make_error(
        request.id, Error(jsonrpc::INTERNAL_ERROR, "Max sessions reached"));

    // Send response through appropriate mechanism
    // For HTTP connections, use filter chain; for stdio, use connection manager
    std::cerr << "[DEBUG] Sending error response for max sessions" << std::endl;

    // Send response through the current connection (for TCP/HTTP connections)
    // Following production pattern: thread-local connection context
    if (current_connection_) {
      filter::HttpSseFilterChainFactory::sendHttpResponse(response,
                                                          *current_connection_);
    }

    // Also try connection managers (for stdio connections)
    for (auto& conn_manager : connection_managers_) {
      if (conn_manager->isConnected()) {
        std::cerr << "[DEBUG] Found connected manager, sending response"
                  << std::endl;
        conn_manager->sendResponse(response);
        break;
      }
    }
    return;
  }

  session->updateActivity();

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
        response = handleCallTool(request, *session);
      } else if (request.method == "prompts/list") {
        response = handleListPrompts(request, *session);
      } else if (request.method == "prompts/get") {
        response = handleGetPrompt(request, *session);
      } else {
        // Method not found
        response = jsonrpc::Response::make_error(
            request.id, Error(jsonrpc::METHOD_NOT_FOUND,
                              "Method not found: " + request.method));
        server_stats_.requests_invalid++;
      }
    }
  }

  // Send response through appropriate channel
  // Following proper architecture: use filter chain for HTTP, connection
  // manager for stdio
  std::cerr << "[DEBUG] Sending response for request id: "
            << (holds_alternative<std::string>(request.id)
                    ? get<std::string>(request.id)
                    : std::to_string(get<int>(request.id)))
            << std::endl;

  // Send response through the current connection (for TCP/HTTP connections)
  // Following production pattern: server sends JSON-RPC, filter handles HTTP
  if (current_connection_) {
    // Convert response to JSON and send through connection
    // The filter chain will handle HTTP protocol wrapping
    auto json_val = json::to_json(response);
    std::string json_str = json_val.toString();

    OwnedBuffer response_buffer;
    response_buffer.add(json_str);

    // Write JSON-RPC response - HTTP filter will wrap it
    current_connection_->write(response_buffer, false);
  }

  // Also try connection managers (for stdio transport)
  // This is the legacy path for non-HTTP transports
  for (auto& conn_manager : connection_managers_) {
    if (conn_manager->isConnected()) {
      conn_manager->sendResponse(response);
      break;
    }
  }

  // Remove request from pending list
  {
    std::lock_guard<std::mutex> lock(pending_requests_mutex_);
    // Convert RequestId to string key
    std::string key = holds_alternative<std::string>(request.id)
                          ? get<std::string>(request.id)
                          : std::to_string(get<int>(request.id));
    pending_requests_.erase(key);
  }
}

void McpServer::onNotification(const jsonrpc::Notification& notification) {
  // Handle notification in dispatcher context
  server_stats_.notifications_total++;

  // Get session for this connection
  auto session = session_manager_->getSessionByConnection(current_connection_);
  if (!session) {
    // Create new session for notifications if needed
    session = session_manager_->createSession(current_connection_);
  }
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
            std::cerr << "[INFO] Request marked as cancelled: " << key_to_cancel
                      << std::endl;
          }
        }
      }
    }
  }
}

void McpServer::onResponse(const jsonrpc::Response& response) {
  // Server typically doesn't receive responses
  // This could happen if server makes requests to client (e.g., elicitation)
}

void McpServer::onConnectionEvent(network::ConnectionEvent event) {
  // Handle connection events
  switch (event) {
    case network::ConnectionEvent::Connected:
      server_stats_.connections_total++;
      server_stats_.connections_active++;
      break;

    case network::ConnectionEvent::RemoteClose:
    case network::ConnectionEvent::LocalClose:
      server_stats_.connections_active--;

      // Clean up session for this connection
      if (session_manager_ && current_connection_) {
        // Remove session associated with the closed connection
        session_manager_->removeSessionByConnection(current_connection_);

        // Remove from connection-session mapping
        // Following production pattern: use lock since this map may be accessed
        // from multiple dispatcher threads
        {
          std::lock_guard<std::mutex> lock(connection_sessions_mutex_);
          connection_sessions_.erase(current_connection_);
        }

        // Remove connection from active list
        // Following production pattern: all in dispatcher thread, no mutex
        // needed
        active_connections_.remove_if(
            [this](const network::ConnectionPtr& conn) {
              return conn.get() == current_connection_;
            });

        // Clear the current connection
        current_connection_ = nullptr;

        // Decrement connection count
        num_connections_--;
      }
      break;
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
  // Built-in handlers are handled directly in onRequest
  // This method can be used to register them explicitly if needed
}

jsonrpc::Response McpServer::handleInitialize(const jsonrpc::Request& request,
                                              SessionContext& session) {
  // Parse initialize request
  // TODO: Deserialize InitializeRequest from request.params

  // Store client info in session
  if (request.params.has_value()) {
    auto params = request.params.value();
    // Extract client info and store in session
    // session.setClientInfo(...);
  }

  // Build initialize result
  InitializeResult result;
  result.protocolVersion = config_.protocol_version;
  result.capabilities = config_.capabilities;
  result.serverInfo = make_optional(
      Implementation(config_.server_name, config_.server_version));

  if (!config_.instructions.empty()) {
    result.instructions = make_optional(config_.instructions);
  }

  // Convert InitializeResult to Metadata for ResponseResult
  // Since ResponseResult doesn't directly support InitializeResult,
  // we need to convert it to a simplified Metadata object
  // TODO: This is a temporary workaround until proper serialization is
  // implemented
  auto builder =
      make<Metadata>().add("protocolVersion", result.protocolVersion);

  // Add server info if present (flattened)
  if (result.serverInfo.has_value()) {
    builder.add("serverInfo.name", result.serverInfo->name)
        .add("serverInfo.version", result.serverInfo->version);
  }

  // Add capabilities (flattened)
  if (result.capabilities.resources.has_value()) {
    if (holds_alternative<bool>(result.capabilities.resources.value())) {
      builder.add("capabilities.resources",
                  get<bool>(result.capabilities.resources.value()));
    } else {
      // Handle ResourcesCapability struct
      builder.add("capabilities.resources", true)
          .add("capabilities.resources.subscribe", true)
          .add("capabilities.resources.listChanged", true);
    }
  }

  if (result.capabilities.tools.has_value()) {
    builder.add("capabilities.tools", result.capabilities.tools.value());
  }
  if (result.capabilities.prompts.has_value()) {
    builder.add("capabilities.prompts", result.capabilities.prompts.value());
  }
  if (result.capabilities.logging.has_value()) {
    builder.add("capabilities.logging", result.capabilities.logging.value());
  }

  // Add instructions if present
  if (result.instructions.has_value()) {
    builder.add("instructions", result.instructions.value());
  }

  auto response_metadata = builder.build();

  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(response_metadata));
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
      cursor = make_optional(get<std::string>(cursor_it->second));
    }
  }

  // Get resources from resource manager
  auto result = resource_manager_->listResources(cursor);

  // Convert to response
  // TODO: Serialize ListResourcesResult to ResponseResult
  auto response_metadata =
      make<Metadata>()
          .add("resourceCount", static_cast<int64_t>(result.resources.size()))
          .build();

  if (result.nextCursor.has_value()) {
    // Add nextCursor to the response
    response_metadata["nextCursor"] = result.nextCursor.value();
  }

  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(response_metadata));
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

  // Read resource
  auto result = resource_manager_->readResource(uri);

  // Convert to response
  // TODO: Serialize ReadResourceResult to ResponseResult
  auto response_metadata =
      make<Metadata>()
          .add("uri", uri)
          .add("contentCount", static_cast<int64_t>(result.contents.size()))
          .build();

  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(response_metadata));
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

  // Return empty success
  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(nullptr));
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

  // Return empty success
  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(nullptr));
}

jsonrpc::Response McpServer::handleListTools(const jsonrpc::Request& request,
                                             SessionContext& session) {
  // Get tools from tool registry
  auto result = tool_registry_->listTools();

  // Convert to response
  // TODO: Serialize ListToolsResult to ResponseResult
  auto response_metadata =
      make<Metadata>()
          .add("toolCount", static_cast<int64_t>(result.tools.size()))
          .build();

  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(response_metadata));
}

jsonrpc::Response McpServer::handleCallTool(const jsonrpc::Request& request,
                                            SessionContext& session) {
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
        arguments = make_optional(json::jsonToMetadata(args_value));
      } catch (const json::JsonException& e) {
        // If parsing fails, treat it as empty arguments
        arguments = make_optional(Metadata());
      }
    } else {
      // Fallback: if arguments is not a string, create empty metadata
      arguments = make_optional(Metadata());
    }
  }

  // Call tool
  auto result = tool_registry_->callTool(name, arguments, session);

  // Convert CallToolResult to proper response
  // Extract text content from the result
  std::string content_text;
  if (!result.content.empty()) {
    for (const auto& content_block : result.content) {
      if (holds_alternative<TextContent>(content_block)) {
        const auto& text_content = get<TextContent>(content_block);
        content_text += text_content.text;
      }
    }
  }

  // Build response metadata with actual result
  auto response_metadata =
      make<Metadata>()
          .add("content",
               content_text.empty() ? std::string("No result") : content_text)
          .add("isError", result.isError)
          .build();

  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(response_metadata));
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
      cursor = make_optional(get<std::string>(cursor_it->second));
    }
  }

  // Get prompts from prompt registry
  auto result = prompt_registry_->listPrompts(cursor);

  // Convert to response
  // TODO: Serialize ListPromptsResult to ResponseResult
  auto response_metadata =
      make<Metadata>()
          .add("prompts",
               std::string("Prompts list placeholder"))  // Simplified - avoid
                                                         // nested metadata
          .build();

  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(response_metadata));
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

  // Extract optional arguments
  // TODO: Properly handle nested metadata arguments
  optional<Metadata> arguments;
  auto args_it = params.find("arguments");
  if (args_it != params.end()) {
    // For now, create empty metadata if arguments are present
    arguments = make_optional(make<Metadata>().build());
  }

  // Get prompt
  auto result = prompt_registry_->getPrompt(name, arguments, session);

  // Convert to response
  // TODO: Serialize GetPromptResult to ResponseResult
  auto response_metadata =
      make<Metadata>()
          .add("promptName", name)
          .add("messageCount", static_cast<int64_t>(0))  // Simplified
          .build();

  return jsonrpc::Response::success(request.id,
                                    jsonrpc::ResponseResult(response_metadata));
}

// Background task management using dispatcher timers
void McpServer::startBackgroundTasks() {
  background_threads_running_ = true;

  // Schedule periodic session cleanup using dispatcher timer
  auto cleanup_timer = main_dispatcher_->createTimer([this]() {
    if (background_threads_running_) {
      // Clean up expired sessions
      session_manager_->cleanupExpiredSessions();

      // Reschedule for next cleanup
      auto next_timer = main_dispatcher_->createTimer([this]() {
        if (background_threads_running_) {
          startBackgroundTasks();
        }
      });
      next_timer->enableTimer(std::chrono::seconds(30));
    }
  });
  cleanup_timer->enableTimer(std::chrono::seconds(30));

  // Schedule periodic resource update notifications
  auto update_timer = main_dispatcher_->createTimer([this]() {
    if (background_threads_running_) {
      // Process pending resource updates for each session
      // Get all sessions and send pending updates

      // Reschedule
      auto next_timer = main_dispatcher_->createTimer([this]() {
        if (background_threads_running_) {
          // Continue resource update processing
        }
      });
      next_timer->enableTimer(config_.resource_update_debounce);
    }
  });
  update_timer->enableTimer(config_.resource_update_debounce);
}

void McpServer::stopBackgroundTasks() {
  background_threads_running_ = false;
  // Timers will naturally expire and not reschedule
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

  // Add ourselves as connection callbacks to track lifecycle
  // This allows us to clean up session when connection closes
  connection->addConnectionCallbacks(*this);

  // Store connection-to-session mapping
  // Following production pattern: listener owns connections
  {
    std::lock_guard<std::mutex> lock(connection_sessions_mutex_);
    connection_sessions_[conn_ptr] = session;
  }

  // Set current connection for request processing context
  current_connection_ = conn_ptr;

  // Update connection count
  ++num_connections_;

  // Store the connection to keep it alive
  // Following production pattern: server owns connections in dispatcher thread
  // No mutex needed - all operations happen in dispatcher thread
  // Connection will be removed from list when it closes via callbacks
  active_connections_.push_back(std::move(connection));

  // The connection is now ready for message processing
  // Messages will flow through filter chain to our onRequest/onNotification
  // handlers

  std::cerr << "[DEBUG] New connection established, session: "
            << session->getId() << std::endl;
}

}  // namespace server
}  // namespace mcp