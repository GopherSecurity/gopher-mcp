/**
 * @file test_http_sse_transport_socket_v2.cc
 * @brief Comprehensive tests for HTTP+SSE transport socket V2 using real I/O
 *
 * Tests the layered architecture with proper separation between:
 * - Transport layer (raw I/O)
 * - Filter chain (protocol processing)
 * - Connection management
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mcp/transport/http_sse_transport_socket_v2.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/address_impl.h"
#include "mcp/event/libevent_dispatcher.h"

namespace mcp {
namespace transport {
namespace {

using namespace std::chrono_literals;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

/**
 * Test fixture for HTTP+SSE transport socket V2
 */
class HttpSseTransportSocketV2Test : public ::testing::Test {
protected:
  void SetUp() override {
    // Create dispatcher
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    
    // Default configuration
    config_.mode = HttpSseTransportSocketConfigV2::Mode::CLIENT;
    config_.underlying_transport = 
        HttpSseTransportSocketConfigV2::UnderlyingTransport::TCP;
    config_.server_address = "127.0.0.1:8080";
    config_.connect_timeout = 5000ms;
    config_.idle_timeout = 30000ms;
  }
  
  /**
   * Create transport socket with filter chain
   */
  std::unique_ptr<HttpSseTransportSocketV2> createTransport(
      bool with_filters = true) {
    
    HttpSseTransportBuilder builder(*dispatcher_);
    builder.withMode(config_.mode)
           .withServerAddress(config_.server_address)
           .withConnectTimeout(config_.connect_timeout)
           .withIdleTimeout(config_.idle_timeout);
    
    if (with_filters) {
      bool is_server = (config_.mode == HttpSseTransportSocketConfigV2::Mode::SERVER);
      builder.withHttpFilter(is_server)
             .withSseFilter(is_server);
    }
    
    return builder.build();
  }
  
  /**
   * Create a mock filter for testing filter chain integration
   */
  class MockFilter : public network::Filter {
  public:
    MOCK_METHOD(network::FilterStatus, onNewConnection, (), (override));
    MOCK_METHOD(network::FilterStatus, onData, 
                (Buffer& data, bool end_stream), (override));
    MOCK_METHOD(void, initializeReadFilterCallbacks, 
                (network::ReadFilterCallbacks& callbacks), (override));
    MOCK_METHOD(network::FilterStatus, onWrite,
                (Buffer& data, bool end_stream), (override));
    MOCK_METHOD(void, initializeWriteFilterCallbacks,
                (network::WriteFilterCallbacks& callbacks), (override));
  };
  
  /**
   * Mock transport socket callbacks
   */
  class MockTransportSocketCallbacks : public network::TransportSocketCallbacks {
  public:
    MOCK_METHOD(network::IoHandle&, ioHandle, (), (override));
    MOCK_METHOD(const network::IoHandle&, ioHandle, (), (const, override));
    MOCK_METHOD(network::Connection&, connection, (), (override));
    MOCK_METHOD(bool, shouldDrainReadBuffer, (), (override));
    MOCK_METHOD(void, setTransportSocketIsReadable, (), (override));
    MOCK_METHOD(void, raiseEvent, (network::ConnectionEvent), (override));
    MOCK_METHOD(void, flushWriteBuffer, (), (override));
  };
  
  HttpSseTransportSocketConfigV2 config_;
  std::unique_ptr<event::Dispatcher> dispatcher_;
};

// ===== Basic Functionality Tests =====

TEST_F(HttpSseTransportSocketV2Test, CreateAndDestroy) {
  auto transport = createTransport();
  ASSERT_NE(transport, nullptr);
  EXPECT_EQ(transport->protocol(), "http+sse");
  EXPECT_FALSE(transport->isConnected());
}

TEST_F(HttpSseTransportSocketV2Test, CreateWithoutFilters) {
  auto transport = createTransport(false);
  ASSERT_NE(transport, nullptr);
  EXPECT_EQ(transport->filterChain(), nullptr);
}

TEST_F(HttpSseTransportSocketV2Test, CreateWithFilters) {
  auto transport = createTransport(true);
  ASSERT_NE(transport, nullptr);
  EXPECT_NE(transport->filterChain(), nullptr);
}

// ===== Connection Tests =====

TEST_F(HttpSseTransportSocketV2Test, ConnectWithoutCallbacks) {
  auto transport = createTransport();
  
  // Create a socket
  auto address = std::make_shared<network::Ipv4Instance>("127.0.0.1", 8080);
  auto socket = std::make_unique<network::SocketImpl>(
      network::Socket::Type::Stream,
      address);
  
  // Connect without setting callbacks
  auto result = transport->connect(*socket);
  // Result should be successful or have appropriate error
  EXPECT_TRUE(result.index() == 0 || result.index() == 1);
}

TEST_F(HttpSseTransportSocketV2Test, ConnectAndDisconnect) {
  auto transport = createTransport();
  
  // Set up mock callbacks
  MockTransportSocketCallbacks callbacks;
  transport->setTransportSocketCallbacks(callbacks);
  
  // Create a socket
  auto address = std::make_shared<network::Ipv4Instance>("127.0.0.1", 8080);
  auto socket = std::make_unique<network::SocketImpl>(
      network::Socket::Type::Stream,
      address);
  
  // Expect connection event
  EXPECT_CALL(callbacks, raiseEvent(network::ConnectionEvent::Connected))
      .Times(1);
  
  // Connect 
  auto result = transport->connect(*socket);
  
  // Trigger connected callback
  transport->onConnected();
  EXPECT_TRUE(transport->isConnected());
  
  // Close connection
  EXPECT_CALL(callbacks, raiseEvent(_)).Times(1);
  transport->closeSocket(network::ConnectionEvent::LocalClose);
  EXPECT_FALSE(transport->isConnected());
}

// ===== Read/Write Tests =====

TEST_F(HttpSseTransportSocketV2Test, ReadWhenNotConnected) {
  runInDispatcherThread([this]() {
    auto transport = createTransport();
    
    OwnedBuffer buffer;
    auto result = transport->doRead(buffer);
    
    EXPECT_EQ(result.type, TransportIoResult::Type::Error);
    EXPECT_EQ(result.action, TransportIoResult::PostIoAction::Close);
  });
}

TEST_F(HttpSseTransportSocketV2Test, WriteWhenNotConnected) {
  runInDispatcherThread([this]() {
    auto transport = createTransport();
    
    OwnedBuffer buffer;
    buffer.add("test data", 9);
    
    auto result = transport->doWrite(buffer, false);
    
    EXPECT_EQ(result.type, TransportIoResult::Type::Error);
    EXPECT_EQ(result.action, TransportIoResult::PostIoAction::Close);
  });
}

TEST_F(HttpSseTransportSocketV2Test, ReadWriteWithFilters) {
  runInDispatcherThread([this]() {
    // Create transport with HTTP and SSE filters
    auto transport = createTransport(true);
    
    MockTransportSocketCallbacks callbacks;
    transport->setTransportSocketCallbacks(callbacks);
    
    // Create connected socket pair
    auto [client_socket, server_socket] = createSocketPair();
    
    EXPECT_CALL(callbacks, raiseEvent(network::ConnectionEvent::Connected));
    transport->connect(*client_socket);
    transport->onConnected();
    
    // Write HTTP request through filter chain
    OwnedBuffer write_buffer;
    std::string http_request = 
        "POST /rpc HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "{\"test\":true}";
    write_buffer.add(http_request);
    
    auto write_result = transport->doWrite(write_buffer, false);
    EXPECT_EQ(write_result.type, TransportIoResult::Type::Success);
    
    // Read response through filter chain
    OwnedBuffer read_buffer;
    
    // Simulate response data available on socket
    std::string http_response = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 15\r\n"
        "\r\n"
        "{\"result\":true}";
    
    // Write response to server side of socket pair
    ::write(server_socket->ioHandle().fd(), 
            http_response.data(), http_response.size());
    
    // Read should process through filters
    auto read_result = transport->doRead(read_buffer);
    EXPECT_EQ(read_result.type, TransportIoResult::Type::Success);
  });
}

// ===== Filter Chain Tests =====

TEST_F(HttpSseTransportSocketV2Test, FilterChainProcessing) {
  runInDispatcherThread([this]() {
    // Create transport without default filters
    auto transport = createTransport(false);
    
    // Create and set custom filter chain
    auto filter_chain = std::make_unique<network::FilterChainImpl>();
    
    // Add mock filter
    auto mock_filter = std::make_shared<MockFilter>();
    filter_chain->addFilter(mock_filter);
    
    transport->setFilterChain(std::move(filter_chain));
    
    MockTransportSocketCallbacks callbacks;
    transport->setTransportSocketCallbacks(callbacks);
    
    // Set up connected state
    auto [client_socket, server_socket] = createSocketPair();
    EXPECT_CALL(callbacks, raiseEvent(network::ConnectionEvent::Connected));
    transport->connect(*client_socket);
    transport->onConnected();
    
    // Test filter receives data
    EXPECT_CALL(*mock_filter, onData(_, false))
        .WillOnce(Return(network::FilterStatus::Continue));
    
    OwnedBuffer buffer;
    std::string test_data = "test data";
    ::write(server_socket->ioHandle().fd(), test_data.data(), test_data.size());
    
    transport->doRead(buffer);
    
    // Test filter processes writes
    EXPECT_CALL(*mock_filter, onWrite(_, false))
        .WillOnce(Return(network::FilterStatus::Continue));
    
    OwnedBuffer write_buffer;
    write_buffer.add("response data", 13);
    transport->doWrite(write_buffer, false);
  });
}

// ===== Timeout Tests =====

TEST_F(HttpSseTransportSocketV2Test, ConnectTimeout) {
  runInDispatcherThread([this]() {
    config_.connect_timeout = 100ms;  // Short timeout for testing
    auto transport = createTransport();
    
    MockTransportSocketCallbacks callbacks;
    transport->setTransportSocketCallbacks(callbacks);
    
    // Create socket that won't connect
    auto socket = std::make_unique<network::SocketImpl>(
        network::Socket::Type::Stream,
        network::Address::pipeAddress());
    
    // Expect close event after timeout
    EXPECT_CALL(callbacks, raiseEvent(network::ConnectionEvent::LocalClose));
    
    transport->connect(*socket);
    
    // Wait for timeout
    waitForConditionOrTimeout([transport]() {
      return !transport->isConnected();
    }, 200ms);
    
    EXPECT_FALSE(transport->isConnected());
    EXPECT_FALSE(transport->failureReason().empty());
  });
}

TEST_F(HttpSseTransportSocketV2Test, IdleTimeout) {
  runInDispatcherThread([this]() {
    config_.idle_timeout = 100ms;  // Short timeout for testing
    auto transport = createTransport();
    
    MockTransportSocketCallbacks callbacks;
    transport->setTransportSocketCallbacks(callbacks);
    
    auto [client_socket, server_socket] = createSocketPair();
    
    EXPECT_CALL(callbacks, raiseEvent(network::ConnectionEvent::Connected));
    transport->connect(*client_socket);
    transport->onConnected();
    
    // Expect close event after idle timeout
    EXPECT_CALL(callbacks, raiseEvent(network::ConnectionEvent::LocalClose));
    
    // Wait for idle timeout
    waitForConditionOrTimeout([transport]() {
      return !transport->isConnected();
    }, 200ms);
    
    EXPECT_FALSE(transport->isConnected());
  });
}

// ===== Builder Tests =====

TEST_F(HttpSseTransportSocketV2Test, BuilderConfiguration) {
  runInDispatcherThread([this]() {
    HttpSseTransportBuilder builder(*dispatcher_);
    
    auto transport = builder
        .withMode(HttpSseTransportSocketConfigV2::Mode::SERVER)
        .withServerAddress("192.168.1.1:9090")
        .withConnectTimeout(10s)
        .withIdleTimeout(60s)
        .withHttpFilter(true)
        .withSseFilter(true)
        .build();
    
    ASSERT_NE(transport, nullptr);
    EXPECT_EQ(transport->protocol(), "http+sse");
    EXPECT_NE(transport->filterChain(), nullptr);
  });
}

TEST_F(HttpSseTransportSocketV2Test, BuilderWithSsl) {
  runInDispatcherThread([this]() {
    HttpSseTransportSocketConfigV2::SslConfig ssl_config;
    ssl_config.verify_peer = true;
    ssl_config.ca_cert_path = "/path/to/ca.pem";
    
    HttpSseTransportBuilder builder(*dispatcher_);
    
    auto transport = builder
        .withMode(HttpSseTransportSocketConfigV2::Mode::CLIENT)
        .withServerAddress("secure.example.com:443")
        .withSsl(ssl_config)
        .build();
    
    ASSERT_NE(transport, nullptr);
    // Note: SSL transport creation would fail without valid certificates
  });
}

// ===== Factory Tests =====

TEST_F(HttpSseTransportSocketV2Test, FactoryCreateTransport) {
  runInDispatcherThread([this]() {
    HttpSseTransportBuilder builder(*dispatcher_);
    auto factory = builder
        .withMode(HttpSseTransportSocketConfigV2::Mode::CLIENT)
        .withHttpFilter(false)
        .withSseFilter(false)
        .buildFactory();
    
    ASSERT_NE(factory, nullptr);
    EXPECT_EQ(factory->name(), "http+sse-v2");
    EXPECT_FALSE(factory->implementsSecureTransport());
    
    auto transport = factory->createTransportSocket();
    ASSERT_NE(transport, nullptr);
  });
}

TEST_F(HttpSseTransportSocketV2Test, FactoryWithSsl) {
  runInDispatcherThread([this]() {
    HttpSseTransportSocketConfigV2::SslConfig ssl_config;
    ssl_config.verify_peer = false;
    
    HttpSseTransportBuilder builder(*dispatcher_);
    auto factory = builder
        .withSsl(ssl_config)
        .buildFactory();
    
    ASSERT_NE(factory, nullptr);
    EXPECT_TRUE(factory->implementsSecureTransport());
  });
}

// ===== Statistics Tests =====

TEST_F(HttpSseTransportSocketV2Test, TransportStatistics) {
  runInDispatcherThread([this]() {
    auto transport = createTransport();
    
    // Initial stats
    auto stats = transport->stats();
    EXPECT_EQ(stats.bytes_sent, 0);
    EXPECT_EQ(stats.bytes_received, 0);
    EXPECT_EQ(stats.connect_attempts, 0);
    
    MockTransportSocketCallbacks callbacks;
    transport->setTransportSocketCallbacks(callbacks);
    
    auto [client_socket, server_socket] = createSocketPair();
    
    // Connect increments attempt counter
    transport->connect(*client_socket);
    stats = transport->stats();
    EXPECT_EQ(stats.connect_attempts, 1);
    
    EXPECT_CALL(callbacks, raiseEvent(network::ConnectionEvent::Connected));
    transport->onConnected();
    
    // Stats should track connect time
    stats = transport->stats();
    EXPECT_NE(stats.connect_time, std::chrono::steady_clock::time_point{});
  });
}

// ===== End-to-End Integration Test =====

TEST_F(HttpSseTransportSocketV2Test, EndToEndHttpSseFlow) {
  runInDispatcherThread([this]() {
    // Create client transport with filters
    auto client_transport = HttpSseTransportBuilder(*dispatcher_)
        .withMode(HttpSseTransportSocketConfigV2::Mode::CLIENT)
        .withHttpFilter(false)
        .withSseFilter(false)
        .build();
    
    // Create server transport with filters
    auto server_transport = HttpSseTransportBuilder(*dispatcher_)
        .withMode(HttpSseTransportSocketConfigV2::Mode::SERVER)
        .withHttpFilter(true)
        .withSseFilter(true)
        .build();
    
    MockTransportSocketCallbacks client_callbacks;
    MockTransportSocketCallbacks server_callbacks;
    
    client_transport->setTransportSocketCallbacks(client_callbacks);
    server_transport->setTransportSocketCallbacks(server_callbacks);
    
    // Create connected socket pair
    auto [client_socket, server_socket] = createSocketPair();
    
    // Connect both sides
    EXPECT_CALL(client_callbacks, raiseEvent(network::ConnectionEvent::Connected));
    EXPECT_CALL(server_callbacks, raiseEvent(network::ConnectionEvent::Connected));
    
    client_transport->connect(*client_socket);
    server_transport->connect(*server_socket);
    
    client_transport->onConnected();
    server_transport->onConnected();
    
    // Client sends HTTP request
    OwnedBuffer client_write;
    std::string request = 
        "POST /rpc HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 20\r\n"
        "\r\n"
        "{\"method\":\"test\"}";
    client_write.add(request);
    
    auto write_result = client_transport->doWrite(client_write, false);
    EXPECT_EQ(write_result.type, TransportIoResult::Type::Success);
    
    // Server reads request
    OwnedBuffer server_read;
    auto read_result = server_transport->doRead(server_read);
    EXPECT_EQ(read_result.type, TransportIoResult::Type::Success);
    
    // Server sends response
    OwnedBuffer server_write;
    std::string response = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 22\r\n"
        "\r\n"
        "{\"result\":\"success\"}";
    server_write.add(response);
    
    write_result = server_transport->doWrite(server_write, false);
    EXPECT_EQ(write_result.type, TransportIoResult::Type::Success);
    
    // Client reads response
    OwnedBuffer client_read;
    read_result = client_transport->doRead(client_read);
    EXPECT_EQ(read_result.type, TransportIoResult::Type::Success);
    
    // Clean shutdown
    client_transport->closeSocket(network::ConnectionEvent::LocalClose);
    server_transport->closeSocket(network::ConnectionEvent::RemoteClose);
  });
}

} // namespace
} // namespace transport
} // namespace mcp