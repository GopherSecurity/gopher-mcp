#include <errno.h>
#include <iostream>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>

#include "mcp/core/result.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/listener.h"
#include "mcp/network/socket.h"
#include "mcp/network/socket_impl.h"
#include "mcp/stream_info/stream_info_impl.h"

namespace mcp {
namespace network {

// ConnectionSocketImpl is defined in socket_impl.h

// Forward declaration
class ActiveListener;

/**
 * Context for processing listener filter chain
 */
class ListenerFilterContext : public ListenerFilterCallbacks {
 public:
  ListenerFilterContext(ActiveListener& listener,
                        ConnectionSocketPtr&& socket,
                        const std::vector<ListenerFilterPtr>& filters)
      : listener_(listener),
        socket_(std::move(socket)),
        filters_(filters),
        dispatcher_(listener.dispatcher()) {}

  // Start processing the filter chain
  void startFilterChain() { processNextFilter(); }

  // ListenerFilterCallbacks implementation
  ConnectionSocket& socket() override { return *socket_; }

  event::Dispatcher& dispatcher() override { return dispatcher_; }

  void continueFilterChain(bool success) override {
    if (!success) {
      // Filter rejected the connection
      socket_.reset();
      return;
    }

    // Continue to next filter
    processNextFilter();
  }

 private:
  void processNextFilter() {
    if (current_filter_index_ >= filters_.size()) {
      // All filters passed, create the connection
      listener_.createConnection(std::move(socket_));
      return;
    }

    // Process current filter
    auto& filter = filters_[current_filter_index_];
    current_filter_index_++;

    auto status = filter->onAccept(*this);

    if (status == ListenerFilterStatus::Continue) {
      // Continue to next filter immediately
      processNextFilter();
    }
    // If StopIteration, wait for continueFilterChain() to be called
  }

  ActiveListener& listener_;
  ConnectionSocketPtr socket_;
  const std::vector<ListenerFilterPtr>& filters_;
  event::Dispatcher& dispatcher_;
  size_t current_filter_index_{0};
};

// ActiveListener implementation

ActiveListener::ActiveListener(event::Dispatcher& dispatcher,
                               SocketInterface& socket_interface,
                               ListenerCallbacks& parent_callbacks,
                               ListenerConfig&& config)
    : dispatcher_(dispatcher),
      socket_interface_(socket_interface),
      parent_callbacks_(parent_callbacks),
      config_(std::move(config)) {
  // Add listener tags
  tags_.push_back("listener");
  tags_.push_back(config_.name);
}

ActiveListener::~ActiveListener() { disable(); }

VoidResult ActiveListener::listen() {
  // Create socket
  if (config_.bind_to_port) {
    // Use the global createListenSocket function
    SocketCreationOptions socket_opts;
    socket_opts.non_blocking = true;
    socket_opts.close_on_exec = true;
    socket_opts.reuse_address = true;  // Essential for server sockets

    auto socket = createListenSocket(
        config_.address,
        socket_opts,
        config_.bind_to_port);

    if (!socket) {
      Error err;
      err.code = -1;
      err.message = "Failed to create listen socket";
      return makeVoidError(err);
    }

    socket_ = std::move(socket);

    // Call listen() to start accepting connections
    auto listen_result =
        static_cast<ListenSocketImpl*>(socket_.get())->listen(config_.backlog);
    if (!listen_result.ok()) {
      Error err;
      err.code = listen_result.error_code();
      err.message = "Failed to listen on socket";
      return makeVoidError(err);
    }

    // Apply socket options
    if (config_.socket_options) {
      for (const auto& option : *config_.socket_options) {
        if (!option->setOption(*socket_)) {
          Error err;
          err.code = -1;
          err.message = "Failed to set socket option";
          return makeVoidError(err);
        }
      }
    }

    // Set SO_REUSEADDR
    int val = 1;
    socket_->setSocketOption(SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // Set SO_REUSEPORT if requested
    if (config_.enable_reuse_port) {
      int val = 1;
      socket_->setSocketOption(SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val));
    }

    // createListenSocket already binds and listens if bind_to_port is true,
    // so we don't need to do it again
  }

  // Create file event for accept
  file_event_ = dispatcher_.createFileEvent(
      socket_->ioHandle().fd(),
      [this](uint32_t events) { onSocketEvent(events); },
      event::PlatformDefaultTriggerType,  // Use platform-specific default
      static_cast<uint32_t>(event::FileReadyType::Closed));

  if (enabled_) {
    file_event_->setEnabled(static_cast<uint32_t>(event::FileReadyType::Read));
  }

  return makeVoidSuccess();
}

void ActiveListener::disable() {
  enabled_ = false;
  if (file_event_) {
    file_event_->setEnabled(0);
  }
}

void ActiveListener::enable() {
  enabled_ = true;
  if (file_event_) {
    file_event_->setEnabled(static_cast<uint32_t>(event::FileReadyType::Read));
  }
}

void ActiveListener::onAccept(ConnectionSocketPtr&& socket) {
  // Run through listener filters
  runListenerFilters(std::move(socket));
}

void ActiveListener::onNewConnection(ConnectionPtr&& connection) {
  num_connections_++;
  parent_callbacks_.onNewConnection(std::move(connection));
}

void ActiveListener::onSocketEvent(uint32_t events) {
  if (events & static_cast<uint32_t>(event::FileReadyType::Read)) {
    doAccept();
  }
}

void ActiveListener::doAccept() {
  // Accept loop - accept as many connections as possible
  while (true) {
    sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);

    auto accept_result =
        socket_interface_.accept(socket_->ioHandle().fd(),
                                 reinterpret_cast<sockaddr*>(&addr), &addr_len);

    if (!accept_result.ok()) {
      if (accept_result.error_code() == EAGAIN ||
          accept_result.error_code() == EWOULDBLOCK) {
        // No more connections to accept
        break;
      } else if (accept_result.error_code() == EMFILE ||
                 accept_result.error_code() == ENFILE) {
        // Out of file descriptors
        // TODO: Log error and potentially disable listener temporarily
        break;
      } else {
        // Other error, log and continue
        continue;
      }
    }

    // Create socket from accepted fd
    auto io_handle = socket_interface_.ioHandleForFd(*accept_result);
    if (!io_handle) {
      socket_interface_.close(*accept_result);
      continue;
    }

    // Create address from sockaddr
    auto remote_address = Address::addressFromSockAddr(addr, addr_len);

    // Create a ConnectionInfoSetter implementation with addresses
    auto local_address = socket_->connectionInfoProvider().localAddress();
    auto connection_info = std::make_shared<ConnectionInfoSetterImpl>(
        local_address, remote_address);

    // Create socket wrapper - Note: SocketImpl is abstract, we need
    // ConnectionSocketImpl
    auto accepted_socket = std::make_unique<ConnectionSocketImpl>(
        std::move(io_handle), local_address, remote_address);

    // Set socket to non-blocking
    accepted_socket->setBlocking(false);

    // Apply socket options
    if (config_.socket_options) {
      for (const auto& option : *config_.socket_options) {
        option->setOption(*accepted_socket);
      }
    }

    // The accepted_socket is already a ConnectionSocketImpl, just move it
    auto connection_socket = std::move(accepted_socket);

    // Remote address is already set in the socket

    // Process through callbacks
    onAccept(std::move(connection_socket));
  }
}

void ActiveListener::createConnection(ConnectionSocketPtr&& socket) {
  // Create stream info for the connection
  auto stream_info = stream_info::StreamInfoImpl::create();

  // Create transport socket
  TransportSocketPtr transport_socket;
  if (config_.transport_socket_factory) {
    auto options = std::make_unique<TransportSocketOptionsImpl>();
    transport_socket =
        config_.transport_socket_factory->createTransportSocket();
  } else {
    // Create default plaintext transport socket
    // This would be implemented in a real system
    return;
  }

  // Create server connection using the accepted socket
  // Flow: Accept socket -> Create transport -> Create ConnectionImpl ->
  // Initialize filters The ConnectionImpl takes ownership of the socket and
  // manages its lifecycle

  // For server connections, we pass the socket directly to ConnectionImpl
  // The socket is already non-blocking and configured with proper options
  auto connection = ConnectionImpl::createServerConnection(
      dispatcher_,
      std::move(socket),  // Pass the ConnectionSocketPtr directly
      std::move(transport_socket), *stream_info);

  // Set buffer limits
  connection->setBufferLimits(config_.per_connection_buffer_limit);

  // Add filter chain and initialize filters
  if (config_.filter_chain_factory) {
    // Cast to ConnectionImplBase to access filter manager
    auto* conn_impl_base = dynamic_cast<ConnectionImplBase*>(connection.get());
    if (conn_impl_base) {
      config_.filter_chain_factory->createFilterChain(
          conn_impl_base->filterManager());
      conn_impl_base->filterManager().initializeReadFilters();
    }
  }

  // Notify about new connection
  // This calls McpConnectionManager::onNewConnection for server-side handling
  onNewConnection(std::move(connection));
}

void ActiveListener::runListenerFilters(ConnectionSocketPtr&& socket) {
  if (config_.listener_filters.empty()) {
    // No filters, create connection directly
    createConnection(std::move(socket));
    return;
  }

  // Create filter chain context for this connection
  auto filter_context = std::make_unique<ListenerFilterContext>(
      *this, std::move(socket), config_.listener_filters);

  // Start processing the filter chain
  filter_context->startFilterChain();

  // Store the context (it will be removed when processing completes)
  pending_filter_contexts_.push_back(std::move(filter_context));
}

// ListenerManagerImpl implementation

ListenerManagerImpl::ListenerManagerImpl(event::Dispatcher& dispatcher,
                                         SocketInterface& socket_interface)
    : dispatcher_(dispatcher), socket_interface_(socket_interface) {}

ListenerManagerImpl::~ListenerManagerImpl() { stopListeners(); }

VoidResult ListenerManagerImpl::addListener(ListenerConfig&& config,
                                            ListenerCallbacks& callbacks) {
  // Check if listener already exists
  if (listeners_.find(config.name) != listeners_.end()) {
    Error err;
    err.code = -1;
    err.message = "Listener already exists: " + config.name;
    return makeVoidError(err);
  }

  // Store the name before moving the config
  std::string listener_name = config.name;

  // Create listener
  auto listener = std::make_unique<ActiveListener>(
      dispatcher_, socket_interface_, callbacks, std::move(config));

  // Start listening
  auto result = listener->listen();
  if (mcp::holds_alternative<Error>(result)) {
    return result;
  }

  // Add to map
  listeners_[listener_name] = std::move(listener);

  return makeVoidSuccess();
}

void ListenerManagerImpl::removeListener(const std::string& name) {
  auto it = listeners_.find(name);
  if (it != listeners_.end()) {
    it->second->disable();
    listeners_.erase(it);
  }
}

Listener* ListenerManagerImpl::getListener(const std::string& name) {
  auto it = listeners_.find(name);
  if (it != listeners_.end()) {
    return it->second.get();
  }
  return nullptr;
}

std::vector<std::reference_wrapper<Listener>>
ListenerManagerImpl::getAllListeners() {
  std::vector<std::reference_wrapper<Listener>> result;
  for (auto& pair : listeners_) {
    // Cast to base class reference explicitly
    result.push_back(std::ref(static_cast<Listener&>(*pair.second)));
  }
  return result;
}

void ListenerManagerImpl::stopListeners() {
  for (auto& pair : listeners_) {
    pair.second->disable();
  }
  listeners_.clear();
}

}  // namespace network
}  // namespace mcp