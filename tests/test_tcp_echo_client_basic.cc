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
#include "mcp/event/libevent_dispatcher.h"

namespace mcp {
namespace examples {
namespace test {

using namespace std::chrono_literals;

// Test fixture for TCP echo client basic tests
class TcpEchoClientBasicTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create dispatcher
    dispatcher_ = std::make_unique<event::LibeventDispatcher>("test");
    socket_interface_ = &network::socketInterface();
  }
  
  void TearDown() override {
    dispatcher_.reset();
  }
  
  std::unique_ptr<event::Dispatcher> dispatcher_;
  network::SocketInterface* socket_interface_;
};

// Test 1: Dispatcher creation and basic functionality
TEST_F(TcpEchoClientBasicTest, DispatcherCreation) {
  // Verify dispatcher was created
  ASSERT_NE(nullptr, dispatcher_);
  
  // Verify name is correct
  EXPECT_EQ("test", dispatcher_->name());
}

// Test 2: Socket interface availability
TEST_F(TcpEchoClientBasicTest, SocketInterfaceAvailable) {
  // Verify socket interface is available
  ASSERT_NE(nullptr, socket_interface_);
}

// Test 3: Address parsing for IPv4
TEST_F(TcpEchoClientBasicTest, IPv4AddressParsing) {
  // Test valid IPv4 address
  auto addr = network::Address::parseInternetAddress("127.0.0.1", 8080);
  ASSERT_NE(nullptr, addr);
  EXPECT_EQ(network::Address::Type::Ip, addr->type());
  ASSERT_NE(nullptr, addr->ip());
  EXPECT_EQ(8080, addr->ip()->port());
}

// Test 4: Socket creation
TEST_F(TcpEchoClientBasicTest, SocketCreation) {
  // Create a socket
  auto result = socket_interface_->socket(
      network::SocketType::Stream,
      network::Address::Type::Ip,
      network::Address::IpVersion::v4,
      false);
  
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.value.has_value());
  
  // Clean up
  socket_interface_->close(*result.value);
}

// Test 5: Any address creation
TEST_F(TcpEchoClientBasicTest, AnyAddressCreation) {
  // Create any address for binding
  auto addr = network::Address::anyAddress(network::Address::IpVersion::v4, 0);
  ASSERT_NE(nullptr, addr);
  EXPECT_EQ(network::Address::Type::Ip, addr->type());
}

} // namespace test
} // namespace examples
} // namespace mcp