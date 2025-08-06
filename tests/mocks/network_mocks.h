#pragma once

#include <gmock/gmock.h>
#include "mcp/network/socket_interface.h"
#include "mcp/network/socket.h"
#include "mcp/network/transport_socket.h"
#include "mcp/network/filter.h"
#include "mcp/network/io_handle.h"
#include "mcp/buffer.h"
#include "mcp/result.h"

namespace mcp {
namespace test {

using ::testing::_;
using ::testing::Return;
using ::testing::ByMove;

// Simplified mock IoHandle for testing - only implement the minimum needed
class MockIoHandle : public network::IoHandle {
public:
  MOCK_METHOD(network::IoCallResult, readv, 
              (size_t max_length, RawSlice* slices, size_t num_slices), 
              (override));
  MOCK_METHOD(network::IoCallResult, read, 
              (Buffer& buffer, optional<size_t> max_length), 
              (override));
  MOCK_METHOD(network::IoCallResult, writev, 
              (const ConstRawSlice* slices, size_t num_slices), 
              (override));
  MOCK_METHOD(network::IoCallResult, write, 
              (Buffer& buffer), 
              (override));
  MOCK_METHOD(network::IoCallResult, sendmsg, 
              (const ConstRawSlice* slices, size_t num_slices, int flags,
               const network::Address::Ip* self_ip, const network::Address::Instance& peer_address), 
              (override));
  MOCK_METHOD(network::IoCallResult, recvmsg, 
              (RawSlice* slices, size_t num_slices, uint32_t self_port, 
               const network::UdpSaveCmsgConfig& save_cmsg_config, network::RecvMsgOutput& output), 
              (override));
  MOCK_METHOD(network::IoCallResult, recvmmsg, 
              (std::vector<RawSlice>& slices, uint32_t self_port, 
               const network::UdpSaveCmsgConfig& save_cmsg_config, network::RecvMsgOutput& output), 
              (override));
  MOCK_METHOD(network::IoVoidResult, close, (), (override));
  MOCK_METHOD(bool, isOpen, (), (const, override));
  MOCK_METHOD(network::os_fd_t, fd, (), (const, override));
  MOCK_METHOD(network::IoResult<int>, shutdown, (int how), (override));
  MOCK_METHOD(network::IoResult<int>, bind, 
              (const network::Address::InstanceConstSharedPtr& address), 
              (override));
  MOCK_METHOD(network::IoResult<int>, listen, (int backlog), (override));
  MOCK_METHOD(network::IoResult<int>, connect, 
              (const network::Address::InstanceConstSharedPtr& address), 
              (override));
  MOCK_METHOD(network::IoResult<int>, setBlocking, (bool blocking), (override));
  MOCK_METHOD(network::IoResult<network::Address::InstanceConstSharedPtr>, localAddress, 
              (), (const, override));
  MOCK_METHOD(network::IoResult<network::Address::InstanceConstSharedPtr>, peerAddress, 
              (), (const, override));
  MOCK_METHOD(network::IoResult<network::IoHandlePtr>, accept, 
              (), 
              (override));
  MOCK_METHOD(network::IoResult<int>, setSocketOption, 
              (int level, int optname, const void* optval, socklen_t optlen), 
              (override));
  MOCK_METHOD(network::IoResult<int>, getSocketOption, 
              (int level, int optname, void* optval, socklen_t* optlen), 
              (const, override));
  MOCK_METHOD(network::IoResult<int>, ioctl, 
              (unsigned long request, void* argp), 
              (override));
  // Note: waitForRead and waitForWrite were removed from the interface
  MOCK_METHOD(void, initializeFileEvent, 
              (event::Dispatcher& dispatcher, event::FileReadyCb cb, 
               event::FileTriggerType trigger, uint32_t events), 
              (override));
  MOCK_METHOD(void, activateFileEvents, (uint32_t events), (override));
  MOCK_METHOD(void, enableFileEvents, (uint32_t events), (override));
  MOCK_METHOD(void, resetFileEvents, (), (override));
  // Note: enableFileEvent was removed from the interface
  MOCK_METHOD(optional<std::string>, interfaceName, 
              (), (const, override));
  MOCK_METHOD(network::IoHandlePtr, duplicate, (), (override));
  MOCK_METHOD(optional<std::chrono::milliseconds>, lastRoundTripTime, (), (const, override));
  MOCK_METHOD(void, configureInitialCongestionWindow, 
              (uint64_t bandwidth_bits_per_sec, std::chrono::microseconds rtt), (override));
  
  MockIoHandle() {
    // Set default behaviors
    ON_CALL(*this, isOpen()).WillByDefault(Return(true));
    ON_CALL(*this, fd()).WillByDefault(Return(0));
    ON_CALL(*this, connect(_)).WillByDefault(Return(network::IoResult<int>::success(0)));
    // Create a properly initialized IoVoidResult for close()
    ON_CALL(*this, close()).WillByDefault([]() -> network::IoVoidResult {
      return network::IoVoidResult::success();
    });
  }
};

// Mock SocketInterface for testing
class MockSocketInterface : public network::SocketInterface {
public:
  MOCK_METHOD(network::IoResult<network::os_fd_t>, socket, 
              (network::SocketType type, network::Address::Type addr_type,
               optional<network::Address::IpVersion> version, bool socket_v6only),
              (override));
  // Note: accept is not in SocketInterface, it's in IoHandle
  MOCK_METHOD(network::IoResult<int>, socketPair,
              (network::SocketType type, network::os_fd_t fds[2]),
              (override));
  MOCK_METHOD(network::IoHandlePtr, ioHandleForFd,
              (network::os_fd_t fd, bool socket_v6only, optional<int> domain),
              (override));
  MOCK_METHOD(network::IoResult<int>, close, (network::os_fd_t fd), (override));
  MOCK_METHOD(network::IoResult<network::os_fd_t>, duplicate, (network::os_fd_t fd), (override));
  MOCK_METHOD(network::IoResult<int>, setFileFlags, (network::os_fd_t fd, int flags), (override));
  MOCK_METHOD(network::IoResult<int>, getFileFlags, (network::os_fd_t fd), (override));
  MOCK_METHOD(network::IoResult<int>, setsockopt,
              (network::os_fd_t fd, int level, int optname, const void* optval, socklen_t optlen),
              (override));
  MOCK_METHOD(network::IoResult<int>, getsockopt,
              (network::os_fd_t fd, int level, int optname, void* optval, socklen_t* optlen),
              (override));
  MOCK_METHOD(network::IoResult<int>, bind, (network::os_fd_t fd, const network::Address::Instance& addr), (override));
  MOCK_METHOD(network::IoResult<int>, listen, (network::os_fd_t fd, int backlog), (override));
  MOCK_METHOD(network::IoResult<network::os_fd_t>, accept, (network::os_fd_t fd, struct sockaddr* addr, socklen_t* addrlen), (override));
  MOCK_METHOD(network::IoResult<int>, connect, (network::os_fd_t fd, const network::Address::Instance& addr), (override));
  MOCK_METHOD(network::IoResult<int>, shutdown, (network::os_fd_t fd, int how), (override));
  MOCK_METHOD(network::IoResult<network::Address::InstanceConstSharedPtr>, localAddress, (network::os_fd_t fd), (override));
  MOCK_METHOD(network::IoResult<network::Address::InstanceConstSharedPtr>, peerAddress, (network::os_fd_t fd), (override));
  MOCK_METHOD(network::IoResult<int>, ioctl, (network::os_fd_t fd, unsigned long request, void* argp), (override));
  MOCK_METHOD(optional<std::string>, interfaceName, (network::os_fd_t fd), (override));
  MOCK_METHOD(bool, supportsSocketOption, (int level, int optname), (const, override));
  MOCK_METHOD(const std::string&, platformName, (), (const, override));
  MOCK_METHOD(bool, supportsUdpGro, (), (const, override));
  
  MockSocketInterface() {
    // Setup default behaviors for common operations
    ON_CALL(*this, socket(_, _, _, _))
        .WillByDefault(Return(network::IoResult<network::os_fd_t>::success(10)));  // Return a fake fd
    
    ON_CALL(*this, ioHandleForFd(_, _, _))
        .WillByDefault([]() {
          return std::make_unique<MockIoHandle>();
        });
    
    ON_CALL(*this, close(_))
        .WillByDefault(Return(network::IoResult<int>::success(0)));
    
    ON_CALL(*this, supportsUdpGro())
        .WillByDefault(Return(false));
        
    static std::string platform_name = "test";
    ON_CALL(*this, platformName())
        .WillByDefault(testing::ReturnRef(platform_name));
  }
};

// Mock TransportSocket for testing
class MockTransportSocket : public network::TransportSocket {
public:
  MOCK_METHOD(void, setTransportSocketCallbacks, (network::TransportSocketCallbacks& callbacks), (override));
  MOCK_METHOD(std::string, protocol, (), (const, override));
  MOCK_METHOD(std::string, failureReason, (), (const, override));
  MOCK_METHOD(bool, canFlushClose, (), (override));
  MOCK_METHOD(VoidResult, connect, (network::Socket& socket), (override));
  MOCK_METHOD(void, closeSocket, (network::ConnectionEvent event), (override));
  MOCK_METHOD(TransportIoResult, doRead, (Buffer& buffer), (override));
  MOCK_METHOD(TransportIoResult, doWrite, (Buffer& buffer, bool end_stream), (override));
  MOCK_METHOD(void, onConnected, (), (override));
  MOCK_METHOD(network::SslConnectionInfoConstSharedPtr, ssl, (), (const, override));
  MOCK_METHOD(bool, startSecureTransport, (), (override));
  MOCK_METHOD(void, configureInitialCongestionWindow, (uint64_t bandwidth_bits_per_sec, std::chrono::microseconds rtt), (override));
  
  MockTransportSocket() {
    // Set default behaviors
    ON_CALL(*this, protocol()).WillByDefault(Return("mock"));
    ON_CALL(*this, failureReason()).WillByDefault(Return(""));
    ON_CALL(*this, canFlushClose()).WillByDefault(Return(true));
    ON_CALL(*this, connect(_)).WillByDefault(Return(makeVoidSuccess()));
    ON_CALL(*this, doRead(_)).WillByDefault(Return(TransportIoResult{TransportIoResult::CONTINUE, 0, false, {}}));
    ON_CALL(*this, doWrite(_, _)).WillByDefault(Return(TransportIoResult{TransportIoResult::CONTINUE, 0, false, {}}));
  }
};

// Mock FilterChainFactory for testing
class MockFilterChainFactory : public network::FilterChainFactory {
public:
  MOCK_METHOD(bool, createFilterChain, (network::FilterManager& filter_manager), (const, override));
  
  MockFilterChainFactory() {
    ON_CALL(*this, createFilterChain(_)).WillByDefault([](network::FilterManager&) { return true; });
  }
};

}  // namespace test
}  // namespace mcp