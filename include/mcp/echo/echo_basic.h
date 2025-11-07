/**
 * @file echo_basic.h
 * @brief Reusable transport-agnostic echo server and client base classes
 *
 * This provides abstract base classes for echo implementations that can
 * work with any transport (stdio, TCP, HTTP, WebSocket, etc.)
 *
 * Thread Safety Architecture:
 * - Transport implementations handle their own thread safety
 * - Base classes use mutex protection for shared state
 * - Message processing is serialized per connection
 * - Callbacks are invoked on transport's thread context
 *
 * Design Principles:
 * - Separation of transport from protocol logic
 * - Virtual methods for customization points
 * - Builder pattern for message construction
 * - Future-based async request/response handling
 */

#ifndef MCP_ECHO_BASIC_H
#define MCP_ECHO_BASIC_H

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

#include "mcp/builders.h"
#include "mcp/event/event_loop.h"
#include "mcp/json/json_serialization.h"
#include "mcp/types.h"

namespace mcp {
namespace echo {

/**
 * Basic transport interface for echo implementations
 *
 * This abstracts the underlying transport mechanism (stdio, TCP, etc.)
 * This is the simpler interface used by basic echo examples.
 * For advanced features, see EchoTransport in echo_transport_advanced.h
 *
 * Thread Safety Requirements:
 * - Implementations must be thread-safe for send()
 * - Callbacks will be invoked from transport's thread
 * - Connection state changes must be atomic
 * - start()/stop() must be idempotent
 */
class EchoTransportBase {
 public:
  virtual ~EchoTransportBase() = default;

  /**
   * Send data through the transport
   */
  virtual void send(const std::string& data) = 0;

  /**
   * Register callback for received data
   */
  using DataCallback = std::function<void(const std::string&)>;
  virtual void setDataCallback(DataCallback callback) = 0;

  /**
   * Register callback for connection events
   */
  using ConnectionCallback = std::function<void(bool connected)>;
  virtual void setConnectionCallback(ConnectionCallback callback) = 0;

  /**
   * Start the transport
   */
  virtual bool start() = 0;

  /**
   * Stop the transport
   */
  virtual void stop() = 0;

  /**
   * Check if transport is connected
   */
  virtual bool isConnected() const = 0;

  /**
   * Get transport type name
   */
  virtual std::string getTransportType() const = 0;
};

using EchoTransportBasePtr = std::unique_ptr<EchoTransportBase>;

/**
 * Base class for echo server implementations
 *
 * Thread Safety:
 * - Single-threaded message processing per connection
 * - Transport callbacks are serialized
 * - Virtual methods called in transport's thread context
 * - Derived classes should not assume main thread execution
 *
 * Message Processing Flow:
 * 1. Transport receives data and invokes callback
 * 2. Base class accumulates data until complete message
 * 3. JSON-RPC message parsed and type determined
 * 4. Virtual handleRequest/handleNotification called
 * 5. Response/notification sent via transport
 */
class EchoServerBase {
 public:
  struct Config {
    bool enable_logging;
    bool echo_notifications;
    std::string server_name;

    Config()
        : enable_logging(true),
          echo_notifications(true),
          server_name("MCP Echo Server") {}
  };

  EchoServerBase(EchoTransportBasePtr transport,
                 const Config& config = Config())
      : config_(config), transport_(std::move(transport)), running_(false) {
    setupCallbacks();
  }

  virtual ~EchoServerBase() { stop(); }

  /**
   * Start the echo server
   *
   * Flow:
   * 1. Check if already running (idempotent)
   * 2. Start transport (may spawn threads)
   * 3. Send server.ready notification
   * 4. Ready to process messages
   */
  bool start() {
    if (running_) {
      return true;
    }

    if (!transport_->start()) {
      logError("Failed to start transport");
      return false;
    }

    running_ = true;
    logInfo("Echo server started using " + transport_->getTransportType());

    // Send server ready notification
    sendServerReady();

    return true;
  }

  /**
   * Stop the echo server
   */
  void stop() {
    if (!running_) {
      return;
    }

    running_ = false;
    transport_->stop();
    logInfo("Echo server stopped");
  }

  /**
   * Check if server is running
   */
  bool isRunning() const { return running_; }

 protected:
  // Protected config_ so derived classes can access it
  Config config_;

  /**
   * Handle incoming JSON-RPC request
   * Override this to customize request handling
   */
  virtual jsonrpc::Response handleRequest(const jsonrpc::Request& request) {
    // Default echo implementation
    auto response =
        make<jsonrpc::Response>(request.id)
            .result(jsonrpc::ResponseResult(
                make<Metadata>()
                    .add("echo", true)
                    .add("method", request.method)
                    .add("server", config_.server_name)
                    .add("timestamp", std::chrono::system_clock::now()
                                          .time_since_epoch()
                                          .count())
                    .build()))
            .build();

    return response;
  }

  /**
   * Handle incoming JSON-RPC notification
   * Override this to customize notification handling
   */
  virtual void handleNotification(const jsonrpc::Notification& notification) {
    // Special handling for shutdown
    if (notification.method == "shutdown") {
      logInfo("Received shutdown notification");
      stop();
      return;
    }

    // Echo notifications back if enabled
    if (config_.echo_notifications) {
      auto echo =
          make<jsonrpc::Notification>("echo/" + notification.method)
              .params(make<Metadata>()
                          .add("original_method", notification.method)
                          .add("server", config_.server_name)
                          .add("timestamp", std::chrono::system_clock::now()
                                                .time_since_epoch()
                                                .count())
                          .build())
              .build();

      sendNotification(echo);
    }
  }

  /**
   * Log info message
   */
  void logInfo(const std::string& message) {
    if (config_.enable_logging) {
      std::cerr << "[INFO] " << config_.server_name << ": " << message
                << std::endl;
    }
  }

  /**
   * Log error message
   */
  void logError(const std::string& message) {
    if (config_.enable_logging) {
      std::cerr << "[ERROR] " << config_.server_name << ": " << message
                << std::endl;
    }
  }

 private:
  void setupCallbacks() {
    // Setup data callback
    // NOTE: This will be called from transport's thread context
    // All message processing happens in that thread
    transport_->setDataCallback(
        [this](const std::string& data) { processIncomingData(data); });

    // Setup connection callback
    transport_->setConnectionCallback([this](bool connected) {
      if (connected) {
        logInfo("Client connected");
      } else {
        logInfo("Client disconnected");
        // Stop server when client disconnects (EOF on stdin)
        // This ensures clean exit when input pipe closes
        stop();
      }
    });
  }

  void processIncomingData(const std::string& data) {
    // Buffer partial data until complete message received
    // Messages are newline-delimited JSON-RPC
    partial_message_ += data;

    // Process newline-delimited JSON-RPC messages
    size_t pos = 0;
    while ((pos = partial_message_.find('\n')) != std::string::npos) {
      std::string message = partial_message_.substr(0, pos);
      partial_message_.erase(0, pos + 1);

      if (!message.empty()) {
        processJsonRpcMessage(message);
      }
    }
  }

  void processJsonRpcMessage(const std::string& message) {
    try {
      auto json_val = json::JsonValue::parse(message);

      // Determine message type
      if (json_val.contains("method")) {
        if (json_val.contains("id")) {
          // Request
          auto request = json::from_json<jsonrpc::Request>(json_val);
          auto response = handleRequest(request);
          sendResponse(response);
        } else {
          // Notification
          auto notification = json::from_json<jsonrpc::Notification>(json_val);
          handleNotification(notification);
        }
      } else {
        logError("Invalid JSON-RPC message: missing method");
      }
    } catch (const std::exception& e) {
      logError("Failed to process message: " + std::string(e.what()));

      // Send error response if we can extract an ID
      try {
        auto json_val = json::JsonValue::parse(message);
        if (json_val.contains("id")) {
          auto id = json_val["id"];
          auto error_response =
              make<jsonrpc::Response>(0)  // Will be overridden
                  .error(Error(jsonrpc::PARSE_ERROR, e.what()))
                  .build();
          sendResponse(error_response);
        }
      } catch (...) {
        // Unable to send error response
      }
    }
  }

  void sendResponse(const jsonrpc::Response& response) {
    auto json_val = json::to_json(response);
    transport_->send(json_val.toString() + "\n");
  }

  void sendNotification(const jsonrpc::Notification& notification) {
    auto json_val = json::to_json(notification);
    transport_->send(json_val.toString() + "\n");
  }

  void sendServerReady() {
    auto notification =
        make<jsonrpc::Notification>("server.ready")
            .params(make<Metadata>()
                        .add("server", config_.server_name)
                        .add("transport", transport_->getTransportType())
                        .add("timestamp", std::chrono::system_clock::now()
                                              .time_since_epoch()
                                              .count())
                        .build())
            .build();

    sendNotification(notification);
  }

  EchoTransportBasePtr transport_;
  bool running_;
  std::string partial_message_;
};

/**
 * Base class for echo client implementations
 *
 * Thread Safety:
 * - Request tracking uses mutex-protected map
 * - Futures provide thread-safe response delivery
 * - Transport callbacks are serialized
 * - Multiple threads can call sendRequest concurrently
 *
 * Request/Response Flow:
 * 1. sendRequest creates promise/future pair
 * 2. Request tracked in pending_requests_ map
 * 3. Request sent via transport
 * 4. Response received in transport thread
 * 5. Response matched by ID, promise completed
 * 6. Calling thread gets response via future
 */
class EchoClientBase {
 public:
  struct Config {
    bool enable_logging;
    std::string client_name;
    std::chrono::milliseconds request_timeout;

    Config()
        : enable_logging(true),
          client_name("MCP Echo Client"),
          request_timeout(30000) {}
  };

  EchoClientBase(EchoTransportBasePtr transport,
                 const Config& config = Config())
      : config_(config),
        transport_(std::move(transport)),
        running_(false),
        next_request_id_(1) {
    setupCallbacks();
  }

  virtual ~EchoClientBase() { stop(); }

  /**
   * Start the echo client
   */
  bool start() {
    if (running_) {
      return true;
    }

    if (!transport_->start()) {
      logError("Failed to start transport");
      return false;
    }

    running_ = true;
    logInfo("Echo client started using " + transport_->getTransportType());

    return true;
  }

  /**
   * Stop the echo client
   */
  void stop() {
    if (!running_) {
      return;
    }

    // Send shutdown notification
    sendNotification("shutdown", {});

    running_ = false;
    transport_->stop();
    logInfo("Echo client stopped");

    // Cancel all pending requests
    std::lock_guard<std::mutex> lock(pending_mutex_);
    for (auto& pair : pending_requests_) {
      pair.second.promise.set_exception(
          std::make_exception_ptr(std::runtime_error("Client stopped")));
    }
    pending_requests_.clear();
  }

  /**
   * Send a request and get future for response
   *
   * Thread-safe: Can be called from any thread
   * Returns immediately with future for async response
   *
   * Flow:
   * 1. Generate unique request ID
   * 2. Create promise/future pair
   * 3. Store in pending requests (mutex protected)
   * 4. Send request via transport
   * 5. Return future to caller
   */
  std::future<jsonrpc::Response> sendRequest(const std::string& method,
                                             const Metadata& params = {}) {
    if (!running_ || !transport_->isConnected()) {
      std::promise<jsonrpc::Response> promise;
      promise.set_exception(
          std::make_exception_ptr(std::runtime_error("Client not connected")));
      return promise.get_future();
    }

    int id = next_request_id_++;

    auto request = make<jsonrpc::Request>(id, method).params(params).build();

    // Store pending request
    PendingRequest pending;
    pending.method = method;
    pending.sent_time = std::chrono::steady_clock::now();
    auto future = pending.promise.get_future();

    {
      std::lock_guard<std::mutex> lock(pending_mutex_);
      pending_requests_[id] = std::move(pending);
    }

    // Send request
    auto json_val = json::to_json(request);
    transport_->send(json_val.toString() + "\n");

    logInfo("Sent request #" + std::to_string(id) + " (method: " + method +
            ")");

    return future;
  }

  /**
   * Send a notification (no response expected)
   */
  void sendNotification(const std::string& method,
                        const Metadata& params = {}) {
    if (!running_ || !transport_->isConnected()) {
      return;
    }

    auto notification =
        make<jsonrpc::Notification>(method).params(params).build();

    auto json_val = json::to_json(notification);
    transport_->send(json_val.toString() + "\n");

    logInfo("Sent notification: " + method);
  }

  /**
   * Check if client is running
   */
  bool isRunning() const { return running_; }

  /**
   * Check if client is connected
   */
  bool isConnected() const { return running_ && transport_->isConnected(); }

 protected:
  // Protected config_ so derived classes can access it
  Config config_;

  /**
   * Handle incoming notification from server
   * Override this to customize notification handling
   */
  virtual void handleNotification(const jsonrpc::Notification& notification) {
    logInfo("Received notification: " + notification.method);
  }

  /**
   * Handle unexpected request from server
   * Override this to customize request handling
   */
  virtual void handleRequest(const jsonrpc::Request& request) {
    logInfo("Received unexpected request from server: " + request.method);

    // Send error response
    auto response = make<jsonrpc::Response>(request.id)
                        .error(Error(jsonrpc::METHOD_NOT_FOUND,
                                     "Client doesn't handle requests"))
                        .build();

    auto json_val = json::to_json(response);
    transport_->send(json_val.toString() + "\n");
  }

  /**
   * Log info message
   */
  void logInfo(const std::string& message) {
    if (config_.enable_logging) {
      std::cerr << "[INFO] " << config_.client_name << ": " << message
                << std::endl;
    }
  }

  /**
   * Log error message
   */
  void logError(const std::string& message) {
    if (config_.enable_logging) {
      std::cerr << "[ERROR] " << config_.client_name << ": " << message
                << std::endl;
    }
  }

 private:
  struct PendingRequest {
    std::string method;
    std::chrono::steady_clock::time_point sent_time;
    std::promise<jsonrpc::Response> promise;
  };

  void setupCallbacks() {
    // Setup data callback
    transport_->setDataCallback(
        [this](const std::string& data) { processIncomingData(data); });

    // Setup connection callback
    transport_->setConnectionCallback([this](bool connected) {
      if (connected) {
        logInfo("Connected to server");
      } else {
        logInfo("Disconnected from server");

        // Fail all pending requests
        std::lock_guard<std::mutex> lock(pending_mutex_);
        for (auto& pair : pending_requests_) {
          pair.second.promise.set_exception(
              std::make_exception_ptr(std::runtime_error("Connection lost")));
        }
        pending_requests_.clear();

        // Stop client when disconnected (EOF on stdin)
        // This ensures clean exit when input pipe closes
        running_ = false;
      }
    });
  }

  void processIncomingData(const std::string& data) {
    // Buffer partial data until complete message received
    // Messages are newline-delimited JSON-RPC
    partial_message_ += data;

    // Process newline-delimited JSON-RPC messages
    size_t pos = 0;
    while ((pos = partial_message_.find('\n')) != std::string::npos) {
      std::string message = partial_message_.substr(0, pos);
      partial_message_.erase(0, pos + 1);

      if (!message.empty()) {
        processJsonRpcMessage(message);
      }
    }
  }

  void processJsonRpcMessage(const std::string& message) {
    try {
      auto json_val = json::JsonValue::parse(message);

      if (json_val.contains("result") || json_val.contains("error")) {
        // Response
        auto response = json::from_json<jsonrpc::Response>(json_val);
        handleResponse(response);
      } else if (json_val.contains("method")) {
        if (json_val.contains("id")) {
          // Request from server
          auto request = json::from_json<jsonrpc::Request>(json_val);
          handleRequest(request);
        } else {
          // Notification from server
          auto notification = json::from_json<jsonrpc::Notification>(json_val);
          handleNotification(notification);
        }
      }
    } catch (const std::exception& e) {
      logError("Failed to process message: " + std::string(e.what()));
    }
  }

  void handleResponse(const jsonrpc::Response& response) {
    // Called from transport thread
    // Must be thread-safe with sendRequest
    std::lock_guard<std::mutex> lock(pending_mutex_);

    int id = 0;
    if (holds_alternative<int64_t>(response.id)) {
      id = get<int64_t>(response.id);
    }

    auto it = pending_requests_.find(id);
    if (it != pending_requests_.end()) {
      // Calculate latency
      auto duration = std::chrono::steady_clock::now() - it->second.sent_time;
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration)
                    .count();

      if (response.error.has_value()) {
        logError("Request #" + std::to_string(id) +
                 " failed: " + response.error->message);
      } else {
        logInfo("Request #" + std::to_string(id) + " completed in " +
                std::to_string(ms) + "ms");
      }

      // Complete the promise
      it->second.promise.set_value(response);
      pending_requests_.erase(it);
    } else {
      logError("Received response for unknown request ID: " +
               std::to_string(id));
    }
  }

  EchoTransportBasePtr transport_;
  bool running_;
  std::string partial_message_;
  std::atomic<int> next_request_id_;
  std::map<int, PendingRequest> pending_requests_;
  std::mutex pending_mutex_;
};

}  // namespace echo
}  // namespace mcp

#endif  // MCP_ECHO_BASIC_H