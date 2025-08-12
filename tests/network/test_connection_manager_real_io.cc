#include <gtest/gtest.h>
#include "mcp/network/socket_interface.h"
#include "mcp/network/address.h"
#include "mcp/buffer.h"
#include "../integration/real_io_test_base.h"
#include <memory>
#include <vector>
#include <atomic>
#include <signal.h>

namespace mcp {
namespace network {
namespace {

// Global test environment to handle SIGPIPE
class SigPipeEnvironment : public ::testing::Environment {
 public:
  void SetUp() override {
    // Ignore SIGPIPE signals - writing to closed sockets will return EPIPE error instead
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
  }
};

// Register the environment - this will be called by gtest automatically
static ::testing::Environment* const sigpipe_env = 
    ::testing::AddGlobalTestEnvironment(new SigPipeEnvironment);

/**
 * Simple real IO tests for network components.
 * These tests verify basic socket operations with real IO.
 */
class NetworkRealIoTest : public mcp::test::RealIoTestBase {
protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
  }
  
  void TearDown() override {
    RealIoTestBase::TearDown();
  }
};

// Test basic socket creation and binding (previously DISABLED)
TEST_F(NetworkRealIoTest, BasicSocketOperations) {
  executeInDispatcher([&]() {
    auto& socket_interface = socketInterface();
    
    // Create a socket
    auto socket_fd = socket_interface.socket(
        SocketType::Stream,
        Address::Type::Ip,
        Address::IpVersion::v4);
    ASSERT_TRUE(socket_fd.ok());
    
    // Create IoHandle
    auto io_handle = socket_interface.ioHandleForFd(*socket_fd, false);
    ASSERT_NE(nullptr, io_handle);
    
    // Bind to ephemeral port
    auto bind_addr = Address::parseInternetAddress("127.0.0.1", 0);
    auto bind_result = io_handle->bind(bind_addr);
    ASSERT_TRUE(bind_result.ok());
    
    // Get local address
    auto local_addr_result = io_handle->localAddress();
    ASSERT_TRUE(local_addr_result.ok());
    auto local_addr = *local_addr_result;
    ASSERT_NE(nullptr, local_addr);
    
    // Verify it's an IP address
    auto ip_addr = dynamic_cast<const Address::Ip*>(local_addr.get());
    ASSERT_NE(nullptr, ip_addr);
    EXPECT_GT(ip_addr->port(), 0);
    
    // Clean up
    io_handle->close();
  });
}

// Test listener operations (previously DISABLED)
TEST_F(NetworkRealIoTest, ListenerOperations) {
  executeInDispatcher([&]() {
    auto& socket_interface = socketInterface();
    
    // Create listener socket
    auto listen_fd = socket_interface.socket(
        SocketType::Stream,
        Address::Type::Ip,
        Address::IpVersion::v4);
    ASSERT_TRUE(listen_fd.ok());
    
    auto listen_handle = socket_interface.ioHandleForFd(*listen_fd, false);
    
    // Bind
    auto bind_addr = Address::parseInternetAddress("127.0.0.1", 0);
    auto bind_result = listen_handle->bind(bind_addr);
    ASSERT_TRUE(bind_result.ok());
    
    // Listen
    auto listen_result = listen_handle->listen(10);
    ASSERT_TRUE(listen_result.ok());
    
    // Get actual port
    auto local_addr_result = listen_handle->localAddress();
    ASSERT_TRUE(local_addr_result.ok());
    auto local_addr = *local_addr_result;
    
    auto ip_addr = dynamic_cast<const Address::Ip*>(local_addr.get());
    ASSERT_NE(nullptr, ip_addr);
    EXPECT_GT(ip_addr->port(), 0);
    
    // Clean up
    listen_handle->close();
  });
}

// Test connection establishment (previously DISABLED)
TEST_F(NetworkRealIoTest, ConnectionEstablishment) {
  executeInDispatcher([&]() {
    auto& socket_interface = socketInterface();
    
    // Create listener
    auto listen_fd = socket_interface.socket(
        SocketType::Stream,
        Address::Type::Ip,
        Address::IpVersion::v4);
    ASSERT_TRUE(listen_fd.ok());
    
    auto listen_handle = socket_interface.ioHandleForFd(*listen_fd, false);
    auto bind_addr = Address::parseInternetAddress("127.0.0.1", 0);
    ASSERT_TRUE(listen_handle->bind(bind_addr).ok());
    ASSERT_TRUE(listen_handle->listen(1).ok());
    
    auto local_addr_result = listen_handle->localAddress();
    ASSERT_TRUE(local_addr_result.ok());
    auto local_addr = *local_addr_result;
    
    // Create client
    auto client_fd = socket_interface.socket(
        SocketType::Stream,
        Address::Type::Ip,
        Address::IpVersion::v4);
    ASSERT_TRUE(client_fd.ok());
    
    auto client_handle = socket_interface.ioHandleForFd(*client_fd, false);
    client_handle->setBlocking(false);
    
    // Connect (may return in progress for non-blocking)
    auto connect_result = client_handle->connect(local_addr);
    
    // For non-blocking socket, connection might be in progress
    if (!connect_result.ok()) {
      // Check if it's just EINPROGRESS
      EXPECT_TRUE(errno == EINPROGRESS || errno == EWOULDBLOCK);
    }
    
    // Accept connection
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto accepted = listen_handle->accept();
    
    // Might need to retry for non-blocking
    if (!accepted.ok()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      accepted = listen_handle->accept();
    }
    
    // Clean up
    if (accepted.ok()) {
      (*accepted)->close();
    }
    client_handle->close();
    listen_handle->close();
  });
}

// Test data transfer with socket pair (previously DISABLED)
TEST_F(NetworkRealIoTest, DataTransfer) {
  executeInDispatcher([&]() {
    // Create socket pair
    auto socket_pair = createSocketPair();
    auto& client_io = socket_pair.first;
    auto& server_io = socket_pair.second;
    
    // Test write from client
    std::string test_data = "Hello from real IO test!";
    OwnedBuffer write_buffer;
    write_buffer.add(test_data);
    
    auto write_result = client_io->write(write_buffer);
    ASSERT_TRUE(write_result.ok());
    EXPECT_EQ(test_data.size(), *write_result);
    
    // Small delay to ensure data is transmitted
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Test read on server
    OwnedBuffer read_buffer;
    auto read_result = server_io->read(read_buffer);
    ASSERT_TRUE(read_result.ok());
    EXPECT_GT(*read_result, 0);
    
    // Verify data
    std::string received = read_buffer.toString();
    EXPECT_EQ(test_data, received);
    
    // Clean up
    client_io->close();
    server_io->close();
  });
}

// Test multiple socket operations (previously DISABLED)
TEST_F(NetworkRealIoTest, MultipleSockets) {
  const int num_sockets = 10;
  std::vector<IoHandlePtr> sockets;
  
  executeInDispatcher([&]() {
    auto& socket_interface = socketInterface();
    
    // Create multiple sockets
    for (int i = 0; i < num_sockets; ++i) {
      auto socket_fd = socket_interface.socket(
          SocketType::Stream,
          Address::Type::Ip,
          Address::IpVersion::v4);
      ASSERT_TRUE(socket_fd.ok());
      
      auto io_handle = socket_interface.ioHandleForFd(*socket_fd, false);
      ASSERT_NE(nullptr, io_handle);
      
      // Bind to ephemeral port
      auto bind_addr = Address::parseInternetAddress("127.0.0.1", 0);
      auto bind_result = io_handle->bind(bind_addr);
      ASSERT_TRUE(bind_result.ok());
      
      sockets.push_back(std::move(io_handle));
    }
    
    EXPECT_EQ(num_sockets, sockets.size());
    
    // Verify all sockets are open
    for (auto& socket : sockets) {
      EXPECT_TRUE(socket->isOpen());
    }
    
    // Close all sockets
    for (auto& socket : sockets) {
      socket->close();
    }
    
    sockets.clear();
  });
}

// Test socket options (previously DISABLED)
TEST_F(NetworkRealIoTest, SocketOptions) {
  executeInDispatcher([&]() {
    auto& socket_interface = socketInterface();
    
    // Create socket
    auto socket_fd = socket_interface.socket(
        SocketType::Stream,
        Address::Type::Ip,
        Address::IpVersion::v4);
    ASSERT_TRUE(socket_fd.ok());
    
    auto io_handle = socket_interface.ioHandleForFd(*socket_fd, false);
    
    // Set SO_REUSEADDR
    int reuse = 1;
    auto set_result = io_handle->setSocketOption(
        SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    EXPECT_TRUE(set_result.ok());
    
    // Get SO_REUSEADDR
    int reuse_val = 0;
    socklen_t len = sizeof(reuse_val);
    auto get_result = io_handle->getSocketOption(
        SOL_SOCKET, SO_REUSEADDR, &reuse_val, &len);
    EXPECT_TRUE(get_result.ok());
    // On macOS, SO_REUSEADDR may return 4 (sizeof(int)) instead of 1
    EXPECT_NE(0, reuse_val);
    
    // Set blocking mode
    auto blocking_result = io_handle->setBlocking(false);
    EXPECT_TRUE(blocking_result.ok());
    
    // Clean up
    io_handle->close();
  });
}

// Test error handling (previously DISABLED)
TEST_F(NetworkRealIoTest, ErrorHandling) {
  executeInDispatcher([&]() {
    auto& socket_interface = socketInterface();
    
    // Create socket
    auto socket_fd = socket_interface.socket(
        SocketType::Stream,
        Address::Type::Ip,
        Address::IpVersion::v4);
    ASSERT_TRUE(socket_fd.ok());
    
    auto io_handle = socket_interface.ioHandleForFd(*socket_fd, false);
    
    // Try to bind to privileged port (should fail)
    auto bind_addr = Address::parseInternetAddress("127.0.0.1", 80);
    auto bind_result = io_handle->bind(bind_addr);
    
    // Should fail with permission denied (unless running as root)
    if (getuid() != 0) {
      EXPECT_FALSE(bind_result.ok());
    }
    
    // Try to connect to a port with no listener
    auto target_addr = Address::parseInternetAddress("127.0.0.1", 12345);
    io_handle->setBlocking(false);  // Set non-blocking to avoid hanging
    auto connect_result = io_handle->connect(target_addr);
    
    // For non-blocking sockets, connect may return immediately with EINPROGRESS
    // or fail with connection refused if no listener
    if (!connect_result.ok()) {
      // Expected behavior - connection refused or in progress
      EXPECT_TRUE(errno == ECONNREFUSED || errno == EINPROGRESS || errno == EWOULDBLOCK);
    } else {
      // Unexpected success - verify it's actually not connected
      // Try to write something to confirm it's not really connected
      OwnedBuffer test_buf;
      test_buf.add("test");
      auto write_result = io_handle->write(test_buf);
      EXPECT_FALSE(write_result.ok());  // Write should fail
    }
    
    // Clean up
    io_handle->close();
  });
}

} // namespace
} // namespace network
} // namespace mcp