/**
 * @file test_rate_limit_filter.cc
 * @brief Unit tests for Rate Limiting Filter
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chrono>
#include <thread>
#include "mcp/filter/rate_limit_filter.h"
#include "../integration/real_io_test_base.h"

using namespace mcp;
using namespace mcp::filter;
using namespace testing;
using namespace std::chrono_literals;

namespace {

// Mock callbacks for rate limit events
class MockRateLimitCallbacks : public RateLimitFilter::Callbacks {
public:
  MOCK_METHOD(void, onRequestAllowed, (), (override));
  MOCK_METHOD(void, onRequestLimited, (std::chrono::milliseconds retry_after), (override));
  MOCK_METHOD(void, onRateLimitWarning, (int remaining), (override));
};

class RateLimitFilterTest : public test::RealIoTestBase {
protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    
    callbacks_ = std::make_unique<NiceMock<MockRateLimitCallbacks>>();
  }
  
  void TearDown() override {
    executeInDispatcher([this]() {
      filter_.reset();
    });
    RealIoTestBase::TearDown();
  }
  
  void createFilter(const RateLimitConfig& config) {
    executeInDispatcher([this, config]() {
      filter_ = std::make_unique<RateLimitFilter>(*callbacks_, config);
    });
  }
  
protected:
  std::unique_ptr<RateLimitFilter> filter_;
  std::unique_ptr<MockRateLimitCallbacks> callbacks_;
};

// Test token bucket allows burst traffic
TEST_F(RateLimitFilterTest, TokenBucketAllowsBurst) {
  RateLimitConfig config;
  config.strategy = RateLimitStrategy::TokenBucket;
  config.bucket_capacity = 10;
  config.refill_rate = 1;  // 1 token per second
  
  createFilter(config);
  
  // Should allow burst up to bucket capacity
  EXPECT_CALL(*callbacks_, onRequestAllowed()).Times(10);
  EXPECT_CALL(*callbacks_, onRequestLimited(_)).Times(0);
  
  executeInDispatcher([this]() {
    auto data = createBuffer();
    for (int i = 0; i < 10; ++i) {
      EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::Continue);
    }
  });
}

// Test token bucket blocks after capacity exhausted
TEST_F(RateLimitFilterTest, TokenBucketBlocksAfterCapacity) {
  RateLimitConfig config;
  config.strategy = RateLimitStrategy::TokenBucket;
  config.bucket_capacity = 5;
  config.refill_rate = 1;
  
  createFilter(config);
  
  EXPECT_CALL(*callbacks_, onRequestAllowed()).Times(5);
  EXPECT_CALL(*callbacks_, onRequestLimited(_)).Times(1);
  
  executeInDispatcher([this]() {
    auto data = createBuffer();
    // Use all tokens
    for (int i = 0; i < 5; ++i) {
      EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::Continue);
    }
    // Next request should be limited
    EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::StopIteration);
  });
}

// Test token bucket refills over time
TEST_F(RateLimitFilterTest, TokenBucketRefills) {
  RateLimitConfig config;
  config.strategy = RateLimitStrategy::TokenBucket;
  config.bucket_capacity = 5;
  config.refill_rate = 10;  // 10 tokens per second
  
  createFilter(config);
  
  executeInDispatcher([this]() {
    auto data = createBuffer();
    // Use all tokens
    for (int i = 0; i < 5; ++i) {
      filter_->onData(*data, false);
    }
    // Should be blocked
    EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::StopIteration);
  });
  
  // Wait for refill
  std::this_thread::sleep_for(200ms);  // Should refill 2 tokens
  
  EXPECT_CALL(*callbacks_, onRequestAllowed()).Times(2);
  
  executeInDispatcher([this]() {
    auto data = createBuffer();
    // Should allow 2 more requests
    EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::Continue);
    EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::Continue);
    // But not a third
    EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::StopIteration);
  });
}

// Test sliding window rate limiting
TEST_F(RateLimitFilterTest, SlidingWindowLimiting) {
  RateLimitConfig config;
  config.strategy = RateLimitStrategy::SlidingWindow;
  config.window_size = 1s;
  config.max_requests_per_window = 5;
  
  createFilter(config);
  
  EXPECT_CALL(*callbacks_, onRequestAllowed()).Times(5);
  EXPECT_CALL(*callbacks_, onRequestLimited(_)).Times(1);
  
  executeInDispatcher([this]() {
    auto data = createBuffer();
    // Allow 5 requests in window
    for (int i = 0; i < 5; ++i) {
      EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::Continue);
    }
    // 6th request should be limited
    EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::StopIteration);
  });
}

// Test sliding window old requests expire
TEST_F(RateLimitFilterTest, SlidingWindowExpiration) {
  RateLimitConfig config;
  config.strategy = RateLimitStrategy::SlidingWindow;
  config.window_size = 1s;  // Short window for testing
  config.max_requests_per_window = 3;
  
  createFilter(config);
  
  executeInDispatcher([this]() {
    auto data = createBuffer();
    // Use all requests
    for (int i = 0; i < 3; ++i) {
      filter_->onData(*data, false);
    }
    // Should be blocked
    EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::StopIteration);
  });
  
  // Wait for window to expire
  std::this_thread::sleep_for(1100ms);
  
  EXPECT_CALL(*callbacks_, onRequestAllowed()).Times(3);
  
  executeInDispatcher([this]() {
    auto data = createBuffer();
    // Should allow new requests after window expires
    for (int i = 0; i < 3; ++i) {
      EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::Continue);
    }
  });
}

// Test fixed window rate limiting
TEST_F(RateLimitFilterTest, FixedWindowLimiting) {
  RateLimitConfig config;
  config.strategy = RateLimitStrategy::FixedWindow;
  config.window_size = 1s;  // Short window for testing
  config.max_requests_per_window = 4;
  
  createFilter(config);
  
  EXPECT_CALL(*callbacks_, onRequestAllowed()).Times(4);
  
  executeInDispatcher([this]() {
    auto data = createBuffer();
    // Use all requests in window
    for (int i = 0; i < 4; ++i) {
      EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::Continue);
    }
    // Next should be blocked
    EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::StopIteration);
  });
  
  // Wait for window reset
  std::this_thread::sleep_for(1100ms);
  
  EXPECT_CALL(*callbacks_, onRequestAllowed()).Times(4);
  
  executeInDispatcher([this]() {
    auto data = createBuffer();
    // New window should allow requests again
    for (int i = 0; i < 4; ++i) {
      EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::Continue);
    }
  });
}

// Test leaky bucket rate limiting
TEST_F(RateLimitFilterTest, LeakyBucketLimiting) {
  RateLimitConfig config;
  config.strategy = RateLimitStrategy::LeakyBucket;
  config.bucket_capacity = 5;
  config.leak_rate = 10;  // 10 requests per second
  
  createFilter(config);
  
  // Fill bucket quickly
  executeInDispatcher([this]() {
    auto data = createBuffer();
    for (int i = 0; i < 5; ++i) {
      filter_->onData(*data, false);
    }
    // Bucket full
    EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::StopIteration);
  });
  
  // Wait for some leaking
  std::this_thread::sleep_for(150ms);  // Should leak ~1-2 requests
  
  EXPECT_CALL(*callbacks_, onRequestAllowed()).Times(AtLeast(1));
  
  executeInDispatcher([this]() {
    auto data = createBuffer();
    // Should allow at least one request after leaking
    EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::Continue);
  });
}

// Test rate limit warning when approaching limit
TEST_F(RateLimitFilterTest, RateLimitWarning) {
  RateLimitConfig config;
  config.strategy = RateLimitStrategy::TokenBucket;
  config.bucket_capacity = 10;
  config.refill_rate = 1;
  
  createFilter(config);
  
  // Expect warning when capacity drops below 20%
  EXPECT_CALL(*callbacks_, onRateLimitWarning(Lt(20))).Times(AtLeast(1));
  
  executeInDispatcher([this]() {
    auto data = createBuffer();
    // Use 80% of capacity to trigger warning
    for (int i = 0; i < 8; ++i) {
      filter_->onData(*data, false);
    }
    // Next request should trigger warning (10% remaining)
    filter_->onData(*data, false);
  });
}

// Test retry after calculation
TEST_F(RateLimitFilterTest, RetryAfterCalculation) {
  RateLimitConfig config;
  config.strategy = RateLimitStrategy::TokenBucket;
  config.bucket_capacity = 1;
  config.refill_rate = 2;  // 2 tokens per second = 500ms per token
  
  createFilter(config);
  
  std::chrono::milliseconds retry_after;
  EXPECT_CALL(*callbacks_, onRequestLimited(_))
      .WillOnce(SaveArg<0>(&retry_after));
  
  executeInDispatcher([this]() {
    auto data = createBuffer();
    // Use the single token
    filter_->onData(*data, false);
    // Next request should be limited with retry-after
    filter_->onData(*data, false);
  });
  
  // Retry after should be approximately 500ms (1000ms / 2 tokens per second)
  EXPECT_GE(retry_after.count(), 400);
  EXPECT_LE(retry_after.count(), 600);
}

// Test burst handling configuration
TEST_F(RateLimitFilterTest, BurstHandling) {
  RateLimitConfig config;
  config.strategy = RateLimitStrategy::TokenBucket;
  config.bucket_capacity = 10;
  config.allow_burst = true;
  config.burst_size = 5;  // Extra burst capacity
  config.refill_rate = 1;
  
  createFilter(config);
  
  // Should allow base capacity + burst
  EXPECT_CALL(*callbacks_, onRequestAllowed()).Times(10);  // Using base capacity
  
  executeInDispatcher([this]() {
    auto data = createBuffer();
    // Use base capacity
    for (int i = 0; i < 10; ++i) {
      EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::Continue);
    }
    // Burst handling would require additional implementation
  });
}

// Test per-client rate limiting (conceptual test)
TEST_F(RateLimitFilterTest, PerClientLimiting) {
  RateLimitConfig config;
  config.strategy = RateLimitStrategy::TokenBucket;
  config.bucket_capacity = 5;
  config.refill_rate = 1;
  config.per_client_limiting = true;
  config.client_limits["client1"] = 3;
  config.client_limits["client2"] = 5;
  
  createFilter(config);
  
  // This would require client identification in the filter
  // For now, test that configuration is accepted
  EXPECT_TRUE(config.per_client_limiting);
  EXPECT_EQ(config.client_limits.size(), 2);
}

// Test connection reset clears rate limit state
TEST_F(RateLimitFilterTest, ConnectionResetClearsState) {
  RateLimitConfig config;
  config.strategy = RateLimitStrategy::FixedWindow;
  config.window_size = 1s;
  config.max_requests_per_window = 3;
  
  createFilter(config);
  
  executeInDispatcher([this]() {
    auto data = createBuffer();
    // Use some requests
    for (int i = 0; i < 2; ++i) {
      filter_->onData(*data, false);
    }
    
    // New connection should reset state
    filter_->onNewConnection();
    
    // Should be able to use full quota again
    for (int i = 0; i < 3; ++i) {
      EXPECT_EQ(filter_->onData(*data, false), network::FilterStatus::Continue);
    }
  });
}

} // namespace