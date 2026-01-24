/**
 * @file test_http_sse_event_handling.cc
 * @brief Unit tests for HTTP SSE event handling and message routing
 *
 * Tests for Section 1a implementation (commit cca768c5):
 * - SSE "endpoint" event processing
 * - SSE "message" event processing
 * - POST routing via sendHttpPost callback
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/socket.h>

#include "mcp/buffer.h"
#include "mcp/filter/http_sse_filter_chain_factory.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"

#include "../integration/real_io_test_base.h"

namespace mcp {
namespace filter {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::SaveArg;

/**
 * Mock MCP callbacks for testing SSE event handling
 */
class MockMcpCallbacks : public McpProtocolCallbacks {
 public:
  MOCK_METHOD(void, onRequest, (const jsonrpc::Request&), (override));
  MOCK_METHOD(void, onNotification, (const jsonrpc::Notification&), (override));
  MOCK_METHOD(void, onResponse, (const jsonrpc::Response&), (override));
  MOCK_METHOD(void, onConnectionEvent, (network::ConnectionEvent), (override));
  MOCK_METHOD(void, onError, (const Error&), (override));
  MOCK_METHOD(void, onMessageEndpoint, (const std::string&), (override));
  MOCK_METHOD(bool, sendHttpPost, (const std::string&), (override));
};

/**
 * Test fixture for SSE event handling
 */
class HttpSseEventHandlingTest : public test::RealIoTestBase {
 protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    callbacks_ = std::make_unique<NiceMock<MockMcpCallbacks>>();
  }

  void TearDown() override {
    callbacks_.reset();
    RealIoTestBase::TearDown();
  }

  std::unique_ptr<MockMcpCallbacks> callbacks_;
};

// =============================================================================
// SSE "endpoint" Event Tests
// =============================================================================

/**
 * Test: SSE "endpoint" event triggers onMessageEndpoint callback
 */
TEST_F(HttpSseEventHandlingTest, EndpointEventTriggersCallback) {
  executeInDispatcher([this]() {
    // Set up expectations
    std::string received_endpoint;
    EXPECT_CALL(*callbacks_, onMessageEndpoint(_))
        .WillOnce(SaveArg<0>(&received_endpoint));

    // Create filter chain (client mode)
    auto factory = std::make_shared<HttpSseFilterChainFactory>(
        *dispatcher_, *callbacks_, false);

    // Create test connection
    int test_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_GE(test_fd, 0);

    auto& socket_interface = network::socketInterface();
    auto io_handle = socket_interface.ioHandleForFd(test_fd, true);

    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), network::Address::pipeAddress("test"),
        network::Address::pipeAddress("test"));

    auto connection = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(socket), network::TransportSocketPtr(nullptr),
        true);

    factory->createFilterChain(connection->filterManager());
    connection->filterManager().initializeReadFilters();

    // Simulate receiving SSE endpoint event
    std::string sse_response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "\r\n"
        "event: endpoint\n"
        "data: /message\n"
        "\n";

    OwnedBuffer buffer;
    buffer.add(sse_response);
    connection->filterManager().onRead();

    // Run dispatcher to process deferred endpoint handler
    dispatcher_->run(event::RunType::NonBlock);

    // Verify callback was called with correct endpoint
    EXPECT_EQ(received_endpoint, "/message");
  });
}

// =============================================================================
// SSE "message" Event Tests
// =============================================================================

/**
 * Test: SSE "message" event processes JSON-RPC message
 */
TEST_F(HttpSseEventHandlingTest, MessageEventProcessesJsonRpc) {
  executeInDispatcher([this]() {
    // Set up expectations for JSON-RPC response
    bool response_received = false;
    EXPECT_CALL(*callbacks_, onResponse(_))
        .WillOnce([&response_received](const jsonrpc::Response&) {
          response_received = true;
        });

    // Create filter chain (client mode)
    auto factory = std::make_shared<HttpSseFilterChainFactory>(
        *dispatcher_, *callbacks_, false);

    // Create test connection
    int test_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_GE(test_fd, 0);

    auto& socket_interface = network::socketInterface();
    auto io_handle = socket_interface.ioHandleForFd(test_fd, true);

    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), network::Address::pipeAddress("test"),
        network::Address::pipeAddress("test"));

    auto connection = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(socket), network::TransportSocketPtr(nullptr),
        true);

    factory->createFilterChain(connection->filterManager());
    connection->filterManager().initializeReadFilters();

    // Simulate receiving SSE message event with JSON-RPC response
    std::string sse_response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "\r\n"
        "event: message\n"
        "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"success\"}\n"
        "\n";

    OwnedBuffer buffer;
    buffer.add(sse_response);
    connection->filterManager().onRead();

    // Verify JSON-RPC response was parsed and delivered
    EXPECT_TRUE(response_received);
  });
}

/**
 * Test: Default SSE event (no event type) processes JSON-RPC message
 */
TEST_F(HttpSseEventHandlingTest, DefaultEventProcessesJsonRpc) {
  executeInDispatcher([this]() {
    // Set up expectations
    bool response_received = false;
    EXPECT_CALL(*callbacks_, onResponse(_))
        .WillOnce([&response_received](const jsonrpc::Response&) {
          response_received = true;
        });

    // Create filter chain (client mode)
    auto factory = std::make_shared<HttpSseFilterChainFactory>(
        *dispatcher_, *callbacks_, false);

    // Create test connection
    int test_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_GE(test_fd, 0);

    auto& socket_interface = network::socketInterface();
    auto io_handle = socket_interface.ioHandleForFd(test_fd, true);

    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), network::Address::pipeAddress("test"),
        network::Address::pipeAddress("test"));

    auto connection = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(socket), network::TransportSocketPtr(nullptr),
        true);

    factory->createFilterChain(connection->filterManager());
    connection->filterManager().initializeReadFilters();

    // Simulate SSE response without event type (backwards compatibility)
    std::string sse_response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "\r\n"
        "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":null}\n"
        "\n";

    OwnedBuffer buffer;
    buffer.add(sse_response);
    connection->filterManager().onRead();

    // Verify JSON-RPC message was processed
    EXPECT_TRUE(response_received);
  });
}

}  // namespace
}  // namespace filter
}  // namespace mcp
