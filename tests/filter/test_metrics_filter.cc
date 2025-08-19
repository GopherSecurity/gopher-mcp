/**
 * @file test_metrics_filter.cc
 * @brief Unit tests for Metrics Filter
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chrono>
#include <thread>
#include "mcp/filter/metrics_filter.h"
#include "mcp/buffer_impl.h"
#include "../integration/real_io_test_base.h"

using namespace mcp;
using namespace mcp::filter;
using namespace testing;
using namespace std::chrono_literals;

namespace {

// Mock callbacks for metrics events
class MockMetricsCallbacks : public MetricsFilter::MetricsCallbacks {
public:
  MOCK_METHOD(void, onMetricsUpdate, (const ConnectionMetrics& metrics), (override));
  MOCK_METHOD(void, onThresholdExceeded, 
              (const std::string& metric_name, uint64_t value, uint64_t threshold), 
              (override));
};

// Mock JSON-RPC callbacks
class MockJsonRpcCallbacks : public McpJsonRpcFilter::Callbacks {
public:
  MOCK_METHOD(void, onRequest, (const jsonrpc::Request& request), (override));
  MOCK_METHOD(void, onResponse, (const jsonrpc::Response& response), (override));
  MOCK_METHOD(void, onNotification, (const jsonrpc::Notification& notification), (override));
  MOCK_METHOD(void, onProtocolError, (const Error& error), (override));
};

class MetricsFilterTest : public test::RealIoTestBase {
protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    
    callbacks_ = std::make_unique<NiceMock<MockMetricsCallbacks>>();
    next_callbacks_ = std::make_unique<NiceMock<MockJsonRpcCallbacks>>();
    
    // Default config
    config_.rate_update_interval = 100ms;  // Short for testing
    config_.report_interval = 200ms;       // Short for testing
    config_.max_latency_threshold_ms = 1000;
    config_.error_rate_threshold = 5;
    config_.bytes_threshold = 10 * 1024;  // 10KB for testing
    config_.track_methods = true;
  }
  
  void TearDown() override {
    executeInDispatcher([this]() {
      filter_.reset();
    });
    RealIoTestBase::TearDown();
  }
  
  void createFilter() {
    executeInDispatcher([this]() {
      filter_ = std::make_unique<MetricsFilter>(*callbacks_, config_);
      filter_->setNextCallbacks(next_callbacks_.get());
    });
  }
  
  // Helper to create buffer with specific size
  BufferImpl createBuffer(size_t size) {
    BufferImpl buffer;
    std::string data(size, 'X');
    buffer.add(data);
    return buffer;
  }
  
  // Helper to create test request
  jsonrpc::Request createRequest(const std::string& method, int id = 1) {
    jsonrpc::Request req;
    req.jsonrpc = "2.0";
    req.method = method;
    req.id = id;
    return req;
  }
  
  // Helper to create test response
  jsonrpc::Response createResponse(int id, bool is_error = false) {
    jsonrpc::Response resp;
    resp.jsonrpc = "2.0";
    resp.id = id;
    if (is_error) {
      resp.error = jsonrpc::ResponseError{500, "Test error", nullopt};
    } else {
      resp.result = jsonrpc::ResponseResult(nullptr);
    }
    return resp;
  }
  
protected:
  std::unique_ptr<MetricsFilter> filter_;
  std::unique_ptr<MockMetricsCallbacks> callbacks_;
  std::unique_ptr<MockJsonRpcCallbacks> next_callbacks_;
  MetricsFilter::Config config_;
};

// Test basic byte counting
TEST_F(MetricsFilterTest, ByteCounting) {
  createFilter();
  
  executeInDispatcher([this]() {
    // Track received bytes
    auto recv_buffer = createBuffer(1024);
    EXPECT_EQ(filter_->onData(recv_buffer, false), network::FilterStatus::Continue);
    
    // Track sent bytes
    auto send_buffer = createBuffer(2048);
    EXPECT_EQ(filter_->onWrite(send_buffer, false), network::FilterStatus::Continue);
    
    // Check metrics
    auto metrics = filter_->getMetrics();
    EXPECT_EQ(metrics.bytes_received, 1024);
    EXPECT_EQ(metrics.bytes_sent, 2048);
    EXPECT_EQ(metrics.messages_received, 1);
    EXPECT_EQ(metrics.messages_sent, 1);
  });
}

// Test request/response tracking
TEST_F(MetricsFilterTest, RequestResponseTracking) {
  createFilter();
  
  EXPECT_CALL(*next_callbacks_, onRequest(_)).Times(2);
  EXPECT_CALL(*next_callbacks_, onResponse(_)).Times(2);
  
  executeInDispatcher([this]() {
    // Send requests
    auto req1 = createRequest("method1", 1);
    auto req2 = createRequest("method2", 2);
    filter_->onRequest(req1);
    filter_->onRequest(req2);
    
    // Receive responses
    auto resp1 = createResponse(1);
    auto resp2 = createResponse(2);
    filter_->onResponse(resp1);
    filter_->onResponse(resp2);
    
    // Check metrics
    auto metrics = filter_->getMetrics();
    EXPECT_EQ(metrics.requests_received, 2);
    EXPECT_EQ(metrics.responses_received, 2);
  });
}

// Test notification tracking
TEST_F(MetricsFilterTest, NotificationTracking) {
  createFilter();
  
  EXPECT_CALL(*next_callbacks_, onNotification(_)).Times(3);
  
  executeInDispatcher([this]() {
    // Send notifications
    for (int i = 0; i < 3; ++i) {
      jsonrpc::Notification notif;
      notif.method = "test.notification";
      filter_->onNotification(notif);
    }
    
    // Check metrics
    auto metrics = filter_->getMetrics();
    EXPECT_EQ(metrics.notifications_received, 3);
  });
}

// Test error tracking
TEST_F(MetricsFilterTest, ErrorTracking) {
  createFilter();
  
  executeInDispatcher([this]() {
    // Send error responses
    auto req = createRequest("test.method", 1);
    filter_->onRequest(req);
    
    auto error_resp = createResponse(1, true);
    filter_->onResponse(error_resp);
    
    // Protocol error
    Error protocol_error(jsonrpc::INTERNAL_ERROR, "Test error");
    filter_->onProtocolError(protocol_error);
    
    // Check metrics
    auto metrics = filter_->getMetrics();
    EXPECT_EQ(metrics.errors_received, 1);
    EXPECT_EQ(metrics.protocol_errors, 1);
  });
}

// Test latency tracking
TEST_F(MetricsFilterTest, LatencyTracking) {
  createFilter();
  
  executeInDispatcher([this]() {
    // Send request
    auto req = createRequest("test.method", 1);
    filter_->onRequest(req);
    
    // Simulate some latency
    std::this_thread::sleep_for(50ms);
    
    // Send response
    auto resp = createResponse(1);
    filter_->onResponse(resp);
    
    // Check latency metrics
    auto metrics = filter_->getMetrics();
    EXPECT_GT(metrics.total_latency_ms, 0);
    EXPECT_GT(metrics.max_latency_ms, 0);
    EXPECT_LT(metrics.min_latency_ms, UINT64_MAX);
    EXPECT_EQ(metrics.latency_samples, 1);
  });
}

// Test method-specific tracking
TEST_F(MetricsFilterTest, MethodSpecificTracking) {
  config_.track_methods = true;
  createFilter();
  
  executeInDispatcher([this]() {
    // Send requests for different methods
    filter_->onRequest(createRequest("method1", 1));
    filter_->onRequest(createRequest("method1", 2));
    filter_->onRequest(createRequest("method2", 3));
    
    // Check method counts
    auto metrics = filter_->getMetrics();
    EXPECT_EQ(metrics.method_counts["method1"], 2);
    EXPECT_EQ(metrics.method_counts["method2"], 1);
  });
}

// Test threshold exceeded for bytes
TEST_F(MetricsFilterTest, BytesThresholdExceeded) {
  config_.bytes_threshold = 5 * 1024;  // 5KB
  createFilter();
  
  EXPECT_CALL(*callbacks_, onThresholdExceeded("bytes_received", _, 5 * 1024))
      .Times(1);
  
  executeInDispatcher([this]() {
    // Send data exceeding threshold
    auto buffer = createBuffer(6 * 1024);
    filter_->onData(buffer, false);
  });
}

// Test threshold exceeded for latency
TEST_F(MetricsFilterTest, LatencyThresholdExceeded) {
  config_.max_latency_threshold_ms = 100;
  createFilter();
  
  EXPECT_CALL(*callbacks_, onThresholdExceeded("latency_ms", _, 100))
      .Times(1);
  
  executeInDispatcher([this]() {
    // Send request
    auto req = createRequest("slow.method", 1);
    filter_->onRequest(req);
    
    // Simulate high latency
    std::this_thread::sleep_for(150ms);
    
    // Send response
    auto resp = createResponse(1);
    filter_->onResponse(resp);
  });
}

// Test error rate threshold
TEST_F(MetricsFilterTest, ErrorRateThreshold) {
  config_.error_rate_threshold = 2;  // 2 errors per minute
  createFilter();
  
  EXPECT_CALL(*callbacks_, onThresholdExceeded("error_rate", _, 2))
      .Times(AtLeast(1));
  
  executeInDispatcher([this]() {
    // Generate errors quickly
    for (int i = 1; i <= 3; ++i) {
      auto req = createRequest("error.method", i);
      filter_->onRequest(req);
      auto error_resp = createResponse(i, true);
      filter_->onResponse(error_resp);
    }
  });
}

// Test rate calculations
TEST_F(MetricsFilterTest, RateCalculations) {
  config_.rate_update_interval = 100ms;
  createFilter();
  
  executeInDispatcher([this]() {
    // Send data
    auto buffer1 = createBuffer(1024);
    filter_->onData(buffer1, false);
  });
  
  // Wait for rate update
  std::this_thread::sleep_for(150ms);
  
  executeInDispatcher([this]() {
    // Send more data
    auto buffer2 = createBuffer(2048);
    filter_->onData(buffer2, false);
    
    // Check rates
    auto metrics = filter_->getMetrics();
    EXPECT_GT(metrics.current_receive_rate_bps, 0);
    EXPECT_GT(metrics.peak_receive_rate_bps, 0);
  });
}

// Test periodic metrics reporting
TEST_F(MetricsFilterTest, PeriodicMetricsReporting) {
  config_.report_interval = 100ms;
  createFilter();
  
  // Expect at least 2 updates in 250ms
  EXPECT_CALL(*callbacks_, onMetricsUpdate(_))
      .Times(AtLeast(2));
  
  executeInDispatcher([this]() {
    // Generate some activity
    auto buffer = createBuffer(1024);
    filter_->onData(buffer, false);
    
    auto req = createRequest("test.method", 1);
    filter_->onRequest(req);
  });
  
  // Wait for periodic reports
  std::this_thread::sleep_for(250ms);
}

// Test new connection resets metrics
TEST_F(MetricsFilterTest, NewConnectionResetsMetrics) {
  createFilter();
  
  executeInDispatcher([this]() {
    // Generate some metrics
    auto buffer = createBuffer(1024);
    filter_->onData(buffer, false);
    
    auto req = createRequest("test.method", 1);
    filter_->onRequest(req);
    
    // Check metrics are recorded
    auto metrics1 = filter_->getMetrics();
    EXPECT_GT(metrics1.bytes_received, 0);
    EXPECT_GT(metrics1.requests_received, 0);
    
    // New connection
    filter_->onNewConnection();
    
    // Metrics should be reset
    auto metrics2 = filter_->getMetrics();
    EXPECT_EQ(metrics2.bytes_received, 0);
    EXPECT_EQ(metrics2.bytes_sent, 0);
    EXPECT_EQ(metrics2.requests_received, 0);
  });
}

// Test min/max latency tracking
TEST_F(MetricsFilterTest, MinMaxLatencyTracking) {
  createFilter();
  
  executeInDispatcher([this]() {
    // Send multiple requests with different latencies
    for (int i = 1; i <= 3; ++i) {
      auto req = createRequest("test.method", i);
      filter_->onRequest(req);
      
      // Vary latency
      std::this_thread::sleep_for(std::chrono::milliseconds(i * 20));
      
      auto resp = createResponse(i);
      filter_->onResponse(resp);
    }
    
    auto metrics = filter_->getMetrics();
    EXPECT_GT(metrics.min_latency_ms, 0);
    EXPECT_LT(metrics.min_latency_ms, metrics.max_latency_ms);
    EXPECT_EQ(metrics.latency_samples, 3);
    
    // Average should be reasonable
    if (metrics.latency_samples > 0) {
      uint64_t avg = metrics.total_latency_ms / metrics.latency_samples;
      EXPECT_GT(avg, metrics.min_latency_ms);
      EXPECT_LT(avg, metrics.max_latency_ms);
    }
  });
}

// Test connection timing
TEST_F(MetricsFilterTest, ConnectionTiming) {
  createFilter();
  
  executeInDispatcher([this]() {
    // New connection starts timing
    filter_->onNewConnection();
    
    auto metrics1 = filter_->getMetrics();
    auto start_time = metrics1.connection_start;
    
    // Activity updates last activity time
    auto buffer = createBuffer(1024);
    filter_->onData(buffer, false);
    
    std::this_thread::sleep_for(50ms);
    
    auto req = createRequest("test.method", 1);
    filter_->onRequest(req);
    
    auto metrics2 = filter_->getMetrics();
    EXPECT_GE(metrics2.last_activity, start_time);
  });
}

} // namespace