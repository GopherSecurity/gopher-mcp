/**
 * @file connection_handler_impl.cc
 * @brief Connection handler implementation for managing listeners
 */

#include "mcp/network/connection_handler.h"
#include "mcp/network/connection_impl.h"

#include <sstream>

namespace mcp {
namespace network {

// =================================================================
// ConnectionHandlerImpl
// =================================================================

ConnectionHandlerImpl::ConnectionHandlerImpl(event::Dispatcher& dispatcher,
                                           optional<uint32_t> worker_index)
    : dispatcher_(dispatcher),
      worker_index_(worker_index) {
  // Create stat prefix based on worker index
  if (worker_index_.has_value()) {
    stat_prefix_ = "worker_" + std::to_string(worker_index_.value()) + ".";
  } else {
    stat_prefix_ = "main_thread.";
  }
  
  // Create thread-local overload state
  createOverloadState();
}

ConnectionHandlerImpl::ConnectionHandlerImpl(event::Dispatcher& dispatcher,
                                           optional<uint32_t> worker_index,
                                           OverloadManager& overload_manager)
    : dispatcher_(dispatcher),
      worker_index_(worker_index),
      overload_manager_(&overload_manager) {
  // Create stat prefix
  if (worker_index_.has_value()) {
    stat_prefix_ = "worker_" + std::to_string(worker_index_.value()) + ".";
  } else {
    stat_prefix_ = "main_thread.";
  }
  
  // Create thread-local overload state
  createOverloadState();
}

ConnectionHandlerImpl::~ConnectionHandlerImpl() {
  stopListeners();
}

void ConnectionHandlerImpl::incNumConnections() {
  num_connections_++;
  
  // Update global count in overload state if available
  if (overload_state_ && overload_state_->global_cx_count) {
    overload_state_->global_cx_count->fetch_add(1);
  }
}

void ConnectionHandlerImpl::decNumConnections() {
  if (num_connections_ > 0) {
    num_connections_--;
    
    // Update global count in overload state if available
    if (overload_state_ && overload_state_->global_cx_count) {
      overload_state_->global_cx_count->fetch_sub(1);
    }
  }
}

void ConnectionHandlerImpl::addListener(ListenerConfig config,
                                       ListenerCallbacks& callbacks) {
  // Generate listener tag if not provided
  static std::atomic<uint64_t> next_tag{1};
  uint64_t listener_tag = next_tag++;
  
  // Create listener details
  auto details = std::make_unique<ListenerDetails>();
  details->listener_tag = listener_tag;
  details->parent_callbacks = &callbacks;
  details->address = config.address;
  
  // Store address for lookup
  std::string address_key = config.address ? config.address->asString() : "";
  
  // Convert to TcpListenerConfig
  TcpListenerConfig tcp_config;
  tcp_config.name = config.name;
  tcp_config.address = config.address;
  tcp_config.socket_options = config.socket_options;
  tcp_config.bind_to_port = config.bind_to_port;
  tcp_config.enable_reuse_port = config.enable_reuse_port;
  tcp_config.backlog = config.backlog;
  tcp_config.per_connection_buffer_limit = config.per_connection_buffer_limit;
  tcp_config.listener_filters = std::move(config.listener_filters);
  tcp_config.transport_socket_factory = config.transport_socket_factory;
  tcp_config.filter_chain_factory = config.filter_chain_factory;
  tcp_config.max_connections_per_event = 1;  // Default
  
  // Create active listener
  // The TcpActiveListener will manage the actual TcpListenerImpl
  details->listener = std::make_unique<TcpActiveListener>(
      dispatcher_,
      std::move(tcp_config),
      *this  // We handle callbacks from TcpActiveListener
  );
  
  // Apply current settings
  if (listeners_disabled_) {
    details->listener->disable();
  }
  details->listener->setRejectFraction(listener_reject_fraction_);
  
  // Configure overload if available
  if (overload_manager_) {
    // In a real implementation, we'd get load shed points from overload manager
    // details->listener->configureLoadShedPoints(...);
  }
  
  // Store listener
  auto* details_ptr = details.get();
  listeners_[listener_tag] = std::move(details);
  
  // Store by address for quick lookup
  if (!address_key.empty()) {
    listeners_by_address_[address_key] = details_ptr;
  }
}

void ConnectionHandlerImpl::removeListener(uint64_t listener_tag) {
  auto it = listeners_.find(listener_tag);
  if (it != listeners_.end()) {
    // Remove from address map
    if (it->second->address) {
      listeners_by_address_.erase(it->second->address->asString());
    }
    
    // Disable and remove
    it->second->listener->disable();
    listeners_.erase(it);
  }
}

void ConnectionHandlerImpl::stopListeners() {
  // Disable all listeners
  disableListeners();
  
  // Clear all listeners
  listeners_by_address_.clear();
  listeners_.clear();
}

void ConnectionHandlerImpl::disableListeners() {
  listeners_disabled_ = true;
  for (auto& pair : listeners_) {
    pair.second->listener->disable();
  }
}

void ConnectionHandlerImpl::enableListeners() {
  listeners_disabled_ = false;
  for (auto& pair : listeners_) {
    pair.second->listener->enable();
  }
}

void ConnectionHandlerImpl::setListenerRejectFraction(UnitFloat reject_fraction) {
  listener_reject_fraction_ = reject_fraction;
  for (auto& pair : listeners_) {
    pair.second->listener->setRejectFraction(reject_fraction);
  }
}

void ConnectionHandlerImpl::onAccept(ConnectionSocketPtr&& socket) {
  // This is called by TcpActiveListener after filters pass
  // We need to find which listener this came from and forward to its callbacks
  
  // For now, just increment connection count
  incNumConnections();
  
  // In a real implementation, we'd:
  // 1. Find the appropriate listener based on the local address
  // 2. Forward to that listener's parent callbacks
  // 3. Or create a connection directly here
}

void ConnectionHandlerImpl::onNewConnection(ConnectionPtr&& connection) {
  // This is called by TcpActiveListener when a connection is fully created
  // Increment connection count
  incNumConnections();
  
  // Set up connection cleanup callback
  // Note: In a real implementation, we'd add callbacks for connection close
  // For now, just track the connection count
  
  // Forward to appropriate listener's callbacks
  // In a real implementation, we'd track which listener created this connection
}

TcpActiveListener* ConnectionHandlerImpl::getListener(uint64_t listener_tag) {
  auto it = listeners_.find(listener_tag);
  if (it != listeners_.end()) {
    return it->second->listener.get();
  }
  return nullptr;
}

TcpActiveListener* ConnectionHandlerImpl::getListenerByAddress(const Address::Instance& address) {
  auto it = listeners_by_address_.find(address.asString());
  if (it != listeners_by_address_.end()) {
    return it->second->listener.get();
  }
  return nullptr;
}

void ConnectionHandlerImpl::createOverloadState() {
  overload_state_ = std::make_unique<ThreadLocalOverloadState>();
  
  // Point to our connection count
  overload_state_->global_cx_count = &num_connections_;
  
  // Set default limits
  overload_state_->global_cx_limit = 1000000;  // 1M connections default
  
  // Track in overload manager if available
  overload_state_->track_global_cx_in_overload_manager = (overload_manager_ != nullptr);
}

}  // namespace network
}  // namespace mcp