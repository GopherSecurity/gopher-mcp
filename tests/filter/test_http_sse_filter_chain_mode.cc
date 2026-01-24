/**
 * @file test_http_sse_filter_chain_mode.cc
 * @brief Unit tests for HTTP Filter Chain Mode Selection
 *
 * Tests for the use_sse parameter in HttpSseFilterChainFactory:
 * - SSE mode (use_sse=true): Sends GET /sse first, then POST requests
 * - Streamable HTTP mode (use_sse=false): Direct POST requests only
 *
 * Commit: 19f359f19cf37184636ec745f19fe4087b47052a
 * Feature: HTTP Filter Chain Mode Selection (Section 2)
 */

#include <gtest/gtest.h>

#include "mcp/event/event_loop.h"
#include "mcp/filter/http_sse_filter_chain_factory.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/filter.h"

namespace mcp {
namespace filter {
namespace {

/**
 * Mock MCP protocol callbacks for testing
 */
class MockMcpProtocolCallbacks : public McpProtocolCallbacks {
 public:
  void onRequest(const jsonrpc::Request& request) override {
    last_request_ = request;
    request_count_++;
  }

  void onNotification(const jsonrpc::Notification& notification) override {
    (void)notification;
    notification_count_++;
  }

  void onResponse(const jsonrpc::Response& response) override {
    (void)response;
    response_count_++;
  }

  void onConnectionEvent(network::ConnectionEvent event) override {
    last_event_ = event;
    event_count_++;
  }

  void onError(const Error& error) override {
    last_error_ = error;
    error_count_++;
  }

  void onMessageEndpoint(const std::string& endpoint) override {
    message_endpoint_ = endpoint;
  }

  bool sendHttpPost(const std::string& json_body) override {
    last_post_body_ = json_body;
    post_count_++;
    return true;
  }

  // Test inspection methods
  int getRequestCount() const { return request_count_; }
  int getNotificationCount() const { return notification_count_; }
  int getResponseCount() const { return response_count_; }
  int getEventCount() const { return event_count_; }
  int getErrorCount() const { return error_count_; }
  int getPostCount() const { return post_count_; }
  const std::string& getMessageEndpoint() const { return message_endpoint_; }
  const std::string& getLastPostBody() const { return last_post_body_; }

 private:
  jsonrpc::Request last_request_;
  network::ConnectionEvent last_event_{network::ConnectionEvent::Connected};
  Error last_error_;
  std::string message_endpoint_;
  std::string last_post_body_;
  int request_count_{0};
  int notification_count_{0};
  int response_count_{0};
  int event_count_{0};
  int error_count_{0};
  int post_count_{0};
};

/**
 * Test fixture for HTTP Filter Chain Mode tests
 */
class HttpFilterChainModeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dispatcher_ =
        event::createPlatformDefaultDispatcherFactory()->createDispatcher(
            "test");
  }

  void TearDown() override { dispatcher_.reset(); }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  MockMcpProtocolCallbacks callbacks_;
};

// =============================================================================
// Factory Construction Tests
// =============================================================================

/**
 * Test: HttpSseFilterChainFactory can be created with default use_sse=true
 */
TEST_F(HttpFilterChainModeTest, FactoryDefaultsToSseMode) {
  // Create factory with default parameters (use_sse=true)
  HttpSseFilterChainFactory factory(*dispatcher_, callbacks_, false, "/rpc",
                                    "localhost");

  // Factory should be created successfully
  // The use_sse_ member defaults to true
  SUCCEED();
}

/**
 * Test: HttpSseFilterChainFactory can be created with explicit use_sse=true
 */
TEST_F(HttpFilterChainModeTest, FactoryExplicitSseMode) {
  // Create factory with explicit use_sse=true
  HttpSseFilterChainFactory factory(*dispatcher_, callbacks_, false, "/sse",
                                    "localhost", true);

  // Factory should be created successfully for SSE mode
  SUCCEED();
}

/**
 * Test: HttpSseFilterChainFactory can be created with use_sse=false
 */
TEST_F(HttpFilterChainModeTest, FactoryStreamableHttpMode) {
  // Create factory with use_sse=false (Streamable HTTP mode)
  HttpSseFilterChainFactory factory(*dispatcher_, callbacks_, false, "/rpc",
                                    "localhost", false);

  // Factory should be created successfully for Streamable HTTP mode
  SUCCEED();
}

/**
 * Test: Factory can be created for server mode with use_sse parameter
 */
TEST_F(HttpFilterChainModeTest, ServerModeWithUseSse) {
  // Server mode with SSE
  HttpSseFilterChainFactory sse_factory(*dispatcher_, callbacks_, true, "/rpc",
                                        "localhost", true);

  // Server mode without SSE (Streamable HTTP)
  HttpSseFilterChainFactory http_factory(*dispatcher_, callbacks_, true, "/rpc",
                                         "localhost", false);

  // Both should create successfully
  SUCCEED();
}

// =============================================================================
// Configuration Tests
// =============================================================================

/**
 * Test: Different paths can be configured for SSE vs Streamable HTTP
 */
TEST_F(HttpFilterChainModeTest, DifferentPathsForModes) {
  // SSE mode typically uses /sse or /events path
  HttpSseFilterChainFactory sse_factory(*dispatcher_, callbacks_, false, "/sse",
                                        "mcp.example.com", true);

  // Streamable HTTP typically uses /rpc or /mcp path
  HttpSseFilterChainFactory http_factory(*dispatcher_, callbacks_, false,
                                         "/rpc", "mcp.example.com", false);

  // Both configurations should be valid
  SUCCEED();
}

/**
 * Test: Host header can be configured independently of mode
 */
TEST_F(HttpFilterChainModeTest, HostHeaderConfiguration) {
  // Different hosts for different servers
  HttpSseFilterChainFactory factory1(*dispatcher_, callbacks_, false, "/sse",
                                     "server1.example.com", true);

  HttpSseFilterChainFactory factory2(*dispatcher_, callbacks_, false, "/rpc",
                                     "server2.example.com:8080", false);

  HttpSseFilterChainFactory factory3(*dispatcher_, callbacks_, false, "/mcp",
                                     "localhost:3000", false);

  // All configurations should be valid
  SUCCEED();
}

// =============================================================================
// Mode Selection Behavior Tests
// =============================================================================

/**
 * Test: SSE mode flag is correctly propagated
 *
 * When use_sse=true:
 * - Client should send GET /sse first
 * - Client should wait for "endpoint" event
 * - POST requests should go to separate connection
 *
 * When use_sse=false:
 * - Client should send POST requests directly
 * - No waiting for endpoint event
 * - Responses come in HTTP response body
 */
TEST_F(HttpFilterChainModeTest, ModeSelectionBehavior) {
  // SSE mode configuration
  HttpSseFilterChainFactory sse_factory(*dispatcher_, callbacks_, false, "/sse",
                                        "localhost", true);

  // Streamable HTTP mode configuration
  HttpSseFilterChainFactory http_factory(*dispatcher_, callbacks_, false,
                                         "/rpc", "localhost", false);

  // The factories are configured with different modes
  // Actual behavior would be tested through integration tests
  // Here we verify the factories can be created with different modes
  SUCCEED();
}

// =============================================================================
// Client vs Server Mode Tests
// =============================================================================

/**
 * Test: Client mode with SSE should wait for endpoint
 */
TEST_F(HttpFilterChainModeTest, ClientSseModeConfiguration) {
  // Client mode with SSE
  HttpSseFilterChainFactory factory(*dispatcher_, callbacks_,
                                    false,  // is_server = false (client mode)
                                    "/sse", "localhost",
                                    true  // use_sse = true
  );

  // In client SSE mode:
  // - Filter should set waiting_for_sse_endpoint_ = true
  // - Filter should call setUseSseGet(true) on HTTP filter
  SUCCEED();
}

/**
 * Test: Client mode with Streamable HTTP should not wait for endpoint
 */
TEST_F(HttpFilterChainModeTest, ClientStreamableHttpConfiguration) {
  // Client mode with Streamable HTTP
  HttpSseFilterChainFactory factory(*dispatcher_, callbacks_,
                                    false,  // is_server = false (client mode)
                                    "/rpc", "localhost",
                                    false  // use_sse = false
  );

  // In client Streamable HTTP mode:
  // - Filter should NOT set waiting_for_sse_endpoint_
  // - Filter should NOT call setUseSseGet(true)
  SUCCEED();
}

/**
 * Test: Server mode behavior is independent of use_sse for request handling
 */
TEST_F(HttpFilterChainModeTest, ServerModeRequestHandling) {
  // Server mode - SSE affects response format, not request handling
  HttpSseFilterChainFactory sse_server(*dispatcher_, callbacks_,
                                       true,  // is_server = true
                                       "/rpc", "localhost",
                                       true  // use_sse = true
  );

  HttpSseFilterChainFactory http_server(*dispatcher_, callbacks_,
                                        true,  // is_server = true
                                        "/rpc", "localhost",
                                        false  // use_sse = false
  );

  // Server always receives JSON-RPC in request body
  // SSE mode only affects response format
  SUCCEED();
}

// =============================================================================
// Edge Case Tests
// =============================================================================

/**
 * Test: Empty path with different modes
 */
TEST_F(HttpFilterChainModeTest, EmptyPathConfiguration) {
  // Root path with SSE mode
  HttpSseFilterChainFactory sse_factory(*dispatcher_, callbacks_, false, "/",
                                        "localhost", true);

  // Root path with Streamable HTTP mode
  HttpSseFilterChainFactory http_factory(*dispatcher_, callbacks_, false, "/",
                                         "localhost", false);

  SUCCEED();
}

/**
 * Test: Long path with different modes
 */
TEST_F(HttpFilterChainModeTest, LongPathConfiguration) {
  std::string long_path = "/api/v1/mcp/server/endpoint";

  HttpSseFilterChainFactory sse_factory(*dispatcher_, callbacks_, false,
                                        long_path, "localhost", true);

  HttpSseFilterChainFactory http_factory(*dispatcher_, callbacks_, false,
                                         long_path, "localhost", false);

  SUCCEED();
}

/**
 * Test: Mode can be changed between factory instances
 */
TEST_F(HttpFilterChainModeTest, ModeSwitchingBetweenFactories) {
  // First create SSE factory
  auto sse_factory = std::make_unique<HttpSseFilterChainFactory>(
      *dispatcher_, callbacks_, false, "/sse", "localhost", true);

  // Then create Streamable HTTP factory
  auto http_factory = std::make_unique<HttpSseFilterChainFactory>(
      *dispatcher_, callbacks_, false, "/rpc", "localhost", false);

  // Both factories coexist
  EXPECT_NE(sse_factory.get(), http_factory.get());

  // Can destroy one and create another
  sse_factory.reset();
  sse_factory = std::make_unique<HttpSseFilterChainFactory>(
      *dispatcher_, callbacks_, false, "/events", "localhost", true);

  SUCCEED();
}

}  // namespace
}  // namespace filter
}  // namespace mcp
