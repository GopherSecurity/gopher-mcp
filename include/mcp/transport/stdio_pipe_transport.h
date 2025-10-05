#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include "mcp/buffer.h"
#include "mcp/core/compat.h"
#include "mcp/network/io_handle.h"
#include "mcp/network/socket.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/transport_socket.h"
#include "mcp/transport/pipe_io_handle.h"

namespace mcp {
namespace transport {

// Configuration for stdio pipe transport
struct StdioPipeTransportConfig {
  int stdin_fd = 0;   // Default to STDIN_FILENO
  int stdout_fd = 1;  // Default to STDOUT_FILENO
  int stderr_fd = 2;  // Default to STDERR_FILENO
  bool non_blocking = true;
  size_t buffer_size = 65536;  // 64KB buffer
};

/**
 * StdioPipeTransport - Bridges stdio file descriptors to internal pipes for use
 * with ConnectionImpl
 *
 * Architecture Overview:
 * ======================
 * This transport creates a bridge between stdio (stdin/stdout) and internal
 * pipes that ConnectionImpl can use with its event-driven I/O model.
 *
 * Data Flow:
 * ----------
 * 1. Input path (stdin -> application):
 *    stdin_fd -> [bridge thread] -> stdin_to_conn_pipe[1] ->
 * stdin_to_conn_pipe[0] -> ConnectionImpl
 *
 * 2. Output path (application -> stdout):
 *    ConnectionImpl -> conn_to_stdout_pipe[1] -> conn_to_stdout_pipe[0] ->
 * [bridge thread] -> stdout_fd
 *
 * Why This Design?
 * ----------------
 * - ConnectionImpl expects a socket-like interface with file events
 * (epoll/kqueue)
 * - stdin/stdout are regular file descriptors that may not support event
 * notification
 * - Pipes DO support event notification and can be used with epoll/kqueue
 * - Bridge threads handle the blocking I/O on stdio, while ConnectionImpl uses
 * non-blocking I/O on pipes
 *
 * Thread Safety:
 * --------------
 * - Bridge threads run independently and communicate via pipes
 * - File descriptors are captured by value to avoid race conditions
 * - Atomic flags coordinate shutdown
 */
// Transport socket that bridges stdio to pipes
class StdioPipeTransport : public network::TransportSocket {
 public:
  explicit StdioPipeTransport(const StdioPipeTransportConfig& config);
  ~StdioPipeTransport() override;

  // Initialize pipes and start bridge threads
  VoidResult initialize();

  // Get the pipe socket for ConnectionImpl to use
  std::unique_ptr<network::ConnectionSocketImpl> takePipeSocket();

  // TransportSocket interface
  void setTransportSocketCallbacks(
      network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override { return "stdio"; }
  std::string failureReason() const override { return failure_reason_; }
  bool canFlushClose() override { return true; }
  void closeSocket(network::ConnectionEvent event) override;
  TransportIoResult doRead(Buffer& buffer) override;
  TransportIoResult doWrite(Buffer& buffer, bool end_stream) override;
  void onConnected() override;
  network::SslConnectionInfoConstSharedPtr ssl() const override {
    return nullptr;
  }
  VoidResult connect(network::Socket& socket) override;

 private:
  // Bridge functions run in separate threads
  void bridgeStdinToPipe(int stdin_fd,
                         int write_pipe_fd,
                         std::atomic<bool>* running);
  void bridgePipeToStdout(int read_pipe_fd,
                          int stdout_fd,
                          std::atomic<bool>* running);
  void setNonBlocking(int fd);

  StdioPipeTransportConfig config_;
  network::TransportSocketCallbacks* callbacks_ = nullptr;

  // Pipes for bridging
  int stdin_to_conn_pipe_[2];   // stdin -> pipe -> ConnectionImpl
  int conn_to_stdout_pipe_[2];  // ConnectionImpl -> pipe -> stdout

  // Bridge threads
  std::thread stdin_bridge_thread_;
  std::thread stdout_bridge_thread_;

  // State
  std::atomic<bool> running_{false};
  std::atomic<bool> connected_{false};
  std::string failure_reason_;

  // Buffers for message framing (newline-delimited JSON)
  std::unique_ptr<Buffer> read_buffer_;
  std::string partial_message_;

  // The pipe socket that ConnectionImpl will use
  std::unique_ptr<network::ConnectionSocketImpl> pipe_socket_;
};

// Factory for creating stdio pipe transport sockets
class StdioPipeTransportFactory
    : public network::UniversalTransportSocketFactory {
 public:
  explicit StdioPipeTransportFactory(const StdioPipeTransportConfig& config);
  ~StdioPipeTransportFactory() override = default;

  // UniversalTransportSocketFactory
  network::TransportSocketPtr createTransportSocket(
      network::TransportSocketOptionsSharedPtr options) const override;
  void hashKey(std::vector<uint8_t>& key,
               network::TransportSocketOptionsSharedPtr options) const override;

  network::TransportSocketPtr createTransportSocket() const override;

 private:
  StdioPipeTransportConfig config_;
};

}  // namespace transport
}  // namespace mcp