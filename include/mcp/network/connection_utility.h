#pragma once

#include <chrono>

#include "mcp/network/io_handle.h"  // For os_fd_t
#include "mcp/network/socket.h"

namespace mcp {
namespace network {

/**
 * Utility class for configuring connection sockets
 */
class SocketConfigUtility {
 public:
  /**
   * Set socket options optimized for performance (simple interface)
   * @param fd Platform-specific socket/fd (os_fd_t: int on Unix, SOCKET on Windows)
   */
  static void setSocketOptions(os_fd_t fd);

  /**
   * Configure a socket for optimal connection settings
   * @param socket The socket to configure
   * @param is_server_connection Whether this is a server-side connection
   * @param no_delay Whether to enable TCP_NODELAY (disable Nagle's algorithm)
   */
  static void configureSocket(Socket& socket,
                              bool is_server_connection,
                              bool no_delay = true);

  /**
   * Configure keep-alive settings on a socket
   * @param socket The socket to configure
   * @param enable Whether to enable keep-alive
   * @param idle_time Time before sending first keep-alive probe
   * @param interval Interval between keep-alive probes
   * @param probes Number of probes before connection is dropped
   */
  static void configureKeepAlive(
      Socket& socket,
      bool enable = true,
      std::chrono::seconds idle_time = std::chrono::seconds(7200),
      std::chrono::seconds interval = std::chrono::seconds(75),
      uint32_t probes = 9);

  /**
   * Configure socket buffer sizes
   * @param socket The socket to configure
   * @param receive_buffer_size Receive buffer size (0 = system default)
   * @param send_buffer_size Send buffer size (0 = system default)
   */
  static void configureBufferSizes(Socket& socket,
                                   uint32_t receive_buffer_size,
                                   uint32_t send_buffer_size);

  /**
   * Configure socket for low latency
   * @param socket The socket to configure
   */
  static void configureForLowLatency(Socket& socket);

  /**
   * Configure socket for high throughput
   * @param socket The socket to configure
   */
  static void configureForHighThroughput(Socket& socket);
};

}  // namespace network
}  // namespace mcp