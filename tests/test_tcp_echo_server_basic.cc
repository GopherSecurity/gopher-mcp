/**
 * @file test_tcp_echo_server_basic.cc
 * @brief Basic unit tests for TCP echo server implementation
 * 
 * Tests focus on verifying core TCP echo server components.
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <memory>

#include "mcp/network/address.h"
#include "mcp/network/socket_interface.h"
#include "mcp/network/listener.h"
#include "mcp/event/libevent_dispatcher.h"

namespace mcp {
namespace examples {
namespace test {

using namespace std::chrono_literals;

// Test fixture for TCP echo server basic tests
class TcpEchoServerBasicTest : public ::testing::Test {
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
TEST_F(TcpEchoServerBasicTest, DispatcherCreation) {
  // Verify dispatcher was created
  ASSERT_NE(nullptr, dispatcher_);
  
  // Verify name is correct
  EXPECT_EQ("test", dispatcher_->name());
}

// Test 2: Socket interface availability
TEST_F(TcpEchoServerBasicTest, SocketInterfaceAvailable) {
  // Verify socket interface is available
  ASSERT_NE(nullptr, socket_interface_);
}

// Test 3: Address creation for binding
TEST_F(TcpEchoServerBasicTest, BindAddressCreation) {
  // Create bind address
  auto addr = network::Address::anyAddress(network::Address::IpVersion::v4, 8080);
  ASSERT_NE(nullptr, addr);
  EXPECT_EQ(network::Address::Type::Ip, addr->type());
  ASSERT_NE(nullptr, addr->ip());
  EXPECT_EQ(8080, addr->ip()->port());
}

// Test 4: Listener config creation
TEST_F(TcpEchoServerBasicTest, ListenerConfigCreation) {
  // Create listener config
  network::ListenerConfig config;
  config.name = "test_listener";
  config.address = network::Address::anyAddress(network::Address::IpVersion::v4, 8080);
  config.bind_to_port = true;
  config.backlog = 128;
  config.per_connection_buffer_limit = 1024 * 1024;
  
  // Verify config was created properly
  EXPECT_EQ("test_listener", config.name);
  ASSERT_NE(nullptr, config.address);
  EXPECT_TRUE(config.bind_to_port);
  EXPECT_EQ(128, config.backlog);
  EXPECT_EQ(1024 * 1024, config.per_connection_buffer_limit);
}

// Test 5: Socket creation for server
TEST_F(TcpEchoServerBasicTest, ServerSocketCreation) {
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

} // namespace test
} // namespace examples
} // namespace mcp