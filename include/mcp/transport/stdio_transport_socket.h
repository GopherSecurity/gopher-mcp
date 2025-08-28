#ifndef MCP_TRANSPORT_STDIO_TRANSPORT_SOCKET_H
#define MCP_TRANSPORT_STDIO_TRANSPORT_SOCKET_H

#include "mcp/network/transport_socket.h"

namespace mcp {
namespace transport {

/**
 * StdIO transport socket configuration
 */
struct StdioTransportSocketConfig {
  int stdin_fd{0};          // File descriptor for stdin
  int stdout_fd{1};         // File descriptor for stdout
  bool non_blocking{true};  // Whether to set non-blocking mode
  bool use_bridge{true};     // Whether to use bridge threads (false for test pipes)
};

/**
 * StdIO transport socket
 *
 * Implements transport over stdin/stdout for stdio-based MCP servers
 */
class StdioTransportSocket : public network::TransportSocket {
 public:
  explicit StdioTransportSocket(const StdioTransportSocketConfig& config);
  ~StdioTransportSocket() override;

  // TransportSocket interface
  void setTransportSocketCallbacks(
      network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override { return "stdio"; }
  std::string failureReason() const override { return failure_reason_; }
  bool canFlushClose() override { return true; }
  VoidResult connect(network::Socket& socket) override;
  void closeSocket(network::ConnectionEvent event) override;
  TransportIoResult doRead(Buffer& buffer) override;
  TransportIoResult doWrite(Buffer& buffer, bool end_stream) override;
  void onConnected() override;

 private:
  // Configuration
  StdioTransportSocketConfig config_;

  // State
  network::TransportSocketCallbacks* callbacks_{nullptr};
  std::string failure_reason_;
  bool connected_{false};
  bool shutdown_read_{false};
  bool shutdown_write_{false};

  // Buffering for partial reads/writes
  std::unique_ptr<Buffer> read_buffer_;

  // Helper methods
  void setNonBlocking(int fd);
  TransportIoResult performRead(Buffer& buffer);
  TransportIoResult performWrite(Buffer& buffer);
};

/**
 * StdIO transport socket factory
 */
class StdioTransportSocketFactory
    : public network::UniversalTransportSocketFactory {
 public:
  explicit StdioTransportSocketFactory(
      const StdioTransportSocketConfig& config);

  // TransportSocketFactoryBase interface
  bool implementsSecureTransport() const override { return false; }
  std::string name() const override { return "stdio"; }

  // ClientTransportSocketFactory interface
  network::TransportSocketPtr createTransportSocket(
      network::TransportSocketOptionsSharedPtr options) const override;
  void hashKey(std::vector<uint8_t>& key,
               network::TransportSocketOptionsSharedPtr options) const override;

  // ServerTransportSocketFactory interface
  network::TransportSocketPtr createTransportSocket() const override;

 private:
  StdioTransportSocketConfig config_;
};

/**
 * Create a stdio transport socket factory
 */
inline std::unique_ptr<network::TransportSocketFactoryBase>
createStdioTransportSocketFactory(
    const StdioTransportSocketConfig& config = {}) {
  return std::make_unique<StdioTransportSocketFactory>(config);
}

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_STDIO_TRANSPORT_SOCKET_H