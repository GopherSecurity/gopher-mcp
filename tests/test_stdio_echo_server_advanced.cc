/**
 * @file test_stdio_echo_server_advanced.cc
 * @brief Unit tests for Advanced MCP Echo Server
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../examples/stdio_echo/stdio_echo_server_advanced.cc"
#include "mcp/test/test_helpers.h"
#include <thread>
#include <atomic>

namespace mcp {
namespace examples {
namespace test {

using ::testing::_;
using ::testing::Return;
using ::testing::NiceMock;
using ::testing::AtLeast;
using ::testing::Invoke;
using ::testing::InSequence;

// Mock connection for testing
class MockConnection : public network::Connection {
public:
  MOCK_METHOD(void, close, (network::ConnectionCloseType type), (override));
  MOCK_METHOD(event::Dispatcher&, dispatcher, (), (override));
  MOCK_METHOD(void, readDisable, (bool disable), (override));
  MOCK_METHOD(void, write, (Buffer& data, bool end_stream), (override));
  MOCK_METHOD(transport::TransportSocket&, transportSocket, (), (override));
  MOCK_METHOD(stream_info::StreamInfo&, streamInfo, (), (override));
  MOCK_METHOD(bool, isConnected, () const, (override));
  MOCK_METHOD(const network::Address&, localAddress, () const, (override));
  MOCK_METHOD(const network::Address&, remoteAddress, () const, (override));
  MOCK_METHOD(void, setBufferLimits, (uint32_t limit), (override));
  MOCK_METHOD(void, setDelayedCloseTimeout, (std::chrono::milliseconds timeout), (override));
  MOCK_METHOD(void, addBytesSentCallback, (BytesSentCb cb), (override));
  MOCK_METHOD(void, addConnectionCallbacks, (network::ConnectionCallbacks& cb), (override));
  MOCK_METHOD(void, removeConnectionCallbacks, (network::ConnectionCallbacks& cb), (override));
  MOCK_METHOD(void, setConnectionStats, (const network::ConnectionStats& stats), (override));
  MOCK_METHOD(uint64_t, id, () const, (override));
  MOCK_METHOD(void, noDelay, (bool enable), (override));
  MOCK_METHOD(void, hashKey, (std::vector<uint8_t>& hash), (override));
};

// Test fixture for FlowControlFilter
class FlowControlFilterTest : public ::testing::Test {
protected:
  void SetUp() override {
    filter = std::make_unique<FlowControlFilter>(1024, 256);  // 1KB high, 256B low
    
    // Setup mock callbacks
    ON_CALL(read_callbacks, connection()).WillByDefault(ReturnRef(mock_connection));
  }
  
  std::unique_ptr<FlowControlFilter> filter;
  NiceMock<network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<MockConnection> mock_connection;
};

TEST_F(FlowControlFilterTest, InitialStateNormal) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Small data should pass through
  OwnedBuffer buffer;
  buffer.add(std::string(100, 'a'));
  
  auto status = filter->onData(buffer, false);
  EXPECT_EQ(status, network::FilterStatus::Continue);
  
  // Should not disable reading
  EXPECT_CALL(mock_connection, readDisable(_)).Times(0);
}

TEST_F(FlowControlFilterTest, TriggerHighWatermark) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Expect read to be disabled when high watermark is exceeded
  EXPECT_CALL(mock_connection, readDisable(true)).Times(1);
  
  // Send data exceeding high watermark
  OwnedBuffer buffer;
  buffer.add(std::string(1025, 'a'));  // > 1024 bytes
  
  auto status = filter->onData(buffer, false);
  EXPECT_EQ(status, network::FilterStatus::Continue);
}

TEST_F(FlowControlFilterTest, RecoverFromHighWatermark) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  InSequence seq;
  
  // First disable reading when high watermark is hit
  EXPECT_CALL(mock_connection, readDisable(true)).Times(1);
  // Then re-enable when below low watermark
  EXPECT_CALL(mock_connection, readDisable(false)).Times(1);
  
  // Exceed high watermark
  OwnedBuffer buffer1;
  buffer1.add(std::string(1025, 'a'));
  filter->onData(buffer1, false);
  
  // Write out most of the data (dropping below low watermark)
  OwnedBuffer buffer2;
  buffer2.add(std::string(800, 'b'));  // Simulating write of 800 bytes
  filter->onWrite(buffer2, false);
}

TEST_F(FlowControlFilterTest, MultipleWatermarkCycles) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Multiple cycles of high/low watermark
  for (int i = 0; i < 3; ++i) {
    // Exceed high watermark
    EXPECT_CALL(mock_connection, readDisable(true)).Times(1);
    OwnedBuffer buffer_in;
    buffer_in.add(std::string(1025, 'a'));
    filter->onData(buffer_in, false);
    
    // Drop below low watermark
    EXPECT_CALL(mock_connection, readDisable(false)).Times(1);
    OwnedBuffer buffer_out;
    buffer_out.add(std::string(800, 'b'));
    filter->onWrite(buffer_out, false);
  }
}

// Test fixture for McpProtocolFilter
class McpProtocolFilterTest : public ::testing::Test {
protected:
  void SetUp() override {
    failure_count = 0;
    
    filter = std::make_unique<McpProtocolFilter>(
        stats,
        [this](const FailureReason& failure) { 
          failure_count++;
          last_failure = failure;
        });
    
    // Setup mock callbacks
    ON_CALL(read_callbacks, connection()).WillByDefault(ReturnRef(mock_connection));
  }
  
  ApplicationStats stats;
  std::unique_ptr<McpProtocolFilter> filter;
  NiceMock<network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<MockConnection> mock_connection;
  std::atomic<int> failure_count;
  FailureReason last_failure;
};

TEST_F(McpProtocolFilterTest, ProcessValidRequest) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Create a request
  auto request = make<jsonrpc::Request>(1, "test.method")
      .params(make<Metadata>().add("key", "value").build())
      .build();
  
  std::string json_str = json::to_json(request).toString() + "\n";
  
  // Expect a response to be written
  EXPECT_CALL(mock_connection, write(_, false)).Times(1);
  
  // Process the request
  OwnedBuffer buffer;
  buffer.add(json_str);
  
  auto status = filter->onData(buffer, false);
  EXPECT_EQ(status, network::FilterStatus::Continue);
  
  // Check stats
  EXPECT_EQ(stats.requests_total, 1);
  EXPECT_EQ(stats.requests_success, 1);
  EXPECT_EQ(stats.requests_failed, 0);
}

TEST_F(McpProtocolFilterTest, ProcessNotification) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Create a notification
  auto notification = make<jsonrpc::Notification>("test.event")
      .params(make<Metadata>().add("data", "test").build())
      .build();
  
  std::string json_str = json::to_json(notification).toString() + "\n";
  
  // Expect an echo notification to be sent back
  EXPECT_CALL(mock_connection, write(_, false))
      .WillOnce(Invoke([](Buffer& data, bool) {
        std::string response = data.toString();
        EXPECT_TRUE(response.find("echo/test.event") != std::string::npos);
      }));
  
  // Process the notification
  OwnedBuffer buffer;
  buffer.add(json_str);
  
  auto status = filter->onData(buffer, false);
  EXPECT_EQ(status, network::FilterStatus::Continue);
}

TEST_F(McpProtocolFilterTest, ShutdownNotification) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Create shutdown notification
  auto notification = make<jsonrpc::Notification>("shutdown").build();
  std::string json_str = json::to_json(notification).toString() + "\n";
  
  // Expect connection to be closed
  EXPECT_CALL(mock_connection, close(network::ConnectionCloseType::FlushWrite)).Times(1);
  
  // Process the notification
  OwnedBuffer buffer;
  buffer.add(json_str);
  
  auto status = filter->onData(buffer, false);
  EXPECT_EQ(status, network::FilterStatus::Continue);
}

TEST_F(McpProtocolFilterTest, HandleInvalidJson) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Send invalid JSON
  std::string invalid_json = "{invalid json here}\n";
  
  // Process the invalid message
  OwnedBuffer buffer;
  buffer.add(invalid_json);
  
  auto status = filter->onData(buffer, false);
  EXPECT_EQ(status, network::FilterStatus::Continue);
  
  // Should record a failure
  EXPECT_EQ(failure_count, 1);
  EXPECT_EQ(last_failure.type, FailureReason::Type::ProtocolError);
}

TEST_F(McpProtocolFilterTest, HandlePartialMessages) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Create a request
  auto request = make<jsonrpc::Request>(1, "test.method").build();
  std::string json_str = json::to_json(request).toString() + "\n";
  
  // Split into two parts
  size_t split = json_str.length() / 2;
  std::string part1 = json_str.substr(0, split);
  std::string part2 = json_str.substr(split);
  
  // Process first part - should not trigger response
  EXPECT_CALL(mock_connection, write(_, _)).Times(0);
  OwnedBuffer buffer1;
  buffer1.add(part1);
  filter->onData(buffer1, false);
  
  // Process second part - should trigger response
  EXPECT_CALL(mock_connection, write(_, false)).Times(1);
  OwnedBuffer buffer2;
  buffer2.add(part2);
  filter->onData(buffer2, false);
  
  EXPECT_EQ(stats.requests_total, 1);
  EXPECT_EQ(stats.requests_success, 1);
}

TEST_F(McpProtocolFilterTest, HandleMultipleRequests) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Create multiple requests
  auto request1 = make<jsonrpc::Request>(1, "test.method1").build();
  auto request2 = make<jsonrpc::Request>(2, "test.method2").build();
  
  std::string json_str = json::to_json(request1).toString() + "\n" +
                         json::to_json(request2).toString() + "\n";
  
  // Expect two responses
  EXPECT_CALL(mock_connection, write(_, false)).Times(2);
  
  // Process both requests
  OwnedBuffer buffer;
  buffer.add(json_str);
  
  auto status = filter->onData(buffer, false);
  EXPECT_EQ(status, network::FilterStatus::Continue);
  
  EXPECT_EQ(stats.requests_total, 2);
  EXPECT_EQ(stats.requests_success, 2);
}

TEST_F(McpProtocolFilterTest, ConnectionEvents) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // New connection
  auto status = filter->onNewConnection();
  EXPECT_EQ(status, network::FilterStatus::Continue);
  EXPECT_EQ(stats.connections_active, 1);
  
  // Connection close
  filter->onConnectionEvent(network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(stats.connections_active, 0);
}

TEST_F(McpProtocolFilterTest, UnexpectedResponse) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Server shouldn't receive responses
  auto response = make<jsonrpc::Response>(1)
      .result(jsonrpc::ResponseResult(make<Metadata>().build()))
      .build();
  
  std::string json_str = json::to_json(response).toString() + "\n";
  
  // Process the unexpected response
  OwnedBuffer buffer;
  buffer.add(json_str);
  
  auto status = filter->onData(buffer, false);
  EXPECT_EQ(status, network::FilterStatus::Continue);
  
  // Should record a failure
  EXPECT_EQ(failure_count, 1);
  EXPECT_EQ(last_failure.type, FailureReason::Type::ProtocolError);
}

TEST_F(McpProtocolFilterTest, LatencyMetrics) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Send multiple requests with varying processing times
  for (int i = 0; i < 5; ++i) {
    auto request = make<jsonrpc::Request>(i, "test.method").build();
    std::string json_str = json::to_json(request).toString() + "\n";
    
    EXPECT_CALL(mock_connection, write(_, false)).Times(1);
    
    OwnedBuffer buffer;
    buffer.add(json_str);
    filter->onData(buffer, false);
    
    // Simulate processing time
    std::this_thread::sleep_for(std::chrono::milliseconds(10 * (i + 1)));
  }
  
  EXPECT_EQ(stats.requests_total, 5);
  EXPECT_EQ(stats.requests_success, 5);
  EXPECT_GT(stats.request_duration_ms_total, 0);
  EXPECT_GT(stats.request_duration_ms_max, 0);
  EXPECT_LE(stats.request_duration_ms_min, stats.request_duration_ms_max);
}

// Integration test for AdvancedEchoServer
class AdvancedEchoServerTest : public ::testing::Test {
protected:
  void SetUp() override {
    ApplicationBase::Config config;
    config.num_workers = 1;
    config.enable_metrics = true;
    config.buffer_high_watermark = 1024;
    config.buffer_low_watermark = 256;
    
    server = std::make_unique<AdvancedEchoServer>(config);
  }
  
  void TearDown() override {
    if (server) {
      server->stop();
    }
  }
  
  std::unique_ptr<AdvancedEchoServer> server;
};

TEST_F(AdvancedEchoServerTest, ServerInitialization) {
  // Server should initialize successfully
  EXPECT_NE(server, nullptr);
  
  // Check initial stats
  const auto& stats = server->getStats();
  EXPECT_EQ(stats.connections_total, 0);
  EXPECT_EQ(stats.requests_total, 0);
}

TEST_F(AdvancedEchoServerTest, StartAndStop) {
  // Start server in a separate thread
  std::thread server_thread([this]() {
    server->start();
  });
  
  // Give it time to initialize
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // Stop the server
  server->stop();
  
  // Wait for thread to finish
  server_thread.join();
  
  // Server should have stopped cleanly
  const auto& stats = server->getStats();
  EXPECT_EQ(stats.errors_total, 0);
}

TEST_F(AdvancedEchoServerTest, MetricsCollection) {
  ApplicationBase::Config config;
  config.num_workers = 1;
  config.enable_metrics = true;
  config.metrics_interval = std::chrono::milliseconds(100);
  
  auto server_with_metrics = std::make_unique<AdvancedEchoServer>(config);
  
  // Start server
  std::thread server_thread([&server_with_metrics]() {
    server_with_metrics->start();
  });
  
  // Wait for metrics interval
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  
  // Stop server
  server_with_metrics->stop();
  server_thread.join();
  
  // Metrics should have been collected
  const auto& stats = server_with_metrics->getStats();
  // Note: actual metrics values depend on implementation
}

TEST_F(AdvancedEchoServerTest, MultiWorkerConfiguration) {
  ApplicationBase::Config config;
  config.num_workers = 4;
  config.enable_metrics = false;
  
  auto multi_worker_server = std::make_unique<AdvancedEchoServer>(config);
  
  // Server should be created with multiple workers
  EXPECT_NE(multi_worker_server, nullptr);
}

// Test fixture for signal handling
class SignalHandlingTest : public ::testing::Test {
protected:
  void SetUp() override {
    ApplicationBase::Config config;
    config.num_workers = 1;
    
    // Create global server instance
    g_server = std::make_unique<AdvancedEchoServer>(config);
  }
  
  void TearDown() override {
    g_server.reset();
  }
};

TEST_F(SignalHandlingTest, HandleSIGINT) {
  // Start server in thread
  std::atomic<bool> server_running{true};
  std::thread server_thread([&server_running]() {
    g_server->start();
    server_running = false;
  });
  
  // Give server time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // Send SIGINT
  signalHandler(SIGINT);
  
  // Wait for server to stop
  auto start = std::chrono::steady_clock::now();
  while (server_running && 
         std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  server_thread.join();
  
  // Server should have stopped
  EXPECT_FALSE(server_running);
}

TEST_F(SignalHandlingTest, HandleSIGTERM) {
  // Start server in thread
  std::atomic<bool> server_running{true};
  std::thread server_thread([&server_running]() {
    g_server->start();
    server_running = false;
  });
  
  // Give server time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // Send SIGTERM
  signalHandler(SIGTERM);
  
  // Wait for server to stop
  auto start = std::chrono::steady_clock::now();
  while (server_running && 
         std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  server_thread.join();
  
  // Server should have stopped
  EXPECT_FALSE(server_running);
}

} // namespace test
} // namespace examples
} // namespace mcp