#include "mcp/mcp_connection_manager.h"
#include "mcp/core/result.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/connection_manager.h"
#include "mcp/network/listener.h"
#include "mcp/network/socket_impl.h"
#include "mcp/stream_info/stream_info_impl.h"
#include "mcp/transport/stdio_transport_socket.h"
#include "mcp/transport/stdio_pipe_transport.h"
#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include <sstream>
#include <iostream>

namespace mcp {

// JsonRpcMessageFilter implementation

JsonRpcMessageFilter::JsonRpcMessageFilter(McpMessageCallbacks& callbacks)
    : callbacks_(callbacks) {}

network::FilterStatus JsonRpcMessageFilter::onData(Buffer& data, bool end_stream) {
  (void)end_stream;
  
  // Parse incoming JSON-RPC messages
  parseMessages(data);
  
  // Note: parseMessages already drains the buffer
  
  return network::FilterStatus::Continue;
}

network::FilterStatus JsonRpcMessageFilter::onNewConnection() {
  // Reset state for new connection
  partial_message_.clear();
  return network::FilterStatus::Continue;
}

network::FilterStatus JsonRpcMessageFilter::onWrite(Buffer& data, bool end_stream) {
  (void)end_stream;
  
  // Frame outgoing messages if needed
  if (use_framing_) {
    frameMessage(data);
  }
  
  return network::FilterStatus::Continue;
}

void JsonRpcMessageFilter::parseMessages(Buffer& data) {
  // Convert buffer to string and drain it
  std::string buffer_str = data.toString();
  data.drain(data.length());
  
  // Add to partial message
  partial_message_ += buffer_str;
  
  // Debug output
  // std::cerr << "parseMessages: received '" << buffer_str << "'" << std::endl;
  // std::cerr << "parseMessages: partial_message_ = '" << partial_message_ << "'" << std::endl;
  
  if (use_framing_) {
    // Parse with message framing (length prefix)
    while (partial_message_.length() >= 4) {
      // Read length prefix (4 bytes, big-endian)
      uint32_t msg_len = 0;
      msg_len |= (static_cast<uint8_t>(partial_message_[0]) << 24);
      msg_len |= (static_cast<uint8_t>(partial_message_[1]) << 16);
      msg_len |= (static_cast<uint8_t>(partial_message_[2]) << 8);
      msg_len |= static_cast<uint8_t>(partial_message_[3]);
      
      if (partial_message_.length() < 4 + msg_len) {
        // Not enough data yet
        break;
      }
      
      // Extract message
      std::string json_str = partial_message_.substr(4, msg_len);
      partial_message_.erase(0, 4 + msg_len);
      
      // Parse JSON-RPC message
      parseMessage(json_str);
    }
  } else {
    // Parse newline-delimited JSON
    size_t pos = 0;
    while ((pos = partial_message_.find('\n')) != std::string::npos) {
      std::string line = partial_message_.substr(0, pos);
      partial_message_.erase(0, pos + 1);
      
      if (!line.empty()) {
        parseMessage(line);
      }
    }
  }
}

bool JsonRpcMessageFilter::parseMessage(const std::string& json_str) {
  try {
    // Debug output
    // std::cerr << "parseMessage: parsing '" << json_str << "'" << std::endl;
    
    auto json_val = json::JsonValue::parse(json_str);
    
    // Debug output
    // std::cerr << "parseMessage: parsed JSON: " << json_val.toString() << std::endl;
    
    // Determine message type
    if (json_val.contains("method")) {
      if (json_val.contains("id")) {
        // Request
        jsonrpc::Request request = json::from_json<jsonrpc::Request>(json_val);
        callbacks_.onRequest(request);
      } else {
        // Notification
        jsonrpc::Notification notification = json::from_json<jsonrpc::Notification>(json_val);
        callbacks_.onNotification(notification);
      }
    } else if (json_val.contains("result") || json_val.contains("error")) {
      // Response
      jsonrpc::Response response = json::from_json<jsonrpc::Response>(json_val);
      callbacks_.onResponse(response);
    }
    
    return true;
  } catch (const json::JsonException& e) {
    Error error;
    error.code = -32700;  // Parse error
    error.message = "JSON parse error: " + std::string(e.what());
    callbacks_.onError(error);
    return false;
  } catch (const std::exception& e) {
    Error error;
    error.code = -32700;  // Parse error
    error.message = "JSON parse error: " + std::string(e.what());
    callbacks_.onError(error);
    return false;
  }
}

void JsonRpcMessageFilter::frameMessage(Buffer& data) {
  if (!use_framing_) {
    return;
  }
  
  // Get current message length
  size_t msg_len = data.length();
  
  if (msg_len == 0) {
    return; // Nothing to frame
  }
  
  // Get message content before modifying buffer
  std::string message_content = data.toString();
  
  // Create new buffer with length prefix
  auto framed_buffer = std::make_unique<OwnedBuffer>();
  
  // Add 4-byte length prefix (big-endian)
  uint8_t len_bytes[4];
  len_bytes[0] = (msg_len >> 24) & 0xFF;
  len_bytes[1] = (msg_len >> 16) & 0xFF;
  len_bytes[2] = (msg_len >> 8) & 0xFF;
  len_bytes[3] = msg_len & 0xFF;
  
  framed_buffer->add(len_bytes, 4);
  framed_buffer->add(message_content);
  
  // Clear original buffer and add framed data
  data.drain(data.length());
  framed_buffer->move(data);  // Move FROM framed_buffer TO data
}

// McpConnectionManager implementation

McpConnectionManager::McpConnectionManager(event::Dispatcher& dispatcher,
                                           network::SocketInterface& socket_interface,
                                           const McpConnectionConfig& config)
    : dispatcher_(dispatcher),
      socket_interface_(socket_interface),
      config_(config) {
  
  // Create connection manager
  network::ConnectionManagerConfig conn_config;
  conn_config.per_connection_buffer_limit = config.buffer_limit;
  conn_config.connection_timeout = config.connection_timeout;
  
  // Set up transport socket factories
  // For now, we'll create separate factories for client and server
  // In a real implementation, we'd have proper factory creation methods
  // TODO: Implement proper client/server transport socket factory creation
  
  // Set up filter chain factory
  conn_config.filter_chain_factory = createFilterChainFactory();
  
  connection_manager_ = std::make_unique<network::ConnectionManagerImpl>(
      dispatcher_, socket_interface_, conn_config);
}

McpConnectionManager::~McpConnectionManager() {
  close();
}

VoidResult McpConnectionManager::connect() {
  if (connected_) {
    Error err;
    err.code = -1;
    err.message = "Already connected";
    return makeVoidError(err);
  }
  
  is_server_ = false;
  
  if (config_.transport_type == TransportType::Stdio) {
    // For stdio, we use the pipe bridge pattern
    if (!config_.stdio_config.has_value()) {
      Error err;
      err.code = -1;
      err.message = "Stdio config not set";
      return makeVoidError(err);
    }
    
    // Create stream info
    auto stream_info = stream_info::StreamInfoImpl::create();
    
    // Create and initialize the pipe transport
    transport::StdioPipeTransportConfig pipe_config;
    pipe_config.stdin_fd = config_.stdio_config->stdin_fd;
    pipe_config.stdout_fd = config_.stdio_config->stdout_fd;
    pipe_config.non_blocking = config_.stdio_config->non_blocking;
    
    auto pipe_transport = std::make_unique<transport::StdioPipeTransport>(pipe_config);
    
    // Initialize the pipe transport (creates pipes and starts bridge threads)
    auto init_result = pipe_transport->initialize();
    if (holds_alternative<Error>(init_result)) {
      return init_result;
    }
    
    // Get the pipe socket that ConnectionImpl will use
    auto socket_wrapper = pipe_transport->takePipeSocket();
    if (!socket_wrapper) {
      Error err;
      err.code = -1;
      err.message = "Failed to get pipe socket from transport";
      return makeVoidError(err);
    }
    
    // Use the pipe transport as the transport socket
    network::TransportSocketPtr transport_socket = std::move(pipe_transport);
    
    // Create connection - for stdio, we're already "connected" since the pipes are ready
    // So we create it similar to a server connection but mark it as client
    auto connection = std::make_unique<network::ConnectionImpl>(
        dispatcher_,
        std::move(socket_wrapper),
        std::move(transport_socket),
        true);  // Pass true for connected since stdio transport is already ready
    
    // Cast to ClientConnection interface
    active_connection_ = std::unique_ptr<network::ClientConnection>(std::move(connection));
    
    if (!active_connection_) {
      Error err;
      err.code = -1;
      err.message = "Failed to create connection";
      return makeVoidError(err);
    }
    
    // Apply filter chain to the connection's filter manager
    auto filter_factory = createFilterChainFactory();
    if (filter_factory && active_connection_) {
      // Cast to ConnectionImplBase to access the filter manager
      auto* conn_base = dynamic_cast<network::ConnectionImplBase*>(active_connection_.get());
      if (conn_base) {
        // Apply the filter chain
        filter_factory->createFilterChain(conn_base->filterManager());
        
        // Initialize the read filters
        conn_base->filterManager().initializeReadFilters();
      }
    }
    
    // Mark as connected
    connected_ = true;
    // The pipe transport is already initialized and running
    if (active_connection_) {
      auto& transport = active_connection_->transportSocket();
      transport.onConnected();
      
      // For stdio pipes with level-triggered events, schedule an initial read
      // This ensures we process any data that might already be in the pipe
      // Note: The initial read trigger was causing closeSocket to be called, but we need it for level-triggered events
    }
    
    // Notify callbacks
    onConnectionEvent(network::ConnectionEvent::Connected);
    
  } else if (config_.transport_type == TransportType::HttpSse) {
    // HTTP/SSE client connection flow:
    // 1. Parse URL to extract host and port
    // 2. Create TCP socket using MCP networking layer
    // 3. Create HTTP/SSE transport socket wrapper
    // 4. Create ConnectionImpl with TCP socket and transport
    // 5. Connect asynchronously in dispatcher thread
    
    if (!config_.http_sse_config.has_value()) {
      Error err;
      err.code = -1;
      err.message = "HTTP/SSE config not set";
      return makeVoidError(err);
    }
    
    // Parse URL to get host and port
    std::string url = config_.http_sse_config.value().endpoint_url;
    std::string host = "127.0.0.1";
    uint32_t port = 8080;
    
    // Extract host and port from URL
    // Support format: http://host:port/path or https://host:port/path
    if (url.find("http://") == 0 || url.find("https://") == 0) {
      size_t protocol_end = url.find("://") + 3;
      size_t port_start = url.find(':', protocol_end);
      size_t path_start = url.find('/', protocol_end);
      
      if (port_start != std::string::npos && 
          (path_start == std::string::npos || port_start < path_start)) {
        // Has explicit port
        host = url.substr(protocol_end, port_start - protocol_end);
        if (host == "localhost") {
          host = "127.0.0.1";
        }
        size_t port_end = (path_start != std::string::npos) ? path_start : url.length();
        std::string port_str = url.substr(port_start + 1, port_end - port_start - 1);
        port = std::stoi(port_str);
      } else if (path_start != std::string::npos) {
        // No explicit port, use default based on protocol
        host = url.substr(protocol_end, path_start - protocol_end);
        if (host == "localhost") {
          host = "127.0.0.1";
        }
        port = (url.find("https://") == 0) ? 443 : 80;
      } else {
        // No path, just host
        host = url.substr(protocol_end);
        if (host == "localhost") {
          host = "127.0.0.1";
        }
        port = (url.find("https://") == 0) ? 443 : 80;
      }
    }
    
    // Create TCP address for remote server
    auto tcp_address = network::Address::parseInternetAddress(host, port);
    if (!tcp_address) {
      Error err;
      err.code = -1;
      err.message = "Failed to parse server address: " + host + ":" + std::to_string(port);
      return makeVoidError(err);
    }
    
    // Create local address (bind to any interface, port 0 for ephemeral)
    auto local_address = network::Address::anyAddress(
        network::Address::IpVersion::v4, 0);
    
    // Create TCP socket using MCP socket interface
    // All socket operations happen in dispatcher thread context
    auto socket_result = socket_interface_.socket(
        network::SocketType::Stream,
        network::Address::Type::Ip,
        network::Address::IpVersion::v4,
        false);
    
    if (!socket_result.ok()) {
      Error err;
      err.code = -1;
      err.message = "Failed to create TCP socket: " + 
          (socket_result.error_info ? socket_result.error_info->message : "Unknown error");
      return makeVoidError(err);
    }
    
    // Create IO handle wrapper for the socket
    auto io_handle = socket_interface_.ioHandleForFd(*socket_result.value, false);
    if (!io_handle) {
      socket_interface_.close(*socket_result.value);
      Error err;
      err.code = -1;
      err.message = "Failed to create IO handle for socket";
      return makeVoidError(err);
    }
    
    // Create ConnectionSocket wrapper
    auto socket_wrapper = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle),
        local_address,
        tcp_address);
    
    // Set socket to non-blocking mode for async I/O
    socket_wrapper->ioHandle().setBlocking(false);
    
    // Create HTTP/SSE transport socket wrapper
    auto transport_factory = createTransportSocketFactory();
    if (!transport_factory) {
      Error err;
      err.code = -1;
      err.message = "Failed to create transport factory";
      return makeVoidError(err);
    }
    
    // Create transport socket instance
    // Cast to client factory to access createTransportSocket method
    auto client_factory = dynamic_cast<network::ClientTransportSocketFactory*>(transport_factory.get());
    if (!client_factory) {
      Error err;
      err.code = -1;
      err.message = "Transport factory does not support client connections";
      return makeVoidError(err);
    }
    
    network::TransportSocketPtr transport_socket = 
        client_factory->createTransportSocket(nullptr);
    if (!transport_socket) {
      Error err;
      err.code = -1;
      err.message = "Failed to create transport socket";
      return makeVoidError(err);
    }
    
    // Create ConnectionImpl for client connection
    // Pass false for 'connected' since we need to connect first
    auto connection = std::make_unique<network::ConnectionImpl>(
        dispatcher_,
        std::move(socket_wrapper),
        std::move(transport_socket),
        false);  // Not yet connected - will connect asynchronously
    
    // Store as active connection
    active_connection_ = std::unique_ptr<network::ClientConnection>(std::move(connection));
    
    if (!active_connection_) {
      Error err;
      err.code = -1;
      err.message = "Failed to create client connection";
      return makeVoidError(err);
    }
    
    // Add ourselves as connection callbacks to track connection events
    active_connection_->addConnectionCallbacks(*this);
    
    // Apply filter chain for JSON-RPC message processing
    auto filter_factory = createFilterChainFactory();
    if (filter_factory && active_connection_) {
      auto* conn_base = dynamic_cast<network::ConnectionImplBase*>(active_connection_.get());
      if (conn_base) {
        // Apply filter chain for message framing and parsing
        filter_factory->createFilterChain(conn_base->filterManager());
        conn_base->filterManager().initializeReadFilters();
      }
    }
    
    // Initiate async TCP connection
    // This will trigger connect() on the socket in dispatcher thread
    // Connection callbacks will be invoked when connected or on error
    // IMPORTANT: All callbacks follow the dispatcher thread principle:
    // - onEvent() will be called in dispatcher thread when connection succeeds/fails
    // - All state transitions happen in dispatcher thread context
    // - No manual synchronization needed as everything runs single-threaded in dispatcher
    
    // Cast to ClientConnection to access connect() method
    auto client_conn = dynamic_cast<network::ClientConnection*>(active_connection_.get());
    if (client_conn) {
      std::cerr << "[DEBUG] Initiating TCP connection to " << host << ":" << port << std::endl;
      client_conn->connect();
      std::cerr << "[DEBUG] TCP connect() called, waiting for async connection..." << std::endl;
    } else {
      Error err;
      err.code = -1;
      err.message = "Failed to cast to ClientConnection";
      return makeVoidError(err);
    }
    
    // NOTE: Connection is now in progress
    // onEvent callback will be called with Connected or LocalClose event
    // TODO: Add connection timeout handling
    // TODO: Add retry logic with exponential backoff for connection failures
    // TODO: Support TLS/HTTPS connections using SSL transport socket
  } else {
    Error err;
    err.code = -1;
    err.message = "Unknown transport type";
    return makeVoidError(err);
  }
  
  return makeVoidSuccess();
}

VoidResult McpConnectionManager::listen(
    const network::Address::InstanceConstSharedPtr& address) {
  if (connected_) {
    Error err;
    err.code = -1;
    err.message = "Already connected";
    return makeVoidError(err);
  }
  
  is_server_ = true;
  
  // Create listener config
  network::ListenerConfig listener_config;
  listener_config.name = "mcp_listener";
  listener_config.address = address;
  listener_config.per_connection_buffer_limit = config_.buffer_limit;
  
  // Create server transport socket factory
  auto transport_factory = createTransportSocketFactory();
  if (!transport_factory) {
    Error err;
    err.code = -1;
    err.message = "Failed to create transport factory";
    return makeVoidError(err);
  }
  
  // Check if it supports server connections and convert to shared_ptr
  auto server_factory = dynamic_cast<network::ServerTransportSocketFactory*>(transport_factory.get());
  if (server_factory) {
    // Release from unique_ptr and create shared_ptr
    transport_factory.release();
    listener_config.transport_socket_factory = 
        std::shared_ptr<network::ServerTransportSocketFactory>(server_factory);
  } else {
    Error err;
    err.code = -1;
    err.message = "Transport factory does not support server connections";
    return makeVoidError(err);
  }
  
  listener_config.filter_chain_factory = createFilterChainFactory();
  
  // Create listener manager and store it as member
  // IMPORTANT: Must keep listener manager alive for server to accept connections
  // The listener manager owns the actual listening socket
  listener_manager_ = std::make_unique<network::ListenerManagerImpl>(
      dispatcher_, socket_interface_);
  
  // Add listener with this as callbacks
  // The listener will call onNewConnection when clients connect
  auto result = listener_manager_->addListener(std::move(listener_config), *this);
  if (mcp::holds_alternative<Error>(result)) {
    return result;
  }
  
  // Mark as "connected" (actually listening) for server mode
  connected_ = true;
  
  return makeVoidSuccess();
}

VoidResult McpConnectionManager::sendRequest(const jsonrpc::Request& request) {
  if (!connected_ || !active_connection_) {
    Error err;
    err.code = -1;
    err.message = "Not connected";
    return makeVoidError(err);
  }
  
  // Convert to JSON using the bridge
  auto json_val = json::to_json(request);
  
  return sendJsonMessage(json_val);
}

VoidResult McpConnectionManager::sendNotification(const jsonrpc::Notification& notification) {
  if (!connected_ || !active_connection_) {
    Error err;
    err.code = -1;
    err.message = "Not connected";
    return makeVoidError(err);
  }
  
  // Convert to JSON using the bridge
  auto json_val = json::to_json(notification);
  
  return sendJsonMessage(json_val);
}

VoidResult McpConnectionManager::sendResponse(const jsonrpc::Response& response) {
  if (!connected_ || !active_connection_) {
    Error err;
    err.code = -1;
    err.message = "Not connected";
    return makeVoidError(err);
  }
  
  // Convert to JSON using the bridge - this properly handles variant serialization
  auto json_val = json::to_json(response);
  
  return sendJsonMessage(json_val);
}

void McpConnectionManager::close() {
  // Close active connection if any
  if (active_connection_) {
    active_connection_->close(network::ConnectionCloseType::FlushWrite);
    active_connection_.reset();
  }
  
  // Stop listening if we're a server
  if (listener_manager_) {
    // Listener manager destructor will close the listening socket
    listener_manager_.reset();
  }
  
  connected_ = false;
}

bool McpConnectionManager::isConnected() const {
  return connected_ && active_connection_ && 
         active_connection_->state() == network::ConnectionState::Open;
}

void McpConnectionManager::onRequest(const jsonrpc::Request& request) {
  if (message_callbacks_) {
    message_callbacks_->onRequest(request);
  }
}

void McpConnectionManager::onNotification(const jsonrpc::Notification& notification) {
  if (message_callbacks_) {
    message_callbacks_->onNotification(notification);
  }
}

void McpConnectionManager::onResponse(const jsonrpc::Response& response) {
  if (message_callbacks_) {
    message_callbacks_->onResponse(response);
  }
}

void McpConnectionManager::onConnectionEvent(network::ConnectionEvent event) {
  // Handle connection state transitions
  // All events are invoked in dispatcher thread context
  std::cerr << "[DEBUG] McpConnectionManager::onConnectionEvent: " << static_cast<int>(event) << std::endl;
  
  if (event == network::ConnectionEvent::Connected) {
    // Connection established successfully
    std::cerr << "[DEBUG] TCP connection established!" << std::endl;
    connected_ = true;
    
    // For HTTP/SSE transport, notify the transport socket about connection
    if (config_.transport_type == TransportType::HttpSse && active_connection_) {
      auto& transport = active_connection_->transportSocket();
      transport.onConnected();
    }
  } else if (event == network::ConnectionEvent::RemoteClose || 
             event == network::ConnectionEvent::LocalClose) {
    // Connection closed - clean up state
    std::cerr << "[DEBUG] Connection closed: " 
              << (event == network::ConnectionEvent::RemoteClose ? "remote" : "local") << std::endl;
    connected_ = false;
    active_connection_.reset();
  }
  
  // Forward event to upper layer callbacks
  if (message_callbacks_) {
    message_callbacks_->onConnectionEvent(event);
  }
}

void McpConnectionManager::onError(const Error& error) {
  if (message_callbacks_) {
    message_callbacks_->onError(error);
  }
}

void McpConnectionManager::onAccept(network::ConnectionSocketPtr&& socket) {
  // For MCP, we don't use listener filters
  // This is handled by the listener implementation
}

void McpConnectionManager::onNewConnection(network::ConnectionPtr&& connection) {
  // Server accepted a new client connection
  // Flow: Listener accepts TCP connection -> Creates ConnectionImpl -> Calls this callback
  // Next: Apply filters, notify transport socket, wait for HTTP request
  
  std::cerr << "[DEBUG] Server: New connection accepted" << std::endl;
  
  // Store the new connection - this is now our active connection
  active_connection_ = std::move(connection);
  
  // Add connection callbacks to track connection events
  // All callbacks are invoked in dispatcher thread context
  if (active_connection_) {
    active_connection_->addConnectionCallbacks(*this);
    
    // Apply filter chain to process JSON-RPC messages
    // The filter chain handles message framing and parsing
    auto filter_factory = createFilterChainFactory();
    if (filter_factory) {
      auto* conn_base = dynamic_cast<network::ConnectionImplBase*>(active_connection_.get());
      if (conn_base) {
        filter_factory->createFilterChain(conn_base->filterManager());
        conn_base->filterManager().initializeReadFilters();
      }
    }
    
    // Mark connection as established
    connected_ = true;
    
    // Notify transport socket that connection is ready
    // For HTTP+SSE server, this triggers waiting for HTTP request
    auto& transport = active_connection_->transportSocket();
    std::cerr << "[DEBUG] Server: Notifying transport socket of new connection" << std::endl;
    transport.onConnected();
  }
}

std::unique_ptr<network::TransportSocketFactoryBase> McpConnectionManager::createTransportSocketFactory() {
  switch (config_.transport_type) {
  case TransportType::Stdio:
    if (config_.stdio_config.has_value()) {
      return transport::createStdioTransportSocketFactory(config_.stdio_config.value());
    } else {
      return transport::createStdioTransportSocketFactory();
    }
    
  case TransportType::HttpSse:
    if (config_.http_sse_config.has_value()) {
      // Create HTTP+SSE transport socket factory
      return transport::createHttpSseTransportSocketFactory(config_.http_sse_config.value(), dispatcher_);
    }
    break;
    
  default:
    break;
  }
  
  return nullptr;
}

std::shared_ptr<network::FilterChainFactory> McpConnectionManager::createFilterChainFactory() {
  auto factory = std::make_shared<network::FilterChainFactoryImpl>();
  
  // Add JSON-RPC message filter
  factory->addFilterFactory([this]() -> network::FilterSharedPtr {
    auto filter = std::make_shared<JsonRpcMessageFilter>(*this);
    filter->setUseFraming(config_.use_message_framing);
    return filter;
  });
  
  return factory;
}

VoidResult McpConnectionManager::sendJsonMessage(const json::JsonValue& message) {
  // Convert to string
  std::string json_str = message.toString();
  
  // Add newline for non-framed mode
  if (!config_.use_message_framing) {
    json_str += "\n";
  }
  
  // Create buffer
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add(json_str);
  
  // Send through connection
  active_connection_->write(*buffer, false);
  
  return makeVoidSuccess();
}

} // namespace mcp