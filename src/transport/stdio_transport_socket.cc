#include "mcp/transport/stdio_transport_socket.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "mcp/buffer.h"

namespace mcp {
namespace transport {

// StdioTransportSocket implementation

StdioTransportSocket::StdioTransportSocket(
    const StdioTransportSocketConfig& config)
    : config_(config) {
  // Initialize read buffer
  read_buffer_ = std::make_unique<OwnedBuffer>();

  // Set non-blocking mode if requested
  if (config_.non_blocking) {
    setNonBlocking(config_.stdin_fd);
    setNonBlocking(config_.stdout_fd);
  }
}

StdioTransportSocket::~StdioTransportSocket() {
  if (connected_) {
    closeSocket(network::ConnectionEvent::LocalClose);
  }
}

void StdioTransportSocket::setTransportSocketCallbacks(
    network::TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

VoidResult StdioTransportSocket::connect(network::Socket& socket) {
  // For stdio transport, we don't actually connect to anything
  // Just mark as connected
  connected_ = true;
  return makeVoidSuccess();
}

void StdioTransportSocket::closeSocket(network::ConnectionEvent event) {
  if (!connected_) {
    return;
  }

  connected_ = false;

  // Don't actually close stdin/stdout, just mark as shutdown
  shutdown_read_ = true;
  shutdown_write_ = true;

  // Notify callbacks
  if (callbacks_) {
    callbacks_->raiseEvent(event);
  }
}

TransportIoResult StdioTransportSocket::doRead(Buffer& buffer) {
  if (!connected_ || shutdown_read_) {
    Error err;
    err.code = ENOTCONN;
    err.message = "Not connected";
    return TransportIoResult::error(err);
  }

  // First check if we have buffered data
  if (read_buffer_->length() > 0) {
    buffer.move(*read_buffer_);
    return TransportIoResult::success(buffer.length());
  }

  // Read from stdin
  return performRead(buffer);
}

TransportIoResult StdioTransportSocket::doWrite(Buffer& buffer,
                                                bool end_stream) {
  if (!connected_ || shutdown_write_) {
    Error err;
    err.code = ENOTCONN;
    err.message = "Not connected";
    return TransportIoResult::error(err);
  }

  // Write to stdout
  auto result = performWrite(buffer);

  if (end_stream && result.ok()) {
    shutdown_write_ = true;
  }

  return result;
}

void StdioTransportSocket::onConnected() {
  // Mark as connected and ready
  connected_ = true;

  // Set transport socket as readable since stdin might have data
  if (callbacks_) {
    callbacks_->setTransportSocketIsReadable();
  }
}

void StdioTransportSocket::setNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    failure_reason_ = "Failed to get file descriptor flags";
    return;
  }

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    failure_reason_ = "Failed to set non-blocking mode";
  }
}

TransportIoResult StdioTransportSocket::performRead(Buffer& buffer) {
  // Reserve space in buffer
  constexpr size_t read_size = 16384;  // 16KB chunks
  RawSlice slice;
  void* mem = buffer.reserveSingleSlice(read_size, slice);

  if (!mem) {
    Error err;
    err.code = ENOMEM;
    err.message = "Out of memory";
    return TransportIoResult::error(err);
  }

  // Read from stdin
  ssize_t bytes_read = ::read(config_.stdin_fd, slice.mem_, slice.len_);

  if (bytes_read > 0) {
    // Commit the read data
    slice.len_ = bytes_read;
    buffer.commit(slice, bytes_read);

    // Check if more data might be available
    if (callbacks_ && static_cast<size_t>(bytes_read) == read_size) {
      callbacks_->setTransportSocketIsReadable();
    }

    return TransportIoResult::success(bytes_read);
  } else if (bytes_read == 0) {
    // EOF on stdin
    shutdown_read_ = true;
    return TransportIoResult::endStream(0);
  } else {
    // Error
    int error = errno;
    if (error == EAGAIN || error == EWOULDBLOCK) {
      // No data available
      return TransportIoResult::success(0);
    } else {
      failure_reason_ = "Read error: " + std::string(strerror(error));
      Error err;
      err.code = error;
      err.message = failure_reason_;
      return TransportIoResult::error(err);
    }
  }
}

TransportIoResult StdioTransportSocket::performWrite(Buffer& buffer) {
  if (buffer.length() == 0) {
    return TransportIoResult::success(0);
  }

  // When we have callbacks (ConnectionImpl), use the io_handle instead of raw FD
  if (callbacks_) {
    // Get buffer slices
    constexpr size_t max_slices = 16;
    ConstRawSlice slices[max_slices];
    size_t num_slices = buffer.getRawSlices(slices, max_slices);
    
    if (num_slices == 0) {
      return TransportIoResult::success(0);
    }
    
    // Write using io_handle
    network::IoHandle& io_handle = callbacks_->ioHandle();
    auto result = io_handle.writev(slices, num_slices);
    
    if (!result.ok()) {
      // Handle would-block
      if (result.wouldBlock()) {
        return TransportIoResult::success(0);
      }
      
      // Handle broken pipe
      if (result.error_code() == EPIPE) {
        shutdown_write_ = true;
        return TransportIoResult::close();
      }
      
      // Other errors
      failure_reason_ = "Write error: " + std::string(strerror(result.error_code()));
      Error err;
      err.code = result.error_code();
      err.message = failure_reason_;
      return TransportIoResult::error(err);
    }
    
    size_t bytes_written = *result;
    buffer.drain(bytes_written);
    return TransportIoResult::success(bytes_written);
  }

  // Fallback: write to raw FD if no callbacks
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
      ssize_t bytes_written = ::write(config_.stdout_fd, data, remaining);

      if (bytes_written > 0) {
        total_written += bytes_written;
        remaining -= bytes_written;
        data += bytes_written;
      } else if (bytes_written == 0) {
        // Shouldn't happen with stdout
        break;
      } else {
        // Error
        int error = errno;
        if (error == EAGAIN || error == EWOULDBLOCK) {
          // Can't write more now
          if (total_written > 0) {
            buffer.drain(total_written);
          }
          return TransportIoResult::success(total_written);
        } else {
          failure_reason_ = "Write error: " + std::string(strerror(error));
          Error err;
          err.code = error;
          err.message = failure_reason_;
          return TransportIoResult::error(err);
        }
      }
    }
  }

  // Drain written data from buffer
  buffer.drain(total_written);

  // Flush stdout to ensure data is sent
  if (total_written > 0) {
    fflush(stdout);
  }

  return TransportIoResult::success(total_written);
}

// StdioTransportSocketFactory implementation

StdioTransportSocketFactory::StdioTransportSocketFactory(
    const StdioTransportSocketConfig& config)
    : config_(config) {}

network::TransportSocketPtr StdioTransportSocketFactory::createTransportSocket(
    network::TransportSocketOptionsSharedPtr options) const {
  // Ignore options for stdio transport
  (void)options;
  return std::make_unique<StdioTransportSocket>(config_);
}

void StdioTransportSocketFactory::hashKey(
    std::vector<uint8_t>& key,
    network::TransportSocketOptionsSharedPtr options) const {
  // Add factory identifier
  const std::string factory_name = "stdio";
  key.insert(key.end(), factory_name.begin(), factory_name.end());

  // Add config values
  key.push_back(static_cast<uint8_t>(config_.stdin_fd));
  key.push_back(static_cast<uint8_t>(config_.stdout_fd));
  key.push_back(config_.non_blocking ? 1 : 0);

  // Options don't affect stdio transport
  (void)options;
}

network::TransportSocketPtr StdioTransportSocketFactory::createTransportSocket()
    const {
  return std::make_unique<StdioTransportSocket>(config_);
}

}  // namespace transport
}  // namespace mcp