/**
 * @file test_backpressure_filter.cc
 * @brief Unit tests for Backpressure Filter
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chrono>
#include <thread>
#include "../../include/mcp/filter/backpressure_filter.h"
#include "../integration/real_io_test_base.h"

using namespace mcp;
using namespace mcp::filter;
using namespace testing;
using namespace std::chrono_literals;

namespace {

// Mock callbacks for backpressure events
class MockBackpressureCallbacks : public BackpressureFilter::Callbacks {
public:
  MOCK_METHOD(void, onBackpressureApplied, (), (override));
  MOCK_METHOD(void, onBackpressureReleased, (), (override));
  MOCK_METHOD(void, onDataDropped, (size_t bytes), (override));
};

class BackpressureFilterTest : public test::RealIoTestBase {
protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    
    callbacks_ = std::make_unique<NiceMock<MockBackpressureCallbacks>>();
  }
  
  void TearDown() override {
    executeInDispatcher([this]() {
      filter_.reset();
    });
    RealIoTestBase::TearDown();
  }
  
  void createFilter(const BackpressureConfig& config) {
    executeInDispatcher([this, config]() {
      filter_ = std::make_unique<BackpressureFilter>(*callbacks_, config);
    });
  }
  
  // Helper to create buffer with specific size
  std::unique_ptr<Buffer> createBufferWithSize(size_t size) {
    auto buffer = createBuffer();
    std::string data(size, 'X');
    buffer->add(data);
    return buffer;
  }
  
protected:
  std::unique_ptr<BackpressureFilter> filter_;
  std::unique_ptr<MockBackpressureCallbacks> callbacks_;
};

// Test normal operation below watermarks
TEST_F(BackpressureFilterTest, NormalOperationBelowWatermarks) {
  BackpressureConfig config;
  config.high_watermark = 1024 * 1024;  // 1MB
  config.low_watermark = 256 * 1024;    // 256KB
  
  createFilter(config);
  
  // No backpressure events should occur
  EXPECT_CALL(*callbacks_, onBackpressureApplied()).Times(0);
  EXPECT_CALL(*callbacks_, onBackpressureReleased()).Times(0);
  
  executeInDispatcher([this]() {
    // Send data below high watermark
    auto buffer = createBufferWithSize(100 * 1024);  // 100KB
    EXPECT_EQ(filter_->onData(*buffer, false), network::FilterStatus::Continue);
    
    // Buffer size should be tracked
    EXPECT_EQ(filter_->getCurrentBufferSize(), 100 * 1024);
    EXPECT_FALSE(filter_->isPaused());
  });
}

// Test backpressure applied when high watermark exceeded
TEST_F(BackpressureFilterTest, BackpressureAppliedAtHighWatermark) {
  BackpressureConfig config;
  config.high_watermark = 100 * 1024;  // 100KB
  config.low_watermark = 50 * 1024;    // 50KB
  
  createFilter(config);
  
  EXPECT_CALL(*callbacks_, onBackpressureApplied()).Times(1);
  
  executeInDispatcher([this]() {
    // Send data to exceed high watermark
    auto buffer1 = createBufferWithSize(60 * 1024);  // 60KB
    EXPECT_EQ(filter_->onData(*buffer1, false), network::FilterStatus::Continue);
    
    auto buffer2 = createBufferWithSize(50 * 1024);  // 50KB more = 110KB total
    EXPECT_EQ(filter_->onData(*buffer2, false), network::FilterStatus::StopIteration);
    
    EXPECT_TRUE(filter_->isPaused());
    EXPECT_EQ(filter_->getCurrentBufferSize(), 110 * 1024);
  });
}

// Test backpressure released when below low watermark
TEST_F(BackpressureFilterTest, BackpressureReleasedAtLowWatermark) {
  BackpressureConfig config;
  config.high_watermark = 100 * 1024;  // 100KB
  config.low_watermark = 50 * 1024;    // 50KB
  
  createFilter(config);
  
  // First apply backpressure
  executeInDispatcher([this]() {
    auto buffer = createBufferWithSize(110 * 1024);
    filter_->onData(*buffer, false);
  });
  
  EXPECT_TRUE(filter_->isPaused());
  
  EXPECT_CALL(*callbacks_, onBackpressureReleased()).Times(1);
  
  executeInDispatcher([this]() {
    // Simulate writing data to reduce buffer
    auto write_buffer = createBufferWithSize(70 * 1024);
    filter_->onWrite(*write_buffer, false);
    
    // Buffer should be at 40KB now (110 - 70), below low watermark
    EXPECT_FALSE(filter_->isPaused());
    EXPECT_EQ(filter_->getCurrentBufferSize(), 40 * 1024);
  });
}

// Test data dropping when buffer is too full
TEST_F(BackpressureFilterTest, DataDroppedWhenBufferTooFull) {
  BackpressureConfig config;
  config.high_watermark = 100 * 1024;  // 100KB
  config.low_watermark = 50 * 1024;    // 50KB
  
  createFilter(config);
  
  // Fill buffer beyond 2x high watermark
  executeInDispatcher([this]() {
    auto buffer1 = createBufferWithSize(210 * 1024);  // More than 2x high watermark
    filter_->onData(*buffer1, false);
  });
  
  EXPECT_CALL(*callbacks_, onDataDropped(10 * 1024)).Times(1);
  
  executeInDispatcher([this]() {
    // This data should be dropped
    auto buffer2 = createBufferWithSize(10 * 1024);
    EXPECT_EQ(filter_->onData(*buffer2, false), network::FilterStatus::StopIteration);
    
    // Buffer should have been drained in the dropped data
    EXPECT_EQ(buffer2->length(), 0);
  });
}

// Test rate limiting
TEST_F(BackpressureFilterTest, RateLimitingEnforced) {
  BackpressureConfig config;
  config.high_watermark = 1024 * 1024;  // 1MB
  config.low_watermark = 256 * 1024;    // 256KB
  config.max_bytes_per_second = 100 * 1024;  // 100KB/s
  
  createFilter(config);
  
  EXPECT_CALL(*callbacks_, onBackpressureApplied()).Times(1);
  
  executeInDispatcher([this]() {
    // Send data at rate exceeding limit
    auto buffer = createBufferWithSize(110 * 1024);  // 110KB exceeds 100KB/s limit
    EXPECT_EQ(filter_->onData(*buffer, false), network::FilterStatus::StopIteration);
    
    EXPECT_TRUE(filter_->isPaused());
  });
}

// Test rate limit resets per second
TEST_F(BackpressureFilterTest, RateLimitResetsPerSecond) {
  BackpressureConfig config;
  config.high_watermark = 1024 * 1024;
  config.low_watermark = 256 * 1024;
  config.max_bytes_per_second = 50 * 1024;  // 50KB/s
  
  createFilter(config);
  
  executeInDispatcher([this]() {
    // Use up rate limit
    auto buffer1 = createBufferWithSize(50 * 1024);
    EXPECT_EQ(filter_->onData(*buffer1, false), network::FilterStatus::Continue);
    
    // Should be rate limited now
    auto buffer2 = createBufferWithSize(10 * 1024);
    EXPECT_EQ(filter_->onData(*buffer2, false), network::FilterStatus::StopIteration);
  });
  
  // Wait for rate limit reset
  std::this_thread::sleep_for(1100ms);
  
  EXPECT_CALL(*callbacks_, onBackpressureReleased()).Times(AtLeast(1));
  
  executeInDispatcher([this]() {
    // Should allow data again after reset
    auto buffer3 = createBufferWithSize(40 * 1024);
    EXPECT_EQ(filter_->onData(*buffer3, false), network::FilterStatus::Continue);
    
    EXPECT_FALSE(filter_->isPaused());
  });
}

// Test write reduces buffer size
TEST_F(BackpressureFilterTest, WriteReducesBufferSize) {
  BackpressureConfig config;
  config.high_watermark = 100 * 1024;
  config.low_watermark = 50 * 1024;
  
  createFilter(config);
  
  executeInDispatcher([this]() {
    // Add data to buffer
    auto read_buffer = createBufferWithSize(80 * 1024);
    filter_->onData(*read_buffer, false);
    EXPECT_EQ(filter_->getCurrentBufferSize(), 80 * 1024);
    
    // Write some data
    auto write_buffer = createBufferWithSize(30 * 1024);
    filter_->onWrite(*write_buffer, false);
    
    // Buffer should be reduced
    EXPECT_EQ(filter_->getCurrentBufferSize(), 50 * 1024);
  });
}

// Test new connection resets state
TEST_F(BackpressureFilterTest, NewConnectionResetsState) {
  BackpressureConfig config;
  config.high_watermark = 100 * 1024;
  config.low_watermark = 50 * 1024;
  config.max_bytes_per_second = 50 * 1024;
  
  createFilter(config);
  
  executeInDispatcher([this]() {
    // Fill buffer and use rate limit
    auto buffer = createBufferWithSize(80 * 1024);
    filter_->onData(*buffer, false);
    
    EXPECT_EQ(filter_->getCurrentBufferSize(), 80 * 1024);
    EXPECT_EQ(filter_->getBytesThisSecond(), 80 * 1024);
    
    // New connection should reset everything
    filter_->onNewConnection();
    
    EXPECT_EQ(filter_->getCurrentBufferSize(), 0);
    EXPECT_FALSE(filter_->isPaused());
    EXPECT_EQ(filter_->getBytesThisSecond(), 0);
  });
}

// Test hysteresis between high and low watermarks
TEST_F(BackpressureFilterTest, WatermarkHysteresis) {
  BackpressureConfig config;
  config.high_watermark = 100 * 1024;  // 100KB
  config.low_watermark = 50 * 1024;    // 50KB
  
  createFilter(config);
  
  EXPECT_CALL(*callbacks_, onBackpressureApplied()).Times(1);
  EXPECT_CALL(*callbacks_, onBackpressureReleased()).Times(1);
  
  executeInDispatcher([this]() {
    // Add data up to high watermark
    auto buffer1 = createBufferWithSize(110 * 1024);
    filter_->onData(*buffer1, false);
    EXPECT_TRUE(filter_->isPaused());
    
    // Write just below high watermark but above low - should stay paused
    auto write1 = createBufferWithSize(30 * 1024);
    filter_->onWrite(*write1, false);
    EXPECT_TRUE(filter_->isPaused());  // Still paused at 80KB
    
    // Write below low watermark - should release
    auto write2 = createBufferWithSize(35 * 1024);
    filter_->onWrite(*write2, false);
    EXPECT_FALSE(filter_->isPaused());  // Released at 45KB
  });
}

// Test pause duration configuration
TEST_F(BackpressureFilterTest, PauseDurationConfiguration) {
  BackpressureConfig config;
  config.high_watermark = 100 * 1024;
  config.low_watermark = 50 * 1024;
  config.pause_duration = 200ms;  // Custom pause duration
  
  createFilter(config);
  
  // Test that configuration is accepted
  executeInDispatcher([this]() {
    auto buffer = createBufferWithSize(110 * 1024);
    filter_->onData(*buffer, false);
    EXPECT_TRUE(filter_->isPaused());
    
    // Pause duration would affect retry timing in real implementation
  });
}

// Test extreme buffer sizes
TEST_F(BackpressureFilterTest, ExtremeBufferSizes) {
  BackpressureConfig config;
  config.high_watermark = 1024;  // 1KB very small
  config.low_watermark = 512;    // 512B
  
  createFilter(config);
  
  EXPECT_CALL(*callbacks_, onBackpressureApplied()).Times(1);
  
  executeInDispatcher([this]() {
    // Even small data should trigger backpressure
    auto buffer = createBufferWithSize(1100);  // 1.1KB
    EXPECT_EQ(filter_->onData(*buffer, false), network::FilterStatus::StopIteration);
    EXPECT_TRUE(filter_->isPaused());
  });
}

} // namespace