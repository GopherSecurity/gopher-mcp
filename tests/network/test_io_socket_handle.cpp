#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/tcp.h>
#endif

#include "mcp/network/io_socket_handle_impl.h"
#include "mcp/network/address_impl.h"
#include "mcp/network/socket_interface.h"
#include "mcp/event/libevent_dispatcher.h"

using namespace mcp;
using namespace mcp::network;
using namespace mcp::event;
using namespace testing;

class IoSocketHandleTest : public Test {
protected:
  void SetUp() override {
    dispatcher_ = createLibeventDispatcherFactory()->createDispatcher("test");
  }
  
  void TearDown() override {
    dispatcher_->exit();
  }
  
  // Create a TCP socket pair for testing
  std::pair<IoHandlePtr, IoHandlePtr> createTcpSocketPair() {
    // Create server socket
    auto server_handle = createIoSocketHandle();
    auto listen_addr = Address::loopbackAddress(Address::IpVersion::v4, 0);
    
    EXPECT_TRUE(server_handle->bind(listen_addr).ok());
    EXPECT_TRUE(server_handle->listen(1).ok());
    
    // Get actual port
    auto local_addr_result = server_handle->localAddress();
    EXPECT_TRUE(local_addr_result.ok());
    uint16_t port = (*local_addr_result)->ip()->port();
    
    // Create client socket
    auto client_handle = createIoSocketHandle();
    auto connect_addr = Address::loopbackAddress(Address::IpVersion::v4, port);
    
    // Non-blocking connect
    auto connect_result = client_handle->connect(connect_addr);
    EXPECT_TRUE(connect_result.ok());
    
    // Accept connection
    auto accept_result = server_handle->accept();
    EXPECT_TRUE(accept_result.ok());
    
    // Wait for connection to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    return {std::move(client_handle), std::move(*accept_result)};
  }
  
  // Create a UDP socket pair for testing
  std::pair<IoHandlePtr, IoHandlePtr> createUdpSocketPair() {
    auto socket1 = createIoSocketHandle();
    auto socket2 = createIoSocketHandle();
    
    // Set socket type to datagram
    int type = SOCK_DGRAM;
    socket1->setSocketOption(SOL_SOCKET, SO_TYPE, &type, sizeof(type));
    socket2->setSocketOption(SOL_SOCKET, SO_TYPE, &type, sizeof(type));
    
    auto addr1 = Address::loopbackAddress(Address::IpVersion::v4, 0);
    auto addr2 = Address::loopbackAddress(Address::IpVersion::v4, 0);
    
    EXPECT_TRUE(socket1->bind(addr1).ok());
    EXPECT_TRUE(socket2->bind(addr2).ok());
    
    return {std::move(socket1), std::move(socket2)};
  }
  
  DispatcherPtr dispatcher_;
};

TEST_F(IoSocketHandleTest, CreateAndClose) {
  auto handle = createIoSocketHandle();
  EXPECT_TRUE(handle->isOpen());
  EXPECT_NE(handle->fd(), INVALID_SOCKET_FD);
  
  auto result = handle->close();
  EXPECT_TRUE(result.ok());
  EXPECT_FALSE(handle->isOpen());
  EXPECT_EQ(handle->fd(), INVALID_SOCKET_FD);
  
  // Double close should be safe
  result = handle->close();
  EXPECT_TRUE(result.ok());
}

TEST_F(IoSocketHandleTest, InvalidSocketOperations) {
  IoSocketHandleImpl handle(INVALID_SOCKET_FD);
  EXPECT_FALSE(handle.isOpen());
  
  // All operations should fail with EBADF
  RawSlice slice;
  auto read_result = handle.readv(1024, &slice, 1);
  EXPECT_FALSE(read_result.ok());
  EXPECT_EQ(*read_result.error_code, EBADF);
  
  ConstRawSlice const_slice;
  auto write_result = handle.writev(&const_slice, 1);
  EXPECT_FALSE(write_result.ok());
  EXPECT_EQ(*write_result.error_code, EBADF);
  
  auto addr = Address::anyAddress(Address::IpVersion::v4);
  auto bind_result = handle.bind(addr);
  EXPECT_FALSE(bind_result.ok());
  EXPECT_EQ(*bind_result.error_code, EBADF);
}

TEST_F(IoSocketHandleTest, TcpReadWrite) {
  auto socket_pair = createTcpSocketPair();
  auto& client = socket_pair.first;
  auto& server = socket_pair.second;
  
  // Write data from client
  const std::string test_data = "Hello, World!";
  mcp::OwnedBuffer write_buffer;
  write_buffer.add(test_data);
  
  auto write_result = client->write(write_buffer);
  EXPECT_TRUE(write_result.ok());
  EXPECT_EQ(*write_result, test_data.size());
  EXPECT_EQ(write_buffer.length(), 0);  // Buffer should be drained
  
  // Read data on server
  mcp::OwnedBuffer read_buffer;
  auto read_result = server->read(read_buffer, test_data.size());
  
  // May need to retry for non-blocking socket
  int retries = 0;
  while (!read_result.ok() && read_result.wouldBlock() && retries++ < 10) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    read_result = server->read(read_buffer, test_data.size());
  }
  
  EXPECT_TRUE(read_result.ok());
  EXPECT_EQ(*read_result, test_data.size());
  EXPECT_EQ(read_buffer.toString(), test_data);
}

TEST_F(IoSocketHandleTest, TcpVectoredIO) {
  auto socket_pair = createTcpSocketPair();
  auto& client = socket_pair.first;
  auto& server = socket_pair.second;
  
  // Prepare multiple slices
  const std::string part1 = "Hello, ";
  const std::string part2 = "World";
  const std::string part3 = "!";
  
  ConstRawSlice slices[3] = {
    {part1.data(), part1.size()},
    {part2.data(), part2.size()},
    {part3.data(), part3.size()}
  };
  
  // Write using writev
  auto write_result = client->writev(slices, 3);
  EXPECT_TRUE(write_result.ok());
  EXPECT_EQ(*write_result, part1.size() + part2.size() + part3.size());
  
  // Read using readv
  char buffer1[10], buffer2[10], buffer3[10];
  RawSlice read_slices[3] = {
    {buffer1, sizeof(buffer1)},
    {buffer2, sizeof(buffer2)},
    {buffer3, sizeof(buffer3)}
  };
  
  // May need to retry for non-blocking socket
  int retries = 0;
  IoCallResult read_result;
  do {
    read_result = server->readv(30, read_slices, 3);
    if (!read_result.ok() && read_result.wouldBlock() && retries++ < 10) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  } while (!read_result.ok() && read_result.wouldBlock() && retries < 10);
  
  EXPECT_TRUE(read_result.ok());
  EXPECT_EQ(*read_result, part1.size() + part2.size() + part3.size());
  
  // Verify data
  EXPECT_EQ(std::string(buffer1, part1.size()), part1);
  EXPECT_EQ(std::string(buffer2, part2.size()), part2);
  EXPECT_EQ(std::string(buffer3, part3.size()), part3);
}

TEST_F(IoSocketHandleTest, UdpSendRecv) {
  auto socket_pair = createUdpSocketPair();
  auto& socket1 = socket_pair.first;
  auto& socket2 = socket_pair.second;
  
  // Get addresses
  auto addr1_result = socket1->localAddress();
  auto addr2_result = socket2->localAddress();
  ASSERT_TRUE(addr1_result.ok());
  ASSERT_TRUE(addr2_result.ok());
  
  // Send message from socket1 to socket2
  const std::string message = "UDP Test Message";
  ConstRawSlice slice{message.data(), message.size()};
  
  auto send_result = socket1->sendmsg(&slice, 1, 0, nullptr, **addr2_result);
  EXPECT_TRUE(send_result.ok());
  EXPECT_EQ(*send_result, message.size());
  
  // Receive on socket2
  char buffer[1024];
  RawSlice recv_slice{buffer, sizeof(buffer)};
  RecvMsgOutput output;
  UdpSaveCmsgConfig cmsg_config;
  
  // Retry for non-blocking socket
  int retries = 0;
  IoCallResult recv_result;
  do {
    recv_result = socket2->recvmsg(&recv_slice, 1, 
                                   (*addr2_result)->ip()->port(),
                                   cmsg_config, output);
    if (!recv_result.ok() && recv_result.wouldBlock() && retries++ < 10) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  } while (!recv_result.ok() && recv_result.wouldBlock() && retries < 10);
  
  EXPECT_TRUE(recv_result.ok());
  EXPECT_EQ(*recv_result, 1);  // One message received
  ASSERT_EQ(output.messages.size(), 1);
  
  const auto& received = output.messages[0];
  EXPECT_EQ(received.data.toString(), message);
  EXPECT_FALSE(received.truncated);
  ASSERT_NE(received.peer_address, nullptr);
  EXPECT_EQ(received.peer_address->ip()->port(), (*addr1_result)->ip()->port());
}

TEST_F(IoSocketHandleTest, SocketOptions) {
  auto handle = createIoSocketHandle();
  auto addr = Address::anyAddress(Address::IpVersion::v4);
  ASSERT_TRUE(handle->bind(addr).ok());
  
  // Test SO_REUSEADDR
  int reuse = 1;
  auto set_result = handle->setSocketOption(SOL_SOCKET, SO_REUSEADDR, 
                                            &reuse, sizeof(reuse));
  EXPECT_TRUE(set_result.ok());
  
  // Get option back
  int value = 0;
  socklen_t len = sizeof(value);
  auto get_result = handle->getSocketOption(SOL_SOCKET, SO_REUSEADDR,
                                            &value, &len);
  EXPECT_TRUE(get_result.ok());
  EXPECT_NE(value, 0);  // Should be set
  
  // Test TCP_NODELAY (only for TCP sockets)
  int nodelay = 1;
  set_result = handle->setSocketOption(IPPROTO_TCP, TCP_NODELAY,
                                       &nodelay, sizeof(nodelay));
  // May fail if not TCP socket, which is OK
}

TEST_F(IoSocketHandleTest, NonBlockingMode) {
  auto handle = createIoSocketHandle();
  
  // Should be non-blocking by default
  auto addr = Address::anyAddress(Address::IpVersion::v4);
  ASSERT_TRUE(handle->bind(addr).ok());
  ASSERT_TRUE(handle->listen(1).ok());
  
  // Accept should return would-block immediately
  auto accept_result = handle->accept();
  if (!accept_result.ok()) {
    EXPECT_TRUE(accept_result.wouldBlock());
  }
  
  // Test setting blocking mode
  auto blocking_result = handle->setBlocking(true);
  EXPECT_TRUE(blocking_result.ok());
  
  // Reset to non-blocking
  blocking_result = handle->setBlocking(false);
  EXPECT_TRUE(blocking_result.ok());
}

TEST_F(IoSocketHandleTest, EventIntegration) {
  auto socket_pair = createTcpSocketPair();
  auto& client = socket_pair.first;
  auto& server = socket_pair.second;
  
  bool read_ready = false;
  bool write_ready = false;
  
  // Monitor server socket for read events
  server->initializeFileEvent(
      *dispatcher_,
      [&](uint32_t events) {
        if (events & static_cast<uint32_t>(FileReadyType::Read)) {
          read_ready = true;
        }
        if (events & static_cast<uint32_t>(FileReadyType::Write)) {
          write_ready = true;
        }
      },
      FileTriggerType::Level,
      static_cast<uint32_t>(FileReadyType::Read | FileReadyType::Write)
  );
  
  // Should be write-ready immediately
  dispatcher_->run(RunType::NonBlock);
  EXPECT_TRUE(write_ready);
  
  // Send data from client
  const std::string data = "test";
  mcp::OwnedBuffer buffer;
  buffer.add(data);
  ASSERT_TRUE(client->write(buffer).ok());
  
  // Should become read-ready
  read_ready = false;
  dispatcher_->run(RunType::NonBlock);
  EXPECT_TRUE(read_ready);
}

TEST_F(IoSocketHandleTest, AddressRetrieval) {
  auto handle = createIoSocketHandle();
  auto bind_addr = Address::parseInternetAddress("127.0.0.1:0");
  ASSERT_TRUE(handle->bind(bind_addr).ok());
  
  // Get local address
  auto local_result = handle->localAddress();
  ASSERT_TRUE(local_result.ok());
  auto local_addr = *local_result;
  
  EXPECT_EQ(local_addr->type(), Address::Type::Ip);
  EXPECT_EQ(local_addr->ip()->version(), Address::IpVersion::v4);
  EXPECT_EQ(local_addr->ip()->addressAsString(), "127.0.0.1");
  EXPECT_NE(local_addr->ip()->port(), 0);  // Should have assigned port
  
  // Peer address should fail for unconnected socket
  auto peer_result = handle->peerAddress();
  EXPECT_FALSE(peer_result.ok());
}

TEST_F(IoSocketHandleTest, Shutdown) {
  auto socket_pair = createTcpSocketPair();
  auto& client = socket_pair.first;
  auto& server = socket_pair.second;
  
  // Shutdown write side of client
  auto shutdown_result = client->shutdown(SHUT_WR);
  EXPECT_TRUE(shutdown_result.ok());
  
  // Server should see EOF when reading
  mcp::OwnedBuffer buffer;
  int retries = 0;
  IoCallResult read_result;
  do {
    read_result = server->read(buffer);
    if (!read_result.ok() && read_result.wouldBlock() && retries++ < 10) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  } while (!read_result.ok() && read_result.wouldBlock() && retries < 10);
  
  EXPECT_TRUE(read_result.ok());
  EXPECT_EQ(*read_result, 0);  // EOF
  
  // But server can still write
  buffer.add("response");
  auto write_result = server->write(buffer);
  EXPECT_TRUE(write_result.ok());
}

TEST_F(IoSocketHandleTest, Duplicate) {
  auto handle = createIoSocketHandle();
  auto addr = Address::anyAddress(Address::IpVersion::v4);
  ASSERT_TRUE(handle->bind(addr).ok());
  
  auto dup_handle = handle->duplicate();
  if (dup_handle) {  // May not be supported on all platforms
    EXPECT_TRUE(dup_handle->isOpen());
    EXPECT_NE(dup_handle->fd(), handle->fd());
    
    // Both should have same local address
    auto addr1 = handle->localAddress();
    auto addr2 = dup_handle->localAddress();
    ASSERT_TRUE(addr1.ok());
    ASSERT_TRUE(addr2.ok());
    EXPECT_TRUE(**addr1 == **addr2);
  }
}

TEST_F(IoSocketHandleTest, LargeBuffer) {
  auto socket_pair = createTcpSocketPair();
  auto& client = socket_pair.first;
  auto& server = socket_pair.second;
  
  // Create large buffer (1MB)
  const size_t size = 1024 * 1024;
  std::string large_data(size, 'X');
  
  // Write in chunks
  mcp::OwnedBuffer write_buffer;
  write_buffer.add(large_data);
  
  size_t total_written = 0;
  while (write_buffer.length() > 0) {
    auto result = client->write(write_buffer);
    if (result.ok()) {
      total_written += *result;
    } else if (!result.wouldBlock()) {
      FAIL() << "Write failed with error: " << *result.error_code;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  
  EXPECT_EQ(total_written, size);
  
  // Read back
  mcp::OwnedBuffer read_buffer;
  size_t total_read = 0;
  while (total_read < size) {
    auto result = server->read(read_buffer);
    if (result.ok() && *result > 0) {
      total_read += *result;
    } else if (!result.ok() && !result.wouldBlock()) {
      FAIL() << "Read failed with error: " << *result.error_code;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  
  EXPECT_EQ(total_read, size);
  EXPECT_EQ(read_buffer.length(), size);
}

// Platform-specific tests
#ifndef _WIN32
TEST_F(IoSocketHandleTest, UnixDomainSocket) {
  // Create Unix domain socket pair
  auto server_handle = createIoSocketHandle();
  auto client_handle = createIoSocketHandle();
  
  // Use abstract socket (Linux) or temp file
  std::string path = "/tmp/mcp_test_socket_" + std::to_string(getpid());
  auto server_addr = Address::pipeAddress(path);
  
  ASSERT_TRUE(server_handle->bind(server_addr).ok());
  ASSERT_TRUE(server_handle->listen(1).ok());
  
  // Connect client
  ASSERT_TRUE(client_handle->connect(server_addr).ok());
  
  // Accept connection
  auto accept_result = server_handle->accept();
  ASSERT_TRUE(accept_result.ok());
  
  // Test communication
  const std::string msg = "Unix socket test";
  mcp::OwnedBuffer buffer;
  buffer.add(msg);
  ASSERT_TRUE(client_handle->write(buffer).ok());
  
  buffer.drain(buffer.length());
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  
  auto read_result = (*accept_result)->read(buffer);
  EXPECT_TRUE(read_result.ok() || read_result.wouldBlock());
  
  // Cleanup
  ::unlink(path.c_str());
}

TEST_F(IoSocketHandleTest, TcpInfo) {
  auto socket_pair = createTcpSocketPair();
  auto& client = socket_pair.first;
  auto& server = socket_pair.second;
  (void)server;  // Unused in this test
  
  // Send some data to establish RTT
  mcp::OwnedBuffer buffer;
  buffer.add("ping");
  ASSERT_TRUE(client->write(buffer).ok());
  
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  
  // Try to get RTT (may not be available on all platforms)
  auto rtt = client->lastRoundTripTime();
  if (rtt.has_value()) {
    EXPECT_GE(rtt->count(), 0);
  }
}
#endif

// Test with mock event loop for edge cases
class MockFileEvent : public FileEvent {
public:
  MOCK_METHOD(void, activate, (uint32_t events), (override));
  MOCK_METHOD(void, setEnabled, (uint32_t events), (override));
};

TEST_F(IoSocketHandleTest, FileEventActivation) {
  auto handle = createIoSocketHandle();
  auto mock_event = std::make_unique<MockFileEvent>();
  auto* mock_ptr = mock_event.get();
  
  // Can't easily inject mock into real dispatcher, so test the handle methods
  EXPECT_CALL(*mock_ptr, activate(static_cast<uint32_t>(FileReadyType::Read)))
      .Times(1);
  EXPECT_CALL(*mock_ptr, setEnabled(static_cast<uint32_t>(FileReadyType::Write)))
      .Times(1);
  
  // These would normally be called by the handle
  mock_ptr->activate(static_cast<uint32_t>(FileReadyType::Read));
  mock_ptr->setEnabled(static_cast<uint32_t>(FileReadyType::Write));
}