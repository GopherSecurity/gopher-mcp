/**
 * @file test_stdio_echo_client_advanced.cc
 * @brief Unit tests for Advanced MCP Echo Client
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../examples/stdio_echo/stdio_echo_client_advanced.cc"
#include "mcp/test/test_helpers.h"
#include <thread>
#include <future>

namespace mcp {
namespace examples {
namespace test {

using ::testing::_;
using ::testing::Return;
using ::testing::NiceMock;
using ::testing::AtLeast;
using ::testing::Invoke;

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

// Mock transport socket
class MockTransportSocket : public transport::TransportSocket {
public:
  MOCK_METHOD(void, onConnected, (), (override));
  MOCK_METHOD(transport::IoResult, doRead, (Buffer& buffer), (override));
  MOCK_METHOD(transport::IoResult, doWrite, (Buffer& buffer, bool end_stream), (override));
  MOCK_METHOD(void, closeSocket, (transport::CloseType type), (override));
  MOCK_METHOD(transport::IoResult, doConnect, (const network::Address& address), (override));
  MOCK_METHOD(bool, canFlushClose, (), (override));
  MOCK_METHOD(void, setTransportSocketCallbacks, (transport::TransportSocketCallbacks& callbacks), (override));
};

// Test fixture for CircuitBreaker
class CircuitBreakerTest : public ::testing::Test {
protected:
  void SetUp() override {
    circuit_breaker = std::make_unique<CircuitBreaker>(3, std::chrono::milliseconds(100));
  }
  
  std::unique_ptr<CircuitBreaker> circuit_breaker;
};

TEST_F(CircuitBreakerTest, InitialStateClosed) {
  EXPECT_EQ(circuit_breaker->getState(), CircuitBreaker::State::Closed);
  EXPECT_TRUE(circuit_breaker->allowRequest());
}

TEST_F(CircuitBreakerTest, OpensAfterThresholdFailures) {
  // Record failures up to threshold
  for (int i = 0; i < 3; ++i) {
    circuit_breaker->recordFailure();
  }
  
  // Circuit should be open now
  EXPECT_EQ(circuit_breaker->getState(), CircuitBreaker::State::Open);
  EXPECT_FALSE(circuit_breaker->allowRequest());
}

TEST_F(CircuitBreakerTest, TransitionsToHalfOpenAfterTimeout) {
  // Open the circuit
  for (int i = 0; i < 3; ++i) {
    circuit_breaker->recordFailure();
  }
  
  EXPECT_EQ(circuit_breaker->getState(), CircuitBreaker::State::Open);
  
  // Wait for timeout
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  
  // Should transition to half-open and allow one request
  EXPECT_TRUE(circuit_breaker->allowRequest());
  EXPECT_EQ(circuit_breaker->getState(), CircuitBreaker::State::HalfOpen);
}

TEST_F(CircuitBreakerTest, ClosesOnSuccessInHalfOpen) {
  // Open the circuit
  for (int i = 0; i < 3; ++i) {
    circuit_breaker->recordFailure();
  }
  
  // Wait for timeout to transition to half-open
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  circuit_breaker->allowRequest();
  
  // Record success - should close circuit
  circuit_breaker->recordSuccess();
  
  EXPECT_EQ(circuit_breaker->getState(), CircuitBreaker::State::Closed);
  EXPECT_TRUE(circuit_breaker->allowRequest());
}

TEST_F(CircuitBreakerTest, ReopensOnFailureInHalfOpen) {
  // Open the circuit
  for (int i = 0; i < 3; ++i) {
    circuit_breaker->recordFailure();
  }
  
  // Wait for timeout to transition to half-open
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  circuit_breaker->allowRequest();
  
  // Record failure - should reopen circuit
  circuit_breaker->recordFailure();
  
  EXPECT_EQ(circuit_breaker->getState(), CircuitBreaker::State::Open);
  EXPECT_FALSE(circuit_breaker->allowRequest());
}

// Test fixture for RequestManager
class RequestManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    request_manager = std::make_unique<RequestManager>(std::chrono::milliseconds(100));
  }
  
  std::unique_ptr<RequestManager> request_manager;
};

TEST_F(RequestManagerTest, AddAndGetRequest) {
  auto params = make<Metadata>().add("test", "value").build();
  int id = request_manager->addRequest("test.method", params);
  
  EXPECT_GT(id, 0);
  
  auto request = request_manager->getRequest(id);
  ASSERT_NE(request, nullptr);
  EXPECT_EQ(request->id, id);
  EXPECT_EQ(request->method, "test.method");
}

TEST_F(RequestManagerTest, CompleteRequest) {
  auto params = make<Metadata>().add("test", "value").build();
  int id = request_manager->addRequest("test.method", params);
  
  auto request = request_manager->getRequest(id);
  ASSERT_NE(request, nullptr);
  
  auto response = make<jsonrpc::Response>(id)
      .result(jsonrpc::ResponseResult(make<Metadata>()
          .add("echo", true)
          .build()))
      .build();
  
  // Complete the request
  request_manager->completeRequest(id, response);
  
  // Request should be removed
  EXPECT_EQ(request_manager->getRequest(id), nullptr);
  EXPECT_EQ(request_manager->getPendingCount(), 0);
  
  // Future should be ready
  auto future_response = request->promise.get_future().get();
  EXPECT_FALSE(future_response.error.has_value());
}

TEST_F(RequestManagerTest, TimeoutDetection) {
  auto params = make<Metadata>().add("test", "value").build();
  int id = request_manager->addRequest("test.method", params);
  
  // Initially no timeouts
  auto timed_out = request_manager->checkTimeouts();
  EXPECT_EQ(timed_out.size(), 0);
  
  // Wait for timeout period
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  
  // Now should detect timeout
  timed_out = request_manager->checkTimeouts();
  EXPECT_EQ(timed_out.size(), 1);
  EXPECT_EQ(timed_out[0]->id, id);
  
  // Request should be removed
  EXPECT_EQ(request_manager->getPendingCount(), 0);
}

TEST_F(RequestManagerTest, ConcurrentRequests) {
  const int num_requests = 10;
  std::vector<int> ids;
  
  // Add multiple requests
  for (int i = 0; i < num_requests; ++i) {
    auto params = make<Metadata>().add("index", i).build();
    ids.push_back(request_manager->addRequest("test.method." + std::to_string(i), params));
  }
  
  EXPECT_EQ(request_manager->getPendingCount(), num_requests);
  
  // Complete half of them
  for (int i = 0; i < num_requests / 2; ++i) {
    auto response = make<jsonrpc::Response>(ids[i])
        .result(jsonrpc::ResponseResult(make<Metadata>().build()))
        .build();
    request_manager->completeRequest(ids[i], response);
  }
  
  EXPECT_EQ(request_manager->getPendingCount(), num_requests / 2);
}

// Test fixture for ClientProtocolFilter
class ClientProtocolFilterTest : public ::testing::Test {
protected:
  void SetUp() override {
    request_manager = std::make_unique<RequestManager>();
    circuit_breaker = std::make_unique<CircuitBreaker>();
    
    filter = std::make_unique<ClientProtocolFilter>(
        *request_manager, *circuit_breaker, stats);
    
    // Setup mock callbacks
    ON_CALL(read_callbacks, connection()).WillByDefault(ReturnRef(mock_connection));
  }
  
  ApplicationStats stats;
  std::unique_ptr<RequestManager> request_manager;
  std::unique_ptr<CircuitBreaker> circuit_breaker;
  std::unique_ptr<ClientProtocolFilter> filter;
  NiceMock<network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<MockConnection> mock_connection;
};

TEST_F(ClientProtocolFilterTest, ProcessValidResponse) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Add a pending request
  int id = request_manager->addRequest("test.method", {});
  auto request = request_manager->getRequest(id);
  
  // Create response JSON
  auto response = make<jsonrpc::Response>(id)
      .result(jsonrpc::ResponseResult(make<Metadata>()
          .add("echo", true)
          .build()))
      .build();
  
  std::string json_str = json::to_json(response).toString() + "\n";
  
  // Process the response
  OwnedBuffer buffer;
  buffer.add(json_str);
  
  auto status = filter->onData(buffer, false);
  EXPECT_EQ(status, network::FilterStatus::Continue);
  
  // Check stats
  EXPECT_EQ(stats.requests_success, 1);
  EXPECT_EQ(stats.requests_failed, 0);
  
  // Request should be completed
  EXPECT_EQ(request_manager->getPendingCount(), 0);
}

TEST_F(ClientProtocolFilterTest, ProcessErrorResponse) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Add a pending request
  int id = request_manager->addRequest("test.method", {});
  
  // Create error response
  auto response = make<jsonrpc::Response>(id)
      .error(Error(jsonrpc::METHOD_NOT_FOUND, "Method not found"))
      .build();
  
  std::string json_str = json::to_json(response).toString() + "\n";
  
  // Process the response
  OwnedBuffer buffer;
  buffer.add(json_str);
  
  auto status = filter->onData(buffer, false);
  EXPECT_EQ(status, network::FilterStatus::Continue);
  
  // Check stats
  EXPECT_EQ(stats.requests_success, 0);
  EXPECT_EQ(stats.requests_failed, 1);
  
  // Circuit breaker should record failure
  EXPECT_EQ(circuit_breaker->getState(), CircuitBreaker::State::Closed);
}

TEST_F(ClientProtocolFilterTest, ProcessNotification) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Create notification
  auto notification = make<jsonrpc::Notification>("server.event")
      .params(make<Metadata>().add("data", "test").build())
      .build();
  
  std::string json_str = json::to_json(notification).toString() + "\n";
  
  // Process the notification
  OwnedBuffer buffer;
  buffer.add(json_str);
  
  auto status = filter->onData(buffer, false);
  EXPECT_EQ(status, network::FilterStatus::Continue);
  
  // Stats should not change for notifications
  EXPECT_EQ(stats.requests_total, 0);
}

TEST_F(ClientProtocolFilterTest, HandlePartialMessages) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Add a pending request
  int id = request_manager->addRequest("test.method", {});
  
  // Create response JSON
  auto response = make<jsonrpc::Response>(id)
      .result(jsonrpc::ResponseResult(make<Metadata>()
          .add("echo", true)
          .build()))
      .build();
  
  std::string json_str = json::to_json(response).toString() + "\n";
  
  // Send in two parts
  size_t split = json_str.length() / 2;
  std::string part1 = json_str.substr(0, split);
  std::string part2 = json_str.substr(split);
  
  // Process first part - should not complete
  OwnedBuffer buffer1;
  buffer1.add(part1);
  filter->onData(buffer1, false);
  EXPECT_EQ(request_manager->getPendingCount(), 1);
  
  // Process second part - should complete
  OwnedBuffer buffer2;
  buffer2.add(part2);
  filter->onData(buffer2, false);
  EXPECT_EQ(request_manager->getPendingCount(), 0);
  
  EXPECT_EQ(stats.requests_success, 1);
}

TEST_F(ClientProtocolFilterTest, HandleMultipleMessages) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Add multiple pending requests
  int id1 = request_manager->addRequest("test.method1", {});
  int id2 = request_manager->addRequest("test.method2", {});
  
  // Create multiple responses
  auto response1 = make<jsonrpc::Response>(id1)
      .result(jsonrpc::ResponseResult(make<Metadata>().build()))
      .build();
  auto response2 = make<jsonrpc::Response>(id2)
      .result(jsonrpc::ResponseResult(make<Metadata>().build()))
      .build();
  
  // Combine into single buffer
  std::string json_str = json::to_json(response1).toString() + "\n" +
                         json::to_json(response2).toString() + "\n";
  
  // Process both messages
  OwnedBuffer buffer;
  buffer.add(json_str);
  
  auto status = filter->onData(buffer, false);
  EXPECT_EQ(status, network::FilterStatus::Continue);
  
  // Both requests should be completed
  EXPECT_EQ(request_manager->getPendingCount(), 0);
  EXPECT_EQ(stats.requests_success, 2);
}

TEST_F(ClientProtocolFilterTest, ParseError) {
  filter->initializeReadFilterCallbacks(read_callbacks);
  
  // Send invalid JSON
  std::string invalid_json = "{invalid json}\n";
  
  OwnedBuffer buffer;
  buffer.add(invalid_json);
  
  auto status = filter->onData(buffer, false);
  EXPECT_EQ(status, network::FilterStatus::Continue);
  
  // Should record error
  EXPECT_EQ(stats.errors_total, 1);
}

// Integration test for AdvancedEchoClient
class AdvancedEchoClientTest : public ::testing::Test {
protected:
  void SetUp() override {
    ApplicationBase::Config config;
    config.num_workers = 1;
    config.enable_metrics = true;
    
    client = std::make_unique<AdvancedEchoClient>(config);
  }
  
  void TearDown() override {
    if (client) {
      client->stop();
    }
  }
  
  std::unique_ptr<AdvancedEchoClient> client;
};

TEST_F(AdvancedEchoClientTest, SendRequest) {
  // Note: This is a simplified test without actual stdio connection
  // In a real test environment, you would mock the connection pool
  
  auto params = make<Metadata>().add("test", "value").build();
  auto future = client->sendRequest("test.method", params);
  
  // The request should return with circuit breaker error (no connection)
  auto response = future.get();
  EXPECT_TRUE(response.error.has_value());
  EXPECT_EQ(response.error->code, -32000);
}

TEST_F(AdvancedEchoClientTest, SendBatch) {
  std::vector<std::pair<std::string, Metadata>> batch;
  
  for (int i = 0; i < 5; ++i) {
    auto params = make<Metadata>().add("index", i).build();
    batch.emplace_back("test.method." + std::to_string(i), params);
  }
  
  auto futures = client->sendBatch(batch);
  
  EXPECT_EQ(futures.size(), 5);
  
  // All should fail with circuit breaker (no connection)
  for (auto& future : futures) {
    auto response = future.get();
    EXPECT_TRUE(response.error.has_value());
  }
}

TEST_F(AdvancedEchoClientTest, MetricsTracking) {
  // Send some requests
  for (int i = 0; i < 3; ++i) {
    auto future = client->sendRequest("test.method");
    future.get();
  }
  
  const auto& stats = client->getStats();
  EXPECT_EQ(stats.requests_total, 3);
}

} // namespace test
} // namespace examples
} // namespace mcp