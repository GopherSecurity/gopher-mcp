/**
 * @file tcp_transport_socket.cc
 * @brief TCP transport socket implementation with state machine integration
 */

#include "mcp/transport/tcp_transport_socket.h"

#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
// Windows uses SD_SEND instead of SHUT_WR
#ifndef SHUT_WR
#define SHUT_WR SD_SEND
#endif
// MSG_NOSIGNAL is Linux-specific (prevents SIGPIPE), not needed on Windows
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#else
#include <unistd.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#include "mcp/buffer.h"
#include "mcp/network/socket.h"

namespace mcp {
namespace transport {

TcpTransportSocket::TcpTransportSocket(event::Dispatcher& dispatcher,
                                       const TcpTransportSocketConfig& config)
    : config_(config), dispatcher_(dispatcher) {
  // Initialize state machine with basic config
  StateMachineConfig sm_config;
  sm_config.mode = StateMachineConfig::Mode::Client;
  sm_config.connect_timeout = config.connect_timeout;
  sm_config.idle_timeout = config.io_timeout;

  state_machine_ =
      std::make_unique<TransportSocketStateMachine>(dispatcher, sm_config);

  // Configure state machine
  configureStateMachine();

  // Register state change listener
  state_machine_->addStateChangeListener([this](const StateChangeEvent& event) {
    onStateChanged(event.from_state, event.to_state);
  });

  // Start in uninitialized state
  state_machine_->transitionTo(TransportSocketState::Uninitialized,
                               "Initial state");
}

TcpTransportSocket::~TcpTransportSocket() {
  // Ensure clean shutdown
  if (state_machine_) {
    auto current_state = state_machine_->currentState();
    if (current_state != TransportSocketState::Closed &&
        current_state != TransportSocketState::Error) {
      state_machine_->transitionTo(TransportSocketState::Closed,
                                   "Destructor cleanup");
    }
  }
}

void TcpTransportSocket::setTransportSocketCallbacks(
    network::TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

void TcpTransportSocket::closeSocket(network::ConnectionEvent event) {
  // Transition state machine to closing/closed
  if (state_machine_) {
    // Transition to shutting down first
    state_machine_->transitionTo(TransportSocketState::ShuttingDown,
                                 "Socket closing");

    // Then to closed
    state_machine_->transitionTo(TransportSocketState::Closed, "Socket closed");
  }

  // Notify callbacks
  if (callbacks_) {
    callbacks_->raiseEvent(event);
  }
}

network::TransportIoResult TcpTransportSocket::doRead(Buffer& buffer) {
  // Check state - only allow reads in Connected state
  if (!state_machine_ ||
      state_machine_->currentState() != TransportSocketState::Connected) {
    Error err;
    err.code = ENOTCONN;
    err.message = "Socket not connected";
    failure_reason_ = err.message;
    return network::TransportIoResult::error(err);
  }

  // Transition to reading state
  state_machine_->transitionTo(TransportSocketState::Reading, "Read requested");

  // Get the socket from callbacks
  if (!callbacks_) {
    Error err;
    err.code = EINVAL;
    err.message = "No callbacks set";
    failure_reason_ = err.message;
    state_machine_->transitionTo(TransportSocketState::Error, err.message);
    return network::TransportIoResult::error(err);
  }

  auto& io_handle = callbacks_->ioHandle();
  if (io_handle.fd() == network::INVALID_SOCKET_FD) {
    Error err;
    err.code = EBADF;
    err.message = "Invalid socket";
    failure_reason_ = err.message;
    state_machine_->transitionTo(TransportSocketState::Error, err.message);
    return network::TransportIoResult::error(err);
  }

  // Reserve space in buffer for reading
  constexpr size_t read_size = 16384;  // 16KB chunks
  RawSlice slice;
  void* mem = buffer.reserveSingleSlice(read_size, slice);

  if (!mem) {
    Error err;
    err.code = ENOMEM;
    err.message = "Out of memory";
    failure_reason_ = err.message;
    state_machine_->transitionTo(TransportSocketState::Error, err.message);
    return network::TransportIoResult::error(err);
  }

  // Perform the actual read
  // Note: Windows recv() expects char* buffer, POSIX expects void*
  ssize_t bytes_read = ::recv(io_handle.fd(),
                              static_cast<char*>(slice.mem_),
                              static_cast<int>(slice.len_), 0);

  if (bytes_read > 0) {
    // Successful read
    slice.len_ = bytes_read;
    buffer.commit(slice, bytes_read);

    // Transition back to connected
    state_machine_->transitionTo(TransportSocketState::Connected,
                                 "Read completed");

    // Check if more data might be available
    if (static_cast<size_t>(bytes_read) == read_size) {
      // Full buffer read, might be more data
      callbacks_->setTransportSocketIsReadable();
    }

    return network::TransportIoResult::success(bytes_read);
  } else if (bytes_read == 0) {
    // EOF - peer closed connection
    state_machine_->transitionTo(TransportSocketState::ShuttingDown,
                                 "Peer closed");
    return network::TransportIoResult::endStream(0);
  } else {
    // Error occurred
    int error = errno;
    if (error == EAGAIN || error == EWOULDBLOCK) {
      // No data available right now
      state_machine_->transitionTo(TransportSocketState::Connected,
                                   "Connection established");
      return network::TransportIoResult::success(0);
    } else {
      // Real error
      failure_reason_ = "Read error: " + std::string(strerror(error));
      Error err;
      err.code = error;
      err.message = failure_reason_;
      state_machine_->transitionTo(TransportSocketState::Error, err.message);
      return network::TransportIoResult::error(err);
    }
  }
}

network::TransportIoResult TcpTransportSocket::doWrite(Buffer& buffer,
                                                       bool end_stream) {
  // Check state - only allow writes in Connected state
  if (!state_machine_ ||
      state_machine_->currentState() != TransportSocketState::Connected) {
    Error err;
    err.code = ENOTCONN;
    err.message = "Socket not connected";
    failure_reason_ = err.message;
    return network::TransportIoResult::error(err);
  }

  // Transition to writing state
  state_machine_->transitionTo(TransportSocketState::Writing,
                               "Write requested");

  // Get the socket from callbacks
  if (!callbacks_) {
    Error err;
    err.code = EINVAL;
    err.message = "No callbacks set";
    failure_reason_ = err.message;
    state_machine_->transitionTo(TransportSocketState::Error, err.message);
    return network::TransportIoResult::error(err);
  }

  auto& io_handle = callbacks_->ioHandle();
  if (io_handle.fd() == network::INVALID_SOCKET_FD) {
    Error err;
    err.code = EBADF;
    err.message = "Invalid socket";
    failure_reason_ = err.message;
    state_machine_->transitionTo(TransportSocketState::Error, err.message);
    return network::TransportIoResult::error(err);
  }

  if (buffer.length() == 0) {
    // Nothing to write
    state_machine_->transitionTo(TransportSocketState::Connected,
                                 "Read completed");
    if (end_stream) {
      // Shutdown write side
      ::shutdown(io_handle.fd(), SHUT_WR);
      state_machine_->transitionTo(TransportSocketState::ShuttingDown,
                                   "Peer closed");
    }
    return network::TransportIoResult::success(0);
  }

  // Get raw slices from buffer
  constexpr size_t max_slices = 16;
  RawSlice slices[max_slices];
  size_t num_slices = buffer.getRawSlices(slices, max_slices);

  size_t total_written = 0;

  for (size_t i = 0; i < num_slices; ++i) {
    const auto& slice = slices[i];
    size_t remaining = slice.len_;
    const uint8_t* data = static_cast<const uint8_t*>(slice.mem_);

    while (remaining > 0) {
      // Note: Windows send() expects const char* buffer, POSIX expects const void*
      ssize_t bytes_written =
          ::send(io_handle.fd(), reinterpret_cast<const char*>(data),
                 static_cast<int>(remaining), MSG_NOSIGNAL);

      if (bytes_written > 0) {
        total_written += bytes_written;
        remaining -= bytes_written;
        data += bytes_written;
      } else if (bytes_written == 0) {
        // Shouldn't happen with TCP
        break;
      } else {
        // Error occurred
        int error = errno;
        if (error == EAGAIN || error == EWOULDBLOCK) {
          // Socket buffer full, can't write more now
          if (total_written > 0) {
            buffer.drain(total_written);
          }
          state_machine_->transitionTo(TransportSocketState::Connected,
                                       "Connection established");
          return network::TransportIoResult::success(total_written);
        } else {
          // Real error
          failure_reason_ = "Write error: " + std::string(strerror(error));
          Error err;
          err.code = error;
          err.message = failure_reason_;
          state_machine_->transitionTo(TransportSocketState::Error,
                                       err.message);
          return network::TransportIoResult::error(err);
        }
      }
    }
  }

  // Drain written data from buffer
  buffer.drain(total_written);

  // Handle end_stream
  if (end_stream && buffer.length() == 0) {
    // All data written and end_stream requested
    ::shutdown(io_handle.fd(), SHUT_WR);
    state_machine_->transitionTo(TransportSocketState::ShuttingDown,
                                 "Peer closed");
  } else {
    // More data might be available to write
    state_machine_->transitionTo(TransportSocketState::Connected,
                                 "Read completed");
  }

  return network::TransportIoResult::success(total_written);
}

void TcpTransportSocket::onConnected() {
  // Called when the underlying socket connects
  if (state_machine_) {
    // Transition from Connecting to Connected
    if (state_machine_->currentState() == TransportSocketState::Connecting) {
      state_machine_->transitionTo(TransportSocketState::Connected,
                                   "Connection established");
    }
  }

  // Mark transport as readable since connection is established
  if (callbacks_) {
    callbacks_->setTransportSocketIsReadable();
  }
}

VoidResult TcpTransportSocket::connect(network::Socket& socket) {
  // Initialize connection process
  if (state_machine_) {
    // Transition from Unconnected to Connecting
    state_machine_->transitionTo(TransportSocketState::Connecting,
                                 "Connect initiated");
  }

  // Apply TCP-specific socket options
  if (config_.tcp_nodelay) {
    int val = 1;
    socket.setSocketOption(IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
  }

  if (config_.tcp_keepalive) {
    int val = 1;
    socket.setSocketOption(SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val));
  }

  // The actual connection is handled by the ConnectionImpl
  // We just prepare the transport layer here
  return makeVoidSuccess();
}

void TcpTransportSocket::onStateChanged(TransportSocketState old_state,
                                        TransportSocketState new_state) {
  // Handle state transitions
  // This is called by the state machine when state changes

  // Log state transitions (if logging was available)
  // TODO: Add proper logging
  // LOG_DEBUG("TCP transport socket state transition: {} -> {}",
  //          TcpTransportSocketStateMachine::getStateName(old_state),
  //          TcpTransportSocketStateMachine::getStateName(new_state));

  switch (new_state) {
    case TransportSocketState::Connected:
      // Clear any previous failure reason
      failure_reason_.clear();
      break;

    case TransportSocketState::Error:
      // Handle error state
      if (callbacks_) {
        callbacks_->raiseEvent(network::ConnectionEvent::LocalClose);
      }
      break;

    case TransportSocketState::Closed:
      // Clean shutdown completed
      break;

    default:
      break;
  }
}

void TcpTransportSocket::configureStateMachine() {
  // Configure state machine behavior
  if (!state_machine_) {
    return;
  }

  // Set up any custom state transition rules or validators
  // For now, use default behavior from TcpTransportSocketStateMachine

  // Could add custom validation like:
  // state_machine_->addTransitionValidator(
  //     TransportSocketState::Connecting,
  //     TransportSocketState::Connected,
  //     [this]() { return callbacks_ != nullptr; });
}

}  // namespace transport
}  // namespace mcp