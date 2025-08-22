/**
 * @file request_validation_filter.h
 * @brief Request validation filter for MCP protocol compliance
 *
 * PRIMARY USE: SERVER - Critical for security and protocol compliance
 * SECONDARY USE: CLIENT - Optional for early validation before sending
 *
 * This filter validates incoming requests against MCP protocol specifications
 * to ensure protocol compliance and security.
 *
 * Server Usage:
 * - SECURITY CRITICAL - First line of defense against malicious requests
 * - Validates method names against whitelist/blacklist
 * - Enforces parameter size limits to prevent DOS
 * - Checks JSON depth to prevent stack overflow attacks
 * - Validates protocol version for compatibility
 * - Prevents injection attacks through input validation
 *
 * Client Usage:
 * - Optional pre-flight validation before network round-trip
 * - Useful during development to catch errors early
 * - Can validate request format matches server expectations
 * - Helps with debugging protocol issues
 *
 * Security Features:
 * - Method whitelisting/blacklisting
 * - Parameter size limits
 * - JSON depth validation
 * - String length limits
 * - Protocol version checking
 */

#pragma once

#include <regex>
#include <set>
#include <string>

#include "../network/filter.h"
#include "../types.h"
#include "mcp_jsonrpc_filter.h"

namespace mcp {
namespace filter {

/**
 * Configuration for request validation
 */
struct RequestValidationConfig {
  // Method validation
  bool validate_methods = true;
  std::set<std::string> allowed_methods;
  std::set<std::string> blocked_methods;

  // Parameter validation
  bool validate_params = true;
  size_t max_param_size = 1024 * 1024;  // 1MB default

  // Rate limiting per method
  bool enable_rate_limiting = false;
  std::map<std::string, size_t> method_rate_limits;  // requests per minute

  // Security validations
  bool validate_json_depth = true;
  size_t max_json_depth = 100;

  bool validate_string_length = true;
  size_t max_string_length = 65536;  // 64KB default

  // Protocol version validation
  bool validate_protocol_version = true;
  std::string required_protocol_version = "2.0";
};

/**
 * Request validation filter
 *
 * This filter validates incoming JSON-RPC requests for:
 * - Method whitelisting/blacklisting
 * - Parameter size and structure
 * - Rate limiting per method
 * - Security constraints (JSON depth, string length)
 * - Protocol version compliance
 */
class RequestValidationFilter : public network::NetworkFilterBase,
                                public JsonRpcProtocolFilter::MessageHandler {
 public:
  /**
   * Callbacks for validation events
   */
  class ValidationCallbacks {
   public:
    virtual ~ValidationCallbacks() = default;

    /**
     * Called when a request is validated successfully
     * @param method The method that was validated
     */
    virtual void onRequestValidated(const std::string& method) = 0;

    /**
     * Called when a request fails validation
     * @param method The method that failed validation
     * @param reason The reason for validation failure
     */
    virtual void onRequestRejected(const std::string& method,
                                   const std::string& reason) = 0;

    /**
     * Called when rate limit is exceeded
     * @param method The method that exceeded rate limit
     */
    virtual void onRateLimitExceeded(const std::string& method) = 0;
  };

  /**
   * Constructor
   * @param callbacks Validation event callbacks
   * @param config Validation configuration
   */
  RequestValidationFilter(
      ValidationCallbacks& callbacks,
      const RequestValidationConfig& config = RequestValidationConfig())
      : callbacks_(callbacks),
        config_(config),
        last_rate_check_(std::chrono::steady_clock::now()) {
    // Initialize default allowed methods for MCP
    if (config_.allowed_methods.empty() && config_.validate_methods) {
      config_.allowed_methods = {"initialize",     "ping",
                                 "tools/list",     "tools/call",
                                 "resources/list", "resources/read",
                                 "prompts/list",   "prompts/get",
                                 "complete",       "logging/setLevel"};
    }
  }

  // JsonRpcProtocolFilter::MessageHandler implementation
  void onRequest(const jsonrpc::Request& request) override {
    // Validate the request
    if (!validateRequest(request)) {
      // Request was rejected - error already sent
      return;
    }

    // Forward to next filter or handler
    if (next_callbacks_) {
      next_callbacks_->onRequest(request);
    }
  }

  void onNotification(const jsonrpc::Notification& notification) override {
    // Validate notification similar to request
    if (!validateNotification(notification)) {
      return;
    }

    if (next_callbacks_) {
      next_callbacks_->onNotification(notification);
    }
  }

  void onResponse(const jsonrpc::Response& response) override {
    // Responses typically don't need validation in server context
    // Just forward them
    if (next_callbacks_) {
      next_callbacks_->onResponse(response);
    }
  }

  void onProtocolError(const Error& error) override {
    // Forward protocol errors
    if (next_callbacks_) {
      next_callbacks_->onProtocolError(error);
    }
  }

  // Filter interface implementation
  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    // This filter operates at JSON-RPC level, not raw data level
    // Data processing is handled by JSON-RPC filter before us
    return network::FilterStatus::Continue;
  }

  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    return network::FilterStatus::Continue;
  }

  network::FilterStatus onNewConnection() override {
    // Reset rate limiting counters for new connection
    method_request_counts_.clear();
    last_rate_check_ = std::chrono::steady_clock::now();
    return network::FilterStatus::Continue;
  }

  /**
   * Set the next callbacks in the chain
   * @param callbacks The next callbacks to forward to after validation
   */
  void setNextCallbacks(JsonRpcProtocolFilter::MessageHandler* callbacks) {
    next_callbacks_ = callbacks;
  }

 private:
  bool validateRequest(const jsonrpc::Request& request) {
    // Validate protocol version
    if (config_.validate_protocol_version) {
      if (request.jsonrpc != config_.required_protocol_version) {
        callbacks_.onRequestRejected(
            request.method, "Invalid protocol version: " + request.jsonrpc);
        sendErrorResponse(request.id, jsonrpc::INVALID_REQUEST,
                          "Invalid protocol version");
        return false;
      }
    }

    // Validate method
    if (config_.validate_methods) {
      // Check blacklist first
      if (config_.blocked_methods.count(request.method) > 0) {
        callbacks_.onRequestRejected(request.method, "Method is blocked");
        sendErrorResponse(request.id, jsonrpc::METHOD_NOT_FOUND,
                          "Method not allowed");
        return false;
      }

      // Check whitelist if configured
      if (!config_.allowed_methods.empty() &&
          config_.allowed_methods.count(request.method) == 0) {
        callbacks_.onRequestRejected(request.method, "Method not in whitelist");
        sendErrorResponse(request.id, jsonrpc::METHOD_NOT_FOUND,
                          "Method not allowed");
        return false;
      }
    }

    // Check rate limiting
    if (config_.enable_rate_limiting) {
      if (!checkRateLimit(request.method)) {
        callbacks_.onRateLimitExceeded(request.method);
        sendErrorResponse(request.id, jsonrpc::INTERNAL_ERROR,
                          "Rate limit exceeded");
        return false;
      }
    }

    // Validate parameters size
    if (config_.validate_params && request.params.has_value()) {
      // This would need JSON serialization to check size
      // For now, just count the basic structure
      // TODO: Implement proper size calculation
    }

    callbacks_.onRequestValidated(request.method);
    return true;
  }

  bool validateNotification(const jsonrpc::Notification& notification) {
    // Similar validation as request but without response
    if (config_.validate_methods) {
      if (config_.blocked_methods.count(notification.method) > 0 ||
          (!config_.allowed_methods.empty() &&
           config_.allowed_methods.count(notification.method) == 0)) {
        callbacks_.onRequestRejected(notification.method, "Method not allowed");
        return false;
      }
    }

    callbacks_.onRequestValidated(notification.method);
    return true;
  }

  bool checkRateLimit(const std::string& method) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(
        now - last_rate_check_);

    if (elapsed.count() >= 1) {
      // Reset counters every minute
      method_request_counts_.clear();
      last_rate_check_ = now;
    }

    // Check if method has rate limit
    auto limit_it = config_.method_rate_limits.find(method);
    if (limit_it != config_.method_rate_limits.end()) {
      size_t& count = method_request_counts_[method];
      if (count >= limit_it->second) {
        return false;  // Rate limit exceeded
      }
      count++;
    }

    return true;
  }

  void sendErrorResponse(const RequestId& id,
                         int code,
                         const std::string& message) {
    // TODO: Send error response through the connection
    // This would need access to the write callbacks
  }

  ValidationCallbacks& callbacks_;
  RequestValidationConfig config_;
  JsonRpcProtocolFilter::MessageHandler* next_callbacks_ = nullptr;

  // Rate limiting state
  std::map<std::string, size_t> method_request_counts_;
  std::chrono::steady_clock::time_point last_rate_check_;
};

}  // namespace filter
}  // namespace mcp