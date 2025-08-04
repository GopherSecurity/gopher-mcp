#ifndef MCP_NETWORK_ADDRESS_IMPL_H
#define MCP_NETWORK_ADDRESS_IMPL_H

#include "mcp/network/address.h"
#include <array>

namespace mcp {
namespace network {
namespace Address {

/**
 * IPv4 address implementation
 */
class Ipv4Instance : public Ip {
public:
  explicit Ipv4Instance(const sockaddr_in* address);
  Ipv4Instance(const std::string& address, uint16_t port = 0);
  
  // Instance interface
  Type type() const override { return Type::Ip; }
  const sockaddr* sockAddr() const override {
    return reinterpret_cast<const sockaddr*>(&addr_);
  }
  socklen_t sockAddrLen() const override { return sizeof(addr_); }
  std::string asString() const override;
  std::string asStringView() const override { return asString(); }
  bool operator==(const Instance& rhs) const override;
  
  // Ip interface
  uint32_t port() const override { return ntohs(addr_.sin_port); }
  IpVersion version() const override { return IpVersion::v4; }
  std::string addressAsString() const override;
  bool isAnyAddress() const override;
  bool isLoopbackAddress() const override;
  bool isMulticastAddress() const override;
  optional<uint32_t> ipv4() const override { return addr_.sin_addr.s_addr; }
  
private:
  sockaddr_in addr_;
};

/**
 * IPv6 address implementation
 */
class Ipv6Instance : public Ip {
public:
  explicit Ipv6Instance(const sockaddr_in6* address);
  Ipv6Instance(const std::string& address, uint16_t port = 0);
  
  // Instance interface
  Type type() const override { return Type::Ip; }
  const sockaddr* sockAddr() const override {
    return reinterpret_cast<const sockaddr*>(&addr_);
  }
  socklen_t sockAddrLen() const override { return sizeof(addr_); }
  std::string asString() const override;
  std::string asStringView() const override { return asString(); }
  bool operator==(const Instance& rhs) const override;
  
  // Ip interface
  uint32_t port() const override { return ntohs(addr_.sin6_port); }
  IpVersion version() const override { return IpVersion::v6; }
  std::string addressAsString() const override;
  bool isAnyAddress() const override;
  bool isLoopbackAddress() const override;
  bool isMulticastAddress() const override;
  optional<std::array<uint8_t, 16>> ipv6() const override;
  
private:
  sockaddr_in6 addr_;
};

/**
 * Unix domain socket address implementation
 */
class PipeInstance : public Pipe {
public:
  explicit PipeInstance(const sockaddr_un* address, socklen_t addr_len);
  PipeInstance(const std::string& path, mode_t mode = 0);
  
  // Instance interface
  Type type() const override { return Type::Pipe; }
  const sockaddr* sockAddr() const override {
    return reinterpret_cast<const sockaddr*>(&addr_);
  }
  socklen_t sockAddrLen() const override { return addr_len_; }
  std::string asString() const override;
  std::string asStringView() const override { return asString(); }
  bool operator==(const Instance& rhs) const override;
  
  // Pipe interface
  const std::string& path() const override { return path_; }
  mode_t mode() const override { return mode_; }
  
private:
  sockaddr_un addr_;
  socklen_t addr_len_;
  std::string path_;
  mode_t mode_;
};

}  // namespace Address
}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_ADDRESS_IMPL_H