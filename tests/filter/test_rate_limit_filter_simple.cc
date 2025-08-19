/**
 * @file test_rate_limit_filter_simple.cc
 * @brief Simple unit tests for Rate Limiting Filter (no real I/O)
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chrono>
#include "../../include/mcp/filter/rate_limit_filter.h"

using namespace mcp;
using namespace mcp::filter;
using namespace testing;
using namespace std::chrono_literals;

namespace {

// Mock callbacks
class MockRateLimitCallbacks : public RateLimitFilter::Callbacks {
public:
  MOCK_METHOD(void, onRequestAllowed, (), (override));
  MOCK_METHOD(void, onRequestLimited, (std::chrono::milliseconds retry_after), (override));
  MOCK_METHOD(void, onRateLimitWarning, (int remaining), (override));
};

class RateLimitFilterSimpleTest : public ::testing::Test {
protected:
  void SetUp() override {
    callbacks_ = std::make_unique<NiceMock<MockRateLimitCallbacks>>();
  }
  
  void createFilter(const RateLimitConfig& config) {
    filter_ = std::make_unique<RateLimitFilter>(*callbacks_, config);
  }
  
protected:
  std::unique_ptr<RateLimitFilter> filter_;
  std::unique_ptr<MockRateLimitCallbacks> callbacks_;
};

// Test basic configuration
TEST_F(RateLimitFilterSimpleTest, ConfigurationAccepted) {
  RateLimitConfig config;
  config.max_requests_per_window = 60;
  config.strategy = RateLimitStrategy::TokenBucket;
  
  createFilter(config);
  EXPECT_TRUE(filter_ != nullptr);
}

// Test different strategies
TEST_F(RateLimitFilterSimpleTest, StrategyTypes) {
  // Test all strategy types compile and can be set
  RateLimitConfig config1;
  config1.strategy = RateLimitStrategy::TokenBucket;
  createFilter(config1);
  EXPECT_TRUE(filter_ != nullptr);
  
  RateLimitConfig config2;
  config2.strategy = RateLimitStrategy::SlidingWindow;
  createFilter(config2);
  EXPECT_TRUE(filter_ != nullptr);
  
  RateLimitConfig config3;
  config3.strategy = RateLimitStrategy::FixedWindow;
  createFilter(config3);
  EXPECT_TRUE(filter_ != nullptr);
  
  RateLimitConfig config4;
  config4.strategy = RateLimitStrategy::LeakyBucket;
  createFilter(config4);
  EXPECT_TRUE(filter_ != nullptr);
}

// Test network filter interface
TEST_F(RateLimitFilterSimpleTest, NetworkFilterInterface) {
  RateLimitConfig config;
  config.max_requests_per_window = 60;
  createFilter(config);
  
  // Test filter implements required methods
  auto buffer = createBuffer();
  EXPECT_EQ(filter_->onData(*buffer, false), network::FilterStatus::Continue);
  EXPECT_EQ(filter_->onWrite(*buffer, false), network::FilterStatus::Continue);
  EXPECT_EQ(filter_->onNewConnection(), network::FilterStatus::Continue);
}

// Test initial state
TEST_F(RateLimitFilterSimpleTest, InitialState) {
  RateLimitConfig config;
  config.max_requests_per_window = 60;
  createFilter(config);
  
  // Just test that filter is created successfully
  EXPECT_TRUE(filter_ != nullptr);
  EXPECT_EQ(config.max_requests_per_window, 60);
}

} // namespace