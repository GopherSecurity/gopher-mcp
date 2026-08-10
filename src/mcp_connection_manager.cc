#include "mcp/mcp_connection_manager.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>

#include "mcp/logging/log_macros.h"

// Define log component for this file
#undef GOPHER_LOG_COMPONENT
#define GOPHER_LOG_COMPONENT "connection_manager"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>  // For getaddrinfo

#include <arpa/inet.h>    // For inet_ntop
#include <netinet/tcp.h>  // For TCP_NODELAY
#endif

#include "mcp/core/result.h"
#include "mcp/filter/http_codec_filter.h"
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
#include "mcp/transport/tcp_transport_socket.h"

namespace mcp {

namespace {

// Helper function to resolve hostname to IP address using DNS
// Returns empty string on failure
std::string resolveHostname(const std::string& hostname) {
  struct addrinfo hints, *result;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;  // IPv4
  hints.ai_socktype = SOCK_STREAM;

  int status = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
  if (status != 0) {
    return "";
  }

  std::string ip_address;
  if (result != nullptr) {
    char ip_str[INET_ADDRSTRLEN];
    struct sockaddr_in* ipv4 =
        reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
    if (inet_ntop(AF_INET, &(ipv4->sin_addr), ip_str, sizeof(ip_str)) !=
        nullptr) {
      ip_address = ip_str;
    }
    freeaddrinfo(result);
  }

  return ip_address;
}

}  // namespace

// McpConnectionManager implementation

McpConnectionManager::McpConnectionManager(
    event::Dispatcher& dispatcher,
    network::SocketInterface& socket_interface,
    const McpConnectionConfig& config)
    : dispatcher_(dispatcher),
      socket_interface_(socket_interface),
      config_(config) {
  if (!config_.current_http_headers) {
    config_.current_http_headers =
        std::make_shared<std::map<std::string, std::string>>(
            config_.http_headers);
  }

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

network::Address::InstanceConstSharedPtr
McpConnectionManager::resolveServerAddress(std::string& error) const {
  if (!config_.http_sse_config.has_value()) {
    error = "HTTP config not set for an HTTP transport";
    return nullptr;
  }

  const std::string server_address =
      config_.http_sse_config.value().server_address;

  // Without a port the scheme decides, which here is whether the
  // transport underneath is doing TLS.
  const bool is_https =
      config_.http_sse_config.value().underlying_transport ==
      transport::HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
  const uint32_t default_port = is_https ? 443 : 80;

  std::string host = server_address;
  uint32_t port = default_port;

  // rfind, so that only a trailing :digits counts as a port and a host
  // that happens to contain a colon is left alone.
  const size_t colon_pos = server_address.rfind(':');
  if (colon_pos != std::string::npos) {
    const std::string port_str = server_address.substr(colon_pos + 1);
    const bool valid_port =
        !port_str.empty() &&
        port_str.find_first_not_of("0123456789") == std::string::npos;
    if (valid_port) {
      try {
        port = static_cast<uint32_t>(std::stoi(port_str));
        host = server_address.substr(0, colon_pos);
      } catch (const std::exception&) {
        host = server_address;
        port = default_port;
      }
    }
  }

  if (host == "localhost") {
    host = "127.0.0.1";
  }

  // A literal address needs no resolving; a name does.
  auto address = network::Address::parseInternetAddress(host, port);
  if (!address) {
    const std::string resolved_ip = resolveHostname(host);
    if (!resolved_ip.empty()) {
      address = network::Address::parseInternetAddress(resolved_ip, port);
    }
  }

  if (!address) {
    error = "Failed to resolve server address: " + host + ":" +
            std::to_string(port) + " (DNS resolution failed)";
    return nullptr;
  }
  return address;
}

std::unique_ptr<network::ClientConnection>
McpConnectionManager::createHttpClientConnection(
    const std::shared_ptr<network::FilterChainFactory>& filter_factory,
    std::string& error) {
  auto tcp_address = resolveServerAddress(error);
  if (!tcp_address) {
    return nullptr;
  }

  // Bind nothing in particular: an ephemeral local port on any interface.
  auto local_address =
      network::Address::anyAddress(network::Address::IpVersion::v4, 0);

  auto socket_result = socket_interface_.socket(
      network::SocketType::Stream, network::Address::Type::Ip,
      network::Address::IpVersion::v4, false);
  if (!socket_result.ok()) {
    error = "Failed to create TCP socket: " +
            (socket_result.error_info ? socket_result.error_info->message
                                      : std::string("Unknown error"));
    return nullptr;
  }

  auto io_handle = socket_interface_.ioHandleForFd(*socket_result.value, false);
  if (!io_handle) {
    socket_interface_.close(*socket_result.value);
    error = "Failed to create IO handle for socket";
    return nullptr;
  }

  auto socket_wrapper = std::make_unique<network::ConnectionSocketImpl>(
      std::move(io_handle), local_address, tcp_address);

  // Non-blocking for async I/O, and Nagle off so a small request goes out
  // when it is written rather than when the kernel feels like it.
  socket_wrapper->ioHandle().setBlocking(false);
  int nodelay = 1;
  socket_wrapper->setSocketOption(IPPROTO_TCP, TCP_NODELAY, &nodelay,
                                  sizeof(nodelay));

  auto transport_factory = createTransportSocketFactory();
  if (!transport_factory) {
    error = "Failed to create transport factory";
    return nullptr;
  }

  auto client_factory = dynamic_cast<network::ClientTransportSocketFactory*>(
      transport_factory.get());
  if (!client_factory) {
    error = "Transport factory does not support client connections";
    return nullptr;
  }

  network::TransportSocketPtr transport_socket =
      client_factory->createTransportSocket(nullptr);
  if (!transport_socket) {
    error = "Failed to create transport socket";
    return nullptr;
  }

  // Not yet connected — the caller attaches its callbacks and then
  // connects, so the first event has somewhere to go.
  auto connection = std::unique_ptr<network::ClientConnection>(
      new network::ConnectionImpl(dispatcher_, std::move(socket_wrapper),
                                  std::move(transport_socket), false));

  if (filter_factory) {
    auto* conn_base =
        dynamic_cast<network::ConnectionImplBase*>(connection.get());
    if (conn_base) {
      filter_factory->createFilterChain(conn_base->filterManager());
      conn_base->filterManager().initializeReadFilters();
    }
  }

  return connection;
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
#ifndef _WIN32
      auto local_address = std::make_shared<network::Address::PipeInstance>(
          "/tmp/test_stdio_in");
      auto remote_address = std::make_shared<network::Address::PipeInstance>(
          "/tmp/test_stdio_out");
#else
      // Windows: Use loopback placeholder addresses for test pipes
      // (PipeInstance is Unix-only, use Ipv4Instance as placeholder)
      auto local_address =
          std::make_shared<network::Address::Ipv4Instance>("127.0.0.1", 0);
      auto remote_address =
          std::make_shared<network::Address::Ipv4Instance>("127.0.0.1", 0);
#endif

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

  } else if (config_.transport_type == TransportType::HttpSse ||
             config_.transport_type == TransportType::StreamableHttp) {
    // HTTP client connection flow, the same for both HTTP transports:
    // 1. Resolve the server address
    // 2. Create a TCP socket and wrap it in a transport socket
    // 3. Apply the filter chain, which is where the two differ
    // 4. Connect asynchronously in the dispatcher thread
    //
    // All callbacks follow the dispatcher thread principle: onEvent() is
    // called in the dispatcher thread when the connection succeeds or
    // fails, every state transition happens there, and nothing needs
    // synchronizing because it is all one thread.

    std::string failure;
    auto connection =
        createHttpClientConnection(createFilterChainFactory(), failure);
    if (!connection) {
      Error err;
      err.code = -1;
      err.message = failure;
      return makeVoidError(err);
    }

    network::ClientConnection* client_conn = connection.get();
    active_connection_ = std::move(connection);

    // Added before the connect below, so the event that connect raises
    // is not raised at nobody.
    active_connection_->addConnectionCallbacks(*this);
    client_conn->connect();
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
  return sendRequest(request, {});
}

VoidResult McpConnectionManager::sendRequest(
    const jsonrpc::Request& request,
    const std::map<std::string, std::string>& http_headers) {
  if (!connected_ || !active_connection_) {
    Error err;
    err.code = -1;
    err.message = "Not connected";
    return makeVoidError(err);
  }

  // Convert to JSON using the bridge
  auto json_val = json::to_json(request);

  return sendJsonMessage(json_val, http_headers,
                         mcp::make_optional(request.id));
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

bool McpConnectionManager::sendSessionDelete() {
  assert(dispatcher_.isThreadSafe() && "sendSessionDelete off dispatcher");

  if (!config_.streamable_client_session ||
      !config_.streamable_client_session->hasId() || !active_connection_ ||
      !config_.current_http_headers) {
    return false;
  }

  // The one request this client sends that is about the conversation
  // rather than a message in it. It goes out the way every other
  // request does — the method travels on the same per-request header
  // map the session id does, because a write is all the codec gets.
  auto headers = config_.http_headers;
  config_.streamable_client_session->decorate(headers);
  headers[":method"] = "DELETE";
  *config_.current_http_headers = std::move(headers);

  GOPHER_LOG_DEBUG("Ending session {}",
                   config_.streamable_client_session->id());

  OwnedBuffer empty;
  active_connection_->write(empty, false);

  *config_.current_http_headers = config_.http_headers;

  // Nothing waits for the answer: whether the server ends the session,
  // says it does not allow that, or says it never had one, this client
  // is finished with it either way.
  config_.streamable_client_session->forget();
  return true;
}

bool McpConnectionManager::openServerStream(const std::string& last_event_id) {
  assert(dispatcher_.isThreadSafe() && "openServerStream off dispatcher");

  if (!config_.streamable_client_session ||
      config_.transport_type != TransportType::StreamableHttp) {
    return false;
  }

  // Whatever was there is replaced rather than joined: one stream at a
  // time, so that what the server pushes has one place to arrive.
  closeServerStream();

  // The stream's filters report through the same callbacks as the
  // request connection's, so a notification arriving here reaches the
  // application by the path everything else does. What differs is only
  // that nothing arriving here is answering a request of ours.
  auto factory = createFilterChainFactory();
  auto* http_factory =
      dynamic_cast<filter::HttpSseFilterChainFactory*>(factory.get());
  if (!http_factory) {
    return false;
  }
  stream_activity_ = std::make_shared<std::atomic<uint64_t>>(0);
  stream_activity_seen_ = 0;
  // Remembered so that a stream which opens here and says nothing can
  // still report where it had got to when it goes.
  stream_cursor_ = last_event_id;
  http_factory->setClientRole(filter::ClientConnectionRole::ServerStream);
  http_factory->setStreamActivityCounter(stream_activity_);

  std::string failure;
  auto connection = createHttpClientConnection(factory, failure);
  if (!connection) {
    GOPHER_LOG_WARN("Could not open a server stream: {}", failure);
    return false;
  }

  // The request that opens it goes out as soon as the connection is up,
  // and says three things: that a stream is what is wanted, which
  // session it belongs to, and where to carry on from.
  auto headers = config_.http_headers;
  config_.streamable_client_session->decorate(headers);
  headers[":method"] = "GET";
  headers[":accept"] = "text/event-stream";
  if (!last_event_id.empty()) {
    headers["Last-Event-ID"] = last_event_id;
  }

  class StreamOpener : public network::ConnectionCallbacks {
   public:
    StreamOpener(McpConnectionManager& manager,
                 network::Connection& connection,
                 std::map<std::string, std::string> headers)
        : manager_(manager),
          connection_(connection),
          headers_(std::move(headers)) {}

    void onEvent(network::ConnectionEvent event) override {
      if (event == network::ConnectionEvent::Connected) {
        // An empty write is how a request with no body reaches the
        // codec; what kind of request it is travels on the header map.
        if (manager_.config_.current_http_headers) {
          *manager_.config_.current_http_headers = headers_;
        }
        OwnedBuffer empty;
        connection_.write(empty, false);
        if (manager_.config_.current_http_headers) {
          *manager_.config_.current_http_headers =
              manager_.config_.http_headers;
        }
        return;
      }
      if (event == network::ConnectionEvent::RemoteClose ||
          event == network::ConnectionEvent::LocalClose) {
        // The filter on this connection reports where the stream had
        // got to; there is nothing to add here.
        manager_.stream_idle_timer_.reset();
      }
    }

    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

   private:
    McpConnectionManager& manager_;
    network::Connection& connection_;
    std::map<std::string, std::string> headers_;
  };

  network::ClientConnection* client_conn = connection.get();
  server_stream_connection_ = std::move(connection);
  // The one it replaces is kept a moment longer rather than destroyed
  // here: this can be reached from inside the old connection's own
  // callback dispatch, and the thing being destroyed would be what is
  // running.
  retired_opener_ = std::move(stream_opener_);
  stream_opener_.reset(
      new StreamOpener(*this, *client_conn, std::move(headers)));
  server_stream_connection_->addConnectionCallbacks(*stream_opener_);
  client_conn->connect();

  GOPHER_LOG_DEBUG("Opening a server stream{}",
                   last_event_id.empty()
                       ? std::string()
                       : std::string(" from ") + last_event_id);

  armStreamIdleWatchdog();
  return true;
}

void McpConnectionManager::closeServerStream() {
  stream_idle_timer_.reset();
  if (!server_stream_connection_) {
    return;
  }
  // Taken out of our hands before it is closed, because what tells a
  // close we asked for from one we did not is whether this is still the
  // stream by the time the event arrives. Without that, giving up a
  // stream in order to open a better one reads as the stream being lost,
  // and asks for it back — forever.
  auto going = std::move(server_stream_connection_);
  server_stream_connection_.reset();
  stream_activity_.reset();

  // Nothing is waiting on the way out: the stream is being given up, and
  // a flush would only delay that.
  going->close(network::ConnectionCloseType::NoFlush);
  dispatcher_.deferredDelete(std::move(going));
}

void McpConnectionManager::armStreamIdleWatchdog() {
  if (stream_idle_timeout_.count() <= 0) {
    return;
  }
  if (!stream_idle_timer_) {
    stream_idle_timer_ =
        dispatcher_.createTimer([this]() { onStreamIdleCheck(); });
  }
  stream_idle_timer_->enableTimer(stream_idle_timeout_);
}

void McpConnectionManager::onStreamIdleCheck() {
  if (!server_stream_connection_ || !stream_activity_) {
    return;
  }
  const uint64_t seen = stream_activity_->load(std::memory_order_relaxed);
  if (seen != stream_activity_seen_) {
    // Something arrived — a message, or the keep-alive that exists to
    // say exactly this. Start the window again.
    stream_activity_seen_ = seen;
    armStreamIdleWatchdog();
    return;
  }

  GOPHER_LOG_WARN("Server stream said nothing for {}ms; treating it as gone",
                  stream_idle_timeout_.count());
  // Closing it is what makes this reportable: the filter says where the
  // stream had got to on the way out, and coming back from silence is
  // then the same thing as coming back from a disconnect.
  closeServerStream();
}

void McpConnectionManager::close() {
  // The stream first: it is the one connection nothing is waiting on,
  // and leaving it open would have the client still reachable after it
  // has stopped listening.
  closeServerStream();

  // Close POST connection first (it may reference resources from main
  // connection)
  if (post_connection_) {
    if (post_callbacks_) {
      post_connection_->removeConnectionCallbacks(*post_callbacks_);
    }
    post_connection_->close(network::ConnectionCloseType::NoFlush);
    post_connection_.reset();
    post_callbacks_.reset();
  }

  // Close active connection if any
  if (active_connection_) {
    if (active_connection_->state() != network::ConnectionState::Open) {
      dispatcher_.deferredDelete(std::move(active_connection_));
    } else {
      // Use NoFlush to avoid triggering writes during shutdown
      // FlushWrite can cause SSL close_notify to be sent which may access
      // resources that are being destroyed
      //
      // Do not reset active_connection_ here. Connection::close() schedules the
      // actual close on the dispatcher, and onConnectionEvent(LocalClose) moves
      // active_connection_ into the deferred-delete queue after close callbacks
      // unwind. Resetting immediately destroys the transport before that posted
      // close runs.
      active_connection_->close(network::ConnectionCloseType::NoFlush);
    }
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

void McpConnectionManager::onRequestWithContext(
    const jsonrpc::Request& request, MessageDispatchContext& context) {
  if (protocol_callbacks_) {
    protocol_callbacks_->onRequestWithContext(request, context);
  }
}

void McpConnectionManager::onNotificationWithContext(
    const jsonrpc::Notification& notification,
    MessageDispatchContext& context) {
  if (protocol_callbacks_) {
    protocol_callbacks_->onNotificationWithContext(notification, context);
  }
  // HTTP 202 response is sent by HttpSseJsonRpcProtocolFilter::onNotification
}

void McpConnectionManager::onNotification(
    const jsonrpc::Notification& notification) {
  if (protocol_callbacks_) {
    protocol_callbacks_->onNotification(notification);
  }
  // HTTP 202 response is sent by HttpSseJsonRpcProtocolFilter::onNotification
}

void McpConnectionManager::onResponse(const jsonrpc::Response& response) {
  std::string id_str;
  if (holds_alternative<std::string>(response.id)) {
    id_str = get<std::string>(response.id);
  } else {
    id_str = std::to_string(get<int64_t>(response.id));
  }
  GOPHER_LOG_DEBUG("McpConnectionManager::onResponse id={}, has_error={}",
                   id_str, response.error.has_value());

  if (protocol_callbacks_) {
    protocol_callbacks_->onResponse(response);
  }
}

void McpConnectionManager::onConnectionEvent(network::ConnectionEvent event) {
  // Connection events are raised from the connection's own callback loop,
  // which runs on the dispatcher thread. Anything else would mean a caller
  // synthesised the event off-thread — a bug, not a transport event.
  assert(dispatcher_.isThreadSafe() && "onConnectionEvent off dispatcher");

  const char* event_name = "unknown";
  switch (event) {
    case network::ConnectionEvent::Connected:
      event_name = "Connected";
      break;
    case network::ConnectionEvent::ConnectedZeroRtt:
      event_name = "ConnectedZeroRtt";
      break;
    case network::ConnectionEvent::RemoteClose:
      event_name = "RemoteClose";
      break;
    case network::ConnectionEvent::LocalClose:
      event_name = "LocalClose";
      break;
  }
  GOPHER_LOG_DEBUG(
      "McpConnectionManager::onConnectionEvent event={}, is_server={}",
      event_name, is_server_);

  // Handle connection state transitions
  // All events are invoked in dispatcher thread context
  if (event == network::ConnectionEvent::Connected) {
    // IMPORTANT: Return early if already connected to prevent infinite
    // recursion. The transport layer may raise additional Connected events as
    // each layer completes its handshake (TCP -> SSL -> HTTP). We only process
    // the first one.
    if (connected_) {
      GOPHER_LOG_DEBUG(
          "McpConnectionManager::onConnectionEvent - already connected, "
          "ignoring duplicate Connected event");
      return;
    }
    // Connection established successfully
    connected_ = true;

    // Ensure connection state is fully propagated
    dispatcher_.post([]() {
      // Connection state verification completed
    });

    // TRANSPORT NOTIFICATION: Notify HTTP/SSE transport about TCP connection
    // Flow: TCP connected → ConnectionEvent::Connected →
    // transport.onConnected() Why: The HTTP/SSE transport needs to know when
    // the underlying TCP connection is established so it can begin the HTTP
    // handshake. This is different from stdio transport which has pre-connected
    // pipes. The transport's state machine expects this call to transition from
    // TcpConnecting → TcpConnected. Note: ConnectionImpl already called
    // transport->connect() before TCP connect, so the transport is in
    // TcpConnecting state waiting for this notification.
    // IMPORTANT: Use re-entrancy guard to prevent infinite recursion.
    // The transport's onConnected() may raise another Connected event.
    if (config_.transport_type == TransportType::HttpSse &&
        active_connection_ && !processing_connected_event_) {
      processing_connected_event_ = true;
      auto& transport = active_connection_->transportSocket();
      transport.onConnected();
      processing_connected_event_ = false;
    }
  } else if (event == network::ConnectionEvent::RemoteClose ||
             event == network::ConnectionEvent::LocalClose) {
    // Guard against duplicate close events - the transport stack may raise
    // LocalClose from multiple layers (TCP, SSL, HTTP). Only process once.
    if (!connected_ && !active_connection_) {
      GOPHER_LOG_DEBUG(
          "McpConnectionManager::onConnectionEvent - ignoring duplicate close "
          "event");
      return;
    }

    // Connection closed - clean up state.
    // We are being called from within the connection's callback loop
    // (raiseConnectionEvent). Destroying the connection synchronously here
    // would be a use-after-free once the callback loop resumes. Hand it to
    // the dispatcher's deferred-delete queue instead — Connection derives
    // from DeferredDeletable, so the dispatcher owns teardown on the next
    // event-loop iteration, after all in-flight callbacks unwind.
    connected_ = false;
    if (active_connection_) {
      dispatcher_.deferredDelete(std::move(active_connection_));
    }

    // The requests still outstanding go with the connection: the retry
    // and deadline machinery owns them from here. Keeping their places
    // would name the wrong request for the first answer on the next
    // connection, which is worse than naming none.
    if (config_.streamable_client_session) {
      config_.streamable_client_session->forgetInFlight();
    }
  }

  // Forward event to upper layer callbacks
  GOPHER_LOG_DEBUG(
      "McpConnectionManager forwarding event to protocol_callbacks_={}",
      (protocol_callbacks_ ? "set" : "NULL"));
  const bool server_close_event =
      is_server_ && (event == network::ConnectionEvent::RemoteClose ||
                     event == network::ConnectionEvent::LocalClose);
  if (protocol_callbacks_ && !server_close_event) {
    GOPHER_LOG_DEBUG(
        "McpConnectionManager calling protocol_callbacks_->onConnectionEvent");
    protocol_callbacks_->onConnectionEvent(event);
    GOPHER_LOG_DEBUG(
        "McpConnectionManager protocol_callbacks_->onConnectionEvent returned");

    // Ensure protocol callbacks are processed before any requests
    if (event == network::ConnectionEvent::Connected) {
      dispatcher_.post([]() {
        // Connection event processing completed
      });
    }
  }
}

void McpConnectionManager::onError(const Error& error) {
  if (protocol_callbacks_) {
    protocol_callbacks_->onError(error);
  }
}

void McpConnectionManager::onClientStreamEvent(
    ClientStreamEvent event,
    const optional<RequestId>& request_id,
    const std::string& last_event_id) {
  // A stream this layer let go of is not news. Only a stream that is
  // still ours when it reports its own closing was lost rather than
  // given up.
  if (event == ClientStreamEvent::Closed && !server_stream_connection_) {
    return;
  }

  // A stream that was opened at a cursor and said nothing has still got
  // to that cursor. Reporting nowhere would have the next attempt ask
  // for the whole stream again, and be given events it already had.
  std::string reached = last_event_id;
  if (event == ClientStreamEvent::Closed && reached.empty()) {
    reached = stream_cursor_;
  }

  // A stream that closed is not this layer's to reopen: whether it is
  // worth asking for again, and how long to wait first, are decisions
  // about the conversation rather than about the socket.
  if (protocol_callbacks_) {
    protocol_callbacks_->onClientStreamEvent(event, request_id, reached);
  }
}

void McpConnectionManager::onTransportStatus(
    int status_code,
    const optional<RequestId>& request_id,
    const std::string& detail) {
  // Passed straight through: what a status means for a request is a
  // question about requests, and they are kept a layer up.
  if (protocol_callbacks_) {
    protocol_callbacks_->onTransportStatus(status_code, request_id, detail);
  }
}

std::string McpConnectionManager::resolveEndpointUrl(
    const std::string& endpoint,
    const std::string& server_address,
    bool use_ssl) {
  if (endpoint.find("://") != std::string::npos) {
    return endpoint;  // Already absolute.
  }
  if (server_address.empty()) {
    return endpoint;  // Nothing to resolve against.
  }
  std::string path = endpoint;
  if (path.empty() || path[0] != '/') {
    path = "/" + path;
  }
  return (use_ssl ? "https://" : "http://") + server_address + path;
}

void McpConnectionManager::onMessageEndpoint(const std::string& endpoint) {
  GOPHER_LOG_DEBUG("McpConnectionManager::onMessageEndpoint endpoint={}",
                   endpoint);
  // The server announces a relative callback URL unless an external URL is
  // configured; sendHttpPost needs an absolute one. Resolve against the
  // same server the SSE stream is connected to.
  std::string server_address;
  bool use_ssl = false;
  if (config_.http_sse_config.has_value()) {
    server_address = config_.http_sse_config->server_address;
    use_ssl = config_.http_sse_config->ssl_config.has_value();
  }
  message_endpoint_ = resolveEndpointUrl(endpoint, server_address, use_ssl);
  if (message_endpoint_ != endpoint) {
    GOPHER_LOG_DEBUG("Resolved relative endpoint '{}' to '{}'", endpoint,
                     message_endpoint_);
  }
  has_message_endpoint_ = true;

  // Forward to protocol callbacks if set. Forward the resolved form so
  // upper layers never see a relative URL they cannot POST to.
  if (protocol_callbacks_ && protocol_callbacks_ != this) {
    protocol_callbacks_->onMessageEndpoint(message_endpoint_);
  }
}

bool McpConnectionManager::sendHttpPost(const std::string& json_body) {
  return sendHttpPost(json_body, {});
}

bool McpConnectionManager::sendHttpPost(
    const std::string& json_body,
    const std::map<std::string, std::string>& http_headers) {
  GOPHER_LOG_DEBUG(
      "McpConnectionManager::sendHttpPost endpoint={}, body_len={}",
      message_endpoint_, json_body.length());

  if (!has_message_endpoint_) {
    GOPHER_LOG_ERROR("McpConnectionManager: No message endpoint available");
    return false;
  }

  // Parse endpoint URL to get host, port, path
  // Format: https://host:port/path or http://host:port/path
  std::string host;
  uint16_t port = 443;
  std::string path;
  bool use_ssl = true;

  size_t proto_end = message_endpoint_.find("://");
  if (proto_end == std::string::npos) {
    GOPHER_LOG_ERROR("McpConnectionManager: Invalid endpoint URL");
    return false;
  }

  std::string proto = message_endpoint_.substr(0, proto_end);
  if (proto == "http") {
    use_ssl = false;
    port = 80;
  }

  size_t host_start = proto_end + 3;
  size_t path_start = message_endpoint_.find('/', host_start);
  if (path_start == std::string::npos) {
    path = "/";
    host = message_endpoint_.substr(host_start);
  } else {
    path = message_endpoint_.substr(path_start);
    host = message_endpoint_.substr(host_start, path_start - host_start);
  }

  // Check for port in host
  size_t port_pos = host.find(':');
  if (port_pos != std::string::npos) {
    port = static_cast<uint16_t>(std::stoi(host.substr(port_pos + 1)));
    host = host.substr(0, port_pos);
  }

  GOPHER_LOG_DEBUG(
      "McpConnectionManager: POST to host={}, port={}, path={}, ssl={}", host,
      port, path, use_ssl);

  // Resolve hostname
  std::string ip_address = resolveHostname(host);
  if (ip_address.empty()) {
    GOPHER_LOG_ERROR("McpConnectionManager: Failed to resolve hostname: {}",
                     host);
    return false;
  }

  // Create HTTP POST request manually
  std::ostringstream request;
  request << "POST " << path << " HTTP/1.1\r\n";
  request << "Host: " << host << "\r\n";
  request << "Content-Type: application/json\r\n";
  request << "Content-Length: " << json_body.length() << "\r\n";
  request << "Connection: close\r\n";  // One-shot connection
  std::map<std::string, std::string> merged_headers =
      config_.current_http_headers ? *config_.current_http_headers
                                   : config_.http_headers;
  for (const auto& header : http_headers) {
    merged_headers[header.first] = header.second;
  }
  for (const auto& header : merged_headers) {
    if (!filter::isValidClientHeader(header.first, header.second) ||
        filter::isGeneratedClientHeader(header.first)) {
      continue;
    }
    request << header.first << ": " << header.second << "\r\n";
  }
  request << "\r\n";
  request << json_body;

  std::string request_str = request.str();
  GOPHER_LOG_TRACE(
      "McpConnectionManager: HTTP POST request (first 300 chars): {}",
      request_str.substr(0, 300));

  // Create address
  auto address =
      std::make_shared<network::Address::Ipv4Instance>(ip_address, port);

  // Create stream info
  auto stream_info = stream_info::StreamInfoImpl::create();

  // Create transport socket using the same factory as the main connection
  // This ensures proper TCP+SSL handling
  auto transport_factory = createTransportSocketFactory();
  if (!transport_factory) {
    GOPHER_LOG_ERROR(
        "McpConnectionManager: Failed to create transport factory");
    return false;
  }

  auto* client_factory = dynamic_cast<network::ClientTransportSocketFactory*>(
      transport_factory.get());
  if (!client_factory) {
    GOPHER_LOG_ERROR(
        "McpConnectionManager: Transport factory doesn't support client "
        "connections");
    return false;
  }
  auto transport_socket = client_factory->createTransportSocket(nullptr);

  // Create TCP socket using MCP socket interface (same pattern as connect())
  auto local_address =
      network::Address::anyAddress(network::Address::IpVersion::v4, 0);

  auto socket_result = socket_interface_.socket(
      network::SocketType::Stream, network::Address::Type::Ip,
      network::Address::IpVersion::v4, false);

  if (!socket_result.ok()) {
    GOPHER_LOG_ERROR("McpConnectionManager: Failed to create socket");
    return false;
  }

  // Create IO handle wrapper for the socket
  auto io_handle = socket_interface_.ioHandleForFd(*socket_result.value, false);
  if (!io_handle) {
    socket_interface_.close(*socket_result.value);
    GOPHER_LOG_ERROR("McpConnectionManager: Failed to create IO handle");
    return false;
  }

  // Create ConnectionSocket wrapper
  auto socket_wrapper = std::make_unique<network::ConnectionSocketImpl>(
      std::move(io_handle), local_address, address);

  // Set socket to non-blocking mode
  socket_wrapper->ioHandle().setBlocking(false);

  // Create the connection (same pattern as connect())
  auto post_connection = std::make_unique<network::ConnectionImpl>(
      dispatcher_, std::move(socket_wrapper), std::move(transport_socket),
      false);  // Not yet connected

  auto* post_conn_ptr = post_connection.get();

  // Simple connection callback that writes the request after connect
  class PostConnectionCallbacks : public network::ConnectionCallbacks {
   public:
    PostConnectionCallbacks(const std::string& request,
                            network::Connection* conn)
        : request_(request), connection_(conn) {}

    void onEvent(network::ConnectionEvent event) override {
      GOPHER_LOG_DEBUG("PostConnection onEvent: {}", static_cast<int>(event));
      if (event == network::ConnectionEvent::Connected) {
        GOPHER_LOG_DEBUG("PostConnection connected, sending POST request");
        OwnedBuffer buffer;
        buffer.add(request_);
        connection_->write(buffer, false);
      } else if (event == network::ConnectionEvent::RemoteClose ||
                 event == network::ConnectionEvent::LocalClose) {
        GOPHER_LOG_DEBUG("PostConnection connection closed");
        // Connection closed - this is expected after we get the response
      }
    }

    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

   private:
    std::string request_;
    network::Connection* connection_;
  };

  // Clean up any previous POST connection
  post_connection_.reset();
  post_callbacks_.reset();

  // Store callbacks as member to keep alive
  post_callbacks_ =
      std::make_unique<PostConnectionCallbacks>(request_str, post_conn_ptr);
  post_connection->addConnectionCallbacks(*post_callbacks_);

  // CRITICAL FIX: Initialize the filter manager for the POST connection.
  // Without this, the filter manager is in an uninitialized state and
  // onRead() returns early without processing, but subsequent code paths
  // may still access connection state that hasn't been properly set up,
  // leading to crashes. Even though we don't need to parse the HTTP response
  // (it's just a 200 OK acknowledgment), we need the filter manager initialized
  // for the connection to function correctly.
  auto* conn_base =
      dynamic_cast<network::ConnectionImplBase*>(post_connection.get());
  if (conn_base) {
    conn_base->filterManager().initializeReadFilters();
  }

  // Store the connection as member (keeps it alive)
  post_connection_ = std::unique_ptr<network::ClientConnection>(
      static_cast<network::ClientConnection*>(post_connection.release()));

  // Initiate connection
  GOPHER_LOG_DEBUG("McpConnectionManager: Initiating POST connection");
  post_connection_->connect();

  return true;
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
    case TransportType::StreamableHttp:
      // Check if SSL is needed for HTTPS
      if (config_.http_sse_config.has_value() &&
          config_.http_sse_config.value().underlying_transport ==
              transport::HttpSseTransportSocketConfig::UnderlyingTransport::
                  SSL) {
        // Use HTTPS transport factory for SSL connections
        return transport::createHttpsSseTransportFactory(
            config_.http_sse_config.value(), dispatcher_);
      }
      // For HTTP without SSL, use RawBufferTransportSocketFactory
      // The filter chain handles the HTTP protocol
      // The transport socket only handles raw buffer I/O
      return std::make_unique<network::RawBufferTransportSocketFactory>();

    case TransportType::WebSocket:
      // WebSocket not yet implemented
      break;

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
        dispatcher_, *this, is_server_, config_.http_path, config_.http_host,
        true /* use_sse */, "/sse", "/mcp", "", config_.http_headers,
        config_.current_http_headers);

  } else if (config_.transport_type == TransportType::StreamableHttp) {
    // Streamable HTTP: Simple POST request/response pattern
    // [TCP] → [HTTP+JSON-RPC Filter] → [Application]
    //
    // No SSE event stream - direct HTTP POST with JSON-RPC body
    // Response is JSON-RPC in HTTP response body

    auto factory = std::make_shared<filter::HttpSseFilterChainFactory>(
        dispatcher_, *this, is_server_, config_.http_path, config_.http_host,
        false /* use_sse */, "/sse", "/mcp", "", config_.http_headers,
        config_.current_http_headers);
    factory->setClientSession(config_.streamable_client_session);
    return factory;

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
  return sendJsonMessage(message, {}, optional<RequestId>());
}

VoidResult McpConnectionManager::sendJsonMessage(
    const json::JsonValue& message,
    const std::map<std::string, std::string>& http_headers) {
  return sendJsonMessage(message, http_headers, optional<RequestId>());
}

VoidResult McpConnectionManager::sendJsonMessage(
    const json::JsonValue& message,
    const std::map<std::string, std::string>& http_headers,
    const optional<RequestId>& correlate) {
  GOPHER_LOG_DEBUG(
      "McpConnectionManager::sendJsonMessage called, connected={}, conn={}",
      connected_, (void*)active_connection_.get());

  // Convert to string
  std::string json_str = message.toString();
  GOPHER_LOG_TRACE(
      "McpConnectionManager: JSON message (first 200 chars): {}...",
      json_str.substr(0, 200));

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

  // Post write to dispatcher thread to ensure thread safety
  // The write() call must happen on the dispatcher thread
  // We capture `this` to check if connection is still valid when callback runs
  dispatcher_.post([this, json_str = std::move(json_str), http_headers,
                    correlate]() {
    // Check if connection is still valid - it may have been closed
    if (!active_connection_) {
      GOPHER_LOG_DEBUG(
          "McpConnectionManager: Write skipped - connection already closed");
      return;
    }

    GOPHER_LOG_DEBUG(
        "McpConnectionManager write callback executing, conn={}, msg_len={}",
        (void*)active_connection_.get(), json_str.length());

    bool reset_current_http_headers = false;
    if (config_.current_http_headers) {
      auto merged_headers = config_.http_headers;
      // What the session is holding, which is nothing until the server
      // has named one and nothing again once it says it has forgotten
      // it. The initialize request carries neither header for that
      // reason alone, without anything here asking which request this is.
      if (config_.streamable_client_session) {
        config_.streamable_client_session->decorate(merged_headers);
      }
      for (const auto& header : http_headers) {
        merged_headers[header.first] = header.second;
      }
      *config_.current_http_headers = std::move(merged_headers);
      reset_current_http_headers = true;
    }

    // Note this message against the answer it will draw, here rather
    // than at the call site: writes posted from several threads reach
    // the wire in the order their posts run, and that is the order the
    // responses come back in.
    if (config_.streamable_client_session) {
      config_.streamable_client_session->recordSent(correlate);
    }

    // Create buffer with JSON payload
    OwnedBuffer buffer;
    buffer.add(json_str);

    // Write through filter chain - each filter handles its protocol layer:
    // - JSON-RPC filter: message framing if configured
    // - SSE filter: SSE event formatting if applicable
    // - HTTP filter: HTTP request/response formatting if applicable
    // - Transport socket: raw I/O only
    active_connection_->write(buffer, false);

    if (reset_current_http_headers && config_.current_http_headers) {
      *config_.current_http_headers = config_.http_headers;
    }

    GOPHER_LOG_DEBUG("McpConnectionManager write completed");
  });

  return makeVoidSuccess();
}

}  // namespace mcp
