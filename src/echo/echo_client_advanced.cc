/**
 * @file echo_client_advanced.cc
 * @brief Implementation of reusable advanced echo client
 */

#include "mcp/echo/echo_client_advanced.h"

#include <sstream>

#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"

namespace mcp {
namespace echo {

// CircuitBreaker implementation
CircuitBreaker::CircuitBreaker(size_t failure_threshold,
                               std::chrono::milliseconds timeout)
    : failure_threshold_(failure_threshold),
      timeout_(timeout),
      state_(State::Closed),
      failure_count_(0) {}

bool CircuitBreaker::allowRequest() {
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

void CircuitBreaker::recordSuccess() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (state_ == State::HalfOpen) {
    state_ = State::Closed;
    failure_count_ = 0;
  }
}

void CircuitBreaker::recordFailure() {
  std::lock_guard<std::mutex> lock(mutex_);

  failure_count_++;
  last_failure_time_ = std::chrono::steady_clock::now();

  if (failure_count_ >= failure_threshold_) {
    state_ = State::Open;
  } else if (state_ == State::HalfOpen) {
    state_ = State::Open;
  }
}

CircuitBreaker::State CircuitBreaker::getState() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

// RequestContext implementation
RequestContext::RequestContext(int id,
                               const std::string& method,
                               const Metadata& params)
    : id(id),
      method(method),
      params(params),
      sent_time(std::chrono::steady_clock::now()) {}

// RequestManager implementation
RequestManager::RequestManager(std::chrono::milliseconds timeout)
    : timeout_(timeout), next_id_(1) {}

int RequestManager::addRequest(const std::string& method,
                               const Metadata& params) {
  std::lock_guard<std::mutex> lock(mutex_);

  int id = next_id_++;
  auto context = std::make_shared<RequestContext>(id, method, params);
  pending_requests_[id] = context;

  return id;
}

std::shared_ptr<RequestContext> RequestManager::getRequest(int id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = pending_requests_.find(id);
  if (it != pending_requests_.end()) {
    return it->second;
  }
  return nullptr;
}

void RequestManager::completeRequest(int id,
                                     const jsonrpc::Response& response) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = pending_requests_.find(id);
  if (it != pending_requests_.end()) {
    it->second->promise.set_value(response);
    pending_requests_.erase(it);
  }
}

std::vector<std::shared_ptr<RequestContext>> RequestManager::checkTimeouts() {
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

size_t RequestManager::getPendingCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_requests_.size();
}

// AdvancedEchoClient implementation
AdvancedEchoClient::AdvancedEchoClient(EchoTransportPtr transport,
                                       const EchoClientConfig& config)
    : transport_(std::move(transport)), config_(config) {
  circuit_breaker_ = std::make_unique<CircuitBreaker>(
      config_.circuit_breaker_threshold, config_.circuit_breaker_timeout);

  request_manager_ = std::make_unique<RequestManager>(config_.request_timeout);
}

AdvancedEchoClient::~AdvancedEchoClient() { stop(); }

variant<Success, Error> AdvancedEchoClient::start(const std::string& endpoint) {
  if (running_) {
    return Error(-1, "Client already running");
  }

  // Initialize transport
  auto init_result = transport_->initialize();
  if (holds_alternative<Error>(init_result)) {
    return get<Error>(init_result);
  }

  // Set callbacks
  EchoTransportAdvanced::Callbacks callbacks;
  callbacks.onDataReceived = [this](const std::string& data) {
    handleDataReceived(data);
  };
  callbacks.onStatusChange = [this](EchoTransportAdvanced::Status status) {
    handleStatusChange(status);
  };
  callbacks.onError = [this](const Error& error) { handleError(error); };
  transport_->setCallbacks(callbacks);

  // Connect to endpoint
  auto connect_result = transport_->connect(endpoint);
  if (holds_alternative<Error>(connect_result)) {
    return get<Error>(connect_result);
  }

  running_ = true;

  // Start timeout checker thread
  timeout_thread_ = std::thread([this]() {
    while (running_) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      checkRequestTimeouts();
    }
  });

  // Start metrics thread if enabled
  if (config_.enable_metrics) {
    metrics_thread_ = std::thread([this]() {
      while (running_) {
        std::this_thread::sleep_for(config_.metrics_interval);
        printMetrics();
      }
    });
  }

  return Success{};
}

void AdvancedEchoClient::stop() {
  if (!running_) {
    return;
  }

  running_ = false;

  // Stop transport
  transport_->close();

  // Join threads
  if (timeout_thread_.joinable()) {
    timeout_thread_.join();
  }
  if (metrics_thread_.joinable()) {
    metrics_thread_.join();
  }

  // Cancel pending requests
  auto pending = request_manager_->checkTimeouts();
  for (const auto& request : pending) {
    request->promise.set_value(make<jsonrpc::Response>(request->id)
                                   .error(Error(-32001, "Client shutting down"))
                                   .build());
  }
}

std::future<jsonrpc::Response> AdvancedEchoClient::sendRequest(
    const std::string& method, const Metadata& params) {
  // Check circuit breaker
  if (!circuit_breaker_->allowRequest()) {
    std::promise<jsonrpc::Response> promise;
    promise.set_value(make<jsonrpc::Response>(0)
                          .error(Error(-32000, "Circuit breaker open"))
                          .build());
    stats_.circuit_breaker_opens++;
    return promise.get_future();
  }

  // Add request to manager
  int id = request_manager_->addRequest(method, params);
  auto request_ctx = request_manager_->getRequest(id);

  // Build JSON-RPC request
  auto request = make<jsonrpc::Request>(id, method).params(params).build();

  // Serialize and send
  auto json_val = json::to_json(request);
  std::string message = json_val.toString() + "\n";

  auto send_result = transport_->send(message);
  if (holds_alternative<Error>(send_result)) {
    request_manager_->completeRequest(
        id, make<jsonrpc::Response>(id).error(get<Error>(send_result)).build());
    stats_.requests_failed++;
    circuit_breaker_->recordFailure();
  } else {
    stats_.requests_total++;
    stats_.bytes_sent += message.length();
  }

  return request_ctx->promise.get_future();
}

variant<Success, Error> AdvancedEchoClient::sendNotification(
    const std::string& method, const Metadata& params) {
  // Build JSON-RPC notification
  auto notification =
      make<jsonrpc::Notification>(method).params(params).build();

  // Serialize and send
  auto json_val = json::to_json(notification);
  std::string message = json_val.toString() + "\n";

  auto send_result = transport_->send(message);
  if (holds_alternative<Success>(send_result)) {
    stats_.bytes_sent += message.length();
  }

  return send_result;
}

std::vector<std::future<jsonrpc::Response>> AdvancedEchoClient::sendBatch(
    const std::vector<std::pair<std::string, Metadata>>& requests) {
  std::vector<std::future<jsonrpc::Response>> futures;

  for (const auto& request_pair : requests) {
    const auto& method = request_pair.first;
    const auto& params = request_pair.second;
    futures.push_back(sendRequest(method, params));
  }

  return futures;
}

void AdvancedEchoClient::resetStats() {
  stats_.requests_total = 0;
  stats_.requests_success = 0;
  stats_.requests_failed = 0;
  stats_.requests_timeout = 0;
  stats_.request_duration_ms_total = 0;
  stats_.request_duration_ms_min = UINT64_MAX;
  stats_.request_duration_ms_max = 0;
  stats_.circuit_breaker_opens = 0;
  stats_.bytes_sent = 0;
  stats_.bytes_received = 0;
}

void AdvancedEchoClient::handleDataReceived(const std::string& data) {
  stats_.bytes_received += data.length();

  partial_message_ += data;

  // Process newline-delimited messages
  size_t pos = 0;
  while ((pos = partial_message_.find('\n')) != std::string::npos) {
    std::string message = partial_message_.substr(0, pos);
    partial_message_.erase(0, pos + 1);

    if (!message.empty()) {
      processMessage(message);
    }
  }
}

void AdvancedEchoClient::handleStatusChange(
    EchoTransportAdvanced::Status status) {
  if (status == EchoTransportAdvanced::Status::Error ||
      status == EchoTransportAdvanced::Status::Disconnected) {
    circuit_breaker_->recordFailure();
  }
}

void AdvancedEchoClient::handleError(const Error& error) {
  stats_.requests_failed++;
  circuit_breaker_->recordFailure();

  GOPHER_LOG_ERROR("Transport error: {}", error.message);
}

void AdvancedEchoClient::processMessage(const std::string& message) {
  try {
    auto json_val = json::JsonValue::parse(message);

    if (json_val.contains("result") || json_val.contains("error")) {
      // Response
      auto response = json::from_json<jsonrpc::Response>(json_val);

      if (holds_alternative<int64_t>(response.id)) {
        int id = get<int64_t>(response.id);
        auto request = request_manager_->getRequest(id);

        if (request) {
          // Calculate latency
          auto duration = std::chrono::steady_clock::now() - request->sent_time;
          auto duration_ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(duration)
                  .count();
          updateLatencyMetrics(duration_ms);

          if (response.error.has_value()) {
            stats_.requests_failed++;
            circuit_breaker_->recordFailure();
          } else {
            stats_.requests_success++;
            circuit_breaker_->recordSuccess();
          }

          request_manager_->completeRequest(id, response);
        }
      }
    } else if (json_val.contains("method")) {
      // Notification from server
      if (!json_val.contains("id")) {
        auto notification = json::from_json<jsonrpc::Notification>(json_val);
        // Log or handle server notifications as needed
      }
    }
  } catch (const std::exception& e) {
    stats_.requests_failed++;
    GOPHER_LOG_ERROR("Failed to parse message: {}", e.what());
  }
}

void AdvancedEchoClient::checkRequestTimeouts() {
  auto timed_out = request_manager_->checkTimeouts();

  for (const auto& request : timed_out) {
    stats_.requests_timeout++;
    stats_.requests_failed++;
    circuit_breaker_->recordFailure();

    request->promise.set_value(make<jsonrpc::Response>(request->id)
                                   .error(Error(-32001, "Request timeout"))
                                   .build());
  }
}

void AdvancedEchoClient::updateLatencyMetrics(uint64_t duration_ms) {
  stats_.request_duration_ms_total += duration_ms;

  uint64_t current_max = stats_.request_duration_ms_max;
  while (duration_ms > current_max &&
         !stats_.request_duration_ms_max.compare_exchange_weak(current_max,
                                                               duration_ms))
    ;

  uint64_t current_min = stats_.request_duration_ms_min;
  while (duration_ms < current_min &&
         !stats_.request_duration_ms_min.compare_exchange_weak(current_min,
                                                               duration_ms))
    ;
}

void AdvancedEchoClient::printMetrics() {
  GOPHER_LOG_INFO("Echo Client Statistics: requests_total={} success={} "
                  "failed={} timeout={} circuit_breaker_opens={} "
                  "bytes_sent={} bytes_recv={}",
                  stats_.requests_total.load(), stats_.requests_success.load(),
                  stats_.requests_failed.load(), stats_.requests_timeout.load(),
                  stats_.circuit_breaker_opens.load(), stats_.bytes_sent.load(),
                  stats_.bytes_received.load());

  if (stats_.requests_success.load() > 0) {
    uint64_t avg_latency =
        stats_.request_duration_ms_total.load() / stats_.requests_success.load();
    GOPHER_LOG_INFO("Latency: avg={} ms min={} ms max={} ms",
                    avg_latency, stats_.request_duration_ms_min.load(),
                    stats_.request_duration_ms_max.load());
  }

  const char* cb_state = "UNKNOWN";
  switch (circuit_breaker_->getState()) {
    case CircuitBreaker::State::Closed:
      cb_state = "CLOSED";
      break;
    case CircuitBreaker::State::Open:
      cb_state = "OPEN";
      break;
    case CircuitBreaker::State::HalfOpen:
      cb_state = "HALF-OPEN";
      break;
  }
  GOPHER_LOG_INFO("Circuit breaker state: {}", cb_state);
}

}  // namespace echo
}  // namespace mcp