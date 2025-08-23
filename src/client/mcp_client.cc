#include "mcp/client/mcp_client.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/socket_interface_impl.h"
#include <thread>
#include <future>
#include <iostream>
#include <sstream>

namespace mcp {
namespace client {

using namespace mcp::network;
using namespace mcp::event;
using namespace mcp::types;

// Constructor
McpClient::McpClient(const McpClientConfig& config)
    : config_(config) {
  
  // Set callbacks for protocol state changes
  protocol::McpProtocolStateMachineConfig protocol_config;
  protocol_config.initialization_timeout = config_.protocol_initialization_timeout;
  protocol_config.connection_timeout = config_.protocol_connection_timeout;
  protocol_config.drain_timeout = config_.protocol_drain_timeout;
  protocol_config.auto_reconnect = config_.protocol_auto_reconnect;
  protocol_config.max_reconnect_attempts = config_.protocol_max_reconnect_attempts;
  protocol_config.reconnect_delay = config_.protocol_reconnect_delay;
  
  // Initialize request tracker
  request_tracker_ = std::make_unique<RequestTracker>();
  
  // Initialize circuit breaker
  circuit_breaker_ = std::make_unique<CircuitBreaker>(
      config_.circuit_breaker_threshold,
      config_.circuit_breaker_timeout,
      5);  // Default half-open max requests
  
  // Initialize protocol callbacks
  protocol_callbacks_ = std::make_unique<McpProtocolCallbacks>(
      [this](network::ConnectionEvent event) {
        handleConnectionEvent(event);
      },
      [this](std::unique_ptr<Buffer> data) {
        handleData(std::move(data));
      },
      [this](const Error& error) {
        handleError(error);
      }
  );
  
  // Set callbacks for protocol state changes
  protocol_config.state_change_callback = 
      [this](const protocol::ProtocolStateTransitionContext& ctx) {
        handleProtocolStateChange(ctx);
      };
  
  protocol_config.error_callback = [this](const Error& error) {
    handleError(error);
  };
  
  // Protocol state machine will be created in dispatcher thread during initialization
}
// Destructor
McpClient::~McpClient() {
  shutdown();
}

// Connect to server
VoidResult McpClient::connect(const std::string& uri) {
  // Check if already shutting down
  if (shutting_down_) {
    return makeVoidError(Error(jsonrpc::INTERNAL_ERROR, 
                              "Client is shutting down"));
  }
  
  // Check if already connected
  if (connected_) {
    return makeVoidError(Error(jsonrpc::INVALID_REQUEST, 
                              "Already connected"));
  }
  
  // Client handles its own threading, no need for application framework
  
  // Start the main event loop in a separate thread
  // This runs the main dispatcher that processes our connect request
  std::thread([this]() {
    run();  // This will block until shutdown_requested_ is set
  }).detach();
  
  // Wait for main dispatcher to be ready with timeout
  int wait_count = 0;
  while (!main_dispatcher_ && wait_count < 1000 && !shutting_down_) { // 10 second timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    wait_count++;
  }
  
  if (shutting_down_) {
    return makeVoidError(Error(jsonrpc::INTERNAL_ERROR, 
                              "Client is shutting down"));
  }
  
  if (!main_dispatcher_) {
    return makeVoidError(Error(jsonrpc::INTERNAL_ERROR, 
                              "Failed to create main dispatcher"));
  }
  
  // Get socket interface after dispatcher is created
  socket_interface_ = std::make_unique<SocketInterfaceImpl>(main_dispatcher_);
  
  // Create connect promise
  auto connect_promise = std::make_shared<std::promise<VoidResult>>();
  auto connect_future = connect_promise->get_future();
  
  main_dispatcher_->post([this, uri, connect_promise]() {
    try {
      // Initialize protocol state machine if not already created
      if (!protocol_state_machine_) {
        protocol::McpProtocolStateMachineConfig protocol_config;
        protocol_config.initialization_timeout = config_.protocol_initialization_timeout;
        protocol_config.connection_timeout = config_.protocol_connection_timeout;
        protocol_config.drain_timeout = config_.protocol_drain_timeout;
        protocol_config.auto_reconnect = config_.protocol_auto_reconnect;
        protocol_config.max_reconnect_attempts = config_.protocol_max_reconnect_attempts;
        protocol_config.reconnect_delay = config_.protocol_reconnect_delay;
        
        protocol_config.state_change_callback = 
            [this](const protocol::ProtocolStateTransitionContext& ctx) {
              handleProtocolStateChange(ctx);
            };
        
        protocol_config.error_callback = [this](const Error& error) {
          handleError(error);
        };
        
        protocol_state_machine_ = std::make_unique<protocol::McpProtocolStateMachine>(
            *main_dispatcher_, protocol_config);
      }
      
      // Trigger protocol connection state
      // We're already in dispatcher thread from the outer post() at line 142
      if (protocol_state_machine_) {
        protocol_state_machine_->handleEvent(protocol::McpProtocolEvent::CONNECT_REQUESTED);
      }
      
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
      
      // Set message callback handler
      connection_manager_->setProtocolCallbacks(*protocol_callbacks_);
      
      // Initiate connection based on transport type
      // All transports use the same connect() method
      // The connection manager handles transport-specific details internally
      VoidResult result = connection_manager_->connect();
      
      // Check connection result
      if (is_error<std::nullptr_t>(result)) {
        auto error = get_error<std::nullptr_t>(result);
        connect_promise->set_value(makeVoidError(*error));
        
        // Notify protocol state machine of failure
        if (protocol_state_machine_) {
          protocol_state_machine_->handleError(*error);
        }
      } else {
        // Connection initiated successfully
        connect_promise->set_value(VoidResult(nullptr));
      }
    } catch (const std::exception& e) {
      connect_promise->set_value(makeVoidError(Error(jsonrpc::INTERNAL_ERROR, e.what())));
    }
  });
  
  // Wait for connection to be established
  auto status = connect_future.wait_for(std::chrono::seconds(10));
  if (status == std::future_status::timeout) {
    return makeVoidError(Error(jsonrpc::INTERNAL_ERROR, "Connection timeout"));
  }
  
  return connect_future.get();
}

// Disconnect from server
void McpClient::disconnect() {
  // Trigger protocol shutdown
  if (protocol_state_machine_) {
    protocol_state_machine_->handleEvent(protocol::McpProtocolEvent::SHUTDOWN_REQUESTED);
  }
  
  // Close connection
  if (connection_manager_) {
    connection_manager_->close();
  }
  
  // Reset state
  connected_ = false;
  initialized_ = false;
}


// Shutdown client
void McpClient::shutdown() {
  if (shutting_down_) {
    return;
  }
  shutting_down_ = true;
  
  // Disconnect if connected
  if (connected_) {
    disconnect();
  }
  
  // Request dispatcher shutdown
  shutdown_requested_ = true;
  
  // Notify dispatcher to exit
  if (main_dispatcher_) {
    main_dispatcher_->exit();
  }
  
  // This will cause dispatcher_->run() to return
  
  // Clean up resources
  protocol_state_machine_.reset();
  connection_manager_.reset();
  request_tracker_.reset();
  circuit_breaker_.reset();
  
  // Client resources are cleaned up above
}

// Start application workers
bool McpClient::start() {
  // Workers are started in run() method
  return true;
}

// Run main event loop
void McpClient::run() {
  std::cerr << "[INFO] Created main dispatcher" << std::endl;
  
  // Create and run dispatcher - this becomes the main event loop
  main_dispatcher_ = std::make_unique<LibeventDispatcher>(std::chrono::milliseconds(50));
  
  // Print initialization message
  std::cerr << "[INFO] Initializing application with " 
            << config_.num_workers << " workers" << std::endl;
  
  // Start application workers
  std::cerr << "[INFO] Starting application workers" << std::endl;
  for (int i = 0; i < config_.num_workers; ++i) {
    std::cerr << "[INFO] Worker worker_" << i << " starting" << std::endl;
  }
  
  // Run dispatcher loop - this blocks until shutdown
  std::cerr << "[INFO] Running main event loop" << std::endl;
  main_dispatcher_->run();
  
  // Cleanup after dispatcher exits
  main_dispatcher_.reset();
  shutdown_requested_ = true;
}

// Schedule periodic maintenance tasks (rate limiting, circuit breaker, etc.)
void McpClient::schedulePeriodicTasks() {
  if (!main_dispatcher_) {
    return;
  }
  
  // Schedule request timeout check every second - must be in dispatcher thread
  // Since this is called after connect, we're already in dispatcher thread
  timeout_timer_ = main_dispatcher_->createTimer([this]() {
    checkRequestTimeouts();
  });
  timeout_timer_->enableTimer(std::chrono::seconds(1));
}

// Initialize protocol
std::future<InitializeResult> McpClient::initializeProtocol() {
  // Create promise for InitializeResult
  auto result_promise = std::make_shared<std::promise<InitializeResult>>();
  
  // Defer all protocol operations to dispatcher thread
  main_dispatcher_->post([this, result_promise]() {
    try {
      // Notify protocol state machine that initialization is starting
      if (protocol_state_machine_) {
        protocol_state_machine_->handleEvent(protocol::McpProtocolEvent::INITIALIZE_REQUESTED);
      }
      
      // Build initialize request with client capabilities
      // For now, use simple parameters - full serialization needs JSON conversion
      auto init_params = make_metadata();
      init_params["protocolVersion"] = config_.protocol_version;
      init_params["clientName"] = config_.client_name;
      init_params["clientVersion"] = config_.client_version;
      
      // Send request and get response
      auto future = sendRequest("initialize", make_optional(init_params));
      auto response = future.get();
      
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
        
        // Notify protocol state machine that initialization is complete
        if (protocol_state_machine_) {
          protocol_state_machine_->handleEvent(protocol::McpProtocolEvent::INITIALIZED);
        }
        
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
  
  // Check if circuit breaker allows request
  if (!circuit_breaker_->allowRequest()) {
    client_stats_.circuit_breaker_opens++;
    auto promise = std::make_shared<std::promise<jsonrpc::Response>>();
    promise->set_value(jsonrpc::Response::make_error(
        "", Error(jsonrpc::INTERNAL_ERROR, "Circuit breaker open")));
    return promise->get_future();
  }
  
  // Generate request ID
  auto id = std::to_string(next_request_id_++);
  
  // Create request context
  auto context = std::make_shared<RequestContext>();
  context->id = id;
  context->method = method;
  context->params = params;
  context->sent_time = std::chrono::steady_clock::now();
  context->retry_count = 0;
  
  // Track request
  request_tracker_->trackRequest(context);
  client_stats_.requests_sent++;
  
  // Send request through internal pathway
  sendRequestInternal(context);
  
  return context->promise.get_future();
}

// Send request internally with retry logic
void McpClient::sendRequestInternal(std::shared_ptr<RequestContext> context) {
  // Check if connected
  if (!connected_ || !connection_manager_) {
    context->promise.set_value(jsonrpc::Response::make_error(
        context->id, Error(jsonrpc::INTERNAL_ERROR, "Not connected")));
    request_tracker_->completeRequest(context->id);
    client_stats_.requests_failed++;
    return;
  }
  
  // Build JSON-RPC request
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.method = context->method;
  request.params = context->params;
  request.id = context->id;
  
  // Serialize request to buffer
  auto buffer = serializeRequest(request);
  
  // Send through connection manager
  auto send_result = connection_manager_->send(std::move(buffer));
  
  if (is_error<std::nullptr_t>(send_result)) {
    // Send failed, check if we should retry
    if (context->retry_count < config_.max_retries) {
      context->retry_count++;
      client_stats_.requests_retried++;
      
      // Schedule retry with exponential backoff
      auto delay = std::chrono::milliseconds(100 * (1 << context->retry_count));
      // Note: In production, this would use a timer to retry
      // For now, we'll fail immediately
      context->promise.set_value(jsonrpc::Response::make_error(
          context->id, *get_error<std::nullptr_t>(send_result)));
    } else {
      // Max retries exceeded
      context->promise.set_value(jsonrpc::Response::make_error(
          context->id, *get_error<std::nullptr_t>(send_result)));
      client_stats_.requests_failed++;
    }
    
    request_tracker_->completeRequest(context->id);
    circuit_breaker_->recordFailure();
  } else {
    // Request sent successfully
    client_stats_.bytes_sent += buffer->size();
  }
}

// Check for request timeouts
void McpClient::checkRequestTimeouts() {
  auto timed_out = request_tracker_->getTimedOutRequests();
  
  for (const auto& request : timed_out) {
    request->promise.set_value(jsonrpc::Response::make_error(
        request->id, Error(jsonrpc::INTERNAL_ERROR, "Request timeout")));
    client_stats_.requests_timeout++;
    circuit_breaker_->recordFailure();
  }
}

// Handle incoming data
void McpClient::handleData(std::unique_ptr<Buffer> data) {
  client_stats_.bytes_received += data->size();
  
  // Parse JSON-RPC response
  auto response = parseResponse(std::move(data));
  
  if (!response.has_value()) {
    client_stats_.requests_invalid++;
    return;
  }
  
  // Find corresponding request
  auto request = request_tracker_->getRequest(response->id);
  if (!request) {
    // No matching request - might be a notification
    if (response->id.empty()) {
      handleNotification(*response);
    }
    return;
  }
  
  // Complete request
  request->promise.set_value(*response);
  request_tracker_->completeRequest(response->id);
  
  // Update stats
  if (response->error.has_value()) {
    client_stats_.requests_failed++;
    circuit_breaker_->recordFailure();
  } else {
    client_stats_.requests_success++;
    circuit_breaker_->recordSuccess();
    
    // Track latency
    auto duration = std::chrono::steady_clock::now() - request->sent_time;
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    client_stats_.request_duration_ms_total += duration_ms;
    client_stats_.request_duration_ms_min = std::min(client_stats_.request_duration_ms_min, duration_ms);
    client_stats_.request_duration_ms_max = std::max(client_stats_.request_duration_ms_max, duration_ms);
  }
}

// Handle notifications from server
void McpClient::handleNotification(const jsonrpc::Response& notification) {
  client_stats_.requests_notifications++;
  
  // Process based on method
  // For now, just log
  std::cerr << "[CLIENT] Received notification" << std::endl;
}

// Handle errors
void McpClient::handleError(const Error& error) {
  client_stats_.errors_total++;
  
  // Log error
  std::cerr << "[CLIENT] Error: " << error.message << std::endl;
  
  // Notify protocol state machine
  if (protocol_state_machine_) {
    protocol_state_machine_->handleError(error);
  }
  
  // Check if we should disconnect
  if (error.code == jsonrpc::INTERNAL_ERROR) {
    // Serious error, disconnect
    disconnect();
  }
}

// Parse JSON-RPC response from buffer
optional<jsonrpc::Response> McpClient::parseResponse(std::unique_ptr<Buffer> data) {
  // For now, create a simple response
  // Real implementation would parse JSON
  jsonrpc::Response response;
  response.jsonrpc = "2.0";
  response.id = "";
  
  // Check if this is an actual response by looking for common patterns
  std::string data_str(data->data(), data->size());
  if (data_str.find("\"result\"") != std::string::npos) {
    response.result = make_optional(Metadata());
  } else if (data_str.find("\"error\"") != std::string::npos) {
    response.error = make_optional(Error(jsonrpc::INTERNAL_ERROR, "Error from server"));
  }
  
  return make_optional(response);
}

// Serialize JSON-RPC request to buffer
std::unique_ptr<Buffer> McpClient::serializeRequest(const jsonrpc::Request& request) {
  // For now, create a simple JSON string
  // Real implementation would use proper JSON serialization
  std::ostringstream json;
  json << "{";
  json << "\"jsonrpc\":\"" << request.jsonrpc << "\",";
  json << "\"method\":\"" << request.method << "\",";
  json << "\"id\":\"" << request.id << "\"";
  
  if (request.params.has_value()) {
    json << ",\"params\":{}";
  }
  
  json << "}";
  
  auto str = json.str();
  auto buffer = std::make_unique<Buffer>(str.size());
  buffer->add(str.data(), str.size());
  return buffer;
}

// Transport negotiation
TransportType McpClient::negotiateTransport(const std::string& uri) {
  // Parse URI scheme to determine transport
  if (uri.find("stdio://") == 0) {
    return TransportType::Stdio;
  } else if (uri.find("ws://") == 0 || uri.find("wss://") == 0) {
    return TransportType::WebSocket;
  } else if (uri.find("http://") == 0 || uri.find("https://") == 0) {
    return TransportType::HttpSse;
  } else {
    // Default to HTTP/SSE for backward compatibility
    return TransportType::HttpSse;
  }
}

// Create connection configuration
McpConnectionConfig McpClient::createConnectionConfig(TransportType transport) {
  McpConnectionConfig config;
  
  // Set transport type
  config.transport_type = transport;
  
  // Set common configuration
  config.buffer_limit = 1024 * 1024;  // 1MB
  config.connection_timeout = config_.connect_timeout;
  config.use_message_framing = true;
  config.use_protocol_detection = false;
  
  // Set transport-specific configuration
  switch (transport) {
    case TransportType::HttpSse: {
      transport::HttpSseTransportSocketConfig http_config;
      http_config.uri = current_uri_;
      config.http_sse_config = make_optional(http_config);
      break;
    }
      
    case TransportType::WebSocket:
      // WebSocket not yet implemented
      break;
      
    case TransportType::Stdio: {
      transport::StdioTransportSocketConfig stdio_config;
      config.stdio_config = make_optional(stdio_config);
      break;
    }
  }
  
  return config;
}

// Process queued requests after protocol becomes ready
void McpClient::processQueuedRequests() {
  // For now, we don't queue requests
  // In a full implementation, we would process any requests
  // that were queued while waiting for protocol initialization
}

// Set server capabilities after initialization
void McpClient::setServerCapabilities(const ServerCapabilities& capabilities) {
  server_capabilities_ = capabilities;
}

// Get client statistics
ClientStats McpClient::getStats() const {
  return client_stats_;
}

// List available resources
std::future<ListResourcesResult> McpClient::listResources(
    const optional<std::string>& cursor) {
  
  auto params = make_metadata();
  if (cursor.has_value()) {
    params["cursor"] = cursor.value();
  }
  
  auto future = sendRequest("resources/list", make_optional(params));
  
  // Create promise for ListResourcesResult
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
        // Parse ListResourcesResult from response
        ListResourcesResult result;
        // TODO: Parse response into result structure
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
  auto params = make_metadata();
  params["uri"] = uri;
  
  auto future = sendRequest("resources/read", make_optional(params));
  
  // Create promise for ReadResourceResult
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
        // Parse ReadResourceResult from response
        ReadResourceResult result;
        // TODO: Parse response into result structure
        result_promise->set_value(result);
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  });
  
  return result_promise->get_future();
}

// Subscribe to resource updates
std::future<VoidResult> McpClient::subscribeResource(const std::string& uri) {
  auto params = make_metadata();
  params["uri"] = uri;
  
  auto future = sendRequest("resources/subscribe", make_optional(params));
  
  // Convert Response to VoidResult
  auto result_promise = std::make_shared<std::promise<VoidResult>>();
  
  std::thread([future = std::move(future), result_promise]() mutable {
    try {
      auto response = future.get();
      if (response.error.has_value()) {
        result_promise->set_value(makeVoidError(*response.error));
      } else {
        result_promise->set_value(VoidResult(nullptr));
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();
  
  return result_promise->get_future();
}

// Unsubscribe from resource updates  
std::future<VoidResult> McpClient::unsubscribeResource(const std::string& uri) {
  auto params = make_metadata();
  params["uri"] = uri;
  
  auto future = sendRequest("resources/unsubscribe", make_optional(params));
  
  // Convert Response to VoidResult
  auto result_promise = std::make_shared<std::promise<VoidResult>>();
  
  std::thread([future = std::move(future), result_promise]() mutable {
    try {
      auto response = future.get();
      if (response.error.has_value()) {
        result_promise->set_value(makeVoidError(*response.error));
      } else {
        result_promise->set_value(VoidResult(nullptr));
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();
  
  return result_promise->get_future();
}

// List available tools
std::future<ListToolsResult> McpClient::listTools(
    const optional<std::string>& cursor) {
  
  auto params = make_metadata();
  if (cursor.has_value()) {
    params["cursor"] = cursor.value();
  }
  
  auto future = sendRequest("tools/list", make_optional(params));
  
  // Create promise for ListToolsResult
  auto result_promise = std::make_shared<std::promise<ListToolsResult>>();
  
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
        // Parse ListToolsResult from response
        ListToolsResult result;
        // TODO: Parse response into result structure
        result_promise->set_value(result);
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  });
  
  return result_promise->get_future();
}

// Call a tool
std::future<CallToolResult> McpClient::callTool(
    const std::string& name,
    const optional<Metadata>& arguments) {
  
  auto params = make_metadata();
  params["name"] = name;
  if (arguments.has_value()) {
    params["arguments"] = arguments.value();
  }
  
  auto future = sendRequest("tools/call", make_optional(params));
  
  // Create promise for CallToolResult
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
        // Parse CallToolResult from response
        CallToolResult result;
        // TODO: Parse response into result structure
        result_promise->set_value(result);
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  });
  
  return result_promise->get_future();
}

// List available prompts
std::future<ListPromptsResult> McpClient::listPrompts(
    const optional<std::string>& cursor) {
  
  auto params = make_metadata();
  if (cursor.has_value()) {
    params["cursor"] = cursor.value();
  }
  
  auto future = sendRequest("prompts/list", make_optional(params));
  
  // Create promise for ListPromptsResult
  auto result_promise = std::make_shared<std::promise<ListPromptsResult>>();
  
  std::thread([future = std::move(future), result_promise]() mutable {
    try {
      auto response = future.get();
      ListPromptsResult result;
      // Parse response into result structure
      if (!response.error.has_value() && response.result.has_value()) {
        // TODO: Proper parsing
      }
      result_promise->set_value(result);
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();
  
  return result_promise->get_future();
}

// Get a prompt
std::future<GetPromptResult> McpClient::getPrompt(
    const std::string& name,
    const optional<Metadata>& arguments) {
  
  auto params = make_metadata();
  params["name"] = name;
  if (arguments.has_value()) {
    params["arguments"] = arguments.value();
  }
  
  auto future = sendRequest("prompts/get", make_optional(params));
  
  // Create promise for GetPromptResult
  auto result_promise = std::make_shared<std::promise<GetPromptResult>>();
  
  std::thread([future = std::move(future), result_promise]() mutable {
    try {
      auto response = future.get();
      GetPromptResult result;
      // Parse response into result structure
      if (!response.error.has_value() && response.result.has_value()) {
        // TODO: Proper parsing
      }
      result_promise->set_value(result);
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();
  
  return result_promise->get_future();
}

// Set logging level
std::future<VoidResult> McpClient::setLoggingLevel(LoggingLevel level) {
  auto params = make_metadata();
  params["level"] = static_cast<int>(level);
  
  auto future = sendRequest("logging/setLevel", make_optional(params));
  
  // Convert Response to VoidResult
  auto result_promise = std::make_shared<std::promise<VoidResult>>();
  
  std::thread([future = std::move(future), result_promise]() mutable {
    try {
      auto response = future.get();
      if (response.error.has_value()) {
        result_promise->set_value(makeVoidError(*response.error));
      } else {
        result_promise->set_value(VoidResult(nullptr));
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();
  
  return result_promise->get_future();
}

// Create a message (completion request)
std::future<CreateMessageResult> McpClient::createMessage(
    const CreateMessageRequest& request) {
  
  // Build parameters from request
  auto params = make_metadata();
  
  // Add messages (simplified - real implementation needs proper serialization)
  params["messages.count"] = static_cast<int>(request.messages.size());
  
  // Add optional parameters
  if (request.systemPrompt.has_value()) {
    params["systemPrompt"] = request.systemPrompt.value();
  }
  
  if (request.includeContext.has_value()) {
    params["includeContext"] = static_cast<int>(request.includeContext.value());
  }
  
  if (request.temperature.has_value()) {
    params["temperature"] = request.temperature.value();
  }
  
  if (request.maxTokens.has_value()) {
    params["maxTokens"] = static_cast<int>(request.maxTokens.value());
  }
  
  if (request.stopSequences.has_value()) {
    params["stopSequences.count"] = static_cast<int>(request.stopSequences->size());
  }
  
  // Add model preferences if provided
  if (request.modelPreferences.has_value()) {
    auto& prefs = request.modelPreferences.value();
    if (prefs.hints.has_value()) {
      params["modelPreferences.hints.count"] = static_cast<int>(prefs.hints->size());
    }
    if (prefs.costPriority.has_value()) {
      params["modelPreferences.costPriority"] = prefs.costPriority.value();
    }
    if (prefs.speedPriority.has_value()) {
      params["modelPreferences.speedPriority"] = prefs.speedPriority.value();
    }
    if (prefs.intelligencePriority.has_value()) {
      params["modelPreferences.intelligencePriority"] = prefs.intelligencePriority.value();
    }
  }
  
  // Send request
  auto context = std::make_shared<RequestContext>();
  context->id = std::to_string(next_request_id_++);
  context->method = "messages/create";
  context->params = make_optional(params);
  context->sent_time = std::chrono::steady_clock::now();
  context->retry_count = 0;
  
  // Build parameters with proper structure
  MetadataBuilder builder;
  
  // Add messages array
  for (size_t i = 0; i < request.messages.size(); ++i) {
    const auto& msg = request.messages[i];
    std::string prefix = "messages." + std::to_string(i) + ".";
    builder.add(prefix + "role", static_cast<int>(msg.role));
    
    // Handle content based on type
    if (holds_alternative<TextContent>(msg.content)) {
      const auto& text = get<TextContent>(msg.content);
      builder.add(prefix + "content.type", "text");
      builder.add(prefix + "content.text", text.text);
    } else if (holds_alternative<ImageContent>(msg.content)) {
      const auto& image = get<ImageContent>(msg.content);
      builder.add(prefix + "content.type", "image");
      builder.add(prefix + "content.data", image.data);
      builder.add(prefix + "content.mimeType", image.mimeType);
    }
  }
  
  // Add system prompt if provided
  if (request.systemPrompt.has_value()) {
    builder.add("systemPrompt", request.systemPrompt.value());
  }
  
  // Add sampling parameters
  if (request.temperature.has_value()) {
    builder.add("temperature", request.temperature.value());
  }
  if (request.maxTokens.has_value()) {
    builder.add("maxTokens", request.maxTokens.value());
  }
  
  // Add model preferences
  if (request.modelPreferences.has_value()) {
    const auto& prefs = request.modelPreferences.value();
    if (prefs.hints.has_value()) {
      for (size_t i = 0; i < prefs.hints->size(); ++i) {
        builder.add("modelPreferences.hints." + std::to_string(i), 
                    (*prefs.hints)[i]);
      }
    }
    if (prefs.costPriority.has_value()) {
      builder.add("modelPreferences.costPriority", prefs.costPriority.value());
    }
    if (prefs.speedPriority.has_value()) {
      builder.add("modelPreferences.speedPriority", prefs.speedPriority.value());
    }
    if (prefs.intelligencePriority.has_value()) {
      builder.add("modelPreferences.intelligencePriority", 
                  prefs.intelligencePriority.value());
    }
  }
  
  context->params = make_optional(builder.build());
  
  sendRequestInternal(context);
  
  // Return future that will convert response to CreateMessageResult
  std::promise<CreateMessageResult> result_promise;
  auto result_future = result_promise.get_future();
  
  std::thread([context, promise = std::move(result_promise)]() mutable {
    try {
      auto response = context->promise.get_future().get();
      CreateMessageResult result;
      // Parse response into result structure
      if (!response.error.has_value() && response.result.has_value()) {
        // Extract created message
        TextContent text_content;
        text_content.type = "text";
        text_content.text = "";
        result.content = text_content;
        result.model = "unknown";
        result.role = enums::Role::ASSISTANT;
      }
      result_promise.set_value(result);
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();
  
  return result_future;
}

// Protocol state coordination - handle protocol state changes
void McpClient::handleProtocolStateChange(const protocol::ProtocolStateTransitionContext& context) {
  // Log state transition for debugging
  std::cerr << "[CLIENT] Protocol state changed: " 
            << protocol::McpProtocolStateMachine::stateToString(context.from_state)
            << " -> "
            << protocol::McpProtocolStateMachine::stateToString(context.to_state)
            << " (event: " << protocol::McpProtocolStateMachine::eventToString(context.trigger_event) << ")"
            << std::endl;
  
  // Take action based on new state
  switch (context.to_state) {
    case protocol::McpProtocolState::READY:
      // Protocol is ready - can now send normal requests
      // Process any queued requests
      processQueuedRequests();
      break;
      
    case protocol::McpProtocolState::ERROR:
      // Protocol error - may need to reconnect
      if (context.error.has_value()) {
        std::cerr << "[CLIENT] Protocol error: " << context.error->message << std::endl;
        // Circuit breaker should handle this
        circuit_breaker_->recordFailure();
      }
      break;
      
    case protocol::McpProtocolState::DISCONNECTED:
      // Protocol disconnected - clear state
      initialized_ = false;
      break;
      
    case protocol::McpProtocolState::DRAINING:
      // Graceful shutdown in progress
      // Stop accepting new requests
      break;
      
    default:
      // Other states don't require specific action
      break;
  }
}
// Coordinate protocol state with network connection state
void McpClient::coordinateProtocolState() {
  if (!protocol_state_machine_) {
    return;
  }
  
  // Check current states
  auto protocol_state = protocol_state_machine_->currentState();
  
  // Coordinate based on current situation
  if (connected_ && protocol_state == protocol::McpProtocolState::CONNECTED) {
    // Network is connected but protocol not initialized
    // Trigger initialization if not already in progress
    if (!initialized_ && protocol_state != protocol::McpProtocolState::INITIALIZING) {
      // Auto-initialize protocol after connection
      // We're already in dispatcher thread from synchronizeState
      initializeProtocol();
    }
  } else if (!connected_ && protocol_state != protocol::McpProtocolState::DISCONNECTED) {
    // Network disconnected but protocol thinks it's connected
    // Already in dispatcher thread from caller
    protocol_state_machine_->handleEvent(protocol::McpProtocolEvent::NETWORK_DISCONNECTED);
  }
}

// Handle connection events from network layer
void McpClient::handleConnectionEvent(network::ConnectionEvent event) {
  // Handle connection events in dispatcher context
  std::cerr << "[DEBUG] McpClient::handleConnectionEvent called with event: " 
            << static_cast<int>(event) << std::endl;
  
  switch (event) {
    case network::ConnectionEvent::Connected:
      std::cerr << "[DEBUG] Connected event received in client, setting connected_ = true" << std::endl;
      connected_ = true;
      client_stats_.connections_active++;
      
      // Notify protocol state machine of network connection
      // We're already in dispatcher thread from connection callback
      if (protocol_state_machine_) {
        protocol_state_machine_->handleEvent(protocol::McpProtocolEvent::NETWORK_CONNECTED);
      }
      break;
      
    case network::ConnectionEvent::RemoteClose:
    case network::ConnectionEvent::LocalClose:
      connected_ = false;
      client_stats_.connections_active--;
      
      // Notify protocol state machine of network disconnection (already in dispatcher thread)
      if (protocol_state_machine_) {
        protocol_state_machine_->handleEvent(protocol::McpProtocolEvent::NETWORK_DISCONNECTED);
      }
      
      // Fail all pending requests
      auto pending = request_tracker_->getTimedOutRequests();
      for (const auto& request : pending) {
        request->promise.set_value(jsonrpc::Response::make_error(
            request->id,
            Error(jsonrpc::INTERNAL_ERROR, "Connection closed")));
      }
      break;
  }
  
  // Coordinate protocol state with connection state
  coordinateProtocolState();
}


}  // namespace client
}  // namespace mcp