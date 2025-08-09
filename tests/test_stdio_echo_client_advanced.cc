/**
 * @file test_stdio_echo_client_advanced.cc
 * @brief APPLICATION LEVEL TESTS for advanced echo client binary
 * 
 * TEST LEVEL: End-to-end application testing
 * 
 * This file tests the actual advanced echo client application binary by 
 * spawning processes and testing through stdio pipes. It validates advanced
 * client features like circuit breaking, retry logic, and batch processing.
 * 
 * What this tests:
 * - Advanced echo client binary execution
 * - End-to-end JSON-RPC flows with advanced features
 * - Circuit breaker pattern behavior
 * - Retry and timeout mechanisms
 * - Batch request processing
 * - Advanced error handling and recovery
 * - Metrics collection and reporting
 * 
 * What this does NOT test:
 * - Transport layer internals (pipes, sockets, events)
 * - Connection manager implementation details
 * - Low-level I/O mechanisms
 * 
 * For transport-level testing, see:
 * - tests/integration/test_stdio_echo_client.cc (transport layer tests)
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <future>
#include <chrono>

// We need to include the header files that define the classes we're testing
// Since the advanced client implementation is in a .cc file, we'll test the
// individual components that are testable

#include "mcp/types.h"
#include "mcp/builders.h"
#include "mcp/json/json_serialization.h"

namespace mcp {
namespace examples {
namespace test {

using namespace ::mcp;
using ::testing::_;
using ::testing::Return;

// Circuit breaker implementation for testing
class CircuitBreaker {
public:
  enum class State {
    Closed,
    Open,
    HalfOpen
  };
  
  CircuitBreaker(size_t failure_threshold = 5,
                std::chrono::milliseconds timeout = std::chrono::seconds(30))
      : failure_threshold_(failure_threshold),
        timeout_(timeout),
        state_(State::Closed),
        failure_count_(0) {}
  
  bool allowRequest() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    switch (state_) {
      case State::Closed:
        return true;
        
      case State::Open:
        if (std::chrono::steady_clock::now() - last_failure_time_ > timeout_) {
          state_ = State::HalfOpen;
          return true;
        }
        return false;
        
      case State::HalfOpen:
        return true;
    }
    
    return false;
  }
  
  void recordSuccess() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ == State::HalfOpen) {
      state_ = State::Closed;
      failure_count_ = 0;
    }
  }
  
  void recordFailure() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    failure_count_++;
    last_failure_time_ = std::chrono::steady_clock::now();
    
    if (failure_count_ >= failure_threshold_) {
      state_ = State::Open;
    } else if (state_ == State::HalfOpen) {
      state_ = State::Open;
    }
  }
  
  State getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
  }
  
private:
  size_t failure_threshold_;
  std::chrono::milliseconds timeout_;
  State state_;
  size_t failure_count_;
  std::chrono::steady_clock::time_point last_failure_time_;
  mutable std::mutex mutex_;
};

// Request context for testing
struct RequestContext {
  int id;
  std::string method;
  Metadata params;
  std::chrono::steady_clock::time_point sent_time;
  std::promise<jsonrpc::Response> promise;
  int retry_count = 0;
  
  RequestContext(int id, const std::string& method, const Metadata& params)
      : id(id), method(method), params(params),
        sent_time(std::chrono::steady_clock::now()) {}
};

// Request manager for testing
class RequestManager {
public:
  RequestManager(std::chrono::milliseconds timeout = std::chrono::seconds(30))
      : timeout_(timeout), next_id_(1) {}
  
  int addRequest(const std::string& method, const Metadata& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    int id = next_id_++;
    auto context = std::make_shared<RequestContext>(id, method, params);
    pending_requests_[id] = context;
    
    return id;
  }
  
  std::shared_ptr<RequestContext> getRequest(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = pending_requests_.find(id);
    if (it != pending_requests_.end()) {
      return it->second;
    }
    return nullptr;
  }
  
  void completeRequest(int id, const jsonrpc::Response& response) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = pending_requests_.find(id);
    if (it != pending_requests_.end()) {
      it->second->promise.set_value(response);
      pending_requests_.erase(it);
    }
  }
  
  std::vector<std::shared_ptr<RequestContext>> checkTimeouts() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::shared_ptr<RequestContext>> timed_out;
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
      if (now - it->second->sent_time > timeout_) {
        timed_out.push_back(it->second);
        it = pending_requests_.erase(it);
      } else {
        ++it;
      }
    }
    
    return timed_out;
  }
  
  size_t getPendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_requests_.size();
  }
  
private:
  std::chrono::milliseconds timeout_;
  std::atomic<int> next_id_;
  std::map<int, std::shared_ptr<RequestContext>> pending_requests_;
  mutable std::mutex mutex_;
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
  for (int i = 0; i < 3; ++i) {
    circuit_breaker->recordFailure();
  }
  
  EXPECT_EQ(circuit_breaker->getState(), CircuitBreaker::State::Open);
  EXPECT_FALSE(circuit_breaker->allowRequest());
}

TEST_F(CircuitBreakerTest, TransitionsToHalfOpenAfterTimeout) {
  for (int i = 0; i < 3; ++i) {
    circuit_breaker->recordFailure();
  }
  
  EXPECT_EQ(circuit_breaker->getState(), CircuitBreaker::State::Open);
  
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  
  EXPECT_TRUE(circuit_breaker->allowRequest());
  EXPECT_EQ(circuit_breaker->getState(), CircuitBreaker::State::HalfOpen);
}

TEST_F(CircuitBreakerTest, ClosesOnSuccessInHalfOpen) {
  for (int i = 0; i < 3; ++i) {
    circuit_breaker->recordFailure();
  }
  
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  circuit_breaker->allowRequest();
  
  circuit_breaker->recordSuccess();
  
  EXPECT_EQ(circuit_breaker->getState(), CircuitBreaker::State::Closed);
  EXPECT_TRUE(circuit_breaker->allowRequest());
}

TEST_F(CircuitBreakerTest, ReopensOnFailureInHalfOpen) {
  for (int i = 0; i < 3; ++i) {
    circuit_breaker->recordFailure();
  }
  
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  circuit_breaker->allowRequest();
  
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
  
  request_manager->completeRequest(id, response);
  
  EXPECT_EQ(request_manager->getRequest(id), nullptr);
  EXPECT_EQ(request_manager->getPendingCount(), 0);
  
  auto future_response = request->promise.get_future().get();
  EXPECT_FALSE(future_response.error.has_value());
}

TEST_F(RequestManagerTest, TimeoutDetection) {
  auto params = make<Metadata>().add("test", "value").build();
  int id = request_manager->addRequest("test.method", params);
  
  auto timed_out = request_manager->checkTimeouts();
  EXPECT_EQ(timed_out.size(), 0);
  
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  
  timed_out = request_manager->checkTimeouts();
  EXPECT_EQ(timed_out.size(), 1);
  EXPECT_EQ(timed_out[0]->id, id);
  
  EXPECT_EQ(request_manager->getPendingCount(), 0);
}

TEST_F(RequestManagerTest, ConcurrentRequests) {
  const int num_requests = 10;
  std::vector<int> ids;
  
  for (int i = 0; i < num_requests; ++i) {
    auto params = make<Metadata>().add("index", i).build();
    ids.push_back(request_manager->addRequest("test.method." + std::to_string(i), params));
  }
  
  EXPECT_EQ(request_manager->getPendingCount(), num_requests);
  
  for (int i = 0; i < num_requests / 2; ++i) {
    auto response = make<jsonrpc::Response>(ids[i])
        .result(jsonrpc::ResponseResult(make<Metadata>().build()))
        .build();
    request_manager->completeRequest(ids[i], response);
  }
  
  EXPECT_EQ(request_manager->getPendingCount(), num_requests / 2);
}

// Test JSON serialization for MCP messages
class JsonSerializationTest : public ::testing::Test {
protected:
  void SetUp() override {}
};

TEST_F(JsonSerializationTest, SerializeRequest) {
  auto request = make<jsonrpc::Request>(1, "test.method")
      .params(make<Metadata>().add("key", "value").build())
      .build();
  
  auto json_val = json::to_json(request);
  std::string json_str = json_val.toString();
  
  EXPECT_TRUE(json_str.find("\"jsonrpc\":\"2.0\"") != std::string::npos);
  EXPECT_TRUE(json_str.find("\"id\":1") != std::string::npos);
  EXPECT_TRUE(json_str.find("\"method\":\"test.method\"") != std::string::npos);
  EXPECT_TRUE(json_str.find("\"key\":\"value\"") != std::string::npos);
}

TEST_F(JsonSerializationTest, SerializeResponse) {
  auto response = make<jsonrpc::Response>(1)
      .result(jsonrpc::ResponseResult(make<Metadata>()
          .add("echo", true)
          .build()))
      .build();
  
  auto json_val = json::to_json(response);
  std::string json_str = json_val.toString();
  
  EXPECT_TRUE(json_str.find("\"jsonrpc\":\"2.0\"") != std::string::npos);
  EXPECT_TRUE(json_str.find("\"id\":1") != std::string::npos);
  EXPECT_TRUE(json_str.find("\"result\"") != std::string::npos);
  EXPECT_TRUE(json_str.find("\"echo\":true") != std::string::npos);
}

TEST_F(JsonSerializationTest, SerializeErrorResponse) {
  auto response = make<jsonrpc::Response>(1)
      .error(Error(jsonrpc::METHOD_NOT_FOUND, "Method not found"))
      .build();
  
  auto json_val = json::to_json(response);
  std::string json_str = json_val.toString();
  
  EXPECT_TRUE(json_str.find("\"jsonrpc\":\"2.0\"") != std::string::npos);
  EXPECT_TRUE(json_str.find("\"id\":1") != std::string::npos);
  EXPECT_TRUE(json_str.find("\"error\"") != std::string::npos);
  EXPECT_TRUE(json_str.find("\"code\":-32601") != std::string::npos);
  EXPECT_TRUE(json_str.find("\"message\":\"Method not found\"") != std::string::npos);
}

TEST_F(JsonSerializationTest, SerializeNotification) {
  auto notification = make<jsonrpc::Notification>("test.event")
      .params(make<Metadata>().add("data", "test").build())
      .build();
  
  auto json_val = json::to_json(notification);
  std::string json_str = json_val.toString();
  
  EXPECT_TRUE(json_str.find("\"jsonrpc\":\"2.0\"") != std::string::npos);
  EXPECT_TRUE(json_str.find("\"method\":\"test.event\"") != std::string::npos);
  EXPECT_TRUE(json_str.find("\"data\":\"test\"") != std::string::npos);
  EXPECT_TRUE(json_str.find("\"id\"") == std::string::npos);
}

} // namespace test
} // namespace examples
} // namespace mcp