/**
 * @file filter_configurations.cc
 * @brief Example filter configurations for MCP client and server
 *
 * This file demonstrates the recommended filter configurations for
 * different deployment scenarios, showing which filters to use for
 * clients vs servers and in what order.
 */

#include <memory>

#include "mcp/client/mcp_client.h"
#include "mcp/filter/backpressure_filter.h"
#include "mcp/filter/circuit_breaker_filter.h"
#include "mcp/filter/filter_event_emitter.h"
#include "mcp/filter/metrics_filter.h"
#include "mcp/filter/rate_limit_filter.h"
#include "mcp/filter/request_validation_filter.h"
#include "mcp/server/mcp_server.h"

namespace mcp {
namespace examples {

// =============================================================================
// CLIENT FILTER CONFIGURATIONS
// =============================================================================

/**
 * Production MCP Client Configuration
 *
 * Optimized for reliability and observability when calling MCP servers
 */
class ProductionMcpClient : public client::McpClient {
 public:
  void setupFilterChain(application::FilterChainBuilder& builder) override {
    // =========================================================================
    // Layer 1: CIRCUIT BREAKER (CLIENT ESSENTIAL)
    // =========================================================================
    // WHY: Clients MUST protect against server failures
    // - Prevents cascading failures by failing fast
    // - Stops hammering a broken server
    // - Automatically recovers when server is healthy
    builder.addFilter([this]() {
      filter::CircuitBreakerConfig config;
      config.failure_threshold = 5;       // Open after 5 consecutive failures
      config.error_rate_threshold = 0.5;  // Open if 50% requests fail
      config.timeout = std::chrono::seconds(30);  // Try recovery after 30s
      config.half_open_max_requests = 3;          // Test with 3 requests

      // CLIENT-SPECIFIC: More aggressive circuit breaking
      // Clients should fail fast to prevent blocking user operations
      // Chain-level callbacks are handled through the unified event hub;
      // this standalone example uses a null emitter.
      return std::make_shared<filter::CircuitBreakerFilter>(
          std::shared_ptr<filter::FilterEventEmitter>(), config);
    });

    // =========================================================================
    // Layer 2: RATE LIMITING (CLIENT OPTIONAL - Self Throttling)
    // =========================================================================
    // WHY: Good citizen behavior, prevent overwhelming servers
    // - Optional but recommended for batch operations
    // - Helps maintain good standing with API providers
    if (config_.enable_self_throttling) {
      builder.addFilter([this]() {
        filter::RateLimitConfig config;
        config.strategy = filter::RateLimitStrategy::LeakyBucket;
        config.leak_rate = 10;  // Max 10 requests per second

        // CLIENT-SPECIFIC: Self-imposed limits
        // Be conservative to avoid triggering server rate limits
        return std::make_shared<filter::RateLimitFilter>(
            std::shared_ptr<filter::FilterEventEmitter>(), config);
      });
    }

    // =========================================================================
    // Layer 3: BACKPRESSURE (BOTH CLIENT AND SERVER NEED THIS)
    // =========================================================================
    // WHY: Prevents memory exhaustion from large responses
    // - Essential for streaming data consumption
    // - Critical for resource-constrained clients
    builder.addFilter([this]() {
      filter::BackpressureConfig config;
      config.high_watermark = 512 * 1024;  // 512KB - smaller for clients
      config.low_watermark = 128 * 1024;   // 128KB

      // CLIENT-SPECIFIC: Lower limits due to client resource constraints
      // Clients often have less memory than servers
      return std::make_shared<filter::BackpressureFilter>(
          *backpressure_callbacks_, config);
    });

    // =========================================================================
    // Layer 4: METRICS (BOTH CLIENT AND SERVER NEED THIS)
    // =========================================================================
    // WHY: Monitor integration health and performance
    // - Track which server methods are slow
    // - Monitor retry rates and failures
    // - Debug connection issues
    builder.addFilter([this]() {
      filter::MetricsFilter::Config config;
      config.report_interval = std::chrono::seconds(60);  // Less frequent
      config.track_methods = true;
      config.enable_histograms = false;  // Simpler metrics for client

      // CLIENT-SPECIFIC: Focus on integration health
      // Track success rates and latencies per method
      auto filter =
          std::make_shared<filter::MetricsFilter>(*metrics_callbacks_, config);
      metrics_filter_ = filter;
      return filter->createNetworkAdapter();
    });

    // =========================================================================
    // Layer 5: REQUEST VALIDATION (CLIENT OPTIONAL - Development)
    // =========================================================================
    // WHY: Catch errors early during development
    // - Not essential in production (server will validate)
    // - Useful for debugging protocol issues
    if (is_development_) {
      builder.addFilter([this]() {
        filter::RequestValidationConfig config;
        config.validate_protocol_version = true;
        config.required_protocol_version = "2.0";

        // CLIENT-SPECIFIC: Minimal validation
        // Just check protocol version and basic format
        return std::make_shared<filter::RequestValidationFilter>(
            *validation_callbacks_, config);
      });
    }

    // Layer 6: JSON-RPC Protocol (always innermost)
    builder.addFilter(createJsonRpcFilter());
  }

 private:
  std::shared_ptr<filter::MetricsFilter> metrics_filter_;
  bool is_development_ = false;
};

/**
 * Batch Processing MCP Client Configuration
 *
 * Optimized for high-volume batch operations
 */
class BatchMcpClient : public client::McpClient {
 public:
  void setupFilterChain(application::FilterChainBuilder& builder) override {
    // CIRCUIT BREAKER - Even more important for batch operations
    builder.addFilter([this]() {
      filter::CircuitBreakerConfig config;
      config.failure_threshold = 3;               // Fail faster in batch mode
      config.error_rate_threshold = 0.3;          // Lower threshold
      config.timeout = std::chrono::seconds(60);  // Longer timeout for batch

      // BATCH-SPECIFIC: Fail entire batch quickly
      return std::make_shared<filter::CircuitBreakerFilter>(
          std::shared_ptr<filter::FilterEventEmitter>(), config);
    });

    // RATE LIMITING - ESSENTIAL for batch operations
    builder.addFilter([this]() {
      filter::RateLimitConfig config;
      config.strategy = filter::RateLimitStrategy::TokenBucket;
      config.bucket_capacity = 100;  // Burst of 100
      config.refill_rate = 5;        // Steady 5 req/sec

      // BATCH-SPECIFIC: Prevent overwhelming server with batch requests
      // NOTE: Using nullptr for event emitter - use FilterCreationContext for
      // chain events
      return std::make_shared<filter::RateLimitFilter>(nullptr, config);
    });

    // Other filters similar to production...
  }
};

// =============================================================================
// SERVER FILTER CONFIGURATIONS
// =============================================================================

/**
 * Production MCP Server Configuration
 *
 * Optimized for security, reliability, and performance under load
 */
class ProductionMcpServer : public server::McpServer {
 public:
  void setupFilterChain(application::FilterChainBuilder& builder) override {
    // =========================================================================
    // Layer 1: RATE LIMITING (SERVER ESSENTIAL)
    // =========================================================================
    // WHY: First line of defense against DOS and abuse
    // - Must be outermost to reject excess requests immediately
    // - Protects all downstream processing
    builder.addFilter([this]() {
      filter::RateLimitConfig config;
      config.strategy = filter::RateLimitStrategy::TokenBucket;
      config.bucket_capacity = 1000;      // Higher capacity for server
      config.refill_rate = 100;           // 100 requests per second
      config.per_client_limiting = true;  // Per-client limits

      // SERVER-SPECIFIC: Enforce fair usage across clients
      // Track and limit each client separately
      // NOTE: Using nullptr for event emitter - use FilterCreationContext for
      // chain events
      return std::make_shared<filter::RateLimitFilter>(nullptr, config);
    });

    // =========================================================================
    // Layer 2: CIRCUIT BREAKER (SERVER OPTIONAL - Only for downstream)
    // =========================================================================
    // WHY: Only if server calls external services (tools, databases)
    // - Not needed for pure request/response servers
    // - Protects server's own dependencies
    if (has_external_dependencies_) {
      builder.addFilter([this]() {
        filter::CircuitBreakerConfig config;
        config.failure_threshold = 10;      // Higher threshold for server
        config.error_rate_threshold = 0.7;  // More tolerant

        // SERVER-SPECIFIC: Protect downstream services
        // Only for outbound calls from server
        return std::make_shared<filter::CircuitBreakerFilter>(
            std::shared_ptr<filter::FilterEventEmitter>(), config);
      });
    }

    // =========================================================================
    // Layer 3: BACKPRESSURE (BOTH CLIENT AND SERVER NEED THIS)
    // =========================================================================
    // WHY: Prevents memory exhaustion from slow clients
    // - Essential for handling multiple concurrent clients
    // - Critical for SSE streaming responses
    builder.addFilter([this]() {
      filter::BackpressureConfig config;
      config.high_watermark = 1024 * 1024;  // 1MB - higher for server
      config.low_watermark = 256 * 1024;    // 256KB
      config.max_bytes_per_second = 10 * 1024 * 1024;  // 10MB/s limit

      // SERVER-SPECIFIC: Higher limits, per-connection tracking
      // Servers have more resources but need to handle many clients
      return std::make_shared<filter::BackpressureFilter>(
          *backpressure_callbacks_, config);
    });

    // =========================================================================
    // Layer 4: METRICS (BOTH CLIENT AND SERVER NEED THIS)
    // =========================================================================
    // WHY: Essential for monitoring service health
    // - Track per-method performance
    // - Feed to monitoring systems (Prometheus, Grafana)
    // - Capacity planning and debugging
    builder.addFilter([this]() {
      filter::MetricsFilter::Config config;
      config.report_interval = std::chrono::seconds(10);  // Frequent updates
      config.track_methods = true;
      config.enable_histograms = true;  // Full histogram data

      // SERVER-SPECIFIC: Comprehensive metrics for monitoring
      // Export to Prometheus, track SLAs
      auto filter =
          std::make_shared<filter::MetricsFilter>(*metrics_callbacks_, config);
      metrics_filter_ = filter;
      return filter->createNetworkAdapter();
    });

    // =========================================================================
    // Layer 5: REQUEST VALIDATION (SERVER ESSENTIAL)
    // =========================================================================
    // WHY: SECURITY CRITICAL - Validate all incoming requests
    // - Prevent injection attacks
    // - Enforce protocol compliance
    // - Block unauthorized methods
    builder.addFilter([this]() {
      filter::RequestValidationConfig config;
      config.validate_methods = true;
      config.allowed_methods = {
          "initialize",          "ping",           "tools/list",
          "tools/call",          "resources/list", "resources/read",
          "resources/subscribe", "prompts/list",   "prompts/get"};
      config.blocked_methods = {
          "admin/*",    // Block all admin methods
          "debug/*",    // Block all debug methods
          "internal/*"  // Block internal methods
      };
      config.max_param_size = 1024 * 1024;  // 1MB limit
      config.validate_json_depth = true;
      config.max_json_depth = 100;
      config.validate_string_length = true;
      config.max_string_length = 65536;

      // SERVER-SPECIFIC: Strict security validation
      // This is the main security boundary
      return std::make_shared<filter::RequestValidationFilter>(
          *validation_callbacks_, config);
    });

    // Layer 6: JSON-RPC Protocol (always innermost)
    builder.addFilter(createJsonRpcFilter());
  }

 private:
  std::shared_ptr<filter::MetricsFilter> metrics_filter_;
  bool has_external_dependencies_ = true;
};

/**
 * Public API MCP Server Configuration
 *
 * Optimized for untrusted clients with strict security
 */
class PublicApiMcpServer : public server::McpServer {
 public:
  void setupFilterChain(application::FilterChainBuilder& builder) override {
    // RATE LIMITING - Even stricter for public APIs
    builder.addFilter([this]() {
      filter::RateLimitConfig config;
      config.strategy = filter::RateLimitStrategy::SlidingWindow;
      config.window_size = std::chrono::minutes(1);
      config.max_requests_per_window = 60;  // 1 req/sec average
      config.per_client_limiting = true;

      // PUBLIC API: Very strict rate limits
      return std::make_shared<filter::RateLimitFilter>(
          std::shared_ptr<filter::FilterEventEmitter>(), config);
    });

    // REQUEST VALIDATION - Maximum security for public APIs
    builder.addFilter([this]() {
      filter::RequestValidationConfig config;
      config.validate_methods = true;
      config.allowed_methods = {"initialize", "ping",  // Minimal public methods
                                "resources/list", "resources/read"};
      // Block everything else by default
      config.max_param_size = 100 * 1024;  // 100KB limit for public
      config.validate_json_depth = true;
      config.max_json_depth = 10;  // Shallow JSON only

      // PUBLIC API: Maximum security validation
      return std::make_shared<filter::RequestValidationFilter>(
          *validation_callbacks_, config);
    });

    // Other filters...
  }
};

// =============================================================================
// DEVELOPMENT/TESTING CONFIGURATIONS
// =============================================================================

/**
 * Development Configuration (Minimal filters for debugging)
 */
class DevelopmentMcpSetup {
 public:
  void setupMinimalFilters(application::FilterChainBuilder& builder) {
    // Just metrics for observability and JSON-RPC for protocol
    builder.addFilter([]() {
      struct DebugMetricsCallbacks : filter::MetricsFilter::MetricsCallbacks {
        void onMetricsUpdate(const filter::ConnectionMetrics&) override {}
        void onThresholdExceeded(const std::string&,
                                 uint64_t,
                                 uint64_t) override {}
      };

      static DebugMetricsCallbacks callbacks;

      filter::MetricsFilter::Config config;
      config.report_interval =
          std::chrono::seconds(1);  // Frequent for debugging
      config.track_methods = true;

      auto filter = std::make_shared<filter::MetricsFilter>(callbacks, config);
      return filter->createNetworkAdapter();
    });

    builder.addFilter(createJsonRpcFilter());
  }
};

}  // namespace examples
}  // namespace mcp
