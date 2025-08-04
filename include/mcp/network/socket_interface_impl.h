#ifndef MCP_NETWORK_SOCKET_INTERFACE_IMPL_H
#define MCP_NETWORK_SOCKET_INTERFACE_IMPL_H

#include "mcp/network/socket_interface.h"

namespace mcp {
namespace network {

/**
 * Default socket interface implementation.
 * 
 * This provides the standard implementation using platform-specific
 * socket APIs (BSD sockets on Unix, Winsock on Windows).
 */
class SocketInterfaceImpl : public SocketInterface {
public:
  SocketInterfaceImpl();
  ~SocketInterfaceImpl() override = default;
  
  // SocketInterface implementation
  IoResult<os_fd_t> socket(SocketType type,
                           Address::Type addr_type,
                           optional<Address::IpVersion> version = nullopt,
                           bool socket_v6only = false) override;
  
  IoResult<int> socketPair(SocketType type, os_fd_t fds[2]) override;
  
  IoHandlePtr ioHandleForFd(os_fd_t fd,
                            bool socket_v6only = false,
                            optional<int> domain = nullopt) override;
  
  IoResult<int> close(os_fd_t fd) override;
  
  IoResult<os_fd_t> duplicate(os_fd_t fd) override;
  
  IoResult<int> setFileFlags(os_fd_t fd, int flags) override;
  
  IoResult<int> getFileFlags(os_fd_t fd) override;
  
  IoResult<int> setsockopt(os_fd_t fd, int level, int optname,
                           const void* optval, socklen_t optlen) override;
  
  IoResult<int> getsockopt(os_fd_t fd, int level, int optname,
                           void* optval, socklen_t* optlen) override;
  
  IoResult<int> bind(os_fd_t fd, const Address::Instance& addr) override;
  
  IoResult<int> listen(os_fd_t fd, int backlog) override;
  
  IoResult<os_fd_t> accept(os_fd_t fd,
                           sockaddr* addr = nullptr,
                           socklen_t* addrlen = nullptr) override;
  
  IoResult<int> connect(os_fd_t fd, const Address::Instance& addr) override;
  
  IoResult<int> shutdown(os_fd_t fd, int how) override;
  
  IoResult<Address::InstanceConstSharedPtr> localAddress(os_fd_t fd) override;
  
  IoResult<Address::InstanceConstSharedPtr> peerAddress(os_fd_t fd) override;
  
  IoResult<int> ioctl(os_fd_t fd, unsigned long request, void* argp) override;
  
  optional<std::string> interfaceName(os_fd_t fd) override;
  
  bool supportsSocketOption(int level, int optname) const override;
  
  const std::string& platformName() const override;
  
  bool supportsIoUring() const override;
  
  bool supportsUdpGro() const override;
  
  bool supportsUdpGso() const override;
  
  bool supportsIpPktInfo() const override;
  
  bool supportsReusePort() const override;
};

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_SOCKET_INTERFACE_IMPL_H