/**
 * @file mcp_server_enhanced_filters.cc
 * @brief Enhanced MCP server filter chain setup using extracted filter
 * components
 *
 * This demonstrates how to replace inline filter implementations with
 * the new reusable filter components following production patterns.
 */

#include "mcp/filter/backpressure_filter.h"
#include "mcp/filter/circuit_breaker_filter.h"
#include "mcp/filter/filter_chain_event_hub.h"
#include "mcp/filter/filter_event_emitter.h"
#include "mcp/filter/metrics_filter.h"
#include "mcp/filter/rate_limit_filter.h"
#include "mcp/filter/request_validation_filter.h"
#include "mcp/server/mcp_server.h"

namespace mcp {
namespace server {

/**
 * Enhanced filter chain setup for MCP server
 *
 * This replaces the inline filter implementations in setupFilterChain()
 * with the new extracted filter components, demonstrating:
 * 1. Clean separation of concerns
 * 2. Reusable filter components
 * 3. Production-ready architecture
 * 4. Proper layering and composition
 */
void McpServer::setupEnhancedFilterChain(
    application::FilterChainBuilder& builder) {
  // Base filters from ApplicationBase
  ApplicationBase::setupFilterChain(builder);

  if (!enhanced_filter_event_hub_) {
    enhanced_filter_event_hub_ = std::make_shared<filter::FilterChainEventHub>();
  }
  auto event_hub = enhanced_filter_event_hub_;

  // Layer 1: Circuit Breaker (outermost - fail fast)
  // Prevents cascading failures by blocking requests when error rates are high
  if (config_.enable_circuit_breaker) {
    builder.addFilter([this, event_hub]() -> network::FilterSharedPtr {
      filter::CircuitBreakerConfig cb_config;
      cb_config.failure_threshold = config_.circuit_breaker_failure_threshold;
      cb_config.error_rate_threshold = config_.circuit_breaker_error_rate;
      cb_config.timeout =
          std::chrono::seconds(config_.circuit_breaker_timeout_seconds);
      cb_config.half_open_max_requests = 3;

      auto emitter = std::make_shared<filter::FilterEventEmitter>(
          event_hub,
          "circuit_breaker");

      return std::make_shared<filter::CircuitBreakerFilter>(emitter, cb_config);
    });
  }

  // Layer 2: Rate Limiting
  // Controls request rates to prevent abuse
  if (config_.enable_rate_limiting) {
    builder.addFilter([this]() -> network::FilterSharedPtr {
      // Create rate limit callbacks adapter
      class RateLimitCallbacksImpl : public filter::RateLimitFilter::Callbacks {
       public:
        RateLimitCallbacksImpl(McpServer& server) : server_(server) {}

        void onRequestAllowed() override {
          // Request allowed - no action needed
        }

        void onRequestLimited(std::chrono::milliseconds retry_after) override {
          server_.server_stats_.rate_limited_requests++;
          std::cerr << "[RATE_LIMIT] Request limited, retry after: "
                    << retry_after.count() << "ms" << std::endl;
        }

        void onRateLimitWarning(int remaining) override {
          std::cerr << "[RATE_LIMIT] Warning: " << remaining
                    << "% capacity remaining" << std::endl;
        }

       private:
        McpServer& server_;
      };

      // Configure rate limiting
      filter::RateLimitConfig rl_config;
      rl_config.strategy = filter::RateLimitStrategy::TokenBucket;
      rl_config.bucket_capacity =
          config_.rate_limit_requests_per_second * 2;  // Allow burst
      rl_config.refill_rate = config_.rate_limit_requests_per_second;
      rl_config.allow_burst = true;
      rl_config.burst_size = config_.rate_limit_burst_size;

      auto callbacks = std::make_shared<RateLimitCallbacksImpl>(*this);
      auto filter =
          std::make_shared<filter::RateLimitFilter>(*callbacks, rl_config);

      // Store callbacks
      rate_limit_callbacks_ = callbacks;

      return filter;
    });
  }

  // Layer 3: Backpressure
  // Flow control to prevent buffer overflow
  builder.addFilter([this]() -> network::FilterSharedPtr {
    // Create backpressure callbacks adapter
    class BackpressureCallbacksImpl
        : public filter::BackpressureFilter::Callbacks {
     public:
      BackpressureCallbacksImpl(McpServer& server) : server_(server) {}

      void onBackpressureApplied() override {
        server_.server_stats_.backpressure_events++;
        std::cerr << "[BACKPRESSURE] Applied - pausing read" << std::endl;
      }

      void onBackpressureReleased() override {
        std::cerr << "[BACKPRESSURE] Released - resuming read" << std::endl;
      }

      void onDataDropped(size_t bytes) override {
        server_.server_stats_.bytes_dropped += bytes;
        std::cerr << "[BACKPRESSURE] Dropped " << bytes << " bytes"
                  << std::endl;
      }

     private:
      McpServer& server_;
    };

    // Configure backpressure
    filter::BackpressureConfig bp_config;
    bp_config.high_watermark = config_.buffer_high_watermark;
    bp_config.low_watermark = config_.buffer_low_watermark;
    bp_config.max_bytes_per_second = config_.max_bytes_per_second;

    auto callbacks = std::make_shared<BackpressureCallbacksImpl>(*this);
    auto filter =
        std::make_shared<filter::BackpressureFilter>(*callbacks, bp_config);

    backpressure_callbacks_ = callbacks;

    return filter;
  });

  // Layer 4: Metrics Collection
  // Detailed performance monitoring
  builder.addFilter([this]() -> network::FilterSharedPtr {
    // Create metrics callbacks adapter
    class MetricsCallbacksImpl
        : public filter::MetricsFilter::MetricsCallbacks {
     public:
      MetricsCallbacksImpl(McpServer& server) : server_(server) {}

      void onMetricsUpdate(const filter::ConnectionMetrics& metrics) override {
        // Update server stats with latest metrics
        server_.server_stats_.bytes_received = metrics.bytes_received;
        server_.server_stats_.bytes_sent = metrics.bytes_sent;
        server_.server_stats_.requests_total = metrics.requests_received;
        server_.server_stats_.notifications_total =
            metrics.notifications_received;

        // Calculate average latency
        if (metrics.latency_samples > 0) {
          server_.server_stats_.average_latency_ms =
              metrics.total_latency_ms / metrics.latency_samples;
        }
      }

      void onThresholdExceeded(const std::string& metric_name,
                               uint64_t value,
                               uint64_t threshold) override {
        std::cerr << "[METRICS] Threshold exceeded: " << metric_name
                  << " value=" << value << " threshold=" << threshold
                  << std::endl;
        server_.server_stats_.threshold_violations++;
      }

     private:
      McpServer& server_;
    };

    // Configure metrics
    filter::MetricsFilter::Config metrics_config;
    metrics_config.report_interval =
        std::chrono::seconds(config_.metrics_report_interval_seconds);
    metrics_config.max_latency_threshold_ms = config_.max_latency_threshold_ms;
    metrics_config.track_methods = true;
    metrics_config.enable_histograms = config_.enable_latency_histograms;

    auto callbacks = std::make_shared<MetricsCallbacksImpl>(*this);
    auto filter =
        std::make_shared<filter::MetricsFilter>(*callbacks, metrics_config);
    auto adapter = filter->createNetworkAdapter();

    metrics_callbacks_ = callbacks;
    metrics_filter_ = filter;

    return adapter;
  });

  // Layer 5: Request Validation
  // Protocol compliance and security checks
  if (config_.enable_request_validation) {
    builder.addFilter([this]() -> network::FilterSharedPtr {
      // Create validation callbacks adapter
      class ValidationCallbacksImpl
          : public filter::RequestValidationFilter::ValidationCallbacks {
       public:
        ValidationCallbacksImpl(McpServer& server) : server_(server) {}

        void onRequestValidated(const std::string& method) override {
          // Request passed validation
        }

        void onRequestRejected(const std::string& method,
                               const std::string& reason) override {
          server_.server_stats_.requests_invalid++;
          std::cerr << "[VALIDATION] Request rejected: " << method
                    << " Reason: " << reason << std::endl;
        }

        void onRateLimitExceeded(const std::string& method) override {
          server_.server_stats_.rate_limited_requests++;
          std::cerr << "[VALIDATION] Method rate limit exceeded: " << method
                    << std::endl;
        }

       private:
        McpServer& server_;
      };

      // Configure validation
      filter::RequestValidationConfig val_config;
      val_config.validate_methods = true;
      val_config.allowed_methods = config_.allowed_methods;
      val_config.blocked_methods = config_.blocked_methods;
      val_config.validate_params = true;
      val_config.max_param_size = config_.max_request_size;
      val_config.validate_protocol_version = true;
      val_config.required_protocol_version = config_.protocol_version;

      auto callbacks = std::make_shared<ValidationCallbacksImpl>(*this);
      auto filter = std::make_shared<filter::RequestValidationFilter>(
          *callbacks, val_config);

      validation_callbacks_ = callbacks;

      return filter;
    });
  }

  // Layer 6: JSON-RPC Protocol Processing (innermost)
  // This is where the actual protocol processing happens
  bool use_framing = true;  // Servers typically use framing
  auto filter_bundle = createJsonRpcFilter(*this, true, use_framing);

  // Add the JSON-RPC filter
  builder.addFilterInstance(filter_bundle->filter);

  // Keep the bundle alive
  builder.addFilter([filter_bundle]() -> network::FilterSharedPtr {
    return nullptr;  // Just keeps filter_bundle alive
  });

  if (event_hub) {
    // Reset any previous observer so we don't leak handles
    enhanced_filter_event_callbacks_.reset();
    enhanced_filter_event_handle_ = filter::FilterChainEventHub::ObserverHandle();

    class LoggingCallbacks : public filter::FilterChainCallbacks {
     public:
      LoggingCallbacks() = default;

      void onFilterEvent(const filter::FilterEvent& event) override {
        if (event.filter_name != "circuit_breaker") {
          return;
        }

        std::cerr << "[CIRCUIT_BREAKER] Event: "
                  << filter::toString(event.event_type) << std::endl;
      }
    };

    enhanced_filter_event_callbacks_ = std::make_shared<LoggingCallbacks>();
    enhanced_filter_event_handle_ =
        event_hub->registerObserver(enhanced_filter_event_callbacks_);
  }

  // The filter chain is now:
  // Network → Circuit Breaker → Rate Limit → Backpressure →
  // Metrics → Validation → JSON-RPC → Application
  //
  // This follows production patterns where:
  // 1. Fail-fast filters are outermost (circuit breaker)
  // 2. Flow control is applied early (rate limit, backpressure)
  // 3. Observability is comprehensive (metrics)
  // 4. Security validation happens before protocol processing
  // 5. Protocol processing is innermost, closest to application
}

/**
 * Enhanced server configuration with all filter settings
 */
struct EnhancedServerConfig : public McpServerConfig {
  // Circuit breaker settings
  bool enable_circuit_breaker = true;
  size_t circuit_breaker_failure_threshold = 5;
  double circuit_breaker_error_rate = 0.5;
  uint32_t circuit_breaker_timeout_seconds = 30;

  // Rate limiting settings
  size_t rate_limit_requests_per_second = 100;
  size_t rate_limit_burst_size = 20;

  // Backpressure settings (already in base config)
  // Using buffer_high_watermark and buffer_low_watermark
  size_t max_bytes_per_second = 0;  // 0 = unlimited

  // Metrics settings
  uint32_t metrics_report_interval_seconds = 10;
  uint64_t max_latency_threshold_ms = 5000;
  bool enable_latency_histograms = false;

  // Validation settings (partially in base config)
  std::set<std::string> allowed_methods;
  std::set<std::string> blocked_methods;
  size_t max_request_size = 1024 * 1024;  // 1MB

  // Additional stats for enhanced filters
  std::atomic<uint64_t> circuit_breaker_trips{0};
  std::atomic<uint64_t> requests_blocked{0};
  std::atomic<uint64_t> rate_limited_requests{0};
  std::atomic<uint64_t> backpressure_events{0};
  std::atomic<uint64_t> bytes_dropped{0};
  std::atomic<uint64_t> threshold_violations{0};
  std::atomic<double> current_success_rate{1.0};
  std::atomic<uint64_t> average_latency_ms{0};
};

/**
 * Example of creating an enhanced MCP server with all filters
 */
std::unique_ptr<McpServer> createEnhancedMcpServer() {
  EnhancedServerConfig config;

  // Configure server basics
  config.server_name = "Enhanced MCP Server";
  config.server_version = "1.0.0";
  config.protocol_version = "2.0";

  // Configure circuit breaker
  config.enable_circuit_breaker = true;
  config.circuit_breaker_failure_threshold = 5;
  config.circuit_breaker_error_rate = 0.5;
  config.circuit_breaker_timeout_seconds = 30;

  // Configure rate limiting
  config.enable_rate_limiting = true;
  config.rate_limit_requests_per_second = 100;
  config.rate_limit_burst_size = 20;

  // Configure backpressure
  config.buffer_high_watermark = 1024 * 1024;      // 1MB
  config.buffer_low_watermark = 256 * 1024;        // 256KB
  config.max_bytes_per_second = 10 * 1024 * 1024;  // 10MB/s

  // Configure metrics
  config.metrics_report_interval_seconds = 10;
  config.max_latency_threshold_ms = 5000;
  config.enable_latency_histograms = true;

  // Configure validation
  config.enable_request_validation = true;
  config.allowed_methods = {
      "initialize",          "ping",           "tools/list",
      "tools/call",          "resources/list", "resources/read",
      "resources/subscribe", "prompts/list",   "prompts/get"};
  config.blocked_methods = {
      "admin/*",  // Block all admin methods
      "debug/*"   // Block all debug methods
  };
  config.max_request_size = 1024 * 1024;  // 1MB max request

  // Create server with enhanced configuration
  auto server = std::make_unique<McpServer>(config);

  // The server now has a comprehensive filter chain:
  // 1. Circuit breaker for failure protection
  // 2. Rate limiting for abuse prevention
  // 3. Backpressure for flow control
  // 4. Metrics for observability
  // 5. Validation for security
  // 6. JSON-RPC for protocol processing

  return server;
}

}  // namespace server
}  // namespace mcp
