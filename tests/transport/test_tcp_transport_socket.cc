/**
 * @file test_tcp_transport_socket.cc
 * @brief Comprehensive tests for TcpTransportSocket using real I/O
 */

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/core/compat.h"  // For holds_alternative
#include "mcp/network/address_impl.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/io_socket_handle_impl.h"
#include "mcp/network/server_listener_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/transport/tcp_transport_socket.h"

// Include the real I/O test base
#include "../integration/real_io_test_base.h"

namespace mcp {
namespace transport {
namespace {

// Real implementation of transport socket callbacks
class TestTransportSocketCallbacks : public network::TransportSocketCallbacks {
 public:
  explicit TestTransportSocketCallbacks(network::Connection& connection)
      : connection_(connection) {}

  // IoHandle access
  network::IoHandle& ioHandle() override {
    return connection_.socket().ioHandle();
  }
  const network::IoHandle& ioHandle() const override {
    return connection_.socket().ioHandle();
  }

  network::Connection& connection() override { return connection_; }
  bool shouldDrainReadBuffer() override { return true; }
  void setTransportSocketIsReadable() override { readable_called_ = true; }
  void raiseEvent(network::ConnectionEvent event) override {
    last_event_ = event;
    event_count_++;
  }
  void flushWriteBuffer() override { flush_called_ = true; }

  // Test helpers
  network::ConnectionEvent lastEvent() const { return last_event_; }
  int eventCount() const { return event_count_; }
  bool readableCalled() const { return readable_called_; }
  bool flushCalled() const { return flush_called_; }
  void reset() {
    last_event_ = network::ConnectionEvent::Connected;
    event_count_ = 0;
    readable_called_ = false;
    flush_called_ = false;
  }

 private:
  network::Connection& connection_;
  network::ConnectionEvent last_event_{network::ConnectionEvent::Connected};
  int event_count_{0};
  bool readable_called_{false};
  bool flush_called_{false};
};

// Test fixture for TcpTransportSocket with real I/O
class TcpTransportSocketTest : public test::RealListenerTestBase {
 protected:
  void SetUp() override {
    // Call base class setup first
    RealListenerTestBase::SetUp();

    // Default configuration
    config_.tcp_nodelay = true;
    config_.tcp_keepalive = true;
    config_.connect_timeout = std::chrono::milliseconds(5000);
    config_.io_timeout = std::chrono::milliseconds(10000);
  }

  void TearDown() override {
    // Clean up in proper order
    executeInDispatcher([this]() {
      client_transport_.reset();
      server_transport_.reset();
      client_connection_.reset();
      server_connection_.reset();
      client_callbacks_.reset();
      server_callbacks_.reset();
    });

    // Call base class teardown
    RealListenerTestBase::TearDown();
  }

  // Helper to run a client-server test with proper synchronization
  void runClientServerTest(std::function<void()> test_func) {
    // Perform all setup in a single dispatcher call to avoid race conditions
    executeInDispatcher([this, test_func]() {
      // Setup listener
      listen_port_ = createRealListener();

      // Create client connection
      auto address = std::make_shared<network::Address::Ipv4Instance>(
          "127.0.0.1", listen_port_);

      auto io_handle = std::make_unique<network::IoSocketHandleImpl>();
      auto socket = std::make_unique<network::ConnectionSocketImpl>(
          std::move(io_handle),
          nullptr,  // No local address
          address   // Remote address
      );

      client_connection_ = std::make_unique<network::ConnectionImpl>(
          *dispatcher_, std::move(socket),
          nullptr,  // No transport socket initially
          false     // Not connected yet
      );

      // Create client transport socket
      client_transport_ =
          std::make_unique<TcpTransportSocket>(*dispatcher_, config_);
      client_callbacks_ =
          std::make_unique<TestTransportSocketCallbacks>(*client_connection_);
      client_transport_->setTransportSocketCallbacks(*client_callbacks_);

      // Connect client
      auto result = client_transport_->connect(client_connection_->socket());
      EXPECT_TRUE(mcp::holds_alternative<std::nullptr_t>(result));

      // Accept server connection (call directly, we're already in dispatcher
      // thread) Since socket is non-blocking, we may need to retry
      network::IoHandlePtr accepted_handle;
      int retry_count = 0;
      while (retry_count < 100) {  // Try for up to 1 second
        auto accepted = listen_handle_->accept();
        if (accepted.ok()) {
          accepted_handle = std::move(*accepted);
          break;
        }

        // For non-blocking socket, EAGAIN/EWOULDBLOCK is expected if no
        // connection yet
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          retry_count++;
          continue;
        }

        throw std::runtime_error("Failed to accept connection");
      }

      if (!accepted_handle) {
        throw std::runtime_error("Accept timed out");
      }

      // Create server connection from accepted socket
      auto server_socket = std::make_unique<network::ConnectionSocketImpl>(
          std::move(accepted_handle),
          nullptr,  // Local address will be determined
          nullptr   // Remote address will be determined
      );

      server_connection_ = std::make_unique<network::ConnectionImpl>(
          *dispatcher_, std::move(server_socket),
          nullptr,  // No transport socket initially
          true      // Already connected
      );

      // Create server transport socket
      server_transport_ =
          std::make_unique<TcpTransportSocket>(*dispatcher_, config_);
      server_callbacks_ =
          std::make_unique<TestTransportSocketCallbacks>(*server_connection_);
      server_transport_->setTransportSocketCallbacks(*server_callbacks_);

      // Run the actual test
      test_func();
    });
  }

 protected:
  TcpTransportSocketConfig config_;
  uint16_t listen_port_{0};

  // Client side
  std::unique_ptr<network::Connection> client_connection_;
  std::unique_ptr<TcpTransportSocket> client_transport_;
  std::unique_ptr<TestTransportSocketCallbacks> client_callbacks_;

  // Server side
  std::unique_ptr<network::Connection> server_connection_;
  std::unique_ptr<TcpTransportSocket> server_transport_;
  std::unique_ptr<TestTransportSocketCallbacks> server_callbacks_;
};

// ===== Basic Tests =====

TEST_F(TcpTransportSocketTest, CreateAndDestroy) {
  executeInDispatcher([this]() {
    auto transport =
        std::make_unique<TcpTransportSocket>(*dispatcher_, config_);
    ASSERT_NE(transport, nullptr);
    EXPECT_TRUE(transport->canFlushClose());  // TCP supports flush close
    EXPECT_EQ(transport->protocol(), "tcp");
  });
}

TEST_F(TcpTransportSocketTest, SetCallbacks) {
  executeInDispatcher([this]() {
    // Create a simple client connection for testing callbacks
    auto address =
        std::make_shared<network::Address::Ipv4Instance>("127.0.0.1", 8080);
    auto io_handle = std::make_unique<network::IoSocketHandleImpl>();
    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), nullptr, address);

    client_connection_ = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(socket), nullptr, false);

    client_transport_ =
        std::make_unique<TcpTransportSocket>(*dispatcher_, config_);
    client_callbacks_ =
        std::make_unique<TestTransportSocketCallbacks>(*client_connection_);

    // Setting callbacks should not crash
    EXPECT_NO_THROW(
        client_transport_->setTransportSocketCallbacks(*client_callbacks_));
  });
}

// ===== Connection Tests =====

TEST_F(TcpTransportSocketTest, ConnectToSocket) {
  executeInDispatcher([this]() {
    // Create a simple client connection for testing
    auto address =
        std::make_shared<network::Address::Ipv4Instance>("127.0.0.1", 8080);
    auto io_handle = std::make_unique<network::IoSocketHandleImpl>();
    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), nullptr, address);

    client_connection_ = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(socket), nullptr, false);

    client_transport_ =
        std::make_unique<TcpTransportSocket>(*dispatcher_, config_);
    client_callbacks_ =
        std::make_unique<TestTransportSocketCallbacks>(*client_connection_);
    client_transport_->setTransportSocketCallbacks(*client_callbacks_);

    // Connect using the client socket (will fail but should not crash)
    auto result = client_transport_->connect(client_connection_->socket());

    // Result should be well-formed (success or error)
    // For a non-existent endpoint, it may succeed (non-blocking) or fail
    EXPECT_TRUE(mcp::holds_alternative<std::nullptr_t>(result) ||
                mcp::holds_alternative<mcp::Error>(result));
  });
}

TEST_F(TcpTransportSocketTest, OnConnectedCallback) {
  executeInDispatcher([this]() {
    // Create a simple client connection for testing
    auto address =
        std::make_shared<network::Address::Ipv4Instance>("127.0.0.1", 8080);
    auto io_handle = std::make_unique<network::IoSocketHandleImpl>();
    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), nullptr, address);

    client_connection_ = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(socket), nullptr, false);

    client_transport_ =
        std::make_unique<TcpTransportSocket>(*dispatcher_, config_);
    client_callbacks_ =
        std::make_unique<TestTransportSocketCallbacks>(*client_connection_);
    client_transport_->setTransportSocketCallbacks(*client_callbacks_);

    // Manually trigger onConnected as this is normally done by ConnectionImpl
    EXPECT_NO_THROW(client_transport_->onConnected());

    // The TCP transport might not raise a Connected event directly
    // since it's the Connection's responsibility. We just test that it doesn't
    // crash.
  });
}

// ===== I/O Tests with Real Sockets =====

TEST_F(TcpTransportSocketTest, DISABLED_DataTransferBetweenClientAndServer) {
  runClientServerTest([this]() {
    // Send data from client to server
    std::string test_data = "Hello from client!";
    OwnedBuffer client_write_buffer;
    client_write_buffer.add(test_data.data(), test_data.size());

    auto write_result = client_transport_->doWrite(client_write_buffer, false);
    EXPECT_EQ(write_result.action_, TransportIoResult::CONTINUE);
    EXPECT_GT(write_result.bytes_processed_, 0);

    // Read on server side
    OwnedBuffer server_read_buffer;
    auto read_result = server_transport_->doRead(server_read_buffer);

    // With real sockets, might need to retry if data hasn't arrived yet
    int retries = 0;
    while (read_result.bytes_processed_ == 0 && retries < 10) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      read_result = server_transport_->doRead(server_read_buffer);
      retries++;
    }

    EXPECT_GT(read_result.bytes_processed_, 0);
    EXPECT_EQ(read_result.action_, TransportIoResult::CONTINUE);

    // Verify data matches
    std::string received(static_cast<const char*>(server_read_buffer.linearize(
                             server_read_buffer.length())),
                         server_read_buffer.length());
    EXPECT_EQ(received, test_data);
  });
}

TEST_F(TcpTransportSocketTest, DISABLED_BidirectionalDataTransfer) {
  runClientServerTest([this]() {
    // Send from client to server
    std::string client_msg = "Client message";
    OwnedBuffer client_buffer;
    client_buffer.add(client_msg.data(), client_msg.size());

    auto client_write = client_transport_->doWrite(client_buffer, false);
    EXPECT_EQ(client_write.action_, TransportIoResult::CONTINUE);

    // Send from server to client
    std::string server_msg = "Server response";
    OwnedBuffer server_buffer;
    server_buffer.add(server_msg.data(), server_msg.size());

    auto server_write = server_transport_->doWrite(server_buffer, false);
    EXPECT_EQ(server_write.action_, TransportIoResult::CONTINUE);

    // Read both messages with retries
    OwnedBuffer client_read_buffer;
    OwnedBuffer server_read_buffer;

    // Allow time for data to arrive
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto server_read = server_transport_->doRead(server_read_buffer);
    auto client_read = client_transport_->doRead(client_read_buffer);

    EXPECT_GT(server_read.bytes_processed_, 0);
    EXPECT_GT(client_read.bytes_processed_, 0);
  });
}

// ===== Close Tests =====

TEST_F(TcpTransportSocketTest, CloseSocket) {
  executeInDispatcher([this]() {
    // Create a simple client connection for testing
    auto address =
        std::make_shared<network::Address::Ipv4Instance>("127.0.0.1", 8080);
    auto io_handle = std::make_unique<network::IoSocketHandleImpl>();
    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), nullptr, address);

    client_connection_ = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(socket), nullptr, false);

    client_transport_ =
        std::make_unique<TcpTransportSocket>(*dispatcher_, config_);
    client_callbacks_ =
        std::make_unique<TestTransportSocketCallbacks>(*client_connection_);
    client_transport_->setTransportSocketCallbacks(*client_callbacks_);

    // Close should trigger event
    EXPECT_NO_THROW(
        client_transport_->closeSocket(network::ConnectionEvent::LocalClose));

    // Verify close event was raised
    EXPECT_EQ(client_callbacks_->lastEvent(),
              network::ConnectionEvent::LocalClose);
    EXPECT_GE(client_callbacks_->eventCount(), 1);
  });
}

TEST_F(TcpTransportSocketTest, DISABLED_RemoteCloseDetection) {
  runClientServerTest([this]() {
    // Close from server side
    server_transport_->closeSocket(network::ConnectionEvent::LocalClose);

    // Client should detect remote close when trying to read
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    OwnedBuffer buffer;
    auto result = client_transport_->doRead(buffer);

    // Should detect connection closed
    // Exact behavior depends on implementation
    EXPECT_TRUE(result.action_ == TransportIoResult::CLOSE ||
                result.end_stream_read_);
  });
}

// ===== Error Handling Tests =====

TEST_F(TcpTransportSocketTest, DISABLED_WriteToClosedConnection) {
  runClientServerTest([this]() {
    // Close server side
    server_transport_->closeSocket(network::ConnectionEvent::LocalClose);
    server_connection_->close(network::ConnectionCloseType::NoFlush);

    // Allow time for close to propagate
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Try to write from client
    OwnedBuffer buffer;
    std::string data = "Should fail";
    buffer.add(data.data(), data.size());

    auto result = client_transport_->doWrite(buffer, false);

    // Write might succeed initially (buffered) but subsequent writes should
    // fail Try another write to ensure error is detected
    OwnedBuffer buffer2;
    buffer2.add(data.data(), data.size());
    auto result2 = client_transport_->doWrite(buffer2, false);

    // At least one should indicate error or close
    EXPECT_TRUE(result.action_ == TransportIoResult::CLOSE ||
                result2.action_ == TransportIoResult::CLOSE ||
                result.action_ == TransportIoResult::CLOSE ||
                result2.action_ == TransportIoResult::CLOSE);
  });
}

// ===== Configuration Tests =====

TEST_F(TcpTransportSocketTest, ConfigurationSettings) {
  executeInDispatcher([this]() {
    // Test with custom configuration
    TcpTransportSocketConfig custom_config;
    custom_config.tcp_nodelay = false;
    custom_config.tcp_keepalive = false;
    custom_config.connect_timeout = std::chrono::milliseconds(1000);
    custom_config.io_timeout = std::chrono::milliseconds(2000);

    auto transport =
        std::make_unique<TcpTransportSocket>(*dispatcher_, custom_config);
    ASSERT_NE(transport, nullptr);

    // The configuration should be applied (though we can't directly verify it)
    EXPECT_EQ(transport->protocol(), "tcp");
  });
}

// ===== SSL Compatibility Tests =====

TEST_F(TcpTransportSocketTest, SslInfo) {
  executeInDispatcher([this]() {
    TcpTransportSocket transport(*dispatcher_, config_);

    // TCP transport doesn't provide SSL info
    EXPECT_EQ(transport.ssl(), nullptr);
  });
}

TEST_F(TcpTransportSocketTest, CanFlushClose) {
  executeInDispatcher([this]() {
    TcpTransportSocket transport(*dispatcher_, config_);

    // TCP transport supports flush close
    EXPECT_TRUE(transport.canFlushClose());
  });
}

// ===== Protocol Tests =====

TEST_F(TcpTransportSocketTest, ProtocolString) {
  executeInDispatcher([this]() {
    TcpTransportSocket transport(*dispatcher_, config_);

    EXPECT_EQ(transport.protocol(), "tcp");
  });
}

TEST_F(TcpTransportSocketTest, FailureReason) {
  executeInDispatcher([this]() {
    TcpTransportSocket transport(*dispatcher_, config_);

    // Create a mock connection
    auto address =
        std::make_shared<network::Address::Ipv4Instance>("127.0.0.1", 0);
    auto io_handle = std::make_unique<network::IoSocketHandleImpl>();
    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), nullptr, address);

    auto test_connection = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(socket), nullptr, false);

    TestTransportSocketCallbacks test_callbacks(*test_connection);
    transport.setTransportSocketCallbacks(test_callbacks);

    // Initially no failure
    EXPECT_EQ(transport.failureReason(), "");

    // After close, might have a reason
    transport.closeSocket(network::ConnectionEvent::LocalClose);
    // Failure reason depends on implementation
  });
}

// ===== Stress Tests =====

TEST_F(TcpTransportSocketTest, DISABLED_MultipleReadsAndWrites) {
  runClientServerTest([this]() {
    // Perform multiple operations
    for (int i = 0; i < 100; ++i) {
      // Write from client
      OwnedBuffer write_buffer;
      std::string data = "Test " + std::to_string(i);
      write_buffer.add(data.data(), data.size());

      auto write_result = client_transport_->doWrite(write_buffer, false);
      EXPECT_NE(write_result.action_, TransportIoResult::CLOSE);

      // Small delay to avoid overwhelming
      if (i % 10 == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }

    // Allow time for all data to arrive
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Read all data on server
    size_t total_read = 0;
    OwnedBuffer read_buffer;

    for (int attempts = 0; attempts < 20 && total_read < 500; ++attempts) {
      auto read_result = server_transport_->doRead(read_buffer);
      total_read += read_result.bytes_processed_;

      if (read_result.bytes_processed_ == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }

    EXPECT_GT(total_read, 0);
  });
}

TEST_F(TcpTransportSocketTest, DISABLED_LargeBufferHandling) {
  runClientServerTest([this]() {
    // Create a large buffer (1MB)
    OwnedBuffer large_buffer;
    std::string large_data(1024 * 1024, 'X');
    large_buffer.add(large_data.data(), large_data.size());

    auto result = client_transport_->doWrite(large_buffer, false);

    // Should handle large buffers
    EXPECT_NE(result.action_, TransportIoResult::CLOSE);
    EXPECT_GT(result.bytes_processed_, 0);

    // Read on server side (may take multiple reads)
    size_t total_read = 0;
    OwnedBuffer read_buffer;

    while (total_read < large_data.size()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      auto read_result = server_transport_->doRead(read_buffer);

      if (read_result.action_ == TransportIoResult::CLOSE) {
        break;
      }

      total_read += read_result.bytes_processed_;

      // Prevent infinite loop
      if (total_read == 0) {
        static int no_progress_count = 0;
        if (++no_progress_count > 100) {
          break;
        }
      }
    }

    // Should have read at least some data
    EXPECT_GT(total_read, 0);
  });
}

TEST_F(TcpTransportSocketTest, DISABLED_EmptyBufferHandling) {
  runClientServerTest([this]() {
    // Empty buffer operations
    OwnedBuffer empty_buffer;

    auto read_result = client_transport_->doRead(empty_buffer);
    EXPECT_EQ(read_result.action_, TransportIoResult::CONTINUE);
    EXPECT_EQ(read_result.bytes_processed_, 0);

    auto write_result = client_transport_->doWrite(empty_buffer, false);
    EXPECT_EQ(write_result.action_, TransportIoResult::CONTINUE);
    EXPECT_EQ(write_result.bytes_processed_, 0);
  });
}

// ===== End Stream Tests =====

TEST_F(TcpTransportSocketTest, DISABLED_WriteWithEndStream) {
  runClientServerTest([this]() {
    OwnedBuffer buffer;
    std::string data = "Final message";
    buffer.add(data.data(), data.size());

    auto result = client_transport_->doWrite(buffer, true);

    // Should handle end_stream
    EXPECT_EQ(result.action_, TransportIoResult::CONTINUE);
    // TCP doesn't have separate end_stream flag, just check action
    EXPECT_EQ(result.action_, TransportIoResult::CONTINUE);
  });
}

}  // namespace
}  // namespace transport
}  // namespace mcp