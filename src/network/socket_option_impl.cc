#include "mcp/network/socket_option_impl.h"

#include <cstring>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#include "mcp/network/socket_interface.h"

namespace mcp {
namespace network {

// ===== Base SocketOption Implementation =====

SocketOptionImpl::SocketOptionImpl(const SocketOptionName& optname,
                                   const void* value,
                                   size_t size)
    : optname_(optname) {
  value_.resize(size);
  std::memcpy(value_.data(), value, size);
}

bool SocketOptionImpl::setOption(Socket& socket) const {
  if (!isSupported()) {
    return false;
  }
  
  auto result = socket.setSocketOption(optname_.level, optname_.option,
                                       value_.data(), value_.size());
  return result.ok();
}

bool SocketOptionImpl::setOptionForListen(Socket& socket) const {
  // Most options are set before listen, not after
  (void)socket;
  return true;
}

void SocketOptionImpl::hashKey(std::vector<uint8_t>& key) const {
  // Add option name
  key.push_back(static_cast<uint8_t>(optname_.level));
  key.push_back(static_cast<uint8_t>(optname_.option));
  
  // Add option value
  key.insert(key.end(), value_.begin(), value_.end());
}

std::string SocketOptionImpl::toString() const {
  std::stringstream ss;
  ss << optname_.name << "=";
  
  // Format value based on size
  if (value_.size() == sizeof(int)) {
    int val;
    std::memcpy(&val, value_.data(), sizeof(int));
    ss << val;
  } else {
    ss << "<" << value_.size() << " bytes>";
  }
  
  return ss.str();
}

bool SocketOptionImpl::isSupported() const {
  return optname_.hasValue() && 
         socketInterface().supportsSocketOption(optname_.level, optname_.option);
}

// ===== IP Transparent Socket Option =====

IpTransparentSocketOption::IpTransparentSocketOption(bool enabled)
    : BoolSocketOption(SOCKET_IP_TRANSPARENT, enabled) {}

bool IpTransparentSocketOption::setOption(Socket& socket) const {
  if (!isSupported()) {
    return false;
  }
  
  // Set both IPv4 and IPv6 transparent options if applicable
  bool success = true;
  
  // IPv4 transparent
  if (SOCKET_IP_TRANSPARENT.hasValue()) {
    success &= BoolSocketOption::setOption(socket);
  }
  
  // IPv6 transparent
  if (SOCKET_IPV6_TRANSPARENT.hasValue() && 
      socket.addressType() == Address::Type::Ip &&
      socket.ipVersion() == Address::IpVersion::v6) {
    int value = value_[0] ? 1 : 0;
    auto result = socket.setSocketOption(SOCKET_IPV6_TRANSPARENT.level,
                                         SOCKET_IPV6_TRANSPARENT.option,
                                         &value, sizeof(value));
    success &= result.ok();
  }
  
  return success;
}

// ===== IP Freebind Socket Option =====

IpFreebindSocketOption::IpFreebindSocketOption(bool enabled)
    : BoolSocketOption(SOCKET_IP_FREEBIND, enabled) {}

// ===== Reuse Port Socket Option =====

ReusePortSocketOption::ReusePortSocketOption(bool enabled)
    : BoolSocketOption(SOCKET_SO_REUSEPORT, enabled) {}

void ReusePortSocketOption::setBpfProgram(const void* prog, size_t prog_len) {
  bpf_program_.resize(prog_len);
  std::memcpy(bpf_program_.data(), prog, prog_len);
}

bool ReusePortSocketOption::setOptionForListen(Socket& socket) const {
  // Apply BPF program after listen on Linux
#ifdef SO_ATTACH_REUSEPORT_CBPF
  if (!bpf_program_.empty() && MCP_SO_ATTACH_REUSEPORT_CBPF.hasValue()) {
    auto result = socket.setSocketOption(MCP_SO_ATTACH_REUSEPORT_CBPF.level,
                                         MCP_SO_ATTACH_REUSEPORT_CBPF.option,
                                         bpf_program_.data(),
                                         bpf_program_.size());
    return result.ok();
  }
#else
  (void)socket;
#endif
  return true;
}

// ===== TCP Keepalive Socket Option =====

bool TcpKeepaliveSocketOption::setOption(Socket& socket) const {
  bool success = true;
  
  // Enable/disable keepalive
  if (settings_.enabled.has_value() && SOCKET_SO_KEEPALIVE.hasValue()) {
    int value = *settings_.enabled ? 1 : 0;
    auto result = socket.setSocketOption(SOCKET_SO_KEEPALIVE.level,
                                         SOCKET_SO_KEEPALIVE.option,
                                         &value, sizeof(value));
    success &= result.ok();
  }
  
  // Set keepalive parameters (only if enabled)
  if (!settings_.enabled.has_value() || *settings_.enabled) {
#ifdef TCP_KEEPIDLE
    if (settings_.idle_time_s.has_value() && SOCKET_TCP_KEEPIDLE.hasValue()) {
      int value = *settings_.idle_time_s;
      auto result = socket.setSocketOption(SOCKET_TCP_KEEPIDLE.level,
                                           SOCKET_TCP_KEEPIDLE.option,
                                           &value, sizeof(value));
      success &= result.ok();
    }
#endif
    
#ifdef TCP_KEEPINTVL
    if (settings_.interval_s.has_value() && SOCKET_TCP_KEEPINTVL.hasValue()) {
      int value = *settings_.interval_s;
      auto result = socket.setSocketOption(SOCKET_TCP_KEEPINTVL.level,
                                           SOCKET_TCP_KEEPINTVL.option,
                                           &value, sizeof(value));
      success &= result.ok();
    }
#endif
    
#ifdef TCP_KEEPCNT
    if (settings_.probes.has_value() && SOCKET_TCP_KEEPCNT.hasValue()) {
      int value = *settings_.probes;
      auto result = socket.setSocketOption(SOCKET_TCP_KEEPCNT.level,
                                           SOCKET_TCP_KEEPCNT.option,
                                           &value, sizeof(value));
      success &= result.ok();
    }
#endif
  }
  
  return success;
}

void TcpKeepaliveSocketOption::hashKey(std::vector<uint8_t>& key) const {
  key.push_back('K');  // Keepalive marker
  
  if (settings_.enabled.has_value()) {
    key.push_back(*settings_.enabled ? 1 : 0);
  }
  
  auto addInt = [&key](const optional<int>& val) {
    if (val.has_value()) {
      int v = *val;
      key.push_back(static_cast<uint8_t>(v >> 24));
      key.push_back(static_cast<uint8_t>(v >> 16));
      key.push_back(static_cast<uint8_t>(v >> 8));
      key.push_back(static_cast<uint8_t>(v));
    } else {
      key.push_back(0xFF);  // Marker for not set
    }
  };
  
  addInt(settings_.idle_time_s);
  addInt(settings_.interval_s);
  addInt(settings_.probes);
}

std::string TcpKeepaliveSocketOption::toString() const {
  std::stringstream ss;
  ss << "TCP_KEEPALIVE{";
  
  if (settings_.enabled.has_value()) {
    ss << "enabled=" << (*settings_.enabled ? "true" : "false");
  }
  
  if (settings_.idle_time_s.has_value()) {
    ss << ",idle=" << *settings_.idle_time_s << "s";
  }
  
  if (settings_.interval_s.has_value()) {
    ss << ",interval=" << *settings_.interval_s << "s";
  }
  
  if (settings_.probes.has_value()) {
    ss << ",probes=" << *settings_.probes;
  }
  
  ss << "}";
  return ss.str();
}

bool TcpKeepaliveSocketOption::isSupported() const {
  // At least SO_KEEPALIVE should be supported
  return SOCKET_SO_KEEPALIVE.hasValue();
}

// ===== Buffer Size Socket Option =====

BufferSizeSocketOption::BufferSizeSocketOption(BufferType type, int size)
    : IntSocketOption(type == Receive ? SOCKET_SO_RCVBUF : SOCKET_SO_SNDBUF, size) {}

// ===== Mark Socket Option =====

MarkSocketOption::MarkSocketOption(uint32_t mark)
    : IntSocketOption(SOCKET_SO_MARK, static_cast<int>(mark)) {}

// ===== Type of Service Socket Option =====

bool TosSocketOption::setOption(Socket& socket) const {
  bool success = true;
  
  // Set IPv4 TOS
  if (socket.addressType() == Address::Type::Ip) {
    if (socket.ipVersion() == Address::IpVersion::v4 || !socket.ipVersion().has_value()) {
#ifdef IP_TOS
      int value = tos_;
      auto result = socket.setSocketOption(IPPROTO_IP, IP_TOS, &value, sizeof(value));
      success &= result.ok();
#endif
    }
    
    // Set IPv6 Traffic Class
    if (socket.ipVersion() == Address::IpVersion::v6 || !socket.ipVersion().has_value()) {
#ifdef IPV6_TCLASS
      int value = tos_;
      auto result = socket.setSocketOption(IPPROTO_IPV6, IPV6_TCLASS, &value, sizeof(value));
      success &= result.ok();
#endif
    }
  }
  
  return success;
}

void TosSocketOption::hashKey(std::vector<uint8_t>& key) const {
  key.push_back('T');  // TOS marker
  key.push_back(tos_);
}

std::string TosSocketOption::toString() const {
  std::stringstream ss;
  ss << "TOS=" << static_cast<int>(tos_);
  return ss.str();
}

bool TosSocketOption::isSupported() const {
#if defined(IP_TOS) || defined(IPV6_TCLASS)
  return true;
#else
  return false;
#endif
}

// ===== Socket Option Factory =====

SocketOptionsSharedPtr buildSocketOptions(const SocketCreationOptions& options) {
  auto socket_options = std::make_shared<std::vector<SocketOptionConstSharedPtr>>();
  
  // SO_REUSEADDR
  if (options.reuse_address) {
    socket_options->push_back(
        std::make_shared<BoolSocketOption>(SOCKET_SO_REUSEADDR, true));
  }
  
  // SO_REUSEPORT
  if (options.reuse_port) {
    socket_options->push_back(
        std::make_shared<ReusePortSocketOption>(true));
  }
  
  // IPV6_V6ONLY
  if (options.v6_only) {
    socket_options->push_back(
        std::make_shared<BoolSocketOption>(SOCKET_IPV6_V6ONLY, true));
  }
  
  // Buffer sizes
  if (options.send_buffer_size.has_value()) {
    socket_options->push_back(
        std::make_shared<BufferSizeSocketOption>(
            BufferSizeSocketOption::Send, *options.send_buffer_size));
  }
  
  if (options.receive_buffer_size.has_value()) {
    socket_options->push_back(
        std::make_shared<BufferSizeSocketOption>(
            BufferSizeSocketOption::Receive, *options.receive_buffer_size));
  }
  
  // Type of Service
  if (options.type_of_service.has_value()) {
    socket_options->push_back(
        std::make_shared<TosSocketOption>(
            static_cast<uint8_t>(*options.type_of_service)));
  }
  
  // TCP options
  if (options.tcp_nodelay.has_value()) {
    socket_options->push_back(
        std::make_shared<BoolSocketOption>(SOCKET_TCP_NODELAY, *options.tcp_nodelay));
  }
  
  // TCP keepalive
  if (options.tcp_keepalive.has_value() ||
      options.tcp_keepidle.has_value() ||
      options.tcp_keepintvl.has_value() ||
      options.tcp_keepcnt.has_value()) {
    TcpKeepaliveSocketOption::KeepaliveSettings settings;
    settings.enabled = options.tcp_keepalive;
    settings.idle_time_s = options.tcp_keepidle;
    settings.interval_s = options.tcp_keepintvl;
    settings.probes = options.tcp_keepcnt;
    
    socket_options->push_back(
        std::make_shared<TcpKeepaliveSocketOption>(settings));
  }
  
  // IP transparent
  if (options.ip_transparent.has_value()) {
    socket_options->push_back(
        std::make_shared<IpTransparentSocketOption>(*options.ip_transparent));
  }
  
  // IP freebind
  if (options.ip_freebind.has_value()) {
    socket_options->push_back(
        std::make_shared<IpFreebindSocketOption>(*options.ip_freebind));
  }
  
  return socket_options;
}

// ===== Socket Option Names (defined here to ensure single definition) =====

// Fix the macro reference
#ifdef SO_ATTACH_REUSEPORT_CBPF
#define MCP_ATTACH_REUSEPORT_CBPF \
  MCP_MAKE_SOCKET_OPTION_NAME(SOL_SOCKET, SO_ATTACH_REUSEPORT_CBPF)
#else
#define MCP_ATTACH_REUSEPORT_CBPF SocketOptionName()
#endif

// Define the constant used in the code
const SocketOptionName MCP_SO_ATTACH_REUSEPORT_CBPF = MCP_ATTACH_REUSEPORT_CBPF;

// IPv6 transparent option
#ifdef IPV6_TRANSPARENT
const SocketOptionName SOCKET_IPV6_TRANSPARENT = 
    MCP_MAKE_SOCKET_OPTION_NAME(IPPROTO_IPV6, IPV6_TRANSPARENT);
#else
const SocketOptionName SOCKET_IPV6_TRANSPARENT;
#endif

// SO_RCVBUF and SO_SNDBUF
#ifdef SO_RCVBUF
const SocketOptionName SOCKET_SO_RCVBUF = 
    MCP_MAKE_SOCKET_OPTION_NAME(SOL_SOCKET, SO_RCVBUF);
#else
const SocketOptionName SOCKET_SO_RCVBUF;
#endif

#ifdef SO_SNDBUF
const SocketOptionName SOCKET_SO_SNDBUF = 
    MCP_MAKE_SOCKET_OPTION_NAME(SOL_SOCKET, SO_SNDBUF);
#else
const SocketOptionName SOCKET_SO_SNDBUF;
#endif

}  // namespace network
}  // namespace mcp