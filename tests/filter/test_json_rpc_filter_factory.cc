/**
 * Unit tests for JsonRpcFilterChainFactory
 *
 * Tests the factory that creates JSON-RPC filter chains for direct
 * JSON-RPC processing without HTTP/SSE layers.
 */

#include <chrono>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/filter/json_rpc_filter_factory.h"
#include "mcp/json/json_serialization.h"
#include "mcp/network/connection_impl.h"
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
class MockMcpMessageCallbacks : public McpMessageCallbacks {
 public:
  MOCK_METHOD(void, onRequest, (const jsonrpc::Request&), (override));
  MOCK_METHOD(void, onNotification, (const jsonrpc::Notification&), (override));
  MOCK_METHOD(void, onResponse, (const jsonrpc::Response&), (override));
  MOCK_METHOD(void, onConnectionEvent, (network::ConnectionEvent), (override));
  MOCK_METHOD(void, onError, (const Error&), (override));
};

/**
 * Test fixture for JsonRpcFilterChainFactory using real I/O
 */
class JsonRpcFilterChainFactoryTest : public test::RealIoTestBase {
 protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    message_callbacks_ = std::make_unique<NiceMock<MockMcpMessageCallbacks>>();
  }

  void TearDown() override {
    message_callbacks_.reset();
    RealIoTestBase::TearDown();
  }

  // Helper to create factory and test filter chain
  void testFilterChain(bool use_framing) {
    executeInDispatcher([this, use_framing]() {
      // Create factory
      auto factory = std::make_shared<JsonRpcFilterChainFactory>(
          *dispatcher_, *message_callbacks_, use_framing);

      // Create test connection
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

      // Test that filters were added properly
      // Send test data through the connection
      std::string test_data;
      if (use_framing) {
        // With framing - add 4-byte length prefix
        std::string json_str = R"({"jsonrpc":"2.0","id":1,"method":"test"})";
        uint32_t length = htonl(json_str.length());
        test_data.append(reinterpret_cast<char*>(&length), 4);
        test_data.append(json_str);
      } else {
        // Without framing - use newline delimiter
        test_data = R"({"jsonrpc":"2.0","id":1,"method":"test"})"
                    "\n";
      }

      OwnedBuffer buffer;
      buffer.add(test_data);

      // This should trigger the filter chain
      connection->filterManager().onRead();
    });
  }

  std::unique_ptr<MockMcpMessageCallbacks> message_callbacks_;
  std::unique_ptr<network::ConnectionImpl> captured_connection_;
};

/**
 * Test filter chain creation with framing
 */
TEST_F(JsonRpcFilterChainFactoryTest, CreateFilterChainWithFraming) {
  testFilterChain(true);
}

/**
 * Test filter chain creation without framing
 */
TEST_F(JsonRpcFilterChainFactoryTest, CreateFilterChainWithoutFraming) {
  testFilterChain(false);
}

/**
 * Test DirectJsonRpcCallbacks adapter
 */
TEST_F(JsonRpcFilterChainFactoryTest, DirectCallbacksAdapter) {
  executeInDispatcher([this]() {
    // Create adapter
    DirectJsonRpcCallbacks adapter(*message_callbacks_);

    // Test request forwarding
    jsonrpc::Request request;
    request.jsonrpc = "2.0";
    request.id = 123;
    request.method = "test.method";

    EXPECT_CALL(*message_callbacks_, onRequest(_))
        .WillOnce([](const jsonrpc::Request& req) {
          EXPECT_EQ("test.method", req.method);
          EXPECT_EQ(123, get<int>(req.id));
        });

    adapter.onRequest(request);

    // Test notification forwarding
    jsonrpc::Notification notification;
    notification.jsonrpc = "2.0";
    notification.method = "test.notify";

    EXPECT_CALL(*message_callbacks_, onNotification(_))
        .WillOnce([](const jsonrpc::Notification& notif) {
          EXPECT_EQ("test.notify", notif.method);
        });

    adapter.onNotification(notification);

    // Test response forwarding
    jsonrpc::Response response;
    response.jsonrpc = "2.0";
    response.id = 456;
    response.result = jsonrpc::ResponseResult(nullptr);

    EXPECT_CALL(*message_callbacks_, onResponse(_))
        .WillOnce([](const jsonrpc::Response& resp) {
          EXPECT_EQ(456, get<int>(resp.id));
        });

    adapter.onResponse(response);

    // Test error forwarding
    Error error;
    error.code = jsonrpc::PARSE_ERROR;
    error.message = "Parse error";

    EXPECT_CALL(*message_callbacks_, onError(_)).WillOnce([](const Error& err) {
      EXPECT_EQ(jsonrpc::PARSE_ERROR, err.code);
    });

    adapter.onProtocolError(error);
  });
}

/**
 * Test unused interface methods
 */
TEST_F(JsonRpcFilterChainFactoryTest, UnusedInterfaceMethods) {
  executeInDispatcher([this]() {
    JsonRpcFilterChainFactory factory(*dispatcher_, *message_callbacks_, true);

    // Create dummy filter manager
    int test_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    auto& socket_interface = network::socketInterface();
    auto io_handle = socket_interface.ioHandleForFd(test_fd, true);

    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), network::Address::pipeAddress("test"),
        network::Address::pipeAddress("test"));

    auto connection = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(socket), network::TransportSocketPtr(nullptr),
        true);

    // Test createNetworkFilterChain (should return true but do nothing)
    std::vector<network::FilterFactoryCb> factories;
    EXPECT_TRUE(factory.createNetworkFilterChain(connection->filterManager(),
                                                 factories));

    // Test createListenerFilterChain (should return true but do nothing)
    EXPECT_TRUE(factory.createListenerFilterChain(connection->filterManager()));
  });
}

/**
 * Test filter lifetime management
 */
TEST_F(JsonRpcFilterChainFactoryTest, FilterLifetimeManagement) {
  executeInDispatcher([this]() {
    network::FilterSharedPtr captured_filter;

    {
      // Create factory in a scope
      JsonRpcFilterChainFactory factory(*dispatcher_, *message_callbacks_,
                                        true);  // use framing

      // Create connection
      int test_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
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

      // Initialize filters
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

}  // namespace
}  // namespace filter
}  // namespace mcp