#include "mcp/network/listener.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket.h"
#include "mcp/network/socket_impl.h"
#include "mcp/stream_info/stream_info_impl.h"
#include "mcp/result.h"
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace mcp {
namespace network {

// ConnectionSocketImpl is defined in socket_impl.h

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

ActiveListener::~ActiveListener() {
  disable();
}

VoidResult ActiveListener::listen() {
  // Create socket
  if (config_.bind_to_port) {
    // Use the global createListenSocket function
    auto socket = createListenSocket(
        config_.address,
        SocketCreationOptions{
            .non_blocking = true,
            .close_on_exec = true
        },
        config_.bind_to_port);
    
    if (!socket) {
      Error err;
      err.code = -1;
      err.message = "Failed to create listen socket";
      return makeVoidError(err);
    }
    
    socket_ = std::move(socket);
    
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
      event::FileTriggerType::Edge,
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
    
    auto accept_result = socket_interface_.accept(
        socket_->ioHandle().fd(),
        reinterpret_cast<sockaddr*>(&addr),
        &addr_len);
    
    if (!accept_result.ok()) {
      if (accept_result.error_code() == EAGAIN || accept_result.error_code() == EWOULDBLOCK) {
        // No more connections to accept
        break;
      } else if (accept_result.error_code() == EMFILE || accept_result.error_code() == ENFILE) {
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
    
    // Create socket wrapper - Note: SocketImpl is abstract, we need ConnectionSocketImpl
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
    transport_socket = config_.transport_socket_factory->createTransportSocket();
  } else {
    // Create default plaintext transport socket
    // This would be implemented in a real system
    return;
  }
  
  // Extract the underlying socket
  auto* socket_impl = dynamic_cast<ConnectionSocketImpl*>(socket.get());
  if (!socket_impl) {
    return;
  }
  
  // Move the socket out of the wrapper
  // Note: This is a simplification - in real implementation we'd have proper move semantics
  SocketPtr raw_socket;
  // raw_socket = std::move(socket_impl->socket_);
  
  // Create server connection
  auto connection = ConnectionImpl::createServerConnection(
      dispatcher_,
      std::move(raw_socket),
      std::move(transport_socket),
      *stream_info);
  
  // Set buffer limits
  connection->setBufferLimits(config_.per_connection_buffer_limit);
  
  // Add filter chain
  if (config_.filter_chain_factory) {
    // TODO: Apply filter chain to connection's filter manager
    // config_.filter_chain_factory->createFilterChain(connection->filterManager());
  }
  
  // TODO: Initialize read filters on the filter manager
  // connection->filterManager().initializeReadFilters();
  
  // Notify about new connection
  onNewConnection(std::move(connection));
}

void ActiveListener::runListenerFilters(ConnectionSocketPtr&& socket) {
  if (config_.listener_filters.empty()) {
    // No filters, create connection directly
    createConnection(std::move(socket));
    return;
  }
  
  // TODO: Implement listener filter chain processing
  // For now, skip filters and create connection directly
  createConnection(std::move(socket));
}

// ListenerManagerImpl implementation

ListenerManagerImpl::ListenerManagerImpl(event::Dispatcher& dispatcher,
                                         SocketInterface& socket_interface)
    : dispatcher_(dispatcher), socket_interface_(socket_interface) {}

ListenerManagerImpl::~ListenerManagerImpl() {
  stopListeners();
}

VoidResult ListenerManagerImpl::addListener(ListenerConfig&& config,
                                              ListenerCallbacks& callbacks) {
  // Check if listener already exists
  if (listeners_.find(config.name) != listeners_.end()) {
    Error err;
    err.code = -1;
    err.message = "Listener already exists: " + config.name;
    return makeVoidError(err);
  }
  
  // Create listener
  auto listener = std::make_unique<ActiveListener>(
      dispatcher_, socket_interface_, callbacks, std::move(config));
  
  // Start listening
  auto result = listener->listen();
  if (result.holds_alternative<Error>()) {
    return result;
  }
  
  // Store the name before moving the config
  std::string listener_name = config.name;
  
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

std::vector<std::reference_wrapper<Listener>> ListenerManagerImpl::getAllListeners() {
  std::vector<std::reference_wrapper<Listener>> result;
  for (auto& pair : listeners_) {
    result.push_back(std::ref(*pair.second));
  }
  return result;
}

void ListenerManagerImpl::stopListeners() {
  for (auto& pair : listeners_) {
    pair.second->disable();
  }
  listeners_.clear();
}

} // namespace network
} // namespace mcp