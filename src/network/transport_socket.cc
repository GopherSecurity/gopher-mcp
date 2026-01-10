#include "mcp/network/transport_socket.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <errno.h>
#include <unistd.h>

#include <sys/socket.h>
#endif

#include "mcp/network/socket_interface_impl.h"

namespace mcp {
namespace network {

// RawBufferTransportSocket implementation

RawBufferTransportSocket::RawBufferTransportSocket() = default;

RawBufferTransportSocket::~RawBufferTransportSocket() = default;

void RawBufferTransportSocket::setTransportSocketCallbacks(
    TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

VoidResult RawBufferTransportSocket::connect(Socket& socket) {
  (void)socket;  // Unused for raw transport
  // For raw buffer transport, connection is immediate
  connected_ = true;
  return makeVoidSuccess();
}

void RawBufferTransportSocket::closeSocket(ConnectionEvent event) {
  // Set shutdown flags
  switch (event) {
    case ConnectionEvent::RemoteClose:
      shutdown_read_ = true;
      break;
    case ConnectionEvent::LocalClose:
      shutdown_write_ = true;
      shutdown_read_ = true;  // Also stop reading on local close
      break;
    default:
      break;
  }

  // Mark as disconnected
  connected_ = false;
  
  // Clear callbacks to prevent use-after-free
  // Don't raise events back - we're being called FROM the connection during close
  // Raising events here would create circular callbacks and potential crashes
  callbacks_ = nullptr;
}

TransportIoResult RawBufferTransportSocket::doRead(Buffer& buffer) {
  if (!callbacks_ || shutdown_read_) {
    return TransportIoResult::stop();
  }

  const size_t max_slice_size = 16384;  // 16KB slices
  RawSlice slice;

  // Reserve space in the buffer
  void* mem = buffer.reserveSingleSlice(max_slice_size, slice);
  if (!mem) {
    return TransportIoResult::stop();
  }

  // Read from socket
  IoHandle& io_handle = callbacks_->ioHandle();
  auto result = io_handle.readv(slice.len_, &slice, 1);

  if (!result.ok()) {
    buffer.commit(slice, 0);

    // Handle would-block
    if (result.wouldBlock()) {
      return TransportIoResult::stop();
    }

    // Handle connection reset
    if (result.error_code() == ECONNRESET) {
      return TransportIoResult::close();
    }

    // Other errors
    failure_reason_ =
        result.error_info ? result.error_info->message : "Unknown error";
    return TransportIoResult::error(
        make_error(jsonrpc::INTERNAL_ERROR, failure_reason_));
  }

  size_t bytes_read = *result;

  // Handle EOF
  if (bytes_read == 0) {
    buffer.commit(slice, 0);
    shutdown_read_ = true;
    return TransportIoResult::endStream(0);
  }

  // Commit the read data
  buffer.commit(slice, bytes_read);

  // Mark socket as readable if edge-triggered
  callbacks_->setTransportSocketIsReadable();

  return TransportIoResult::success(bytes_read);
}

TransportIoResult RawBufferTransportSocket::doWrite(Buffer& buffer,
                                                    bool end_stream) {
  if (!callbacks_ || shutdown_write_) {
    return TransportIoResult::stop();
  }

  if (buffer.length() == 0) {
    if (end_stream) {
      shutdown_write_ = true;
    }
    return TransportIoResult::success(0);
  }

  IoHandle& io_handle = callbacks_->ioHandle();
  uint64_t total_bytes_sent = 0;

  // Gather slices for vectored I/O
  constexpr size_t max_iovecs = 16;
  ConstRawSlice slices[max_iovecs];
  const size_t num_slices = buffer.getRawSlices(slices, max_iovecs);

  // Send data using writev
  auto result = io_handle.writev(slices, num_slices);

  if (!result.ok()) {
    // Handle would-block
    if (result.wouldBlock()) {
      return TransportIoResult::stop();
    }

    // Handle connection reset/broken pipe
    int error_code = result.error_code();
    if (error_code == ECONNRESET || error_code == EPIPE) {
      return TransportIoResult::close();
    }

    // Other errors
    failure_reason_ =
        result.error_info ? result.error_info->message : "Unknown error";
    return TransportIoResult::error(
        make_error(jsonrpc::INTERNAL_ERROR, failure_reason_));
  }

  total_bytes_sent = *result;

  // Drain sent data from buffer
  buffer.drain(total_bytes_sent);

  // Handle end of stream
  if (buffer.length() == 0 && end_stream) {
    shutdown_write_ = true;
    callbacks_->flushWriteBuffer();
  }

  return TransportIoResult::success(total_bytes_sent);
}

void RawBufferTransportSocket::onConnected() { connected_ = true; }

// RawBufferTransportSocketFactory implementation

TransportSocketPtr RawBufferTransportSocketFactory::createTransportSocket(
    TransportSocketOptionsSharedPtr options) const {
  (void)options;  // Unused for raw buffer
  return std::make_unique<RawBufferTransportSocket>();
}

TransportSocketPtr RawBufferTransportSocketFactory::createTransportSocket()
    const {
  return std::make_unique<RawBufferTransportSocket>();
}

void RawBufferTransportSocketFactory::hashKey(
    std::vector<uint8_t>& key, TransportSocketOptionsSharedPtr options) const {
  (void)options;
  // Raw buffer transport has no configuration to hash
  key.push_back(0);  // Just a marker byte
}

}  // namespace network
}  // namespace mcp