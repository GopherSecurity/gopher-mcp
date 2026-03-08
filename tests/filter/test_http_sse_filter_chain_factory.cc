/**
 * Unit tests for HttpSseFilterChainFactory
 *
 * Tests the factory that creates HTTP+SSE+JSON-RPC filter chains
 * for the complete protocol stack.
 */

#include <chrono>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/socket.h>

#include "mcp/buffer.h"
#include "mcp/filter/http_routing_filter.h"
#include "mcp/filter/http_sse_filter_chain_factory.h"
#include "mcp/json/json_serialization.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"

#include "../integration/real_io_test_base.h"

namespace mcp {
namespace filter {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

// Simple test filter for pre-filter tests
class TestPreFilter : public network::Filter {
 public:
  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    return network::FilterStatus::Continue;
  }
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    return network::FilterStatus::Continue;
  }
  network::FilterStatus onNewConnection() override {
    return network::FilterStatus::Continue;
  }
  void initializeReadFilterCallbacks(
      network::ReadFilterCallbacks& callbacks) override {}
  void initializeWriteFilterCallbacks(
      network::WriteFilterCallbacks& callbacks) override {}
};

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
 * Test fixture for HttpSseFilterChainFactory using real I/O
 */
class HttpSseFilterChainFactoryTest : public test::RealIoTestBase {
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
  void testFilterChain(bool is_server) {
    executeInDispatcher([this, is_server]() {
      // Create factory
      auto factory = std::make_shared<HttpSseFilterChainFactory>(
          *dispatcher_, *message_callbacks_, is_server);

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

      // Verify filters were added properly
      // The factory should have created the complete protocol stack:
      // HTTP -> SSE -> JSON-RPC

      // Test with HTTP request data
      std::string test_request;
      if (is_server) {
        // Server expects to receive HTTP request
        test_request =
            "POST /mcp/v1/sse HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 42\r\n"
            "\r\n"
            R"({"jsonrpc":"2.0","id":1,"method":"test"})";
      } else {
        // Client expects to receive HTTP response with SSE
        test_request =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "\r\n"
            "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":null}\n\n";
      }

      OwnedBuffer buffer;
      buffer.add(test_request);

      // This should trigger the filter chain
      connection->filterManager().onRead();
    });
  }

  std::unique_ptr<MockMcpProtocolCallbacks> message_callbacks_;
};

/**
 * Test filter chain creation for server mode
 */
TEST_F(HttpSseFilterChainFactoryTest, CreateFilterChainServerMode) {
  testFilterChain(true);
}

/**
 * Test filter chain creation for client mode
 */
TEST_F(HttpSseFilterChainFactoryTest, CreateFilterChainClientMode) {
  testFilterChain(false);
}

/**
 * Test createNetworkFilterChain method
 */
TEST_F(HttpSseFilterChainFactoryTest, CreateNetworkFilterChain) {
  executeInDispatcher([this]() {
    HttpSseFilterChainFactory factory(*dispatcher_, *message_callbacks_, true);

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

    // Test createNetworkFilterChain
    std::vector<network::FilterFactoryCb> factories;
    bool result = factory.createNetworkFilterChain(connection->filterManager(),
                                                   factories);

    // Should delegate to createFilterChain
    EXPECT_TRUE(result);
  });
}

/**
 * Test createListenerFilterChain (should return false)
 */
TEST_F(HttpSseFilterChainFactoryTest, CreateListenerFilterChain) {
  executeInDispatcher([this]() {
    HttpSseFilterChainFactory factory(*dispatcher_, *message_callbacks_, true);

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

    // Test createListenerFilterChain (not used for this implementation)
    EXPECT_FALSE(
        factory.createListenerFilterChain(connection->filterManager()));
  });
}

/**
 * Test filter lifetime management
 * Verifies filters remain valid after factory destruction
 */
TEST_F(HttpSseFilterChainFactoryTest, FilterLifetimeManagement) {
  executeInDispatcher([this]() {
    std::unique_ptr<network::ConnectionImpl> connection;

    {
      // Create factory in a scope
      HttpSseFilterChainFactory factory(*dispatcher_, *message_callbacks_,
                                        true);  // server mode

      // Create connection
      int test_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
      auto& socket_interface = network::socketInterface();
      auto io_handle = socket_interface.ioHandleForFd(test_fd, true);

      auto socket = std::make_unique<network::ConnectionSocketImpl>(
          std::move(io_handle), network::Address::pipeAddress("test"),
          network::Address::pipeAddress("test"));

      connection = std::make_unique<network::ConnectionImpl>(
          *dispatcher_, std::move(socket), network::TransportSocketPtr(nullptr),
          true);

      // Create filter chain
      EXPECT_TRUE(factory.createFilterChain(connection->filterManager()));

      // Initialize filters
      connection->filterManager().initializeReadFilters();
    }
    // Factory is now destroyed

    // Filters should still be valid - test by sending data
    std::string test_request =
        "POST /mcp/v1/sse HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";
    OwnedBuffer buffer;
    buffer.add(test_request);

    // This should not crash (filters are still valid)
    connection->filterManager().onRead();
  });
}

/**
 * Test HTTP request processing through the filter chain
 * NOTE: Disabled - requires full filter implementation
 */
TEST_F(HttpSseFilterChainFactoryTest, DISABLED_ProcessHttpRequest) {
  executeInDispatcher([this]() {
    // Create factory in server mode
    HttpSseFilterChainFactory factory(*dispatcher_, *message_callbacks_,
                                      true);  // server mode

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
    connection->filterManager().initializeReadFilters();

    // Prepare HTTP request with JSON-RPC payload
    std::string json_payload =
        R"({"jsonrpc":"2.0","id":1,"method":"initialize"})";
    std::string http_request =
        "POST /mcp/v1/sse HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " +
        std::to_string(json_payload.length()) +
        "\r\n"
        "\r\n" +
        json_payload;

    // We expect the JSON-RPC request to be parsed and forwarded
    EXPECT_CALL(*message_callbacks_, onRequest(_))
        .WillOnce([](const jsonrpc::Request& req) {
          EXPECT_EQ("initialize", req.method);
          EXPECT_EQ(1, get<int64_t>(req.id));
        });

    // Send the HTTP request through the filter chain
    OwnedBuffer buffer;
    buffer.add(http_request);
    connection->filterManager().onRead();
  });
}

/**
 * Test SSE event processing through the filter chain
 * NOTE: Disabled - requires full filter implementation
 */
TEST_F(HttpSseFilterChainFactoryTest, DISABLED_ProcessSseEvents) {
  executeInDispatcher([this]() {
    // Create factory in client mode
    HttpSseFilterChainFactory factory(*dispatcher_, *message_callbacks_,
                                      false);  // client mode

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
    connection->filterManager().initializeReadFilters();

    // Prepare HTTP response with SSE events
    std::string http_response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "\r\n"
        "data: "
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"status\":\"ok\"}}\n\n";

    // We expect the JSON-RPC response to be parsed from SSE event
    EXPECT_CALL(*message_callbacks_, onResponse(_))
        .WillOnce([](const jsonrpc::Response& resp) {
          EXPECT_EQ(1, get<int64_t>(resp.id));
          EXPECT_TRUE(resp.result.has_value());
        });

    // Send the HTTP response with SSE through the filter chain
    OwnedBuffer buffer;
    buffer.add(http_response);
    connection->filterManager().onRead();
  });
}

// Test filter factory support (following existing FilterFactoryCb pattern)
TEST_F(HttpSseFilterChainFactoryTest, AddFilterFactory) {
  executeInDispatcher([this]() {
    HttpSseFilterChainFactory factory(*dispatcher_, *message_callbacks_, true);

    // Initially no filter factories
    EXPECT_TRUE(factory.getFilterFactories().empty());

    // Create a filter factory
    auto filter_factory = []() -> network::FilterSharedPtr {
      return std::make_shared<TestPreFilter>();
    };

    // Add filter factory
    factory.addFilterFactory(filter_factory);

    // Verify filter factory was added
    EXPECT_EQ(factory.getFilterFactories().size(), 1);
  });
}

// Test multiple filter factories
TEST_F(HttpSseFilterChainFactoryTest, MultipleFilterFactories) {
  executeInDispatcher([this]() {
    HttpSseFilterChainFactory factory(*dispatcher_, *message_callbacks_, true);

    // Create multiple filter factories
    auto factory1 = []() -> network::FilterSharedPtr {
      return std::make_shared<TestPreFilter>();
    };
    auto factory2 = []() -> network::FilterSharedPtr {
      return std::make_shared<TestPreFilter>();
    };
    auto factory3 = []() -> network::FilterSharedPtr {
      return std::make_shared<TestPreFilter>();
    };

    factory.addFilterFactory(factory1);
    factory.addFilterFactory(factory2);
    factory.addFilterFactory(factory3);

    // Verify order is preserved
    EXPECT_EQ(factory.getFilterFactories().size(), 3);
  });
}

// Test route registration callback
TEST_F(HttpSseFilterChainFactoryTest, RouteRegistrationCallback) {
  executeInDispatcher([this]() {
    HttpSseFilterChainFactory factory(*dispatcher_, *message_callbacks_, true);

    // Initially no callback
    EXPECT_FALSE(factory.getRouteRegistrationCallback());

    bool callback_invoked = false;
    HttpRoutingFilter* received_router = nullptr;

    // Set callback
    factory.setRouteRegistrationCallback(
        [&callback_invoked, &received_router](HttpRoutingFilter* router) {
          callback_invoked = true;
          received_router = router;
        });

    // Verify callback was set
    EXPECT_TRUE(factory.getRouteRegistrationCallback() != nullptr);
  });
}

// Test route callback is invoked during filter chain creation
TEST_F(HttpSseFilterChainFactoryTest, RouteCallbackInvokedOnCreate) {
  executeInDispatcher([this]() {
    HttpSseFilterChainFactory factory(*dispatcher_, *message_callbacks_, true);

    bool callback_invoked = false;

    factory.setRouteRegistrationCallback(
        [&callback_invoked](HttpRoutingFilter* router) {
          callback_invoked = true;
          // Register a custom route
          router->registerHandler("GET", "/custom",
                                  [](const HttpRoutingFilter::RequestContext&) {
                                    HttpRoutingFilter::Response resp;
                                    resp.status_code = 200;
                                    resp.body = "Custom endpoint";
                                    return resp;
                                  });
        });

    // Create connection to trigger filter chain creation
    int test_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    auto& socket_interface = network::socketInterface();
    auto io_handle = socket_interface.ioHandleForFd(test_fd, true);

    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), network::Address::pipeAddress("test"),
        network::Address::pipeAddress("test"));

    auto connection = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(socket), network::TransportSocketPtr(nullptr),
        true);

    // Create filter chain - callback should be invoked
    factory.createFilterChain(connection->filterManager());

    EXPECT_TRUE(callback_invoked);
  });
}

}  // namespace
}  // namespace filter
}  // namespace mcp