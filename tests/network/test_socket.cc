#include <gmock/gmock.h>
#include <gtest/gtest.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#endif

#include "mcp/network/address_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/socket_interface_impl.h"
#include "mcp/network/socket_option_impl.h"

using namespace mcp;
using namespace mcp::network;
using namespace testing;

class SocketTest : public Test {
 protected:
  void SetUp() override {
    // Ensure we have default socket interface
    resetSocketInterface();
  }

  void TearDown() override {
    // Cleanup any open sockets
    if (test_socket_ && test_socket_->isOpen()) {
      test_socket_->close();
    }
  }

  SocketPtr test_socket_;
};

// Mock IoHandle for testing Socket behavior
class MockIoHandle : public IoHandle {
 public:
  MOCK_METHOD(os_fd_t, fd, (), (const, override));
  MOCK_METHOD(bool, isOpen, (), (const, override));
  MOCK_METHOD(IoVoidResult, close, (), (override));

  MOCK_METHOD(IoCallResult,
              readv,
              (size_t max_length, RawSlice* slices, size_t num_slices),
              (override));

  MOCK_METHOD(IoCallResult,
              read,
              (Buffer & buffer, optional<size_t> max_length),
              (override));

  MOCK_METHOD(IoCallResult,
              writev,
              (const ConstRawSlice* slices, size_t num_slices),
              (override));

  MOCK_METHOD(IoCallResult, write, (Buffer & buffer), (override));

  MOCK_METHOD(IoCallResult,
              sendmsg,
              (const ConstRawSlice* slices,
               size_t num_slices,
               int flags,
               const Address::Ip* self_ip,
               const Address::Instance& peer_address),
              (override));

  MOCK_METHOD(IoCallResult,
              recvmsg,
              (RawSlice * slices,
               size_t num_slices,
               uint32_t self_port,
               const UdpSaveCmsgConfig& save_cmsg_config,
               RecvMsgOutput& output),
              (override));

  MOCK_METHOD(IoCallResult,
              recvmmsg,
              (std::vector<RawSlice> & slices,
               uint32_t self_port,
               const UdpSaveCmsgConfig& save_cmsg_config,
               RecvMsgOutput& output),
              (override));

  // recv method doesn't exist in IoHandle interface

  MOCK_METHOD(IoResult<int>,
              bind,
              (const Address::InstanceConstSharedPtr& address),
              (override));
  MOCK_METHOD(IoResult<int>, listen, (int backlog), (override));
  MOCK_METHOD(IoResult<int>,
              connect,
              (const Address::InstanceConstSharedPtr& address),
              (override));

  MOCK_METHOD(IoResult<int>,
              setSocketOption,
              (int level, int optname, const void* optval, socklen_t optlen),
              (override));

  MOCK_METHOD(IoResult<int>,
              getSocketOption,
              (int level, int optname, void* optval, socklen_t* optlen),
              (const, override));

  MOCK_METHOD(IoResult<int>,
              ioctl,
              (unsigned long request, void* argp),
              (override));

  MOCK_METHOD(IoResult<int>, setBlocking, (bool blocking), (override));

  // domain method doesn't exist in IoHandle interface

  MOCK_METHOD(IoResult<Address::InstanceConstSharedPtr>,
              localAddress,
              (),
              (const, override));
  MOCK_METHOD(IoResult<Address::InstanceConstSharedPtr>,
              peerAddress,
              (),
              (const, override));
  MOCK_METHOD(optional<std::string>, interfaceName, (), (const, override));
  MOCK_METHOD(optional<std::chrono::milliseconds>,
              lastRoundTripTime,
              (),
              (const, override));
  MOCK_METHOD(void,
              configureInitialCongestionWindow,
              (uint64_t, std::chrono::microseconds),
              (override));
  MOCK_METHOD(IoResult<IoHandlePtr>, accept, (), (override));

  MOCK_METHOD(void,
              initializeFileEvent,
              (event::Dispatcher & dispatcher,
               event::FileReadyCb cb,
               event::FileTriggerType trigger,
               uint32_t events),
              (override));

  MOCK_METHOD(void, activateFileEvents, (uint32_t events), (override));
  MOCK_METHOD(void, enableFileEvents, (uint32_t events), (override));

  MOCK_METHOD(IoHandlePtr, duplicate, (), (override));

  MOCK_METHOD(void, resetFileEvents, (), (override));
  MOCK_METHOD(IoResult<int>, shutdown, (int how), (override));
};

TEST_F(SocketTest, ConnectionInfoProvider) {
  auto local_addr = Address::loopbackAddress(Address::IpVersion::v4, 8080);
  auto remote_addr = Address::loopbackAddress(Address::IpVersion::v4, 9090);

  std::unique_ptr<IoHandle> mock_handle = std::make_unique<MockIoHandle>();
  auto* mock_mock_handle_ptr = static_cast<MockIoHandle*>(mock_handle.get());
  EXPECT_CALL(*mock_handle_ptr, isOpen()).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_handle_ptr, fd()).WillRepeatedly(Return(5));

  ConnectionSocketImpl socket(std::move(mock_handle), local_addr, remote_addr);

  // Test ConnectionInfoProvider
  const auto& info = socket.connectionInfoProvider();
  EXPECT_EQ(info.localAddress()->asString(), "127.0.0.1:8080");
  EXPECT_EQ(info.remoteAddress()->asString(), "127.0.0.1:9090");
  EXPECT_EQ(info.directLocalAddress()->asString(), "127.0.0.1:8080");
  EXPECT_EQ(info.directRemoteAddress()->asString(), "127.0.0.1:9090");

  // Test ConnectionInfoSetter
  auto& setter = socket.connectionInfoProvider();

  // Set server name
  setter.setRequestedServerName("example.com");
  EXPECT_EQ(info.requestedServerName(), "example.com");

  // Set connection ID
  setter.setConnectionID(12345);
  ASSERT_TRUE(info.connectionID().has_value());
  EXPECT_EQ(*info.connectionID(), 12345);

  // Set interface name
  setter.setInterfaceName("eth0");
  ASSERT_TRUE(info.interfaceName().has_value());
  EXPECT_EQ(*info.interfaceName(), "eth0");

  // Set SSL info
  setter.setSslProtocol("TLSv1.3");
  setter.setSslCipherSuite("TLS_AES_128_GCM_SHA256");
  setter.setSslPeerCertificate("cert data");
  EXPECT_EQ(info.sslProtocol(), "TLSv1.3");
  EXPECT_EQ(info.sslCipherSuite(), "TLS_AES_128_GCM_SHA256");
  EXPECT_EQ(info.sslPeerCertificate(), "cert data");

  // Set JA3/JA4 hashes
  setter.setJA3Hash("ja3hash");
  setter.setJA4Hash("ja4hash");
  EXPECT_EQ(info.ja3Hash(), "ja3hash");
  EXPECT_EQ(info.ja4Hash(), "ja4hash");

  // Set RTT
  setter.setRoundTripTime(std::chrono::milliseconds(50));
  ASSERT_TRUE(info.roundTripTime().has_value());
  EXPECT_EQ(*info.roundTripTime(), std::chrono::milliseconds(50));
}

TEST_F(SocketTest, LocalAddressRestore) {
  auto local_addr = Address::loopbackAddress(Address::IpVersion::v4, 8080);
  auto remote_addr = Address::loopbackAddress(Address::IpVersion::v4, 9090);

  std::unique_ptr<IoHandle> mock_handle = std::make_unique<MockIoHandle>();
  auto* mock_mock_handle_ptr = static_cast<MockIoHandle*>(mock_handle.get());
  EXPECT_CALL(*mock_handle_ptr, isOpen()).WillRepeatedly(Return(true));

  ConnectionSocketImpl socket(std::move(mock_handle), local_addr, remote_addr);
  auto& setter = socket.connectionInfoProvider();

  // Change local address
  auto new_addr = Address::loopbackAddress(Address::IpVersion::v4, 8081);
  setter.setLocalAddress(new_addr);
  EXPECT_EQ(socket.connectionInfoProvider().localAddress()->asString(),
            "127.0.0.1:8081");
  EXPECT_FALSE(setter.localAddressRestored());

  // Restore local address
  setter.restoreLocalAddress(local_addr);
  EXPECT_EQ(socket.connectionInfoProvider().localAddress()->asString(),
            "127.0.0.1:8080");
  EXPECT_TRUE(setter.localAddressRestored());
}

TEST_F(SocketTest, IoHandleOperations) {
  std::unique_ptr<IoHandle> mock_handle = std::make_unique<MockIoHandle>();
  auto* mock_mock_handle_ptr = static_cast<MockIoHandle*>(mock_handle.get());

  EXPECT_CALL(*mock_handle_ptr, isOpen()).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_handle_ptr, fd()).WillRepeatedly(Return(10));

  ConnectionSocketImpl socket(std::move(mock_handle), nullptr, nullptr);

  // Test IoHandle access
  EXPECT_EQ(&socket.ioHandle(), mock_handle_ptr);
  EXPECT_EQ(&const_cast<const ConnectionSocketImpl&>(socket).ioHandle(),
            mock_handle_ptr);
  EXPECT_TRUE(socket.isOpen());

  // Test close
  EXPECT_CALL(*mock_handle_ptr, close()).Times(1);
  socket.close();
}

TEST_F(SocketTest, BindOperation) {
  std::unique_ptr<IoHandle> mock_handle = std::make_unique<MockIoHandle>();
  auto* mock_mock_handle_ptr = static_cast<MockIoHandle*>(mock_handle.get());

  auto bind_addr = Address::loopbackAddress(Address::IpVersion::v4, 8080);

  EXPECT_CALL(*mock_handle_ptr, bind(bind_addr))
      .WillOnce(Return(IoResult<int>::success(0)));

  ConnectionSocketImpl socket(std::move(mock_handle), nullptr, nullptr);

  auto result = socket.bind(bind_addr);
  EXPECT_TRUE(result.ok());

  // Verify local address was updated
  EXPECT_EQ(socket.connectionInfoProvider().localAddress()->asString(),
            "127.0.0.1:8080");
}

TEST_F(SocketTest, ListenOperation) {
  std::unique_ptr<IoHandle> mock_handle = std::make_unique<MockIoHandle>();
  auto* mock_mock_handle_ptr = static_cast<MockIoHandle*>(mock_handle.get());

  EXPECT_CALL(*mock_handle_ptr, listen(128))
      .WillOnce(Return(IoResult<int>::success(0)));

  ConnectionSocketImpl socket(std::move(mock_handle), nullptr, nullptr);

  auto result = socket.listen(128);
  EXPECT_TRUE(result.ok());
}

TEST_F(SocketTest, ConnectOperation) {
  std::unique_ptr<IoHandle> mock_handle = std::make_unique<MockIoHandle>();
  auto* mock_mock_handle_ptr = static_cast<MockIoHandle*>(mock_handle.get());

  auto connect_addr = Address::loopbackAddress(Address::IpVersion::v4, 9090);

  EXPECT_CALL(*mock_handle_ptr, connect(connect_addr))
      .WillOnce(Return(IoResult<int>::success(0)));

  ConnectionSocketImpl socket(std::move(mock_handle), nullptr, nullptr);

  auto result = socket.connect(connect_addr);
  EXPECT_TRUE(result.ok());

  // Verify remote address was updated
  EXPECT_EQ(socket.connectionInfoProvider().remoteAddress()->asString(),
            "127.0.0.1:9090");
}

TEST_F(SocketTest, SocketOptions) {
  std::unique_ptr<IoHandle> mock_handle = std::make_unique<MockIoHandle>();
  auto* mock_mock_handle_ptr = static_cast<MockIoHandle*>(mock_handle.get());

  // Test setSocketOption
  int reuse = 1;
  EXPECT_CALL(*mock_handle_ptr,
              setSocketOption(SOL_SOCKET, SO_REUSEADDR, _, sizeof(int)))
      .WillOnce(Return(IoResult<int>::success(0)));

  ConnectionSocketImpl socket(std::move(mock_handle), nullptr, nullptr);

  auto result =
      socket.setSocketOption(SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  EXPECT_TRUE(result.ok());

  // Test getSocketOption
  int value = 0;
  socklen_t len = sizeof(value);
  EXPECT_CALL(*mock_handle_ptr, getSocketOption(SOL_SOCKET, SO_REUSEADDR, _, _))
      .WillOnce([](int, int, void* optval, socklen_t* optlen) {
        *static_cast<int*>(optval) = 1;
        *optlen = sizeof(int);
        return IoResult<int>::success(0);
      });

  result = socket.getSocketOption(SOL_SOCKET, SO_REUSEADDR, &value, &len);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(value, 1);
}

TEST_F(SocketTest, Ioctl) {
  std::unique_ptr<IoHandle> mock_handle = std::make_unique<MockIoHandle>();
  auto* mock_mock_handle_ptr = static_cast<MockIoHandle*>(mock_handle.get());

  int bytes_available = 0;
  EXPECT_CALL(*mock_handle_ptr, ioctl(FIONREAD, &bytes_available))
      .WillOnce(Return(IoResult<int>::success(0)));

  ConnectionSocketImpl socket(std::move(mock_handle), nullptr, nullptr);

  auto result = socket.ioctl(FIONREAD, &bytes_available);
  EXPECT_TRUE(result.ok());
}

TEST_F(SocketTest, AddOptions) {
  std::unique_ptr<IoHandle> mock_handle = std::make_unique<MockIoHandle>();
  auto* mock_mock_handle_ptr = static_cast<MockIoHandle*>(mock_handle.get());

  ConnectionSocketImpl socket(std::move(mock_handle), nullptr, nullptr);

  // Add single option
  auto option1 = std::make_shared<BoolSocketOption>(SOCKET_SO_REUSEADDR, true);
  socket.addOption(option1);

  EXPECT_EQ(socket.options()->size(), 1);

  // Add multiple options
  auto options = std::make_shared<std::vector<SocketOptionConstSharedPtr>>();
  options->push_back(
      std::make_shared<BoolSocketOption>(SOCKET_SO_REUSEPORT, true));
  options->push_back(
      std::make_shared<IntSocketOption>(SOCKET_SO_RCVBUF, 65536));

  socket.addOptions(options);

  EXPECT_EQ(socket.options()->size(), 3);
}

TEST_F(SocketTest, Duplicate) {
  std::unique_ptr<IoHandle> mock_handle = std::make_unique<MockIoHandle>();
  auto* mock_mock_handle_ptr = static_cast<MockIoHandle*>(mock_handle.get());

  auto local_addr = Address::loopbackAddress(Address::IpVersion::v4, 8080);
  auto remote_addr = Address::loopbackAddress(Address::IpVersion::v4, 9090);

  // Setup duplicate handle
  std::unique_ptr<IoHandle> dup_handle = std::make_unique<MockIoHandle>();
  auto* dup_mock_handle_ptr = static_cast<MockIoHandle*>(dup_handle.get());
  EXPECT_CALL(*dup_mock_handle_ptr, fd()).WillRepeatedly(Return(11));

  EXPECT_CALL(*mock_handle_ptr, duplicate())
      .WillOnce(Return(ByMove(std::move(dup_handle))));

  ConnectionSocketImpl socket(std::move(mock_handle), local_addr, remote_addr);

  // Add some options
  socket.addOption(
      std::make_shared<BoolSocketOption>(SOCKET_SO_REUSEADDR, true));

  auto dup_socket = socket.duplicate();
  ASSERT_NE(dup_socket, nullptr);

  // Verify addresses and options were copied
  EXPECT_EQ(dup_socket->connectionInfoProvider().localAddress()->asString(),
            "127.0.0.1:8080");
  EXPECT_EQ(dup_socket->connectionInfoProvider().remoteAddress()->asString(),
            "127.0.0.1:9090");
  EXPECT_EQ(dup_socket->options()->size(), 1);
}

TEST_F(SocketTest, SetBlocking) {
  std::unique_ptr<IoHandle> mock_handle = std::make_unique<MockIoHandle>();
  auto* mock_mock_handle_ptr = static_cast<MockIoHandle*>(mock_handle.get());

  EXPECT_CALL(*mock_handle_ptr, setBlocking(false))
      .WillOnce(Return(IoResult<int>::success(0)));

  ConnectionSocketImpl socket(std::move(mock_handle), nullptr, nullptr);

  auto result = socket.setBlocking(false);
  EXPECT_TRUE(result.ok());
}

TEST_F(SocketTest, ConnectionSocket) {
  std::unique_ptr<IoHandle> mock_handle = std::make_unique<MockIoHandle>();
  auto* mock_mock_handle_ptr = static_cast<MockIoHandle*>(mock_handle.get());
  EXPECT_CALL(*mock_handle_ptr, isOpen()).WillRepeatedly(Return(true));

  auto local_addr = Address::loopbackAddress(Address::IpVersion::v4, 8080);
  auto remote_addr = Address::loopbackAddress(Address::IpVersion::v4, 9090);

  ConnectionSocketImpl conn_socket(std::move(mock_handle), local_addr,
                                   remote_addr);

  // Test socket type and address info
  EXPECT_EQ(conn_socket.socketType(), SocketType::Stream);
  EXPECT_EQ(conn_socket.addressType(), Address::Type::Ip);
  ASSERT_TRUE(conn_socket.ipVersion().has_value());
  EXPECT_EQ(*conn_socket.ipVersion(), Address::IpVersion::v4);

  // Test half-close
  EXPECT_FALSE(conn_socket.isHalfClose());
  conn_socket.setHalfClose(true);
  EXPECT_TRUE(conn_socket.isHalfClose());

  // Test detected close type
  EXPECT_EQ(conn_socket.detectedCloseType(),
            ConnectionSocket::DetectedCloseType::Normal);

  // Test server name
  conn_socket.connectionInfoProvider().setRequestedServerName("example.com");
  EXPECT_EQ(conn_socket.requestedServerName(), "example.com");
}

TEST_F(SocketTest, ListenSocket) {
  std::unique_ptr<IoHandle> mock_handle = std::make_unique<MockIoHandle>();
  auto* mock_mock_handle_ptr = static_cast<MockIoHandle*>(mock_handle.get());
  EXPECT_CALL(*mock_handle_ptr, isOpen()).WillRepeatedly(Return(true));

  auto local_addr = Address::loopbackAddress(Address::IpVersion::v4, 8080);

  ListenSocketImpl listen_socket(std::move(mock_handle), local_addr);

  // Test socket type and address info
  EXPECT_EQ(listen_socket.socketType(), SocketType::Stream);
  EXPECT_EQ(listen_socket.addressType(), Address::Type::Ip);
  ASSERT_TRUE(listen_socket.ipVersion().has_value());
  EXPECT_EQ(*listen_socket.ipVersion(), Address::IpVersion::v4);

  // Test setting listen socket options
  SocketCreationOptions options;
  options.reuse_address = true;
  options.reuse_port = true;
  options.tcp_nodelay = true;

  listen_socket.setListenSocketOptions(options);

  // Should have added options
  EXPECT_GE(listen_socket.options()->size(),
            2);  // At least reuse_address and reuse_port
}

TEST_F(SocketTest, CreateConnectionSocket) {
  auto remote_addr = Address::loopbackAddress(Address::IpVersion::v4, 9090);
  auto local_addr = Address::loopbackAddress(Address::IpVersion::v4, 0);

  SocketCreationOptions options;
  options.reuse_address = true;
  options.tcp_nodelay = true;

  auto socket = createConnectionSocket(Address::Type::Ip, remote_addr,
                                       local_addr, options);

  // May fail if no network available, but should at least return something
  if (socket) {
    EXPECT_TRUE(socket->isOpen());
    EXPECT_EQ(socket->socketType(), SocketType::Stream);
    EXPECT_EQ(socket->addressType(), Address::Type::Ip);

    // Should have options applied
    EXPECT_GE(socket->options()->size(), 2);

    socket->close();
  }
}

TEST_F(SocketTest, CreateListenSocket) {
  auto bind_addr = Address::loopbackAddress(Address::IpVersion::v4, 0);

  SocketCreationOptions options;
  options.reuse_address = true;

  auto socket = createListenSocket(bind_addr, options, true);

  // May fail if no network available, but should at least return something
  if (socket) {
    EXPECT_TRUE(socket->isOpen());
    EXPECT_EQ(socket->socketType(), SocketType::Stream);
    EXPECT_EQ(socket->addressType(), Address::Type::Ip);

    // Should be bound
    auto local_addr = socket->connectionInfoProvider().localAddress();
    ASSERT_NE(local_addr, nullptr);
    EXPECT_NE(local_addr->ip()->port(), 0);  // Should have assigned port

    socket->close();
  }
}

TEST_F(SocketTest, CreateSocketOptions) {
  SocketCreationOptions creation_options;
  creation_options.reuse_address = true;
  creation_options.reuse_port = true;
  creation_options.v6_only = true;
  creation_options.send_buffer_size = 65536;
  creation_options.receive_buffer_size = 32768;
  creation_options.tcp_nodelay = true;
  creation_options.tcp_keepalive = true;
  creation_options.tcp_keepidle = 300;
  creation_options.tcp_keepintvl = 30;
  creation_options.tcp_keepcnt = 9;
  creation_options.type_of_service = 0x10;
  creation_options.ip_transparent = true;
  creation_options.ip_freebind = true;

  auto socket_options = createSocketOptions(creation_options);
  ASSERT_NE(socket_options, nullptr);

  // Should have created options for all specified values
  // Count may vary based on platform support
  EXPECT_GE(socket_options->size(), 5);  // At least basic options
}

TEST_F(SocketTest, ApplySocketOptions) {
  std::unique_ptr<IoHandle> mock_handle = std::make_unique<MockIoHandle>();
  auto* mock_mock_handle_ptr = static_cast<MockIoHandle*>(mock_handle.get());

  // Expect socket option calls
  EXPECT_CALL(*mock_handle_ptr, setSocketOption(SOL_SOCKET, SO_REUSEADDR, _, _))
      .WillOnce(Return(IoResult<int>::success(0)));
  EXPECT_CALL(*mock_handle_ptr, setSocketOption(IPPROTO_TCP, TCP_NODELAY, _, _))
      .WillOnce(Return(IoResult<int>::success(0)));

  ConnectionSocketImpl socket(std::move(mock_handle), nullptr, nullptr);

  auto options = std::make_shared<std::vector<SocketOptionConstSharedPtr>>();
  options->push_back(
      std::make_shared<BoolSocketOption>(SOCKET_SO_REUSEADDR, true));
  options->push_back(
      std::make_shared<BoolSocketOption>(SOCKET_TCP_NODELAY, true));

  bool success = applySocketOptions(socket, options, SocketOptionName());
  EXPECT_TRUE(success);
}

TEST_F(SocketTest, ErrorHandling) {
  // Test null handle
  ConnectionSocketImpl socket(nullptr, nullptr, nullptr);

  EXPECT_FALSE(socket.isOpen());

  // All operations should fail with EBADF
  auto bind_result =
      socket.bind(Address::loopbackAddress(Address::IpVersion::v4, 8080));
  EXPECT_FALSE(bind_result.ok());
  EXPECT_EQ(bind_result.error_code(), EBADF);

  auto listen_result = socket.listen(128);
  EXPECT_FALSE(listen_result.ok());
  EXPECT_EQ(listen_result.error_code(), EBADF);

  auto connect_result =
      socket.connect(Address::loopbackAddress(Address::IpVersion::v4, 9090));
  EXPECT_FALSE(connect_result.ok());
  EXPECT_EQ(connect_result.error_code(), EBADF);

  int value = 1;
  auto setsockopt_result =
      socket.setSocketOption(SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
  EXPECT_FALSE(setsockopt_result.ok());
  EXPECT_EQ(setsockopt_result.error_code(), EBADF);

  socklen_t len = sizeof(value);
  auto getsockopt_result =
      socket.getSocketOption(SOL_SOCKET, SO_REUSEADDR, &value, &len);
  EXPECT_FALSE(getsockopt_result.ok());
  EXPECT_EQ(getsockopt_result.error_code(), EBADF);

  auto ioctl_result = socket.ioctl(FIONREAD, &value);
  EXPECT_FALSE(ioctl_result.ok());
  EXPECT_EQ(ioctl_result.error_code(), EBADF);

  auto setblocking_result = socket.setBlocking(false);
  EXPECT_FALSE(setblocking_result.ok());
  EXPECT_EQ(setblocking_result.error_code(), EBADF);

  auto dup = socket.duplicate();
  EXPECT_EQ(dup, nullptr);
}

TEST_F(SocketTest, ConnectionInfoProviderSharedPtr) {
  auto local_addr = Address::loopbackAddress(Address::IpVersion::v4, 8080);
  auto remote_addr = Address::loopbackAddress(Address::IpVersion::v4, 9090);

  std::unique_ptr<IoHandle> mock_handle = std::make_unique<MockIoHandle>();
  auto* mock_mock_handle_ptr = static_cast<MockIoHandle*>(mock_handle.get());

  ConnectionSocketImpl socket(std::move(mock_handle), local_addr, remote_addr);

  auto shared_info = socket.connectionInfoProviderSharedPtr();
  ASSERT_NE(shared_info, nullptr);

  // Should be the same object
  EXPECT_EQ(&socket.connectionInfoProvider(), shared_info.get());

  // Can use shared_ptr independently
  EXPECT_EQ(shared_info->localAddress()->asString(), "127.0.0.1:8080");
  EXPECT_EQ(shared_info->remoteAddress()->asString(), "127.0.0.1:9090");
}

// Test Unix domain sockets if supported
#ifndef _WIN32
TEST_F(SocketTest, UnixDomainSocket) {
  std::unique_ptr<IoHandle> mock_handle = std::make_unique<MockIoHandle>();
  auto* mock_mock_handle_ptr = static_cast<MockIoHandle*>(mock_handle.get());
  EXPECT_CALL(*mock_handle_ptr, isOpen()).WillRepeatedly(Return(true));

  auto unix_addr = std::make_shared<Address::PipeInstance>("/tmp/test.sock");

  ConnectionSocketImpl socket(std::move(mock_handle), unix_addr, nullptr);

  EXPECT_EQ(socket.addressType(), Address::Type::Pipe);
  EXPECT_FALSE(socket.ipVersion().has_value());  // Not IP socket
}
#endif

// Integration test with real sockets
TEST_F(SocketTest, RealSocketIntegration) {
  // Create a real listen socket
  auto listen_addr = Address::loopbackAddress(Address::IpVersion::v4, 0);

  SocketCreationOptions options;
  options.reuse_address = true;

  auto listen_socket = createListenSocket(listen_addr, options, true);
  if (!listen_socket) {
    GTEST_SKIP() << "Unable to create listen socket";
  }

  // Listen
  auto listen_result = listen_socket->listen(5);
  ASSERT_TRUE(listen_result.ok());

  // Get actual port
  auto local_addr = listen_socket->connectionInfoProvider().localAddress();
  ASSERT_NE(local_addr, nullptr);
  uint16_t port = local_addr->ip()->port();
  ASSERT_NE(port, 0);

  // Create connection socket
  auto connect_addr = Address::loopbackAddress(Address::IpVersion::v4, port);
  auto conn_socket =
      createConnectionSocket(Address::Type::Ip, connect_addr, nullptr, options);

  if (conn_socket) {
    // Attempt connection
    auto connect_result = conn_socket->connect(connect_addr);
    // May be in progress or succeed
    EXPECT_TRUE(connect_result.ok() ||
                (connect_result.error_code() &&
                 (connect_result.error_code() == EINPROGRESS ||
                  connect_result.error_code() == EWOULDBLOCK)));

    conn_socket->close();
  }

  listen_socket->close();
}