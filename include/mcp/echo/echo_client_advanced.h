/**
 * @file echo_client_advanced.h
 * @brief Reusable advanced echo client with transport abstraction
 */

#pragma once

#include "mcp/echo/echo_transport_advanced.h"
#include "mcp/types.h"
#include "mcp/builders.h"
#include <chrono>
#include <future>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>

namespace mcp {
namespace echo {

/**
 * @brief Circuit breaker for handling cascading failures
 */
class CircuitBreaker {
public:
  enum class State {
    Closed,     // Normal operation
    Open,       // Circuit open, rejecting requests
    HalfOpen    // Testing if service recovered
  };
  
  CircuitBreaker(size_t failure_threshold = 5,
                std::chrono::milliseconds timeout = std::chrono::seconds(30));
  
  bool allowRequest();
  void recordSuccess();
  void recordFailure();
  State getState() const;
  
private:
  size_t failure_threshold_;
  std::chrono::milliseconds timeout_;
  State state_;
  size_t failure_count_;
  std::chrono::steady_clock::time_point last_failure_time_;
  mutable std::mutex mutex_;
};

/**
 * @brief Request context for tracking in-flight requests
 */
struct RequestContext {
  int id;
  std::string method;
  Metadata params;
  std::chrono::steady_clock::time_point sent_time;
  std::promise<jsonrpc::Response> promise;
  int retry_count = 0;
  
  RequestContext(int id, const std::string& method, const Metadata& params);
};

/**
 * @brief Request manager for tracking and timeout handling
 */
class RequestManager {
public:
  RequestManager(std::chrono::milliseconds timeout = std::chrono::seconds(30));
  
  int addRequest(const std::string& method, const Metadata& params);
  std::shared_ptr<RequestContext> getRequest(int id);
  void completeRequest(int id, const jsonrpc::Response& response);
  std::vector<std::shared_ptr<RequestContext>> checkTimeouts();
  size_t getPendingCount() const;
  
private:
  std::chrono::milliseconds timeout_;
  std::atomic<int> next_id_;
  std::map<int, std::shared_ptr<RequestContext>> pending_requests_;
  mutable std::mutex mutex_;
};

/**
 * @brief Advanced echo client statistics
 */
struct ClientStats {
  std::atomic<uint64_t> requests_total{0};
  std::atomic<uint64_t> requests_success{0};
  std::atomic<uint64_t> requests_failed{0};
  std::atomic<uint64_t> requests_timeout{0};
  std::atomic<uint64_t> request_duration_ms_total{0};
  std::atomic<uint64_t> request_duration_ms_min{UINT64_MAX};
  std::atomic<uint64_t> request_duration_ms_max{0};
  std::atomic<uint64_t> circuit_breaker_opens{0};
  std::atomic<uint64_t> bytes_sent{0};
  std::atomic<uint64_t> bytes_received{0};
};

/**
 * @brief Configuration for advanced echo client
 */
struct EchoClientConfig {
  // Circuit breaker settings
  size_t circuit_breaker_threshold = 5;
  std::chrono::milliseconds circuit_breaker_timeout = std::chrono::seconds(60);
  
  // Request settings
  std::chrono::milliseconds request_timeout = std::chrono::seconds(30);
  size_t max_retries = 3;
  
  // Batch settings
  size_t batch_size = 10;
  std::chrono::milliseconds batch_timeout = std::chrono::milliseconds(100);
  
  // Metrics
  bool enable_metrics = true;
  std::chrono::seconds metrics_interval = std::chrono::seconds(10);
};

/**
 * @brief Advanced echo client with transport abstraction
 * 
 * Features:
 * - Transport-agnostic communication
 * - Circuit breaker pattern
 * - Request tracking with timeouts
 * - Retry logic
 * - Batch request processing
 * - Comprehensive metrics
 */
class AdvancedEchoClient {
public:
  AdvancedEchoClient(EchoTransportPtr transport, const EchoClientConfig& config = {});
  ~AdvancedEchoClient();
  
  /**
   * @brief Start the client
   * @param endpoint Transport-specific endpoint
   * @return Success or error
   */
  variant<Success, Error> start(const std::string& endpoint);
  
  /**
   * @brief Stop the client
   */
  void stop();
  
  /**
   * @brief Send a request and get future for response
   * @param method Method name
   * @param params Optional parameters
   * @return Future that will contain the response
   */
  std::future<jsonrpc::Response> sendRequest(const std::string& method, 
                                             const Metadata& params = {});
  
  /**
   * @brief Send a notification (no response expected)
   * @param method Method name
   * @param params Optional parameters
   * @return Success or error
   */
  variant<Success, Error> sendNotification(const std::string& method,
                                          const Metadata& params = {});
  
  /**
   * @brief Send a batch of requests
   * @param requests Vector of method/params pairs
   * @return Vector of futures for responses
   */
  std::vector<std::future<jsonrpc::Response>> sendBatch(
      const std::vector<std::pair<std::string, Metadata>>& requests);
  
  /**
   * @brief Get client statistics
   */
  const ClientStats& getStats() const { return stats_; }
  
  /**
   * @brief Reset statistics
   */
  void resetStats();
  
private:
  void handleDataReceived(const std::string& data);
  void handleStatusChange(EchoTransportAdvanced::Status status);
  void handleError(const Error& error);
  void processMessage(const std::string& message);
  void checkRequestTimeouts();
  void updateLatencyMetrics(uint64_t duration_ms);
  void printMetrics();
  
  EchoTransportPtr transport_;
  EchoClientConfig config_;
  ClientStats stats_;
  
  std::unique_ptr<CircuitBreaker> circuit_breaker_;
  std::unique_ptr<RequestManager> request_manager_;
  
  std::string partial_message_;
  std::atomic<bool> running_{false};
  std::thread timeout_thread_;
  std::thread metrics_thread_;
  mutable std::mutex mutex_;
};

} // namespace echo
} // namespace mcp