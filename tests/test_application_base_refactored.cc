/**
 * @file test_application_base_refactored.cc
 * @brief Test the refactored ApplicationBase with external filters
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/event/libevent_dispatcher.h"
#include "mcp/mcp_application_base.h"

using namespace mcp;
using namespace mcp::application;
using namespace testing;

// Test application implementation
class TestApplication : public ApplicationBase {
 public:
  TestApplication(const Config& config) : ApplicationBase(config) {}

  bool onStart() override {
    started_ = true;
    return true;
  }

  void onShutdown() override { shutdown_ = true; }

  void onRequest(const jsonrpc::Request& request) override {
    requests_received_++;
    ApplicationBase::onRequest(request);
  }

  void onConnectionEvent(network::ConnectionEvent evt) {
    // Handle connection events
  }

  bool started_ = false;
  bool shutdown_ = false;
  int requests_received_ = 0;
};

// Test fixture with dispatcher
class ApplicationBaseRefactoredTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dispatcher_ = std::make_unique<mcp::event::LibeventDispatcher>("test");
  }

  void TearDown() override { dispatcher_.reset(); }

  std::unique_ptr<mcp::event::LibeventDispatcher> dispatcher_;
};

// Test the filter chain builder
TEST_F(ApplicationBaseRefactoredTest, FilterChainBuilder) {
  ApplicationStats stats;

  FilterChainBuilder builder(*dispatcher_, stats);

  // Configure and build filter chain
  filter::RateLimitConfig rate_config;
  rate_config.max_requests_per_window = 100;

  filter::MetricsFilter::Config metrics_config;
  metrics_config.report_interval = std::chrono::seconds(10);

  filter::CircuitBreakerConfig circuit_config;
  circuit_config.failure_threshold = 5;

  auto filters = builder.withRateLimiting(rate_config)
                     .withMetrics(metrics_config)
                     .withCircuitBreaker(circuit_config)
                     .build();

  // Verify filters were created
  EXPECT_EQ(filters.size(), 3);

  // Verify metrics filter is accessible
  auto metrics_filter = builder.getMetricsFilter();
  EXPECT_NE(metrics_filter, nullptr);
}

// Test application initialization
TEST_F(ApplicationBaseRefactoredTest, ApplicationInitialization) {
  ApplicationBase::Config config;
  config.name = "TestApp";
  config.worker_threads = 2;
  config.enable_metrics = true;
  config.enable_rate_limiting = true;

  TestApplication app(config);

  // Initialize application
  EXPECT_TRUE(app.initialize());

  // Verify workers were created
  EXPECT_TRUE(app.start());
  EXPECT_TRUE(app.started_);

  // Shutdown
  app.shutdown();
  EXPECT_TRUE(app.shutdown_);
}

// Test metrics callbacks
TEST_F(ApplicationBaseRefactoredTest, MetricsCallbacks) {
  ApplicationStats stats;
  ApplicationMetricsCallbacks callbacks(stats);

  // Create test metrics
  filter::ConnectionMetrics metrics;
  metrics.bytes_sent = 1000;
  metrics.bytes_received = 2000;
  metrics.requests_sent = 10;
  metrics.max_latency_ms = 100;
  metrics.min_latency_ms = 10;

  // Update stats via callback
  callbacks.onMetricsUpdate(metrics);

  // Verify stats were updated
  EXPECT_EQ(stats.bytes_sent.load(), 1000);
  EXPECT_EQ(stats.bytes_received.load(), 2000);
  EXPECT_EQ(stats.requests_total.load(), 10);
  EXPECT_EQ(stats.request_duration_ms_max.load(), 100);
  EXPECT_EQ(stats.request_duration_ms_min.load(), 10);
}

// Test filter chain creation
TEST_F(ApplicationBaseRefactoredTest, CreateFilterChain) {
  ApplicationBase::Config config;
  config.name = "TestApp";
  config.enable_metrics = true;
  config.enable_rate_limiting = true;
  config.enable_circuit_breaker = true;
  config.enable_backpressure = true;
  config.enable_request_validation = false;

  TestApplication app(config);

  // Create filter chain
  auto filters = app.createFilterChain(*dispatcher_);

  // Should have 4 filters (circuit breaker, rate limit, metrics, backpressure)
  EXPECT_EQ(filters.size(), 4);
}

// Test JSON-RPC callback adapter
TEST_F(ApplicationBaseRefactoredTest, JsonRpcCallbackAdapter) {
  class MockCallbacks : public McpProtocolCallbacks {
   public:
    MOCK_METHOD(void, onRequest, (const jsonrpc::Request&), (override));
    MOCK_METHOD(void,
                onNotification,
                (const jsonrpc::Notification&),
                (override));
    MOCK_METHOD(void, onResponse, (const jsonrpc::Response&), (override));
    MOCK_METHOD(void, onError, (const Error&), (override));
    MOCK_METHOD(void,
                onConnectionEvent,
                (network::ConnectionEvent),
                (override));
  };

  MockCallbacks mock_callbacks;
  McpToJsonRpcAdapter adapter(mock_callbacks);

  // Test request forwarding
  jsonrpc::Request request;
  request.method = "test";
  request.id = 1;

  EXPECT_CALL(mock_callbacks, onRequest(_)).Times(1);
  adapter.onRequest(request);

  // Test notification forwarding
  jsonrpc::Notification notification;
  notification.method = "test_notify";

  EXPECT_CALL(mock_callbacks, onNotification(_)).Times(1);
  adapter.onNotification(notification);

  // Test response forwarding
  jsonrpc::Response response;
  response.id = 1;

  EXPECT_CALL(mock_callbacks, onResponse(_)).Times(1);
  adapter.onResponse(response);

  // Test error forwarding
  Error error(jsonrpc::INTERNAL_ERROR, "test error");

  EXPECT_CALL(mock_callbacks, onError(_)).Times(1);
  adapter.onProtocolError(error);
}

// Test connection pool
TEST_F(ApplicationBaseRefactoredTest, ConnectionPool) {
  class TestConnectionPool : public ConnectionPool {
   public:
    TestConnectionPool(mcp::event::Dispatcher& dispatcher)
        : ConnectionPool(dispatcher, 5, 2) {}

   protected:
    ConnectionPtr createNewConnection() override {
      // Return mock connection for testing
      return nullptr;
    }
  };

  TestConnectionPool pool(*dispatcher_);

  // Verify initial state
  EXPECT_EQ(pool.getActiveConnections(), 0);
  EXPECT_EQ(pool.getTotalConnections(), 0);
  // Note: getIdleConnections() doesn't exist in the API
}

// Test failure reason tracking
TEST_F(ApplicationBaseRefactoredTest, FailureReason) {
  FailureReason failure(FailureReason::Type::ConnectionFailure,
                        "Connection refused");

  // Add context
  failure.addContext("host", "localhost");
  failure.addContext("port", "3000");

  // Add stack trace
  failure.addStackFrame("connect() at connection.cc:123");
  failure.addStackFrame("tryConnect() at client.cc:456");

  // Verify
  EXPECT_EQ(failure.getType(), FailureReason::Type::ConnectionFailure);
  EXPECT_EQ(failure.getDescription(), "Connection refused");
  EXPECT_EQ(failure.getContext().at("host"), "localhost");
  EXPECT_EQ(failure.getContext().at("port"), "3000");
  EXPECT_EQ(failure.getStackTrace().size(), 2);
}

// Test filter chain with real dispatcher
TEST_F(ApplicationBaseRefactoredTest, FilterChainWithRealDispatcher) {
  ApplicationStats stats;
  FilterChainBuilder builder(*dispatcher_, stats);

  // Build a complete filter chain
  filter::CircuitBreakerConfig cb_config;
  cb_config.failure_threshold = 3;

  filter::RateLimitConfig rl_config;
  rl_config.max_requests_per_window = 10;

  filter::BackpressureConfig bp_config;
  bp_config.high_watermark = 1024 * 1024;  // 1MB

  auto filters = builder.withCircuitBreaker(cb_config)
                     .withRateLimiting(rl_config)
                     .withBackpressure(bp_config)
                     .build();

  EXPECT_EQ(filters.size(), 3);

  // All filters should be valid
  for (const auto& filter : filters) {
    EXPECT_NE(filter, nullptr);
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}