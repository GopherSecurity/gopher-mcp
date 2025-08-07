#include "mcp/network/connection_manager.h"
#include <sys/socket.h>
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/stream_info/stream_info_impl.h"
#include <algorithm>

namespace mcp {
namespace network {

// ConnectionHandlerImpl implementation

ConnectionHandlerImpl::ConnectionHandlerImpl(event::Dispatcher& dispatcher,
                                             SocketInterface& socket_interface)
    : dispatcher_(dispatcher), socket_interface_(socket_interface) {}

ConnectionHandlerImpl::~ConnectionHandlerImpl() {
  stopListeners();
}

void ConnectionHandlerImpl::addListener(ListenerPtr&& listener) {
  listeners_.push_back(std::move(listener));
}

void ConnectionHandlerImpl::removeListener(const std::string& name) {
  listeners_.erase(
      std::remove_if(listeners_.begin(), listeners_.end(),
                     [&name](const ListenerPtr& listener) {
                       return listener->name() == name;
                     }),
      listeners_.end());
}

void ConnectionHandlerImpl::stopListeners() {
  for (auto& listener : listeners_) {
    listener->disable();
  }
  listeners_.clear();
}

void ConnectionHandlerImpl::disableListeners() {
  listeners_disabled_ = true;
  for (auto& listener : listeners_) {
    listener->disable();
  }
}

void ConnectionHandlerImpl::enableListeners() {
  listeners_disabled_ = false;
  for (auto& listener : listeners_) {
    listener->enable();
  }
}

void ConnectionHandlerImpl::onAccept(ConnectionSocketPtr&& socket) {
  if (listener_callbacks_) {
    listener_callbacks_->onAccept(std::move(socket));
  }
}

void ConnectionHandlerImpl::onNewConnection(ConnectionPtr&& connection) {
  connections_.push_back(std::move(connection));
  
  if (listener_callbacks_) {
    // Note: This would need proper move semantics
    // listener_callbacks_->onNewConnection(std::move(connection));
  }
}

void ConnectionHandlerImpl::removeConnection(Connection& connection) {
  connections_.erase(
      std::remove_if(connections_.begin(), connections_.end(),
                     [&connection](const ConnectionPtr& conn) {
                       return conn.get() == &connection;
                     }),
      connections_.end());
}

// ConnectionManagerImpl implementation

ConnectionManagerImpl::ConnectionManagerImpl(event::Dispatcher& dispatcher,
                                             SocketInterface& socket_interface,
                                             const ConnectionManagerConfig& config)
    : dispatcher_(dispatcher),
      socket_interface_(socket_interface),
      config_(config) {}

ConnectionManagerImpl::~ConnectionManagerImpl() {
  closeAllConnections();
}

ClientConnectionPtr ConnectionManagerImpl::createClientConnection(
    const network::Address::InstanceConstSharedPtr& address,
    const TransportSocketOptionsSharedPtr& transport_options) {
  
  // Check connection limit
  if (config_.max_connections.has_value() && 
      connections_.size() >= config_.max_connections.value()) {
    return nullptr;
  }
  
  // Create socket
  auto socket_result = socket_interface_.socket(
      SocketType::Stream,
      address->type());
  
  if (!socket_result.ok()) {
    return nullptr;
  }
  
  auto socket_fd = *socket_result;
  
  // Create IoHandle from fd
  auto io_handle = socket_interface_.ioHandleForFd(socket_fd);
  if (!io_handle) {
    socket_interface_.close(socket_fd);
    return nullptr;
  }
  
  // Create socket
  auto socket = std::make_unique<ConnectionSocketImpl>(
      std::move(io_handle), nullptr, address);
  
  // Create transport socket
  auto transport_socket = createClientTransportSocket(transport_options);
  if (!transport_socket) {
    return nullptr;
  }
  
  // Create stream info
  auto stream_info = stream_info::StreamInfoImpl::create();
  
  // Create client connection
  auto connection = ConnectionImpl::createClientConnection(
      dispatcher_,
      std::move(socket),
      std::move(transport_socket),
      *stream_info);
  
  // Configure connection
  connection->setBufferLimits(config_.per_connection_buffer_limit);
  
  // Apply filter chain
  applyFilterChain(*connection);
  
  // Initialize filters
  auto* conn_impl_base = dynamic_cast<ConnectionImplBase*>(connection.get());
  if (conn_impl_base) {
    conn_impl_base->filterManager().initializeReadFilters();
  }
  
  // Add connection callbacks
  connection->addConnectionCallbacks(*this);
  
  // Track connection
  trackConnection(connection.get());
  
  // Set connection timeout if configured
  if (config_.connection_timeout.has_value()) {
    connection->setDelayedCloseTimeout(config_.connection_timeout.value());
  }
  
  return connection;
}

ServerConnectionPtr ConnectionManagerImpl::createServerConnection(
    ConnectionSocketPtr&& socket) {
  
  // Check connection limit
  if (config_.max_connections.has_value() && 
      connections_.size() >= config_.max_connections.value()) {
    return nullptr;
  }
  
  // Extract the raw socket
  // Note: This is simplified - real implementation would have proper extraction
  SocketPtr raw_socket;
  
  // Create transport socket
  auto transport_socket = createServerTransportSocket();
  if (!transport_socket) {
    return nullptr;
  }
  
  // Create stream info
  auto stream_info = stream_info::StreamInfoImpl::create();
  
  // Create server connection
  auto connection = ConnectionImpl::createServerConnection(
      dispatcher_,
      std::move(raw_socket),
      std::move(transport_socket),
      *stream_info);
  
  // Configure connection
  connection->setBufferLimits(config_.per_connection_buffer_limit);
  
  // Apply filter chain
  applyFilterChain(*connection);
  
  // Initialize filters
  auto* conn_impl_base = dynamic_cast<ConnectionImplBase*>(connection.get());
  if (conn_impl_base) {
    conn_impl_base->filterManager().initializeReadFilters();
  }
  
  // Add connection callbacks
  connection->addConnectionCallbacks(*this);
  
  // Track connection
  trackConnection(connection.get());
  
  return connection;
}

void ConnectionManagerImpl::closeAllConnections() {
  // Clear the connections map first to avoid issues with destroyed connections
  // Since we return unique_ptrs, we don't actually own the connections and
  // they may have been destroyed already
  connections_.clear();
}

void ConnectionManagerImpl::onEvent(ConnectionEvent event) {
  // This callback is called per-connection, but we need to identify which one.
  // For now, we'll iterate through tracked connections to find the one that's closing
  if (event == ConnectionEvent::LocalClose || event == ConnectionEvent::RemoteClose) {
    // Find and remove closed connections
    for (auto it = connections_.begin(); it != connections_.end(); ) {
      Connection* conn = it->first;
      if (conn && conn->state() == ConnectionState::Closed) {
        // Notify pool callbacks before removing
        if (pool_callbacks_) {
          pool_callbacks_->onConnectionEvent(*conn, event);
        }
        it = connections_.erase(it);
      } else {
        ++it;
      }
    }
  } else if (pool_callbacks_) {
    // TODO: For non-close events, notify for all connections (not ideal but safe)
    // TODO: In production, we'd use a per-connection callback wrapper
    for (auto& pair : connections_) {
      Connection* conn = pair.first;
      if (conn) {
        pool_callbacks_->onConnectionEvent(*conn, event);
      }
    }
  }
}

void ConnectionManagerImpl::trackConnection(Connection* connection) {
  // Note: In real implementation, we'd store shared_ptr properly
  connections_[connection] = std::weak_ptr<Connection>();
}

void ConnectionManagerImpl::untrackConnection(Connection* connection) {
  connections_.erase(connection);
}

TransportSocketPtr ConnectionManagerImpl::createClientTransportSocket(
    const TransportSocketOptionsSharedPtr& options) {
  if (!config_.client_transport_socket_factory) {
    return nullptr;
  }
  
  auto socket_options = options;
  if (!socket_options) {
    socket_options = std::make_shared<TransportSocketOptionsImpl>();
  }
  
  return config_.client_transport_socket_factory->createTransportSocket(socket_options);
}

TransportSocketPtr ConnectionManagerImpl::createServerTransportSocket() {
  if (!config_.server_transport_socket_factory) {
    return nullptr;
  }
  
  auto options = std::make_shared<TransportSocketOptionsImpl>();
  return config_.server_transport_socket_factory->createTransportSocket();
}

void ConnectionManagerImpl::applyFilterChain(Connection& connection) {
  if (config_.filter_chain_factory) {
    // Get the filter manager from ConnectionImplBase
    auto* conn_impl_base = dynamic_cast<ConnectionImplBase*>(&connection);
    if (conn_impl_base) {
      config_.filter_chain_factory->createFilterChain(conn_impl_base->filterManager());
    }
  }
}

// ConnectionPoolImpl implementation

ConnectionPoolImpl::ConnectionPoolImpl(event::Dispatcher& dispatcher,
                                       const network::Address::InstanceConstSharedPtr& address,
                                       const ConnectionManagerConfig& config,
                                       ConnectionManager& connection_manager)
    : dispatcher_(dispatcher),
      address_(address),
      config_(config),
      connection_manager_(connection_manager) {}

ConnectionPoolImpl::~ConnectionPoolImpl() {
  closeConnections();
}

void ConnectionPoolImpl::newConnection(Callbacks& callbacks) {
  if (draining_) {
    callbacks.onPoolFailure(PoolFailureReason::Overflow, "Pool is draining");
    return;
  }
  
  // Check if we have an idle connection
  if (!idle_connections_.empty()) {
    auto connection = std::move(idle_connections_.front());
    idle_connections_.pop_front();
    
    assignConnection(*connection, callbacks);
    return;
  }
  
  // Check connection limits
  size_t total_connections = active_connections_.size() + 
                            pending_connections_.size() + 
                            idle_connections_.size();
  
  if (config_.max_connections.has_value() && 
      total_connections >= config_.max_connections.value()) {
    callbacks.onPoolFailure(PoolFailureReason::Overflow, "Connection limit reached");
    return;
  }
  
  // Create new connection
  createNewConnection(callbacks);
}

void ConnectionPoolImpl::closeConnections() {
  // Clear all connections
  pending_connections_.clear();
  active_connections_.clear();
  idle_connections_.clear();
}

bool ConnectionPoolImpl::isIdle() const {
  return active_connections_.empty() && 
         pending_connections_.empty() && 
         idle_connections_.empty();
}

void ConnectionPoolImpl::drainConnections() {
  draining_ = true;
  
  // Close idle connections
  idle_connections_.clear();
  
  // Cancel pending connections
  for (auto& pending : pending_connections_) {
    pending.callbacks->onPoolFailure(PoolFailureReason::LocalFailure, "Pool draining");
  }
  pending_connections_.clear();
}

void ConnectionPoolImpl::onEvent(ConnectionEvent event) {
  switch (event) {
  case ConnectionEvent::Connected:
    // Handle connection ready
    // Find in pending connections and move to ready state
    for (auto it = pending_connections_.begin(); it != pending_connections_.end(); ++it) {
      // Note: Need proper connection identification
      // For now, just handle the first pending
      movePendingToIdle(*it);
      pending_connections_.erase(it);
      break;
    }
    break;
    
  case ConnectionEvent::RemoteClose:
  case ConnectionEvent::LocalClose:
    // Handle connection close
    // Remove from active/idle connections
    checkIdleCallbacks();
    break;
    
  default:
    break;
  }
}

void ConnectionPoolImpl::createNewConnection(Callbacks& callbacks) {
  auto connection = connection_manager_.createClientConnection(address_);
  if (!connection) {
    callbacks.onPoolFailure(PoolFailureReason::LocalFailure, "Failed to create connection");
    return;
  }
  
  // Create pending connection entry
  PendingConnection pending;
  pending.connection = std::move(connection);
  pending.callbacks = &callbacks;
  
  // Set connection timeout
  if (config_.connection_timeout.has_value()) {
    pending.timeout_timer = dispatcher_.createTimer([this, &pending]() {
      onConnectionTimeout(pending);
    });
    pending.timeout_timer->enableTimer(config_.connection_timeout.value());
  }
  
  // Add connection callbacks
  pending.connection->addConnectionCallbacks(*this);
  
  // Start connection
  pending.connection->connect();
  
  pending_connections_.push_back(std::move(pending));
}

void ConnectionPoolImpl::assignConnection(ClientConnection& connection, Callbacks& callbacks) {
  // Create active connection entry
  ActiveConnection active;
  // Don't take ownership of connection passed by reference
  // active.connection should already own it
  active.busy = true;
  
  active_connections_.push_back(std::move(active));
  
  // Notify callbacks
  callbacks.onPoolReady(connection, "");
}

void ConnectionPoolImpl::onConnectionTimeout(PendingConnection& pending) {
  pending.callbacks->onPoolFailure(PoolFailureReason::Timeout, "Connection timeout");
  
  // Remove from pending
  pending_connections_.erase(
      std::remove_if(pending_connections_.begin(), pending_connections_.end(),
                     [&pending](const PendingConnection& p) {
                       return p.connection == pending.connection;
                     }),
      pending_connections_.end());
}

void ConnectionPoolImpl::checkIdleCallbacks() {
  if (isIdle()) {
    for (auto& cb : idle_callbacks_) {
      cb();
    }
    idle_callbacks_.clear();
  }
}

void ConnectionPoolImpl::movePendingToIdle(PendingConnection& pending) {
  // Cancel timeout timer
  if (pending.timeout_timer) {
    pending.timeout_timer->disableTimer();
  }
  
  // Move to idle or assign to waiting callback
  if (pending.callbacks) {
    assignConnection(*pending.connection, *pending.callbacks);
  } else {
    idle_connections_.push_back(std::move(pending.connection));
  }
}

} // namespace network
} // namespace mcp