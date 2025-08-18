/**
 * @file mcp_client.cc
 * @brief Implementation of enterprise-grade MCP client using MCP abstraction layer
 */

#include "mcp/client/mcp_client.h"

#include <algorithm>
#include <iostream>
#include <sstream>

#include "mcp/transport/http_sse_transport_socket.h"

namespace mcp {
namespace client {

// Constructor
McpClient::McpClient(const McpClientConfig& config)
    : ApplicationBase(config),
      config_(config),
      client_stats_() {
  
  // Initialize request tracker with configured timeout
  request_tracker_ = std::make_unique<RequestTracker>(config_.request_timeout);
  
  // Initialize circuit breaker with configured thresholds
  circuit_breaker_ = std::make_unique<CircuitBreaker>(
      config_.circuit_breaker_threshold,
      config_.circuit_breaker_timeout,
      config_.circuit_breaker_error_rate);
  
  // Initialize retry manager with exponential backoff settings
  retry_manager_ = std::make_unique<RetryManager>(
      config_.max_retries,
      config_.initial_retry_delay,
      config_.retry_backoff_multiplier,
      config_.max_retry_delay);
  
  // Initialize connection pool for efficient connection reuse
  connection_pool_ = std::make_unique<ConnectionPoolImpl>(
      *this,
      config_.connection_pool_size,
      config_.max_idle_connections);
}

// Destructor
McpClient::~McpClient() {
  // Stop the application base (stops dispatchers and workers)
  stop();
  
  // Disconnect if still connected
  if (connected_) {
    disconnect();
  }
  
  // Clean up any pending requests - do this in dispatcher context
  if (main_dispatcher_) {
    main_dispatcher_->post([this]() {
      auto pending = request_tracker_->getTimedOutRequests();
      for (const auto& request : pending) {
        request->promise.set_exception(
            std::make_exception_ptr(std::runtime_error("Client shutting down")));
      }
    });
  }
}

// Connect to MCP server
VoidResult McpClient::connect(const std::string& uri) {
  // Start the application if not already running
  if (!running_) {
    // Start in non-blocking mode - workers and dispatchers start
    std::thread([this]() {
      start();  // This will block until stop() is called
    }).detach();
    
    // Wait for main dispatcher to be ready
    while (!main_dispatcher_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  
  // All connection operations happen in dispatcher thread
  auto connect_promise = std::make_shared<std::promise<VoidResult>>();
  auto connect_future = connect_promise->get_future();
  
  main_dispatcher_->post([this, uri, connect_promise]() {
    try {
      // Transport negotiation flow:
      // 1. Parse URI to determine transport type
      // 2. Create connection configuration with transport settings
      // 3. Create connection manager and connect
      
      // Store URI before creating config so it's available
      current_uri_ = uri;
      
      // Negotiate transport based on URI scheme and configuration
      TransportType transport = negotiateTransport(uri);
      
      // Create connection configuration with URI information
      McpConnectionConfig conn_config = createConnectionConfig(transport);
      
      // Create connection manager in dispatcher context
      // The manager handles all protocol communication for this transport
      connection_manager_ = std::make_unique<McpConnectionManager>(
          *main_dispatcher_,
          *socket_interface_,
          conn_config);
      
      // Set ourselves as message callback handler
      connection_manager_->setMessageCallbacks(*this);
      
      // Initiate connection based on transport type
      // All transports use the same connect() method
      // The connection manager handles transport-specific details internally
      VoidResult result = connection_manager_->connect();
      
      // Check connection result
      if (holds_alternative<std::nullptr_t>(result)) {
        // Connection initiated successfully
        // For HTTP+SSE, this means TCP connection is in progress
        // We'll receive Connected event when actually connected
        // Don't set connected_ = true here - wait for the event
        client_stats_.connections_total++;
        
        // Only mark as connected for stdio (synchronous connection)
        if (transport == TransportType::Stdio) {
          connected_ = true;
          client_stats_.connections_active++;
        }
        
        // Schedule periodic tasks in dispatcher
        schedulePeriodicTasks();
      }
      
      connect_promise->set_value(result);
    } catch (const std::exception& e) {
      connect_promise->set_value(makeVoidError(
          Error(jsonrpc::INTERNAL_ERROR, e.what())));
    }
  });
  
  return connect_future.get();
}

// Disconnect from server
void McpClient::disconnect() {
  if (!connected_) {
    return;
  }
  
  connected_ = false;
  
  // Perform disconnect in dispatcher context
  if (main_dispatcher_) {
    main_dispatcher_->post([this]() {
      if (connection_manager_) {
        connection_manager_->close();
      }
      
      client_stats_.connections_active--;
      
      // Clear any pending requests
      auto pending = request_tracker_->getTimedOutRequests();
      for (const auto& request : pending) {
        request->promise.set_exception(
            std::make_exception_ptr(std::runtime_error("Connection closed")));
      }
      
      // Cancel periodic timers
      if (timeout_timer_) {
        timeout_timer_->disableTimer();
      }
      if (retry_timer_) {
        retry_timer_->disableTimer();
      }
    });
  }
}

// Initialize protocol
std::future<InitializeResult> McpClient::initialize() {
  // Build initialize request with client capabilities
  // For now, use simple parameters - full serialization needs JSON conversion
  auto init_params = make_metadata();
  init_params["protocolVersion"] = config_.protocol_version;
  init_params["clientName"] = config_.client_name;
  init_params["clientVersion"] = config_.client_version;
  
  // Send request and convert response to InitializeResult
  auto future = sendRequest("initialize", make_optional(init_params));
  
  // Create promise for InitializeResult
  auto result_promise = std::make_shared<std::promise<InitializeResult>>();
  
  // Process response in dispatcher context
  // Use shared_ptr to allow copying the lambda
  auto shared_future = std::make_shared<std::future<jsonrpc::Response>>(std::move(future));
  main_dispatcher_->post([shared_future, result_promise, this]() {
    // Process the future result directly
    try {
      auto response = shared_future->get();
      if (response.error.has_value()) {
        result_promise->set_exception(
            std::make_exception_ptr(std::runtime_error(response.error->message)));
      } else {
        // Parse InitializeResult from response
        InitializeResult init_result;
        
        // Parse the flattened response from server
        // The server returns a Metadata object with flattened fields
        if (holds_alternative<Metadata>(response.result.value())) {
          auto& metadata = get<Metadata>(response.result.value());
          
          // Extract protocol version
          auto proto_it = metadata.find("protocolVersion");
          if (proto_it != metadata.end() && holds_alternative<std::string>(proto_it->second)) {
            init_result.protocolVersion = get<std::string>(proto_it->second);
          }
          
          // Extract server info
          auto name_it = metadata.find("serverInfo.name");
          auto version_it = metadata.find("serverInfo.version");
          if (name_it != metadata.end() && version_it != metadata.end()) {
            Implementation server_info(
                holds_alternative<std::string>(name_it->second) ? get<std::string>(name_it->second) : "",
                holds_alternative<std::string>(version_it->second) ? get<std::string>(version_it->second) : ""
            );
            init_result.serverInfo = make_optional(server_info);
          }
          
          // Extract capabilities (simplified)
          ServerCapabilities caps;
          
          auto tools_it = metadata.find("capabilities.tools");
          if (tools_it != metadata.end() && holds_alternative<bool>(tools_it->second)) {
            caps.tools = make_optional(get<bool>(tools_it->second));
          }
          
          auto prompts_it = metadata.find("capabilities.prompts");
          if (prompts_it != metadata.end() && holds_alternative<bool>(prompts_it->second)) {
            caps.prompts = make_optional(get<bool>(prompts_it->second));
          }
          
          auto resources_it = metadata.find("capabilities.resources");
          if (resources_it != metadata.end() && holds_alternative<bool>(resources_it->second)) {
            caps.resources = make_optional(variant<bool, ResourcesCapability>(get<bool>(resources_it->second)));
          }
          
          auto logging_it = metadata.find("capabilities.logging");
          if (logging_it != metadata.end() && holds_alternative<bool>(logging_it->second)) {
            caps.logging = make_optional(get<bool>(logging_it->second));
          }
          
          init_result.capabilities = caps;
        } else {
          // Fallback if response format is unexpected
          init_result.protocolVersion = config_.protocol_version;
          init_result.capabilities = ServerCapabilities();
        }
        
        // Store server capabilities
        server_capabilities_ = init_result.capabilities;
        initialized_ = true;
        
        result_promise->set_value(init_result);
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  });
  
  return result_promise->get_future();
}

// Send request with future-based async API
std::future<jsonrpc::Response> McpClient::sendRequest(
    const std::string& method,
    const optional<Metadata>& params) {
  
  // Check circuit breaker state
  if (!circuit_breaker_->allowRequest()) {
    // Circuit is open, fail fast
    client_stats_.circuit_breaker_opens++;
    
    std::promise<jsonrpc::Response> promise;
    promise.set_value(jsonrpc::Response::make_error(
        make_request_id(0),
        Error(jsonrpc::INTERNAL_ERROR, "Circuit breaker is open")));
    return promise.get_future();
  }
  
  // Check connection
  if (!connected_) {
    std::promise<jsonrpc::Response> promise;
    promise.set_value(jsonrpc::Response::make_error(
        make_request_id(0),
        Error(jsonrpc::INTERNAL_ERROR, "Not connected")));
    return promise.get_future();
  }
  
  // Create request context
  auto context = createRequestContext(method, params);
  
  // Track request for timeout management
  request_tracker_->trackRequest(context);
  
  // Send request in dispatcher context
  main_dispatcher_->post([this, context]() {
    // Check flow control - queue if at limit
    if (config_.enable_flow_control) {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      
      if (request_queue_.size() >= config_.request_queue_limit) {
        // Queue is full, reject request
        context->promise.set_value(jsonrpc::Response::make_error(
            context->id,
            Error(jsonrpc::INTERNAL_ERROR, "Request queue full")));
        return;
      }
      
      if (request_tracker_->getPendingCount() >= config_.max_concurrent_requests) {
        // Queue request for later processing
        request_queue_.push(context);
        client_stats_.requests_queued++;
        return;
      }
    }
    
    // Send request immediately
    sendRequestInternal(context);
  });
  
  return context->promise.get_future();
}

// Send batch of requests
std::vector<std::future<jsonrpc::Response>> McpClient::sendBatch(
    const std::vector<std::pair<std::string, optional<Metadata>>>& requests) {
  
  std::vector<std::future<jsonrpc::Response>> futures;
  
  // Check if batch processing is beneficial
  if (requests.size() <= 1) {
    // Single request, use normal path
    for (const auto& req : requests) {
      futures.push_back(sendRequest(req.first, req.second));
    }
    return futures;
  }
  
  // Mark requests as part of batch
  client_stats_.requests_batched += requests.size();
  
  // Create contexts for all requests
  std::vector<std::shared_ptr<RequestContext>> contexts;
  for (const auto& req : requests) {
    auto context = createRequestContext(req.first, req.second);
    context->is_batch = true;
    contexts.push_back(context);
    futures.push_back(context->promise.get_future());
  }
  
  // Send batch in dispatcher context
  main_dispatcher_->post([this, contexts]() {
    // TODO: Implement actual batch sending to optimize network usage
    // For now, send individually
    for (auto& context : contexts) {
      request_tracker_->trackRequest(context);
      sendRequestInternal(context);
    }
  });
  
  return futures;
}

// Send notification (fire-and-forget)
VoidResult McpClient::sendNotification(const std::string& method,
                                       const optional<Metadata>& params) {
  if (!connected_) {
    return makeVoidError(Error(jsonrpc::INTERNAL_ERROR, "Not connected"));
  }
  
  // Send in dispatcher context
  auto notif_promise = std::make_shared<std::promise<VoidResult>>();
  auto notif_future = notif_promise->get_future();
  
  main_dispatcher_->post([this, method, params, notif_promise]() {
    // Create notification
    jsonrpc::Notification notification(method);
    notification.params = params;
    
    // Send through connection manager
    auto result = connection_manager_->sendNotification(notification);
    
    if (holds_alternative<std::nullptr_t>(result)) {
      client_stats_.requests_total++;
    }
    
    notif_promise->set_value(result);
  });
  
  return notif_future.get();
}

// Resource operations
std::future<ListResourcesResult> McpClient::listResources(const optional<Cursor>& cursor) {
  // Build request parameters
  auto params = make<Metadata>();
  if (cursor.has_value()) {
    params.add("cursor", cursor.value());
  }
  
  // Send request and convert response
  auto future = sendRequest("resources/list", make_optional(params.build()));
  
  auto result_promise = std::make_shared<std::promise<ListResourcesResult>>();
  
  // Process response in dispatcher context
  // Use shared_ptr to allow copying the lambda
  auto shared_future = std::make_shared<std::future<jsonrpc::Response>>(std::move(future));
  main_dispatcher_->post([shared_future, result_promise, this]() {
    // Process the future result directly
    try {
      auto response = shared_future->get();
      if (response.error.has_value()) {
        result_promise->set_exception(
            std::make_exception_ptr(std::runtime_error(response.error->message)));
      } else {
        ListResourcesResult result;
        // TODO: Deserialize from response.result
        client_stats_.resources_read++;
        result_promise->set_value(result);
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  });
  
  return result_promise->get_future();
}

// Read resource content
std::future<ReadResourceResult> McpClient::readResource(const std::string& uri) {
  auto params = make<Metadata>().add("uri", uri).build();
  
  auto future = sendRequest("resources/read", make_optional(params));
  
  auto result_promise = std::make_shared<std::promise<ReadResourceResult>>();
  
  // Process response in dispatcher context
  // Use shared_ptr to allow copying the lambda
  auto shared_future = std::make_shared<std::future<jsonrpc::Response>>(std::move(future));
  main_dispatcher_->post([shared_future, result_promise, this]() {
    // Process the future result directly
    try {
      auto response = shared_future->get();
      if (response.error.has_value()) {
        result_promise->set_exception(
            std::make_exception_ptr(std::runtime_error(response.error->message)));
      } else {
        ReadResourceResult result;
        // TODO: Deserialize from response.result
        client_stats_.resources_read++;
        result_promise->set_value(result);
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  });
  
  return result_promise->get_future();
}

// Tool operations
std::future<CallToolResult> McpClient::callTool(const std::string& name,
                                                const optional<Metadata>& arguments) {
  auto params = make<Metadata>().add("name", name);
  if (arguments.has_value()) {
    // TODO: Add arguments serialization properly
  }
  
  auto future = sendRequest("tools/call", make_optional(params.build()));
  
  auto result_promise = std::make_shared<std::promise<CallToolResult>>();
  
  // Process response in dispatcher context
  // Use shared_ptr to allow copying the lambda
  auto shared_future = std::make_shared<std::future<jsonrpc::Response>>(std::move(future));
  main_dispatcher_->post([shared_future, result_promise, this]() {
    // Process the future result directly
    try {
      auto response = shared_future->get();
      if (response.error.has_value()) {
        result_promise->set_exception(
            std::make_exception_ptr(std::runtime_error(response.error->message)));
      } else {
        CallToolResult result;
        // TODO: Deserialize from response.result
        client_stats_.tools_called++;
        result_promise->set_value(result);
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  });
  
  return result_promise->get_future();
}

// ApplicationBase overrides
void McpClient::initializeWorker(application::WorkerContext& worker) {
  // Initialize worker-specific resources
  // Each worker can handle requests independently
  
  // Workers process queued requests when main dispatcher delegates
  worker.getDispatcher().post([this, &worker]() {
    // Worker is ready to process requests
    // Connection pool can create connections in this worker's context
  });
}

void McpClient::setupFilterChain(application::FilterChainBuilder& builder) {
  // Call base class to add standard filters (rate limiting, metrics)
  ApplicationBase::setupFilterChain(builder);
  
  // Add MCP-specific filters
  
  // TODO: Replace with McpJsonRpcFilter once integrated with ApplicationBase
  // For now, use a simple placeholder filter until proper integration
  builder.addFilter([this]() -> network::FilterSharedPtr {
    // Temporary: return null filter until McpJsonRpcFilter is integrated
    // The actual JSON-RPC handling is done in onData callbacks
    return nullptr;
  });
  
  // Add request tracking filter for correlation and metrics
  builder.addFilter([this]() -> network::FilterSharedPtr {
    class RequestTrackingFilter : public network::NetworkFilterBase {
    public:
      RequestTrackingFilter(McpClient& client) : client_(client) {}
      
      // Track incoming data bytes
      network::FilterStatus onData(Buffer& data, bool end_stream) override {
        client_.client_stats_.bytes_received += data.length();
        return network::FilterStatus::Continue;
      }
      
      // Track outgoing data bytes
      network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
        client_.client_stats_.bytes_sent += data.length();
        return network::FilterStatus::Continue;
      }
      
      network::FilterStatus onNewConnection() override {
        return network::FilterStatus::Continue;
      }
      
    private:
      McpClient& client_;
    };
    
    return std::make_shared<RequestTrackingFilter>(*this);
  });
  
  // Add backpressure filter for flow control
  builder.addFilter([this]() -> network::FilterSharedPtr {
    class BackpressureFilter : public network::NetworkFilterBase {
    public:
      BackpressureFilter(McpClient& client) : client_(client) {}
      
      network::FilterStatus onData(Buffer& data, bool end_stream) override {
        // Check if we should apply backpressure
        if (client_.request_tracker_->getPendingCount() > 
            client_.config_.max_concurrent_requests * 0.8) {
          // Getting close to limit, slow down
          return network::FilterStatus::StopIteration;
        }
        return network::FilterStatus::Continue;
      }
      
      network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
        return network::FilterStatus::Continue;
      }
      
      network::FilterStatus onNewConnection() override {
        return network::FilterStatus::Continue;
      }
      
    private:
      McpClient& client_;
    };
    
    return std::make_shared<BackpressureFilter>(*this);
  });
}

// McpMessageCallbacks overrides
void McpClient::onRequest(const jsonrpc::Request& request) {
  // Client typically doesn't receive requests, but handle if needed
  // Could be server-initiated requests like elicitation
  client_stats_.requests_total++;
  
  // Process in dispatcher context
  main_dispatcher_->post([this, request]() {
    // Send error response for unsupported methods
    auto response = jsonrpc::Response::make_error(
        request.id,
        Error(jsonrpc::METHOD_NOT_FOUND, "Method not supported by client"));
    
    connection_manager_->sendResponse(response);
  });
}

void McpClient::onNotification(const jsonrpc::Notification& notification) {
  // Handle server notifications in dispatcher context
  
  // Progress notification
  if (notification.method == "notifications/progress") {
    // Extract progress notification data
    // TODO: Parse ProgressNotification from notification.params
    return;
  }
  
  // Resource update notification
  if (notification.method == "notifications/resources/updated") {
    // Handle resource update
    // Notify any resource subscribers
    return;
  }
  
  // Logging notification
  if (notification.method == "notifications/message") {
    // Handle log message from server
    // Could forward to logging system
    return;
  }
  
  // Tool list changed notification
  if (notification.method == "notifications/tools/list_changed") {
    // Invalidate cached tool list
    return;
  }
}

void McpClient::onResponse(const jsonrpc::Response& response) {
  // Process response in dispatcher context - already in dispatcher
  
  // Find the pending request
  auto context = request_tracker_->removeRequest(response.id);
  
  if (!context) {
    // Response for unknown request
    client_stats_.protocol_errors++;
    return;
  }
  
  // Update latency metrics
  auto duration = std::chrono::steady_clock::now() - context->start_time;
  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  updateLatencyMetrics(duration_ms);
  
  // Handle response based on success/error
  if (response.error.has_value()) {
    // Request failed
    client_stats_.requests_failed++;
    circuit_breaker_->recordFailure();
    
    // Check if we should retry
    if (retry_manager_->shouldRetry(context->retry_count)) {
      context->retry_count++;
      client_stats_.requests_retried++;
      
      // Schedule retry with exponential backoff
      auto delay = retry_manager_->getRetryDelay(context->retry_count);
      
      auto retry_timer = main_dispatcher_->createTimer([this, context]() {
        // Re-track request with new timestamp
        context->start_time = std::chrono::steady_clock::now();
        request_tracker_->trackRequest(context);
        
        // Resend request
        sendRequestInternal(context);
      });
      retry_timer->enableTimer(delay);
    } else {
      // No more retries, complete with error
      context->promise.set_value(response);
    }
  } else {
    // Request succeeded
    client_stats_.requests_success++;
    circuit_breaker_->recordSuccess();
    context->promise.set_value(response);
    
    // Process queued requests if any
    processQueuedRequests();
  }
}

void McpClient::onConnectionEvent(network::ConnectionEvent event) {
  // Handle connection events in dispatcher context
  switch (event) {
    case network::ConnectionEvent::Connected:
      connected_ = true;
      client_stats_.connections_active++;
      break;
      
    case network::ConnectionEvent::RemoteClose:
    case network::ConnectionEvent::LocalClose:
      connected_ = false;
      client_stats_.connections_active--;
      
      // Fail all pending requests
      auto pending = request_tracker_->getTimedOutRequests();
      for (const auto& request : pending) {
        request->promise.set_value(jsonrpc::Response::make_error(
            request->id,
            Error(jsonrpc::INTERNAL_ERROR, "Connection closed")));
      }
      break;
  }
}

void McpClient::onError(const Error& error) {
  client_stats_.errors_total++;
  
  // Track failure for circuit breaker
  circuit_breaker_->recordFailure();
  
  // Log error with context
  trackFailure(application::FailureReason(
      application::FailureReason::Type::ProtocolError,
      error.message));
}

// Internal methods
RequestId McpClient::generateRequestId() {
  return make_request_id(static_cast<int>(next_request_id_++));
}

std::shared_ptr<RequestContext> McpClient::createRequestContext(
    const std::string& method,
    const optional<Metadata>& params) {
  auto context = std::make_shared<RequestContext>(generateRequestId(), method);
  context->params = params;
  return context;
}

void McpClient::sendRequestInternal(std::shared_ptr<RequestContext> context) {
  // Must be called in dispatcher context
  
  // Create JSON-RPC request
  jsonrpc::Request request(context->id, context->method);
  request.params = context->params;
  
  // Send through connection manager
  auto result = connection_manager_->sendRequest(request);
  
  if (holds_alternative<Error>(result)) {
    // Failed to send
    client_stats_.requests_failed++;
    context->promise.set_value(jsonrpc::Response::make_error(
        context->id,
        get<Error>(result)));
  } else {
    client_stats_.requests_total++;
  }
}

void McpClient::handleTimeout(std::shared_ptr<RequestContext> context) {
  client_stats_.requests_timeout++;
  
  // Set timeout error
  context->promise.set_value(jsonrpc::Response::make_error(
      context->id,
      Error(jsonrpc::INTERNAL_ERROR, "Request timeout")));
}

// Connection pool implementation
McpClient::ConnectionPoolImpl::ConnectionPtr McpClient::ConnectionPoolImpl::createNewConnection() {
  // Create new connection through connection manager
  // This is called in dispatcher thread context
  
  // For MCP, we typically have a single connection per transport
  // Connection pooling is more relevant for HTTP/WebSocket transports
  return nullptr;  // Placeholder - actual implementation would create connection
}

// Transport negotiation
TransportType McpClient::negotiateTransport(const std::string& uri) {
  // Parse URI scheme to determine transport
  if (uri.find("stdio://") == 0) {
    return TransportType::Stdio;
  } else if (uri.find("http://") == 0 || uri.find("https://") == 0) {
    return TransportType::HttpSse;
  } else if (uri.find("ws://") == 0 || uri.find("wss://") == 0) {
    return TransportType::WebSocket;
  }
  
  // Default to configured preference
  return config_.preferred_transport;
}

McpConnectionConfig McpClient::createConnectionConfig(TransportType transport) {
  McpConnectionConfig config;
  config.transport_type = transport;
  config.buffer_limit = config_.buffer_high_watermark;
  config.connection_timeout = std::chrono::milliseconds(config_.request_timeout.count());
  
  // Set transport-specific configuration
  if (transport == TransportType::Stdio) {
    config.stdio_config = transport::StdioTransportSocketConfig();
  } else if (transport == TransportType::HttpSse) {
    // Configure HTTP+SSE transport with new architecture
    transport::HttpSseTransportSocketConfig http_config;
    
    // Parse URI to extract server address
    // For now, assume format like "http://localhost:8080" or "localhost:8080"
    std::string server_address = current_uri_;
    if (server_address.find("http://") == 0) {
      server_address = server_address.substr(7); // Remove "http://"
    } else if (server_address.find("https://") == 0) {
      server_address = server_address.substr(8); // Remove "https://"
      http_config.underlying_transport = 
          transport::HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
    }
    
    // Remove any path from the address
    size_t path_pos = server_address.find('/');
    if (path_pos != std::string::npos) {
      server_address = server_address.substr(0, path_pos);
    }
    
    http_config.server_address = server_address;
    http_config.mode = transport::HttpSseTransportSocketConfig::Mode::CLIENT;
    http_config.connect_timeout = std::chrono::milliseconds(30000);
    http_config.idle_timeout = config_.request_timeout;
    
    // Note: Headers and paths are now handled by the filter chain
    // The new architecture uses filters for HTTP protocol handling
    
    config.http_sse_config = http_config;
  }
  
  return config;
}

// Progress handling
void McpClient::handleProgressNotification(const ProgressNotification& notification) {
  // Extract progress token and value from notification params
  if (!notification.params.has_value()) {
    return;
  }
  
  // TODO: Parse ProgressNotification from params
  // For now, simplified handling
  
  // Find registered callback for this progress token
  std::lock_guard<std::mutex> lock(progress_mutex_);
  // Look up and call progress callback
}

// Track progress updates for a specific token
void McpClient::trackProgress(const ProgressToken& token,
                              std::function<void(double)> callback) {
  std::lock_guard<std::mutex> lock(progress_mutex_);
  // Convert token to string for map key
  std::string token_str;
  if (holds_alternative<std::string>(token)) {
    token_str = get<std::string>(token);
  } else {
    token_str = std::to_string(get<int>(token));
  }
  progress_callbacks_[token_str] = callback;
}

// Metrics
void McpClient::updateLatencyMetrics(uint64_t duration_ms) {
  client_stats_.request_duration_ms_total += duration_ms;
  
  // Update min/max atomically
  uint64_t current_min = client_stats_.request_duration_ms_min.load();
  while (duration_ms < current_min && 
         !client_stats_.request_duration_ms_min.compare_exchange_weak(
             current_min, duration_ms)) {
    // Retry if changed
  }
  
  uint64_t current_max = client_stats_.request_duration_ms_max.load();
  while (duration_ms > current_max &&
         !client_stats_.request_duration_ms_max.compare_exchange_weak(
             current_max, duration_ms)) {
    // Retry if changed
  }
}

// Schedule periodic tasks using dispatcher timers
void McpClient::schedulePeriodicTasks() {
  // Schedule timeout checking every second
  timeout_timer_ = main_dispatcher_->createTimer([this]() {
    // Check for timed out requests
    auto timed_out = request_tracker_->getTimedOutRequests();
    for (const auto& request : timed_out) {
      handleTimeout(request);
    }
    
    // Reschedule for next check
    timeout_timer_->enableTimer(std::chrono::seconds(1));
  });
  timeout_timer_->enableTimer(std::chrono::seconds(1));
  
  // Schedule retry processing
  retry_timer_ = main_dispatcher_->createTimer([this]() {
    processQueuedRequests();
    
    // Reschedule
    retry_timer_->enableTimer(std::chrono::milliseconds(100));
  });
  retry_timer_->enableTimer(std::chrono::milliseconds(100));
}

// Process queued requests when capacity available
void McpClient::processQueuedRequests() {
  std::unique_lock<std::mutex> lock(queue_mutex_);
  
  // Process queued requests if connection available
  while (!request_queue_.empty() && 
         request_tracker_->getPendingCount() < config_.max_concurrent_requests) {
    auto context = request_queue_.front();
    request_queue_.pop();
    lock.unlock();
    
    // Send request
    sendRequestInternal(context);
    
    lock.lock();
  }
}

// Subscribe to resource
std::future<VoidResult> McpClient::subscribeResource(const std::string& uri) {
  auto context = createRequestContext("resources/subscribe", nullopt);
  
  auto params = make<Metadata>()
      .add("uri", uri)
      .build();
  context->params = make_optional(params);
  
  sendRequestInternal(context);
  
  // Return future that will convert response to VoidResult
  std::promise<VoidResult> result_promise;
  auto result_future = result_promise.get_future();
  
  std::thread([context, promise = std::move(result_promise)]() mutable {
    try {
      auto response = context->promise.get_future().get();
      promise.set_value(VoidResult(nullptr));
    } catch (...) {
      promise.set_exception(std::current_exception());
    }
  }).detach();
  
  return result_future;
}

// Unsubscribe from resource
std::future<VoidResult> McpClient::unsubscribeResource(const std::string& uri) {
  auto context = createRequestContext("resources/unsubscribe", nullopt);
  
  auto params = make<Metadata>()
      .add("uri", uri)
      .build();
  context->params = make_optional(params);
  
  sendRequestInternal(context);
  
  // Return future that will convert response to VoidResult
  std::promise<VoidResult> result_promise;
  auto result_future = result_promise.get_future();
  
  std::thread([context, promise = std::move(result_promise)]() mutable {
    try {
      auto response = context->promise.get_future().get();
      promise.set_value(VoidResult(nullptr));
    } catch (...) {
      promise.set_exception(std::current_exception());
    }
  }).detach();
  
  return result_future;
}

// List tools
std::future<ListToolsResult> McpClient::listTools(const optional<Cursor>& cursor) {
  auto context = createRequestContext("tools/list", nullopt);
  
  if (cursor.has_value()) {
    auto params = make<Metadata>()
        .add("cursor", cursor.value())
        .build();
    context->params = make_optional(params);
  }
  
  sendRequestInternal(context);
  
  // Return future that will convert response to ListToolsResult
  std::promise<ListToolsResult> result_promise;
  auto result_future = result_promise.get_future();
  
  std::thread([context, promise = std::move(result_promise)]() mutable {
    try {
      auto response = context->promise.get_future().get();
      ListToolsResult result;
      // TODO: Parse actual response
      promise.set_value(result);
    } catch (...) {
      promise.set_exception(std::current_exception());
    }
  }).detach();
  
  return result_future;
}


// List prompts
std::future<ListPromptsResult> McpClient::listPrompts(const optional<Cursor>& cursor) {
  auto context = createRequestContext("prompts/list", nullopt);
  
  if (cursor.has_value()) {
    auto params = make<Metadata>()
        .add("cursor", cursor.value())
        .build();
    context->params = make_optional(params);
  }
  
  sendRequestInternal(context);
  
  // Return future that will convert response to ListPromptsResult
  std::promise<ListPromptsResult> result_promise;
  auto result_future = result_promise.get_future();
  
  std::thread([context, promise = std::move(result_promise)]() mutable {
    try {
      auto response = context->promise.get_future().get();
      ListPromptsResult result;
      // TODO: Parse actual response
      promise.set_value(result);
    } catch (...) {
      promise.set_exception(std::current_exception());
    }
  }).detach();
  
  return result_future;
}

// Get prompt
std::future<GetPromptResult> McpClient::getPrompt(const std::string& name,
                                                  const optional<Metadata>& arguments) {
  auto context = createRequestContext("prompts/get", nullopt);
  
  auto params = make<Metadata>()
      .add("name", name)
      .build();
  // TODO: Add arguments if provided
  context->params = make_optional(params);
  
  sendRequestInternal(context);
  
  // Return future that will convert response to GetPromptResult
  std::promise<GetPromptResult> result_promise;
  auto result_future = result_promise.get_future();
  
  std::thread([context, promise = std::move(result_promise)]() mutable {
    try {
      auto response = context->promise.get_future().get();
      GetPromptResult result;
      // TODO: Parse actual response
      promise.set_value(result);
    } catch (...) {
      promise.set_exception(std::current_exception());
    }
  }).detach();
  
  return result_future;
}

// Set log level
std::future<VoidResult> McpClient::setLogLevel(enums::LoggingLevel::Value level) {
  auto context = createRequestContext("logging/setLevel", nullopt);
  
  auto params = make<Metadata>()
      .add("level", static_cast<long long>(level))
      .build();
  context->params = make_optional(params);
  
  sendRequestInternal(context);
  
  // Return future that will convert response to VoidResult
  std::promise<VoidResult> result_promise;
  auto result_future = result_promise.get_future();
  
  std::thread([context, promise = std::move(result_promise)]() mutable {
    try {
      auto response = context->promise.get_future().get();
      promise.set_value(VoidResult(nullptr));
    } catch (...) {
      promise.set_exception(std::current_exception());
    }
  }).detach();
  
  return result_future;
}

// Create message
std::future<CreateMessageResult> McpClient::createMessage(
    const std::vector<SamplingMessage>& messages,
    const optional<ModelPreferences>& preferences) {
  auto context = createRequestContext("sampling/createMessage", nullopt);
  
  // TODO: Serialize messages and preferences
  auto params = make<Metadata>().build();
  context->params = make_optional(params);
  
  sendRequestInternal(context);
  
  // Return future that will convert response to CreateMessageResult
  std::promise<CreateMessageResult> result_promise;
  auto result_future = result_promise.get_future();
  
  std::thread([context, promise = std::move(result_promise)]() mutable {
    try {
      auto response = context->promise.get_future().get();
      CreateMessageResult result;
      // TODO: Parse actual response
      promise.set_value(result);
    } catch (...) {
      promise.set_exception(std::current_exception());
    }
  }).detach();
  
  return result_future;
}


}  // namespace client
}  // namespace mcp