/**
 * @file tcp_transport_socket.h
 * @brief TCP transport socket implementation using state machine
 *
 * This implementation provides a basic TCP transport socket that integrates
 * with the TransportSocketStateMachine for proper state management.
 */

#ifndef MCP_TRANSPORT_TCP_TRANSPORT_SOCKET_H
#define MCP_TRANSPORT_TCP_TRANSPORT_SOCKET_H

#include <memory>
#include <string>

#include "mcp/buffer.h"
#include "mcp/core/result.h"
#include "mcp/network/socket.h"
#include "mcp/network/transport_socket.h"
#include "mcp/transport/transport_socket_state_machine.h"

namespace mcp {
namespace transport {

/**
 * TCP transport socket configuration
 */
struct TcpTransportSocketConfig {
  // Connection timeout in milliseconds
  std::chrono::milliseconds connect_timeout{30000};

  // Read/write timeout in milliseconds
  std::chrono::milliseconds io_timeout{60000};

  // Enable TCP keep-alive
  bool tcp_keepalive{true};

  // TCP no-delay (disable Nagle's algorithm)
  bool tcp_nodelay{true};
};

/**
 * TCP transport socket implementation
 *
 * Provides a basic TCP transport that integrates with the
 * TcpTransportSocketStateMachine for proper state management.
 */
class TcpTransportSocket : public network::TransportSocket {
 public:
  explicit TcpTransportSocket(event::Dispatcher& dispatcher,
                              const TcpTransportSocketConfig& config = {});
  ~TcpTransportSocket() override;

  // TransportSocket interface
  void setTransportSocketCallbacks(
      network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override { return "tcp"; }
  std::string failureReason() const override { return failure_reason_; }
  bool canFlushClose() override { return true; }
  void closeSocket(network::ConnectionEvent event) override;
  network::TransportIoResult doRead(Buffer& buffer) override;
  network::TransportIoResult doWrite(Buffer& buffer, bool end_stream) override;
  void onConnected() override;
  VoidResult connect(network::Socket& socket) override;
  network::SslConnectionInfoConstSharedPtr ssl() const override {
    return nullptr;
  }
  bool startSecureTransport() override { return false; }
  void configureInitialCongestionWindow(
      uint64_t bandwidth_bits_per_sec, std::chrono::microseconds rtt) override {
  }

 private:
  // State machine integration
  void onStateChanged(TransportSocketState old_state,
                      TransportSocketState new_state);
  void configureStateMachine();

  // Configuration
  TcpTransportSocketConfig config_;

  // State machine
  std::unique_ptr<TransportSocketStateMachine> state_machine_;

  // Callbacks
  network::TransportSocketCallbacks* callbacks_{nullptr};

  // Failure reason
  std::string failure_reason_;

  // Dispatcher reference
  event::Dispatcher& dispatcher_;
};

using TcpTransportSocketPtr = std::unique_ptr<TcpTransportSocket>;
using TcpTransportSocketSharedPtr = std::shared_ptr<TcpTransportSocket>;

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_TCP_TRANSPORT_SOCKET_H