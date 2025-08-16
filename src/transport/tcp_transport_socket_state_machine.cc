/**
 * @file tcp_transport_socket_state_machine.cc
 * @brief TCP transport socket state machine implementation
 */

#include "mcp/transport/tcp_transport_socket_state_machine.h"
#include "mcp/buffer.h"
#include <sys/socket.h>
#include <netinet/tcp.h>

namespace mcp {
namespace transport {

// =================================================================
// TcpTransportSocketStateMachine Implementation
// =================================================================

TcpTransportSocketStateMachine::TcpTransportSocketStateMachine(
    network::Connection& connection,
    event::Dispatcher& dispatcher,
    TcpStateMachineConfig config)
    : TransportSocketStateMachine(dispatcher, config),
      connection_(&connection),
      tcp_config_(std::move(config)) {
  
  // Initialize buffers
  pending_read_data_ = createBuffer();
  pending_write_data_ = createBuffer();
  
  // Configure TCP options
  configureTcpOptions();
}

TcpTransportSocketStateMachine::TcpTransportSocketStateMachine(
    network::ConnectionSocket& socket,
    event::Dispatcher& dispatcher,
    TcpStateMachineConfig config)
    : TransportSocketStateMachine(dispatcher, config),
      socket_(&socket),
      tcp_config_(std::move(config)) {
  
  // Initialize buffers
  pending_read_data_ = createBuffer();
  pending_write_data_ = createBuffer();
  
  // Server mode - already connected
  current_state_ = TransportSocketState::TcpConnected;
  
  // Configure TCP options
  configureTcpOptions();
}

TcpTransportSocketStateMachine::~TcpTransportSocketStateMachine() {
  // Cleanup
  if (reconnect_timer_) {
    reconnect_timer_->disableTimer();
  }
}

TransportIoResult TcpTransportSocketStateMachine::doRead(Buffer& buffer) {
  assertInDispatcherThread();
  
  // Check state
  if (current_state_ != TransportSocketState::Connected &&
      current_state_ != TransportSocketState::Reading) {
    return TransportIoResult::stop();
  }
  
  // Transition to reading state
  if (current_state_ == TransportSocketState::Connected) {
    transitionTo(TransportSocketState::Reading, "Read requested", nullptr);
  }
  
  return handleRead(buffer);
}

TransportIoResult TcpTransportSocketStateMachine::doWrite(
    Buffer& buffer, bool end_stream) {
  assertInDispatcherThread();
  
  // Check state
  if (current_state_ != TransportSocketState::Connected &&
      current_state_ != TransportSocketState::Writing) {
    // Buffer data for later
    pending_write_data_->add(buffer);
    return TransportIoResult::stop();
  }
  
  // Transition to writing state
  if (current_state_ == TransportSocketState::Connected) {
    transitionTo(TransportSocketState::Writing, "Write requested", nullptr);
  }
  
  return handleWrite(buffer, end_stream);
}

VoidResult TcpTransportSocketStateMachine::connect(network::Socket& socket) {
  assertInDispatcherThread();
  
  // Transition to connecting
  auto result = transitionTo(TransportSocketState::Connecting, 
                            "Connect initiated", nullptr);
  if (!result.success) {
    return VoidResult(Error{ErrorCode::INVALID_REQUEST, result.error_message});
  }
  
  // Actual connection would happen here
  // For now, just return success
  return makeVoidSuccess();
}

void TcpTransportSocketStateMachine::closeSocket(network::ConnectionEvent event) {
  assertInDispatcherThread();
  
  // Transition to shutdown
  transitionTo(TransportSocketState::ShuttingDown, 
              "Close requested: " + std::to_string(static_cast<int>(event)),
              [this](bool) {
                // After shutdown entry, transition to closed
                transitionTo(TransportSocketState::Closed, "Shutdown complete", nullptr);
              });
}

void TcpTransportSocketStateMachine::onConnected() {
  assertInDispatcherThread();
  
  // Handle connection established
  handleConnectionResult(true);
  
  // For TCP, go directly to connected (no handshake)
  transitionTo(TransportSocketState::Connected, "TCP connected", nullptr);
  
  metrics_.connections++;
}

void TcpTransportSocketStateMachine::onStateEnter(
    TransportSocketState state,
    CompletionCallback callback) {
  
  // Call base implementation first
  TransportSocketStateMachine::onStateEnter(state, callback);
  
  // TCP-specific state entry actions
  switch (state) {
    case TransportSocketState::Connected:
      // Flush any pending writes
      if (pending_write_data_ && pending_write_data_->length() > 0) {
        handleWrite(*pending_write_data_, false);
      }
      break;
      
    case TransportSocketState::Error:
      // Maybe initiate reconnection
      if (tcp_config_.enable_auto_reconnect && 
          reconnect_attempts_ < tcp_config_.max_reconnect_attempts) {
        initiateReconnection();
      }
      break;
      
    default:
      break;
  }
}

void TcpTransportSocketStateMachine::onStateExit(
    TransportSocketState state,
    CompletionCallback callback) {
  
  // TCP-specific state exit actions
  updateMetrics();
  
  // Call base implementation
  TransportSocketStateMachine::onStateExit(state, callback);
}

std::unordered_set<TransportSocketState> 
TcpTransportSocketStateMachine::getValidTransitions(
    TransportSocketState from) const {
  
  // Start with base transitions
  auto valid = TransportSocketStateMachine::getValidTransitions(from);
  
  // Add TCP-specific transitions
  // (TCP doesn't need handshake states)
  if (from == TransportSocketState::TcpConnected) {
    valid.insert(TransportSocketState::Connected);
  }
  
  return valid;
}

void TcpTransportSocketStateMachine::onStateTimeout(TransportSocketState state) {
  // Handle TCP-specific timeouts
  TransportSocketStateMachine::onStateTimeout(state);
}

void TcpTransportSocketStateMachine::configureTcpOptions() {
  if (!socket_) {
    return;
  }
  
  // TCP_NODELAY
  if (tcp_config_.tcp_nodelay) {
    int val = 1;
    socket_->setSocketOption(IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
  }
  
  // Keep-alive
  if (tcp_config_.keep_alive) {
    int val = 1;
    socket_->setSocketOption(SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val));
    
#ifdef TCP_KEEPIDLE
    socket_->setSocketOption(IPPROTO_TCP, TCP_KEEPIDLE, 
                            &tcp_config_.keep_alive_idle, 
                            sizeof(tcp_config_.keep_alive_idle));
#endif
    
#ifdef TCP_KEEPINTVL
    socket_->setSocketOption(IPPROTO_TCP, TCP_KEEPINTVL,
                            &tcp_config_.keep_alive_interval,
                            sizeof(tcp_config_.keep_alive_interval));
#endif
    
#ifdef TCP_KEEPCNT
    socket_->setSocketOption(IPPROTO_TCP, TCP_KEEPCNT,
                            &tcp_config_.keep_alive_count,
                            sizeof(tcp_config_.keep_alive_count));
#endif
  }
}

TransportIoResult TcpTransportSocketStateMachine::handleRead(Buffer& buffer) {
  // In real implementation, this would read from socket
  // For now, just return success
  metrics_.bytes_read += buffer.length();
  
  transitionTo(TransportSocketState::Connected, "Read complete", nullptr);
  return TransportIoResult::success(0);
}

TransportIoResult TcpTransportSocketStateMachine::handleWrite(
    Buffer& buffer, bool end_stream) {
  
  // In real implementation, this would write to socket
  // For now, just return success
  metrics_.bytes_written += buffer.length();
  
  if (end_stream) {
    transitionTo(TransportSocketState::ShuttingDown, "End stream", nullptr);
  } else {
    transitionTo(TransportSocketState::Connected, "Write complete", nullptr);
  }
  
  return TransportIoResult::success(buffer.length());
}

void TcpTransportSocketStateMachine::initiateReconnection() {
  reconnect_attempts_++;
  metrics_.reconnections++;
  
  // Schedule reconnection after delay
  reconnect_timer_ = dispatcher_.createTimer([this]() {
    // Transition back to initialized for retry
    transitionTo(TransportSocketState::Initialized, 
                "Reconnection attempt " + std::to_string(reconnect_attempts_),
                nullptr);
  });
  
  // Exponential backoff
  auto delay = tcp_config_.reconnect_delay * (1 << std::min(reconnect_attempts_, 5u));
  reconnect_timer_->enableTimer(delay);
}

void TcpTransportSocketStateMachine::updateMetrics() {
  // Update any real-time metrics here
}

// =================================================================
// TcpTransportSocketStateMachineFactory Implementation
// =================================================================

std::unique_ptr<TransportSocketStateMachine> 
TcpTransportSocketStateMachineFactory::createStateMachine(
    event::Dispatcher& dispatcher,
    StateMachineConfig config) {
  
  // Create TCP-specific config from base config
  TcpStateMachineConfig tcp_config;
  tcp_config.mode = config.mode;
  tcp_config.connect_timeout = config.connect_timeout;
  tcp_config.handshake_timeout = config.handshake_timeout;
  tcp_config.idle_timeout = config.idle_timeout;
  tcp_config.shutdown_timeout = config.shutdown_timeout;
  tcp_config.max_state_history = config.max_state_history;
  tcp_config.allow_force_transitions = config.allow_force_transitions;
  tcp_config.max_error_recoveries = config.max_error_recoveries;
  tcp_config.state_change_callback = config.state_change_callback;
  tcp_config.error_callback = config.error_callback;
  
  // TCP defaults from our config
  tcp_config.tcp_nodelay = config_.tcp_nodelay;
  tcp_config.keep_alive = config_.keep_alive;
  tcp_config.keep_alive_idle = config_.keep_alive_idle;
  tcp_config.keep_alive_interval = config_.keep_alive_interval;
  tcp_config.keep_alive_count = config_.keep_alive_count;
  tcp_config.read_buffer_limit = config_.read_buffer_limit;
  tcp_config.write_buffer_limit = config_.write_buffer_limit;
  tcp_config.enable_auto_reconnect = config_.enable_auto_reconnect;
  tcp_config.reconnect_delay = config_.reconnect_delay;
  tcp_config.max_reconnect_attempts = config_.max_reconnect_attempts;
  
  // Can't create standalone state machine without connection
  // This would normally be created with a connection
  return nullptr;
}

}  // namespace transport
}  // namespace mcp