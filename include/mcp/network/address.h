#ifndef MCP_NETWORK_ADDRESS_H
#define MCP_NETWORK_ADDRESS_H

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "mcp/core/compat.h"

// Platform-specific includes
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

namespace mcp {
namespace network {

// Forward declarations
namespace Address {
class Instance;
class Ip;
using InstanceConstSharedPtr = std::shared_ptr<const Instance>;
using IpConstSharedPtr = std::shared_ptr<const Ip>;
}  // namespace Address

namespace Address {

/**
 * Address types
 */
enum class Type {
  Ip,    // IPv4 or IPv6
  Pipe,  // Unix domain socket or Windows named pipe
};

/**
 * IP versions
 */
enum class IpVersion { v4, v6 };

/**
 * Base address interface
 */
class Instance {
 public:
  virtual ~Instance() = default;

  /**
   * Get address type.
   */
  virtual Type type() const = 0;

  /**
   * Get the IP interface if this is an IP address.
   * @return IP interface or nullptr
   */
  virtual const Ip* ip() const { return nullptr; }

  /**
   * Get the sockaddr structure.
   * @return Pointer to sockaddr
   */
  virtual const sockaddr* sockAddr() const = 0;

  /**
   * Get the sockaddr length.
   * @return Size of sockaddr structure
   */
  virtual socklen_t sockAddrLen() const = 0;

  /**
   * Convert to human-readable string.
   * @return String representation
   */
  virtual std::string asString() const = 0;

  /**
   * Convert to string with port (if applicable).
   * @return String with port
   */
  virtual std::string asStringView() const { return asString(); }

  /**
   * Get the logical name (for logging).
   * @return Logical name
   */
  virtual std::string logicalName() const { return asString(); }

  /**
   * Compare addresses.
   */
  virtual bool operator==(const Instance& rhs) const = 0;
  bool operator!=(const Instance& rhs) const { return !(*this == rhs); }
};

/**
 * IP address interface
 */
class Ip : public Instance {
 public:
  /**
   * Get port number.
   * @return Port number
   */
  virtual uint32_t port() const = 0;

  /**
   * Get IP version.
   * @return IPv4 or IPv6
   */
  virtual IpVersion version() const = 0;

  /**
   * Get address as string without port.
   * @return IP address string
   */
  virtual std::string addressAsString() const = 0;

  /**
   * Check if this is an any address (0.0.0.0 or ::).
   * @return True if any address
   */
  virtual bool isAnyAddress() const = 0;

  /**
   * Check if this is a loopback address.
   * @return True if loopback
   */
  virtual bool isLoopbackAddress() const = 0;

  /**
   * Check if this is a multicast address.
   * @return True if multicast
   */
  virtual bool isMulticastAddress() const = 0;

  /**
   * Get IPv4 address as uint32_t (network byte order).
   * @return Address or nullopt for IPv6
   */
  virtual optional<uint32_t> ipv4() const { return nullopt; }

  /**
   * Get IPv6 address as array.
   * @return Address or nullopt for IPv4
   */
  virtual optional<std::array<uint8_t, 16>> ipv6() const { return nullopt; }

  const Ip* ip() const override { return this; }
};

/**
 * Pipe address interface (Unix sockets / Windows named pipes)
 */
class Pipe : public Instance {
 public:
  /**
   * Get the pipe path.
   * @return Path
   */
  virtual const std::string& path() const = 0;

  /**
   * Get the mode (for Unix sockets).
   * @return Mode or 0
   */
  virtual mode_t mode() const { return 0; }
};

/**
 * Create an IP address from string.
 * @param address Address string (with optional port)
 * @param port Default port if not in string
 * @return Address instance or nullptr on error
 */
InstanceConstSharedPtr parseInternetAddress(const std::string& address,
                                            uint16_t port = 0);

/**
 * Create an IP address from string without port.
 * @param address IP address string
 * @param port Port number
 * @param v6only Force IPv6 for IPv4-mapped addresses
 * @return Address instance or nullptr on error
 */
InstanceConstSharedPtr parseInternetAddressNoPort(const std::string& address,
                                                  uint16_t port = 0,
                                                  bool v6only = true);

/**
 * Create an address from sockaddr.
 * @param addr Socket address
 * @param len Address length
 * @param v6only Force IPv6 for IPv4-mapped addresses
 * @return Address instance
 */
InstanceConstSharedPtr addressFromSockAddr(const sockaddr_storage& addr,
                                           socklen_t len,
                                           bool v6only = true);

/**
 * Create a pipe address.
 * @param path Pipe path
 * @param mode Unix socket mode (ignored on Windows)
 * @return Address instance
 */
InstanceConstSharedPtr pipeAddress(const std::string& path, mode_t mode = 0);

/**
 * Get any address for IP version.
 * @param version IP version
 * @param port Port number
 * @return Any address (0.0.0.0 or ::)
 */
InstanceConstSharedPtr anyAddress(IpVersion version, uint16_t port = 0);

/**
 * Get loopback address for IP version.
 * @param version IP version
 * @param port Port number
 * @return Loopback address (127.0.0.1 or ::1)
 */
InstanceConstSharedPtr loopbackAddress(IpVersion version, uint16_t port = 0);

/**
 * CIDR range for IP filtering
 */
class CidrRange {
 public:
  /**
   * Create from string (e.g., "192.168.1.0/24").
   * @param range CIDR string
   * @return CidrRange or error
   */
  static optional<CidrRange> parse(const std::string& range);

  /**
   * Create from address and prefix length.
   * @param address Base address
   * @param prefix_len Prefix length
   */
  CidrRange(const InstanceConstSharedPtr& address, uint32_t prefix_len);

  /**
   * Check if address is in range.
   * @param address Address to check
   * @return True if in range
   */
  bool contains(const Instance& address) const;

  /**
   * Get the base address.
   */
  const InstanceConstSharedPtr& address() const { return address_; }

  /**
   * Get the prefix length.
   */
  uint32_t prefixLength() const { return prefix_len_; }

  /**
   * Convert to string.
   */
  std::string asString() const;

 private:
  InstanceConstSharedPtr address_;
  uint32_t prefix_len_;
  // Cached network bits for fast comparison
  std::vector<uint8_t> network_bits_;
};

}  // namespace Address

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_ADDRESS_H