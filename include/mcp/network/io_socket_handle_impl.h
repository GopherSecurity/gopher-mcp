#ifndef MCP_NETWORK_IO_SOCKET_HANDLE_IMPL_H
#define MCP_NETWORK_IO_SOCKET_HANDLE_IMPL_H

#include "mcp/event/event_loop.h"
#include "mcp/network/io_handle.h"

namespace mcp {
namespace network {

/**
 * Standard implementation of IoHandle for sockets.
 *
 * This provides the default socket I/O implementation using
 * platform-specific APIs (BSD sockets on Unix, Winsock on Windows).
 */
class IoSocketHandleImpl : public IoHandle {
 public:
  explicit IoSocketHandleImpl(os_fd_t fd = INVALID_SOCKET_FD,
                              bool socket_v6only = false,
                              optional<int> domain = nullopt);
  ~IoSocketHandleImpl() override;

  // Disable copy
  IoSocketHandleImpl(const IoSocketHandleImpl&) = delete;
  IoSocketHandleImpl& operator=(const IoSocketHandleImpl&) = delete;

  // IoHandle interface
  IoCallResult readv(size_t max_length,
                     RawSlice* slices,
                     size_t num_slices) override;
  IoCallResult read(Buffer& buffer,
                    optional<size_t> max_length = nullopt) override;
  IoCallResult writev(const ConstRawSlice* slices, size_t num_slices) override;
  IoCallResult write(Buffer& buffer) override;

  IoCallResult sendmsg(const ConstRawSlice* slices,
                       size_t num_slices,
                       int flags,
                       const Address::Ip* self_ip,
                       const Address::Instance& peer_address) override;
  IoCallResult recvmsg(RawSlice* slices,
                       size_t num_slices,
                       uint32_t self_port,
                       const UdpSaveCmsgConfig& save_cmsg_config,
                       RecvMsgOutput& output) override;
  IoCallResult recvmmsg(std::vector<RawSlice>& slices,
                        uint32_t self_port,
                        const UdpSaveCmsgConfig& save_cmsg_config,
                        RecvMsgOutput& output) override;

  IoCallVoidResult close() override;
  bool isOpen() const override { return fd_ != INVALID_SOCKET_FD; }

  IoResult<int> bind(const Address::InstanceConstSharedPtr& address) override;
  IoResult<int> listen(int backlog) override;
  IoResult<IoHandlePtr> accept() override;
  IoResult<int> connect(
      const Address::InstanceConstSharedPtr& address) override;
  IoResult<int> shutdown(int how) override;

  IoResult<int> setSocketOption(int level,
                                int optname,
                                const void* optval,
                                socklen_t optlen) override;
  IoResult<int> getSocketOption(int level,
                                int optname,
                                void* optval,
                                socklen_t* optlen) const override;
  IoResult<int> ioctl(unsigned long request, void* argp) override;

  void initializeFileEvent(event::Dispatcher& dispatcher,
                           event::FileReadyCb cb,
                           event::FileTriggerType trigger,
                           uint32_t events) override;
  void activateFileEvents(uint32_t events) override;
  void enableFileEvents(uint32_t events) override;
  void resetFileEvents() override;

  os_fd_t fd() const override { return fd_; }

  IoResult<Address::InstanceConstSharedPtr> localAddress() const override;
  IoResult<Address::InstanceConstSharedPtr> peerAddress() const override;
  optional<std::string> interfaceName() const override;

  IoResult<int> setBlocking(bool blocking) override;
  optional<std::chrono::milliseconds> lastRoundTripTime() const override;
  void configureInitialCongestionWindow(uint64_t bandwidth_bits_per_sec,
                                        std::chrono::microseconds rtt) override;

  IoHandlePtr duplicate() override;

 protected:
  void setNonBlocking();

  os_fd_t fd_;
  bool socket_v6only_;
  optional<int> domain_;
  event::FileEventPtr file_event_;
};

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_IO_SOCKET_HANDLE_IMPL_H