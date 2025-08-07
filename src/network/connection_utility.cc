#include "mcp/network/connection_utility.h"
#include "mcp/network/socket_option_impl.h"
#include <netinet/tcp.h>
#include <sys/socket.h>

namespace mcp {
namespace network {

void ConnectionUtility::configureSocket(Socket& socket, 
                                       bool is_server_connection,
                                       bool no_delay) {
  // Set TCP_NODELAY to disable Nagle's algorithm for low latency
  if (no_delay) {
    int flag = 1;
    auto option = std::make_unique<SocketOptionImpl>(
        IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    option->setOption(socket, Socket::SocketState::PreBind);
  }

  // Configure keep-alive with reasonable defaults
  configureKeepAlive(socket, true);

  // For server connections, we might want different buffer sizes
  if (is_server_connection) {
    // Server connections often benefit from larger buffers
    configureBufferSizes(socket, 256 * 1024, 256 * 1024);
  } else {
    // Client connections can use smaller buffers
    configureBufferSizes(socket, 128 * 1024, 128 * 1024);
  }

  // Set SO_REUSEADDR for server sockets (though this is usually set at bind time)
  if (is_server_connection) {
    int flag = 1;
    auto option = std::make_unique<SocketOptionImpl>(
        SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    option->setOption(socket, Socket::SocketState::PreBind);
  }
}

void ConnectionUtility::configureKeepAlive(Socket& socket,
                                          bool enable,
                                          std::chrono::seconds idle_time,
                                          std::chrono::seconds interval,
                                          uint32_t probes) {
  // Enable/disable keep-alive
  int keep_alive = enable ? 1 : 0;
  auto ka_option = std::make_unique<SocketOptionImpl>(
      SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive));
  ka_option->setOption(socket, Socket::SocketState::PreBind);

  if (enable) {
#ifdef __linux__
    // Linux-specific keep-alive parameters
    int idle_secs = static_cast<int>(idle_time.count());
    auto idle_option = std::make_unique<SocketOptionImpl>(
        IPPROTO_TCP, TCP_KEEPIDLE, &idle_secs, sizeof(idle_secs));
    idle_option->setOption(socket, Socket::SocketState::PreBind);

    int interval_secs = static_cast<int>(interval.count());
    auto interval_option = std::make_unique<SocketOptionImpl>(
        IPPROTO_TCP, TCP_KEEPINTVL, &interval_secs, sizeof(interval_secs));
    interval_option->setOption(socket, Socket::SocketState::PreBind);

    int probe_count = static_cast<int>(probes);
    auto probe_option = std::make_unique<SocketOptionImpl>(
        IPPROTO_TCP, TCP_KEEPCNT, &probe_count, sizeof(probe_count));
    probe_option->setOption(socket, Socket::SocketState::PreBind);
#elif defined(__APPLE__)
    // macOS uses TCP_KEEPALIVE instead of TCP_KEEPIDLE
    int idle_secs = static_cast<int>(idle_time.count());
    auto idle_option = std::make_unique<SocketOptionImpl>(
        IPPROTO_TCP, TCP_KEEPALIVE, &idle_secs, sizeof(idle_secs));
    idle_option->setOption(socket, Socket::SocketState::PreBind);
    
    // Note: macOS doesn't support TCP_KEEPINTVL and TCP_KEEPCNT directly
    // They use TCP_KEEPINTVL and TCP_KEEPCNT but with different behavior
#ifdef TCP_KEEPINTVL
    int interval_secs = static_cast<int>(interval.count());
    auto interval_option = std::make_unique<SocketOptionImpl>(
        IPPROTO_TCP, TCP_KEEPINTVL, &interval_secs, sizeof(interval_secs));
    interval_option->setOption(socket, Socket::SocketState::PreBind);
#endif

#ifdef TCP_KEEPCNT
    int probe_count = static_cast<int>(probes);
    auto probe_option = std::make_unique<SocketOptionImpl>(
        IPPROTO_TCP, TCP_KEEPCNT, &probe_count, sizeof(probe_count));
    probe_option->setOption(socket, Socket::SocketState::PreBind);
#endif
#endif
  }
}

void ConnectionUtility::configureBufferSizes(Socket& socket,
                                            uint32_t receive_buffer_size,
                                            uint32_t send_buffer_size) {
  if (receive_buffer_size > 0) {
    auto recv_option = std::make_unique<SocketOptionImpl>(
        SOL_SOCKET, SO_RCVBUF, &receive_buffer_size, sizeof(receive_buffer_size));
    recv_option->setOption(socket, Socket::SocketState::PreBind);
  }

  if (send_buffer_size > 0) {
    auto send_option = std::make_unique<SocketOptionImpl>(
        SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size));
    send_option->setOption(socket, Socket::SocketState::PreBind);
  }
}

void ConnectionUtility::configureForLowLatency(Socket& socket) {
  // Disable Nagle's algorithm
  int flag = 1;
  auto nodelay_option = std::make_unique<SocketOptionImpl>(
      IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
  nodelay_option->setOption(socket, Socket::SocketState::PreBind);

  // Use smaller buffers for lower latency
  configureBufferSizes(socket, 64 * 1024, 64 * 1024);

#ifdef TCP_QUICKACK
  // Linux-specific: Enable TCP quick ACK
  int quickack = 1;
  auto quickack_option = std::make_unique<SocketOptionImpl>(
      IPPROTO_TCP, TCP_QUICKACK, &quickack, sizeof(quickack));
  quickack_option->setOption(socket, Socket::SocketState::PreBind);
#endif
}

void ConnectionUtility::configureForHighThroughput(Socket& socket) {
  // Enable Nagle's algorithm for better throughput
  int flag = 0;
  auto nodelay_option = std::make_unique<SocketOptionImpl>(
      IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
  nodelay_option->setOption(socket, Socket::SocketState::PreBind);

  // Use larger buffers for better throughput
  configureBufferSizes(socket, 512 * 1024, 512 * 1024);

#ifdef SO_RCVLOWAT
  // Set minimum data before waking up receiver
  int lowat = 4096;
  auto lowat_option = std::make_unique<SocketOptionImpl>(
      SOL_SOCKET, SO_RCVLOWAT, &lowat, sizeof(lowat));
  lowat_option->setOption(socket, Socket::SocketState::PreBind);
#endif
}

} // namespace network
} // namespace mcp