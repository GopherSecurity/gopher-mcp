#include "mcp/mcp_connection_manager.h"
#include "mcp/result.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/connection_manager.h"
#include "mcp/network/listener.h"
#include "mcp/stream_info/stream_info_impl.h"
#include "mcp/transport/stdio_transport_socket.h"
#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/json.h"
#include <sstream>

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
    auto json = nlohmann::json::parse(json_str);
    
    // Determine message type
    if (json.contains("method")) {
      if (json.contains("id")) {
        // Request
        jsonrpc::Request request;
        request.id = json["id"];
        request.method = json["method"];
        if (json.contains("params")) {
          request.params = json["params"];
        }
        callbacks_.onRequest(request);
      } else {
        // Notification
        jsonrpc::Notification notification;
        notification.method = json["method"];
        if (json.contains("params")) {
          notification.params = json["params"];
        }
        callbacks_.onNotification(notification);
      }
    } else if (json.contains("result") || json.contains("error")) {
      // Response
      jsonrpc::Response response;
      response.id = json["id"];
      if (json.contains("result")) {
        response.result = json["result"];
      }
      if (json.contains("error")) {
        Error error;
        error.code = json["error"]["code"];
        error.message = json["error"]["message"];
        if (json["error"].contains("data")) {
          error.data = json["error"]["data"];
        }
        response.error = error;
      }
      callbacks_.onResponse(response);
    }
    
    return true;
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
  
  // Create new buffer with length prefix
  auto framed_buffer = std::make_unique<OwnedBuffer>();
  
  // Add 4-byte length prefix (big-endian)
  uint8_t len_bytes[4];
  len_bytes[0] = (msg_len >> 24) & 0xFF;
  len_bytes[1] = (msg_len >> 16) & 0xFF;
  len_bytes[2] = (msg_len >> 8) & 0xFF;
  len_bytes[3] = msg_len & 0xFF;
  
  framed_buffer->add(len_bytes, 4);
  
  // Add message data
  framed_buffer->move(data);
  
  // Move framed data back
  data.move(*framed_buffer);
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
    // For stdio, we create a "fake" connection that wraps stdin/stdout
    // Create a dummy socket
    auto socket = socket_interface_.socket(
        network::SocketType::Stream,
        network::Address::Type::Pipe);
    
    if (!socket.ok()) {
      Error err;
      err.code = -1;
      err.message = "Failed to create socket";
      return makeVoidError(err);
    }
    
    // Create stream info
    auto stream_info = stream_info::StreamInfoImpl::create();
    
    // Create transport socket
    auto transport_factory = createTransportSocketFactory();
    if (!transport_factory) {
      Error err;
      err.code = -1;
      err.message = "Failed to create transport factory";
      return makeVoidError(err);
    }
    
    // Create IoHandle from fd
    auto io_handle = socket_interface_.ioHandleForFd(*socket);
    if (!io_handle) {
      socket_interface_.close(*socket);
      Error err;
      err.code = -1;
      err.message = "Failed to create IO handle";
      return makeVoidError(err);
    }
    
    // Create socket wrapper
    auto socket_wrapper = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), nullptr, nullptr);
    
    // For client connections, we need to create the transport socket differently
    // TODO: Implement proper client transport socket creation
    network::TransportSocketPtr transport_socket;
    
    // Create connection
    active_connection_ = network::ConnectionImpl::createClientConnection(
        dispatcher_,
        std::move(socket_wrapper),
        std::move(transport_socket),
        *stream_info);
    
    if (!active_connection_) {
      Error err;
      err.code = -1;
      err.message = "Failed to create connection";
      return makeVoidError(err);
    }
    
    // Apply filter chain
    auto filter_factory = createFilterChainFactory();
    if (filter_factory) {
      // TODO: Filter chain needs to be applied to FilterManager, not Connection directly
    }
    
    // Initialize filters
    // TODO: initializeReadFilters() needs to be called on the FilterManager
    
    // Mark as connected
    connected_ = true;
    // Transport socket should be notified instead
    if (active_connection_) {
      auto& transport = active_connection_->transportSocket();
      transport.onConnected();
    }
    
    // Notify callbacks
    onConnectionEvent(network::ConnectionEvent::Connected);
    
  } else if (config_.transport_type == TransportType::HttpSse) {
    // For HTTP/SSE, we need to parse the URL and connect
    // This would involve creating a real TCP connection
    // For now, return error
    Error err;
    err.code = -1;
    err.message = "HTTP/SSE transport not fully implemented";
    return makeVoidError(err);
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
  // TODO: Create proper server transport socket factory
  // listener_config.transport_socket_factory = createServerTransportSocketFactory();
  listener_config.filter_chain_factory = createFilterChainFactory();
  
  // Create listener manager
  auto listener_manager = std::make_unique<network::ListenerManagerImpl>(
      dispatcher_, socket_interface_);
  
  // Add listener with this as callbacks
  auto result = listener_manager->addListener(std::move(listener_config), *this);
  if (result.holds_alternative<Error>()) {
    return result;
  }
  
  return makeVoidSuccess();
}

VoidResult McpConnectionManager::sendRequest(const jsonrpc::Request& request) {
  if (!connected_ || !active_connection_) {
    Error err;
    err.code = -1;
    err.message = "Not connected";
    return makeVoidError(err);
  }
  
  // Convert to JSON
  nlohmann::json json;
  json["jsonrpc"] = "2.0";
  json["id"] = request.id;
  json["method"] = request.method;
  if (request.params.has_value()) {
    json["params"] = request.params.value();
  }
  
  return sendJsonMessage(json);
}

VoidResult McpConnectionManager::sendNotification(const jsonrpc::Notification& notification) {
  if (!connected_ || !active_connection_) {
    Error err;
    err.code = -1;
    err.message = "Not connected";
    return makeVoidError(err);
  }
  
  // Convert to JSON
  nlohmann::json json;
  json["jsonrpc"] = "2.0";
  json["method"] = notification.method;
  if (notification.params.has_value()) {
    json["params"] = notification.params.value();
  }
  
  return sendJsonMessage(json);
}

VoidResult McpConnectionManager::sendResponse(const jsonrpc::Response& response) {
  if (!connected_ || !active_connection_) {
    Error err;
    err.code = -1;
    err.message = "Not connected";
    return makeVoidError(err);
  }
  
  // Convert to JSON
  nlohmann::json json;
  json["jsonrpc"] = "2.0";
  json["id"] = response.id;
  
  if (response.result.has_value()) {
    // TODO: Implement variant serialization
    json["result"] = nullptr;  // Placeholder
  }
  
  if (response.error.has_value()) {
    json["error"] = {
        {"code", response.error->code},
        {"message", response.error->message}
    };
    if (response.error->data.has_value()) {
      // TODO: Implement variant serialization
      json["error"]["data"] = nullptr;  // Placeholder
    }
  }
  
  return sendJsonMessage(json);
}

void McpConnectionManager::close() {
  if (active_connection_) {
    active_connection_->close(network::ConnectionCloseType::FlushWrite);
    active_connection_.reset();
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
  if (event == network::ConnectionEvent::RemoteClose || 
      event == network::ConnectionEvent::LocalClose) {
    connected_ = false;
    active_connection_.reset();
  }
  
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
  // Store the new connection
  active_connection_ = std::move(connection);
  
  // TODO: Add connection callbacks and filters
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
      return transport::createHttpSseTransportSocketFactory(config_.http_sse_config.value());
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

VoidResult McpConnectionManager::sendJsonMessage(const nlohmann::json& message) {
  // Convert to string
  std::string json_str = message.dump();
  
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