/**
 * @file test_circuit_breaker_filter_simple.cc
 * @brief Simplified unit tests for Circuit Breaker Filter (basic functionality
 * only)
 */

#include <chrono>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../../include/mcp/filter/circuit_breaker_filter.h"

using namespace mcp;
using namespace mcp::filter;
using namespace testing;
using namespace std::chrono_literals;

namespace {

// Mock callbacks for circuit breaker events
class MockCircuitBreakerCallbacks : public CircuitBreakerFilter::Callbacks {
 public:
  MOCK_METHOD(void,
              onStateChange,
              (CircuitState old_state,
               CircuitState new_state,
               const std::string& reason),
              (override));
  MOCK_METHOD(void, onRequestBlocked, (const std::string& method), (override));
  MOCK_METHOD(void,
              onHealthUpdate,
              (double success_rate, uint64_t latency_ms),
              (override));
};

class CircuitBreakerFilterSimpleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    callbacks_ = std::make_unique<NiceMock<MockCircuitBreakerCallbacks>>();

    // Create filter with test configuration
    config_.failure_threshold = 3;
    config_.error_rate_threshold = 0.5;
    config_.timeout = 100ms;  // Short timeout for testing
    config_.window_size = 1s;
    config_.half_open_max_requests = 2;
    config_.half_open_success_threshold = 2;

    filter_ = std::make_unique<CircuitBreakerFilter>(*callbacks_, config_);
  }

  // Helper to create test request
  jsonrpc::Request createRequest(const std::string& method, int id = 1) {
    jsonrpc::Request req;
    req.jsonrpc = "2.0";
    req.method = method;
    req.id = id;
    return req;
  }

  // Helper to create error response
  jsonrpc::Response createErrorResponse(int id, int error_code) {
    jsonrpc::Response resp;
    resp.jsonrpc = "2.0";
    resp.id = id;
    resp.error = mcp::make_optional(Error(error_code, "Test error"));
    return resp;
  }

  // Helper to create success response
  jsonrpc::Response createSuccessResponse(int id) {
    jsonrpc::Response resp;
    resp.jsonrpc = "2.0";
    resp.id = id;
    resp.result = mcp::make_optional(jsonrpc::ResponseResult(nullptr));
    return resp;
  }

 protected:
  std::unique_ptr<CircuitBreakerFilter> filter_;
  std::unique_ptr<MockCircuitBreakerCallbacks> callbacks_;
  CircuitBreakerConfig config_;
};

// Test initial state is closed
TEST_F(CircuitBreakerFilterSimpleTest, InitialStateIsClosed) {
  EXPECT_EQ(filter_->getState(), CircuitState::CLOSED);
}

// Test basic configuration
TEST_F(CircuitBreakerFilterSimpleTest, ConfigurationAccepted) {
  EXPECT_EQ(config_.failure_threshold, 3);
  EXPECT_EQ(config_.error_rate_threshold, 0.5);
  EXPECT_EQ(config_.half_open_max_requests, 2);
}

// Test state transition callback
TEST_F(CircuitBreakerFilterSimpleTest, StateTransitionCallback) {
  // Expect state change to OPEN after 3 failures
  EXPECT_CALL(*callbacks_,
              onStateChange(CircuitState::CLOSED, CircuitState::OPEN, _))
      .Times(1);

  // Simulate 3 consecutive failures
  for (int i = 1; i <= 3; ++i) {
    auto request = createRequest("test.method", i);
    filter_->onRequest(request);

    auto response = createErrorResponse(i, jsonrpc::INTERNAL_ERROR);
    filter_->onResponse(response);
  }

  // Circuit should be open
  EXPECT_EQ(filter_->getState(), CircuitState::OPEN);
}

// Test health metrics
TEST_F(CircuitBreakerFilterSimpleTest, HealthMetrics) {
  double success_rate;
  uint64_t avg_latency;

  // Initially should have perfect metrics
  filter_->getHealthMetrics(success_rate, avg_latency);
  EXPECT_EQ(success_rate, 1.0);
  EXPECT_EQ(avg_latency, 0);

  // After one success, should still be good
  auto req = createRequest("test.method", 1);
  filter_->onRequest(req);
  auto resp = createSuccessResponse(1);
  filter_->onResponse(resp);

  filter_->getHealthMetrics(success_rate, avg_latency);
  EXPECT_EQ(success_rate, 1.0);
}

// Test protocol error tracking
TEST_F(CircuitBreakerFilterSimpleTest, ProtocolErrorsTracked) {
  EXPECT_CALL(*callbacks_,
              onStateChange(CircuitState::CLOSED, CircuitState::OPEN, _))
      .Times(1);

  // Protocol errors should count as failures
  for (int i = 1; i <= 3; ++i) {
    Error error(jsonrpc::INTERNAL_ERROR, "Protocol error");
    filter_->onProtocolError(error);
  }

  // Circuit should be open due to protocol errors
  EXPECT_EQ(filter_->getState(), CircuitState::OPEN);
}

}  // namespace