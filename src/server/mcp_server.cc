/**
 * @file mcp_server.cc
 * @brief Implementation of enterprise-grade MCP server using MCP abstraction layer
 */

#include "mcp/server/mcp_server.h"
#include <future>

#include <algorithm>
#include <iostream>
#include <sstream>

#include "mcp/transport/http_sse_transport_socket.h"

namespace mcp {
namespace server {

// Constructor
McpServer::McpServer(const McpServerConfig& config)
    : ApplicationBase(config),
      config_(config),
      server_stats_() {
  
  // Initialize session manager for client connection tracking
  session_manager_ = std::make_unique<SessionManager>(config_, server_stats_);
  
  // Initialize resource manager for resource handling
  resource_manager_ = std::make_unique<ResourceManager>(server_stats_);
  
  // Initialize tool registry for tool management
  tool_registry_ = std::make_unique<ToolRegistry>(server_stats_);
  
  // Initialize prompt registry for prompt templates
  prompt_registry_ = std::make_unique<PromptRegistry>(server_stats_);
  
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
  // Start the application if not already running
  if (!running_) {
    // Start application base - this creates workers and dispatchers
    std::thread([this]() {
      std::cerr << "[DEBUG] ApplicationBase thread starting..." << std::endl;
      try {
        start();  // This will block until stop() is called
        std::cerr << "[DEBUG] ApplicationBase thread exiting normally" << std::endl;
      } catch (const std::exception& e) {
        std::cerr << "[ERROR] ApplicationBase thread exception: " << e.what() << std::endl;
      }
    }).detach();
    
    // Wait for main dispatcher to be ready
    int wait_count = 0;
    while (!main_dispatcher_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      if (++wait_count > 500) {  // 5 seconds timeout
        std::cerr << "[ERROR] Timeout waiting for dispatcher" << std::endl;
        return makeVoidError(Error(jsonrpc::INTERNAL_ERROR, "Dispatcher initialization timeout"));
      }
    }
    std::cerr << "[DEBUG] Main dispatcher ready" << std::endl;
  }
  
  // Parse address to determine transport type
  auto listen_promise = std::make_shared<std::promise<VoidResult>>();
  auto listen_future = listen_promise->get_future();
  
  // Perform listen operation in dispatcher context
  // All network operations must happen in dispatcher thread to ensure thread safety
  main_dispatcher_->post([this, address, listen_promise]() {
    try {
      // Transport selection flow:
      // 1. Parse the address URL to determine transport type
      // 2. Create a single connection manager for that transport
      // 3. Configure transport-specific settings
      // 4. Start listening on the appropriate endpoint
      
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
      
      // Create connection manager for the determined transport type
      // Only create one manager per server instance to avoid conflicts
      McpConnectionConfig conn_config;
      conn_config.transport_type = transport_type;
      conn_config.buffer_limit = config_.buffer_high_watermark;
      conn_config.connection_timeout = config_.request_processing_timeout;
      
      // Configure transport-specific settings
      if (transport_type == TransportType::Stdio) {
        // Stdio transport configuration
        // Uses standard input/output for bidirectional communication
        conn_config.stdio_config = transport::StdioTransportSocketConfig();
      } else if (transport_type == TransportType::HttpSse) {
        // HTTP+SSE transport configuration
        // Creates HTTP server for requests and SSE stream for server->client messages
        transport::HttpSseTransportSocketConfig http_config;
        
        // Store full endpoint URL - will be parsed by transport layer
        // Format: http://host:port/path
        http_config.endpoint_url = address;
        conn_config.http_sse_config = http_config;
      }
      
      // Create connection manager for the selected transport
      // The manager handles all connections for this transport type
      auto conn_manager = std::make_unique<McpConnectionManager>(
          *main_dispatcher_,
          *socket_interface_,
          conn_config);
      
      // Set ourselves as message callback handler
      // This allows the server to receive all incoming messages
      conn_manager->setMessageCallbacks(*this);
      
      // Start listening based on transport type
      // Each transport has different connection semantics
      VoidResult result;
      if (transport_type == TransportType::Stdio) {
        // Stdio transport: immediate bidirectional connection
        // Acts as a "server" by reading from stdin and writing to stdout
        // No network listening required
        result = conn_manager->connect();
      } else if (transport_type == TransportType::HttpSse) {
        // HTTP+SSE transport: TCP server with HTTP protocol
        // Parse URL to extract listening port
        uint32_t port = 8080;  // Default HTTP port
        
        // URL parsing flow:
        // Format: http://[host]:port[/path]
        // We only need the port for listening, host is ignored (listens on all interfaces)
        if (address.find("http://") == 0 || address.find("https://") == 0) {
          std::string url = address;
          size_t protocol_end = url.find("://") + 3;
          size_t port_start = url.find(':', protocol_end);
          
          if (port_start != std::string::npos) {
            // Extract port number from URL
            size_t port_end = url.find('/', port_start + 1);
            std::string port_str = (port_end != std::string::npos) 
                ? url.substr(port_start + 1, port_end - port_start - 1)
                : url.substr(port_start + 1);
            port = std::stoi(port_str);
          }
        }
        
        // Create TCP address for listening
        // Binds to all interfaces (0.0.0.0) on the specified port
        auto tcp_address = network::Address::anyAddress(network::Address::IpVersion::v4, port);
        result = conn_manager->listen(tcp_address);
      } else {
        // Future: support for other transports (Unix domain sockets, named pipes, etc.)
        auto net_address = network::Address::pipeAddress(address);
        result = conn_manager->listen(net_address);
      }
      
      // Check if listening was successful
      if (holds_alternative<std::nullptr_t>(result)) {
        // Success - store the connection manager
        connection_managers_.push_back(std::move(conn_manager));
        std::cerr << "[DEBUG] Successfully started " 
                  << (transport_type == TransportType::Stdio ? "stdio" : "HTTP+SSE")
                  << " transport" << std::endl;
      } else {
        // Failed to start transport
        auto error = get<Error>(result);
        std::cerr << "[ERROR] Failed to setup transport: " << error.message << std::endl;
        listen_promise->set_value(makeVoidError(error));
        return;
      }
      
      if (!connection_managers_.empty()) {
        server_running_ = true;
        
        // Start background tasks for session cleanup and resource updates
        startBackgroundTasks();
        
        listen_promise->set_value(nullptr);  // Success
      } else {
        listen_promise->set_value(makeVoidError(
            Error(jsonrpc::INTERNAL_ERROR, "Failed to start any transport listeners")));
      }
    } catch (const std::exception& e) {
      listen_promise->set_value(makeVoidError(
          Error(jsonrpc::INTERNAL_ERROR, e.what())));
    }
  });
  
  return listen_future.get();
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
      // Close all connection managers
      for (auto& conn_manager : connection_managers_) {
        conn_manager->close();
      }
      connection_managers_.clear();
      
      // Clear all sessions
      // Session cleanup will happen automatically
    });
  }
  
  // Stop application base (stops workers and dispatchers)
  stop();
}

// Register request handler
void McpServer::registerRequestHandler(
    const std::string& method,
    std::function<jsonrpc::Response(const jsonrpc::Request&, SessionContext&)> handler) {
  std::lock_guard<std::mutex> lock(handlers_mutex_);
  request_handlers_[method] = handler;
}

// Register notification handler
void McpServer::registerNotificationHandler(
    const std::string& method,
    std::function<void(const jsonrpc::Notification&, SessionContext&)> handler) {
  std::lock_guard<std::mutex> lock(handlers_mutex_);
  notification_handlers_[method] = handler;
}

// Send notification to specific session
VoidResult McpServer::sendNotification(const std::string& session_id,
                                       const jsonrpc::Notification& notification) {
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
void McpServer::broadcastNotification(const jsonrpc::Notification& notification) {
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
  
  // Add JSON-RPC message filter for protocol handling
  builder.addFilter([this]() -> network::FilterSharedPtr {
    return std::make_shared<JsonRpcMessageFilter>(*this);
  });
  
  // Add session tracking filter
  builder.addFilter([this]() -> network::FilterSharedPtr {
    class SessionTrackingFilter : public network::NetworkFilterBase {
    public:
      SessionTrackingFilter(McpServer& server) : server_(server) {}
      
      network::FilterStatus onData(Buffer& data, bool end_stream) override {
        // Update session activity on data received
        // Session tracking happens at higher level
        return network::FilterStatus::Continue;
      }
      
      network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
        return network::FilterStatus::Continue;
      }
      
      network::FilterStatus onNewConnection() override {
        // New connection - will create session when initialized
        return network::FilterStatus::Continue;
      }
      
    private:
      McpServer& server_;
    };
    
    return std::make_shared<SessionTrackingFilter>(*this);
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

// McpMessageCallbacks overrides
void McpServer::onRequest(const jsonrpc::Request& request) {
  // Handle request in dispatcher context - already in dispatcher
  server_stats_.requests_total++;
  
  // Get or create session for this connection
  // For now, use a simplified session lookup
  auto session = session_manager_->createSession(nullptr);  // TODO: Get actual connection
  if (!session) {
    // Max sessions reached
    server_stats_.requests_failed++;
    auto response = jsonrpc::Response::make_error(
        request.id,
        Error(jsonrpc::INTERNAL_ERROR, "Max sessions reached"));
    
    // Send response through appropriate connection manager
    for (auto& conn_manager : connection_managers_) {
      if (conn_manager->isConnected()) {
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
            request.id,
            Error(jsonrpc::INTERNAL_ERROR, e.what()));
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
            request.id,
            Error(jsonrpc::METHOD_NOT_FOUND, "Method not found: " + request.method));
        server_stats_.requests_invalid++;
      }
    }
  }
  
  // Send response through connection manager
  for (auto& conn_manager : connection_managers_) {
    if (conn_manager->isConnected()) {
      conn_manager->sendResponse(response);
      break;
    }
  }
}

void McpServer::onNotification(const jsonrpc::Notification& notification) {
  // Handle notification in dispatcher context
  server_stats_.notifications_total++;
  
  // Get session for this connection
  auto session = session_manager_->createSession(nullptr);  // TODO: Get actual connection
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
    // TODO: Cancel pending request if still processing
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
      // TODO: Find and remove session for closed connection
      break;
  }
}

void McpServer::onError(const Error& error) {
  server_stats_.errors_total++;
  
  // Log error with context
  trackFailure(application::FailureReason(
      application::FailureReason::Type::ProtocolError,
      error.message));
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
  result.serverInfo = make_optional(Implementation(
      config_.server_name, config_.server_version));
  
  if (!config_.instructions.empty()) {
    result.instructions = make_optional(config_.instructions);
  }
  
  // Convert to response
  // For now, return simplified response without nested objects
  auto response_metadata = make<Metadata>()
      .add("protocolVersion", result.protocolVersion)
      .add("serverName", config_.server_name)
      .add("serverVersion", config_.server_version)
      .build();
  
  return jsonrpc::Response::success(request.id, 
      jsonrpc::ResponseResult(response_metadata));
}

jsonrpc::Response McpServer::handlePing(const jsonrpc::Request& request,
                                        SessionContext& session) {
  // Simple ping response
  return jsonrpc::Response::success(request.id, 
      jsonrpc::ResponseResult(make<Metadata>().add("pong", true).build()));
}

jsonrpc::Response McpServer::handleListResources(const jsonrpc::Request& request,
                                                 SessionContext& session) {
  // Extract cursor if provided
  optional<Cursor> cursor;
  if (request.params.has_value()) {
    auto params = request.params.value();
    auto cursor_it = params.find("cursor");
    if (cursor_it != params.end() && holds_alternative<std::string>(cursor_it->second)) {
      cursor = make_optional(get<std::string>(cursor_it->second));
    }
  }
  
  // Get resources from resource manager
  auto result = resource_manager_->listResources(cursor);
  
  // Convert to response
  // TODO: Serialize ListResourcesResult to ResponseResult
  auto response_metadata = make<Metadata>()
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
    return jsonrpc::Response::make_error(request.id,
        Error(jsonrpc::INVALID_PARAMS, "Missing uri parameter"));
  }
  
  auto params = request.params.value();
  auto uri_it = params.find("uri");
  if (uri_it == params.end() || !holds_alternative<std::string>(uri_it->second)) {
    return jsonrpc::Response::make_error(request.id,
        Error(jsonrpc::INVALID_PARAMS, "Invalid uri parameter"));
  }
  
  std::string uri = get<std::string>(uri_it->second);
  
  // Read resource
  auto result = resource_manager_->readResource(uri);
  
  // Convert to response
  // TODO: Serialize ReadResourceResult to ResponseResult
  auto response_metadata = make<Metadata>()
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
    return jsonrpc::Response::make_error(request.id,
        Error(jsonrpc::INVALID_PARAMS, "Missing uri parameter"));
  }
  
  auto params = request.params.value();
  auto uri_it = params.find("uri");
  if (uri_it == params.end() || !holds_alternative<std::string>(uri_it->second)) {
    return jsonrpc::Response::make_error(request.id,
        Error(jsonrpc::INVALID_PARAMS, "Invalid uri parameter"));
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
    return jsonrpc::Response::make_error(request.id,
        Error(jsonrpc::INVALID_PARAMS, "Missing uri parameter"));
  }
  
  auto params = request.params.value();
  auto uri_it = params.find("uri");
  if (uri_it == params.end() || !holds_alternative<std::string>(uri_it->second)) {
    return jsonrpc::Response::make_error(request.id,
        Error(jsonrpc::INVALID_PARAMS, "Invalid uri parameter"));
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
  auto response_metadata = make<Metadata>()
      .add("toolCount", static_cast<int64_t>(result.tools.size()))
      .build();
  
  return jsonrpc::Response::success(request.id, 
      jsonrpc::ResponseResult(response_metadata));
}

jsonrpc::Response McpServer::handleCallTool(const jsonrpc::Request& request,
                                            SessionContext& session) {
  // Extract tool name and arguments
  if (!request.params.has_value()) {
    return jsonrpc::Response::make_error(request.id,
        Error(jsonrpc::INVALID_PARAMS, "Missing parameters"));
  }
  
  auto params = request.params.value();
  auto name_it = params.find("name");
  if (name_it == params.end() || !holds_alternative<std::string>(name_it->second)) {
    return jsonrpc::Response::make_error(request.id,
        Error(jsonrpc::INVALID_PARAMS, "Invalid name parameter"));
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
  
  // Call tool
  auto result = tool_registry_->callTool(name, arguments, session);
  
  // Convert to response
  // TODO: Serialize CallToolResult to ResponseResult
  auto response_metadata = make<Metadata>()
      .add("content", std::string("Tool result placeholder"))  // Simplified - avoid nested metadata
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
    if (cursor_it != params.end() && holds_alternative<std::string>(cursor_it->second)) {
      cursor = make_optional(get<std::string>(cursor_it->second));
    }
  }
  
  // Get prompts from prompt registry
  auto result = prompt_registry_->listPrompts(cursor);
  
  // Convert to response
  // TODO: Serialize ListPromptsResult to ResponseResult
  auto response_metadata = make<Metadata>()
      .add("prompts", std::string("Prompts list placeholder"))  // Simplified - avoid nested metadata
      .build();
  
  return jsonrpc::Response::success(request.id, 
      jsonrpc::ResponseResult(response_metadata));
}

jsonrpc::Response McpServer::handleGetPrompt(const jsonrpc::Request& request,
                                             SessionContext& session) {
  // Extract prompt name and arguments
  if (!request.params.has_value()) {
    return jsonrpc::Response::make_error(request.id,
        Error(jsonrpc::INVALID_PARAMS, "Missing parameters"));
  }
  
  auto params = request.params.value();
  auto name_it = params.find("name");
  if (name_it == params.end() || !holds_alternative<std::string>(name_it->second)) {
    return jsonrpc::Response::make_error(request.id,
        Error(jsonrpc::INVALID_PARAMS, "Invalid name parameter"));
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
  auto response_metadata = make<Metadata>()
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

}  // namespace server
}  // namespace mcp