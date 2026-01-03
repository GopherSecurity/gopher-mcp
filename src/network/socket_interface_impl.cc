#include "mcp/network/socket_interface_impl.h"

#include <cstring>
#include <map>
#include <mutex>

#include "mcp/logging/log_macros.h"

#ifdef _WIN32
#include <mswsock.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
// Define O_NONBLOCK for Windows compatibility
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x0004
#endif
#ifndef FD_CLOEXEC
#define FD_CLOEXEC 1
#endif
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#include "mcp/network/address_impl.h"
#include "mcp/network/io_socket_handle_impl.h"

namespace mcp {
namespace network {

namespace {

// Additional platform-specific error codes not in io_handle.h
// Common error codes (SOCKET_ERROR_AGAIN, SOCKET_ERROR_INPROGRESS,
// SOCKET_ERROR_WOULDBLOCK, getLastSocketError) are defined in io_handle.h
#ifdef _WIN32
constexpr int SOCKET_ERROR_INVAL = WSAEINVAL;
constexpr int SOCKET_ERROR_AFNOSUPPORT = WSAEAFNOSUPPORT;

class WinsockInitializer {
 public:
  WinsockInitializer() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
      throw std::runtime_error("WSAStartup failed");
    }
  }

  ~WinsockInitializer() { WSACleanup(); }
};

// Initialize Winsock on Windows
static WinsockInitializer winsock_init;

#else
constexpr int SOCKET_ERROR_INVAL = EINVAL;
constexpr int SOCKET_ERROR_AFNOSUPPORT = EAFNOSUPPORT;
#endif

// Convert address type and IP version to socket domain
int addressTypeToDomain(Address::Type addr_type,
                        optional<Address::IpVersion> version = nullopt) {
  switch (addr_type) {
    case Address::Type::Ip:
      if (version.has_value()) {
        return (*version == Address::IpVersion::v4) ? AF_INET : AF_INET6;
      }
      return AF_INET;  // Default to IPv4

    case Address::Type::Pipe:
#ifdef _WIN32
      return -1;  // Not supported on Windows yet
#else
      return AF_UNIX;
#endif

    default:
      return -1;
  }
}

// Convert socket type to system constant
int socketTypeToInt(SocketType type) {
  switch (type) {
    case SocketType::Stream:
      return SOCK_STREAM;
    case SocketType::Datagram:
      return SOCK_DGRAM;
    case SocketType::Raw:
      return SOCK_RAW;
    default:
      return -1;
  }
}

// Singleton storage
std::mutex g_socket_interface_mutex;
SocketInterfacePtr g_socket_interface;
std::map<std::string, SocketInterfaceLoaderPtr> g_socket_interface_loaders;

}  // namespace

// Implementation of SocketInterfaceImpl methods
// Class is already declared in the header file
SocketInterfaceImpl::SocketInterfaceImpl() : platform_name_(detectPlatform()) {
  initializeCapabilities();
}

IoResult<os_fd_t> SocketInterfaceImpl::socket(
    SocketType type,
    Address::Type addr_type,
    optional<Address::IpVersion> version,
    bool socket_v6only) {
  GOPHER_LOG_TRACE("socket() requested: type={} addr_type={} version={} v6only={}",
                   static_cast<int>(type), static_cast<int>(addr_type),
                   version.has_value() ? static_cast<int>(*version) : -1,
                   socket_v6only);

  int domain = addressTypeToDomain(addr_type, version);
  if (domain < 0) {
    GOPHER_LOG_TRACE("socket() failed: unsupported domain");
    return IoResult<os_fd_t>::error(SOCKET_ERROR_AFNOSUPPORT);
  }

  int sock_type = socketTypeToInt(type);
  if (sock_type < 0) {
    GOPHER_LOG_TRACE("socket() failed: invalid type");
    return IoResult<os_fd_t>::error(SOCKET_ERROR_INVAL);
  }

#ifdef __linux__
  // Use SOCK_CLOEXEC and SOCK_NONBLOCK on Linux
  sock_type |= SOCK_CLOEXEC | SOCK_NONBLOCK;
#endif

  os_fd_t fd = ::socket(domain, sock_type, 0);
  if (fd == INVALID_SOCKET_FD) {
    GOPHER_LOG_TRACE("::socket() failed: error={}", getLastSocketError());
    return IoResult<os_fd_t>::error(getLastSocketError());
  }
  GOPHER_LOG_TRACE("::socket() success: fd={}", fd);

  // Set non-blocking and close-on-exec on other platforms
#ifndef __linux__
  int nb_result = setNonBlocking(fd);
  if (nb_result != 0) {
    GOPHER_LOG_ERROR("Failed to set socket non-blocking: fd={}", fd);
    // Continue anyway - socket may still work
  }
  setCloseOnExec(fd);
#endif

  // Set IPv6-only option if requested
  if (socket_v6only && domain == AF_INET6) {
    int opt = 1;
    ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));
  }

  return IoResult<os_fd_t>::success(fd);
}

IoResult<int> SocketInterfaceImpl::socketPair(SocketType type, os_fd_t fds[2]) {
#ifdef _WIN32
  // Windows doesn't have socketpair, emulate with TCP sockets
  return emulateSocketPairWindows(type, fds);
#else
  int sock_type = socketTypeToInt(type);
  if (sock_type < 0) {
    return IoResult<int>::error(SOCKET_ERROR_INVAL);
  }

#ifdef __linux__
  sock_type |= SOCK_CLOEXEC | SOCK_NONBLOCK;
#endif

  int result = ::socketpair(AF_UNIX, sock_type, 0, fds);
  if (result < 0) {
    return IoResult<int>::error(getLastSocketError());
  }

#ifndef __linux__
  // Set non-blocking and close-on-exec
  setNonBlocking(fds[0]);
  setNonBlocking(fds[1]);
  setCloseOnExec(fds[0]);
  setCloseOnExec(fds[1]);
#endif

  return IoResult<int>::success(0);
#endif
}

IoHandlePtr SocketInterfaceImpl::ioHandleForFd(os_fd_t fd,
                                               bool socket_v6only,
                                               optional<int> domain) {
  return std::make_unique<IoSocketHandleImpl>(fd, socket_v6only, domain);
}

IoResult<int> SocketInterfaceImpl::close(os_fd_t fd) {
#ifdef _WIN32
  int result = ::closesocket(fd);
#else
  int result = ::close(fd);
#endif
  if (result < 0) {
    return IoResult<int>::error(getLastSocketError());
  }
  return IoResult<int>::success(0);
}

IoResult<os_fd_t> SocketInterfaceImpl::duplicate(os_fd_t fd) {
#ifdef _WIN32
  // Windows socket duplication is more complex
  WSAPROTOCOL_INFO info;
  GOPHER_LOG_TRACE("SocketInterfaceImpl::duplicate() called: fd={}", fd);
  if (::WSADuplicateSocket(fd, ::GetCurrentProcessId(), &info) != 0) {
    GOPHER_LOG_TRACE("WSADuplicateSocket() failed: WSAError={}", WSAGetLastError());
    return IoResult<os_fd_t>::error(getLastSocketError());
  }

  GOPHER_LOG_TRACE("WSADuplicateSocket() success, calling WSASocket()");
  os_fd_t new_fd =
      ::WSASocket(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO,
                  &info, 0, WSA_FLAG_OVERLAPPED);
  GOPHER_LOG_TRACE("WSASocket() returned: new_fd={} (INVALID_SOCKET={})", new_fd, INVALID_SOCKET);
  if (new_fd == INVALID_SOCKET) {
    GOPHER_LOG_TRACE("WSASocket() failed: WSAError={}", WSAGetLastError());
    return IoResult<os_fd_t>::error(getLastSocketError());
  }
  return IoResult<os_fd_t>::success(new_fd);
#else
  int new_fd = ::dup(fd);
  if (new_fd < 0) {
    return IoResult<os_fd_t>::error(getLastSocketError());
  }

  setCloseOnExec(new_fd);
  return IoResult<os_fd_t>::success(new_fd);
#endif
}

IoResult<int> SocketInterfaceImpl::setFileFlags(os_fd_t fd, int flags) {
#ifdef _WIN32
  // Windows doesn't have fcntl, handle specific flags
  if (flags & O_NONBLOCK) {
    u_long mode = 1;
    if (::ioctlsocket(fd, FIONBIO, &mode) != 0) {
      return IoResult<int>::error(getLastSocketError());
    }
  }
  return IoResult<int>::success(0);
#else
  int current = ::fcntl(fd, F_GETFL, 0);
  if (current < 0) {
    return IoResult<int>::error(errno);
  }

  if (::fcntl(fd, F_SETFL, current | flags) < 0) {
    return IoResult<int>::error(errno);
  }

  // Handle FD_CLOEXEC separately
  if (flags & FD_CLOEXEC) {
    if (::fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
      return IoResult<int>::error(errno);
    }
  }

  return IoResult<int>::success(0);
#endif
}

IoResult<int> SocketInterfaceImpl::getFileFlags(os_fd_t fd) {
#ifdef _WIN32
  // Limited flag support on Windows
  return IoResult<int>::success(0);
#else
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return IoResult<int>::error(errno);
  }
  return IoResult<int>::success(flags);
#endif
}

IoResult<int> SocketInterfaceImpl::setsockopt(
    os_fd_t fd, int level, int optname, const void* optval, socklen_t optlen) {
#ifdef _WIN32
  int result = ::setsockopt(fd, level, optname,
                            static_cast<const char*>(optval), optlen);
#else
  int result = ::setsockopt(fd, level, optname, optval, optlen);
#endif
  if (result < 0) {
    return IoResult<int>::error(getLastSocketError());
  }
  return IoResult<int>::success(0);
}

IoResult<int> SocketInterfaceImpl::getsockopt(
    os_fd_t fd, int level, int optname, void* optval, socklen_t* optlen) {
#ifdef _WIN32
  int result =
      ::getsockopt(fd, level, optname, static_cast<char*>(optval), optlen);
#else
  int result = ::getsockopt(fd, level, optname, optval, optlen);
#endif
  if (result < 0) {
    return IoResult<int>::error(getLastSocketError());
  }
  return IoResult<int>::success(0);
}

IoResult<int> SocketInterfaceImpl::bind(os_fd_t fd,
                                        const Address::Instance& addr) {
  GOPHER_LOG_TRACE("bind() called: fd={} addr={}", fd, addr.asStringView());
  int result = ::bind(fd, addr.sockAddr(), addr.sockAddrLen());
  if (result < 0) {
    GOPHER_LOG_TRACE("::bind() failed: error={}", getLastSocketError());
    return IoResult<int>::error(getLastSocketError());
  }
  GOPHER_LOG_TRACE("::bind() success");
  return IoResult<int>::success(0);
}

IoResult<int> SocketInterfaceImpl::listen(os_fd_t fd, int backlog) {
  GOPHER_LOG_TRACE("SocketInterfaceImpl::listen() called: fd={} backlog={}", fd, backlog);

  int result = ::listen(fd, backlog);
#ifdef _WIN32
  if (result != 0) {
    GOPHER_LOG_TRACE("::listen() returned: {} WSAError={}", result, WSAGetLastError());
  } else {
    GOPHER_LOG_TRACE("::listen() returned: {}", result);
  }
#else
  GOPHER_LOG_TRACE("::listen() returned: {}", result);
#endif

  if (result < 0) {
    return IoResult<int>::error(getLastSocketError());
  }

  int accept_conn = 0;
  socklen_t optlen = sizeof(accept_conn);
  int opt_result = ::getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN,
                                reinterpret_cast<char*>(&accept_conn), &optlen);
  if (opt_result == 0) {
    GOPHER_LOG_TRACE("SO_ACCEPTCONN={}", accept_conn);
  } else {
    GOPHER_LOG_TRACE("SO_ACCEPTCONN check failed: {}", getLastSocketError());
  }

  return IoResult<int>::success(0);
}

IoResult<os_fd_t> SocketInterfaceImpl::accept(os_fd_t fd,
                                              sockaddr* addr,
                                              socklen_t* addrlen) {
#ifdef __linux__
  // Use accept4 on Linux for SOCK_CLOEXEC and SOCK_NONBLOCK
  os_fd_t new_fd = ::accept4(fd, addr, addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (new_fd < 0) {
    // Fall back to accept if accept4 not available
    if (errno == ENOSYS) {
      new_fd = ::accept(fd, addr, addrlen);
      if (new_fd >= 0) {
        setNonBlocking(new_fd);
        setCloseOnExec(new_fd);
      }
    }
  }
#else
  os_fd_t new_fd = ::accept(fd, addr, addrlen);
  if (new_fd != INVALID_SOCKET_FD) {
    GOPHER_LOG_TRACE("accept() success: fd={}", new_fd);
    setNonBlocking(new_fd);
    setCloseOnExec(new_fd);
  } else {
    GOPHER_LOG_TRACE("accept() failed: error={}", getLastSocketError());
  }
#endif

  if (new_fd == INVALID_SOCKET_FD) {
    return IoResult<os_fd_t>::error(getLastSocketError());
  }
  return IoResult<os_fd_t>::success(new_fd);
}

IoResult<int> SocketInterfaceImpl::connect(os_fd_t fd,
                                           const Address::Instance& addr) {
  int result = ::connect(fd, addr.sockAddr(), addr.sockAddrLen());
  if (result < 0) {
    int error = getLastSocketError();
    // EINPROGRESS is expected for non-blocking connect
    if (error == SOCKET_ERROR_INPROGRESS || error == SOCKET_ERROR_AGAIN) {
      return IoResult<int>::success(0);
    }
    return IoResult<int>::error(error);
  }
  return IoResult<int>::success(0);
}

IoResult<int> SocketInterfaceImpl::shutdown(os_fd_t fd, int how) {
  int result = ::shutdown(fd, how);
  if (result < 0) {
    return IoResult<int>::error(getLastSocketError());
  }
  return IoResult<int>::success(0);
}

IoResult<Address::InstanceConstSharedPtr> SocketInterfaceImpl::localAddress(
    os_fd_t fd) {
  sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);

  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) < 0) {
    return IoResult<Address::InstanceConstSharedPtr>::error(
        getLastSocketError());
  }

  return IoResult<Address::InstanceConstSharedPtr>::success(
      Address::addressFromSockAddr(addr, addr_len, false));
}

IoResult<Address::InstanceConstSharedPtr> SocketInterfaceImpl::peerAddress(
    os_fd_t fd) {
  sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);

  if (::getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) < 0) {
    return IoResult<Address::InstanceConstSharedPtr>::error(
        getLastSocketError());
  }

  return IoResult<Address::InstanceConstSharedPtr>::success(
      Address::addressFromSockAddr(addr, addr_len, false));
}

IoResult<int> SocketInterfaceImpl::ioctl(os_fd_t fd,
                                         unsigned long request,
                                         void* argp) {
#ifdef _WIN32
  int result = ::ioctlsocket(fd, request, static_cast<u_long*>(argp));
#else
  int result = ::ioctl(fd, request, argp);
#endif
  if (result < 0) {
    return IoResult<int>::error(getLastSocketError());
  }
  return IoResult<int>::success(0);
}

optional<std::string> SocketInterfaceImpl::interfaceName(os_fd_t fd) {
  // TODO: Implement interface name retrieval
  // This requires platform-specific implementation
  (void)fd;
  return nullopt;
}

bool SocketInterfaceImpl::supportsSocketOption(int level, int optname) const {
  // Check common options
  if (level == SOL_SOCKET) {
    switch (optname) {
      case SO_REUSEADDR:
      case SO_KEEPALIVE:
      case SO_RCVBUF:
      case SO_SNDBUF:
        return true;
#ifdef SO_REUSEPORT
      case SO_REUSEPORT:
        return supports_reuse_port_;
#endif
      default:
        break;
    }
  } else if (level == IPPROTO_TCP) {
    switch (optname) {
      case TCP_NODELAY:
        return true;
#ifdef TCP_KEEPIDLE
      case TCP_KEEPIDLE:
      case TCP_KEEPINTVL:
      case TCP_KEEPCNT:
        return true;
#endif
      default:
        break;
    }
  } else if (level == IPPROTO_IP) {
#ifdef IP_TRANSPARENT
    if (optname == IP_TRANSPARENT)
      return supports_ip_transparent_;
#endif
#ifdef IP_FREEBIND
    if (optname == IP_FREEBIND)
      return supports_ip_freebind_;
#endif
  }

  // For unknown options, assume not supported
  return false;
}

const std::string& SocketInterfaceImpl::platformName() const {
  return platform_name_;
}

bool SocketInterfaceImpl::supportsIoUring() const { return supports_io_uring_; }

bool SocketInterfaceImpl::supportsUdpGro() const { return supports_udp_gro_; }

bool SocketInterfaceImpl::supportsUdpGso() const { return supports_udp_gso_; }

bool SocketInterfaceImpl::supportsIpPktInfo() const {
  return supports_ip_pktinfo_;
}

bool SocketInterfaceImpl::supportsReusePort() const {
  return supports_reuse_port_;
}

int SocketInterfaceImpl::setNonBlocking(os_fd_t fd) {
#ifdef _WIN32
  u_long mode = 1;
  int result = ::ioctlsocket(fd, FIONBIO, &mode);
  if (result != 0) {
    int err = WSAGetLastError();
    GOPHER_LOG_TRACE("setNonBlocking (ioctlsocket): fd={} mode={} result={} WSAError={} FAILED",
                     fd, mode, result, err);
  } else {
    GOPHER_LOG_TRACE("setNonBlocking (ioctlsocket): fd={} mode={} result={} SUCCESS",
                     fd, mode, result);
  }
  return result;
#else
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    GOPHER_LOG_TRACE("setNonBlocking: fcntl(F_GETFL) failed fd={} errno={}", fd, errno);
    return -1;
  }
  int result = ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  if (result < 0) {
    GOPHER_LOG_TRACE("setNonBlocking: fd={} result={} errno={} FAILED", fd, result, errno);
  } else {
    GOPHER_LOG_TRACE("setNonBlocking: fd={} result={} SUCCESS", fd, result);
  }
  return result;
#endif
}

void SocketInterfaceImpl::setCloseOnExec(os_fd_t fd) {
#ifndef _WIN32
  ::fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
}

std::string SocketInterfaceImpl::detectPlatform() {
#ifdef _WIN32
  return "windows";
#elif __APPLE__
  return "macos";
#elif __linux__
  return "linux";
#elif __FreeBSD__
  return "freebsd";
#else
  return "unknown";
#endif
}

void SocketInterfaceImpl::initializeCapabilities() {
  // Detect platform capabilities
#ifdef SO_REUSEPORT
  supports_reuse_port_ = true;
#endif

#ifdef __linux__
  // Check for io_uring support (kernel 5.1+)
  // TODO: Actual runtime detection
  supports_io_uring_ = false;

#ifdef UDP_GRO
  supports_udp_gro_ = true;
#endif

#ifdef UDP_SEGMENT
  supports_udp_gso_ = true;
#endif

#ifdef IP_TRANSPARENT
  supports_ip_transparent_ = true;
#endif

#ifdef IP_FREEBIND
  supports_ip_freebind_ = true;
#endif
#endif

#ifdef IP_PKTINFO
  supports_ip_pktinfo_ = true;
#elif defined(IP_RECVDSTADDR)
  supports_ip_pktinfo_ = true;
#endif
}

#ifdef _WIN32
IoResult<int> SocketInterfaceImpl::emulateSocketPairWindows(SocketType type,
                                                            os_fd_t fds[2]) {
  // Create listening socket
  auto listen_result =
      socket(type, Address::Type::Ip, Address::IpVersion::v4, false);
  if (!listen_result.ok()) {
    return IoResult<int>::error(listen_result.error_code());
  }
  os_fd_t listen_fd = *listen_result;

  // Bind to loopback
  auto addr = Address::loopbackAddress(Address::IpVersion::v4, 0);
  if (::bind(listen_fd, addr->sockAddr(), addr->sockAddrLen()) < 0) {
    ::closesocket(listen_fd);
    return IoResult<int>::error(getLastSocketError());
  }

  // Get assigned port
  sockaddr_storage bound_addr;
  socklen_t addr_len = sizeof(bound_addr);
  if (::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound_addr),
                    &addr_len) < 0) {
    ::closesocket(listen_fd);
    return IoResult<int>::error(getLastSocketError());
  }

  // Listen
  if (type == SocketType::Stream && ::listen(listen_fd, 1) < 0) {
    ::closesocket(listen_fd);
    return IoResult<int>::error(getLastSocketError());
  }

  // Create connecting socket
  auto connect_result =
      socket(type, Address::Type::Ip, Address::IpVersion::v4, false);
  if (!connect_result.ok()) {
    ::closesocket(listen_fd);
    return IoResult<int>::error(connect_result.error_code());
  }
  fds[0] = *connect_result;

  // Connect
  auto connect_addr = Address::addressFromSockAddr(bound_addr, addr_len, false);
  if (::connect(fds[0], connect_addr->sockAddr(), connect_addr->sockAddrLen()) <
      0) {
    int error = getLastSocketError();
    if (error != WSAEWOULDBLOCK) {
      ::closesocket(listen_fd);
      ::closesocket(fds[0]);
      return IoResult<int>::error(error);
    }
  }

  if (type == SocketType::Stream) {
    // Accept connection
    sockaddr_storage peer_addr;
    socklen_t peer_len = sizeof(peer_addr);
    fds[1] =
        ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer_addr), &peer_len);
    ::closesocket(listen_fd);

    if (fds[1] == INVALID_SOCKET) {
      ::closesocket(fds[0]);
      return IoResult<int>::error(getLastSocketError());
    }
  } else {
    // For datagram, both sockets are ready
    fds[1] = listen_fd;
  }

  setNonBlocking(fds[0]);
  setNonBlocking(fds[1]);

  return IoResult<int>::success(0);
}
#endif

// ===== Global Functions =====

SocketInterface& socketInterface() {
  std::lock_guard<std::mutex> lock(g_socket_interface_mutex);
  if (!g_socket_interface) {
    g_socket_interface = createDefaultSocketInterface();
  }
  return *g_socket_interface;
}

void setSocketInterface(SocketInterfacePtr iface) {
  std::lock_guard<std::mutex> lock(g_socket_interface_mutex);
  g_socket_interface = std::move(iface);
}

void resetSocketInterface() {
  std::lock_guard<std::mutex> lock(g_socket_interface_mutex);
  g_socket_interface.reset();
}

void registerSocketInterfaceLoader(SocketInterfaceLoaderPtr loader) {
  std::lock_guard<std::mutex> lock(g_socket_interface_mutex);
  g_socket_interface_loaders[loader->name()] = std::move(loader);
}

std::vector<std::string> getSocketInterfaceLoaderNames() {
  std::lock_guard<std::mutex> lock(g_socket_interface_mutex);
  std::vector<std::string> names;
  for (const auto& pair : g_socket_interface_loaders) {
    names.push_back(pair.first);
  }
  return names;
}

SocketInterfacePtr createDefaultSocketInterface() {
  return std::make_unique<SocketInterfaceImpl>();
}

#ifdef __linux__
// Placeholder for io_uring implementation
SocketInterfacePtr createIoUringSocketInterface() {
  // TODO: Implement io_uring socket interface
  return nullptr;
}
#endif

}  // namespace network
}  // namespace mcp
