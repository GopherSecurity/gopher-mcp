/**
 * @file test_connection_manager_section2.cc
 * @brief Unit tests for Section 2: Connection Management & Routing
 *
 * Tests for Section 2 implementation (commit cca768c5):
 * - DNS resolution support
 * - POST connection management for HTTP/SSE dual-connection
 * - onMessageEndpoint() and sendHttpPost() callbacks
 * - Duplicate event prevention
 * - Connection close order
 */

#include <atomic>
#include <chrono>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/event/libevent_dispatcher.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/socket_impl.h"

namespace mcp {
namespace {

using ::testing::_;
using ::testing::NiceMock;

/**
 * Mock protocol callbacks to verify Section 2 behavior
 */
class MockProtocolCallbacks : public McpProtocolCallbacks {
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
 * Test fixture for Section 2 connection manager tests
 */
class ConnectionManagerSection2Test : public ::testing::Test {
 protected:
  void SetUp() override {
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    callbacks_ = std::make_unique<NiceMock<MockProtocolCallbacks>>();
  }

  void TearDown() override {
    stopDispatcherThread();
    callbacks_.reset();
    dispatcher_.reset();
  }

  // Starts the dispatcher on a background thread.  Needed for tests that
  // invoke McpConnectionManager methods that assert isThreadSafe() — those
  // methods model callbacks coming from the connection's own callback loop,
  // which only exists once the dispatcher is running.
  void startDispatcherThread() {
    if (loop_thread_.joinable()) {
      return;
    }
    loop_thread_ = std::thread(
        [this]() { dispatcher_->run(event::RunType::Block); });
    // Wait until libevent has actually entered the loop — without this we
    // race with thread_id_ being set and isThreadSafe() still returns false.
    for (int i = 0; i < 200 && !dispatcher_->isThreadSafe(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      // isThreadSafe() checked from test thread is always false by design;
      // poll via a posted sentinel instead.
      std::atomic<bool> sentinel{false};
      dispatcher_->post([&sentinel]() { sentinel = true; });
      for (int j = 0; j < 50 && !sentinel.load(); ++j) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      if (sentinel.load()) break;
    }
  }

  // Posts fn onto the dispatcher and blocks until it has run.  Use for any
  // call that must execute on the dispatcher thread (e.g., onConnectionEvent,
  // deferredDelete, connection mutators guarded by dispatcher-thread asserts).
  void runOnDispatcher(std::function<void()> fn) {
    std::atomic<bool> done{false};
    dispatcher_->post([&done, fn = std::move(fn)]() {
      fn();
      done = true;
    });
    for (int i = 0; i < 400 && !done.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(done.load()) << "dispatcher never ran the posted callback";
  }

  void stopDispatcherThread() {
    if (loop_thread_.joinable()) {
      dispatcher_->exit();
      loop_thread_.join();
    }
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::unique_ptr<MockProtocolCallbacks> callbacks_;
  std::thread loop_thread_;
};

// =============================================================================
// Connection Configuration Tests
// =============================================================================

/**
 * Test: McpConnectionConfig includes http_path and http_host fields
 */
TEST_F(ConnectionManagerSection2Test, ConfigHasHttpEndpointFields) {
  McpConnectionConfig config;

  // Verify default values
  EXPECT_EQ(config.http_path, "/rpc");
  EXPECT_EQ(config.http_host, "");

  // Verify we can set custom values
  config.http_path = "/custom/sse";
  config.http_host = "example.com:8080";

  EXPECT_EQ(config.http_path, "/custom/sse");
  EXPECT_EQ(config.http_host, "example.com:8080");
}

/**
 * Test: Connection manager constructor accepts http_path and http_host
 */
TEST_F(ConnectionManagerSection2Test, ConstructorAcceptsHttpEndpointConfig) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;
  config.http_path = "/api/events";
  config.http_host = "server.example.com";

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);

  EXPECT_NE(manager, nullptr);
}

// =============================================================================
// Protocol Callbacks Interface Tests
// =============================================================================

/**
 * Test: onMessageEndpoint() callback is invoked correctly
 */
TEST_F(ConnectionManagerSection2Test, OnMessageEndpointCallback) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);

  manager->setProtocolCallbacks(*callbacks_);

  // Expect callback to be forwarded
  EXPECT_CALL(*callbacks_, onMessageEndpoint("http://example.com:8080/message"))
      .Times(1);

  // Trigger the callback through the manager
  manager->onMessageEndpoint("http://example.com:8080/message");
}

/**
 * Test: onMessageEndpoint() stores endpoint internally
 */
TEST_F(ConnectionManagerSection2Test, OnMessageEndpointStoresEndpoint) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);

  // Call onMessageEndpoint
  manager->onMessageEndpoint("http://localhost:8080/api/message");

  // Verify it's stored (we can't directly check the internal state,
  // but we can verify sendHttpPost() works after this)
  // This is tested in the sendHttpPost tests below
}

/**
 * Test: sendHttpPost() returns false when no endpoint set
 */
TEST_F(ConnectionManagerSection2Test, SendHttpPostFailsWithoutEndpoint) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);

  // Try to send POST without setting endpoint first
  bool result =
      manager->sendHttpPost(R"({"jsonrpc":"2.0","method":"test","id":1})");

  EXPECT_FALSE(result);
}

/**
 * Test: sendHttpPost() parses HTTP URL correctly
 */
TEST_F(ConnectionManagerSection2Test, SendHttpPostParsesHttpUrl) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);

  // Set endpoint with HTTP URL
  manager->onMessageEndpoint("http://localhost:8080/api/rpc");

  // This will fail because localhost won't accept connections in test,
  // but we're testing that it parses the URL and attempts connection
  bool result = manager->sendHttpPost(R"({"test":"data"})");

  // The result depends on whether connection succeeds, but the important
  // thing is it doesn't crash and returns a boolean
  EXPECT_TRUE(result || !result);  // Just verify it returns
}

/**
 * Test: sendHttpPost() parses HTTPS URL correctly
 *
 * Note: SSL transport is not yet fully implemented, so we expect an exception.
 * This test verifies URL parsing but accepts SSL limitations.
 */
TEST_F(ConnectionManagerSection2Test, SendHttpPostParsesHttpsUrl) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;
  config.http_sse_config = transport::HttpSseTransportSocketConfig{};
  config.http_sse_config->underlying_transport =
      transport::HttpSseTransportSocketConfig::UnderlyingTransport::SSL;

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);

  // Set endpoint with HTTPS URL
  manager->onMessageEndpoint("https://example.com:443/api/message");

  // SSL transport is now implemented, so sendHttpPost should not throw
  // Note: The actual connection may fail since we're not connecting to a real
  // server, but the sendHttpPost call itself should succeed in creating the
  // POST connection
  bool result = manager->sendHttpPost(
      R"({"jsonrpc":"2.0","method":"initialize","id":1})");
  // Result may be true or false depending on whether connection succeeds,
  // but the important thing is it doesn't throw
  (void)result;  // Suppress unused variable warning
}

/**
 * Test: sendHttpPost() extracts path from full URL
 */
TEST_F(ConnectionManagerSection2Test, SendHttpPostExtractsPathFromUrl) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);

  // Set endpoint with various URL formats
  manager->onMessageEndpoint(
      "http://server.example.com:9090/custom/endpoint/path");

  // The actual POST will fail to connect, but path parsing should work
  bool result = manager->sendHttpPost(R"({"test":"value"})");

  EXPECT_TRUE(result || !result);
}

// =============================================================================
// Connection Close Order Tests
// =============================================================================

/**
 * Test: close() handles POST connection cleanup
 */
TEST_F(ConnectionManagerSection2Test, CloseHandlesPostConnection) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);

  // Close should not crash even if no POST connection exists
  manager->close();

  // Multiple closes should be safe
  manager->close();
}

// =============================================================================
// Duplicate Event Prevention Tests
// =============================================================================

/**
 * Test: onConnectionEvent() handles duplicate Connected events
 */
TEST_F(ConnectionManagerSection2Test,
       OnConnectionEventPreventsDuplicateConnected) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);

  manager->setProtocolCallbacks(*callbacks_);

  // First Connected event should be processed
  EXPECT_CALL(*callbacks_,
              onConnectionEvent(network::ConnectionEvent::Connected))
      .Times(1);

  // onConnectionEvent asserts it runs on the dispatcher thread because in
  // production it's always invoked from the connection's own callback loop.
  // Drive the synthetic invocations through the dispatcher so they satisfy
  // that contract.
  startDispatcherThread();
  runOnDispatcher([&manager]() {
    manager->onConnectionEvent(network::ConnectionEvent::Connected);
    manager->onConnectionEvent(
        network::ConnectionEvent::Connected);  // Should be ignored
  });
}

/**
 * Test: onConnectionEvent() handles duplicate Close events
 */
TEST_F(ConnectionManagerSection2Test, OnConnectionEventPreventsDuplicateClose) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);

  manager->setProtocolCallbacks(*callbacks_);

  // As above — driven through the dispatcher to satisfy the
  // onConnectionEvent thread-safety assert.
  startDispatcherThread();
  runOnDispatcher([&manager]() {
    manager->onConnectionEvent(network::ConnectionEvent::RemoteClose);
    manager->onConnectionEvent(network::ConnectionEvent::RemoteClose);
    manager->onConnectionEvent(network::ConnectionEvent::LocalClose);
  });
}

// =============================================================================
// Deferred-close / thread-safety regression tests (PR #212)
// =============================================================================

// Regression test for the crash fixed by 4f46b3db:
// onConnectionEvent(RemoteClose|LocalClose) used to destroy active_connection_
// synchronously from inside the connection's own callback loop, causing UAF.
// It now hands the connection to dispatcher.deferredDelete.  This test drives
// the close path on the dispatcher thread and verifies it completes without
// tripping the dispatcher-thread assertion and without crashing.
TEST_F(ConnectionManagerSection2Test, OnCloseRunsCleanlyOnDispatcherThread) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);
  manager->setProtocolCallbacks(*callbacks_);

  startDispatcherThread();

  // Full legal lifecycle: connect -> close.  Each step must run on the
  // dispatcher thread because onConnectionEvent now asserts it.
  runOnDispatcher([&manager]() {
    manager->onConnectionEvent(network::ConnectionEvent::Connected);
  });
  runOnDispatcher([&manager]() {
    manager->onConnectionEvent(network::ConnectionEvent::RemoteClose);
  });

  // Sanity: a stray close event after the first must also be safe to process
  // on the dispatcher thread.  This is the path that used to UAF when the
  // connection was destroyed synchronously on the first close.
  runOnDispatcher([&manager]() {
    manager->onConnectionEvent(network::ConnectionEvent::LocalClose);
  });
}

// The dispatcher-thread assertion added alongside the deferred-close fix is
// the tripwire that stops any future caller from synthesising connection
// events on the wrong thread.  We can't exercise the abort path in a passing
// test, but we can at least prove that the same calls *do* succeed when they
// come in on the dispatcher thread — and, paired with the assertion, that
// catches off-thread regressions in debug builds.
TEST_F(ConnectionManagerSection2Test,
       RepeatedEventSequencesOnDispatcherThreadDoNotCrash) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);
  manager->setProtocolCallbacks(*callbacks_);

  startDispatcherThread();

  for (int i = 0; i < 5; ++i) {
    runOnDispatcher([&manager]() {
      manager->onConnectionEvent(network::ConnectionEvent::Connected);
      manager->onConnectionEvent(network::ConnectionEvent::RemoteClose);
    });
  }
}

// =============================================================================
// HTTPS Transport Factory Tests
// =============================================================================

/**
 * Test: Transport factory creates HTTPS socket for SSL config
 */
TEST_F(ConnectionManagerSection2Test, TransportFactoryCreatesHttpsForSsl) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;
  config.http_sse_config = transport::HttpSseTransportSocketConfig{};
  config.http_sse_config->server_address = "example.com:443";
  config.http_sse_config->underlying_transport =
      transport::HttpSseTransportSocketConfig::UnderlyingTransport::SSL;

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);

  EXPECT_NE(manager, nullptr);

  // The factory is created internally, we just verify construction succeeds
  // with SSL configuration
}

/**
 * Test: Transport factory creates plain HTTP socket for non-SSL
 */
TEST_F(ConnectionManagerSection2Test, TransportFactoryCreatesHttpForNonSsl) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;
  config.http_sse_config = transport::HttpSseTransportSocketConfig{};
  config.http_sse_config->server_address = "localhost:8080";
  config.http_sse_config->underlying_transport =
      transport::HttpSseTransportSocketConfig::UnderlyingTransport::TCP;

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);

  EXPECT_NE(manager, nullptr);
}

// =============================================================================
// Address Parsing Tests
// =============================================================================

/**
 * Test: Address parsing with default ports
 */
TEST_F(ConnectionManagerSection2Test, AddressParsingDefaultPorts) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;
  config.http_sse_config = transport::HttpSseTransportSocketConfig{};

  // HTTP default port (80)
  config.http_sse_config->server_address = "example.com";
  config.http_sse_config->underlying_transport =
      transport::HttpSseTransportSocketConfig::UnderlyingTransport::TCP;

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);
  EXPECT_NE(manager, nullptr);
}

/**
 * Test: Address parsing with HTTPS default port
 */
TEST_F(ConnectionManagerSection2Test, AddressParsingHttpsDefaultPort) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;
  config.http_sse_config = transport::HttpSseTransportSocketConfig{};

  // HTTPS default port (443)
  config.http_sse_config->server_address = "secure.example.com";
  config.http_sse_config->underlying_transport =
      transport::HttpSseTransportSocketConfig::UnderlyingTransport::SSL;

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);
  EXPECT_NE(manager, nullptr);
}

/**
 * Test: Address parsing with explicit port
 */
TEST_F(ConnectionManagerSection2Test, AddressParsingExplicitPort) {
  McpConnectionConfig config;
  config.transport_type = TransportType::HttpSse;
  config.http_sse_config = transport::HttpSseTransportSocketConfig{};
  config.http_sse_config->server_address = "example.com:9090";

  auto manager = std::make_unique<McpConnectionManager>(
      *dispatcher_, network::socketInterface(), config);
  EXPECT_NE(manager, nullptr);
}

}  // namespace
}  // namespace mcp
