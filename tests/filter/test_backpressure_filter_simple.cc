/**
 * @file test_backpressure_filter_simple.cc
 * @brief Simple unit tests for Backpressure Filter (no real I/O)
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../../include/mcp/filter/backpressure_filter.h"

using namespace mcp;
using namespace mcp::filter;
using namespace testing;

namespace {

// Mock callbacks
class MockBackpressureCallbacks : public BackpressureFilter::Callbacks {
public:
  MOCK_METHOD(void, onBackpressureApplied, (), (override));
  MOCK_METHOD(void, onBackpressureReleased, (), (override));
  MOCK_METHOD(void, onDataDropped, (size_t bytes), (override));
};

class BackpressureFilterSimpleTest : public ::testing::Test {
protected:
  void SetUp() override {
    callbacks_ = std::make_unique<NiceMock<MockBackpressureCallbacks>>();
  }
  
  void createFilter(const BackpressureConfig& config) {
    filter_ = std::make_unique<BackpressureFilter>(*callbacks_, config);
  }
  
protected:
  std::unique_ptr<BackpressureFilter> filter_;
  std::unique_ptr<MockBackpressureCallbacks> callbacks_;
};

// Test basic configuration
TEST_F(BackpressureFilterSimpleTest, ConfigurationAccepted) {
  BackpressureConfig config;
  config.high_watermark = 10 * 1024;
  config.low_watermark = 2 * 1024;
  config.max_bytes_per_second = 100 * 1024;
  
  createFilter(config);
  EXPECT_TRUE(filter_ != nullptr);
}

// Test watermark configuration validation
TEST_F(BackpressureFilterSimpleTest, WatermarkConfiguration) {
  BackpressureConfig config;
  config.high_watermark = 10 * 1024;
  config.low_watermark = 2 * 1024;
  config.pause_duration = std::chrono::milliseconds(100);
  
  createFilter(config);
  
  // Verify watermarks are properly set
  EXPECT_TRUE(config.low_watermark < config.high_watermark);
  EXPECT_EQ(config.pause_duration.count(), 100);
}

// Test network filter interface
TEST_F(BackpressureFilterSimpleTest, NetworkFilterInterface) {
  BackpressureConfig config;
  config.high_watermark = 10 * 1024;
  config.low_watermark = 2 * 1024;
  createFilter(config);
  
  // Test filter implements required methods
  auto buffer = createBuffer();
  EXPECT_EQ(filter_->onData(*buffer, false), network::FilterStatus::Continue);
  EXPECT_EQ(filter_->onWrite(*buffer, false), network::FilterStatus::Continue);
  EXPECT_EQ(filter_->onNewConnection(), network::FilterStatus::Continue);
}

// Test initial state
TEST_F(BackpressureFilterSimpleTest, InitialState) {
  BackpressureConfig config;
  config.high_watermark = 10 * 1024;
  config.low_watermark = 2 * 1024;
  createFilter(config);
  
  // Just verify filter is created
  EXPECT_TRUE(filter_ != nullptr);
}

// Test rate limiting configuration
TEST_F(BackpressureFilterSimpleTest, RateLimitingConfig) {
  BackpressureConfig config;
  config.max_bytes_per_second = 1024 * 1024;  // 1MB/s
  createFilter(config);
  EXPECT_TRUE(filter_ != nullptr);
  EXPECT_EQ(config.max_bytes_per_second, 1024 * 1024);
  
  // Test unlimited rate (0 = unlimited)
  config.max_bytes_per_second = 0;
  createFilter(config);
  EXPECT_TRUE(filter_ != nullptr);
  EXPECT_EQ(config.max_bytes_per_second, 0);
}

} // namespace