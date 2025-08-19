/**
 * @file test_circuit_breaker_filter.cc
 * @brief Unit tests for Circuit Breaker Filter
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chrono>
#include <thread>
#include "../../include/mcp/filter/circuit_breaker_filter.h"
#include "../integration/real_io_test_base.h"

using namespace mcp;
using namespace mcp::filter;
using namespace testing;
using namespace std::chrono_literals;

namespace {

// Mock callbacks for circuit breaker events
class MockCircuitBreakerCallbacks : public CircuitBreakerFilter::Callbacks {
public:
  MOCK_METHOD(void, onStateChange, 
              (CircuitState old_state, CircuitState new_state, const std::string& reason), 
              (override));
  MOCK_METHOD(void, onRequestBlocked, (const std::string& method), (override));
  MOCK_METHOD(void, onHealthUpdate, (double success_rate, uint64_t latency_ms), (override));
};

// Mock JSON-RPC callbacks
class MockJsonRpcCallbacks : public McpJsonRpcFilter::Callbacks {
public:
  MOCK_METHOD(void, onRequest, (const jsonrpc::Request& request), (override));
  MOCK_METHOD(void, onResponse, (const jsonrpc::Response& response), (override));
  MOCK_METHOD(void, onNotification, (const jsonrpc::Notification& notification), (override));
  MOCK_METHOD(void, onProtocolError, (const Error& error), (override));
};

class CircuitBreakerFilterTest : public test::RealIoTestBase {
protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    
    // Create mock callbacks
    callbacks_ = std::make_unique<NiceMock<MockCircuitBreakerCallbacks>>();
    next_callbacks_ = std::make_unique<NiceMock<MockJsonRpcCallbacks>>();
    
    // Create filter with test configuration
    config_.failure_threshold = 3;
    config_.error_rate_threshold = 0.5;
    config_.timeout = 100ms;  // Short timeout for testing
    config_.window_size = 1s;
    config_.half_open_max_requests = 2;
    config_.half_open_success_threshold = 2;
    
    executeInDispatcher([this]() {
      filter_ = std::make_unique<CircuitBreakerFilter>(*callbacks_, config_);
      filter_->setNextCallbacks(next_callbacks_.get());
    });
  }
  
  void TearDown() override {
    executeInDispatcher([this]() {
      filter_.reset();
    });
    RealIoTestBase::TearDown();
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
    resp.error = make_optional(Error(error_code, "Test error"));
    return resp;
  }
  
  // Helper to create success response
  jsonrpc::Response createSuccessResponse(int id) {
    jsonrpc::Response resp;
    resp.jsonrpc = "2.0";
    resp.id = id;
    resp.result = jsonrpc::ResponseResult(nullptr);
    return resp;
  }
  
protected:
  std::unique_ptr<CircuitBreakerFilter> filter_;
  std::unique_ptr<MockCircuitBreakerCallbacks> callbacks_;
  std::unique_ptr<MockJsonRpcCallbacks> next_callbacks_;
  CircuitBreakerConfig config_;
};

// Test circuit remains closed under normal operation
TEST_F(CircuitBreakerFilterTest, CircuitRemainsClosedOnSuccess) {
  // Expect all requests to be forwarded
  EXPECT_CALL(*next_callbacks_, onRequest(_)).Times(5);
  EXPECT_CALL(*callbacks_, onRequestBlocked(_)).Times(0);
  
  executeInDispatcher([this]() {
    // Send successful requests
    for (int i = 1; i <= 5; ++i) {
      auto request = createRequest("test.method", i);
      filter_->onRequest(request);
      
      // Simulate successful response
      auto response = createSuccessResponse(i);
      filter_->onResponse(response);
    }
  });
  
  // Circuit should remain closed
  EXPECT_EQ(filter_->getState(), CircuitState::CLOSED);
}

// Test circuit opens after consecutive failures
TEST_F(CircuitBreakerFilterTest, CircuitOpensAfterConsecutiveFailures) {
  // Expect state change to OPEN
  EXPECT_CALL(*callbacks_, onStateChange(CircuitState::CLOSED, CircuitState::OPEN, _))
      .Times(1);
  
  executeInDispatcher([this]() {
    // Send failing requests
    for (int i = 1; i <= 3; ++i) {
      auto request = createRequest("test.method", i);
      filter_->onRequest(request);
      
      // Simulate error response
      auto response = createErrorResponse(i, jsonrpc::INTERNAL_ERROR);
      filter_->onResponse(response);
    }
  });
  
  // Circuit should be open
  EXPECT_EQ(filter_->getState(), CircuitState::OPEN);
}

// Test requests are blocked when circuit is open
TEST_F(CircuitBreakerFilterTest, RequestsBlockedWhenCircuitOpen) {
  // Open the circuit first
  executeInDispatcher([this]() {
    for (int i = 1; i <= 3; ++i) {
      auto request = createRequest("test.method", i);
      filter_->onRequest(request);
      auto response = createErrorResponse(i, jsonrpc::INTERNAL_ERROR);
      filter_->onResponse(response);
    }
  });
  
  EXPECT_EQ(filter_->getState(), CircuitState::OPEN);
  
  // Now try to send request - should be blocked
  EXPECT_CALL(*callbacks_, onRequestBlocked("blocked.method")).Times(1);
  EXPECT_CALL(*next_callbacks_, onRequest(_)).Times(0);
  
  executeInDispatcher([this]() {
    auto request = createRequest("blocked.method", 100);
    filter_->onRequest(request);
  });
}

// Test circuit transitions to half-open after timeout
TEST_F(CircuitBreakerFilterTest, CircuitTransitionsToHalfOpenAfterTimeout) {
  // Open the circuit
  executeInDispatcher([this]() {
    for (int i = 1; i <= 3; ++i) {
      auto request = createRequest("test.method", i);
      filter_->onRequest(request);
      auto response = createErrorResponse(i, jsonrpc::INTERNAL_ERROR);
      filter_->onResponse(response);
    }
  });
  
  EXPECT_EQ(filter_->getState(), CircuitState::OPEN);
  
  // Wait for timeout
  std::this_thread::sleep_for(150ms);
  
  // Next request should be allowed (half-open state)
  EXPECT_CALL(*next_callbacks_, onRequest(_)).Times(1);
  
  executeInDispatcher([this]() {
    auto request = createRequest("test.method", 200);
    filter_->onRequest(request);
  });
  
  // Should be in half-open state
  EXPECT_EQ(filter_->getState(), CircuitState::HALF_OPEN);
}

// Test circuit closes from half-open after successful requests
TEST_F(CircuitBreakerFilterTest, CircuitClosesFromHalfOpenOnSuccess) {
  // Open the circuit
  executeInDispatcher([this]() {
    for (int i = 1; i <= 3; ++i) {
      auto request = createRequest("test.method", i);
      filter_->onRequest(request);
      auto response = createErrorResponse(i, jsonrpc::INTERNAL_ERROR);
      filter_->onResponse(response);
    }
  });
  
  // Wait for timeout to transition to half-open
  std::this_thread::sleep_for(150ms);
  
  // Expect state change to CLOSED
  EXPECT_CALL(*callbacks_, onStateChange(CircuitState::HALF_OPEN, CircuitState::CLOSED, _))
      .Times(1);
  
  executeInDispatcher([this]() {
    // Send successful requests in half-open state
    for (int i = 100; i <= 101; ++i) {
      auto request = createRequest("test.method", i);
      filter_->onRequest(request);
      auto response = createSuccessResponse(i);
      filter_->onResponse(response);
    }
  });
  
  // Circuit should be closed again
  EXPECT_EQ(filter_->getState(), CircuitState::CLOSED);
}

// Test circuit reopens from half-open on failure
TEST_F(CircuitBreakerFilterTest, CircuitReopensFromHalfOpenOnFailure) {
  // Open the circuit
  executeInDispatcher([this]() {
    for (int i = 1; i <= 3; ++i) {
      auto request = createRequest("test.method", i);
      filter_->onRequest(request);
      auto response = createErrorResponse(i, jsonrpc::INTERNAL_ERROR);
      filter_->onResponse(response);
    }
  });
  
  // Wait for timeout to transition to half-open
  std::this_thread::sleep_for(150ms);
  
  // Send request to enter half-open
  executeInDispatcher([this]() {
    auto request = createRequest("test.method", 100);
    filter_->onRequest(request);
  });
  
  EXPECT_EQ(filter_->getState(), CircuitState::HALF_OPEN);
  
  // Expect state change back to OPEN
  EXPECT_CALL(*callbacks_, onStateChange(CircuitState::HALF_OPEN, CircuitState::OPEN, _))
      .Times(1);
  
  executeInDispatcher([this]() {
    // Send failing response in half-open state
    auto response = createErrorResponse(100, jsonrpc::INTERNAL_ERROR);
    filter_->onResponse(response);
  });
  
  // Circuit should be open again
  EXPECT_EQ(filter_->getState(), CircuitState::OPEN);
}

// Test error rate threshold triggers circuit open
TEST_F(CircuitBreakerFilterTest, ErrorRateThresholdTriggersOpen) {
  // Set up for error rate testing (50% threshold)
  EXPECT_CALL(*callbacks_, onStateChange(CircuitState::CLOSED, CircuitState::OPEN, _))
      .Times(1);
  
  executeInDispatcher([this]() {
    // Send mixed success/failure to trigger error rate
    // Need minimum requests first
    for (int i = 1; i <= 10; ++i) {
      auto request = createRequest("test.method", i);
      filter_->onRequest(request);
      
      // Alternate success/failure to get exactly 50% error rate
      if (i <= 6) {
        auto response = createErrorResponse(i, jsonrpc::INTERNAL_ERROR);
        filter_->onResponse(response);
      } else {
        auto response = createSuccessResponse(i);
        filter_->onResponse(response);
      }
    }
  });
  
  // Circuit should be open due to error rate
  EXPECT_EQ(filter_->getState(), CircuitState::OPEN);
}

// Test health metrics updates
TEST_F(CircuitBreakerFilterTest, HealthMetricsUpdate) {
  EXPECT_CALL(*callbacks_, onHealthUpdate(_, _))
      .Times(AtLeast(1));
  
  executeInDispatcher([this]() {
    // Send requests with varying latencies
    for (int i = 1; i <= 5; ++i) {
      auto request = createRequest("test.method", i);
      filter_->onRequest(request);
      
      // Simulate some latency
      std::this_thread::sleep_for(10ms);
      
      auto response = createSuccessResponse(i);
      filter_->onResponse(response);
    }
  });
  
  // Get health metrics
  double success_rate;
  uint64_t avg_latency;
  filter_->getHealthMetrics(success_rate, avg_latency);
  
  EXPECT_EQ(success_rate, 1.0);  // All successful
  EXPECT_GT(avg_latency, 0);     // Some latency recorded
}

// Test client error codes don't trigger circuit
TEST_F(CircuitBreakerFilterTest, ClientErrorsDontTriggerCircuit) {
  executeInDispatcher([this]() {
    // Send client errors (4xx)
    for (int i = 1; i <= 5; ++i) {
      auto request = createRequest("test.method", i);
      filter_->onRequest(request);
      
      // Client errors shouldn't trigger circuit
      auto response = createErrorResponse(i, jsonrpc::INVALID_PARAMS);
      filter_->onResponse(response);
    }
  });
  
  // Circuit should remain closed
  EXPECT_EQ(filter_->getState(), CircuitState::CLOSED);
}

// Test protocol errors are tracked
TEST_F(CircuitBreakerFilterTest, ProtocolErrorsTracked) {
  EXPECT_CALL(*callbacks_, onStateChange(CircuitState::CLOSED, CircuitState::OPEN, _))
      .Times(1);
  
  executeInDispatcher([this]() {
    // Protocol errors should count as failures
    for (int i = 1; i <= 3; ++i) {
      Error error(jsonrpc::INTERNAL_ERROR, "Protocol error");
      filter_->onProtocolError(error);
    }
  });
  
  // Circuit should be open due to protocol errors
  EXPECT_EQ(filter_->getState(), CircuitState::OPEN);
}

} // namespace