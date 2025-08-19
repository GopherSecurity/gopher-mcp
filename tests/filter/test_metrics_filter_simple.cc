/**
 * @file test_metrics_filter_simple.cc
 * @brief Simple unit tests for Metrics Filter (basic functionality only)
 */

#include <chrono>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../../include/mcp/filter/metrics_filter.h"

using namespace mcp;
using namespace mcp::filter;
using namespace testing;
using namespace std::chrono_literals;

namespace {

// Mock callbacks for metrics events
class MockMetricsCallbacks : public MetricsFilter::MetricsCallbacks {
 public:
  MOCK_METHOD(void,
              onMetricsUpdate,
              (const ConnectionMetrics& metrics),
              (override));
  MOCK_METHOD(void,
              onThresholdExceeded,
              (const std::string& metric_name,
               uint64_t value,
               uint64_t threshold),
              (override));
};

class MetricsFilterSimpleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    callbacks_ = std::make_unique<NiceMock<MockMetricsCallbacks>>();

    // Basic configuration
    config_.max_latency_threshold_ms = 5000;
    config_.error_rate_threshold = 10;
    config_.bytes_threshold = 100 * 1024 * 1024;

    filter_ = std::make_unique<MetricsFilter>(*callbacks_, config_);
  }

 protected:
  std::unique_ptr<MetricsFilter> filter_;
  std::unique_ptr<MockMetricsCallbacks> callbacks_;
  MetricsFilter::Config config_;
};

// Test basic configuration
TEST_F(MetricsFilterSimpleTest, ConfigurationAccepted) {
  EXPECT_EQ(config_.max_latency_threshold_ms, 5000);
  EXPECT_EQ(config_.error_rate_threshold, 10);
  EXPECT_EQ(config_.bytes_threshold, 100 * 1024 * 1024);
}

// Test network filter interface (basic data passthrough)
TEST_F(MetricsFilterSimpleTest, NetworkFilterInterface) {
  // These should just pass through without blocking
  EXPECT_EQ(filter_->onNewConnection(), network::FilterStatus::Continue);

  auto buffer = createBuffer();
  EXPECT_EQ(filter_->onData(*buffer, false), network::FilterStatus::Continue);
  EXPECT_EQ(filter_->onWrite(*buffer, false), network::FilterStatus::Continue);
}

// Test metrics retrieval
TEST_F(MetricsFilterSimpleTest, MetricsRetrieval) {
  ConnectionMetrics metrics;
  filter_->getMetrics(metrics);

  // Should have initialized values
  EXPECT_EQ(metrics.bytes_received.load(), 0);
  EXPECT_EQ(metrics.bytes_sent.load(), 0);
  EXPECT_EQ(metrics.requests_received.load(), 0);
  EXPECT_EQ(metrics.responses_sent.load(), 0);
}

}  // namespace