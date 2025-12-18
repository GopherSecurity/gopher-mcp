#include "mcp/network/connection_utility.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#include "mcp/network/socket_option_impl.h"

namespace mcp {
namespace network {

void SocketConfigUtility::setSocketOptions(os_fd_t fd) {
  if (fd == INVALID_SOCKET_FD)
    return;

  // Set TCP_NODELAY to disable Nagle's algorithm
  int flag = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
             reinterpret_cast<const char*>(&flag), sizeof(flag));

  // Enable keep-alive
  int keepalive = 1;
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
             reinterpret_cast<const char*>(&keepalive), sizeof(keepalive));

  // Set keep-alive parameters
#ifdef __linux__
  int keepidle = 60;   // Start keepalives after 60 seconds
  int keepintvl = 10;  // Interval between keepalives
  int keepcnt = 3;     // Number of keepalives before death
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));

  // Set TCP_USER_TIMEOUT for connection timeout
  int user_timeout = 30000;  // 30 seconds
  setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &user_timeout,
             sizeof(user_timeout));

  // Enable TCP_QUICKACK for lower latency
  int quickack = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &quickack, sizeof(quickack));
#endif

#ifdef __APPLE__
  // macOS uses TCP_KEEPALIVE for idle time
  int keepalive_time = 60;
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &keepalive_time,
             sizeof(keepalive_time));
#endif

#ifdef SO_NOSIGPIPE
  // Prevent SIGPIPE on macOS/BSD
  int nosigpipe = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof(nosigpipe));
#endif

  // Set socket buffer sizes
  int sndbuf = 256 * 1024;
  int rcvbuf = 256 * 1024;
  setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
             reinterpret_cast<const char*>(&sndbuf), sizeof(sndbuf));
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
             reinterpret_cast<const char*>(&rcvbuf), sizeof(rcvbuf));
}

void SocketConfigUtility::configureSocket(Socket& socket,
                                          bool is_server_connection,
                                          bool no_delay) {
  // Simply delegate to the fd-based version
  setSocketOptions(socket.ioHandle().fd());
}

void SocketConfigUtility::configureKeepAlive(Socket& socket,
                                             bool enable,
                                             std::chrono::seconds idle_time,
                                             std::chrono::seconds interval,
                                             uint32_t probes) {
  os_fd_t fd = socket.ioHandle().fd();

  // Enable/disable keep-alive
  int keep_alive = enable ? 1 : 0;
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
             reinterpret_cast<const char*>(&keep_alive), sizeof(keep_alive));

  if (!enable)
    return;

#ifdef __linux__
  // Linux-specific keep-alive parameters
  int idle = idle_time.count();
  int intvl = interval.count();
  int cnt = probes;

  setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#endif

#ifdef __APPLE__
  // macOS uses TCP_KEEPALIVE for idle time
  int idle = idle_time.count();
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &idle, sizeof(idle));
#endif
}

void SocketConfigUtility::configureBufferSizes(Socket& socket,
                                               uint32_t receive_buffer_size,
                                               uint32_t send_buffer_size) {
  os_fd_t fd = socket.ioHandle().fd();

  if (receive_buffer_size > 0) {
    int size = receive_buffer_size;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&size), sizeof(size));
  }

  if (send_buffer_size > 0) {
    int size = send_buffer_size;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
               reinterpret_cast<const char*>(&size), sizeof(size));
  }
}

void SocketConfigUtility::configureForLowLatency(Socket& socket) {
  os_fd_t fd = socket.ioHandle().fd();

  // Disable Nagle's algorithm
  int nodelay = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
             reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));

#ifdef __linux__
  // Enable TCP_QUICKACK for lower latency on Linux
  int quickack = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &quickack, sizeof(quickack));
#endif

  // Use smaller buffers for lower latency
  configureBufferSizes(socket, 64 * 1024, 64 * 1024);
}

void SocketConfigUtility::configureForHighThroughput(Socket& socket) {
  os_fd_t fd = socket.ioHandle().fd();

  // Enable Nagle's algorithm for better throughput
  int nodelay = 0;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
             reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));

  // Use larger buffers for higher throughput
  configureBufferSizes(socket, 512 * 1024, 512 * 1024);

  // Configure keep-alive with longer intervals
  configureKeepAlive(socket, true, std::chrono::seconds(120),
                     std::chrono::seconds(30), 6);
}

}  // namespace network
}  // namespace mcp