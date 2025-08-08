#ifndef MCP_NETWORK_SOCKET_H
#define MCP_NETWORK_SOCKET_H

#include <memory>
#include <string>
#include <vector>

#include "mcp/core/compat.h"
#include "mcp/network/address.h"
#include "mcp/network/io_handle.h"

namespace mcp {
namespace network {

// Forward declarations
class Socket;
class ConnectionInfo;
class SocketOption;
using SocketPtr = std::unique_ptr<Socket>;
using SocketSharedPtr = std::shared_ptr<Socket>;
using SocketOptionConstSharedPtr = std::shared_ptr<const SocketOption>;
using SocketOptionsSharedPtr =
    std::shared_ptr<std::vector<SocketOptionConstSharedPtr>>;

/**
 * Socket types
 */
enum class SocketType {
  Stream,    // TCP socket
  Datagram,  // UDP socket
  Raw        // Raw socket
};

/**
 * Socket creation options
 */
struct SocketCreationOptions {
  bool non_blocking{true};            // Create non-blocking socket
  bool reuse_address{true};           // SO_REUSEADDR
  bool reuse_port{false};             // SO_REUSEPORT (if available)
  bool v6_only{false};                // IPV6_V6ONLY for IPv6 sockets
  bool close_on_exec{true};           // FD_CLOEXEC
  optional<int> send_buffer_size;     // SO_SNDBUF
  optional<int> receive_buffer_size;  // SO_RCVBUF
  optional<int> type_of_service;      // IP_TOS
  optional<bool> tcp_nodelay;         // TCP_NODELAY
  optional<bool> tcp_keepalive;       // SO_KEEPALIVE
  optional<int> tcp_keepidle;         // TCP_KEEPIDLE
  optional<int> tcp_keepintvl;        // TCP_KEEPINTVL
  optional<int> tcp_keepcnt;          // TCP_KEEPCNT
  optional<bool> ip_transparent;      // IP_TRANSPARENT (Linux)
  optional<bool> ip_freebind;         // IP_FREEBIND (Linux)
};

/**
 * Connection information provider interface
 */
class ConnectionInfoProvider {
 public:
  virtual ~ConnectionInfoProvider() = default;

  virtual const Address::InstanceConstSharedPtr& localAddress() const = 0;
  virtual const Address::InstanceConstSharedPtr& directLocalAddress() const = 0;
  virtual const Address::InstanceConstSharedPtr& remoteAddress() const = 0;
  virtual const Address::InstanceConstSharedPtr& directRemoteAddress()
      const = 0;

  virtual std::string requestedServerName() const = 0;
  virtual optional<uint64_t> connectionID() const = 0;
  virtual optional<std::string> interfaceName() const = 0;

  // SSL/TLS information (if applicable)
  virtual std::string sslProtocol() const { return ""; }
  virtual std::string sslCipherSuite() const { return ""; }
  virtual std::string sslPeerCertificate() const { return ""; }
  virtual std::string ja3Hash() const { return ""; }
  virtual std::string ja4Hash() const { return ""; }

  virtual optional<std::chrono::milliseconds> roundTripTime() const = 0;
};

/**
 * Connection information setter interface
 */
class ConnectionInfoSetter : public ConnectionInfoProvider {
 public:
  virtual void setLocalAddress(
      const Address::InstanceConstSharedPtr& address) = 0;
  virtual void restoreLocalAddress(
      const Address::InstanceConstSharedPtr& address) = 0;
  virtual bool localAddressRestored() const = 0;

  virtual void setRemoteAddress(
      const Address::InstanceConstSharedPtr& address) = 0;
  virtual void setRequestedServerName(const std::string& server_name) = 0;
  virtual void setConnectionID(uint64_t id) = 0;
  virtual void setInterfaceName(const std::string& interface_name) = 0;

  virtual void setSslProtocol(const std::string& protocol) = 0;
  virtual void setSslCipherSuite(const std::string& cipher) = 0;
  virtual void setSslPeerCertificate(const std::string& cert) = 0;
  virtual void setJA3Hash(const std::string& hash) = 0;
  virtual void setJA4Hash(const std::string& hash) = 0;

  virtual void setRoundTripTime(std::chrono::milliseconds rtt) = 0;
};

using ConnectionInfoProviderSharedPtr = std::shared_ptr<ConnectionInfoProvider>;
using ConnectionInfoSetterSharedPtr = std::shared_ptr<ConnectionInfoSetter>;

/**
 * Socket option interface
 */
class SocketOption {
 public:
  virtual ~SocketOption() = default;

  /**
   * Apply option before bind().
   */
  virtual bool setOption(Socket& socket) const {
    (void)socket;
    return true;
  }

  /**
   * Apply option after listen() for server sockets.
   */
  virtual bool setOptionForListen(Socket& socket) const {
    (void)socket;
    return true;
  }

  /**
   * Get option details for logging.
   */
  virtual void hashKey(std::vector<uint8_t>& key) const = 0;

  /**
   * Get human-readable description.
   */
  virtual std::string toString() const = 0;

  /**
   * Check if this option is supported on the current platform.
   */
  virtual bool isSupported() const = 0;
};

/**
 * Socket option names for common options
 */
struct SocketOptionName {
  int level;
  int option;
  std::string name;

  SocketOptionName() : level(0), option(0) {}
  SocketOptionName(int l, int o, const std::string& n)
      : level(l), option(o), name(n) {}

  bool hasValue() const { return !name.empty(); }
  bool operator==(const SocketOptionName& rhs) const {
    return level == rhs.level && option == rhs.option;
  }
};

// Common socket option names (platform-specific implementations)
extern const SocketOptionName SOCKET_SO_REUSEADDR;
extern const SocketOptionName SOCKET_SO_REUSEPORT;
extern const SocketOptionName SOCKET_SO_KEEPALIVE;
extern const SocketOptionName SOCKET_TCP_NODELAY;
extern const SocketOptionName SOCKET_IP_TRANSPARENT;
extern const SocketOptionName SOCKET_IPV6_V6ONLY;

/**
 * Main socket interface
 *
 * This abstracts socket lifecycle and configuration, separating it from
 * the I/O operations (IoHandle) and connection management (Connection).
 */
class Socket {
 public:
  virtual ~Socket() = default;

  /**
   * Get the connection information provider.
   */
  virtual ConnectionInfoSetter& connectionInfoProvider() = 0;
  virtual const ConnectionInfoProvider& connectionInfoProvider() const = 0;
  virtual ConnectionInfoProviderSharedPtr connectionInfoProviderSharedPtr()
      const = 0;

  /**
   * Get the underlying I/O handle.
   */
  virtual IoHandle& ioHandle() = 0;
  virtual const IoHandle& ioHandle() const = 0;

  /**
   * Close the socket.
   */
  virtual void close() = 0;

  /**
   * Check if socket is open.
   */
  virtual bool isOpen() const = 0;

  /**
   * Bind to an address.
   */
  virtual IoResult<int> bind(
      const Address::InstanceConstSharedPtr& address) = 0;

  /**
   * Listen for connections.
   */
  virtual IoResult<int> listen(int backlog) = 0;

  /**
   * Connect to an address.
   */
  virtual IoResult<int> connect(
      const Address::InstanceConstSharedPtr& address) = 0;

  /**
   * Set a socket option.
   */
  virtual IoResult<int> setSocketOption(int level,
                                        int optname,
                                        const void* optval,
                                        socklen_t optlen) = 0;

  /**
   * Get a socket option.
   */
  virtual IoResult<int> getSocketOption(int level,
                                        int optname,
                                        void* optval,
                                        socklen_t* optlen) const = 0;

  /**
   * Platform-specific ioctl.
   */
  virtual IoResult<int> ioctl(unsigned long request, void* argp) = 0;

  /**
   * Add a socket option.
   */
  virtual void addOption(const SocketOptionConstSharedPtr& option) = 0;

  /**
   * Add multiple socket options.
   */
  virtual void addOptions(const SocketOptionsSharedPtr& options) = 0;

  /**
   * Get all socket options.
   */
  virtual const SocketOptionsSharedPtr& options() const = 0;

  /**
   * Get socket type.
   */
  virtual SocketType socketType() const = 0;

  /**
   * Get address type.
   */
  virtual Address::Type addressType() const = 0;

  /**
   * Get IP version (if applicable).
   */
  virtual optional<Address::IpVersion> ipVersion() const = 0;

  /**
   * Duplicate the socket.
   */
  virtual SocketPtr duplicate() = 0;

  /**
   * Set blocking mode (for testing).
   */
  virtual IoResult<int> setBlocking(bool blocking) = 0;
};

/**
 * Socket specifically for network connections (TCP/UDP)
 */
class ConnectionSocket : public Socket {
 public:
  /**
   * Get the requested server name (for SNI).
   */
  virtual std::string requestedServerName() const = 0;

  /**
   * Enable/disable half-close.
   */
  virtual void setHalfClose(bool enabled) = 0;

  /**
   * Check if half-close is enabled.
   */
  virtual bool isHalfClose() const = 0;

  /**
   * Get detected close type (for debugging).
   */
  enum class DetectedCloseType { Normal, LocalClose, RemoteClose };
  virtual DetectedCloseType detectedCloseType() const = 0;
};

using ConnectionSocketPtr = std::unique_ptr<ConnectionSocket>;

/**
 * Socket for listening (server sockets)
 */
class ListenSocket : public Socket {
 public:
  /**
   * Set listener-specific options.
   */
  virtual void setListenSocketOptions(const SocketCreationOptions& options) = 0;

  /**
   * Get the accept filter (if configured).
   */
  virtual std::string acceptFilter() const { return ""; }
};

using ListenSocketPtr = std::unique_ptr<ListenSocket>;
using ListenSocketSharedPtr = std::shared_ptr<ListenSocket>;

/**
 * Factory functions for creating sockets
 */

/**
 * Create a client socket.
 * @param address_type Address type (IPv4, IPv6, Unix, Pipe)
 * @param remote_address Remote address to connect to
 * @param local_address Local address to bind to (optional)
 * @param options Socket creation options
 * @return ConnectionSocket instance
 */
ConnectionSocketPtr createConnectionSocket(
    Address::Type address_type,
    const Address::InstanceConstSharedPtr& remote_address,
    const Address::InstanceConstSharedPtr& local_address = nullptr,
    const SocketCreationOptions& options = {});

/**
 * Create a server socket.
 * @param address Address to listen on
 * @param options Socket creation options
 * @param bind_to_port Whether to bind immediately
 * @return ListenSocket instance
 */
ListenSocketPtr createListenSocket(
    const Address::InstanceConstSharedPtr& address,
    const SocketCreationOptions& options = {},
    bool bind_to_port = true);

/**
 * Create socket options from creation options.
 * @param options Creation options
 * @return Vector of socket options
 */
SocketOptionsSharedPtr createSocketOptions(
    const SocketCreationOptions& options);

/**
 * Utility functions
 */

/**
 * Apply socket options to a socket.
 * @param socket Socket to apply options to
 * @param options Options to apply
 * @param phase When to apply (pre_bind, post_listen)
 * @return true if all options applied successfully
 */
bool applySocketOptions(Socket& socket,
                        const SocketOptionsSharedPtr& options,
                        SocketOptionName phase);

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_SOCKET_H