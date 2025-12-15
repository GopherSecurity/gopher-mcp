#include "mcp/mcp_connection_manager.h"

#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/tcp.h>  // For TCP_NODELAY
#endif

#include "mcp/core/result.h"
#include "mcp/filter/http_sse_filter_chain_factory.h"
#include "mcp/filter/protocol_detection_filter_chain_factory.h"
#include "mcp/filter/stdio_filter_chain_factory.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/network/address_impl.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/connection_manager.h"
#include "mcp/network/listener.h"
#include "mcp/network/socket_impl.h"
#include "mcp/stream_info/stream_info_impl.h"
#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/transport/https_sse_transport_factory.h"
#include "mcp/transport/pipe_io_handle.h"
#include "mcp/transport/stdio_pipe_transport.h"
#include "mcp/transport/stdio_transport_socket.h"

namespace mcp {

// McpConnectionManager implementation

McpConnectionManager::McpConnectionManager(
    event::Dispatcher& dispatcher,
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

McpConnectionManager::~McpConnectionManager() { close(); }

VoidResult McpConnectionManager::connect() {
  if (connected_) {
    Error err;
    err.code = -1;
    err.message = "Already connected";
    return makeVoidError(err);
  }

  is_server_ = false;

  if (config_.transport_type == TransportType::Stdio) {
    // For stdio, we use either direct socket or pipe bridge pattern
    if (!config_.stdio_config.has_value()) {
      Error err;
      err.code = -1;
      err.message = "Stdio config not set";
      return makeVoidError(err);
    }

    // Create stream info
    auto stream_info = stream_info::StreamInfoImpl::create();

    std::unique_ptr<network::ConnectionSocketImpl> socket_wrapper;
    network::TransportSocketPtr transport_socket;

    if (config_.stdio_config->use_bridge) {
      // Real stdio: use pipe bridge pattern for blocking I/O
      // Create and initialize the pipe transport
      transport::StdioPipeTransportConfig pipe_config;
      pipe_config.stdin_fd = config_.stdio_config->stdin_fd;
      pipe_config.stdout_fd = config_.stdio_config->stdout_fd;
      pipe_config.non_blocking = config_.stdio_config->non_blocking;

      auto pipe_transport =
          std::make_unique<transport::StdioPipeTransport>(pipe_config);

      // Initialize the pipe transport (creates pipes and starts bridge threads)
      auto init_result = pipe_transport->initialize();
      if (holds_alternative<Error>(init_result)) {
        return init_result;
      }

      // Get the pipe socket that ConnectionImpl will use
      socket_wrapper = pipe_transport->takePipeSocket();
      if (!socket_wrapper) {
        Error err;
        err.code = -1;
        err.message = "Failed to get pipe socket from transport";
        return makeVoidError(err);
      }

      // Use the pipe transport as the transport socket
      transport_socket = std::move(pipe_transport);
    } else {
      // Test pipes: use StdioTransportSocket directly
      // Create PipeIoHandle for the provided FDs
      auto io_handle = std::make_unique<transport::PipeIoHandle>(
          config_.stdio_config->stdin_fd, config_.stdio_config->stdout_fd);

      // Create pipe addresses
      auto local_address = std::make_shared<network::Address::PipeInstance>(
          "/tmp/test_stdio_in");
      auto remote_address = std::make_shared<network::Address::PipeInstance>(
          "/tmp/test_stdio_out");

      // Create the connection socket
      socket_wrapper = std::make_unique<network::ConnectionSocketImpl>(
          std::move(io_handle), local_address, remote_address);

      // Create StdioTransportSocket
      transport_socket = std::make_unique<transport::StdioTransportSocket>(
          *config_.stdio_config);
    }

    // Create connection - for stdio, we're already "connected" since the pipes
    // are ready So we create it similar to a server connection but mark it as
    // client
    auto connection = std::make_unique<network::ConnectionImpl>(
        dispatcher_, std::move(socket_wrapper), std::move(transport_socket),
        true);  // Pass true for connected since stdio transport is already
                // ready

    // Cast to ClientConnection interface
    active_connection_ =
        std::unique_ptr<network::ClientConnection>(std::move(connection));

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
      auto* conn_base =
          dynamic_cast<network::ConnectionImplBase*>(active_connection_.get());
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
      // Note: The initial read trigger was causing closeSocket to be called,
      // but we need it for level-triggered events
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

    // Parse server address to get host and port
    std::string server_address = config_.http_sse_config.value().server_address;
    std::string host = "127.0.0.1";
    uint32_t port = 8080;

    // Extract host and port from server_address
    // Support format: host:port or IP:port
    size_t colon_pos = server_address.rfind(':');
    if (colon_pos != std::string::npos) {
      host = server_address.substr(0, colon_pos);
      std::string port_str = server_address.substr(colon_pos + 1);
      try {
        port = std::stoi(port_str);
      } catch (const std::exception& e) {
        // Invalid port, use default
        // Invalid port, using default
        port = 8080;
      }
    } else {
      // No port specified, use entire string as host
      host = server_address;
    }

    // Convert localhost to IP
    if (host == "localhost") {
      host = "127.0.0.1";
    }

    // Create TCP address for remote server
    auto tcp_address = network::Address::parseInternetAddress(host, port);
    if (!tcp_address) {
      Error err;
      err.code = -1;
      err.message = "Failed to parse server address: " + host + ":" +
                    std::to_string(port);
      return makeVoidError(err);
    }

    // Create local address (bind to any interface, port 0 for ephemeral)
    auto local_address =
        network::Address::anyAddress(network::Address::IpVersion::v4, 0);

    // Create TCP socket using MCP socket interface
    // All socket operations happen in dispatcher thread context
    auto socket_result = socket_interface_.socket(
        network::SocketType::Stream, network::Address::Type::Ip,
        network::Address::IpVersion::v4, false);

    if (!socket_result.ok()) {
      Error err;
      err.code = -1;
      err.message =
          "Failed to create TCP socket: " +
          (socket_result.error_info ? socket_result.error_info->message
                                    : "Unknown error");
      return makeVoidError(err);
    }

    // Create IO handle wrapper for the socket
    auto io_handle =
        socket_interface_.ioHandleForFd(*socket_result.value, false);
    if (!io_handle) {
      socket_interface_.close(*socket_result.value);
      Error err;
      err.code = -1;
      err.message = "Failed to create IO handle for socket";
      return makeVoidError(err);
    }

    // Create ConnectionSocket wrapper
    auto socket_wrapper = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), local_address, tcp_address);

    // Set socket to non-blocking mode for async I/O
    socket_wrapper->ioHandle().setBlocking(false);

    // Enable TCP_NODELAY to disable Nagle's algorithm for low latency
    // This ensures data is sent immediately rather than being buffered
    int nodelay = 1;
    socket_wrapper->setSocketOption(IPPROTO_TCP, TCP_NODELAY, &nodelay,
                                    sizeof(nodelay));

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
    auto client_factory = dynamic_cast<network::ClientTransportSocketFactory*>(
        transport_factory.get());
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
        dispatcher_, std::move(socket_wrapper), std::move(transport_socket),
        false);  // Not yet connected - will connect asynchronously

    // Store as active connection
    active_connection_ =
        std::unique_ptr<network::ClientConnection>(std::move(connection));

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
      auto* conn_base =
          dynamic_cast<network::ConnectionImplBase*>(active_connection_.get());
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
    // - onEvent() will be called in dispatcher thread when connection
    // succeeds/fails
    // - All state transitions happen in dispatcher thread context
    // - No manual synchronization needed as everything runs single-threaded in
    // dispatcher

    // Cast to ClientConnection to access connect() method
    auto client_conn =
        dynamic_cast<network::ClientConnection*>(active_connection_.get());
    if (client_conn) {
      client_conn->connect();
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
  auto server_factory = dynamic_cast<network::ServerTransportSocketFactory*>(
      transport_factory.get());
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
  // IMPORTANT: Must keep listener manager alive for server to accept
  // connections The listener manager owns the actual listening socket
  listener_manager_ = std::make_unique<network::ListenerManagerImpl>(
      dispatcher_, socket_interface_);

  // Add listener with this as callbacks
  // The listener will call onNewConnection when clients connect
  auto result =
      listener_manager_->addListener(std::move(listener_config), *this);
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

VoidResult McpConnectionManager::sendNotification(
    const jsonrpc::Notification& notification) {
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

VoidResult McpConnectionManager::sendResponse(
    const jsonrpc::Response& response) {
  if (!connected_ || !active_connection_) {
    Error err;
    err.code = -1;
    err.message = "Not connected";
    return makeVoidError(err);
  }

  // Convert to JSON using the bridge - this properly handles variant
  // serialization
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
  if (protocol_callbacks_) {
    protocol_callbacks_->onRequest(request);
  }
}

void McpConnectionManager::onNotification(
    const jsonrpc::Notification& notification) {
  if (protocol_callbacks_) {
    protocol_callbacks_->onNotification(notification);
  }
}

void McpConnectionManager::onResponse(const jsonrpc::Response& response) {
  if (protocol_callbacks_) {
    protocol_callbacks_->onResponse(response);
  }
}

void McpConnectionManager::onConnectionEvent(network::ConnectionEvent event) {
  // Handle connection state transitions
  // All events are invoked in dispatcher thread context

  if (event == network::ConnectionEvent::Connected) {
    // Connection established successfully
    connected_ = true;

    // TRANSPORT NOTIFICATION: Notify HTTP/SSE transport about TCP connection
    // Flow: TCP connected → ConnectionEvent::Connected →
    // transport.onConnected() Why: The HTTP/SSE transport needs to know when
    // the underlying TCP connection is established so it can begin the HTTP
    // handshake. This is different from stdio transport which has pre-connected
    // pipes. The transport's state machine expects this call to transition from
    // TcpConnecting → TcpConnected. Note: ConnectionImpl already called
    // transport->connect() before TCP connect, so the transport is in
    // TcpConnecting state waiting for this notification.
    if (config_.transport_type == TransportType::HttpSse &&
        active_connection_) {
      auto& transport = active_connection_->transportSocket();
      transport.onConnected();
    }
  } else if (event == network::ConnectionEvent::RemoteClose ||
             event == network::ConnectionEvent::LocalClose) {
    // Connection closed - clean up state
    connected_ = false;
    active_connection_.reset();
  }

  // Forward event to upper layer callbacks
  if (protocol_callbacks_) {
    protocol_callbacks_->onConnectionEvent(event);
  }
}

void McpConnectionManager::onError(const Error& error) {
  if (protocol_callbacks_) {
    protocol_callbacks_->onError(error);
  }
}

void McpConnectionManager::onAccept(network::ConnectionSocketPtr&& socket) {
  // For MCP, we don't use listener filters
  // This is handled by the listener implementation
}

void McpConnectionManager::onNewConnection(
    network::ConnectionPtr&& connection) {
  // Server accepted a new client connection
  // Flow: Listener accepts TCP connection -> Creates ConnectionImpl -> Calls
  // this callback Next: Apply filters, notify transport socket, wait for HTTP
  // request

  // Store the new connection - this is now our active connection
  active_connection_ = std::move(connection);

  // Add connection callbacks to track connection events
  // All callbacks are invoked in dispatcher thread context
  if (active_connection_) {
    active_connection_->addConnectionCallbacks(*this);

    // Filter chain is already created by the listener
    // We don't need to create it again here

    // Mark connection as established
    connected_ = true;

    // For connections with transport sockets, notify that connection is ready
    if (active_connection_->transportSocket().protocol() != "") {
      active_connection_->transportSocket().onConnected();
    }

    // Connection should now be ready to receive data
    // The file events are already registered and should fire when data arrives
  }
}

std::unique_ptr<network::TransportSocketFactoryBase>
McpConnectionManager::createTransportSocketFactory() {
  switch (config_.transport_type) {
    case TransportType::Stdio:
      if (config_.stdio_config.has_value()) {
        return transport::createStdioTransportSocketFactory(
            config_.stdio_config.value());
      } else {
        return transport::createStdioTransportSocketFactory();
      }

    case TransportType::HttpSse:
      // For HTTP+SSE, use RawBufferTransportSocketFactory
      // The filter chain handles the HTTP and SSE protocols
      // The transport socket only handles raw buffer I/O
      // Following production pattern: transport sockets handle only I/O,
      // filters handle all protocol logic
      return std::make_unique<network::RawBufferTransportSocketFactory>();

    default:
      break;
  }

  return nullptr;
}

std::shared_ptr<network::FilterChainFactory>
McpConnectionManager::createFilterChainFactory() {
  // Create filter chain based on transport requirements
  // Architecture principle: Each filter handles exactly one protocol layer

  // Check if protocol detection is enabled
  if (config_.use_protocol_detection) {
    // Use protocol detection to automatically determine HTTP vs native MCP
    // This allows the client to connect to any server without knowing
    // the protocol in advance
    return std::make_shared<filter::ProtocolDetectionFilterChainFactory>(
        dispatcher_, *this, is_server_,
        true,  // enable_http
        true   // enable_native_mcp
    );
  }

  if (config_.transport_type == TransportType::HttpSse) {
    // Complex protocol stack for HTTP+SSE:
    // [TCP] → [Combined HTTP+SSE+JSON-RPC Filter] → [Application]
    //
    // The combined filter internally manages the protocol layers:
    // - HTTP codec for request/response
    // - SSE codec for event streams
    // - JSON-RPC for message protocol

    return std::make_shared<filter::HttpSseFilterChainFactory>(
        dispatcher_, *this, is_server_);

  } else {
    // Simple direct transport (stdio, websocket):
    // [Transport] → [JSON-RPC Filter] → [Application]
    //
    // No protocol stack needed - just JSON-RPC message handling

    return std::make_shared<filter::StdioFilterChainFactory>(
        dispatcher_, *this, is_server_, config_.use_message_framing);
  }
}

VoidResult McpConnectionManager::sendJsonMessage(
    const json::JsonValue& message) {
  // Convert to string
  std::string json_str = message.toString();

  // Layered architecture:
  // - This method: JSON serialization only
  // - Filters: Protocol formatting (HTTP, SSE, framing)
  // - Transport: Raw I/O only

  // For direct transports without framing, add newline delimiter
  // NOTE: HTTP+SSE transport doesn't need this as protocol layers handle it
  if (!config_.use_message_framing &&
      config_.transport_type == TransportType::Stdio) {
    json_str += "\n";
  }

  // Create buffer with JSON payload
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add(json_str);

  // Write through filter chain - each filter handles its protocol layer:
  // - JSON-RPC filter: message framing if configured
  // - SSE filter: SSE event formatting if applicable
  // - HTTP filter: HTTP request/response formatting if applicable
  // - Transport socket: raw I/O only
  active_connection_->write(*buffer, false);

  return makeVoidSuccess();
}

}  // namespace mcp