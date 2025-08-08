#include "mcp/transport/stdio_pipe_transport.h"
#include "mcp/network/address.h"
#include "mcp/network/address_impl.h"
#include "mcp/network/socket_interface_impl.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <iostream>
#include <vector>

namespace mcp {
namespace transport {

// StdioPipeTransport implementation

StdioPipeTransport::StdioPipeTransport(const StdioPipeTransportConfig& config)
    : config_(config) {
  read_buffer_ = std::make_unique<OwnedBuffer>();
  stdin_to_conn_pipe_[0] = -1;
  stdin_to_conn_pipe_[1] = -1;
  conn_to_stdout_pipe_[0] = -1;
  conn_to_stdout_pipe_[1] = -1;
}

StdioPipeTransport::~StdioPipeTransport() {
  // Signal threads to stop
  running_ = false;
  
  // Close write end of stdin pipe to signal EOF to the reader thread
  // This will wake up the bridgeStdinToPipe thread if it's blocked on write()
  if (stdin_to_conn_pipe_[1] != -1) {
    ::close(stdin_to_conn_pipe_[1]);
    stdin_to_conn_pipe_[1] = -1;
  }
  
  // Close read end of stdout pipe to signal EOF to the writer thread  
  // This will wake up the bridgePipeToStdout thread if it's blocked on read()
  if (conn_to_stdout_pipe_[0] != -1) {
    ::close(conn_to_stdout_pipe_[0]);
    conn_to_stdout_pipe_[0] = -1;
  }
  
  // Wait for threads to finish if they're joinable
  // The threads should exit cleanly after we closed the pipes above
  if (stdin_bridge_thread_.joinable()) {
    stdin_bridge_thread_.join();
  }
  if (stdout_bridge_thread_.joinable()) {
    stdout_bridge_thread_.join();
  }
  
  // Close remaining pipe ends that weren't transferred to ConnectionSocketImpl
  // Only close if fd != -1 (i.e., not transferred via takePipeSocket)
  if (stdin_to_conn_pipe_[0] != -1) {
    ::close(stdin_to_conn_pipe_[0]);
    stdin_to_conn_pipe_[0] = -1;
  }
  if (conn_to_stdout_pipe_[1] != -1) {
    ::close(conn_to_stdout_pipe_[1]);
    conn_to_stdout_pipe_[1] = -1;
  }
}

VoidResult StdioPipeTransport::initialize() {
  // Create pipes
  if (::pipe(stdin_to_conn_pipe_) == -1) {
    failure_reason_ = "Failed to create stdin pipe: ";
    failure_reason_ += strerror(errno);
    Error err;
    err.code = errno;
    err.message = failure_reason_;
    return makeVoidError(err);
  }
  
  if (::pipe(conn_to_stdout_pipe_) == -1) {
    ::close(stdin_to_conn_pipe_[0]);
    ::close(stdin_to_conn_pipe_[1]);
    failure_reason_ = "Failed to create stdout pipe: ";
    failure_reason_ += strerror(errno);
    Error err;
    err.code = errno;
    err.message = failure_reason_;
    return makeVoidError(err);
  }
  
  // Set non-blocking mode on the pipes that ConnectionImpl will use
  if (config_.non_blocking) {
    setNonBlocking(stdin_to_conn_pipe_[0]);  // Read end for ConnectionImpl
    setNonBlocking(conn_to_stdout_pipe_[1]); // Write end for ConnectionImpl
  }
  
  // Create socket interface to create IO handle
  network::SocketInterfaceImpl socket_interface;
  
  // Create IO handle from the pipe fds
  auto io_handle = socket_interface.ioHandleForFd(stdin_to_conn_pipe_[0], conn_to_stdout_pipe_[1]);
  if (!io_handle) {
    ::close(stdin_to_conn_pipe_[0]);
    ::close(stdin_to_conn_pipe_[1]);
    ::close(conn_to_stdout_pipe_[0]);
    ::close(conn_to_stdout_pipe_[1]);
    failure_reason_ = "Failed to create IO handle for pipes";
    Error err;
    err.code = -1;
    err.message = failure_reason_;
    return makeVoidError(err);
  }
  
  // Create pipe addresses
  auto local_address = std::make_shared<network::Address::PipeInstance>("/tmp/mcp_stdio_in");
  auto remote_address = std::make_shared<network::Address::PipeInstance>("/tmp/mcp_stdio_out");
  
  // Create the connection socket that ConnectionImpl will use
  // IMPORTANT: The io_handle takes ownership of stdin_to_conn_pipe_[0] and conn_to_stdout_pipe_[1]
  // These fds will be managed by ConnectionSocketImpl and closed when it's destroyed
  // We must NOT close these fds in our destructor to avoid double-close errors
  pipe_socket_ = std::make_unique<network::ConnectionSocketImpl>(
      std::move(io_handle), local_address, remote_address);
  
  // Start bridge threads
  running_ = true;
  
  stdin_bridge_thread_ = std::thread([this]() {
    bridgeStdinToPipe();
  });
  
  stdout_bridge_thread_ = std::thread([this]() {
    bridgePipeToStdout();
  });
  
  return makeVoidSuccess();
}

std::unique_ptr<network::ConnectionSocketImpl> StdioPipeTransport::takePipeSocket() {
  // When we transfer ownership of the pipe socket to the caller,
  // we need to mark the file descriptors that were moved to the io_handle
  // as invalid (-1) so our destructor doesn't try to close them.
  // This prevents double-close errors since ConnectionSocketImpl's io_handle
  // will close these fds when it's destroyed.
  if (pipe_socket_) {
    // These fds are now owned by the ConnectionSocketImpl's io_handle
    stdin_to_conn_pipe_[0] = -1;  // Read end used by ConnectionImpl
    conn_to_stdout_pipe_[1] = -1;  // Write end used by ConnectionImpl
  }
  return std::move(pipe_socket_);
}

void StdioPipeTransport::setTransportSocketCallbacks(
    network::TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

VoidResult StdioPipeTransport::connect(network::Socket& socket) {
  (void)socket;
  connected_ = true;
  return makeVoidSuccess();
}

void StdioPipeTransport::closeSocket(network::ConnectionEvent event) {
  if (!connected_) {
    return;
  }
  
  connected_ = false;
  running_ = false;
  
  // Close write end of stdin pipe to signal EOF to ConnectionImpl
  if (stdin_to_conn_pipe_[1] != -1) {
    ::close(stdin_to_conn_pipe_[1]);
    stdin_to_conn_pipe_[1] = -1;
  }
  
  // Notify callbacks
  if (callbacks_) {
    callbacks_->raiseEvent(event);
  }
}

TransportIoResult StdioPipeTransport::doRead(Buffer& buffer) {
  // This is called by ConnectionImpl to read data
  // The data has already been bridged from stdin to the pipe
  // ConnectionImpl will read directly from the pipe via IoHandle
  // This method shouldn't be called if we're using the pipe properly
  
  // Just return success with no data
  return TransportIoResult::success(0);
}

TransportIoResult StdioPipeTransport::doWrite(Buffer& buffer, bool end_stream) {
  // This is called by ConnectionImpl to write data
  // The data will be written to the pipe and bridged to stdout
  // ConnectionImpl will write directly to the pipe via IoHandle
  // This method shouldn't be called if we're using the pipe properly
  
  (void)end_stream;
  
  // Just drain the buffer and return success
  size_t len = buffer.length();
  buffer.drain(len);
  return TransportIoResult::success(len);
}

void StdioPipeTransport::onConnected() {
  connected_ = true;
  
  // Signal that there might be data available to read
  if (callbacks_) {
    callbacks_->setTransportSocketIsReadable();
  }
}

void StdioPipeTransport::bridgeStdinToPipe() {
  // This thread reads from stdin and writes to the pipe
  std::vector<char> buffer(config_.buffer_size);
  
  while (running_) {
    ssize_t bytes_read = ::read(config_.stdin_fd, buffer.data(), buffer.size());
    
    if (bytes_read > 0) {
      // Write all data to the pipe
      size_t total_written = 0;
      while (total_written < static_cast<size_t>(bytes_read) && running_) {
        ssize_t bytes_written = ::write(stdin_to_conn_pipe_[1],
                                       buffer.data() + total_written,
                                       bytes_read - total_written);
        
        if (bytes_written > 0) {
          total_written += bytes_written;
        } else if (bytes_written == -1) {
          int err = errno;
          if (err != EAGAIN && err != EWOULDBLOCK) {
            // Error writing to pipe
            failure_reason_ = "Error writing to stdin pipe: ";
            failure_reason_ += strerror(err);
            running_ = false;
            break;
          }
          // Otherwise, retry
          usleep(1000); // Sleep 1ms
        }
      }
    } else if (bytes_read == 0) {
      // EOF on stdin
      break;
    } else {
      // Error reading from stdin
      int err = errno;
      if (err != EAGAIN && err != EWOULDBLOCK) {
        failure_reason_ = "Error reading from stdin: ";
        failure_reason_ += strerror(err);
        break;
      }
      // Otherwise, retry
      usleep(1000); // Sleep 1ms
    }
  }
  
  // Close write end of pipe to signal EOF
  if (stdin_to_conn_pipe_[1] != -1) {
    ::close(stdin_to_conn_pipe_[1]);
    stdin_to_conn_pipe_[1] = -1;
  }
}

void StdioPipeTransport::bridgePipeToStdout() {
  // This thread reads from the pipe and writes to stdout
  std::vector<char> buffer(config_.buffer_size);
  
  while (running_) {
    ssize_t bytes_read = ::read(conn_to_stdout_pipe_[0], buffer.data(), buffer.size());
    
    if (bytes_read > 0) {
      // Write all data to stdout
      size_t total_written = 0;
      while (total_written < static_cast<size_t>(bytes_read) && running_) {
        ssize_t bytes_written = ::write(config_.stdout_fd,
                                       buffer.data() + total_written,
                                       bytes_read - total_written);
        
        if (bytes_written > 0) {
          total_written += bytes_written;
          // Flush stdout after each write to ensure data is sent immediately
          if (config_.stdout_fd == 1) {
            fflush(stdout);
          }
        } else if (bytes_written == -1) {
          int err = errno;
          if (err != EAGAIN && err != EWOULDBLOCK) {
            // Error writing to stdout
            failure_reason_ = "Error writing to stdout: ";
            failure_reason_ += strerror(err);
            running_ = false;
            break;
          }
          // Otherwise, retry
          usleep(1000); // Sleep 1ms
        }
      }
    } else if (bytes_read == 0) {
      // EOF on pipe (ConnectionImpl closed)
      break;
    } else {
      // Error reading from pipe
      int err = errno;
      if (err != EAGAIN && err != EWOULDBLOCK) {
        failure_reason_ = "Error reading from stdout pipe: ";
        failure_reason_ += strerror(err);
        break;
      }
      // Otherwise, retry
      usleep(1000); // Sleep 1ms
    }
  }
  
  // Close read end of pipe
  if (conn_to_stdout_pipe_[0] != -1) {
    ::close(conn_to_stdout_pipe_[0]);
    conn_to_stdout_pipe_[0] = -1;
  }
}

void StdioPipeTransport::setNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    failure_reason_ = "Failed to get file descriptor flags";
    return;
  }
  
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    failure_reason_ = "Failed to set non-blocking mode";
  }
}

// StdioPipeTransportFactory implementation

StdioPipeTransportFactory::StdioPipeTransportFactory(const StdioPipeTransportConfig& config)
    : config_(config) {}

network::TransportSocketPtr StdioPipeTransportFactory::createTransportSocket(
    network::TransportSocketOptionsSharedPtr options) const {
  (void)options;
  return std::make_unique<StdioPipeTransport>(config_);
}

void StdioPipeTransportFactory::hashKey(
    std::vector<uint8_t>& key,
    network::TransportSocketOptionsSharedPtr options) const {
  // Add factory identifier
  const std::string factory_name = "stdio_pipe";
  key.insert(key.end(), factory_name.begin(), factory_name.end());
  
  // Add config values
  key.push_back(static_cast<uint8_t>(config_.stdin_fd));
  key.push_back(static_cast<uint8_t>(config_.stdout_fd));
  key.push_back(config_.non_blocking ? 1 : 0);
  
  (void)options;
}

network::TransportSocketPtr StdioPipeTransportFactory::createTransportSocket() const {
  return std::make_unique<StdioPipeTransport>(config_);
}

} // namespace transport
} // namespace mcp