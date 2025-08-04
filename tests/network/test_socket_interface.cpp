#include <gtest/gtest.h>
#include <gmock/gmock.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netinet/tcp.h>
#endif

#include "mcp/network/socket_interface_impl.h"
#include "mcp/network/address_impl.h"

using namespace mcp;
using namespace mcp::network;
using namespace testing;

class SocketInterfaceTest : public Test {
protected:
  void SetUp() override {
    // Ensure we start with default interface
    resetSocketInterface();
    interface_ = &socketInterface();
  }
  
  void TearDown() override {
    // Reset to default
    resetSocketInterface();
  }
  
  SocketInterface* interface_;
};

// Mock socket interface for testing
class MockSocketInterface : public SocketInterface {
public:
  MOCK_METHOD(IoResult<os_fd_t>, socket, 
              (SocketType type, Address::Type addr_type,
               optional<Address::IpVersion> version, bool socket_v6only),
              (override));
  
  MOCK_METHOD(IoResult<int>, socketPair,
              (SocketType type, os_fd_t fds[2]),
              (override));
  
  MOCK_METHOD(IoHandlePtr, ioHandleForFd,
              (os_fd_t fd, bool socket_v6only, optional<int> domain),
              (override));
  
  MOCK_METHOD(IoResult<int>, close, (os_fd_t fd), (override));
  
  MOCK_METHOD(IoResult<os_fd_t>, duplicate, (os_fd_t fd), (override));
  
  MOCK_METHOD(IoResult<int>, setFileFlags, (os_fd_t fd, int flags), (override));
  
  MOCK_METHOD(IoResult<int>, getFileFlags, (os_fd_t fd), (override));
  
  MOCK_METHOD(IoResult<int>, setsockopt,
              (os_fd_t fd, int level, int optname,
               const void* optval, socklen_t optlen),
              (override));
  
  MOCK_METHOD(IoResult<int>, getsockopt,
              (os_fd_t fd, int level, int optname,
               void* optval, socklen_t* optlen),
              (override));
  
  MOCK_METHOD(IoResult<int>, bind,
              (os_fd_t fd, const Address::Instance& addr),
              (override));
  
  MOCK_METHOD(IoResult<int>, listen,
              (os_fd_t fd, int backlog),
              (override));
  
  MOCK_METHOD(IoResult<os_fd_t>, accept,
              (os_fd_t fd, sockaddr* addr, socklen_t* addrlen),
              (override));
  
  MOCK_METHOD(IoResult<int>, connect,
              (os_fd_t fd, const Address::Instance& addr),
              (override));
  
  MOCK_METHOD(IoResult<int>, shutdown,
              (os_fd_t fd, int how),
              (override));
  
  MOCK_METHOD(IoResult<Address::InstanceConstSharedPtr>, localAddress,
              (os_fd_t fd),
              (override));
  
  MOCK_METHOD(IoResult<Address::InstanceConstSharedPtr>, peerAddress,
              (os_fd_t fd),
              (override));
  
  MOCK_METHOD(IoResult<int>, ioctl,
              (os_fd_t fd, unsigned long request, void* argp),
              (override));
  
  MOCK_METHOD(optional<std::string>, interfaceName,
              (os_fd_t fd),
              (override));
  
  MOCK_METHOD(bool, supportsSocketOption,
              (int level, int optname),
              (const, override));
  
  MOCK_METHOD(const std::string&, platformName,
              (),
              (const, override));
  
  MOCK_METHOD(bool, supportsIoUring, (), (const, override));
  MOCK_METHOD(bool, supportsUdpGro, (), (const, override));
  MOCK_METHOD(bool, supportsUdpGso, (), (const, override));
  MOCK_METHOD(bool, supportsIpPktInfo, (), (const, override));
  MOCK_METHOD(bool, supportsReusePort, (), (const, override));
};

TEST_F(SocketInterfaceTest, DefaultInterface) {
  // Should have a default interface
  EXPECT_NE(&socketInterface(), nullptr);
  
  // Platform name should be set
  EXPECT_FALSE(interface_->platformName().empty());
  
  // Check capabilities
  bool has_some_capability = 
      interface_->supportsReusePort() ||
      interface_->supportsIpPktInfo() ||
      interface_->supportsIoUring() ||
      interface_->supportsUdpGro() ||
      interface_->supportsUdpGso();
  
  // Should support at least some capabilities
  EXPECT_TRUE(has_some_capability);
}

TEST_F(SocketInterfaceTest, CreateSocket) {
  // Create IPv4 TCP socket
  auto result = interface_->socket(SocketType::Stream, Address::Type::Ip,
                                   Address::IpVersion::v4, false);
  EXPECT_TRUE(result.ok());
  EXPECT_NE(*result, INVALID_SOCKET_FD);
  
  // Cleanup
  interface_->close(*result);
  
  // Create IPv6 TCP socket
  result = interface_->socket(SocketType::Stream, Address::Type::Ip,
                             Address::IpVersion::v6, true);
  EXPECT_TRUE(result.ok());
  EXPECT_NE(*result, INVALID_SOCKET_FD);
  
  // Cleanup
  interface_->close(*result);
  
  // Create UDP socket
  result = interface_->socket(SocketType::Datagram, Address::Type::Ip,
                             Address::IpVersion::v4, false);
  EXPECT_TRUE(result.ok());
  EXPECT_NE(*result, INVALID_SOCKET_FD);
  
  // Cleanup
  interface_->close(*result);
}

TEST_F(SocketInterfaceTest, SocketPair) {
  os_fd_t fds[2] = {INVALID_SOCKET_FD, INVALID_SOCKET_FD};
  
#ifdef _WIN32
  // Windows doesn't have native socketpair, but our implementation emulates it
  auto result = interface_->socketPair(SocketType::Stream, fds);
  if (result.ok()) {
    EXPECT_NE(fds[0], INVALID_SOCKET_FD);
    EXPECT_NE(fds[1], INVALID_SOCKET_FD);
    EXPECT_NE(fds[0], fds[1]);
    
    // Cleanup
    interface_->close(fds[0]);
    interface_->close(fds[1]);
  }
#else
  // Unix platforms have socketpair
  auto result = interface_->socketPair(SocketType::Stream, fds);
  EXPECT_TRUE(result.ok());
  EXPECT_NE(fds[0], INVALID_SOCKET_FD);
  EXPECT_NE(fds[1], INVALID_SOCKET_FD);
  EXPECT_NE(fds[0], fds[1]);
  
  // Test communication
  const char* msg = "test";
  ssize_t sent = ::write(fds[0], msg, 4);
  EXPECT_EQ(sent, 4);
  
  char buf[10];
  ssize_t received = ::read(fds[1], buf, sizeof(buf));
  EXPECT_EQ(received, 4);
  EXPECT_EQ(std::string(buf, 4), "test");
  
  // Cleanup
  interface_->close(fds[0]);
  interface_->close(fds[1]);
#endif
}

TEST_F(SocketInterfaceTest, IoHandleCreation) {
  // Create a socket first
  auto socket_result = interface_->socket(SocketType::Stream, Address::Type::Ip,
                                         Address::IpVersion::v4, false);
  ASSERT_TRUE(socket_result.ok());
  
  // Create IO handle for it
  auto handle = interface_->ioHandleForFd(*socket_result, false, AF_INET);
  ASSERT_NE(handle, nullptr);
  EXPECT_TRUE(handle->isOpen());
  EXPECT_EQ(handle->fd(), *socket_result);
  
  // Handle should close the socket
  handle.reset();
}

TEST_F(SocketInterfaceTest, SocketOptions) {
  // Create socket
  auto socket_result = interface_->socket(SocketType::Stream, Address::Type::Ip,
                                         Address::IpVersion::v4, false);
  ASSERT_TRUE(socket_result.ok());
  os_fd_t fd = *socket_result;
  
  // Test SO_REUSEADDR
  int reuse = 1;
  auto opt_result = interface_->setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                                           &reuse, sizeof(reuse));
  EXPECT_TRUE(opt_result.ok());
  
  // Get it back
  int value = 0;
  socklen_t len = sizeof(value);
  opt_result = interface_->getsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                                      &value, &len);
  EXPECT_TRUE(opt_result.ok());
  EXPECT_NE(value, 0);
  
  // Check if option is supported
  EXPECT_TRUE(interface_->supportsSocketOption(SOL_SOCKET, SO_REUSEADDR));
  
  // Cleanup
  interface_->close(fd);
}

TEST_F(SocketInterfaceTest, BindListenAccept) {
  // Create server socket
  auto server_result = interface_->socket(SocketType::Stream, Address::Type::Ip,
                                          Address::IpVersion::v4, false);
  ASSERT_TRUE(server_result.ok());
  os_fd_t server_fd = *server_result;
  
  // Enable reuse
  int reuse = 1;
  interface_->setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                        &reuse, sizeof(reuse));
  
  // Bind
  auto bind_addr = Address::loopbackAddress(Address::IpVersion::v4, 0);
  auto bind_result = interface_->bind(server_fd, *bind_addr);
  ASSERT_TRUE(bind_result.ok());
  
  // Get actual port
  auto local_addr_result = interface_->localAddress(server_fd);
  ASSERT_TRUE(local_addr_result.ok());
  uint16_t port = (*local_addr_result)->ip()->port();
  EXPECT_NE(port, 0);
  
  // Listen
  auto listen_result = interface_->listen(server_fd, 5);
  ASSERT_TRUE(listen_result.ok());
  
  // Create client socket
  auto client_result = interface_->socket(SocketType::Stream, Address::Type::Ip,
                                         Address::IpVersion::v4, false);
  ASSERT_TRUE(client_result.ok());
  os_fd_t client_fd = *client_result;
  
  // Connect (non-blocking, so may return EINPROGRESS)
  auto connect_addr = Address::loopbackAddress(Address::IpVersion::v4, port);
  auto connect_result = interface_->connect(client_fd, *connect_addr);
  EXPECT_TRUE(connect_result.ok() || 
              (connect_result.error_code && 
               (*connect_result.error_code == EINPROGRESS ||
                *connect_result.error_code == EWOULDBLOCK)));
  
  // Accept
  sockaddr_storage peer_addr;
  socklen_t peer_len = sizeof(peer_addr);
  auto accept_result = interface_->accept(server_fd,
                                         reinterpret_cast<sockaddr*>(&peer_addr),
                                         &peer_len);
  
  // May need to retry for non-blocking
  int retries = 0;
  while (!accept_result.ok() && retries++ < 10) {
    if (!accept_result.wouldBlock()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    accept_result = interface_->accept(server_fd,
                                      reinterpret_cast<sockaddr*>(&peer_addr),
                                      &peer_len);
  }
  
  if (accept_result.ok()) {
    EXPECT_NE(*accept_result, INVALID_SOCKET_FD);
    interface_->close(*accept_result);
  }
  
  // Cleanup
  interface_->close(client_fd);
  interface_->close(server_fd);
}

TEST_F(SocketInterfaceTest, FileFlags) {
#ifndef _WIN32
  // Create socket
  auto socket_result = interface_->socket(SocketType::Stream, Address::Type::Ip,
                                         Address::IpVersion::v4, false);
  ASSERT_TRUE(socket_result.ok());
  os_fd_t fd = *socket_result;
  
  // Get current flags
  auto flags_result = interface_->getFileFlags(fd);
  ASSERT_TRUE(flags_result.ok());
  
  // Should have O_NONBLOCK set by default
  EXPECT_TRUE((*flags_result & O_NONBLOCK) != 0);
  
  // Set additional flags
  auto set_result = interface_->setFileFlags(fd, *flags_result | O_APPEND);
  EXPECT_TRUE(set_result.ok());
  
  // Cleanup
  interface_->close(fd);
#endif
}

TEST_F(SocketInterfaceTest, DuplicateSocket) {
  // Create socket
  auto socket_result = interface_->socket(SocketType::Stream, Address::Type::Ip,
                                         Address::IpVersion::v4, false);
  ASSERT_TRUE(socket_result.ok());
  os_fd_t fd = *socket_result;
  
  // Duplicate it
  auto dup_result = interface_->duplicate(fd);
  if (dup_result.ok()) {
    EXPECT_NE(*dup_result, INVALID_SOCKET_FD);
    EXPECT_NE(*dup_result, fd);
    
    // Both should be valid
    int value = 0;
    socklen_t len = sizeof(value);
    EXPECT_TRUE(interface_->getsockopt(fd, SOL_SOCKET, SO_TYPE,
                                      &value, &len).ok());
    EXPECT_TRUE(interface_->getsockopt(*dup_result, SOL_SOCKET, SO_TYPE,
                                      &value, &len).ok());
    
    // Cleanup
    interface_->close(*dup_result);
  }
  
  interface_->close(fd);
}

TEST_F(SocketInterfaceTest, Shutdown) {
  // Create connected socket pair
  os_fd_t fds[2];
  auto pair_result = interface_->socketPair(SocketType::Stream, fds);
  
  if (!pair_result.ok()) {
    // Try manual connection
    auto server_result = interface_->socket(SocketType::Stream, Address::Type::Ip,
                                           Address::IpVersion::v4, false);
    ASSERT_TRUE(server_result.ok());
    fds[0] = *server_result;
    
    // Skip shutdown test if we can't create a pair
    interface_->close(fds[0]);
    return;
  }
  
  // Shutdown write side
  auto shutdown_result = interface_->shutdown(fds[0], SHUT_WR);
  EXPECT_TRUE(shutdown_result.ok());
  
  // Read from other side should see EOF eventually
  char buf[10];
  ssize_t n = ::recv(fds[1], buf, sizeof(buf), MSG_DONTWAIT);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    // Wait a bit and try again
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    n = ::recv(fds[1], buf, sizeof(buf), MSG_DONTWAIT);
  }
  
  // Cleanup
  interface_->close(fds[0]);
  interface_->close(fds[1]);
}

TEST_F(SocketInterfaceTest, CustomInterface) {
  auto mock_interface = std::make_unique<MockSocketInterface>();
  MockSocketInterface* mock_ptr = mock_interface.get();
  
  // Create a persistent string for the mock to return
  static const std::string mock_platform_name = "mock";
  
  // Set expectations
  EXPECT_CALL(*mock_ptr, platformName())
      .WillRepeatedly(ReturnRef(mock_platform_name));
  
  EXPECT_CALL(*mock_ptr, supportsReusePort())
      .WillRepeatedly(Return(true));
  
  // Install mock
  setSocketInterface(std::move(mock_interface));
  
  // Should use mock now
  EXPECT_EQ(&socketInterface(), mock_ptr);
  EXPECT_EQ(socketInterface().platformName(), "mock");
  EXPECT_TRUE(socketInterface().supportsReusePort());
  
  // Reset cleans up
  resetSocketInterface();
  EXPECT_NE(&socketInterface(), mock_ptr);
}

TEST_F(SocketInterfaceTest, PlatformCapabilities) {
  // Test various capability queries
  const std::string& platform = interface_->platformName();
  
  if (platform == "linux") {
    // Linux should support some specific features
    EXPECT_TRUE(interface_->supportsIpPktInfo());
#ifdef SO_REUSEPORT
    EXPECT_TRUE(interface_->supportsReusePort());
#endif
  } else if (platform == "windows") {
    // Windows has limited support
    EXPECT_FALSE(interface_->supportsIoUring());
  }
  
  // Common socket options should be supported
  EXPECT_TRUE(interface_->supportsSocketOption(SOL_SOCKET, SO_REUSEADDR));
  EXPECT_TRUE(interface_->supportsSocketOption(IPPROTO_TCP, TCP_NODELAY));
  
  // Random unsupported option
  EXPECT_FALSE(interface_->supportsSocketOption(999, 999));
}

TEST_F(SocketInterfaceTest, Ioctl) {
  // Create socket
  auto socket_result = interface_->socket(SocketType::Stream, Address::Type::Ip,
                                         Address::IpVersion::v4, false);
  ASSERT_TRUE(socket_result.ok());
  os_fd_t fd = *socket_result;
  
#ifdef FIONREAD
  // Try to get bytes available to read (should be 0)
  int bytes_available = 0;
  auto ioctl_result = interface_->ioctl(fd, FIONREAD, &bytes_available);
  if (ioctl_result.ok()) {
    EXPECT_EQ(bytes_available, 0);
  }
#endif
  
  // Cleanup
  interface_->close(fd);
}

// Test loader registration
class TestSocketInterfaceLoader : public SocketInterfaceLoader {
public:
  const std::string& name() const override {
    static std::string n = "test_loader";
    return n;
  }
  
  SocketInterfacePtr createSocketInterface() override {
    return std::make_unique<SocketInterfaceImpl>();
  }
};

TEST_F(SocketInterfaceTest, LoaderRegistration) {
  // Register a loader
  auto loader = std::make_unique<TestSocketInterfaceLoader>();
  registerSocketInterfaceLoader(std::move(loader));
  
  // Should appear in list
  auto names = getSocketInterfaceLoaderNames();
  EXPECT_THAT(names, Contains("test_loader"));
}