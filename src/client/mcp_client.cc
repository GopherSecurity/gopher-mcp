#include "mcp/client/mcp_client.h"

#include <algorithm>
#include <future>
#include <sstream>
#include <thread>

#include "mcp/event/libevent_dispatcher.h"
#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"
#include "mcp/mcp_application_base.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/socket_interface_impl.h"

namespace mcp {
namespace client {

using namespace mcp::network;
using namespace mcp::event;
using namespace mcp::application;

// Import specific types
using mcp::Buffer;
using mcp::CallToolResult;
using mcp::CreateMessageRequest;
using mcp::CreateMessageResult;
using mcp::Error;
using mcp::get;
using mcp::get_error;
using mcp::GetPromptResult;
using mcp::holds_alternative;
using mcp::ImageContent;
using mcp::Implementation;
using mcp::InitializeResult;
using mcp::is_error;
using mcp::ListPromptsResult;
using mcp::ListResourcesResult;
using mcp::ListToolsResult;
using mcp::make_optional;
using mcp::makeVoidError;
using mcp::Metadata;
using mcp::MetadataBuilder;
using mcp::nullopt;
using mcp::optional;
using mcp::ReadResourceResult;
using mcp::RequestId;
using mcp::ServerCapabilities;
using mcp::TextContent;
using mcp::variant;
using mcp::VoidResult;
using mcp::jsonrpc::Notification;
using mcp::jsonrpc::Request;
using mcp::jsonrpc::Response;

namespace jsonrpc = mcp::jsonrpc;

// Constructor
McpClient::McpClient(const McpClientConfig& config)
    : ApplicationBase(config), config_(config) {
  // Set callbacks for protocol state changes
  protocol::McpProtocolStateMachineConfig protocol_config;
  protocol_config.initialization_timeout =
      config_.protocol_initialization_timeout;
  protocol_config.connection_timeout = config_.protocol_connection_timeout;
  protocol_config.drain_timeout = config_.protocol_drain_timeout;
  protocol_config.auto_reconnect = config_.protocol_auto_reconnect;
  protocol_config.max_reconnect_attempts =
      config_.protocol_max_reconnect_attempts;
  protocol_config.reconnect_delay = config_.protocol_reconnect_delay;

  // Initialize request tracker
  request_tracker_ = std::make_unique<RequestTracker>(config_.request_timeout);

  // Initialize circuit breaker
  circuit_breaker_ = std::make_unique<CircuitBreaker>(
      config_.circuit_breaker_threshold, config_.circuit_breaker_timeout,
      0.5);  // 50% error rate threshold

  // Initialize protocol callbacks
  protocol_callbacks_ = std::make_unique<ProtocolCallbacksImpl>(*this);

  // Set callbacks for protocol state changes
  protocol_config.state_change_callback =
      [this](const protocol::ProtocolStateTransitionContext& ctx) {
        handleProtocolStateChange(ctx);
      };

  protocol_config.error_callback = [this](const Error& error) {
    handleError(error);
  };

  // Protocol state machine will be created in dispatcher thread during
  // initialization
}
// Destructor
McpClient::~McpClient() { shutdown(); }

// Connect to server
VoidResult McpClient::connect(const std::string& uri) {
  // Check if already shutting down
  if (shutting_down_) {
    return makeVoidError(
        Error(::mcp::jsonrpc::INTERNAL_ERROR, "Client is shutting down"));
  }

  // Check if already connected
  if (connected_) {
    return makeVoidError(
        Error(::mcp::jsonrpc::INVALID_REQUEST, "Already connected"));
  }

  // Create main dispatcher
  main_dispatcher_ = new LibeventDispatcher("client");

  // Start dispatcher in a separate thread
  // Store thread handle for proper cleanup (reference pattern)
  dispatcher_thread_ =
      std::thread([this]() { main_dispatcher_->run(RunType::Block); });

  // Give dispatcher thread time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Get socket interface after dispatcher is created
  socket_interface_ = std::make_unique<SocketInterfaceImpl>();

  // Create connect promise
  auto connect_promise = std::make_shared<std::promise<VoidResult>>();
  auto connect_future = connect_promise->get_future();

  main_dispatcher_->post([this, uri, connect_promise]() {
    try {
      // Initialize protocol state machine if not already created
      if (!protocol_state_machine_) {
        protocol::McpProtocolStateMachineConfig protocol_config;
        protocol_config.initialization_timeout =
            config_.protocol_initialization_timeout;
        protocol_config.connection_timeout =
            config_.protocol_connection_timeout;
        protocol_config.drain_timeout = config_.protocol_drain_timeout;
        protocol_config.auto_reconnect = config_.protocol_auto_reconnect;
        protocol_config.max_reconnect_attempts =
            config_.protocol_max_reconnect_attempts;
        protocol_config.reconnect_delay = config_.protocol_reconnect_delay;

        protocol_config.state_change_callback =
            [this](const protocol::ProtocolStateTransitionContext& ctx) {
              handleProtocolStateChange(ctx);
            };

        protocol_config.error_callback = [this](const Error& error) {
          handleError(error);
        };

        protocol_state_machine_ =
            std::make_unique<protocol::McpProtocolStateMachine>(
                *main_dispatcher_, protocol_config);
      }

      // Trigger protocol connection state
      // We're already in dispatcher thread from the outer post() at line 142
      if (protocol_state_machine_) {
        protocol_state_machine_->handleEvent(
            protocol::McpProtocolEvent::CONNECT_REQUESTED);
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
          *main_dispatcher_, *socket_interface_, conn_config);

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
        last_activity_time_ = std::chrono::steady_clock::now();
        connect_promise->set_value(VoidResult(nullptr));
      }
    } catch (const std::exception& e) {
      connect_promise->set_value(
          makeVoidError(Error(::mcp::jsonrpc::INTERNAL_ERROR, e.what())));
    }
  });

  // Wait for connection to be established
  auto status = connect_future.wait_for(std::chrono::seconds(10));
  if (status == std::future_status::timeout) {
    return makeVoidError(
        Error(::mcp::jsonrpc::INTERNAL_ERROR, "Connection timeout"));
  }

  return connect_future.get();
}

// Disconnect from server
void McpClient::disconnect() {
  // Don't create timers if we're shutting down
  if (shutting_down_) {
    return;
  }
  
  // Check if we're in dispatcher thread or post to it
  if (main_dispatcher_ && !main_dispatcher_->isThreadSafe()) {
    // We're not in dispatcher thread, post the disconnect
    main_dispatcher_->post([this]() {
      if (protocol_state_machine_ && !shutting_down_) {
        protocol_state_machine_->handleEvent(
            protocol::McpProtocolEvent::SHUTDOWN_REQUESTED);
      }
    });
    return;
  }
  
  // We're in dispatcher thread or no dispatcher, proceed directly
  if (protocol_state_machine_) {
    protocol_state_machine_->handleEvent(
        protocol::McpProtocolEvent::SHUTDOWN_REQUESTED);
  }

  // Close connection
  if (connection_manager_) {
    connection_manager_->close();
  }

  // Reset state
  connected_ = false;
  initialized_ = false;
}

// Check if the underlying connection is actually open
bool McpClient::isConnectionOpen() const {
  if (!connected_ || !connection_manager_) {
    return false;
  }
  return connection_manager_->isConnected();
}

// Reconnect using stored URI
VoidResult McpClient::reconnect() {
  if (current_uri_.empty()) {
    return makeVoidError(
        Error(::mcp::jsonrpc::INTERNAL_ERROR, "No URI stored for reconnection"));
  }

  // Disconnect first if we think we're connected
  if (connected_ || connection_manager_) {
    // Close the old connection
    if (connection_manager_) {
      connection_manager_->close();
      connection_manager_.reset();
    }
    connected_ = false;
    initialized_ = false;
  }

  // Now reconnect - reuse existing dispatcher if available
  if (!main_dispatcher_) {
    return makeVoidError(
        Error(::mcp::jsonrpc::INTERNAL_ERROR, "No dispatcher available for reconnection"));
  }

  // Do reconnection work directly (not via dispatcher post)
  // This avoids deadlock when called from the dispatcher thread
  try {
    // Negotiate transport based on URI scheme
    TransportType transport = negotiateTransport(current_uri_);

    // Create connection configuration
    McpConnectionConfig conn_config = createConnectionConfig(transport);

    // Create new connection manager
    connection_manager_ = std::make_unique<McpConnectionManager>(
        *main_dispatcher_, *socket_interface_, conn_config);

    // Set message callback handler
    connection_manager_->setProtocolCallbacks(*protocol_callbacks_);

    // Initiate connection (asynchronous - doesn't wait for TCP handshake)
    VoidResult result = connection_manager_->connect();

    if (is_error<std::nullptr_t>(result)) {
      auto error = get_error<std::nullptr_t>(result);
      return makeVoidError(*error);
    }

    // The connection_manager_->connect() initiates the TCP connection asynchronously.
    // The dispatcher needs to process events for the connection to complete.
    // Since we might be ON the dispatcher thread, we can't block waiting.
    //
    // Simply mark that reconnection is in progress. The handleConnectionEvent
    // callback will set connected_=true when the TCP handshake completes.
    // We return success here - the connection will be ready shortly.
    last_activity_time_ = std::chrono::steady_clock::now();

    return makeSuccess<std::nullptr_t>(nullptr);
  } catch (const std::exception& e) {
    return makeVoidError(Error(::mcp::jsonrpc::INTERNAL_ERROR, e.what()));
  }
}

// Shutdown client
void McpClient::shutdown() {
  if (shutting_down_) {
    return;
  }
  shutting_down_ = true;

  // Close connection directly without triggering state machine
  if (connection_manager_) {
    if (main_dispatcher_ && !main_dispatcher_->isThreadSafe()) {
      // Post to dispatcher thread
      main_dispatcher_->post([this]() {
        if (connection_manager_) {
          connection_manager_->close();
        }
      });
    } else {
      connection_manager_->close();
    }
  }
  
  connected_ = false;

  // Request dispatcher shutdown
  shutdown_requested_ = true;

  // Notify dispatcher to exit
  if (main_dispatcher_) {
    main_dispatcher_->exit();
  }

  // Join dispatcher thread if it's joinable (reference pattern)
  if (dispatcher_thread_.joinable()) {
    dispatcher_thread_.join();
  }

  // Clean up dispatcher after thread has exited
  if (main_dispatcher_) {
    delete main_dispatcher_;
    main_dispatcher_ = nullptr;
  }

  // Clean up resources
  protocol_state_machine_.reset();
  connection_manager_.reset();
  request_tracker_.reset();
  circuit_breaker_.reset();

  // Client resources are cleaned up above
}

// Initialize protocol
std::future<InitializeResult> McpClient::initializeProtocol() {
  // Create promise for InitializeResult
  auto result_promise = std::make_shared<std::promise<InitializeResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // Defer all protocol operations to dispatcher thread
  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<jsonrpc::Response>>();

  // Step 1: Post to dispatcher to send the request (non-blocking)
  main_dispatcher_->post([this, request_future_ptr]() {
    // Notify protocol state machine that initialization is starting
    if (protocol_state_machine_) {
      protocol_state_machine_->handleEvent(
          protocol::McpProtocolEvent::INITIALIZE_REQUESTED);
    }

    // Build initialize request with client capabilities
    auto init_params = make_metadata();
    init_params["protocolVersion"] = config_.protocol_version;
    init_params["clientName"] = config_.client_name;
    init_params["clientVersion"] = config_.client_version;

    // Send request - do NOT block here!
    *request_future_ptr =
        sendRequest("initialize", mcp::make_optional(init_params));
    GOPHER_LOG_TRACE("initializeProtocol: request sent, callback returning");
    // Callback returns immediately - response will be processed elsewhere
  });

  // Step 2: Use std::async to wait for response on a worker thread (not
  // dispatcher!) Fire and forget - the async thread will set the promise when
  // done
  std::thread([this, result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent (the dispatcher callback to complete)
      // Then wait for the response - this blocks a worker thread, NOT the
      // dispatcher
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      GOPHER_LOG_TRACE(
          "initializeProtocol: waiting for response on worker thread");
      auto response = request_future_ptr->get();
      GOPHER_LOG_TRACE("initializeProtocol: got response");

      if (response.error.has_value()) {
        result_promise->set_exception(std::make_exception_ptr(
            std::runtime_error(response.error->message)));
      } else {
        // Parse InitializeResult from response
        InitializeResult init_result;

        // Parse the flattened response from server
        // The server returns a Metadata object with flattened fields
        if (holds_alternative<Metadata>(response.result.value())) {
          auto& metadata = get<Metadata>(response.result.value());

          // Extract protocol version
          auto proto_it = metadata.find("protocolVersion");
          if (proto_it != metadata.end() &&
              holds_alternative<std::string>(proto_it->second)) {
            init_result.protocolVersion = get<std::string>(proto_it->second);
          }

          // Extract server info
          auto name_it = metadata.find("serverInfo.name");
          auto version_it = metadata.find("serverInfo.version");
          if (name_it != metadata.end() && version_it != metadata.end()) {
            Implementation server_info(
                holds_alternative<std::string>(name_it->second)
                    ? get<std::string>(name_it->second)
                    : "",
                holds_alternative<std::string>(version_it->second)
                    ? get<std::string>(version_it->second)
                    : "");
            init_result.serverInfo = mcp::make_optional(server_info);
          }

          // Extract capabilities (simplified)
          ServerCapabilities caps;

          auto tools_it = metadata.find("capabilities.tools");
          if (tools_it != metadata.end() &&
              holds_alternative<bool>(tools_it->second)) {
            caps.tools = mcp::make_optional(get<bool>(tools_it->second));
          }

          auto prompts_it = metadata.find("capabilities.prompts");
          if (prompts_it != metadata.end() &&
              holds_alternative<bool>(prompts_it->second)) {
            caps.prompts = mcp::make_optional(get<bool>(prompts_it->second));
          }

          auto resources_it = metadata.find("capabilities.resources");
          if (resources_it != metadata.end() &&
              holds_alternative<bool>(resources_it->second)) {
            caps.resources =
                mcp::make_optional(variant<bool, ResourcesCapability>(
                    get<bool>(resources_it->second)));
          }

          auto logging_it = metadata.find("capabilities.logging");
          if (logging_it != metadata.end() &&
              holds_alternative<bool>(logging_it->second)) {
            caps.logging = mcp::make_optional(get<bool>(logging_it->second));
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
          protocol_state_machine_->handleEvent(
              protocol::McpProtocolEvent::INITIALIZED);
        }

        result_promise->set_value(init_result);
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();  // Detach the thread - it will set the promise when done

  return result_promise->get_future();
}

// Send request with future-based async API
std::future<Response> McpClient::sendRequest(const std::string& method,
                                             const optional<Metadata>& params) {
  // Check if circuit breaker allows request
  if (!circuit_breaker_->allowRequest()) {
    client_stats_.circuit_breaker_opens++;
    auto promise = std::make_shared<std::promise<Response>>();
    promise->set_value(Response::make_error(
        "", Error(::mcp::jsonrpc::INTERNAL_ERROR, "Circuit breaker open")));
    return promise->get_future();
  }

  // Generate request ID
  RequestId id = static_cast<int64_t>(next_request_id_++);

  // Create request context
  auto context = std::make_shared<RequestContext>(id, method);
  context->params = params;
  context->start_time = std::chrono::steady_clock::now();

  // Track request
  request_tracker_->trackRequest(context);
  // Track request sent

  // Send request through internal pathway
  sendRequestInternal(context);

  return context->promise.get_future();
}

// Send notification (fire-and-forget, no response expected)
VoidResult McpClient::sendNotification(const std::string& method,
                                       const optional<Metadata>& params) {
  // Check if connected
  if (!connected_ || !connection_manager_) {
    return makeError<std::nullptr_t>(
        Error(::mcp::jsonrpc::INTERNAL_ERROR, "Not connected"));
  }

  // Build JSON-RPC notification (no id field)
  Notification notification;
  notification.jsonrpc = "2.0";
  notification.method = method;
  notification.params = params;

  // Send through connection manager
  // Post to dispatcher thread to ensure thread safety
  main_dispatcher_->post([this, notification]() {
    if (connection_manager_) {
      connection_manager_->sendNotification(notification);
    }
  });

  return makeSuccess<std::nullptr_t>(nullptr);
}

// Send request internally with retry logic
void McpClient::sendRequestInternal(std::shared_ptr<RequestContext> context) {
  std::cerr << "[McpClient] sendRequestInternal: method=" << context->method
            << ", connected_=" << connected_
            << ", isConnectionOpen()=" << isConnectionOpen()
            << ", retry_count=" << context->retry_count << std::endl;

  // Check if connection is stale (idle for too long)
  auto now = std::chrono::steady_clock::now();
  auto idle_seconds = std::chrono::duration_cast<std::chrono::seconds>(
      now - last_activity_time_).count();
  bool is_stale = connected_ && (idle_seconds >= kConnectionIdleTimeoutSec);

  std::cerr << "[McpClient] sendRequestInternal stale check: idle_seconds=" << idle_seconds
            << ", timeout=" << kConnectionIdleTimeoutSec
            << ", is_stale=" << is_stale << std::endl;

  // Check if connection is stale or not open - need to reconnect
  // Maximum retries to wait for connection after reconnect (50 * 10ms = 500ms max)
  static constexpr int kMaxReconnectRetries = 50;

  if (is_stale || !isConnectionOpen()) {
    // Track if this is a retry after reconnect
    if (context->retry_count > 0 && context->retry_count <= kMaxReconnectRetries) {
      // This is a retry - check if we're connected now
      if (!connected_ && !isConnectionOpen()) {
        // Still not connected, schedule another retry with timer delay
        // Timer allows event loop to process I/O events (like TCP connect) between retries
        context->retry_count++;
        context->retry_timer = main_dispatcher_->createTimer([this, context]() {
          sendRequestInternal(context);
        });
        context->retry_timer->enableTimer(std::chrono::milliseconds(10));
        return;
      }
      // Connected now, proceed with send below
    } else if (context->retry_count > kMaxReconnectRetries) {
      // Too many retries
      context->promise.set_value(Response::make_error(
          context->id, Error(::mcp::jsonrpc::INTERNAL_ERROR, "Connection not ready after reconnect")));
      request_tracker_->removeRequest(context->id);
      client_stats_.requests_failed++;
      return;
    } else {
      // First attempt - need to reconnect
      // Attempt to reconnect (async - just initiates connection)
      auto reconnect_result = reconnect();
      if (is_error<std::nullptr_t>(reconnect_result)) {
        context->promise.set_value(Response::make_error(
            context->id, Error(::mcp::jsonrpc::INTERNAL_ERROR, "Connection closed and reconnect failed")));
        request_tracker_->removeRequest(context->id);
        client_stats_.requests_failed++;
        return;
      }

      // Reconnect initiated - schedule retry to allow connection event to be processed
      context->retry_count = 1;
      main_dispatcher_->post([this, context]() {
        sendRequestInternal(context);
      });
      return;
    }
  }

  // Double-check connection after potential reconnect
  if (!connected_ || !connection_manager_) {
    context->promise.set_value(Response::make_error(
        context->id, Error(::mcp::jsonrpc::INTERNAL_ERROR, "Not connected")));
    request_tracker_->removeRequest(context->id);
    client_stats_.requests_failed++;
    return;
  }

  // Build JSON-RPC request
  Request request;
  request.jsonrpc = "2.0";
  request.method = context->method;
  request.params = context->params;
  request.id = context->id;

  std::cerr << "[McpClient] Sending request through connection_manager: method=" << context->method << std::endl;

  // Update activity time BEFORE sending - we're actively using the connection
  // This prevents stale connection detection while waiting for response
  last_activity_time_ = std::chrono::steady_clock::now();

  // Send through connection manager
  auto send_result = connection_manager_->sendRequest(request);

  std::cerr << "[McpClient] sendRequest result: is_error=" << is_error<std::nullptr_t>(send_result) << std::endl;

  if (is_error<std::nullptr_t>(send_result)) {
    // Send failed, check if we should retry
    if (context->retry_count < config_.max_retries) {
      context->retry_count++;
      client_stats_.requests_retried++;

      // Schedule retry with exponential backoff
      auto delay = std::chrono::milliseconds(100 * (1 << context->retry_count));
      // Note: In production, this would use a timer to retry
      // For now, we'll fail immediately
      context->promise.set_value(Response::make_error(
          context->id, *get_error<std::nullptr_t>(send_result)));
    } else {
      // Max retries exceeded
      context->promise.set_value(Response::make_error(
          context->id, *get_error<std::nullptr_t>(send_result)));
      client_stats_.requests_failed++;
    }

    request_tracker_->removeRequest(context->id);
    circuit_breaker_->recordFailure();
  } else {
    // Request sent successfully
    // Track bytes sent
  }
}

// Handle incoming response
void McpClient::handleResponse(const Response& response) {
  // Update last activity time - we received data from the server
  last_activity_time_ = std::chrono::steady_clock::now();

  // Find corresponding request
  auto request = request_tracker_->getRequest(response.id);
  if (!request) {
    // No matching request
    return;
  }

  // Complete request
  request->promise.set_value(response);
  request_tracker_->removeRequest(response.id);

  // Update stats
  if (response.error.has_value()) {
    client_stats_.requests_failed++;
    circuit_breaker_->recordFailure();
  } else {
    client_stats_.requests_success++;
    circuit_breaker_->recordSuccess();

    // Track latency
    auto duration = std::chrono::steady_clock::now() - request->start_time;
    auto duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    client_stats_.request_duration_ms_total += duration_ms;
    client_stats_.request_duration_ms_min =
        std::min(client_stats_.request_duration_ms_min.load(),
                 static_cast<uint64_t>(duration_ms));
    client_stats_.request_duration_ms_max =
        std::max(client_stats_.request_duration_ms_max.load(),
                 static_cast<uint64_t>(duration_ms));
  }
}

// Handle incoming request (server calling client)
void McpClient::handleRequest(const Request& request) {
  // Clients typically don't handle requests from server
  // But we may need to respond to certain protocol requests
  Response response = Response::make_error(
      request.id, Error(::mcp::jsonrpc::METHOD_NOT_FOUND,
                        "Client does not handle requests"));
  connection_manager_->sendResponse(response);
}

// Handle notifications from server
void McpClient::handleNotification(const Notification& notification) {
  // Process based on method
}

// Handle errors
void McpClient::handleError(const Error& error) {
  client_stats_.errors_total++;

  // Notify protocol state machine
  if (protocol_state_machine_) {
    protocol_state_machine_->handleError(error);
  }

  // Check if we should disconnect
  if (error.code == ::mcp::jsonrpc::INTERNAL_ERROR) {
    // Serious error, disconnect
    disconnect();
  }
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
  config.connection_timeout = config_.request_timeout;
  config.use_message_framing = true;
  config.use_protocol_detection = false;

  // Set transport-specific configuration
  switch (transport) {
    case TransportType::HttpSse: {
      transport::HttpSseTransportSocketConfig http_config;
      http_config.mode = transport::HttpSseTransportSocketConfig::Mode::CLIENT;

      // Extract server address from URI
      // URI format: http://host:port or https://host:port
      std::string server_addr;
      bool is_https = false;
      if (current_uri_.find("http://") == 0) {
        server_addr = current_uri_.substr(7);  // Remove "http://"
      } else if (current_uri_.find("https://") == 0) {
        server_addr = current_uri_.substr(8);  // Remove "https://"
        is_https = true;
      } else {
        server_addr = current_uri_;
      }

      // Extract path component (everything after first /)
      std::string http_path = "/";  // Default path
      size_t slash_pos = server_addr.find('/');
      if (slash_pos != std::string::npos) {
        http_path = server_addr.substr(slash_pos);  // Include the /
        server_addr = server_addr.substr(0, slash_pos);  // Remove path from address
      }

      http_config.server_address = server_addr;
      config.http_path = http_path;
      config.http_host = server_addr;  // Host header value (without port for now)

      // Set SSL transport for HTTPS URLs
      if (is_https) {
        http_config.underlying_transport =
            transport::HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
        // Configure SSL with reasonable defaults
        transport::HttpSseTransportSocketConfig::SslConfig ssl_cfg;
        ssl_cfg.verify_peer = false;  // For now, disable peer verification
        // Use HTTP/1.1 only - our HTTP codec doesn't support HTTP/2 binary framing
        ssl_cfg.alpn_protocols = std::vector<std::string>{"http/1.1"};

        // Extract hostname for SNI (Server Name Indication)
        // server_addr may contain port (host:port), extract just the hostname
        std::string sni_host = server_addr;
        size_t colon_pos = sni_host.find(':');
        if (colon_pos != std::string::npos) {
          sni_host = sni_host.substr(0, colon_pos);
        }
        ssl_cfg.sni_hostname = mcp::make_optional(sni_host);

        http_config.ssl_config = mcp::make_optional(ssl_cfg);
      }

      config.http_sse_config = mcp::make_optional(http_config);
      break;
    }

    case TransportType::WebSocket:
      // WebSocket not yet implemented
      break;

    case TransportType::Stdio: {
      transport::StdioTransportSocketConfig stdio_config;
      config.stdio_config = mcp::make_optional(stdio_config);
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

// List available resources
std::future<ListResourcesResult> McpClient::listResources(
    const optional<std::string>& cursor) {
  auto result_promise = std::make_shared<std::promise<ListResourcesResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Prepare params before posting to dispatcher
  auto params = make_metadata();
  if (cursor.has_value()) {
    params["cursor"] = cursor.value();
  }
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  // Step 1: Post to dispatcher to send the request (non-blocking)
  main_dispatcher_->post([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequest("resources/list", mcp::make_optional(*params_ptr));
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        result_promise->set_exception(std::make_exception_ptr(
            std::runtime_error(response.error->message)));
      } else if (response.result.has_value()) {
        // Extract ListResourcesResult from response
        // ResponseResult variant directly contains ListResourcesResult
        if (holds_alternative<ListResourcesResult>(response.result.value())) {
          result_promise->set_value(
              get<ListResourcesResult>(response.result.value()));
        } else {
          // Fallback: return empty result if type doesn't match
          result_promise->set_value(ListResourcesResult());
        }
      } else {
        result_promise->set_value(ListResourcesResult());
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

// Read resource content
std::future<ReadResourceResult> McpClient::readResource(
    const std::string& uri) {
  auto result_promise = std::make_shared<std::promise<ReadResourceResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Prepare params before posting to dispatcher
  auto params = make_metadata();
  params["uri"] = uri;
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  // Step 1: Post to dispatcher to send the request (non-blocking)
  main_dispatcher_->post([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequest("resources/read", mcp::make_optional(*params_ptr));
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        result_promise->set_exception(std::make_exception_ptr(
            std::runtime_error(response.error->message)));
      } else {
        // Parse ReadResourceResult from response
        ReadResourceResult result;
        // TODO: Parse response into result structure
        result_promise->set_value(result);
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

// Subscribe to resource updates
std::future<VoidResult> McpClient::subscribeResource(const std::string& uri) {
  auto result_promise = std::make_shared<std::promise<VoidResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Prepare params before posting to dispatcher
  auto params = make_metadata();
  params["uri"] = uri;
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  // Step 1: Post to dispatcher to send the request (non-blocking)
  main_dispatcher_->post([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequest("resources/subscribe", mcp::make_optional(*params_ptr));
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
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
  auto result_promise = std::make_shared<std::promise<VoidResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Prepare params before posting to dispatcher
  auto params = make_metadata();
  params["uri"] = uri;
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  // Step 1: Post to dispatcher to send the request (non-blocking)
  main_dispatcher_->post([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequest("resources/unsubscribe", mcp::make_optional(*params_ptr));
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
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
  auto result_promise = std::make_shared<std::promise<ListToolsResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Prepare params before posting to dispatcher
  auto params = make_metadata();
  if (cursor.has_value()) {
    params["cursor"] = cursor.value();
  }
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  // Step 1: Post to dispatcher to send the request (non-blocking)
  main_dispatcher_->post([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequest("tools/list", mcp::make_optional(*params_ptr));
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        result_promise->set_exception(std::make_exception_ptr(
            std::runtime_error(response.error->message)));
      } else if (response.result.has_value()) {
        // Extract tools from response
        // The response.result contains ListToolsResult
        ListToolsResult result;
        if (holds_alternative<ListToolsResult>(response.result.value())) {
          result = get<ListToolsResult>(response.result.value());
        } else if (holds_alternative<std::vector<Tool>>(response.result.value())) {
          // Backward compatibility: if it's a vector of tools directly
          result.tools = get<std::vector<Tool>>(response.result.value());
        }
        result_promise->set_value(result);
      } else {
        result_promise->set_value(ListToolsResult());
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

// Call a tool
std::future<CallToolResult> McpClient::callTool(
    const std::string& name, const optional<Metadata>& arguments) {
  auto result_promise = std::make_shared<std::promise<CallToolResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Prepare params before posting to dispatcher
  auto params = make_metadata();
  params["name"] = name;
  if (arguments.has_value()) {
    // Convert arguments to JSON string for nested object support
    // Server expects "arguments" as a nested JSON object which is stored
    // as a JSON string in Metadata since MetadataValue doesn't support nesting
    auto args_json = json::metadataToJson(arguments.value());
    params["arguments"] = args_json.toString();
  }
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  // Step 1: Post to dispatcher to send the request (non-blocking)
  main_dispatcher_->post([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequest("tools/call", mcp::make_optional(*params_ptr));
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        result_promise->set_exception(std::make_exception_ptr(
            std::runtime_error(response.error->message)));
      } else if (response.result.has_value()) {
        // Extract CallToolResult from response
        // Server returns Metadata with "content" (string) and "isError" (bool)
        CallToolResult result;
        if (holds_alternative<Metadata>(response.result.value())) {
          auto metadata = get<Metadata>(response.result.value());
          // Extract content string and convert to TextContent
          auto content_it = metadata.find("content");
          if (content_it != metadata.end() &&
              holds_alternative<std::string>(content_it->second)) {
            result.content.push_back(ExtendedContentBlock(
                TextContent(get<std::string>(content_it->second))));
          }
          // Extract isError flag
          auto error_it = metadata.find("isError");
          if (error_it != metadata.end() &&
              holds_alternative<bool>(error_it->second)) {
            result.isError = get<bool>(error_it->second);
          }
        }
        result_promise->set_value(result);
      } else {
        result_promise->set_value(CallToolResult());
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

// List available prompts
std::future<ListPromptsResult> McpClient::listPrompts(
    const optional<std::string>& cursor) {
  auto result_promise = std::make_shared<std::promise<ListPromptsResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Prepare params before posting to dispatcher
  auto params = make_metadata();
  if (cursor.has_value()) {
    params["cursor"] = cursor.value();
  }
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  // Step 1: Post to dispatcher to send the request (non-blocking)
  main_dispatcher_->post([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequest("prompts/list", mcp::make_optional(*params_ptr));
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        result_promise->set_exception(std::make_exception_ptr(
            std::runtime_error(response.error->message)));
      } else if (response.result.has_value()) {
        // Extract prompts vector from response and wrap in ListPromptsResult
        // ResponseResult variant contains std::vector<Prompt>
        ListPromptsResult result;
        if (holds_alternative<std::vector<Prompt>>(response.result.value())) {
          result.prompts = get<std::vector<Prompt>>(response.result.value());
        }
        result_promise->set_value(result);
      } else {
        result_promise->set_value(ListPromptsResult());
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

// Get a prompt
std::future<GetPromptResult> McpClient::getPrompt(
    const std::string& name, const optional<Metadata>& arguments) {
  auto result_promise = std::make_shared<std::promise<GetPromptResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Prepare params before posting to dispatcher
  auto params = make_metadata();
  params["name"] = name;
  if (arguments.has_value()) {
    // Convert arguments to JSON string for nested object support
    // Server expects "arguments" as a nested JSON object which is stored
    // as a JSON string in Metadata since MetadataValue doesn't support nesting
    auto args_json = json::metadataToJson(arguments.value());
    params["arguments"] = args_json.toString();
  }
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  // Step 1: Post to dispatcher to send the request (non-blocking)
  main_dispatcher_->post([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequest("prompts/get", mcp::make_optional(*params_ptr));
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        result_promise->set_exception(std::make_exception_ptr(
            std::runtime_error(response.error->message)));
      } else if (response.result.has_value()) {
        // Extract GetPromptResult from response
        // Server serializes GetPromptResult to Metadata containing:
        // - description (optional string)
        // - messages (JSON string of array)
        GetPromptResult result;
        if (holds_alternative<Metadata>(response.result.value())) {
          auto metadata = get<Metadata>(response.result.value());
          // Extract description
          auto desc_it = metadata.find("description");
          if (desc_it != metadata.end() &&
              holds_alternative<std::string>(desc_it->second)) {
            result.description =
                mcp::make_optional(get<std::string>(desc_it->second));
          }
          // Extract messages from JSON string
          auto msgs_it = metadata.find("messages");
          if (msgs_it != metadata.end() &&
              holds_alternative<std::string>(msgs_it->second)) {
            // Parse messages JSON string back to PromptMessage array
            std::string msgs_json = get<std::string>(msgs_it->second);
            try {
              auto msgs_value = json::JsonValue::parse(msgs_json);
              if (msgs_value.isArray()) {
                size_t size = msgs_value.size();
                for (size_t i = 0; i < size; ++i) {
                  result.messages.push_back(
                      json::from_json<PromptMessage>(msgs_value[i]));
                }
              }
            } catch (...) {
              // Failed to parse messages, leave empty
            }
          }
        }
        result_promise->set_value(result);
      } else {
        result_promise->set_value(GetPromptResult());
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

// Set logging level
std::future<VoidResult> McpClient::setLogLevel(
    enums::LoggingLevel::Value level) {
  auto result_promise = std::make_shared<std::promise<VoidResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Prepare params before posting to dispatcher
  auto params = make_metadata();
  params["level"] = static_cast<int64_t>(level);
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  // Step 1: Post to dispatcher to send the request (non-blocking)
  main_dispatcher_->post([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequest("logging/setLevel", mcp::make_optional(*params_ptr));
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
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
    const std::vector<SamplingMessage>& messages,
    const optional<ModelPreferences>& preferences) {
  // Build parameters from request
  auto params = make_metadata();

  // Add messages (simplified - real implementation needs proper serialization)
  params["messages.count"] = static_cast<int64_t>(messages.size());

  // Add optional preferences
  if (preferences.has_value()) {
    // Add model preferences as metadata fields
    // This is a simplified implementation
    params["preferences"] = "provided";
  }

  // Request-specific parameters were removed since signature changed
  // to use messages and preferences parameters directly

  // Send request
  RequestId id = static_cast<int64_t>(next_request_id_++);
  auto context = std::make_shared<RequestContext>(id, "messages/create");
  context->params = mcp::make_optional(params);
  context->start_time = std::chrono::steady_clock::now();

  // Build parameters with proper structure
  MetadataBuilder builder;

  // Add messages array
  for (size_t i = 0; i < messages.size(); ++i) {
    const auto& msg = messages[i];
    std::string prefix = "messages." + std::to_string(i) + ".";
    builder.add(prefix + "role", static_cast<int64_t>(msg.role));

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

  // Add model preferences if provided
  if (preferences.has_value()) {
    const auto& prefs = preferences.value();
    // TODO: For now, just mark that preferences were provided
    // Full serialization would require JSON conversion
    builder.add("modelPreferences", "provided");
    if (prefs.costPriority.has_value()) {
      builder.add("modelPreferences.costPriority", prefs.costPriority.value());
    }
    if (prefs.speedPriority.has_value()) {
      builder.add("modelPreferences.speedPriority",
                  prefs.speedPriority.value());
    }
    if (prefs.intelligencePriority.has_value()) {
      builder.add("modelPreferences.intelligencePriority",
                  prefs.intelligencePriority.value());
    }
  }

  context->params = mcp::make_optional(builder.build());

  sendRequestInternal(context);

  // Return future that will convert response to CreateMessageResult
  auto result_promise = std::make_shared<std::promise<CreateMessageResult>>();
  auto result_future = result_promise->get_future();

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we wait on a worker thread.

  // Step: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([context, result_promise]() {
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
      result_promise->set_value(result);
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_future;
}

// Protocol state coordination - handle protocol state changes
void McpClient::handleProtocolStateChange(
    const protocol::ProtocolStateTransitionContext& context) {
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
    if (!initialized_ &&
        protocol_state != protocol::McpProtocolState::INITIALIZING) {
      // Auto-initialize protocol after connection
      // We're already in dispatcher thread from synchronizeState
      // DISABLED: Let the user explicitly call initializeProtocol()
      // initializeProtocol();
    }
  } else if (!connected_ &&
             protocol_state != protocol::McpProtocolState::DISCONNECTED) {
    // Network disconnected but protocol thinks it's connected
    // Already in dispatcher thread from caller
    protocol_state_machine_->handleEvent(
        protocol::McpProtocolEvent::NETWORK_DISCONNECTED);
  }
}

// Handle connection events from network layer
void McpClient::handleConnectionEvent(network::ConnectionEvent event) {
  std::cerr << "[McpClient] handleConnectionEvent called, event="
            << static_cast<int>(event) << std::endl;
  // Handle connection events in dispatcher context
  switch (event) {
    case network::ConnectionEvent::Connected:
    case network::ConnectionEvent::ConnectedZeroRtt:
      std::cerr << "[McpClient] Setting connected_=true" << std::endl;
      connected_ = true;
      last_activity_time_ = std::chrono::steady_clock::now();  // Reset idle timer on connection
      client_stats_.connections_active++;

      // Notify protocol state machine of network connection
      // We're already in dispatcher thread from connection callback
      if (protocol_state_machine_) {
        protocol_state_machine_->handleEvent(
            protocol::McpProtocolEvent::NETWORK_CONNECTED);
      }
      break;

    case network::ConnectionEvent::RemoteClose:
    case network::ConnectionEvent::LocalClose:
      connected_ = false;
      client_stats_.connections_active--;

      // Notify protocol state machine of network disconnection (already in
      // dispatcher thread)
      if (protocol_state_machine_) {
        protocol_state_machine_->handleEvent(
            protocol::McpProtocolEvent::NETWORK_DISCONNECTED);
      }

      // Fail all pending requests
      auto pending = request_tracker_->getTimedOutRequests();
      for (const auto& request : pending) {
        request->promise.set_value(jsonrpc::Response::make_error(
            request->id, Error(jsonrpc::INTERNAL_ERROR, "Connection closed")));
      }
      break;
  }

  // Coordinate protocol state with connection state
  coordinateProtocolState();
}

// Setup filter chain for the application
void McpClient::setupFilterChain(application::FilterChainBuilder& builder) {
  // Add filters as needed for the client
  // This is typically configured based on transport type
}

// Initialize worker thread
void McpClient::initializeWorker(application::WorkerContext& context) {
  // Worker initialization logic
  // Clients typically don't need special worker setup
}

// Send batch of requests
std::vector<std::future<Response>> McpClient::sendBatch(
    const std::vector<std::pair<std::string, optional<Metadata>>>& requests) {
  std::vector<std::future<Response>> futures;

  for (const auto& request : requests) {
    futures.push_back(sendRequest(request.first, request.second));
  }

  return futures;
}

// Track progress for a given token
void McpClient::trackProgress(const ProgressToken& token,
                              std::function<void(double)> callback) {
  // Store the callback for this progress token
  // Will be invoked when progress updates are received
}

}  // namespace client
}  // namespace mcp
