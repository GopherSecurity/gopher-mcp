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
              (const ConstRawSlice* slices, size_t num_slices, uint32_t self_port, 
               const network::Address::Ip& peer_address), 
              (override));
  MOCK_METHOD(network::IoCallResult, recvmsg, 
              (RawSlice* slices, size_t num_slices, uint32_t self_port, 
               network::RecvMsgOutput& output), 
              (override));
  MOCK_METHOD(network::IoCallResult, recvmmsg, 
              (RawSlice* slices, size_t num_slices, uint32_t self_port, 
               network::RecvMsgOutput& output), 
              (override));
  MOCK_METHOD(network::IoVoidResult, close, (), (override));
  MOCK_METHOD(bool, isOpen, (), (const, override));
  MOCK_METHOD(network::os_fd_t, fd, (), (const, override));
  MOCK_METHOD(network::IoVoidResult, shutdown, (int how), (override));
  MOCK_METHOD(network::IoVoidResult, bind, 
              (const network::Address::InstanceConstSharedPtr& address), 
              (override));
  MOCK_METHOD(network::IoVoidResult, listen, (int backlog), (override));
  MOCK_METHOD(network::IoVoidResult, connect, 
              (const network::Address::InstanceConstSharedPtr& address), 
              (override));
  MOCK_METHOD(network::IoVoidResult, setBlocking, (bool blocking), (override));
  MOCK_METHOD(optional<network::Address::InstanceConstSharedPtr>, localAddress, 
              (), (override));
  MOCK_METHOD(optional<network::Address::InstanceConstSharedPtr>, peerAddress, 
              (), (override));
  MOCK_METHOD(network::SocketPtr, accept, 
              (struct sockaddr* addr, socklen_t* addrlen), 
              (override));
  MOCK_METHOD(network::IoVoidResult, setOption, 
              (int level, int optname, const void* optval, socklen_t optlen), 
              (override));
  MOCK_METHOD(network::IoVoidResult, getOption, 
              (int level, int optname, void* optval, socklen_t* optlen), 
              (const, override));
  MOCK_METHOD(network::IoVoidResult, ioctl, 
              (unsigned long request, void* argp), 
              (override));
  MOCK_METHOD(network::IoVoidResult, waitForRead, 
              (event::FileReadyCb cb, event::FileTriggerType trigger, 
               event::Dispatcher& dispatcher), 
              (override));
  MOCK_METHOD(network::IoVoidResult, waitForWrite, 
              (event::FileReadyCb cb, event::FileTriggerType trigger, 
               event::Dispatcher& dispatcher), 
              (override));
  MOCK_METHOD(void, initializeFileEvent, 
              (event::Dispatcher& dispatcher, event::FileReadyCb cb, 
               event::FileTriggerType trigger, uint32_t events), 
              (override));
  MOCK_METHOD(void, activateFileEvents, (uint32_t events), (override));
  MOCK_METHOD(void, enableFileEvents, (uint32_t events), (override));
  MOCK_METHOD(void, resetFileEvents, (), (override));
  MOCK_METHOD(network::IoVoidResult, enableFileEvent, 
              (event::FileReadyType events), 
              (override));
  MOCK_METHOD(network::SupportedSocketProtocols, supportedProtocols, 
              (), (const, override));
  MOCK_METHOD(network::IoVoidResult, setUdpSaveCmsgConfig, 
              (const network::UdpSaveCmsgConfig& config), 
              (override));
  
  MockIoHandle() {
    // Set default behaviors
    ON_CALL(*this, isOpen()).WillByDefault(Return(true));
    ON_CALL(*this, fd()).WillByDefault(Return(0));
  }
};

// Mock SocketInterface for testing
class MockSocketInterface : public network::SocketInterface {
public:
  MOCK_METHOD(network::IoResult<network::os_fd_t>, socket, 
              (network::SocketType type, network::Address::Type addr_type,
               network::Address::IpVersion version, bool socket_v6only),
              (override));
  MOCK_METHOD(network::IoResult<network::os_fd_t>, socket,
              (network::SocketType type, network::Address::Type addr_type),
              (override));
  MOCK_METHOD(network::IoResult<network::os_fd_t>, accept,
              (network::os_fd_t fd, sockaddr* addr, socklen_t* addrlen),
              (override));
  MOCK_METHOD(network::IoResult<int>, socketPair,
              (network::SocketType type, network::os_fd_t fds[2]),
              (override));
  MOCK_METHOD(network::IoHandlePtr, ioHandleForFd,
              (network::os_fd_t fd, bool socket_v6only, optional<int> domain),
              (override));
  MOCK_METHOD(network::IoResult<int>, close, (network::os_fd_t fd), (override));
  MOCK_METHOD(network::SocketPtr, createSocket,
              (const network::SocketOptionsSharedPtr& options,
               const network::Address::InstanceConstSharedPtr& address),
              (override));
  MOCK_METHOD(bool, supportsUdpGro, (), (const, override));
  MOCK_METHOD(network::ErrorCounters&, errorCounters, (), (override));
  
  MockSocketInterface() {
    // Setup default behaviors for common operations
    ON_CALL(*this, socket(_, _))
        .WillByDefault(Return(network::IoResult<network::os_fd_t>(10)));  // Return a fake fd
    
    ON_CALL(*this, socket(_, _, _, _))
        .WillByDefault(Return(network::IoResult<network::os_fd_t>(10)));  // Return a fake fd
    
    ON_CALL(*this, ioHandleForFd(_, _, _))
        .WillByDefault([]() {
          return std::make_unique<MockIoHandle>();
        });
    
    ON_CALL(*this, close(_))
        .WillByDefault(Return(network::IoResult<int>(0)));
    
    ON_CALL(*this, supportsUdpGro())
        .WillByDefault(Return(false));
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
    ON_CALL(*this, doRead(_)).WillByDefault(Return(TransportIoResult{TransportIoAction::KeepOpen, 0, false}));
    ON_CALL(*this, doWrite(_, _)).WillByDefault(Return(TransportIoResult{TransportIoAction::KeepOpen, 0, false}));
  }
};

// Mock FilterChainFactory for testing
class MockFilterChainFactory : public network::FilterChainFactory {
public:
  MOCK_METHOD(void, createFilterChain, (network::FilterManager& filter_manager), (const, override));
  
  MockFilterChainFactory() {
    ON_CALL(*this, createFilterChain(_)).WillByDefault([](network::FilterManager&) {});
  }
};

}  // namespace test
}  // namespace mcp