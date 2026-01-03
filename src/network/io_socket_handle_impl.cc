#include "mcp/network/io_socket_handle_impl.h"

#include <algorithm>
#include <cstring>

#include "mcp/logging/log_macros.h"

#ifdef _WIN32
#include <mswsock.h>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#endif

#include "mcp/network/address_impl.h"

namespace mcp {
namespace network {

namespace {

// Additional platform-specific error codes not in io_handle.h
#ifdef _WIN32
constexpr int SOCKET_ERROR_INVAL = WSAEINVAL;
constexpr int SOCKET_ERROR_MSGSIZE = WSAEMSGSIZE;
#else
constexpr int SOCKET_ERROR_INVAL = EINVAL;
constexpr int SOCKET_ERROR_MSGSIZE = EMSGSIZE;
#endif

// Use constants from io_handle.h for common error codes:
// - SOCKET_ERROR_AGAIN
// - SOCKET_ERROR_INPROGRESS
// - SOCKET_ERROR_WOULDBLOCK
// - getLastSocketError()

// Maximum number of iovecs for vectored I/O
#ifdef IOV_MAX
constexpr size_t MAX_IOV = IOV_MAX;
#else
constexpr size_t MAX_IOV = 1024;
#endif

}  // namespace

IoSocketHandleImpl::IoSocketHandleImpl(os_fd_t fd,
                                       bool socket_v6only,
                                       optional<int> domain)
    : fd_(fd), socket_v6only_(socket_v6only), domain_(domain) {
  if (fd_ != INVALID_SOCKET_FD) {
    // Set non-blocking mode by default
    setNonBlocking();
  }
}

IoSocketHandleImpl::~IoSocketHandleImpl() {
  if (isOpen()) {
    close();
  }
}

void IoSocketHandleImpl::setNonBlocking() {
#ifdef _WIN32
  u_long mode = 1;
  int result = ::ioctlsocket(fd_, FIONBIO, &mode);
  if (result != 0) {
    GOPHER_LOG_TRACE(
        "IoSocketHandleImpl::setNonBlocking: fd={} result={} WSAError={}", fd_,
        result, WSAGetLastError());
  } else {
    GOPHER_LOG_TRACE("IoSocketHandleImpl::setNonBlocking: fd={} result={}", fd_,
                     result);
  }
#else
  int flags = ::fcntl(fd_, F_GETFL, 0);
  if (flags != -1) {
    ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
  }
#endif
}

IoCallResult IoSocketHandleImpl::readv(size_t max_length,
                                       RawSlice* slices,
                                       size_t num_slices) {
  if (!isOpen()) {
    return IoCallResult::error(EBADF);
  }

  // Calculate actual slices to read
  size_t bytes_to_read = 0;
  size_t num_slices_to_read = 0;
  for (size_t i = 0; i < num_slices && bytes_to_read < max_length; ++i) {
    size_t slice_len = std::min(slices[i].len_, max_length - bytes_to_read);
    if (slice_len > 0) {
      bytes_to_read += slice_len;
      num_slices_to_read++;
    }
  }

  if (num_slices_to_read == 0) {
    return IoCallResult::success(0);
  }

#ifdef _WIN32
  // Windows doesn't have readv, use WSARecv
  std::vector<WSABUF> buffers(num_slices_to_read);
  for (size_t i = 0; i < num_slices_to_read; ++i) {
    buffers[i].buf = static_cast<char*>(slices[i].mem_);
    buffers[i].len = static_cast<ULONG>(slices[i].len_);
  }

  DWORD bytes_received = 0;
  DWORD flags = 0;
  int result =
      ::WSARecv(fd_, buffers.data(), static_cast<DWORD>(num_slices_to_read),
                &bytes_received, &flags, nullptr, nullptr);

  if (result == 0) {
    return IoCallResult::success(bytes_received);
  } else {
    int error = getLastSocketError();
    if (error == SOCKET_ERROR_AGAIN) {
      return IoCallResult::error(error);
    }
    return IoCallResult::error(error);
  }
#else
  // Optimize single slice case
  if (num_slices_to_read == 1) {
    // Use read() instead of recv() for pipes (recv gives ENOTSOCK on pipes)
    ssize_t result = ::read(fd_, slices[0].mem_, slices[0].len_);
    if (result >= 0) {
      return IoCallResult::success(static_cast<size_t>(result));
    } else {
      return IoCallResult::error(errno);
    }
  }

  // Use readv for multiple slices
  std::vector<iovec> iov(num_slices_to_read);
  for (size_t i = 0; i < num_slices_to_read; ++i) {
    iov[i].iov_base = slices[i].mem_;
    iov[i].iov_len = slices[i].len_;
  }

  ssize_t result =
      ::readv(fd_, iov.data(), static_cast<int>(num_slices_to_read));
  if (result >= 0) {
    return IoCallResult::success(static_cast<size_t>(result));
  } else {
    return IoCallResult::error(getLastSocketError());
  }
#endif
}

IoCallResult IoSocketHandleImpl::read(Buffer& buffer,
                                      optional<size_t> max_length) {
  // Reserve space in buffer
  size_t length_to_read = max_length.value_or(16384);  // Default 16KB
  auto reservation = buffer.reserveForRead();

  // Read into reservation slices
  size_t total_read = 0;
  RawSlice* slices = reservation->slices();
  size_t num_slices = reservation->numSlices();

  auto result = readv(length_to_read, slices, num_slices);
  if (result.ok()) {
    total_read = *result;
    reservation->commit(total_read);
  }

  return result;
}

IoCallResult IoSocketHandleImpl::writev(const ConstRawSlice* slices,
                                        size_t num_slices) {
  if (!isOpen()) {
    return IoCallResult::error(EBADF);
  }

  if (num_slices == 0) {
    return IoCallResult::success(0);
  }

  // Limit number of slices to platform maximum
  num_slices = std::min(num_slices, MAX_IOV);

#ifdef _WIN32
  // Windows doesn't have writev, use WSASend
  std::vector<WSABUF> buffers(num_slices);
  for (size_t i = 0; i < num_slices; ++i) {
    buffers[i].buf =
        const_cast<char*>(static_cast<const char*>(slices[i].mem_));
    buffers[i].len = static_cast<ULONG>(slices[i].len_);
  }

  DWORD bytes_sent = 0;
  int result = ::WSASend(fd_, buffers.data(), static_cast<DWORD>(num_slices),
                         &bytes_sent, 0, nullptr, nullptr);

  if (result == 0) {
    return IoCallResult::success(bytes_sent);
  } else {
    int error = getLastSocketError();
    if (error == SOCKET_ERROR_AGAIN) {
      return IoCallResult::error(error);
    }
    return IoCallResult::error(error);
  }
#else
  // Optimize single slice case
  if (num_slices == 1) {
    // Use write() for pipes instead of send() to avoid ENOTSOCK error
    // Pipes don't support socket operations like send()
    ssize_t result = ::write(fd_, slices[0].mem_, slices[0].len_);
    if (result >= 0) {
      return IoCallResult::success(static_cast<size_t>(result));
    } else {
      return IoCallResult::error(getLastSocketError());
    }
  }

  // Use writev for multiple slices
  std::vector<iovec> iov(num_slices);
  for (size_t i = 0; i < num_slices; ++i) {
    iov[i].iov_base = const_cast<void*>(slices[i].mem_);
    iov[i].iov_len = slices[i].len_;
  }

  ssize_t result = ::writev(fd_, iov.data(), static_cast<int>(num_slices));
  if (result >= 0) {
    return IoCallResult::success(static_cast<size_t>(result));
  } else {
    return IoCallResult::error(getLastSocketError());
  }
#endif
}

IoCallResult IoSocketHandleImpl::write(Buffer& buffer) {
  // Get slices from buffer
  ConstRawSlice slices[MAX_IOV];
  size_t num_slices = buffer.getRawSlices(slices, MAX_IOV);

  auto result = writev(slices, num_slices);
  if (result.ok() && *result > 0) {
    buffer.drain(*result);
  }

  return result;
}

IoCallResult IoSocketHandleImpl::sendmsg(
    const ConstRawSlice* slices,
    size_t num_slices,
    int flags,
    const Address::Ip* self_ip,
    const Address::Instance& peer_address) {
  if (!isOpen()) {
    return IoCallResult::error(EBADF);
  }

#ifdef _WIN32
  // Windows doesn't have sendmsg, use WSASendTo
  std::vector<WSABUF> buffers(num_slices);
  for (size_t i = 0; i < num_slices; ++i) {
    buffers[i].buf =
        const_cast<char*>(static_cast<const char*>(slices[i].mem_));
    buffers[i].len = static_cast<ULONG>(slices[i].len_);
  }

  DWORD bytes_sent = 0;
  int result = ::WSASendTo(fd_, buffers.data(), static_cast<DWORD>(num_slices),
                           &bytes_sent, flags, peer_address.sockAddr(),
                           peer_address.sockAddrLen(), nullptr, nullptr);

  if (result == 0) {
    return IoCallResult::success(bytes_sent);
  } else {
    return IoCallResult::error(getLastSocketError());
  }
#else
  // Build iovec array
  std::vector<iovec> iov(num_slices);
  for (size_t i = 0; i < num_slices; ++i) {
    iov[i].iov_base = const_cast<void*>(slices[i].mem_);
    iov[i].iov_len = slices[i].len_;
  }

  // Build message header
  msghdr msg;
  std::memset(&msg, 0, sizeof(msg));
  msg.msg_name = const_cast<sockaddr*>(peer_address.sockAddr());
  msg.msg_namelen = peer_address.sockAddrLen();
  msg.msg_iov = iov.data();
  msg.msg_iovlen = num_slices;

  // Add control message for source address if specified
  char control_buffer[256];
  if (self_ip) {
    msg.msg_control = control_buffer;
    msg.msg_controllen = sizeof(control_buffer);

    // TODO: Add IP_PKTINFO/IPV6_PKTINFO control message
    // This requires platform-specific implementation
  }

  ssize_t result = ::sendmsg(fd_, &msg, flags | MSG_NOSIGNAL);
  if (result >= 0) {
    return IoCallResult::success(static_cast<size_t>(result));
  } else {
    return IoCallResult::error(getLastSocketError());
  }
#endif
}

IoCallResult IoSocketHandleImpl::recvmsg(
    RawSlice* slices,
    size_t num_slices,
    uint32_t self_port,
    const UdpSaveCmsgConfig& save_cmsg_config,
    RecvMsgOutput& output) {
  if (!isOpen()) {
    return IoCallResult::error(EBADF);
  }

  output.messages.clear();

#ifdef _WIN32
  // Windows doesn't have recvmsg, use WSARecvFrom
  std::vector<WSABUF> buffers(num_slices);
  size_t total_buffer_size = 0;
  for (size_t i = 0; i < num_slices; ++i) {
    buffers[i].buf = static_cast<char*>(slices[i].mem_);
    buffers[i].len = static_cast<ULONG>(slices[i].len_);
    total_buffer_size += slices[i].len_;
  }

  sockaddr_storage peer_addr;
  int peer_addr_len = sizeof(peer_addr);
  DWORD bytes_received = 0;
  DWORD flags = 0;

  int result = ::WSARecvFrom(fd_, buffers.data(),
                             static_cast<DWORD>(num_slices), &bytes_received,
                             &flags, reinterpret_cast<sockaddr*>(&peer_addr),
                             &peer_addr_len, nullptr, nullptr);

  if (result == 0) {
    RecvMsgOutput::ReceivedMessage msg;

    // Copy data to buffer
    size_t bytes_copied = 0;
    for (size_t i = 0; i < num_slices && bytes_copied < bytes_received; ++i) {
      size_t to_copy = std::min(slices[i].len_, bytes_received - bytes_copied);
      msg.data.add(slices[i].mem_, to_copy);
      bytes_copied += to_copy;
    }

    // Set peer address
    msg.peer_address =
        Address::addressFromSockAddr(peer_addr, peer_addr_len, socket_v6only_);

    // Windows doesn't provide local address in recvfrom
    msg.truncated = (flags & MSG_PARTIAL) != 0;

    output.messages.push_back(std::move(msg));
    return IoCallResult::success(1);
  } else {
    return IoCallResult::error(getLastSocketError());
  }
#else
  // Build iovec array
  std::vector<iovec> iov(num_slices);
  for (size_t i = 0; i < num_slices; ++i) {
    iov[i].iov_base = slices[i].mem_;
    iov[i].iov_len = slices[i].len_;
  }

  // Prepare message header
  sockaddr_storage peer_addr;
  msghdr msg;
  std::memset(&msg, 0, sizeof(msg));
  msg.msg_name = &peer_addr;
  msg.msg_namelen = sizeof(peer_addr);
  msg.msg_iov = iov.data();
  msg.msg_iovlen = num_slices;

  // Control message buffer
  alignas(alignof(cmsghdr)) char control_buffer[512];
  if (save_cmsg_config.save_local_address ||
      save_cmsg_config.save_packet_info) {
    msg.msg_control = control_buffer;
    msg.msg_controllen = sizeof(control_buffer);
  }

  ssize_t result = ::recvmsg(fd_, &msg, MSG_DONTWAIT);
  if (result < 0) {
    return IoCallResult::error(getLastSocketError());
  }

  // Create received message
  RecvMsgOutput::ReceivedMessage received_msg;

  // Copy data to buffer
  size_t bytes_copied = 0;
  for (size_t i = 0;
       i < num_slices && bytes_copied < static_cast<size_t>(result); ++i) {
    size_t to_copy =
        std::min(slices[i].len_, static_cast<size_t>(result) - bytes_copied);
    received_msg.data.add(slices[i].mem_, to_copy);
    bytes_copied += to_copy;
  }

  // Set peer address
  received_msg.peer_address =
      Address::addressFromSockAddr(peer_addr, msg.msg_namelen, socket_v6only_);

  // Check if message was truncated
  received_msg.truncated = (msg.msg_flags & MSG_TRUNC) != 0;

  // Process control messages
  if (msg.msg_controllen > 0) {
    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
      // Handle IP_PKTINFO / IPV6_PKTINFO for local address
      if (save_cmsg_config.save_local_address) {
#ifdef IP_PKTINFO
        if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_PKTINFO) {
          const in_pktinfo* pktinfo =
              reinterpret_cast<const in_pktinfo*>(CMSG_DATA(cmsg));
          sockaddr_in local_addr;
          std::memset(&local_addr, 0, sizeof(local_addr));
          local_addr.sin_family = AF_INET;
          local_addr.sin_addr = pktinfo->ipi_addr;
          local_addr.sin_port = htons(self_port);
          received_msg.local_address = Address::addressFromSockAddr(
              *reinterpret_cast<sockaddr_storage*>(&local_addr),
              sizeof(local_addr), false);
        }
#endif
#ifdef IPV6_PKTINFO
        if (cmsg->cmsg_level == IPPROTO_IPV6 &&
            cmsg->cmsg_type == IPV6_PKTINFO) {
          const in6_pktinfo* pktinfo =
              reinterpret_cast<const in6_pktinfo*>(CMSG_DATA(cmsg));
          sockaddr_in6 local_addr;
          std::memset(&local_addr, 0, sizeof(local_addr));
          local_addr.sin6_family = AF_INET6;
          local_addr.sin6_addr = pktinfo->ipi6_addr;
          local_addr.sin6_port = htons(self_port);
          received_msg.local_address = Address::addressFromSockAddr(
              *reinterpret_cast<sockaddr_storage*>(&local_addr),
              sizeof(local_addr), socket_v6only_);
        }
#endif
      }

      // TODO: Handle other control messages (timestamps, packet drops, etc.)
    }
  }

  output.messages.push_back(std::move(received_msg));
  return IoCallResult::success(1);
#endif
}

IoCallResult IoSocketHandleImpl::recvmmsg(
    std::vector<RawSlice>& slices,
    uint32_t self_port,
    const UdpSaveCmsgConfig& save_cmsg_config,
    RecvMsgOutput& output) {
#ifdef __linux__
  // Linux has recvmmsg for batch receiving
  // TODO: Implement recvmmsg support
  // For now, fall back to single recvmsg
#endif

  // Fall back to single recvmsg
  if (!slices.empty()) {
    return recvmsg(&slices[0], 1, self_port, save_cmsg_config, output);
  }
  return IoCallResult::success(0);
}

IoVoidResult IoSocketHandleImpl::close() {
  if (!isOpen()) {
    return IoVoidResult::success();
  }

  // Reset file events first
  resetFileEvents();

#ifdef _WIN32
  int result = ::closesocket(fd_);
#else
  int result = ::close(fd_);
#endif

  fd_ = INVALID_SOCKET_FD;

  if (result == 0) {
    return IoVoidResult::success();
  } else {
    return IoVoidResult::error(getLastSocketError());
  }
}

IoResult<int> IoSocketHandleImpl::bind(
    const Address::InstanceConstSharedPtr& address) {
  if (!isOpen()) {
    return IoResult<int>::error(EBADF);
  }

  GOPHER_LOG_TRACE("IoSocketHandleImpl::bind() called: fd={} addr={}", fd_,
                   address ? address->asStringView() : "<null>");
  int result = ::bind(fd_, address->sockAddr(), address->sockAddrLen());
  if (result == 0) {
    sockaddr_storage local_addr;
    socklen_t local_len = sizeof(local_addr);
    int local_result = ::getsockname(
        fd_, reinterpret_cast<sockaddr*>(&local_addr), &local_len);
    if (local_result == 0) {
      auto addr =
          Address::addressFromSockAddr(local_addr, local_len, socket_v6only_);
      GOPHER_LOG_TRACE("bind() local address: {}",
                       addr ? addr->asStringView() : "<unknown>");
    } else {
      GOPHER_LOG_TRACE("bind() getsockname failed: {}", getLastSocketError());
    }
    return IoResult<int>::success(0);
  } else {
    GOPHER_LOG_TRACE("bind() failed: error={}", getLastSocketError());
    return IoResult<int>::error(getLastSocketError());
  }
}

IoResult<int> IoSocketHandleImpl::listen(int backlog) {
  GOPHER_LOG_TRACE("IoSocketHandleImpl::listen() called: fd={} backlog={}", fd_,
                   backlog);

  if (!isOpen()) {
    GOPHER_LOG_TRACE("listen() failed: socket not open");
    return IoResult<int>::error(EBADF);
  }

  int result = ::listen(fd_, backlog);
#ifdef _WIN32
  if (result != 0) {
    GOPHER_LOG_TRACE("::listen() returned: {} WSAError={}", result,
                     WSAGetLastError());
  } else {
    GOPHER_LOG_TRACE("::listen() returned: {}", result);
  }
#else
  GOPHER_LOG_TRACE("::listen() returned: {}", result);
#endif

  if (result == 0) {
    int accept_conn = 0;
    socklen_t optlen = sizeof(accept_conn);
    int opt_result =
        ::getsockopt(fd_, SOL_SOCKET, SO_ACCEPTCONN,
                     reinterpret_cast<char*>(&accept_conn), &optlen);
    if (opt_result == 0) {
      GOPHER_LOG_TRACE("SO_ACCEPTCONN={}", accept_conn);
    } else {
      GOPHER_LOG_TRACE("SO_ACCEPTCONN check failed: {}", getLastSocketError());
    }
    return IoResult<int>::success(0);
  } else {
    return IoResult<int>::error(getLastSocketError());
  }
}

IoResult<IoHandlePtr> IoSocketHandleImpl::accept() {
  if (!isOpen()) {
    return IoResult<IoHandlePtr>::error(EBADF);
  }

  sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);

#ifdef _WIN32
  SOCKET new_fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&addr), &addr_len);
  if (new_fd != INVALID_SOCKET) {
    GOPHER_LOG_TRACE("accept() success: fd={}", new_fd);
    return IoResult<IoHandlePtr>::success(
        std::make_unique<IoSocketHandleImpl>(new_fd, socket_v6only_, domain_));
  } else {
    GOPHER_LOG_TRACE("accept() failed: WSAError={}", WSAGetLastError());
  }
#else
#ifdef __linux__
  int new_fd = ::accept4(fd_, reinterpret_cast<sockaddr*>(&addr), &addr_len,
                         SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (new_fd == -1 && errno == ENOSYS) {
    // accept4 not available, fall back to accept
    new_fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&addr), &addr_len);
    if (new_fd != -1) {
      // Set non-blocking and close-on-exec
      int flags = ::fcntl(new_fd, F_GETFL, 0);
      if (flags != -1) {
        ::fcntl(new_fd, F_SETFL, flags | O_NONBLOCK);
      }
      ::fcntl(new_fd, F_SETFD, FD_CLOEXEC);
    }
  }
#else
  // For BSD/macOS, use accept directly
  int new_fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&addr), &addr_len);
  if (new_fd != -1) {
    // Set non-blocking and close-on-exec
    int flags = ::fcntl(new_fd, F_GETFL, 0);
    if (flags != -1) {
      ::fcntl(new_fd, F_SETFL, flags | O_NONBLOCK);
    }
    ::fcntl(new_fd, F_SETFD, FD_CLOEXEC);
  }
#endif

  if (new_fd != -1) {
    return IoResult<IoHandlePtr>::success(
        std::make_unique<IoSocketHandleImpl>(new_fd, socket_v6only_, domain_));
  }
#endif

  return IoResult<IoHandlePtr>::error(getLastSocketError());
}

IoResult<int> IoSocketHandleImpl::connect(
    const Address::InstanceConstSharedPtr& address) {
  if (!isOpen()) {
    GOPHER_LOG_TRACE("connect(): fd not open");
    return IoResult<int>::error(EBADF);
  }

  GOPHER_LOG_TRACE("connect(): fd={} addr={}", fd_, address->asString());

  int result = ::connect(fd_, address->sockAddr(), address->sockAddrLen());
  if (result == 0) {
    // Immediate connection success (rare but can happen for local connections)
    GOPHER_LOG_TRACE("connect(): fd={} immediate success", fd_);
    return IoResult<int>::success(0);
  } else {
    int error = getLastSocketError();
    GOPHER_LOG_TRACE(
        "connect(): fd={} result={} error={} (INPROGRESS={} AGAIN={})", fd_,
        result, error, SOCKET_ERROR_INPROGRESS, SOCKET_ERROR_AGAIN);

    // For non-blocking connect:
    // - EINPROGRESS (Unix) or WSAEINPROGRESS (Windows): connection in progress
    // - EWOULDBLOCK/WSAEWOULDBLOCK: also means connection in progress on some
    // platforms Return error with the appropriate code so caller can wait for
    // completion
    if (error == SOCKET_ERROR_INPROGRESS || error == SOCKET_ERROR_AGAIN) {
      // Return EINPROGRESS (normalized) so caller knows to wait for write event
      GOPHER_LOG_TRACE(
          "connect(): fd={} connection in progress, returning INPROGRESS", fd_);
      return IoResult<int>::error(SOCKET_ERROR_INPROGRESS);
    }
    GOPHER_LOG_TRACE("connect(): fd={} connect failed with error={}", fd_,
                     error);
    return IoResult<int>::error(error);
  }
}

IoResult<int> IoSocketHandleImpl::shutdown(int how) {
  if (!isOpen()) {
    return IoResult<int>::error(EBADF);
  }

  int result = ::shutdown(fd_, how);
  if (result == 0) {
    return IoResult<int>::success(0);
  } else {
    return IoResult<int>::error(getLastSocketError());
  }
}

IoResult<int> IoSocketHandleImpl::setSocketOption(int level,
                                                  int optname,
                                                  const void* optval,
                                                  socklen_t optlen) {
  if (!isOpen()) {
    return IoResult<int>::error(EBADF);
  }

#ifdef _WIN32
  int result = ::setsockopt(fd_, level, optname,
                            static_cast<const char*>(optval), optlen);
#else
  int result = ::setsockopt(fd_, level, optname, optval, optlen);
#endif

  if (result == 0) {
    return IoResult<int>::success(0);
  } else {
    return IoResult<int>::error(getLastSocketError());
  }
}

IoResult<int> IoSocketHandleImpl::getSocketOption(int level,
                                                  int optname,
                                                  void* optval,
                                                  socklen_t* optlen) const {
  if (!isOpen()) {
    return IoResult<int>::error(EBADF);
  }

#ifdef _WIN32
  int result =
      ::getsockopt(fd_, level, optname, static_cast<char*>(optval), optlen);
#else
  int result = ::getsockopt(fd_, level, optname, optval, optlen);
#endif

  if (result == 0) {
    return IoResult<int>::success(0);
  } else {
    return IoResult<int>::error(getLastSocketError());
  }
}

IoResult<int> IoSocketHandleImpl::ioctl(unsigned long request, void* argp) {
  if (!isOpen()) {
    return IoResult<int>::error(EBADF);
  }

#ifdef _WIN32
  int result = ::ioctlsocket(fd_, request, static_cast<u_long*>(argp));
#else
  int result = ::ioctl(fd_, request, argp);
#endif

  if (result == 0) {
    return IoResult<int>::success(0);
  } else {
    return IoResult<int>::error(getLastSocketError());
  }
}

void IoSocketHandleImpl::initializeFileEvent(event::Dispatcher& dispatcher,
                                             event::FileReadyCb cb,
                                             event::FileTriggerType trigger,
                                             uint32_t events) {
  if (!isOpen()) {
    return;
  }

  file_event_ = dispatcher.createFileEvent(fd_, cb, trigger, events);
}

void IoSocketHandleImpl::activateFileEvents(uint32_t events) {
  if (file_event_) {
    file_event_->activate(events);
  }
}

void IoSocketHandleImpl::enableFileEvents(uint32_t events) {
  if (file_event_) {
    file_event_->setEnabled(events);
  }
}

void IoSocketHandleImpl::resetFileEvents() { file_event_.reset(); }

IoResult<Address::InstanceConstSharedPtr> IoSocketHandleImpl::localAddress()
    const {
  if (!isOpen()) {
    return IoResult<Address::InstanceConstSharedPtr>::error(EBADF);
  }

  sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);

  int result =
      ::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &addr_len);
  if (result == 0) {
    return IoResult<Address::InstanceConstSharedPtr>::success(
        Address::addressFromSockAddr(addr, addr_len, socket_v6only_));
  } else {
    return IoResult<Address::InstanceConstSharedPtr>::error(
        getLastSocketError());
  }
}

IoResult<Address::InstanceConstSharedPtr> IoSocketHandleImpl::peerAddress()
    const {
  if (!isOpen()) {
    return IoResult<Address::InstanceConstSharedPtr>::error(EBADF);
  }

  sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);

  int result =
      ::getpeername(fd_, reinterpret_cast<sockaddr*>(&addr), &addr_len);
  if (result == 0) {
    return IoResult<Address::InstanceConstSharedPtr>::success(
        Address::addressFromSockAddr(addr, addr_len, socket_v6only_));
  } else {
    return IoResult<Address::InstanceConstSharedPtr>::error(
        getLastSocketError());
  }
}

optional<std::string> IoSocketHandleImpl::interfaceName() const {
  // TODO: Implement interface name retrieval
  // This requires platform-specific implementation
  return nullopt;
}

IoResult<int> IoSocketHandleImpl::setBlocking(bool blocking) {
  if (!isOpen()) {
    return IoResult<int>::error(EBADF);
  }

#ifdef _WIN32
  u_long mode = blocking ? 0 : 1;
  int result = ::ioctlsocket(fd_, FIONBIO, &mode);
  if (result == 0) {
    return IoResult<int>::success(0);
  } else {
    return IoResult<int>::error(getLastSocketError());
  }
#else
  int flags = ::fcntl(fd_, F_GETFL, 0);
  if (flags == -1) {
    return IoResult<int>::error(errno);
  }

  if (blocking) {
    flags &= ~O_NONBLOCK;
  } else {
    flags |= O_NONBLOCK;
  }

  int result = ::fcntl(fd_, F_SETFL, flags);
  if (result == 0) {
    return IoResult<int>::success(0);
  } else {
    return IoResult<int>::error(errno);
  }
#endif
}

optional<std::chrono::milliseconds> IoSocketHandleImpl::lastRoundTripTime()
    const {
#ifdef TCP_INFO
  if (!isOpen() || !domain_.has_value() ||
      (*domain_ != AF_INET && *domain_ != AF_INET6)) {
    return nullopt;
  }

  tcp_info info;
  socklen_t info_len = sizeof(info);

  int result = ::getsockopt(fd_, IPPROTO_TCP, TCP_INFO, &info, &info_len);
  if (result == 0 && info_len == sizeof(info)) {
    // RTT is in microseconds
    return std::chrono::milliseconds(info.tcpi_rtt / 1000);
  }
#endif
  return nullopt;
}

void IoSocketHandleImpl::configureInitialCongestionWindow(
    uint64_t bandwidth_bits_per_sec, std::chrono::microseconds rtt) {
  // TODO: Implement TCP congestion window configuration
  // This requires platform-specific TCP tuning
  (void)bandwidth_bits_per_sec;
  (void)rtt;
}

IoHandlePtr IoSocketHandleImpl::duplicate() {
#ifdef _WIN32
  WSAPROTOCOL_INFO info;
  GOPHER_LOG_TRACE("IoSocketHandleImpl::duplicate() called: fd={}", fd_);
  if (::WSADuplicateSocket(fd_, ::GetCurrentProcessId(), &info) == 0) {
    GOPHER_LOG_TRACE("WSADuplicateSocket() success, calling WSASocket()");
    SOCKET new_fd =
        ::WSASocket(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO,
                    &info, 0, WSA_FLAG_OVERLAPPED);
    GOPHER_LOG_TRACE("WSASocket() returned: new_fd={} (INVALID_SOCKET={})",
                     new_fd, INVALID_SOCKET);
    if (new_fd != INVALID_SOCKET) {
      return std::make_unique<IoSocketHandleImpl>(new_fd, socket_v6only_,
                                                  domain_);
    } else {
      GOPHER_LOG_TRACE("WSASocket() failed: WSAError={}", WSAGetLastError());
    }
  } else {
    GOPHER_LOG_TRACE("WSADuplicateSocket() failed: WSAError={}",
                     WSAGetLastError());
  }
#else
  int new_fd = ::dup(fd_);
  if (new_fd != -1) {
    // Set non-blocking and close-on-exec on the new fd
    int flags = ::fcntl(new_fd, F_GETFL, 0);
    if (flags != -1) {
      ::fcntl(new_fd, F_SETFL, flags | O_NONBLOCK);
    }
    ::fcntl(new_fd, F_SETFD, FD_CLOEXEC);

    return std::make_unique<IoSocketHandleImpl>(new_fd, socket_v6only_,
                                                domain_);
  }
#endif
  return nullptr;
}

// Factory function
IoHandlePtr createIoSocketHandle(os_fd_t fd,
                                 bool socket_v6only,
                                 optional<int> domain) {
  return std::make_unique<IoSocketHandleImpl>(fd, socket_v6only, domain);
}

}  // namespace network
}  // namespace mcp
