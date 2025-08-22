#pragma once

#include "mcp/event/event_loop.h"
#include "mcp/filter/backpressure_filter.h"
#include "mcp/filter/circuit_breaker_filter.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/filter/metrics_filter.h"
#include "mcp/filter/rate_limit_filter.h"
#include "mcp/filter/request_validation_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/network/connection.h"
#include "mcp/network/filter.h"

// Forward declarations
namespace mcp {
class McpProtocolCallbacks;
}

namespace mcp {
namespace filter {

/**
 * Enhanced MCP Filter Chain Factory with Enterprise Features
 *
 * Builds a complete filter chain with:
 * - Circuit breaker for fault tolerance
 * - Rate limiting for flow control
 * - Metrics collection for observability
 * - Request validation for security
 * - Backpressure management for stability
 * - Protocol handling (HTTP, SSE, JSON-RPC)
 *
 * Filter Chain Architecture:
 * ```
 * [TCP Socket] → [Circuit Breaker] → [Rate Limiter] → [Metrics]
 *              → [Request Validator] → [Backpressure] → [HTTP Codec]
 *              → [SSE Codec] → [JSON-RPC] → [Application]
 * ```
 */
class McpEnhancedFilterChainFactory : public network::FilterChainFactory {
 public:
  /**
   * Configuration for enterprise filters
   */
  struct Config {
    // Circuit breaker settings
    bool enable_circuit_breaker = true;
    CircuitBreakerConfig circuit_breaker_config;

    // Rate limiting settings
    bool enable_rate_limiting = true;
    RateLimitConfig rate_limit_config;

    // Metrics collection settings
    bool enable_metrics = true;
    MetricsConfig metrics_config;

    // Request validation settings
    bool enable_request_validation = true;
    RequestValidationConfig validation_config;

    // Backpressure settings
    bool enable_backpressure = true;
    BackpressureConfig backpressure_config;
  };

  /**
   * Constructor
   * @param dispatcher Event dispatcher for async operations
   * @param message_callbacks MCP message callbacks for handling requests
   * @param is_server True for server mode, false for client mode
   * @param config Configuration for enterprise filters
   */
  McpEnhancedFilterChainFactory(event::Dispatcher& dispatcher,
                                McpProtocolCallbacks& message_callbacks,
                                bool is_server = true,
                                const Config& config = Config())
      : dispatcher_(dispatcher),
        message_callbacks_(message_callbacks),
        is_server_(is_server),
        config_(config) {}

  /**
   * Create filter chain for the connection
   * @param filter_manager The filter manager to add filters to
   * @return true if filter chain was created successfully
   */
  bool createFilterChain(network::FilterManager& filter_manager) const override;

  /**
   * Create network filter chain (alternative interface)
   */
  bool createNetworkFilterChain(network::FilterManager& filter_manager,
                                const std::vector<network::FilterFactoryCb>&
                                    filter_factories) const override;

  /**
   * Create listener filter chain
   * Not used for this implementation
   */
  bool createListenerFilterChain(
      network::FilterManager& filter_manager) const override {
    return false;
  }

  /**
   * Get metrics collector for external monitoring
   */
  std::shared_ptr<MetricsFilter> getMetricsFilter() const {
    return metrics_filter_;
  }

 private:
  event::Dispatcher& dispatcher_;
  McpProtocolCallbacks& message_callbacks_;
  bool is_server_;
  Config config_;

  // Store filters for lifetime management and access
  mutable std::shared_ptr<CircuitBreakerFilter> circuit_breaker_filter_;
  mutable std::shared_ptr<RateLimitFilter> rate_limit_filter_;
  mutable std::shared_ptr<MetricsFilter> metrics_filter_;
  mutable std::shared_ptr<RequestValidationFilter> validation_filter_;
  mutable std::shared_ptr<BackpressureFilter> backpressure_filter_;
  mutable std::vector<network::FilterSharedPtr> filters_;
};

}  // namespace filter
}  // namespace mcp