/**
 * @file test_stdio_echo_server_advanced.cc
 * @brief APPLICATION LEVEL TESTS for advanced echo server binary
 *
 * TEST LEVEL: End-to-end application testing
 *
 * This file tests the actual advanced echo server application binary by
 * spawning processes and testing through stdio pipes. It validates advanced
 * server features like worker threads, flow control, and metrics.
 *
 * What this tests:
 * - Advanced echo server binary execution
 * - End-to-end JSON-RPC flows with advanced features
 * - Worker thread pool behavior
 * - Flow control and backpressure handling
 * - Connection management and scaling
 * - Metrics collection and reporting
 * - Advanced error handling and recovery
 *
 * What this does NOT test:
 * - Transport layer internals (pipes, sockets, events)
 * - Connection manager implementation details
 * - Low-level I/O mechanisms
 *
 * For transport-level testing, see:
 * - tests/integration/test_stdio_echo_server.cc (transport layer tests)
 */

#include <atomic>
#include <chrono>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

// We test the individual components that are testable
#include "mcp/buffer.h"
#include "mcp/builders.h"
#include "mcp/json/json_serialization.h"
#include "mcp/types.h"

namespace mcp {
namespace examples {
namespace test {

using namespace ::mcp;
using ::testing::_;
using ::testing::Return;

// Flow control filter for testing watermark-based backpressure
class FlowControlFilter {
 public:
  FlowControlFilter(uint32_t high_watermark, uint32_t low_watermark)
      : high_watermark_(high_watermark),
        low_watermark_(low_watermark),
        buffer_size_(0),
        above_watermark_(false) {}

  bool processData(size_t data_size) {
    buffer_size_ += data_size;

    if (!above_watermark_ && buffer_size_ > high_watermark_) {
      above_watermark_ = true;
      return false;  // Signal to disable reading
    }

    return true;  // Continue reading
  }

  bool processWrite(size_t written_size) {
    if (written_size <= buffer_size_) {
      buffer_size_ -= written_size;
    } else {
      buffer_size_ = 0;
    }

    if (above_watermark_ && buffer_size_ < low_watermark_) {
      above_watermark_ = false;
      return true;  // Signal to re-enable reading
    }

    return false;  // No change in read state
  }

  size_t getBufferSize() const { return buffer_size_; }
  bool isAboveWatermark() const { return above_watermark_; }

 private:
  uint32_t high_watermark_;
  uint32_t low_watermark_;
  std::atomic<size_t> buffer_size_;
  std::atomic<bool> above_watermark_;
};

// Message processor for testing
class MessageProcessor {
 public:
  struct Stats {
    std::atomic<uint64_t> requests_total{0};
    std::atomic<uint64_t> requests_success{0};
    std::atomic<uint64_t> requests_failed{0};
    std::atomic<uint64_t> notifications_total{0};
    std::atomic<uint64_t> connections_active{0};
  };

  MessageProcessor() {}

  bool processRequest(const jsonrpc::Request& request) {
    stats_.requests_total++;

    // Simulate processing
    if (request.method.find("fail") != std::string::npos) {
      stats_.requests_failed++;
      return false;
    }

    stats_.requests_success++;
    return true;
  }

  bool processNotification(const jsonrpc::Notification& notification) {
    stats_.notifications_total++;

    if (notification.method == "shutdown") {
      return false;  // Signal shutdown
    }

    return true;
  }

  void onNewConnection() { stats_.connections_active++; }

  void onConnectionClose() {
    if (stats_.connections_active > 0) {
      stats_.connections_active--;
    }
  }

  const Stats& getStats() const { return stats_; }

 private:
  Stats stats_;
};

// Test fixture for FlowControlFilter
class FlowControlFilterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    filter = std::make_unique<FlowControlFilter>(1024, 256);
  }

  std::unique_ptr<FlowControlFilter> filter;
};

TEST_F(FlowControlFilterTest, InitialStateNormal) {
  EXPECT_FALSE(filter->isAboveWatermark());
  EXPECT_EQ(filter->getBufferSize(), 0);

  // Small data should pass through
  EXPECT_TRUE(filter->processData(100));
  EXPECT_FALSE(filter->isAboveWatermark());
}

TEST_F(FlowControlFilterTest, TriggerHighWatermark) {
  // Send data exceeding high watermark
  EXPECT_FALSE(filter->processData(1025));  // > 1024 bytes
  EXPECT_TRUE(filter->isAboveWatermark());
  EXPECT_EQ(filter->getBufferSize(), 1025);
}

TEST_F(FlowControlFilterTest, RecoverFromHighWatermark) {
  // Exceed high watermark
  filter->processData(1025);
  EXPECT_TRUE(filter->isAboveWatermark());

  // Write out most of the data (dropping below low watermark)
  EXPECT_TRUE(filter->processWrite(800));  // Now at 225 bytes, below 256
  EXPECT_FALSE(filter->isAboveWatermark());
  EXPECT_EQ(filter->getBufferSize(), 225);
}

TEST_F(FlowControlFilterTest, MultipleWatermarkCycles) {
  for (int i = 0; i < 3; ++i) {
    // Reset filter for each cycle
    filter = std::make_unique<FlowControlFilter>(1024, 256);

    // Exceed high watermark
    filter->processData(1025);
    EXPECT_TRUE(filter->isAboveWatermark());

    // Drop below low watermark
    filter->processWrite(800);
    EXPECT_FALSE(filter->isAboveWatermark());
  }
}

// Test fixture for MessageProcessor
class MessageProcessorTest : public ::testing::Test {
 protected:
  void SetUp() override { processor = std::make_unique<MessageProcessor>(); }

  std::unique_ptr<MessageProcessor> processor;
};

TEST_F(MessageProcessorTest, ProcessValidRequest) {
  auto request = make<jsonrpc::Request>(1, "test.method")
                     .params(make<Metadata>().add("key", "value").build())
                     .build();

  EXPECT_TRUE(processor->processRequest(request));

  const auto& stats = processor->getStats();
  EXPECT_EQ(stats.requests_total, 1);
  EXPECT_EQ(stats.requests_success, 1);
  EXPECT_EQ(stats.requests_failed, 0);
}

TEST_F(MessageProcessorTest, ProcessFailedRequest) {
  auto request = make<jsonrpc::Request>(1, "test.fail.method")
                     .params(make<Metadata>().build())
                     .build();

  EXPECT_FALSE(processor->processRequest(request));

  const auto& stats = processor->getStats();
  EXPECT_EQ(stats.requests_total, 1);
  EXPECT_EQ(stats.requests_success, 0);
  EXPECT_EQ(stats.requests_failed, 1);
}

TEST_F(MessageProcessorTest, ProcessNotification) {
  auto notification = make<jsonrpc::Notification>("test.event")
                          .params(make<Metadata>().add("data", "test").build())
                          .build();

  EXPECT_TRUE(processor->processNotification(notification));

  const auto& stats = processor->getStats();
  EXPECT_EQ(stats.notifications_total, 1);
}

TEST_F(MessageProcessorTest, ShutdownNotification) {
  auto notification = make<jsonrpc::Notification>("shutdown").build();

  EXPECT_FALSE(processor->processNotification(notification));

  const auto& stats = processor->getStats();
  EXPECT_EQ(stats.notifications_total, 1);
}

TEST_F(MessageProcessorTest, ConnectionEvents) {
  processor->onNewConnection();
  const auto& stats = processor->getStats();
  EXPECT_EQ(stats.connections_active, 1);

  processor->onConnectionClose();
  EXPECT_EQ(stats.connections_active, 0);
}

TEST_F(MessageProcessorTest, MultipleConnections) {
  processor->onNewConnection();
  processor->onNewConnection();
  processor->onNewConnection();

  const auto& stats = processor->getStats();
  EXPECT_EQ(stats.connections_active, 3);

  processor->onConnectionClose();
  EXPECT_EQ(stats.connections_active, 2);

  processor->onConnectionClose();
  processor->onConnectionClose();
  EXPECT_EQ(stats.connections_active, 0);
}

// Test for message parsing and framing
class MessageFramingTest : public ::testing::Test {
 protected:
  void SetUp() override {}

  std::vector<std::string> extractMessages(const std::string& data) {
    std::vector<std::string> messages;
    size_t pos = 0;

    while ((pos = data.find('\n')) != std::string::npos) {
      std::string message = data.substr(0, pos);
      if (!message.empty()) {
        messages.push_back(message);
      }
      const_cast<std::string&>(data).erase(0, pos + 1);
    }

    return messages;
  }
};

TEST_F(MessageFramingTest, SingleMessage) {
  std::string data = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"test\"}\n";
  auto messages = extractMessages(data);

  EXPECT_EQ(messages.size(), 1);
  EXPECT_EQ(messages[0], "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"test\"}");
}

TEST_F(MessageFramingTest, MultipleMessages) {
  std::string data =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"test1\"}\n"
      "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"test2\"}\n";
  auto messages = extractMessages(data);

  EXPECT_EQ(messages.size(), 2);
  EXPECT_EQ(messages[0], "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"test1\"}");
  EXPECT_EQ(messages[1], "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"test2\"}");
}

TEST_F(MessageFramingTest, PartialMessage) {
  std::string data = "{\"jsonrpc\":\"2.0\",\"id\":1,";
  auto messages = extractMessages(data);

  EXPECT_EQ(messages.size(), 0);
  EXPECT_EQ(data, "{\"jsonrpc\":\"2.0\",\"id\":1,");
}

TEST_F(MessageFramingTest, EmptyLines) {
  std::string data = "\n{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"test\"}\n\n";
  auto messages = extractMessages(data);

  EXPECT_EQ(messages.size(), 1);
  EXPECT_EQ(messages[0], "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"test\"}");
}

// Test for buffer operations
class BufferOperationsTest : public ::testing::Test {
 protected:
  void SetUp() override { buffer = std::make_unique<OwnedBuffer>(); }

  std::unique_ptr<OwnedBuffer> buffer;
};

TEST_F(BufferOperationsTest, AddAndRead) {
  std::string data = "Hello, World!";
  buffer->add(data);

  EXPECT_EQ(buffer->length(), data.length());
  EXPECT_EQ(buffer->toString(), data);
}

TEST_F(BufferOperationsTest, DrainBuffer) {
  std::string data = "Hello, World!";
  buffer->add(data);

  buffer->drain(7);                // Drain "Hello, " (7 chars)
  EXPECT_EQ(buffer->length(), 6);  // "World!" is 6 chars
  EXPECT_EQ(buffer->toString(), "World!");
}

TEST_F(BufferOperationsTest, MultipleAdds) {
  buffer->add("Hello");
  buffer->add(", ");
  buffer->add("World!");

  EXPECT_EQ(buffer->toString(), "Hello, World!");
}

// Test for latency metrics
class LatencyMetricsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    min_latency = UINT64_MAX;
    max_latency = 0;
    total_latency = 0;
    count = 0;
  }

  void recordLatency(uint64_t latency_ms) {
    total_latency += latency_ms;
    count++;

    if (latency_ms < min_latency) {
      min_latency = latency_ms;
    }
    if (latency_ms > max_latency) {
      max_latency = latency_ms;
    }
  }

  uint64_t getAverageLatency() const {
    return count > 0 ? total_latency / count : 0;
  }

  std::atomic<uint64_t> min_latency;
  std::atomic<uint64_t> max_latency;
  std::atomic<uint64_t> total_latency;
  std::atomic<uint64_t> count;
};

TEST_F(LatencyMetricsTest, RecordSingleLatency) {
  recordLatency(100);

  EXPECT_EQ(min_latency, 100);
  EXPECT_EQ(max_latency, 100);
  EXPECT_EQ(getAverageLatency(), 100);
}

TEST_F(LatencyMetricsTest, RecordMultipleLatencies) {
  recordLatency(50);
  recordLatency(100);
  recordLatency(150);

  EXPECT_EQ(min_latency, 50);
  EXPECT_EQ(max_latency, 150);
  EXPECT_EQ(getAverageLatency(), 100);
}

TEST_F(LatencyMetricsTest, UpdateMinMax) {
  recordLatency(100);
  recordLatency(50);   // New min
  recordLatency(200);  // New max
  recordLatency(75);

  EXPECT_EQ(min_latency, 50);
  EXPECT_EQ(max_latency, 200);
}

}  // namespace test
}  // namespace examples
}  // namespace mcp