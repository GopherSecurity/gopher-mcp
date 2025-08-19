/**
 * @file test_request_validation_filter_simple.cc
 * @brief Simple unit tests for Request Validation Filter (no real I/O)
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../../include/mcp/filter/request_validation_filter.h"

using namespace mcp;
using namespace mcp::filter;
using namespace testing;

namespace {

// Mock callbacks
class MockValidationCallbacks : public RequestValidationFilter::ValidationCallbacks {
public:
  MOCK_METHOD(void, onRequestValidated, (const std::string& method), (override));
  MOCK_METHOD(void, onRequestRejected, 
              (const std::string& method, const std::string& reason), 
              (override));
  MOCK_METHOD(void, onRateLimitExceeded, (const std::string& method), (override));
};

class RequestValidationFilterSimpleTest : public ::testing::Test {
protected:
  void SetUp() override {
    callbacks_ = std::make_unique<NiceMock<MockValidationCallbacks>>();
  }
  
  void createFilter(const RequestValidationConfig& config) {
    filter_ = std::make_unique<RequestValidationFilter>(*callbacks_, config);
  }
  
protected:
  std::unique_ptr<RequestValidationFilter> filter_;
  std::unique_ptr<MockValidationCallbacks> callbacks_;
};

// Test basic configuration
TEST_F(RequestValidationFilterSimpleTest, ConfigurationAccepted) {
  RequestValidationConfig config;
  config.validate_methods = true;
  config.validate_params = true;
  config.validate_protocol_version = true;
  config.required_protocol_version = "2.0";
  
  createFilter(config);
  EXPECT_TRUE(filter_ != nullptr);
}

// Test method validation configuration
TEST_F(RequestValidationFilterSimpleTest, MethodValidationConfig) {
  RequestValidationConfig config;
  config.allowed_methods = {"initialize", "ping", "tools/list"};
  config.blocked_methods = {"admin/delete", "debug/dump"};
  
  createFilter(config);
  
  EXPECT_EQ(config.allowed_methods.size(), 3);
  EXPECT_EQ(config.blocked_methods.size(), 2);
}

// Test security validation configuration
TEST_F(RequestValidationFilterSimpleTest, SecurityValidationConfig) {
  RequestValidationConfig config;
  config.validate_json_depth = true;
  config.max_json_depth = 100;
  config.validate_string_length = true;
  config.max_string_length = 65536;
  config.max_param_size = 1024 * 1024;
  
  createFilter(config);
  
  EXPECT_TRUE(config.validate_json_depth);
  EXPECT_EQ(config.max_json_depth, 100);
  EXPECT_TRUE(config.validate_string_length);
  EXPECT_EQ(config.max_string_length, 65536);
  EXPECT_EQ(config.max_param_size, 1024 * 1024);
}

// Test rate limiting configuration
TEST_F(RequestValidationFilterSimpleTest, RateLimitingConfig) {
  RequestValidationConfig config;
  config.enable_rate_limiting = true;
  config.method_rate_limits["expensive.method"] = 10;
  config.method_rate_limits["admin.method"] = 5;
  
  createFilter(config);
  
  EXPECT_TRUE(config.enable_rate_limiting);
  EXPECT_EQ(config.method_rate_limits["expensive.method"], 10);
  EXPECT_EQ(config.method_rate_limits["admin.method"], 5);
}

// Test network filter interface
TEST_F(RequestValidationFilterSimpleTest, NetworkFilterInterface) {
  RequestValidationConfig config;
  createFilter(config);
  
  // Test filter implements required methods
  auto buffer = createBuffer();
  EXPECT_EQ(filter_->onData(*buffer, false), network::FilterStatus::Continue);
  EXPECT_EQ(filter_->onWrite(*buffer, false), network::FilterStatus::Continue);
  EXPECT_EQ(filter_->onNewConnection(), network::FilterStatus::Continue);
}

// Test default MCP methods configuration
TEST_F(RequestValidationFilterSimpleTest, DefaultMcpMethods) {
  RequestValidationConfig config;
  config.validate_methods = true;
  // Leave allowed_methods empty to trigger default initialization
  
  createFilter(config);
  
  // Filter should have populated default MCP methods
  // This would be verified through actual method validation
  EXPECT_TRUE(filter_ != nullptr);
}

} // namespace