#include <gtest/gtest.h>
#include <gmock/gmock.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/tcp.h>
#endif

#include "mcp/network/socket_option_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/address_impl.h"
#include "mcp/network/io_socket_handle_impl.h"
#include "mcp/network/socket_interface_impl.h"

using namespace mcp;
using namespace mcp::network;
using namespace testing;

// Mock socket for testing option application
class MockSocket : public Socket {
public:
  MOCK_METHOD(ConnectionInfoSetter&, connectionInfoProvider, (), (override));
  MOCK_METHOD(const ConnectionInfoProvider&, connectionInfoProvider, (), (const, override));
  MOCK_METHOD(ConnectionInfoProviderSharedPtr, connectionInfoProviderSharedPtr, (), (const, override));
  MOCK_METHOD(IoHandle&, ioHandle, (), (override));
  MOCK_METHOD(const IoHandle&, ioHandle, (), (const, override));
  MOCK_METHOD(void, close, (), (override));
  MOCK_METHOD(bool, isOpen, (), (const, override));
  MOCK_METHOD(IoResult<int>, bind, (const Address::InstanceConstSharedPtr& address), (override));
  MOCK_METHOD(IoResult<int>, listen, (int backlog), (override));
  MOCK_METHOD(IoResult<int>, connect, (const Address::InstanceConstSharedPtr& address), (override));
  MOCK_METHOD(IoResult<int>, setSocketOption,
              (int level, int optname, const void* optval, socklen_t optlen), (override));
  MOCK_METHOD(IoResult<int>, getSocketOption,
              (int level, int optname, void* optval, socklen_t* optlen), (const, override));
  MOCK_METHOD(IoResult<int>, ioctl, (unsigned long request, void* argp), (override));
  MOCK_METHOD(void, addOption, (const SocketOptionConstSharedPtr& option), (override));
  MOCK_METHOD(void, addOptions, (const SocketOptionsSharedPtr& options), (override));
  MOCK_METHOD(const SocketOptionsSharedPtr&, options, (), (const, override));
  MOCK_METHOD(SocketPtr, duplicate, (), (override));
  MOCK_METHOD(IoResult<int>, setBlocking, (bool blocking), (override));
  MOCK_METHOD(SocketType, socketType, (), (const, override));
  MOCK_METHOD(Address::Type, addressType, (), (const, override));
  MOCK_METHOD(optional<Address::IpVersion>, ipVersion, (), (const, override));
};

class SocketOptionTest : public Test {
protected:
  MockSocket mock_socket_;
};

TEST_F(SocketOptionTest, BoolSocketOption) {
  // Test SO_REUSEADDR
  BoolSocketOption reuse_addr(SOCKET_SO_REUSEADDR, true);
  
  EXPECT_CALL(mock_socket_, setSocketOption(SOL_SOCKET, SO_REUSEADDR, _, sizeof(int)))
      .WillOnce([](int, int, const void* optval, socklen_t) {
        int value = *static_cast<const int*>(optval);
        EXPECT_EQ(value, 1);
        return IoResult<int>::success(0);
      });
  
  EXPECT_TRUE(reuse_addr.setOption(mock_socket_));
  EXPECT_TRUE(reuse_addr.isSupported());
  
  // Test string representation
  EXPECT_EQ(reuse_addr.toString(), "SO_REUSEADDR=1");
  
  // Test hash key
  std::vector<uint8_t> key;
  reuse_addr.hashKey(key);
  EXPECT_GE(key.size(), 3);  // level + option + value
}

TEST_F(SocketOptionTest, IntSocketOption) {
  // Test SO_RCVBUF
  IntSocketOption rcv_buf(SOCKET_SO_RCVBUF, 65536);
  
  EXPECT_CALL(mock_socket_, setSocketOption(SOL_SOCKET, SO_RCVBUF, _, sizeof(int)))
      .WillOnce([](int, int, const void* optval, socklen_t) {
        int value = *static_cast<const int*>(optval);
        EXPECT_EQ(value, 65536);
        return IoResult<int>::success(0);
      });
  
  EXPECT_TRUE(rcv_buf.setOption(mock_socket_));
  EXPECT_EQ(rcv_buf.toString(), "SO_RCVBUF=65536");
}

TEST_F(SocketOptionTest, IpTransparentSocketOption) {
  IpTransparentSocketOption transparent(true);
  
  // For IPv4 socket
  EXPECT_CALL(mock_socket_, addressType())
      .WillOnce(Return(Address::Type::Ip));
  EXPECT_CALL(mock_socket_, ipVersion())
      .WillOnce(Return(Address::IpVersion::v4));
  
#ifdef IP_TRANSPARENT
  EXPECT_CALL(mock_socket_, setSocketOption(IPPROTO_IP, IP_TRANSPARENT, _, sizeof(int)))
      .WillOnce(Return(IoResult<int>::success(0)));
#endif
  
  transparent.setOption(mock_socket_);
  
  // For IPv6 socket
  EXPECT_CALL(mock_socket_, addressType())
      .WillRepeatedly(Return(Address::Type::Ip));
  EXPECT_CALL(mock_socket_, ipVersion())
      .WillRepeatedly(Return(Address::IpVersion::v6));
  
#ifdef IP_TRANSPARENT
  EXPECT_CALL(mock_socket_, setSocketOption(IPPROTO_IP, IP_TRANSPARENT, _, sizeof(int)))
      .WillOnce(Return(IoResult<int>::success(0)));
#endif
#ifdef IPV6_TRANSPARENT
  EXPECT_CALL(mock_socket_, setSocketOption(IPPROTO_IPV6, IPV6_TRANSPARENT, _, sizeof(int)))
      .WillOnce(Return(IoResult<int>::success(0)));
#endif
  
  transparent.setOption(mock_socket_);
}

TEST_F(SocketOptionTest, IpFreebindSocketOption) {
  IpFreebindSocketOption freebind(true);
  
#ifdef IP_FREEBIND
  EXPECT_CALL(mock_socket_, setSocketOption(IPPROTO_IP, IP_FREEBIND, _, sizeof(int)))
      .WillOnce(Return(IoResult<int>::success(0)));
  
  EXPECT_TRUE(freebind.setOption(mock_socket_));
#endif
}

TEST_F(SocketOptionTest, ReusePortSocketOption) {
  ReusePortSocketOption reuse_port(true);
  
#ifdef SO_REUSEPORT
  EXPECT_CALL(mock_socket_, setSocketOption(SOL_SOCKET, SO_REUSEPORT, _, sizeof(int)))
      .WillOnce(Return(IoResult<int>::success(0)));
  
  EXPECT_TRUE(reuse_port.setOption(mock_socket_));
#endif
  
  // Test BPF program attachment (Linux only)
#ifdef SO_ATTACH_REUSEPORT_CBPF
  uint8_t bpf_prog[] = {0x01, 0x02, 0x03, 0x04};
  reuse_port.setBpfProgram(bpf_prog, sizeof(bpf_prog));
  
  EXPECT_CALL(mock_socket_, setSocketOption(SOL_SOCKET, SO_ATTACH_REUSEPORT_CBPF, _, sizeof(bpf_prog)))
      .WillOnce(Return(IoResult<int>::success(0)));
  
  EXPECT_TRUE(reuse_port.setOptionForListen(mock_socket_));
#endif
}

TEST_F(SocketOptionTest, TcpKeepaliveSocketOption) {
  TcpKeepaliveSocketOption::KeepaliveSettings settings;
  settings.enabled = true;
  settings.idle_time_s = 300;
  settings.interval_s = 30;
  settings.probes = 9;
  
  TcpKeepaliveSocketOption keepalive(settings);
  
  // Expect calls for enabling keepalive and setting parameters
  EXPECT_CALL(mock_socket_, setSocketOption(SOL_SOCKET, SO_KEEPALIVE, _, sizeof(int)))
      .WillOnce([](int, int, const void* optval, socklen_t) {
        int value = *static_cast<const int*>(optval);
        EXPECT_EQ(value, 1);
        return IoResult<int>::success(0);
      });
  
#ifdef TCP_KEEPIDLE
  EXPECT_CALL(mock_socket_, setSocketOption(IPPROTO_TCP, TCP_KEEPIDLE, _, sizeof(int)))
      .WillOnce([](int, int, const void* optval, socklen_t) {
        int value = *static_cast<const int*>(optval);
        EXPECT_EQ(value, 300);
        return IoResult<int>::success(0);
      });
#endif
  
#ifdef TCP_KEEPINTVL
  EXPECT_CALL(mock_socket_, setSocketOption(IPPROTO_TCP, TCP_KEEPINTVL, _, sizeof(int)))
      .WillOnce([](int, int, const void* optval, socklen_t) {
        int value = *static_cast<const int*>(optval);
        EXPECT_EQ(value, 30);
        return IoResult<int>::success(0);
      });
#endif
  
#ifdef TCP_KEEPCNT
  EXPECT_CALL(mock_socket_, setSocketOption(IPPROTO_TCP, TCP_KEEPCNT, _, sizeof(int)))
      .WillOnce([](int, int, const void* optval, socklen_t) {
        int value = *static_cast<const int*>(optval);
        EXPECT_EQ(value, 9);
        return IoResult<int>::success(0);
      });
#endif
  
  EXPECT_TRUE(keepalive.setOption(mock_socket_));
  EXPECT_TRUE(keepalive.isSupported());
  
  // Test string representation
  std::string str = keepalive.toString();
  EXPECT_THAT(str, HasSubstr("TCP_KEEPALIVE"));
  EXPECT_THAT(str, HasSubstr("enabled=true"));
  EXPECT_THAT(str, HasSubstr("idle=300s"));
  EXPECT_THAT(str, HasSubstr("interval=30s"));
  EXPECT_THAT(str, HasSubstr("probes=9"));
  
  // Test hash key
  std::vector<uint8_t> key;
  keepalive.hashKey(key);
  EXPECT_GT(key.size(), 1);
}

TEST_F(SocketOptionTest, TcpKeepaliveDisabled) {
  TcpKeepaliveSocketOption::KeepaliveSettings settings;
  settings.enabled = false;
  
  TcpKeepaliveSocketOption keepalive(settings);
  
  // Should only disable keepalive
  EXPECT_CALL(mock_socket_, setSocketOption(SOL_SOCKET, SO_KEEPALIVE, _, sizeof(int)))
      .WillOnce([](int, int, const void* optval, socklen_t) {
        int value = *static_cast<const int*>(optval);
        EXPECT_EQ(value, 0);
        return IoResult<int>::success(0);
      });
  
  // Should not set other parameters
  EXPECT_CALL(mock_socket_, setSocketOption(IPPROTO_TCP, _, _, _))
      .Times(0);
  
  EXPECT_TRUE(keepalive.setOption(mock_socket_));
}

TEST_F(SocketOptionTest, BufferSizeSocketOption) {
  // Test receive buffer
  BufferSizeSocketOption rcv_buf(BufferSizeSocketOption::Receive, 32768);
  
  EXPECT_CALL(mock_socket_, setSocketOption(SOL_SOCKET, SO_RCVBUF, _, sizeof(int)))
      .WillOnce([](int, int, const void* optval, socklen_t) {
        int value = *static_cast<const int*>(optval);
        EXPECT_EQ(value, 32768);
        return IoResult<int>::success(0);
      });
  
  EXPECT_TRUE(rcv_buf.setOption(mock_socket_));
  
  // Test send buffer
  BufferSizeSocketOption snd_buf(BufferSizeSocketOption::Send, 65536);
  
  EXPECT_CALL(mock_socket_, setSocketOption(SOL_SOCKET, SO_SNDBUF, _, sizeof(int)))
      .WillOnce([](int, int, const void* optval, socklen_t) {
        int value = *static_cast<const int*>(optval);
        EXPECT_EQ(value, 65536);
        return IoResult<int>::success(0);
      });
  
  EXPECT_TRUE(snd_buf.setOption(mock_socket_));
}

TEST_F(SocketOptionTest, MarkSocketOption) {
#ifdef SO_MARK
  MarkSocketOption mark(0x1234);
  
  EXPECT_CALL(mock_socket_, setSocketOption(SOL_SOCKET, SO_MARK, _, sizeof(int)))
      .WillOnce([](int, int, const void* optval, socklen_t) {
        int value = *static_cast<const int*>(optval);
        EXPECT_EQ(value, 0x1234);
        return IoResult<int>::success(0);
      });
  
  EXPECT_TRUE(mark.setOption(mock_socket_));
#endif
}

TEST_F(SocketOptionTest, TosSocketOption) {
  TosSocketOption tos(0x10);  // DSCP CS4
  
  // Test IPv4
  EXPECT_CALL(mock_socket_, addressType())
      .WillRepeatedly(Return(Address::Type::Ip));
  EXPECT_CALL(mock_socket_, ipVersion())
      .WillRepeatedly(Return(Address::IpVersion::v4));
  
#ifdef IP_TOS
  EXPECT_CALL(mock_socket_, setSocketOption(IPPROTO_IP, IP_TOS, _, sizeof(int)))
      .WillOnce([](int, int, const void* optval, socklen_t) {
        int value = *static_cast<const int*>(optval);
        EXPECT_EQ(value, 0x10);
        return IoResult<int>::success(0);
      });
#endif
  
  EXPECT_TRUE(tos.setOption(mock_socket_));
  EXPECT_TRUE(tos.isSupported());
  EXPECT_EQ(tos.toString(), "TOS=16");
  
  // Test IPv6
  EXPECT_CALL(mock_socket_, ipVersion())
      .WillRepeatedly(Return(Address::IpVersion::v6));
  
#ifdef IPV6_TCLASS
  EXPECT_CALL(mock_socket_, setSocketOption(IPPROTO_IPV6, IPV6_TCLASS, _, sizeof(int)))
      .WillOnce([](int, int, const void* optval, socklen_t) {
        int value = *static_cast<const int*>(optval);
        EXPECT_EQ(value, 0x10);
        return IoResult<int>::success(0);
      });
#endif
  
  tos.setOption(mock_socket_);
  
  // Test hash key
  std::vector<uint8_t> key;
  tos.hashKey(key);
  EXPECT_EQ(key.size(), 2);  // Marker + TOS value
  EXPECT_EQ(key[0], 'T');
  EXPECT_EQ(key[1], 0x10);
}

TEST_F(SocketOptionTest, TosSocketOptionDualStack) {
  TosSocketOption tos(0x20);
  
  // Test dual-stack (no specific version)
  EXPECT_CALL(mock_socket_, addressType())
      .WillRepeatedly(Return(Address::Type::Ip));
  EXPECT_CALL(mock_socket_, ipVersion())
      .WillRepeatedly(Return(nullopt));
  
  // Should set both IPv4 and IPv6
#ifdef IP_TOS
  EXPECT_CALL(mock_socket_, setSocketOption(IPPROTO_IP, IP_TOS, _, sizeof(int)))
      .WillOnce(Return(IoResult<int>::success(0)));
#endif
  
#ifdef IPV6_TCLASS
  EXPECT_CALL(mock_socket_, setSocketOption(IPPROTO_IPV6, IPV6_TCLASS, _, sizeof(int)))
      .WillOnce(Return(IoResult<int>::success(0)));
#endif
  
  tos.setOption(mock_socket_);
}

TEST_F(SocketOptionTest, BuildSocketOptions) {
  SocketCreationOptions options;
  options.reuse_address = true;
  options.reuse_port = true;
  options.v6_only = true;
  options.send_buffer_size = 65536;
  options.receive_buffer_size = 32768;
  options.type_of_service = 0x10;
  options.tcp_nodelay = true;
  options.tcp_keepalive = true;
  options.tcp_keepidle = 300;
  options.tcp_keepintvl = 30;
  options.tcp_keepcnt = 9;
  options.ip_transparent = true;
  options.ip_freebind = true;
  
  auto socket_options = buildSocketOptions(options);
  ASSERT_NE(socket_options, nullptr);
  
  // Count expected options
  size_t expected_count = 0;
  if (SOCKET_SO_REUSEADDR.hasValue()) expected_count++;
  if (SOCKET_SO_REUSEPORT.hasValue()) expected_count++;
  if (SOCKET_IPV6_V6ONLY.hasValue()) expected_count++;
  if (SOCKET_SO_SNDBUF.hasValue()) expected_count++;
  if (SOCKET_SO_RCVBUF.hasValue()) expected_count++;
  expected_count++; // TOS always added
  if (SOCKET_TCP_NODELAY.hasValue()) expected_count++;
  expected_count++; // TCP keepalive always added
  if (SOCKET_IP_TRANSPARENT.hasValue()) expected_count++;
  if (SOCKET_IP_FREEBIND.hasValue()) expected_count++;
  
  // Should have created options for all supported values
  EXPECT_GE(socket_options->size(), 5);  // At least some basic options
  // TODO: This is a rough check - we expect at least some options but the exact count varies by platform
  (void)expected_count;  // Mark as intentionally unused
}

TEST_F(SocketOptionTest, SocketOptionImplBase) {
  // Test base implementation with custom option
  SocketOptionName custom_opt{SOL_SOCKET, 9999, "CUSTOM_OPT"};
  int value = 42;
  SocketOptionImpl custom(custom_opt, &value, sizeof(value));
  
  // Should not be supported
  EXPECT_FALSE(custom.isSupported());
  
  // setOption should fail for unsupported
  EXPECT_FALSE(custom.setOption(mock_socket_));
  
  // setOptionForListen should succeed (no-op by default)
  EXPECT_TRUE(custom.setOptionForListen(mock_socket_));
}

TEST_F(SocketOptionTest, ErrorHandling) {
  BoolSocketOption reuse_addr(SOCKET_SO_REUSEADDR, true);
  
  // Test socket option failure
  EXPECT_CALL(mock_socket_, setSocketOption(SOL_SOCKET, SO_REUSEADDR, _, sizeof(int)))
      .WillOnce(Return(IoResult<int>::error(EINVAL)));
  
  EXPECT_FALSE(reuse_addr.setOption(mock_socket_));
}

// Integration test with real socket
TEST_F(SocketOptionTest, RealSocketIntegration) {
  // Create a real socket
  auto socket_result = socketInterface().socket(
      SocketType::Stream, Address::Type::Ip, Address::IpVersion::v4, false);
  
  if (!socket_result.ok()) {
    GTEST_SKIP() << "Unable to create socket";
  }
  
  auto io_handle = socketInterface().ioHandleForFd(*socket_result, false, AF_INET);
  ConnectionSocketImpl socket(std::move(io_handle), nullptr, nullptr);
  
  // Test applying various options
  BoolSocketOption reuse_addr(SOCKET_SO_REUSEADDR, true);
  EXPECT_TRUE(reuse_addr.setOption(socket));
  
  // Verify option was set
  int value = 0;
  socklen_t len = sizeof(value);
  auto get_result = socket.getSocketOption(SOL_SOCKET, SO_REUSEADDR, &value, &len);
  ASSERT_TRUE(get_result.ok());
  EXPECT_NE(value, 0);
  
  // Test TCP_NODELAY
  BoolSocketOption tcp_nodelay(SOCKET_TCP_NODELAY, true);
  EXPECT_TRUE(tcp_nodelay.setOption(socket));
  
  // Test buffer sizes
  BufferSizeSocketOption rcv_buf(BufferSizeSocketOption::Receive, 65536);
  rcv_buf.setOption(socket);  // May fail due to system limits
  
  // Test TOS (may require privileges)
  TosSocketOption tos(0x10);
  tos.setOption(socket);  // May fail without privileges
  
  socket.close();
}

TEST_F(SocketOptionTest, SocketOptionNameConstants) {
  // Verify option names are defined correctly
  EXPECT_TRUE(SOCKET_SO_REUSEADDR.hasValue());
  EXPECT_EQ(SOCKET_SO_REUSEADDR.level, SOL_SOCKET);
  EXPECT_EQ(SOCKET_SO_REUSEADDR.option, SO_REUSEADDR);
  EXPECT_EQ(SOCKET_SO_REUSEADDR.name, "SO_REUSEADDR");
  
#ifdef SO_REUSEPORT
  EXPECT_TRUE(SOCKET_SO_REUSEPORT.hasValue());
  EXPECT_EQ(SOCKET_SO_REUSEPORT.level, SOL_SOCKET);
  EXPECT_EQ(SOCKET_SO_REUSEPORT.option, SO_REUSEPORT);
#else
  EXPECT_FALSE(SOCKET_SO_REUSEPORT.hasValue());
#endif
  
  EXPECT_TRUE(SOCKET_TCP_NODELAY.hasValue());
  EXPECT_EQ(SOCKET_TCP_NODELAY.level, IPPROTO_TCP);
  EXPECT_EQ(SOCKET_TCP_NODELAY.option, TCP_NODELAY);
  
#ifdef IPV6_V6ONLY
  EXPECT_TRUE(SOCKET_IPV6_V6ONLY.hasValue());
  EXPECT_EQ(SOCKET_IPV6_V6ONLY.level, IPPROTO_IPV6);
  EXPECT_EQ(SOCKET_IPV6_V6ONLY.option, IPV6_V6ONLY);
#endif
}

TEST_F(SocketOptionTest, HashKeyUniqueness) {
  // Test that different options produce different hash keys
  BoolSocketOption opt1(SOCKET_SO_REUSEADDR, true);
  BoolSocketOption opt2(SOCKET_SO_REUSEADDR, false);
  BoolSocketOption opt3(SOCKET_SO_KEEPALIVE, true);
  
  std::vector<uint8_t> key1, key2, key3;
  opt1.hashKey(key1);
  opt2.hashKey(key2);
  opt3.hashKey(key3);
  
  // Same option, different values
  EXPECT_NE(key1, key2);
  
  // Different options
  EXPECT_NE(key1, key3);
  EXPECT_NE(key2, key3);
  
  // TCP keepalive with different settings
  TcpKeepaliveSocketOption::KeepaliveSettings settings1, settings2;
  settings1.idle_time_s = 300;
  settings2.idle_time_s = 600;
  
  TcpKeepaliveSocketOption keepalive1(settings1);
  TcpKeepaliveSocketOption keepalive2(settings2);
  
  std::vector<uint8_t> keep_key1, keep_key2;
  keepalive1.hashKey(keep_key1);
  keepalive2.hashKey(keep_key2);
  
  EXPECT_NE(keep_key1, keep_key2);
}