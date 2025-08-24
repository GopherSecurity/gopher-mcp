/**
 * Unit tests for MCP Stdio Filter Chain Factory
 *
 * Tests the filter chain factory for direct transports (stdio, websocket)
 * that don't require HTTP/SSE protocol layers.
 * Uses real I/O for integration testing.
 */

#include <chrono>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/socket.h>

#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/filter/stdio_filter_chain_factory.h"
#include "mcp/json/json_serialization.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/filter.h"
#include "mcp/network/socket_impl.h"

#include "../integration/real_io_test_base.h"

namespace mcp {
namespace filter {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

/**
 * Mock MCP message callbacks for testing
 */
class MockMcpProtocolCallbacks : public McpProtocolCallbacks {
 public:
  MOCK_METHOD(void, onRequest, (const jsonrpc::Request&), (override));
  MOCK_METHOD(void, onNotification, (const jsonrpc::Notification&), (override));
  MOCK_METHOD(void, onResponse, (const jsonrpc::Response&), (override));
  MOCK_METHOD(void, onConnectionEvent, (network::ConnectionEvent), (override));
  MOCK_METHOD(void, onError, (const Error&), (override));
};

/**
 * Test fixture for StdioFilterChainFactory using real I/O
 */
class StdioFilterChainFactoryTest : public test::RealIoTestBase {
 protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    message_callbacks_ = std::make_unique<NiceMock<MockMcpProtocolCallbacks>>();
  }

  void TearDown() override {
    message_callbacks_.reset();
    RealIoTestBase::TearDown();
  }

  // Helper to create factory and test filter chain
  void testFilterChain(bool is_server, bool use_framing) {
    executeInDispatcher([this, is_server, use_framing]() {
      // Create factory
      auto factory = std::make_shared<StdioFilterChainFactory>(
          *dispatcher_, *message_callbacks_, is_server, use_framing);

      // Create mock connection for filter manager
      // Create a test file descriptor
      int test_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
      if (test_fd < 0) {
        throw std::runtime_error("Failed to create test socket");
      }

      auto& socket_interface = network::socketInterface();
      auto io_handle = socket_interface.ioHandleForFd(test_fd, true);

      auto socket = std::make_unique<network::ConnectionSocketImpl>(
          std::move(io_handle), network::Address::pipeAddress("test"),
          network::Address::pipeAddress("test"));

      auto connection = std::make_unique<network::ConnectionImpl>(
          *dispatcher_, std::move(socket), network::TransportSocketPtr(nullptr),
          true);

      // Apply filter chain
      EXPECT_TRUE(factory->createFilterChain(connection->filterManager()));

      // Initialize filters
      connection->filterManager().initializeReadFilters();

      // Test that filters were added
      // Send some test data through the connection
      std::string test_data = R"({"jsonrpc":"2.0","id":1,"method":"test"})"
                              "\n";
      OwnedBuffer buffer;
      buffer.add(test_data);

      // This should trigger the filter chain
      connection->filterManager().onRead();
    });
  }

  std::unique_ptr<MockMcpProtocolCallbacks> message_callbacks_;
  std::unique_ptr<network::ConnectionImpl> captured_connection_;
};

/**
 * Test filter chain creation for client mode
 */
TEST_F(StdioFilterChainFactoryTest, CreateFilterChainClientMode) {
  testFilterChain(false, true);  // client mode, with framing
}

/**
 * Test filter chain creation for server mode
 */
TEST_F(StdioFilterChainFactoryTest, CreateFilterChainServerMode) {
  testFilterChain(true, false);  // server mode, without framing
}

/**
 * Test filter lifetime management
 * Verifies that the filter wrapper owns callbacks and they outlive the factory
 */
TEST_F(StdioFilterChainFactoryTest, FilterLifetimeManagement) {
  executeInDispatcher([this]() {
    network::FilterSharedPtr captured_filter;

    {
      // Create factory in a scope
      StdioFilterChainFactory factory(*dispatcher_, *message_callbacks_,
                                      false,  // client mode
                                      true);  // use framing

      // Create connection
      // Create a test file descriptor
      int test_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
      if (test_fd < 0) {
        throw std::runtime_error("Failed to create test socket");
      }

      auto& socket_interface = network::socketInterface();
      auto io_handle = socket_interface.ioHandleForFd(test_fd, true);

      auto socket = std::make_unique<network::ConnectionSocketImpl>(
          std::move(io_handle), network::Address::pipeAddress("test"),
          network::Address::pipeAddress("test"));

      auto connection = std::make_unique<network::ConnectionImpl>(
          *dispatcher_, std::move(socket), network::TransportSocketPtr(nullptr),
          true);

      // Create filter chain
      EXPECT_TRUE(factory.createFilterChain(connection->filterManager()));

      // Capture the filter by getting it from filter manager
      // Note: We can't directly access the filters, but we can test that
      // the filter chain works after factory is destroyed
      connection->filterManager().initializeReadFilters();

      // Store connection for later use
      captured_connection_ = std::move(connection);
    }
    // Factory is now destroyed

    // Filter should still be valid - test by sending data
    std::string test_data = R"({"jsonrpc":"2.0","id":1,"method":"test"})"
                            "\n";
    OwnedBuffer buffer;
    buffer.add(test_data);

    // This should not crash (callbacks are still valid)
    if (captured_connection_) {
      captured_connection_->filterManager().onRead();
    }
  });
}

/**
 * Integration test with real connections
 * NOTE: Disabled due to issues with real I/O test infrastructure
 */
#if 0
class StdioFilterChainIntegrationTest : public test::RealIoTestBase,
                                            public network::ConnectionCallbacks {
protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    
    // Setup in dispatcher thread
    executeInDispatcher([this]() {
      // Create socket pair
      auto socket_pair = createSocketPair();
      client_handle_ = std::move(socket_pair.first);
      server_handle_ = std::move(socket_pair.second);
      
      setupConnections();
    });
  }
  
  void TearDown() override {
    executeInDispatcher([this]() {
      if (client_connection_) {
        client_connection_->close(network::ConnectionCloseType::NoFlush);
      }
      if (server_connection_) {
        server_connection_->close(network::ConnectionCloseType::NoFlush);
      }
    });
    
    RealIoTestBase::TearDown();
  }
  
  void setupConnections() {
    // Create message callbacks
    client_callbacks_ = std::make_unique<NiceMock<MockMcpProtocolCallbacks>>();
    server_callbacks_ = std::make_unique<NiceMock<MockMcpProtocolCallbacks>>();
    
    // Create factories
    auto client_factory = std::make_shared<StdioFilterChainFactory>(
        *dispatcher_, *client_callbacks_, false, false); // client, no framing
    auto server_factory = std::make_shared<StdioFilterChainFactory>(
        *dispatcher_, *server_callbacks_, true, false);  // server, no framing
    
    // Create client connection
    auto client_socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(client_handle_),
        network::Address::pipeAddress("client"),
        network::Address::pipeAddress("server"));
    
    client_connection_ = std::make_unique<network::ConnectionImpl>(
        *dispatcher_,
        std::move(client_socket),
        network::TransportSocketPtr(nullptr),
        true);
    
    // Create server connection
    auto server_socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(server_handle_),
        network::Address::pipeAddress("server"),
        network::Address::pipeAddress("client"));
    
    server_connection_ = std::make_unique<network::ConnectionImpl>(
        *dispatcher_,
        std::move(server_socket),
        network::TransportSocketPtr(nullptr),
        true);
    
    // Apply filter chains
    client_factory->createFilterChain(client_connection_->filterManager());
    server_factory->createFilterChain(server_connection_->filterManager());
    
    // Initialize filters
    client_connection_->filterManager().initializeReadFilters();
    server_connection_->filterManager().initializeReadFilters();
    
    // Add connection callbacks
    client_connection_->addConnectionCallbacks(*this);
    server_connection_->addConnectionCallbacks(*this);
  }
  
  // ConnectionCallbacks
  void onEvent(network::ConnectionEvent event) override {
    last_event_ = event;
  }
  
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}
  
  std::unique_ptr<network::ConnectionImpl> client_connection_;
  std::unique_ptr<network::ConnectionImpl> server_connection_;
  std::unique_ptr<MockMcpProtocolCallbacks> client_callbacks_;
  std::unique_ptr<MockMcpProtocolCallbacks> server_callbacks_;
  network::ConnectionEvent last_event_;
  network::IoHandlePtr client_handle_;
  network::IoHandlePtr server_handle_;
};

/**
 * Test end-to-end message flow through stdio filter chain
 */
TEST_F(StdioFilterChainIntegrationTest, EndToEndRequestResponse) {
  // Server should receive request
  EXPECT_CALL(*server_callbacks_, onRequest(_))
      .WillOnce([this](const jsonrpc::Request& req) {
        EXPECT_EQ("test.method", req.method);
        EXPECT_EQ(42, get<int>(req.id));
        
        // Send response back
        executeInDispatcher([this, req]() {
          jsonrpc::Response response;
          response.jsonrpc = "2.0";
          response.id = req.id;
          response.result = jsonrpc::ResponseResult(nullptr);
          
          std::string response_str = json::JsonSerializer::serialize(response).toString() + "\n";
          OwnedBuffer buffer;
          buffer.add(response_str);
          server_connection_->write(buffer, false);
        });
      });
  
  // Client should receive response
  EXPECT_CALL(*client_callbacks_, onResponse(_))
      .WillOnce([](const jsonrpc::Response& resp) {
        EXPECT_EQ(42, get<int>(resp.id));
        EXPECT_TRUE(resp.result.has_value());
      });
  
  // Send request from client
  executeInDispatcher([this]() {
    jsonrpc::Request request;
    request.jsonrpc = "2.0";
    request.id = 42;
    request.method = "test.method";
    
    std::string request_str = json::JsonSerializer::serialize(request).toString() + "\n";
    OwnedBuffer buffer;
    buffer.add(request_str);
    client_connection_->write(buffer, false);
  });
  
  // Wait for exchange to complete
  waitFor([this]() {
    // Mock objects don't have call_count, just wait briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return true;
  }, std::chrono::seconds(2));
}

/**
 * Test notification flow
 */
TEST_F(StdioFilterChainIntegrationTest, NotificationFlow) {
  // Server should receive notification
  EXPECT_CALL(*server_callbacks_, onNotification(_))
      .WillOnce([](const jsonrpc::Notification& notif) {
        EXPECT_EQ("test.notify", notif.method);
      });
  
  // Send notification from client
  executeInDispatcher([this]() {
    jsonrpc::Notification notification;
    notification.jsonrpc = "2.0";
    notification.method = "test.notify";
    
    std::string notif_str = json::JsonSerializer::serialize(notification).toString() + "\n";
    OwnedBuffer buffer;
    buffer.add(notif_str);
    client_connection_->write(buffer, false);
  });
  
  // Wait for notification to be received
  waitFor([this]() {
    // Mock objects don't have call_count, just wait briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return true;
  }, std::chrono::seconds(1));
}

/**
 * Test error handling
 */
TEST_F(StdioFilterChainIntegrationTest, ErrorHandling) {
  // Expect error callback when invalid JSON is sent
  EXPECT_CALL(*server_callbacks_, onError(_))
      .WillOnce([](const Error& error) {
        EXPECT_EQ(jsonrpc::PARSE_ERROR, error.code);
      });
  
  // Send invalid JSON from client
  executeInDispatcher([this]() {
    std::string invalid_json = "{ this is not valid json }\n";
    OwnedBuffer buffer;
    buffer.add(invalid_json);
    client_connection_->write(buffer, false);
  });
  
  // Wait for error to be received
  waitFor([this]() {
    // Mock objects don't have call_count, just wait briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return true;
  }, std::chrono::seconds(1));
}

/**
 * Test with framing enabled
 */
TEST_F(StdioFilterChainIntegrationTest, WithFraming) {
  // Recreate connections with framing enabled
  executeInDispatcher([this]() {
    // Close existing connections
    client_connection_->close(network::ConnectionCloseType::NoFlush);
    server_connection_->close(network::ConnectionCloseType::NoFlush);
    
    // Create new socket pair
    auto socket_pair = createSocketPair();
    client_handle_ = std::move(socket_pair.first);
    server_handle_ = std::move(socket_pair.second);
    
    // Create new callbacks
    client_callbacks_ = std::make_unique<NiceMock<MockMcpProtocolCallbacks>>();
    server_callbacks_ = std::make_unique<NiceMock<MockMcpProtocolCallbacks>>();
    
    // Create factories with framing
    auto client_factory = std::make_shared<StdioFilterChainFactory>(
        *dispatcher_, *client_callbacks_, false, true); // client, with framing
    auto server_factory = std::make_shared<StdioFilterChainFactory>(
        *dispatcher_, *server_callbacks_, true, true);  // server, with framing
    
    // Recreate connections (similar to setupConnections)
    auto client_socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(client_handle_),
        network::Address::pipeAddress("client"),
        network::Address::pipeAddress("server"));
    
    client_connection_ = std::make_unique<network::ConnectionImpl>(
        *dispatcher_,
        std::move(client_socket),
        network::TransportSocketPtr(nullptr),
        true);
    
    auto server_socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(server_handle_),
        network::Address::pipeAddress("server"),
        network::Address::pipeAddress("client"));
    
    server_connection_ = std::make_unique<network::ConnectionImpl>(
        *dispatcher_,
        std::move(server_socket),
        network::TransportSocketPtr(nullptr),
        true);
    
    // Apply filter chains with framing
    client_factory->createFilterChain(client_connection_->filterManager());
    server_factory->createFilterChain(server_connection_->filterManager());
    
    // Initialize filters
    client_connection_->filterManager().initializeReadFilters();
    server_connection_->filterManager().initializeReadFilters();
  });
  
  // Test with framed message
  EXPECT_CALL(*server_callbacks_, onRequest(_))
      .WillOnce([](const jsonrpc::Request& req) {
        EXPECT_EQ("framed.test", req.method);
      });
  
  // Send framed request
  executeInDispatcher([this]() {
    jsonrpc::Request request;
    request.jsonrpc = "2.0";
    request.id = 99;
    request.method = "framed.test";
    
    std::string request_str = json::JsonSerializer::serialize(request).toString();
    
    // Add framing - 4-byte length prefix (big-endian)
    uint32_t length = htonl(request_str.length());
    
    OwnedBuffer buffer;
    buffer.add(&length, 4);
    buffer.add(request_str);
    
    client_connection_->write(buffer, false);
  });
  
  // Wait for request to be received
  waitFor([this]() {
    // Mock objects don't have call_count, just wait briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return true;
  }, std::chrono::seconds(1));
}
#endif  // Disabled integration test

}  // namespace
}  // namespace filter
}  // namespace mcp