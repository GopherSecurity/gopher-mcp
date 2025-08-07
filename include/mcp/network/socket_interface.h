#ifndef MCP_NETWORK_SOCKET_INTERFACE_H
#define MCP_NETWORK_SOCKET_INTERFACE_H

#include <memory>
#include <string>

#include "mcp/compat.h"
#include "mcp/network/address.h"
#include "mcp/network/io_handle.h"
#include "mcp/network/socket.h"
#include "mcp/compat.h"

namespace mcp {
namespace network {

/**
 * Socket interface for creating and managing sockets.
 *
 * This is the main entry point for socket operations.
 * It provides platform abstraction and allows
 * for different implementations (e.g., standard sockets, io_uring, etc.).
 */
class SocketInterface {
 public:
  virtual ~SocketInterface() = default;

  /**
   * Create a socket.
   * @param type Socket type (stream, datagram, raw)
   * @param addr_type Address type to determine domain
   * @param version IP version (for IP addresses)
   * @param socket_v6only True for IPv6-only sockets
   * @return File descriptor or error
   */
  virtual IoResult<os_fd_t> socket(
      SocketType type,
      Address::Type addr_type,
      optional<Address::IpVersion> version = nullopt,
      bool socket_v6only = false) = 0;

  /**
   * Create a socket pair (Unix domain).
   * @param type Socket type
   * @param fds Array to store the two file descriptors
   * @return Success or error
   */
  virtual IoResult<int> socketPair(SocketType type, os_fd_t fds[2]) = 0;

  /**
   * Create an IoHandle for a file descriptor.
   * @param fd File descriptor
   * @param socket_v6only True if IPv6-only socket
   * @param domain Socket domain (optional)
   * @return IoHandle instance
   */
  virtual IoHandlePtr ioHandleForFd(os_fd_t fd,
                                    bool socket_v6only = false,
                                    optional<int> domain = nullopt) = 0;

  /**
   * Close a file descriptor.
   * @param fd File descriptor to close
   * @return Success or error
   */
  virtual IoResult<int> close(os_fd_t fd) = 0;

  /**
   * Duplicate a file descriptor.
   * @param fd File descriptor to duplicate
   * @return New file descriptor or error
   */
  virtual IoResult<os_fd_t> duplicate(os_fd_t fd) = 0;

  /**
   * Set file descriptor flags.
   * @param fd File descriptor
   * @param flags Flags to set (e.g., O_NONBLOCK, FD_CLOEXEC)
   * @return Success or error
   */
  virtual IoResult<int> setFileFlags(os_fd_t fd, int flags) = 0;

  /**
   * Get file descriptor flags.
   * @param fd File descriptor
   * @return Flags or error
   */
  virtual IoResult<int> getFileFlags(os_fd_t fd) = 0;

  /**
   * Set socket option.
   * @param fd File descriptor
   * @param level Option level
   * @param optname Option name
   * @param optval Option value
   * @param optlen Option length
   * @return Success or error
   */
  virtual IoResult<int> setsockopt(os_fd_t fd,
                                   int level,
                                   int optname,
                                   const void* optval,
                                   socklen_t optlen) = 0;

  /**
   * Get socket option.
   * @param fd File descriptor
   * @param level Option level
   * @param optname Option name
   * @param optval Buffer for option value
   * @param optlen Option length (in/out)
   * @return Success or error
   */
  virtual IoResult<int> getsockopt(
      os_fd_t fd, int level, int optname, void* optval, socklen_t* optlen) = 0;

  /**
   * Bind socket to address.
   * @param fd File descriptor
   * @param addr Address to bind to
   * @return Success or error
   */
  virtual IoResult<int> bind(os_fd_t fd, const Address::Instance& addr) = 0;

  /**
   * Listen on socket.
   * @param fd File descriptor
   * @param backlog Maximum pending connections
   * @return Success or error
   */
  virtual IoResult<int> listen(os_fd_t fd, int backlog) = 0;

  /**
   * Accept connection.
   * @param fd Listening socket
   * @param addr Buffer for peer address (optional)
   * @param addrlen Address length (in/out)
   * @return New file descriptor or error
   */
  virtual IoResult<os_fd_t> accept(os_fd_t fd,
                                   sockaddr* addr = nullptr,
                                   socklen_t* addrlen = nullptr) = 0;

  /**
   * Connect to address.
   * @param fd File descriptor
   * @param addr Address to connect to
   * @return Success or error (EINPROGRESS for non-blocking)
   */
  virtual IoResult<int> connect(os_fd_t fd, const Address::Instance& addr) = 0;

  /**
   * Shutdown socket.
   * @param fd File descriptor
   * @param how SHUT_RD, SHUT_WR, or SHUT_RDWR
   * @return Success or error
   */
  virtual IoResult<int> shutdown(os_fd_t fd, int how) = 0;

  /**
   * Get local address.
   * @param fd File descriptor
   * @return Address or error
   */
  virtual IoResult<Address::InstanceConstSharedPtr> localAddress(
      os_fd_t fd) = 0;

  /**
   * Get peer address.
   * @param fd File descriptor
   * @return Address or error
   */
  virtual IoResult<Address::InstanceConstSharedPtr> peerAddress(os_fd_t fd) = 0;

  /**
   * Platform-specific ioctl.
   * @param fd File descriptor
   * @param request Request code
   * @param argp Argument pointer
   * @return Success or error
   */
  virtual IoResult<int> ioctl(os_fd_t fd,
                              unsigned long request,
                              void* argp) = 0;

  /**
   * Get interface name for socket.
   * @param fd File descriptor
   * @return Interface name or nullopt
   */
  virtual optional<std::string> interfaceName(os_fd_t fd) = 0;

  /**
   * Check if socket option is supported.
   * @param level Option level
   * @param optname Option name
   * @return True if supported
   */
  virtual bool supportsSocketOption(int level, int optname) const = 0;

  /**
   * Get platform name.
   * @return Platform name (e.g., "linux", "macos", "windows")
   */
  virtual const std::string& platformName() const = 0;

  /**
   * Check if io_uring is available (Linux).
   * @return True if available
   */
  virtual bool supportsIoUring() const { return false; }

  /**
   * Check if UDP GRO is available.
   * @return True if available
   */
  virtual bool supportsUdpGro() const { return false; }

  /**
   * Check if UDP GSO is available.
   * @return True if available
   */
  virtual bool supportsUdpGso() const { return false; }

  /**
   * Check if IP_PKTINFO is available.
   * @return True if available
   */
  virtual bool supportsIpPktInfo() const { return false; }

  /**
   * Check if SO_REUSEPORT is available.
   * @return True if available
   */
  virtual bool supportsReusePort() const { return false; }
};

using SocketInterfacePtr = std::unique_ptr<SocketInterface>;

/**
 * Get the singleton socket interface.
 * @return Socket interface instance
 */
SocketInterface& socketInterface();

/**
 * Set a custom socket interface (for testing).
 * @param interface Custom interface
 */
void setSocketInterface(SocketInterfacePtr interface);

/**
 * Reset to default socket interface.
 */
void resetSocketInterface();

/**
 * Socket interface loader for platform-specific implementations.
 *
 * Different platforms can register their own loaders to provide
 * optimized implementations.
 */
class SocketInterfaceLoader {
 public:
  virtual ~SocketInterfaceLoader() = default;

  /**
   * Get the name of this loader.
   * @return Loader name
   */
  virtual const std::string& name() const = 0;

  /**
   * Create a socket interface instance.
   * @return Socket interface
   */
  virtual SocketInterfacePtr createSocketInterface() = 0;
};

using SocketInterfaceLoaderPtr = std::unique_ptr<SocketInterfaceLoader>;

/**
 * Register a socket interface loader.
 * @param loader Loader to register
 */
void registerSocketInterfaceLoader(SocketInterfaceLoaderPtr loader);

/**
 * Get names of all registered loaders.
 * @return Loader names
 */
std::vector<std::string> getSocketInterfaceLoaderNames();

/**
 * Create default socket interface for the platform.
 * @return Socket interface
 */
SocketInterfacePtr createDefaultSocketInterface();

/**
 * Create io_uring socket interface (Linux only).
 * @return Socket interface or nullptr if not supported
 */
#ifdef __linux__
SocketInterfacePtr createIoUringSocketInterface();
#endif

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_SOCKET_INTERFACE_H