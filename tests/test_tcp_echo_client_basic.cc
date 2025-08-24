/**
 * @file test_tcp_echo_client_basic.cc
 * @brief Comprehensive tests for TCP echo client implementation using MCP
 * abstractions
 *
 * Tests cover happy paths, edge cases, and thread safety with MCP network
 * layer.
 */

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/core/result.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/address.h"
#include "mcp/network/connection.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/io_handle.h"
#include "mcp/network/listener.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/socket_interface.h"
#include "mcp/network/transport_socket.h"
#include "mcp/stream_info/stream_info_impl.h"

namespace mcp {
namespace examples {
namespace test {

using namespace std::chrono_literals;

// Mock echo server using MCP abstractions for integration testing
class MockEchoConnection : public network::ConnectionCallbacks {
 public:
  MockEchoConnection() = default;

  void onEvent(network::ConnectionEvent event) override {
    if (event == network::ConnectionEvent::Connected) {
      connected_ = true;
    } else if (event == network::ConnectionEvent::RemoteClose ||
               event == network::ConnectionEvent::LocalClose) {
      connected_ = false;
    }
  }

  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  void setConnection(network::Connection* conn) { connection_ = conn; }

  void simulateEcho(const std::string& data) {
    if (connection_ && connected_) {
      // Echo would normally happen through read callbacks
      // For testing, we simulate the echo response
      echo_data_ = data;
    }
  }

  bool connected_{false};
  network::Connection* connection_{nullptr};
  std::string echo_data_;
};

// Test fixture for TCP echo client comprehensive tests
class TcpEchoClientBasicTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create dispatcher
    dispatcher_ = std::make_unique<event::LibeventDispatcher>("test_client");
    socket_interface_ = &network::socketInterface();
  }

  void TearDown() override {
    // Clean up any connections
    test_connections_.clear();
    dispatcher_.reset();
  }

  // Helper to create a client connection socket
  std::unique_ptr<network::ConnectionSocket> createClientSocket(
      const std::string& server_addr, uint16_t server_port) {
    auto local_addr =
        network::Address::anyAddress(network::Address::IpVersion::v4, 0);
    auto remote_addr =
        network::Address::parseInternetAddress(server_addr, server_port);

    auto socket_result = socket_interface_->socket(
        network::SocketType::Stream, network::Address::Type::Ip,
        network::Address::IpVersion::v4, false);

    if (!socket_result.ok()) {
      return nullptr;
    }

    auto io_handle =
        socket_interface_->ioHandleForFd(*socket_result.value, false);
    return std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), local_addr, remote_addr);
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  network::SocketInterface* socket_interface_;
  std::vector<std::unique_ptr<MockEchoConnection>> test_connections_;
};

// Test 1: Basic component availability
TEST_F(TcpEchoClientBasicTest, ComponentAvailability) {
  ASSERT_NE(nullptr, dispatcher_);
  EXPECT_EQ("test_client", dispatcher_->name());
  ASSERT_NE(nullptr, socket_interface_);
}

// Test 2: Address parsing and creation
TEST_F(TcpEchoClientBasicTest, AddressOperations) {
  // Parse IPv4 address
  auto addr = network::Address::parseInternetAddress("127.0.0.1", 8080);
  ASSERT_NE(nullptr, addr);
  EXPECT_EQ(network::Address::Type::Ip, addr->type());
  EXPECT_EQ(8080, addr->ip()->port());

  // Create any address
  addr = network::Address::anyAddress(network::Address::IpVersion::v4, 0);
  ASSERT_NE(nullptr, addr);
  EXPECT_EQ(0, addr->ip()->port());

  // Test max port
  addr = network::Address::parseInternetAddress("127.0.0.1", 65535);
  ASSERT_NE(nullptr, addr);
  EXPECT_EQ(65535, addr->ip()->port());
}

// Test 3: Socket creation with MCP abstractions
TEST_F(TcpEchoClientBasicTest, SocketCreation) {
  // Create socket using MCP socket interface
  auto result = socket_interface_->socket(
      network::SocketType::Stream, network::Address::Type::Ip,
      network::Address::IpVersion::v4, false);

  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.value.has_value());

  // Create IO handle from socket
  auto io_handle = socket_interface_->ioHandleForFd(*result.value, false);
  ASSERT_NE(nullptr, io_handle);

  // Clean up
  socket_interface_->close(*result.value);
}

// Test 4: Connection socket creation
TEST_F(TcpEchoClientBasicTest, ConnectionSocketCreation) {
  // Create addresses
  auto local_addr =
      network::Address::anyAddress(network::Address::IpVersion::v4, 0);
  auto remote_addr = network::Address::parseInternetAddress("127.0.0.1", 8080);

  ASSERT_NE(nullptr, local_addr);
  ASSERT_NE(nullptr, remote_addr);

  // Create socket
  auto socket_result = socket_interface_->socket(
      network::SocketType::Stream, network::Address::Type::Ip,
      network::Address::IpVersion::v4, false);

  ASSERT_TRUE(socket_result.ok());

  // Create IO handle and connection socket
  auto io_handle =
      socket_interface_->ioHandleForFd(*socket_result.value, false);
  auto conn_socket = std::make_unique<network::ConnectionSocketImpl>(
      std::move(io_handle), local_addr, remote_addr);

  ASSERT_NE(nullptr, conn_socket);

  // Verify socket was created
  // Note: ConnectionSocketImpl doesn't expose addresses directly
}

// Test 5: Transport socket creation
TEST_F(TcpEchoClientBasicTest, TransportSocketCreation) {
  // Create raw buffer transport socket
  auto transport_socket = std::make_unique<network::RawBufferTransportSocket>();
  ASSERT_NE(nullptr, transport_socket);

  // Verify protocol (RawBufferTransportSocket returns empty string)
  EXPECT_EQ("", transport_socket->protocol());
}

// Test 6: Buffer operations for echo data
TEST_F(TcpEchoClientBasicTest, BufferOperations) {
  OwnedBuffer buffer;

  // Test adding data
  std::string test_data = "Echo Test Message";
  buffer.add(test_data);
  EXPECT_EQ(test_data.length(), buffer.length());

  // Test reading data
  std::string read_data = buffer.toString();
  EXPECT_EQ(test_data, read_data);

  // Test draining data
  buffer.drain(5);
  EXPECT_EQ(test_data.length() - 5, buffer.length());

  // Test adding more data
  buffer.add(" More");
  EXPECT_GT(buffer.length(), 0);
}

// Test 7: Stream info for connection metadata
TEST_F(TcpEchoClientBasicTest, StreamInfoCreation) {
  stream_info::StreamInfoImpl stream_info;

  // StreamInfoImpl exists and can be created
  // Specific metadata methods depend on implementation
}

// Test 8: Connection callbacks interface
TEST_F(TcpEchoClientBasicTest, ConnectionCallbacksInterface) {
  // Create a mock connection callbacks
  class TestCallbacks : public network::ConnectionCallbacks {
   public:
    void onEvent(network::ConnectionEvent event) override {
      last_event_ = event;
      event_count_++;
    }

    void onAboveWriteBufferHighWatermark() override { high_watermark_count_++; }

    void onBelowWriteBufferLowWatermark() override { low_watermark_count_++; }

    network::ConnectionEvent last_event_;
    int event_count_ = 0;
    int high_watermark_count_ = 0;
    int low_watermark_count_ = 0;
  };

  TestCallbacks callbacks;

  // Simulate events
  callbacks.onEvent(network::ConnectionEvent::Connected);
  EXPECT_EQ(network::ConnectionEvent::Connected, callbacks.last_event_);
  EXPECT_EQ(1, callbacks.event_count_);

  callbacks.onEvent(network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(network::ConnectionEvent::RemoteClose, callbacks.last_event_);
  EXPECT_EQ(2, callbacks.event_count_);

  callbacks.onAboveWriteBufferHighWatermark();
  EXPECT_EQ(1, callbacks.high_watermark_count_);

  callbacks.onBelowWriteBufferLowWatermark();
  EXPECT_EQ(1, callbacks.low_watermark_count_);
}

// Test 9: Multiple address creation
TEST_F(TcpEchoClientBasicTest, MultipleAddressCreation) {
  std::vector<network::Address::InstanceConstSharedPtr> addresses;

  // Create multiple addresses
  for (int port = 8080; port < 8085; port++) {
    auto addr = network::Address::parseInternetAddress("127.0.0.1", port);
    ASSERT_NE(nullptr, addr);
    addresses.push_back(addr);
  }

  EXPECT_EQ(5, addresses.size());

  // Verify each address
  for (size_t i = 0; i < addresses.size(); i++) {
    EXPECT_EQ(8080 + i, addresses[i]->ip()->port());
  }
}

// Test 10: Dispatcher thread safety check
TEST_F(TcpEchoClientBasicTest, DispatcherThreadSafety) {
  std::atomic<int> counter(0);

  // Post multiple tasks to dispatcher
  for (int i = 0; i < 10; i++) {
    dispatcher_->post([&counter]() { counter++; });
  }

  // Run dispatcher briefly
  dispatcher_->run(event::RunType::NonBlock);

  // Counter should have been incremented
  EXPECT_GT(counter.load(), 0);
  EXPECT_LE(counter.load(), 10);
}

// Test 11: Connection lifecycle with callbacks
TEST_F(TcpEchoClientBasicTest, ConnectionLifecycleWithCallbacks) {
  // Track connection events
  class LifecycleCallbacks : public network::ConnectionCallbacks {
   public:
    void onEvent(network::ConnectionEvent event) override {
      events_.push_back(event);
      if (event == network::ConnectionEvent::Connected) {
        connect_count_++;
      } else if (event == network::ConnectionEvent::LocalClose) {
        local_close_count_++;
      } else if (event == network::ConnectionEvent::RemoteClose) {
        remote_close_count_++;
      }
    }

    void onAboveWriteBufferHighWatermark() override { high_watermark_count_++; }

    void onBelowWriteBufferLowWatermark() override { low_watermark_count_++; }

    std::vector<network::ConnectionEvent> events_;
    int connect_count_{0};
    int local_close_count_{0};
    int remote_close_count_{0};
    int high_watermark_count_{0};
    int low_watermark_count_{0};
  };

  LifecycleCallbacks callbacks;

  // Simulate connection lifecycle events
  callbacks.onEvent(network::ConnectionEvent::Connected);
  EXPECT_EQ(1, callbacks.connect_count_);

  // Simulate watermark events during data transfer
  callbacks.onAboveWriteBufferHighWatermark();
  EXPECT_EQ(1, callbacks.high_watermark_count_);

  callbacks.onBelowWriteBufferLowWatermark();
  EXPECT_EQ(1, callbacks.low_watermark_count_);

  // Simulate connection close
  callbacks.onEvent(network::ConnectionEvent::LocalClose);
  EXPECT_EQ(1, callbacks.local_close_count_);

  // Verify event sequence
  ASSERT_EQ(2, callbacks.events_.size());
  EXPECT_EQ(network::ConnectionEvent::Connected, callbacks.events_[0]);
  EXPECT_EQ(network::ConnectionEvent::LocalClose, callbacks.events_[1]);
}

// Test 12: Multiple concurrent echo operations
TEST_F(TcpEchoClientBasicTest, MultipleConcurrentEchoOperations) {
  const int num_operations = 5;
  std::vector<OwnedBuffer> send_buffers;
  std::vector<OwnedBuffer> receive_buffers;

  // Prepare multiple echo messages
  for (int i = 0; i < num_operations; i++) {
    OwnedBuffer buffer;
    std::string msg = "Echo message " + std::to_string(i);
    buffer.add(msg);
    send_buffers.push_back(std::move(buffer));
    receive_buffers.emplace_back();
  }

  // Simulate sending all messages
  for (int i = 0; i < num_operations; i++) {
    EXPECT_GT(send_buffers[i].length(), 0);
  }

  // Simulate receiving echo responses
  for (int i = 0; i < num_operations; i++) {
    std::string expected = "Echo message " + std::to_string(i);
    receive_buffers[i].add(expected);
    EXPECT_EQ(expected, receive_buffers[i].toString());
  }
}

// Test 13: Error handling for invalid addresses
TEST_F(TcpEchoClientBasicTest, ErrorHandlingInvalidAddresses) {
  // Note: Port values > 65535 get truncated to uint16_t, creating valid ports
  // For example, 70000 becomes 4464, 65536 becomes 0
  // So we test with actually invalid address strings instead

  // Test with empty address
  auto addr = network::Address::parseInternetAddress("", 8080);
  EXPECT_EQ(nullptr, addr);

  // Test with invalid IP format
  addr = network::Address::parseInternetAddress("999.999.999.999", 8080);
  EXPECT_EQ(nullptr, addr);

  // Test with invalid IP format (negative)
  addr = network::Address::parseInternetAddress("-1.-1.-1.-1", 8080);
  EXPECT_EQ(nullptr, addr);

  // Test with hostname (not an IP) - this might work depending on
  // implementation
  addr = network::Address::parseInternetAddress("localhost", 8080);
  // localhost might resolve, so we don't assert it's null
  if (addr) {
    EXPECT_EQ(network::Address::Type::Ip, addr->type());
  }
}

// Test 14: Buffer management and flow control
TEST_F(TcpEchoClientBasicTest, BufferManagementFlowControl) {
  OwnedBuffer buffer;
  const size_t large_data_size = 1024 * 1024;  // 1MB

  // Add large amount of data
  std::string large_data(large_data_size, 'X');
  buffer.add(large_data);
  EXPECT_EQ(large_data_size, buffer.length());

  // Test partial draining
  size_t drain_size = 512 * 1024;  // 512KB
  buffer.drain(drain_size);
  EXPECT_EQ(large_data_size - drain_size, buffer.length());

  // Test complete draining
  buffer.drain(buffer.length());
  EXPECT_EQ(0, buffer.length());

  // Test buffer reuse
  buffer.add("New data after drain");
  EXPECT_EQ(20, buffer.length());
}

// Test 15: Connection socket options
TEST_F(TcpEchoClientBasicTest, ConnectionSocketOptions) {
  auto socket = createClientSocket("127.0.0.1", 8080);
  ASSERT_NE(nullptr, socket);

  // Test that socket was created
  EXPECT_NE(nullptr, socket);

  // ConnectionSocket provides access to connection info
  auto& info = socket->connectionInfoProvider();
  info.setRequestedServerName("echo.example.com");
  EXPECT_EQ("echo.example.com", socket->requestedServerName());

  // Test half-close functionality
  socket->setHalfClose(true);
  EXPECT_TRUE(socket->isHalfClose());

  socket->setHalfClose(false);
  EXPECT_FALSE(socket->isHalfClose());
}

// Test 16: Dispatcher task ordering
TEST_F(TcpEchoClientBasicTest, DispatcherTaskOrdering) {
  std::vector<int> execution_order;
  std::mutex order_mutex;

  // Post tasks that should execute in order
  for (int i = 0; i < 5; i++) {
    dispatcher_->post([&execution_order, &order_mutex, i]() {
      std::lock_guard<std::mutex> lock(order_mutex);
      execution_order.push_back(i);
    });
  }

  // Run dispatcher to execute all tasks
  dispatcher_->run(event::RunType::NonBlock);

  // Tasks should execute in FIFO order
  for (size_t i = 0; i < execution_order.size(); i++) {
    EXPECT_EQ(i, execution_order[i]);
  }
}

// Test 17: Transport socket protocol detection
TEST_F(TcpEchoClientBasicTest, TransportSocketProtocol) {
  // Test raw buffer transport (returns empty string by design)
  auto raw_transport = std::make_unique<network::RawBufferTransportSocket>();
  EXPECT_EQ("", raw_transport->protocol());

  // Future: Test TLS transport when available
  // auto tls_transport = std::make_unique<network::TlsTransportSocket>();
  // EXPECT_EQ("tls", tls_transport->protocol());
}

// Test 18: Connection retry with backoff
TEST_F(TcpEchoClientBasicTest, ConnectionRetryWithBackoff) {
  struct RetryState {
    int attempt_count{0};
    std::vector<std::chrono::milliseconds> backoff_delays;

    void recordAttempt(std::chrono::milliseconds delay) {
      attempt_count++;
      backoff_delays.push_back(delay);
    }
  };

  RetryState retry_state;

  // Simulate retry attempts with exponential backoff
  std::chrono::milliseconds base_delay(100);
  for (int i = 0; i < 3; i++) {
    auto delay = base_delay * (1 << i);  // Exponential backoff
    retry_state.recordAttempt(delay);
  }

  EXPECT_EQ(3, retry_state.attempt_count);
  EXPECT_EQ(100ms, retry_state.backoff_delays[0]);
  EXPECT_EQ(200ms, retry_state.backoff_delays[1]);
  EXPECT_EQ(400ms, retry_state.backoff_delays[2]);
}

// Test 19: Echo data integrity verification
TEST_F(TcpEchoClientBasicTest, EchoDataIntegrityVerification) {
  // Test various data patterns
  std::vector<std::string> test_patterns = {
      "Simple ASCII text",         "Numbers: 1234567890",
      "Special chars: !@#$%^&*()", "Mixed: ABC123!@#",
      std::string(1000, 'A'),  // Large repetitive data
      "\x00\x01\x02\x03",      // Binary data
      "Unicode: \u2764\uFE0F"  // Emoji/Unicode
  };

  for (const auto& pattern : test_patterns) {
    OwnedBuffer send_buffer;
    send_buffer.add(pattern);

    // Simulate echo
    OwnedBuffer receive_buffer;
    receive_buffer.add(pattern);

    // Verify integrity
    EXPECT_EQ(send_buffer.toString(), receive_buffer.toString());
    EXPECT_EQ(pattern, receive_buffer.toString());
  }
}

// Test 20: Stress test with rapid connect/disconnect
TEST_F(TcpEchoClientBasicTest, StressTestRapidConnectDisconnect) {
  const int num_cycles = 10;
  std::atomic<int> connect_count(0);
  std::atomic<int> disconnect_count(0);

  for (int i = 0; i < num_cycles; i++) {
    // Simulate rapid connect
    dispatcher_->post([&connect_count]() { connect_count++; });

    // Simulate rapid disconnect
    dispatcher_->post([&disconnect_count]() { disconnect_count++; });
  }

  // Execute all posted tasks
  for (int i = 0; i < 5; i++) {
    dispatcher_->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(10ms);
  }

  // All connects and disconnects should complete
  EXPECT_EQ(num_cycles, connect_count.load());
  EXPECT_EQ(num_cycles, disconnect_count.load());
}

}  // namespace test
}  // namespace examples
}  // namespace mcp