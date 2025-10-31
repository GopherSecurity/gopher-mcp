/**
 * Enhanced MCP Filter Chain Factory Implementation
 *
 * Integrates enterprise-grade filters for production use:
 * - Fault tolerance with circuit breaker
 * - Flow control with rate limiting
 * - Observability with metrics
 * - Security with request validation
 * - Stability with backpressure management
 */

#include "mcp/filter/enhanced_filter_chain_factory.h"

#include <iostream>

#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/mcp_connection_manager.h"

namespace mcp {
namespace filter {

/**
 * Enhanced filter implementing all protocol and enterprise layers
 * This combines the protocol handling with enterprise features
 */
class EnhancedProtocolFilter : public network::Filter,
                               public HttpCodecFilter::MessageCallbacks,
                               public SseCodecFilter::EventCallbacks,
                               public JsonRpcProtocolFilter::MessageHandler,
                               public CircuitBreakerFilter::Callbacks,
                               public RateLimitFilter::Callbacks,
                               public MetricsFilter::Callbacks,
                               public RequestValidationFilter::Callbacks,
                               public BackpressureFilter::Callbacks {
 public:
  EnhancedProtocolFilter(event::Dispatcher& dispatcher,
                         McpProtocolCallbacks& mcp_callbacks,
                         bool is_server,
                         const EnhancedFilterChainFactory::Config& config)
      : dispatcher_(dispatcher),
        mcp_callbacks_(mcp_callbacks),
        is_server_(is_server),
        config_(config) {
    // Create enterprise filters if enabled
    if (config_.enable_circuit_breaker) {
      circuit_breaker_ = std::make_shared<CircuitBreakerFilter>(
          *this, config_.circuit_breaker_config);
    }

    if (config_.enable_rate_limiting) {
      rate_limiter_ = std::make_shared<RateLimitFilter>(
          *this, dispatcher_, config_.rate_limit_config);
    }

    if (config_.enable_metrics) {
      metrics_collector_ = std::make_shared<MetricsFilter>(
          *this, config_.metrics_config);
      metrics_network_adapter_ = metrics_collector_->createNetworkAdapter();
    }

    if (config_.enable_request_validation) {
      request_validator_ = std::make_shared<RequestValidationFilter>(
          *this, config_.validation_config);
    }

    if (config_.enable_backpressure) {
      backpressure_manager_ = std::make_shared<BackpressureFilter>(
          *this, dispatcher_, config_.backpressure_config);
    }

    // Create protocol filters
    http_filter_ =
        std::make_shared<HttpCodecFilter>(*this, dispatcher_, is_server_);
    sse_filter_ =
        std::make_shared<SseCodecFilter>(*this, dispatcher_, is_server_);
    jsonrpc_filter_ =
        std::make_shared<JsonRpcProtocolFilter>(*this, dispatcher_, is_server_);

    // Wire up the filter chain callbacks
    if (circuit_breaker_) {
      circuit_breaker_->setNextCallbacks(this);
    }
  }

  // ===== Network Filter Interface =====

  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    // Data flows through enterprise filters first, then protocol layers
    // Circuit Breaker -> Rate Limiter -> Metrics -> Validator -> Backpressure
    // -> HTTP -> SSE -> JSON-RPC

    auto status = network::FilterStatus::Continue;

    // Enterprise filters
    if (circuit_breaker_) {
      status = circuit_breaker_->onData(data, end_stream);
      if (status == network::FilterStatus::StopIteration) {
        return status;
      }
    }

    if (rate_limiter_) {
      status = rate_limiter_->onData(data, end_stream);
      if (status == network::FilterStatus::StopIteration) {
        return status;
      }
    }

    if (metrics_network_adapter_) {
      metrics_network_adapter_->onData(data, end_stream);
    }

    if (request_validator_) {
      status = request_validator_->onData(data, end_stream);
      if (status == network::FilterStatus::StopIteration) {
        return status;
      }
    }

    if (backpressure_manager_) {
      status = backpressure_manager_->onData(data, end_stream);
      if (status == network::FilterStatus::StopIteration) {
        return status;
      }
    }

    // Protocol layers
    status = http_filter_->onData(data, end_stream);
    if (status == network::FilterStatus::StopIteration) {
      return status;
    }

    if (is_sse_mode_) {
      status = sse_filter_->onData(data, end_stream);
      if (status == network::FilterStatus::StopIteration) {
        return status;
      }
    }

    if (pending_json_data_.length() > 0) {
      status = jsonrpc_filter_->onData(pending_json_data_, end_stream);
      pending_json_data_.drain(pending_json_data_.length());
    }

    return status;
  }

  network::FilterStatus onNewConnection() override {
    // Initialize all filters
    if (circuit_breaker_)
      circuit_breaker_->onNewConnection();
    if (rate_limiter_)
      rate_limiter_->onNewConnection();
    if (metrics_network_adapter_)
      metrics_network_adapter_->onNewConnection();
    if (request_validator_)
      request_validator_->onNewConnection();
    if (backpressure_manager_)
      backpressure_manager_->onNewConnection();

    http_filter_->onNewConnection();
    sse_filter_->onNewConnection();
    jsonrpc_filter_->onNewConnection();

    return network::FilterStatus::Continue;
  }

  network::FilterStatus onWrite(Buffer& data, bool end_stream) override {
    // Write flows through filters in reverse order
    auto status = jsonrpc_filter_->onWrite(data, end_stream);
    if (status == network::FilterStatus::StopIteration) {
      return status;
    }

    if (is_sse_mode_) {
      status = sse_filter_->onWrite(data, end_stream);
      if (status == network::FilterStatus::StopIteration) {
        return status;
      }
    }

    return http_filter_->onWrite(data, end_stream);
  }

  void initializeReadFilterCallbacks(
      network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    if (circuit_breaker_)
      circuit_breaker_->initializeReadFilterCallbacks(callbacks);
    if (rate_limiter_)
      rate_limiter_->initializeReadFilterCallbacks(callbacks);
    if (metrics_network_adapter_)
      metrics_network_adapter_->initializeReadFilterCallbacks(callbacks);
    if (request_validator_)
      request_validator_->initializeReadFilterCallbacks(callbacks);
    if (backpressure_manager_)
      backpressure_manager_->initializeReadFilterCallbacks(callbacks);
    http_filter_->initializeReadFilterCallbacks(callbacks);
    sse_filter_->initializeReadFilterCallbacks(callbacks);
    jsonrpc_filter_->initializeReadFilterCallbacks(callbacks);
  }

  void initializeWriteFilterCallbacks(
      network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
    if (circuit_breaker_)
      circuit_breaker_->initializeWriteFilterCallbacks(callbacks);
    if (rate_limiter_)
      rate_limiter_->initializeWriteFilterCallbacks(callbacks);
    if (metrics_network_adapter_)
      metrics_network_adapter_->initializeWriteFilterCallbacks(callbacks);
    if (request_validator_)
      request_validator_->initializeWriteFilterCallbacks(callbacks);
    if (backpressure_manager_)
      backpressure_manager_->initializeWriteFilterCallbacks(callbacks);
    http_filter_->initializeWriteFilterCallbacks(callbacks);
    sse_filter_->initializeWriteFilterCallbacks(callbacks);
    jsonrpc_filter_->initializeWriteFilterCallbacks(callbacks);
  }

  // ===== HttpCodecFilter::MessageCallbacks =====

  void onHeaders(const std::map<std::string, std::string>& headers,
                 bool keep_alive) override {
    // Determine transport mode based on headers
    if (is_server_) {
      auto accept = headers.find("accept");
      if (accept != headers.end() &&
          accept->second.find("text/event-stream") != std::string::npos) {
        is_sse_mode_ = true;

        std::map<std::string, std::string> response_headers = {
            {"content-type", "text/event-stream"},
            {"cache-control", "no-cache"},
            {"connection", keep_alive ? "keep-alive" : "close"},
            {"access-control-allow-origin", "*"}};

        http_filter_->messageEncoder().encodeHeaders("200", response_headers,
                                                     false);
        sse_filter_->startEventStream();
      } else {
        is_sse_mode_ = false;
      }
    } else {
      auto content_type = headers.find("content-type");
      is_sse_mode_ =
          content_type != headers.end() &&
          content_type->second.find("text/event-stream") != std::string::npos;
    }
  }

  void onBody(const std::string& data, bool end_stream) override {
    if (is_sse_mode_) {
      sse_filter_->onData(
          reinterpret_cast<Buffer&>(const_cast<std::string&>(data)),
          end_stream);
    } else {
      pending_json_data_.add(data);
      if (end_stream) {
        jsonrpc_filter_->onData(pending_json_data_, true);
        pending_json_data_.drain(pending_json_data_.length());
      }
    }
  }

  void onTrailers(const std::map<std::string, std::string>& trailers) override {
    // Pass through trailers if needed
  }

  void onComplete() override {
    // Request/response complete
  }

  void onError(const std::string& error) override {
    std::cerr << "[HTTP Error] " << error << std::endl;
  }

  // ===== SseCodecFilter::EventCallbacks =====

  void onSseEvent(const std::string& event,
                  const std::string& data,
                  const optional<std::string>& id,
                  const optional<int>& retry) override {
    // Parse JSON-RPC from SSE event data
    pending_json_data_.add(data);
    jsonrpc_filter_->onData(pending_json_data_, false);
    pending_json_data_.drain(pending_json_data_.length());
  }

  void onSseError(const std::string& error) override {
    std::cerr << "[SSE Error] " << error << std::endl;
  }

  // ===== JsonRpcProtocolFilter::MessageHandler =====

  void onRequest(const jsonrpc::Request& request) override {
    // Track metrics
    if (metrics_collector_) {
      metrics_collector_->onRequest(request);
    }

    // Forward to MCP layer
    mcp_callbacks_.onRequest(request);
  }

  void onResponse(const jsonrpc::Response& response) override {
    // Track metrics
    if (metrics_collector_) {
      metrics_collector_->onResponse(response);
    }

    // Forward to MCP layer
    mcp_callbacks_.onResponse(response);
  }

  void onNotification(const jsonrpc::Notification& notification) override {
    // Track metrics
    if (metrics_collector_) {
      metrics_collector_->onNotification(notification);
    }

    // Forward to MCP layer
    mcp_callbacks_.onNotification(notification);
  }

  void onProtocolError(const Error& error) override {
    // Track errors
    if (circuit_breaker_) {
      circuit_breaker_->onProtocolError(error);
    }

    // Forward to MCP layer
    mcp_callbacks_.onProtocolError(error);
  }

  // ===== Circuit Breaker Callbacks =====

  void onStateChange(CircuitState old_state,
                     CircuitState new_state,
                     const std::string& reason) override {
    std::cerr << "[Circuit Breaker] State change: "
              << static_cast<int>(old_state) << " -> "
              << static_cast<int>(new_state) << " (" << reason << ")"
              << std::endl;
  }

  void onRequestBlocked(const std::string& method) override {
    std::cerr << "[Circuit Breaker] Request blocked: " << method << std::endl;
  }

  void onHealthUpdate(double success_rate, uint64_t latency_ms) override {
    // Could forward to metrics collector
  }

  // ===== Rate Limiter Callbacks =====

  void onRequestThrottled(const std::string& method,
                          size_t current_rate,
                          size_t max_rate) override {
    std::cerr << "[Rate Limiter] Request throttled: " << method
              << " (rate: " << current_rate << "/" << max_rate << ")"
              << std::endl;
  }

  void onRateLimitExceeded(const std::string& bucket_name) override {
    std::cerr << "[Rate Limiter] Rate limit exceeded for bucket: "
              << bucket_name << std::endl;
  }

  // ===== Metrics Callbacks =====

  void onMetricsUpdate(const MetricsSnapshot& snapshot) override {
    // Metrics updated - could expose via endpoint
  }

  // ===== Request Validation Callbacks =====

  void onValidationSuccess(const jsonrpc::Request& request) override {
    // Request passed validation
  }

  void onValidationFailure(const jsonrpc::Request& request,
                           const std::string& reason) override {
    std::cerr << "[Validator] Request validation failed: " << reason
              << std::endl;
  }

  // ===== Backpressure Callbacks =====

  void onBackpressureActivated(size_t queue_size, size_t max_size) override {
    std::cerr << "[Backpressure] Activated (queue: " << queue_size << "/"
              << max_size << ")" << std::endl;
  }

  void onBackpressureRelieved() override {
    std::cerr << "[Backpressure] Relieved" << std::endl;
  }

  void onRequestDropped(const std::string& reason) override {
    std::cerr << "[Backpressure] Request dropped: " << reason << std::endl;
  }

 private:
  event::Dispatcher& dispatcher_;
  McpProtocolCallbacks& mcp_callbacks_;
  bool is_server_;
  EnhancedFilterChainFactory::Config config_;

  // Enterprise filters
  std::shared_ptr<CircuitBreakerFilter> circuit_breaker_;
  std::shared_ptr<RateLimitFilter> rate_limiter_;
  std::shared_ptr<MetricsFilter> metrics_collector_;
  std::shared_ptr<MetricsFilter::NetworkAdapter> metrics_network_adapter_;
  std::shared_ptr<RequestValidationFilter> request_validator_;
  std::shared_ptr<BackpressureFilter> backpressure_manager_;

  // Protocol filters
  std::shared_ptr<HttpCodecFilter> http_filter_;
  std::shared_ptr<SseCodecFilter> sse_filter_;
  std::shared_ptr<JsonRpcProtocolFilter> jsonrpc_filter_;

  // State
  bool is_sse_mode_ = false;
  Buffer pending_json_data_;
  network::ReadFilterCallbacks* read_callbacks_ = nullptr;
  network::WriteFilterCallbacks* write_callbacks_ = nullptr;
};

// ===== Factory Implementation =====

bool EnhancedFilterChainFactory::createFilterChain(
    network::FilterManager& filter_manager) const {
  // Create the enhanced filter with all features
  auto filter = std::make_shared<EnhancedProtocolFilter>(
      dispatcher_, message_callbacks_, is_server_, config_);

  // Store references to specific filters for external access
  if (config_.enable_circuit_breaker) {
    circuit_breaker_filter_ = filter->circuit_breaker_;
  }

  if (config_.enable_rate_limiting) {
    rate_limit_filter_ = filter->rate_limiter_;
  }

  if (config_.enable_metrics) {
    metrics_filter_ = filter->metrics_collector_;
  }

  if (config_.enable_request_validation) {
    validation_filter_ = filter->request_validator_;
  }

  if (config_.enable_backpressure) {
    backpressure_filter_ = filter->backpressure_manager_;
  }

  // Add to filter manager
  filter_manager.addReadFilter(filter);
  filter_manager.addWriteFilter(filter);

  // Store for lifetime management
  filters_.push_back(filter);

  return true;
}

bool EnhancedFilterChainFactory::createNetworkFilterChain(
    network::FilterManager& filter_manager,
    const std::vector<network::FilterFactoryCb>& filter_factories) const {
  // Create our enhanced filter chain
  bool success = createFilterChain(filter_manager);

  // Apply any additional filter factories
  for (const auto& factory : filter_factories) {
    factory(filter_manager);
  }

  return success;
}

}  // namespace filter
}  // namespace mcp
