/**
 * @file test_tcp_echo_client_basic.cc
 * @brief Basic unit tests for TCP echo client implementation
 * 
 * Tests focus on verifying core TCP echo client components.
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <memory>

#include "mcp/network/address.h"
#include "mcp/network/socket_interface.h"
#include "mcp/network/io_handle.h"
#include "mcp/network/connection.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/transport_socket.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/stream_info/stream_info_impl.h"
#include "mcp/buffer.h"

namespace mcp {
namespace examples {
namespace test {

using namespace std::chrono_literals;

// Test fixture for TCP echo client basic tests
class TcpEchoClientBasicTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create dispatcher
    dispatcher_ = std::make_unique<event::LibeventDispatcher>("test_client");
    socket_interface_ = &network::socketInterface();
  }
  
  void TearDown() override {
    dispatcher_.reset();
  }
  
  std::unique_ptr<event::Dispatcher> dispatcher_;
  network::SocketInterface* socket_interface_;
};

// Test 1: Basic component availability
TEST_F(TcpEchoClientBasicTest, ComponentAvailability) {
  // Verify dispatcher was created
  ASSERT_NE(nullptr, dispatcher_);
  EXPECT_EQ("test_client", dispatcher_->name());
  
  // Verify socket interface is available
  ASSERT_NE(nullptr, socket_interface_);
}

// Test 2: Address parsing - happy path
TEST_F(TcpEchoClientBasicTest, AddressParsing) {
  // Happy path - valid IPv4
  auto addr = network::Address::parseInternetAddress("127.0.0.1", 8080);
  ASSERT_NE(nullptr, addr);
  EXPECT_EQ(network::Address::Type::Ip, addr->type());
  EXPECT_EQ(8080, addr->ip()->port());
  
  // Edge case - port 0 (any port)
  addr = network::Address::parseInternetAddress("127.0.0.1", 0);
  ASSERT_NE(nullptr, addr);
  EXPECT_EQ(0, addr->ip()->port());
  
  // Edge case - max port
  addr = network::Address::parseInternetAddress("127.0.0.1", 65535);
  ASSERT_NE(nullptr, addr);
  EXPECT_EQ(65535, addr->ip()->port());
  
  // Edge case - invalid address
  addr = network::Address::parseInternetAddress("999.999.999.999", 8080);
  EXPECT_EQ(nullptr, addr);
}

// Test 3: Socket creation and cleanup
TEST_F(TcpEchoClientBasicTest, SocketOperations) {
  // Happy path - create socket
  auto result = socket_interface_->socket(
      network::SocketType::Stream,
      network::Address::Type::Ip,
      network::Address::IpVersion::v4,
      false);
  
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.value.has_value());
  
  // Edge case - create IO handle
  auto io_handle = socket_interface_->ioHandleForFd(*result.value, false);
  ASSERT_NE(nullptr, io_handle);
  
  // Clean up
  socket_interface_->close(*result.value);
}

// Test 4: Transport socket creation
TEST_F(TcpEchoClientBasicTest, TransportSocketCreation) {
  // Create raw buffer transport socket
  auto transport_socket = std::make_unique<network::RawBufferTransportSocket>();
  ASSERT_NE(nullptr, transport_socket);
}

// Test 5: Stream info creation
TEST_F(TcpEchoClientBasicTest, StreamInfoCreation) {
  // Create stream info
  stream_info::StreamInfoImpl stream_info;
  // Just verify it was created (no public interface to test)
  SUCCEED();
}

// Test 6: Buffer operations
TEST_F(TcpEchoClientBasicTest, BufferOperations) {
  // Create buffer
  OwnedBuffer buffer;
  
  // Add data
  std::string test_data = "Test Message";
  buffer.add(test_data);
  
  // Verify data
  EXPECT_EQ(test_data.length(), buffer.length());
  EXPECT_EQ(test_data, buffer.toString());
  
  // Clear buffer
  buffer.drain(buffer.length());
  EXPECT_EQ(0, buffer.length());
}

// Test 7: Address any address creation
TEST_F(TcpEchoClientBasicTest, AnyAddressCreation) {
  // Create any address IPv4
  auto addr = network::Address::anyAddress(network::Address::IpVersion::v4, 0);
  ASSERT_NE(nullptr, addr);
  EXPECT_EQ(network::Address::Type::Ip, addr->type());
  EXPECT_EQ(0, addr->ip()->port());
  
  // Create any address IPv6
  addr = network::Address::anyAddress(network::Address::IpVersion::v6, 8080);
  ASSERT_NE(nullptr, addr);
  EXPECT_EQ(network::Address::IpVersion::v6, addr->ip()->version());
  EXPECT_EQ(8080, addr->ip()->port());
}

// Test 8: Socket options
TEST_F(TcpEchoClientBasicTest, SocketOptions) {
  // Create socket
  auto result = socket_interface_->socket(
      network::SocketType::Stream,
      network::Address::Type::Ip,
      network::Address::IpVersion::v4,
      false);
  
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.value.has_value());
  
  // Just test that we can close it
  socket_interface_->close(*result.value);
  SUCCEED();
}

// Test 9: Multiple socket creation
TEST_F(TcpEchoClientBasicTest, MultipleSocketCreation) {
  std::vector<network::os_fd_t> sockets;
  
  // Create multiple sockets
  for (int i = 0; i < 5; i++) {
    auto result = socket_interface_->socket(
        network::SocketType::Stream,
        network::Address::Type::Ip,
        network::Address::IpVersion::v4,
        false);
    
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    sockets.push_back(*result.value);
  }
  
  // Clean up all sockets
  for (auto fd : sockets) {
    socket_interface_->close(fd);
  }
  
  EXPECT_EQ(5, sockets.size());
}

} // namespace test
} // namespace examples
} // namespace mcp