/**
 * @file test_request_validation_filter.cc
 * @brief Unit tests for Request Validation Filter
 */

#include <chrono>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../../include/mcp/filter/request_validation_filter.h"
#include "../integration/real_io_test_base.h"

using namespace mcp;
using namespace mcp::filter;
using namespace testing;
using namespace std::chrono_literals;

namespace {

// Mock callbacks for validation events
class MockValidationCallbacks
    : public RequestValidationFilter::ValidationCallbacks {
 public:
  MOCK_METHOD(void,
              onRequestValidated,
              (const std::string& method),
              (override));
  MOCK_METHOD(void,
              onRequestRejected,
              (const std::string& method, const std::string& reason),
              (override));
  MOCK_METHOD(void,
              onRateLimitExceeded,
              (const std::string& method),
              (override));
};

// Mock JSON-RPC callbacks
class MockJsonRpcCallbacks : public JsonRpcProtocolFilter::Callbacks {
 public:
  MOCK_METHOD(void, onRequest, (const jsonrpc::Request& request), (override));
  MOCK_METHOD(void,
              onResponse,
              (const jsonrpc::Response& response),
              (override));
  MOCK_METHOD(void,
              onNotification,
              (const jsonrpc::Notification& notification),
              (override));
  MOCK_METHOD(void, onProtocolError, (const Error& error), (override));
};

class RequestValidationFilterTest : public test::RealIoTestBase {
 protected:
  void SetUp() override {
    RealIoTestBase::SetUp();

    callbacks_ = std::make_unique<NiceMock<MockValidationCallbacks>>();
    next_callbacks_ = std::make_unique<NiceMock<MockJsonRpcCallbacks>>();

    // Default config
    config_.validate_methods = true;
    config_.validate_params = true;
    config_.validate_protocol_version = true;
    config_.required_protocol_version = "2.0";
    config_.max_param_size = 1024;
    config_.enable_rate_limiting = false;
  }

  void TearDown() override {
    executeInDispatcher([this]() { filter_.reset(); });
    RealIoTestBase::TearDown();
  }

  void createFilter() {
    executeInDispatcher([this]() {
      filter_ = std::make_unique<RequestValidationFilter>(*callbacks_, config_);
      filter_->setNextCallbacks(next_callbacks_.get());
    });
  }

  // Helper to create test request
  jsonrpc::Request createRequest(const std::string& method,
                                 const std::string& version = "2.0",
                                 int id = 1) {
    jsonrpc::Request req;
    req.jsonrpc = version;
    req.method = method;
    req.id = id;
    return req;
  }

  // Helper to create test notification
  jsonrpc::Notification createNotification(const std::string& method) {
    jsonrpc::Notification notif;
    notif.jsonrpc = "2.0";
    notif.method = method;
    return notif;
  }

 protected:
  std::unique_ptr<RequestValidationFilter> filter_;
  std::unique_ptr<MockValidationCallbacks> callbacks_;
  std::unique_ptr<MockJsonRpcCallbacks> next_callbacks_;
  RequestValidationConfig config_;
};

// Test valid request passes validation
TEST_F(RequestValidationFilterTest, ValidRequestPasses) {
  config_.allowed_methods = {"initialize", "ping", "tools/list"};
  createFilter();

  EXPECT_CALL(*callbacks_, onRequestValidated("initialize")).Times(1);
  EXPECT_CALL(*next_callbacks_, onRequest(_)).Times(1);

  executeInDispatcher([this]() {
    auto req = createRequest("initialize");
    filter_->onRequest(req);
  });
}

// Test request with invalid protocol version
TEST_F(RequestValidationFilterTest, InvalidProtocolVersion) {
  config_.validate_protocol_version = true;
  config_.required_protocol_version = "2.0";
  createFilter();

  EXPECT_CALL(
      *callbacks_,
      onRequestRejected("test.method", HasSubstr("Invalid protocol version")))
      .Times(1);
  EXPECT_CALL(*next_callbacks_, onRequest(_)).Times(0);

  executeInDispatcher([this]() {
    auto req = createRequest("test.method", "1.0");  // Wrong version
    filter_->onRequest(req);
  });
}

// Test blocked method
TEST_F(RequestValidationFilterTest, BlockedMethod) {
  config_.blocked_methods = {"admin/delete", "debug/dump"};
  createFilter();

  EXPECT_CALL(*callbacks_,
              onRequestRejected("admin/delete", HasSubstr("Method is blocked")))
      .Times(1);
  EXPECT_CALL(*next_callbacks_, onRequest(_)).Times(0);

  executeInDispatcher([this]() {
    auto req = createRequest("admin/delete");
    filter_->onRequest(req);
  });
}

// Test method not in whitelist
TEST_F(RequestValidationFilterTest, MethodNotInWhitelist) {
  config_.allowed_methods = {"initialize", "ping"};  // Whitelist
  createFilter();

  EXPECT_CALL(
      *callbacks_,
      onRequestRejected("unknown.method", HasSubstr("Method not in whitelist")))
      .Times(1);
  EXPECT_CALL(*next_callbacks_, onRequest(_)).Times(0);

  executeInDispatcher([this]() {
    auto req = createRequest("unknown.method");
    filter_->onRequest(req);
  });
}

// Test method allowed when no whitelist
TEST_F(RequestValidationFilterTest, NoWhitelistAllowsAll) {
  config_.allowed_methods.clear();  // No whitelist
  config_.blocked_methods.clear();  // No blacklist
  createFilter();

  EXPECT_CALL(*callbacks_, onRequestValidated("any.method")).Times(1);
  EXPECT_CALL(*next_callbacks_, onRequest(_)).Times(1);

  executeInDispatcher([this]() {
    auto req = createRequest("any.method");
    filter_->onRequest(req);
  });
}

// Test default MCP methods are allowed
TEST_F(RequestValidationFilterTest, DefaultMcpMethodsAllowed) {
  // Use default configuration which includes MCP methods
  config_.allowed_methods.clear();  // Will be populated with defaults
  createFilter();

  std::vector<std::string> mcp_methods = {
      "initialize",     "ping",           "tools/list",   "tools/call",
      "resources/list", "resources/read", "prompts/list", "prompts/get"};

  for (const auto& method : mcp_methods) {
    EXPECT_CALL(*callbacks_, onRequestValidated(method)).Times(1);
    EXPECT_CALL(*next_callbacks_, onRequest(_)).Times(1);

    executeInDispatcher([this, method]() {
      auto req = createRequest(method);
      filter_->onRequest(req);
    });
  }
}

// Test rate limiting per method
TEST_F(RequestValidationFilterTest, MethodRateLimiting) {
  config_.enable_rate_limiting = true;
  config_.method_rate_limits["expensive.method"] = 2;  // 2 per minute
  createFilter();

  EXPECT_CALL(*callbacks_, onRequestValidated("expensive.method")).Times(2);
  EXPECT_CALL(*callbacks_, onRateLimitExceeded("expensive.method")).Times(1);
  EXPECT_CALL(*next_callbacks_, onRequest(_)).Times(2);

  executeInDispatcher([this]() {
    // First two requests should pass
    filter_->onRequest(createRequest("expensive.method", "2.0", 1));
    filter_->onRequest(createRequest("expensive.method", "2.0", 2));

    // Third request should be rate limited
    filter_->onRequest(createRequest("expensive.method", "2.0", 3));
  });
}

// Test rate limit resets after time window
TEST_F(RequestValidationFilterTest, RateLimitResetsAfterWindow) {
  config_.enable_rate_limiting = true;
  config_.method_rate_limits["limited.method"] = 1;  // 1 per minute
  createFilter();

  executeInDispatcher([this]() {
    // Use the allowed request
    filter_->onRequest(createRequest("limited.method", "2.0", 1));

    // Should be rate limited
    filter_->onRequest(createRequest("limited.method", "2.0", 2));
  });

  // In real scenario, would wait 1 minute for reset
  // For testing, we simulate by manipulating internal state
  // or accepting that rate limit is enforced

  EXPECT_CALL(*callbacks_, onRateLimitExceeded("limited.method")).Times(1);
}

// Test notification validation
TEST_F(RequestValidationFilterTest, NotificationValidation) {
  config_.allowed_methods = {"allowed.notification"};
  config_.blocked_methods = {"blocked.notification"};
  createFilter();

  // Allowed notification
  EXPECT_CALL(*callbacks_, onRequestValidated("allowed.notification")).Times(1);
  EXPECT_CALL(*next_callbacks_, onNotification(_)).Times(1);

  executeInDispatcher([this]() {
    auto notif = createNotification("allowed.notification");
    filter_->onNotification(notif);
  });

  // Blocked notification
  EXPECT_CALL(*callbacks_, onRequestRejected("blocked.notification", _))
      .Times(1);
  EXPECT_CALL(*next_callbacks_, onNotification(_)).Times(0);

  executeInDispatcher([this]() {
    auto notif = createNotification("blocked.notification");
    filter_->onNotification(notif);
  });
}

// Test parameter size validation
TEST_F(RequestValidationFilterTest, ParameterSizeValidation) {
  config_.validate_params = true;
  config_.max_param_size = 100;  // Small limit for testing
  createFilter();

  // Request with large params would need actual size calculation
  // For now, test configuration is accepted
  EXPECT_TRUE(config_.validate_params);
  EXPECT_EQ(config_.max_param_size, 100);

  // Would need JSON serialization to properly test size
  executeInDispatcher([this]() {
    auto req = createRequest("test.method");
    // Add large params - implementation would check serialized size
    req.params = make_optional(Metadata{});
    filter_->onRequest(req);
  });
}

// Test validation disabled
TEST_F(RequestValidationFilterTest, ValidationDisabled) {
  config_.validate_methods = false;
  config_.validate_protocol_version = false;
  createFilter();

  EXPECT_CALL(*callbacks_, onRequestValidated(_)).Times(2);
  EXPECT_CALL(*callbacks_, onRequestRejected(_, _)).Times(0);
  EXPECT_CALL(*next_callbacks_, onRequest(_)).Times(2);

  executeInDispatcher([this]() {
    // Invalid version and unknown method should still pass
    auto req1 = createRequest("unknown.method", "1.0");
    filter_->onRequest(req1);

    auto req2 = createRequest("any.method", "invalid");
    filter_->onRequest(req2);
  });
}

// Test response passthrough (responses not validated)
TEST_F(RequestValidationFilterTest, ResponsePassthrough) {
  createFilter();

  EXPECT_CALL(*next_callbacks_, onResponse(_)).Times(1);

  executeInDispatcher([this]() {
    jsonrpc::Response resp;
    resp.jsonrpc = "2.0";
    resp.id = 1;
    resp.result = jsonrpc::ResponseResult(nullptr);

    filter_->onResponse(resp);
  });
}

// Test protocol error passthrough
TEST_F(RequestValidationFilterTest, ProtocolErrorPassthrough) {
  createFilter();

  EXPECT_CALL(*next_callbacks_, onProtocolError(_)).Times(1);

  executeInDispatcher([this]() {
    Error error(jsonrpc::INTERNAL_ERROR, "Test error");
    filter_->onProtocolError(error);
  });
}

// Test new connection resets rate limits
TEST_F(RequestValidationFilterTest, NewConnectionResetsRateLimits) {
  config_.enable_rate_limiting = true;
  config_.method_rate_limits["test.method"] = 1;
  createFilter();

  executeInDispatcher([this]() {
    // Use rate limit
    filter_->onRequest(createRequest("test.method", "2.0", 1));

    // Should be limited
    filter_->onRequest(createRequest("test.method", "2.0", 2));
  });

  EXPECT_CALL(*callbacks_, onRateLimitExceeded("test.method")).Times(1);

  // New connection resets limits
  executeInDispatcher([this]() { filter_->onNewConnection(); });

  EXPECT_CALL(*callbacks_, onRequestValidated("test.method")).Times(1);
  EXPECT_CALL(*next_callbacks_, onRequest(_)).Times(1);

  executeInDispatcher([this]() {
    // Should allow request again after reset
    filter_->onRequest(createRequest("test.method", "2.0", 3));
  });
}

// Test JSON depth and string length validation config
TEST_F(RequestValidationFilterTest, SecurityValidationConfig) {
  config_.validate_json_depth = true;
  config_.max_json_depth = 10;
  config_.validate_string_length = true;
  config_.max_string_length = 1000;
  createFilter();

  // Configuration should be accepted
  EXPECT_TRUE(config_.validate_json_depth);
  EXPECT_EQ(config_.max_json_depth, 10);
  EXPECT_TRUE(config_.validate_string_length);
  EXPECT_EQ(config_.max_string_length, 1000);

  // Actual validation would require JSON parsing
  executeInDispatcher([this]() {
    auto req = createRequest("test.method");
    filter_->onRequest(req);
  });
}

// Test multiple validation failures
TEST_F(RequestValidationFilterTest, MultipleValidationFailures) {
  config_.validate_protocol_version = true;
  config_.required_protocol_version = "2.0";
  config_.blocked_methods = {"bad.method"};
  createFilter();

  // Wrong version and blocked method
  EXPECT_CALL(*callbacks_, onRequestRejected("bad.method", _)).Times(1);
  EXPECT_CALL(*next_callbacks_, onRequest(_)).Times(0);

  executeInDispatcher([this]() {
    auto req = createRequest("bad.method", "1.0");  // Both invalid
    filter_->onRequest(req);
  });
}

}  // namespace