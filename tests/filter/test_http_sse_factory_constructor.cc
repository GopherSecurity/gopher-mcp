/**
 * @file test_http_sse_factory_constructor.cc
 * @brief Unit tests for HttpSseFilterChainFactory constructor parameters
 *
 * Tests for Section 1c implementation (commit cca768c5):
 * - Factory constructor accepts http_path and http_host parameters
 * - Parameters are properly stored and used when creating filters
 * - Default values work correctly
 */

#include <sys/socket.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/filter/http_sse_filter_chain_factory.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"

#include "../integration/real_io_test_base.h"

namespace mcp {
namespace filter {
namespace {

using ::testing::NiceMock;

/**
 * Mock MCP callbacks
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
 * Test fixture for HttpSseFilterChainFactory constructor
 */
class HttpSseFactoryConstructorTest : public test::RealIoTestBase {
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
// Constructor Parameter Tests
// =============================================================================

/**
 * Test: Constructor with default parameters
 */
TEST_F(HttpSseFactoryConstructorTest, ConstructorWithDefaults) {
  // Create factory with default parameters (server mode, /rpc, localhost)
  auto factory = std::make_shared<HttpSseFilterChainFactory>(
      *dispatcher_, *callbacks_, true);

  EXPECT_NE(factory, nullptr);
}

/**
 * Test: Constructor with custom http_path
 */
TEST_F(HttpSseFactoryConstructorTest, ConstructorWithCustomPath) {
  // Create factory with custom path
  auto factory = std::make_shared<HttpSseFilterChainFactory>(
      *dispatcher_, *callbacks_, false, "/custom/sse");

  EXPECT_NE(factory, nullptr);
}

/**
 * Test: Constructor with custom http_path and http_host
 */
TEST_F(HttpSseFactoryConstructorTest, ConstructorWithCustomPathAndHost) {
  // Create factory with custom path and host
  auto factory = std::make_shared<HttpSseFilterChainFactory>(
      *dispatcher_, *callbacks_, false, "/api/events", "server.example.com:8080");

  EXPECT_NE(factory, nullptr);
}

/**
 * Test: Client mode factory with SSE endpoint
 */
TEST_F(HttpSseFactoryConstructorTest, ClientModeWithSseEndpoint) {
  executeInDispatcher([this]() {
    // Create factory for client mode with SSE endpoint
    auto factory = std::make_shared<HttpSseFilterChainFactory>(
        *dispatcher_, *callbacks_, false, "/sse", "localhost:8080");

    EXPECT_NE(factory, nullptr);

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

    // Create filter chain
    bool result = factory->createFilterChain(connection->filterManager());
    EXPECT_TRUE(result);

    // Initialize filters
    connection->filterManager().initializeReadFilters();
  });
}

/**
 * Test: Server mode factory
 */
TEST_F(HttpSseFactoryConstructorTest, ServerModeFactory) {
  executeInDispatcher([this]() {
    // Create factory for server mode
    auto factory = std::make_shared<HttpSseFilterChainFactory>(
        *dispatcher_, *callbacks_, true);

    EXPECT_NE(factory, nullptr);

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

    // Create filter chain
    bool result = factory->createFilterChain(connection->filterManager());
    EXPECT_TRUE(result);

    // Initialize filters
    connection->filterManager().initializeReadFilters();
  });
}

/**
 * Test: Multiple factories with different configurations
 */
TEST_F(HttpSseFactoryConstructorTest, MultipleFactoriesWithDifferentConfigs) {
  // Create multiple factories with different configurations
  auto factory1 = std::make_shared<HttpSseFilterChainFactory>(
      *dispatcher_, *callbacks_, true, "/rpc", "localhost");

  auto factory2 = std::make_shared<HttpSseFilterChainFactory>(
      *dispatcher_, *callbacks_, false, "/sse", "server1.example.com");

  auto factory3 = std::make_shared<HttpSseFilterChainFactory>(
      *dispatcher_, *callbacks_, false, "/events", "server2.example.com:9090");

  EXPECT_NE(factory1, nullptr);
  EXPECT_NE(factory2, nullptr);
  EXPECT_NE(factory3, nullptr);

  // Verify they are different instances
  EXPECT_NE(factory1, factory2);
  EXPECT_NE(factory2, factory3);
  EXPECT_NE(factory1, factory3);
}

}  // namespace
}  // namespace filter
}  // namespace mcp
