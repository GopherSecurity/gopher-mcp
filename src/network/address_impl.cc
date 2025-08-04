#include "mcp/network/address_impl.h"

#include <cstring>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

namespace mcp {
namespace network {
namespace Address {

namespace {

// Helper to check if IPv4 address is in a specific range
bool isInRange(uint32_t addr, uint32_t base, uint32_t mask) {
  return (ntohl(addr) & mask) == base;
}

// Convert IPv6 address to string
std::string ipv6ToString(const in6_addr& addr) {
  char buffer[INET6_ADDRSTRLEN];
  if (::inet_ntop(AF_INET6, &addr, buffer, sizeof(buffer)) != nullptr) {
    return buffer;
  }
  return "";
}

// Check if IPv6 address needs brackets (has colons)
bool needsBrackets(const std::string& address) {
  return address.find(':') != std::string::npos;
}

}  // namespace

// ===== IPv4 Implementation =====

Ipv4Instance::Ipv4Instance(const sockaddr_in* address) {
  std::memset(&addr_, 0, sizeof(addr_));
  std::memcpy(&addr_, address, sizeof(sockaddr_in));
}

Ipv4Instance::Ipv4Instance(const std::string& address, uint16_t port) {
  std::memset(&addr_, 0, sizeof(addr_));
  addr_.sin_family = AF_INET;
  addr_.sin_port = htons(port);
  
  if (::inet_pton(AF_INET, address.c_str(), &addr_.sin_addr) != 1) {
    throw std::runtime_error("Invalid IPv4 address: " + address);
  }
}

std::string Ipv4Instance::asString() const {
  return addressAsString() + ":" + std::to_string(port());
}

std::string Ipv4Instance::addressAsString() const {
  char buffer[INET_ADDRSTRLEN];
  if (::inet_ntop(AF_INET, &addr_.sin_addr, buffer, sizeof(buffer)) != nullptr) {
    return buffer;
  }
  return "";
}

bool Ipv4Instance::operator==(const Instance& rhs) const {
  if (rhs.type() != Type::Ip) {
    return false;
  }
  
  const Ip* other_ip = rhs.ip();
  if (!other_ip || other_ip->version() != IpVersion::v4) {
    return false;
  }
  
  auto other_v4 = other_ip->ipv4();
  return other_v4.has_value() && 
         addr_.sin_addr.s_addr == *other_v4 &&
         addr_.sin_port == htons(other_ip->port());
}

bool Ipv4Instance::isAnyAddress() const {
  return addr_.sin_addr.s_addr == INADDR_ANY;
}

bool Ipv4Instance::isLoopbackAddress() const {
  // 127.0.0.0/8
  return isInRange(addr_.sin_addr.s_addr, 0x7F000000, 0xFF000000);
}

bool Ipv4Instance::isMulticastAddress() const {
  // 224.0.0.0/4
  return isInRange(addr_.sin_addr.s_addr, 0xE0000000, 0xF0000000);
}

// ===== IPv6 Implementation =====

Ipv6Instance::Ipv6Instance(const sockaddr_in6* address) {
  std::memset(&addr_, 0, sizeof(addr_));
  std::memcpy(&addr_, address, sizeof(sockaddr_in6));
}

Ipv6Instance::Ipv6Instance(const std::string& address, uint16_t port) {
  std::memset(&addr_, 0, sizeof(addr_));
  addr_.sin6_family = AF_INET6;
  addr_.sin6_port = htons(port);
  
  if (::inet_pton(AF_INET6, address.c_str(), &addr_.sin6_addr) != 1) {
    throw std::runtime_error("Invalid IPv6 address: " + address);
  }
}

std::string Ipv6Instance::asString() const {
  std::string addr_str = addressAsString();
  if (needsBrackets(addr_str)) {
    return "[" + addr_str + "]:" + std::to_string(port());
  }
  return addr_str + ":" + std::to_string(port());
}

std::string Ipv6Instance::addressAsString() const {
  return ipv6ToString(addr_.sin6_addr);
}

bool Ipv6Instance::operator==(const Instance& rhs) const {
  if (rhs.type() != Type::Ip) {
    return false;
  }
  
  const Ip* other_ip = rhs.ip();
  if (!other_ip || other_ip->version() != IpVersion::v6) {
    return false;
  }
  
  auto other_v6 = other_ip->ipv6();
  if (!other_v6.has_value()) {
    return false;
  }
  
  return std::memcmp(&addr_.sin6_addr, other_v6->data(), sizeof(in6_addr)) == 0 &&
         addr_.sin6_port == htons(other_ip->port());
}

bool Ipv6Instance::isAnyAddress() const {
  return IN6_IS_ADDR_UNSPECIFIED(&addr_.sin6_addr);
}

bool Ipv6Instance::isLoopbackAddress() const {
  return IN6_IS_ADDR_LOOPBACK(&addr_.sin6_addr);
}

bool Ipv6Instance::isMulticastAddress() const {
  return IN6_IS_ADDR_MULTICAST(&addr_.sin6_addr);
}

optional<std::array<uint8_t, 16>> Ipv6Instance::ipv6() const {
  std::array<uint8_t, 16> result;
  std::memcpy(result.data(), &addr_.sin6_addr, 16);
  return result;
}

// ===== Pipe (Unix Domain Socket) Implementation =====

#ifndef _WIN32
PipeInstance::PipeInstance(const sockaddr_un* address, socklen_t addr_len)
    : addr_len_(addr_len), mode_(0) {
  std::memset(&addr_, 0, sizeof(addr_));
  std::memcpy(&addr_, address, std::min(addr_len, static_cast<socklen_t>(sizeof(addr_))));
  
  // Extract path from sockaddr_un
  if (addr_.sun_family == AF_UNIX && addr_len > offsetof(sockaddr_un, sun_path)) {
    size_t path_len = addr_len - offsetof(sockaddr_un, sun_path);
    if (addr_.sun_path[0] == '\0') {
      // Abstract socket (Linux)
      path_ = std::string(addr_.sun_path, path_len);
    } else {
      // Regular path
      path_ = std::string(addr_.sun_path);
    }
  }
}

PipeInstance::PipeInstance(const std::string& path, mode_t mode)
    : path_(path), mode_(mode) {
  std::memset(&addr_, 0, sizeof(addr_));
  addr_.sun_family = AF_UNIX;
  
  if (path.length() >= sizeof(addr_.sun_path)) {
    throw std::runtime_error("Unix socket path too long: " + path);
  }
  
  if (!path.empty() && path[0] == '\0') {
    // Abstract socket
    std::memcpy(addr_.sun_path, path.data(), path.length());
    addr_len_ = offsetof(sockaddr_un, sun_path) + path.length();
  } else {
    // Regular path
    std::strncpy(addr_.sun_path, path.c_str(), sizeof(addr_.sun_path) - 1);
    addr_len_ = offsetof(sockaddr_un, sun_path) + path.length() + 1;
  }
}

std::string PipeInstance::asString() const {
  if (!path_.empty() && path_[0] == '\0') {
    // Abstract socket - use @ prefix convention
    return "@" + path_.substr(1);
  }
  return path_;
}

bool PipeInstance::operator==(const Instance& rhs) const {
  if (rhs.type() != Type::Pipe) {
    return false;
  }
  
  const auto* other = dynamic_cast<const PipeInstance*>(&rhs);
  return other != nullptr && path_ == other->path_;
}
#endif

// ===== Factory Functions =====

InstanceConstSharedPtr parseInternetAddress(const std::string& address, uint16_t default_port) {
  // Check if it has a port
  size_t last_colon = address.rfind(':');
  if (last_colon != std::string::npos) {
    // For IPv6, check if the colon is inside brackets
    size_t bracket_pos = address.find(']');
    if (bracket_pos != std::string::npos && last_colon > bracket_pos) {
      // [IPv6]:port format
      std::string addr_part = address.substr(1, bracket_pos - 1);
      std::string port_part = address.substr(last_colon + 1);
      try {
        uint16_t port = static_cast<uint16_t>(std::stoi(port_part));
        return std::make_shared<Ipv6Instance>(addr_part, port);
      } catch (...) {
        return nullptr;
      }
    } else if (address.find(':') != last_colon) {
      // Multiple colons, must be IPv6 without port
      try {
        return std::make_shared<Ipv6Instance>(address, default_port);
      } catch (...) {
        return nullptr;
      }
    } else {
      // IPv4:port format
      std::string addr_part = address.substr(0, last_colon);
      std::string port_part = address.substr(last_colon + 1);
      try {
        uint16_t port = static_cast<uint16_t>(std::stoi(port_part));
        return std::make_shared<Ipv4Instance>(addr_part, port);
      } catch (...) {
        return nullptr;
      }
    }
  }
  
  // No port specified, try to parse as is
  return parseInternetAddressNoPort(address, default_port);
}

InstanceConstSharedPtr parseInternetAddressNoPort(const std::string& address,
                                                  uint16_t port, bool v6only) {
  (void)v6only;  // Currently unused
  // Try IPv4 first
  try {
    return std::make_shared<Ipv4Instance>(address, port);
  } catch (...) {
    // Not IPv4
  }
  
  // Try IPv6
  try {
    return std::make_shared<Ipv6Instance>(address, port);
  } catch (...) {
    // Not IPv6
  }
  
  return nullptr;
}

InstanceConstSharedPtr addressFromSockAddr(const sockaddr_storage& addr,
                                          socklen_t len, bool v6only) {
  switch (addr.ss_family) {
    case AF_INET:
      if (len >= sizeof(sockaddr_in)) {
        return std::make_shared<Ipv4Instance>(
            reinterpret_cast<const sockaddr_in*>(&addr));
      }
      break;
      
    case AF_INET6:
      if (len >= sizeof(sockaddr_in6)) {
        const auto* addr6 = reinterpret_cast<const sockaddr_in6*>(&addr);
        
        // Check for IPv4-mapped IPv6 address
        if (!v6only && IN6_IS_ADDR_V4MAPPED(&addr6->sin6_addr)) {
          // Convert to IPv4
          sockaddr_in addr4;
          std::memset(&addr4, 0, sizeof(addr4));
          addr4.sin_family = AF_INET;
          addr4.sin_port = addr6->sin6_port;
          std::memcpy(&addr4.sin_addr, &addr6->sin6_addr.s6_addr[12], 4);
          return std::make_shared<Ipv4Instance>(&addr4);
        }
        
        return std::make_shared<Ipv6Instance>(addr6);
      }
      break;
      
#ifndef _WIN32
    case AF_UNIX:
      if (len >= offsetof(sockaddr_un, sun_path)) {
        return std::make_shared<PipeInstance>(
            reinterpret_cast<const sockaddr_un*>(&addr), len);
      }
      break;
#endif
  }
  
  return nullptr;
}

InstanceConstSharedPtr pipeAddress(const std::string& path, mode_t mode) {
#ifdef _WIN32
  // Windows named pipes use different API
  // TODO: Implement Windows named pipe support
  (void)path;
  (void)mode;
  return nullptr;
#else
  return std::make_shared<PipeInstance>(path, mode);
#endif
}

InstanceConstSharedPtr anyAddress(IpVersion version, uint16_t port) {
  if (version == IpVersion::v4) {
    return std::make_shared<Ipv4Instance>("0.0.0.0", port);
  } else {
    return std::make_shared<Ipv6Instance>("::", port);
  }
}

InstanceConstSharedPtr loopbackAddress(IpVersion version, uint16_t port) {
  if (version == IpVersion::v4) {
    return std::make_shared<Ipv4Instance>("127.0.0.1", port);
  } else {
    return std::make_shared<Ipv6Instance>("::1", port);
  }
}

// ===== CidrRange Implementation =====

optional<CidrRange> CidrRange::parse(const std::string& range) {
  size_t slash_pos = range.find('/');
  if (slash_pos == std::string::npos) {
    return nullopt;
  }
  
  std::string addr_part = range.substr(0, slash_pos);
  std::string prefix_part = range.substr(slash_pos + 1);
  
  try {
    uint32_t prefix_len = static_cast<uint32_t>(std::stoi(prefix_part));
    auto address = parseInternetAddressNoPort(addr_part);
    if (address) {
      return CidrRange(address, prefix_len);
    }
  } catch (...) {
    // Invalid prefix length
  }
  
  return nullopt;
}

CidrRange::CidrRange(const InstanceConstSharedPtr& address, uint32_t prefix_len)
    : address_(address), prefix_len_(prefix_len) {
  if (!address_ || address_->type() != Type::Ip) {
    throw std::invalid_argument("CidrRange requires IP address");
  }
  
  const Ip* ip = address_->ip();
  if (ip->version() == IpVersion::v4 && prefix_len > 32) {
    throw std::invalid_argument("IPv4 prefix length must be <= 32");
  } else if (ip->version() == IpVersion::v6 && prefix_len > 128) {
    throw std::invalid_argument("IPv6 prefix length must be <= 128");
  }
  
  // Calculate and cache network bits
  if (ip->version() == IpVersion::v4) {
    uint32_t addr = *ip->ipv4();
    uint32_t mask = prefix_len == 0 ? 0 : (~0u << (32 - prefix_len));
    uint32_t network = htonl(ntohl(addr) & mask);
    network_bits_.resize(4);
    std::memcpy(network_bits_.data(), &network, 4);
  } else {
    auto addr_bytes = *ip->ipv6();
    network_bits_ = std::vector<uint8_t>(addr_bytes.begin(), addr_bytes.end());
    
    // Apply prefix mask
    size_t full_bytes = prefix_len / 8;
    size_t remaining_bits = prefix_len % 8;
    
    if (full_bytes < 16) {
      if (remaining_bits > 0) {
        uint8_t mask = static_cast<uint8_t>(0xFF << (8 - remaining_bits));
        network_bits_[full_bytes] &= mask;
        full_bytes++;
      }
      // Zero out remaining bytes
      std::fill(network_bits_.begin() + full_bytes, network_bits_.end(), 0);
    }
  }
}

bool CidrRange::contains(const Instance& address) const {
  if (address.type() != Type::Ip) {
    return false;
  }
  
  const Ip* ip = address.ip();
  const Ip* range_ip = address_->ip();
  
  if (!ip || !range_ip || ip->version() != range_ip->version()) {
    return false;
  }
  
  if (ip->version() == IpVersion::v4) {
    uint32_t addr = *ip->ipv4();
    uint32_t network = *reinterpret_cast<const uint32_t*>(network_bits_.data());
    uint32_t mask = prefix_len_ == 0 ? 0 : (~0u << (32 - prefix_len_));
    return (ntohl(addr) & mask) == ntohl(network);
  } else {
    auto addr_bytes = *ip->ipv6();
    
    // Compare prefix bits
    size_t full_bytes = prefix_len_ / 8;
    size_t remaining_bits = prefix_len_ % 8;
    
    // Compare full bytes
    if (std::memcmp(addr_bytes.data(), network_bits_.data(), full_bytes) != 0) {
      return false;
    }
    
    // Compare remaining bits
    if (remaining_bits > 0 && full_bytes < 16) {
      uint8_t mask = static_cast<uint8_t>(0xFF << (8 - remaining_bits));
      if ((addr_bytes[full_bytes] & mask) != (network_bits_[full_bytes] & mask)) {
        return false;
      }
    }
    
    return true;
  }
}

std::string CidrRange::asString() const {
  return address_->ip()->addressAsString() + "/" + std::to_string(prefix_len_);
}

}  // namespace Address
}  // namespace network
}  // namespace mcp