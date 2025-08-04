#include "mcp/network/transport_socket.h"

#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

#include "mcp/network/socket_interface_impl.h"

namespace mcp {
namespace network {

// RawBufferTransportSocket implementation

RawBufferTransportSocket::RawBufferTransportSocket() = default;

RawBufferTransportSocket::~RawBufferTransportSocket() = default;

void RawBufferTransportSocket::setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

Result<void> RawBufferTransportSocket::connect(Socket& socket) {
  // For raw buffer transport, connection is immediate
  connected_ = true;
  return make_result(nullptr);
}

void RawBufferTransportSocket::closeSocket(ConnectionEvent event) {
  if (!callbacks_) {
    return;
  }
  
  switch (event) {
    case ConnectionEvent::RemoteClose:
      shutdown_read_ = true;
      break;
    case ConnectionEvent::LocalClose:
      shutdown_write_ = true;
      break;
    default:
      break;
  }
  
  callbacks_->raiseEvent(event);
}

IoResult RawBufferTransportSocket::doRead(Buffer& buffer) {
  if (!callbacks_ || shutdown_read_) {
    return IoResult::stop();
  }
  
  const size_t max_slice_size = 16384; // 16KB slices
  RawSlice slice;
  
  // Reserve space in the buffer
  void* mem = buffer.reserveSingleSlice(max_slice_size, slice);
  if (!mem) {
    return IoResult::stop();
  }
  
  // Read from socket
  IoHandle& io_handle = callbacks_->ioHandle();
  auto result = io_handle.recv(slice.mem_, slice.len_, 0);
  
  if (!result.ok()) {
    auto error_code = result.error().error_code;
    
    // Handle would-block
    if (error_code == EAGAIN || error_code == EWOULDBLOCK) {
      buffer.commit(slice, 0);
      return IoResult::stop();
    }
    
    // Handle connection reset
    if (error_code == ECONNRESET) {
      buffer.commit(slice, 0);
      return IoResult::close();
    }
    
    // Other errors
    failure_reason_ = strerror(error_code);
    buffer.commit(slice, 0);
    return IoResult::error(make_error(jsonrpc::INTERNAL_ERROR, failure_reason_));
  }
  
  size_t bytes_read = result.return_value_;
  
  // Handle EOF
  if (bytes_read == 0) {
    buffer.commit(slice, 0);
    shutdown_read_ = true;
    return IoResult(PostIoAction::Close, 0, true);
  }
  
  // Commit the read data
  buffer.commit(slice, bytes_read);
  
  // Mark socket as readable if edge-triggered
  callbacks_->setTransportSocketIsReadable();
  
  return IoResult::success(bytes_read);
}

IoResult RawBufferTransportSocket::doWrite(Buffer& buffer, bool end_stream) {
  if (!callbacks_ || shutdown_write_) {
    return IoResult::stop();
  }
  
  if (buffer.length() == 0) {
    if (end_stream) {
      shutdown_write_ = true;
    }
    return IoResult::success();
  }
  
  IoHandle& io_handle = callbacks_->ioHandle();
  uint64_t total_bytes_sent = 0;
  
  // Gather slices for vectored I/O
  constexpr size_t max_iovecs = 16;
  RawSlice slices[max_iovecs];
  const size_t num_slices = buffer.getRawSlices(slices, max_iovecs);
  
  // Convert to iovec for sendmsg
  struct iovec iov[max_iovecs];
  for (size_t i = 0; i < num_slices; ++i) {
    iov[i].iov_base = slices[i].mem_;
    iov[i].iov_len = slices[i].len_;
  }
  
  // Send data
  auto result = io_handle.sendmsg(iov, num_slices, 0);
  
  if (!result.ok()) {
    auto error_code = result.error().error_code;
    
    // Handle would-block
    if (error_code == EAGAIN || error_code == EWOULDBLOCK) {
      return IoResult::stop();
    }
    
    // Handle connection reset/broken pipe
    if (error_code == ECONNRESET || error_code == EPIPE) {
      return IoResult::close();
    }
    
    // Other errors
    failure_reason_ = strerror(error_code);
    return IoResult::error(make_error(jsonrpc::INTERNAL_ERROR, failure_reason_));
  }
  
  total_bytes_sent = result.return_value_;
  
  // Drain sent data from buffer
  buffer.drain(total_bytes_sent);
  
  // Handle end of stream
  if (buffer.length() == 0 && end_stream) {
    shutdown_write_ = true;
    callbacks_->flushWriteBuffer();
  }
  
  return IoResult::success(total_bytes_sent);
}

void RawBufferTransportSocket::onConnected() {
  connected_ = true;
}

// RawBufferTransportSocketFactory implementation

TransportSocketPtr RawBufferTransportSocketFactory::createTransportSocket(
    TransportSocketOptionsSharedPtr options) const {
  (void)options; // Unused for raw buffer
  return std::make_unique<RawBufferTransportSocket>();
}

TransportSocketPtr RawBufferTransportSocketFactory::createTransportSocket() const {
  return std::make_unique<RawBufferTransportSocket>();
}

void RawBufferTransportSocketFactory::hashKey(std::vector<uint8_t>& key,
                                               TransportSocketOptionsSharedPtr options) const {
  (void)options;
  // Raw buffer transport has no configuration to hash
  key.push_back(0); // Just a marker byte
}

} // namespace network
} // namespace mcp