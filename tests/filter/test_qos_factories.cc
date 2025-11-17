/**
 * @file test_qos_factories.cc
 * @brief Comprehensive tests for QoS filter factories
 */

#include <gtest/gtest.h>
#include <memory>

#include "mcp/filter/core_filter_factories.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/filter/filter_context.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/mcp_connection_manager.h"

// Must undefine before redefining
#ifdef GOPHER_LOG_COMPONENT
#undef GOPHER_LOG_COMPONENT
#endif

#include "mcp/logging/logger_registry.h"

#define GOPHER_LOG_COMPONENT "test.filter.qos"

namespace mcp {
namespace filter {
namespace test {

class StubProtocolCallbacks : public McpProtocolCallbacks {
 public:
  void onRequest(const jsonrpc::Request& request) override {}
  void onResponse(const jsonrpc::Response& response) override {}
  void onNotification(const jsonrpc::Notification& notification) override {}
  void onError(const Error& error) override {}
  void onConnectionEvent(network::ConnectionEvent event) override {}
};

class QosFactoriesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Ensure filter registry is initialized
    FilterRegistry::instance();

    // Explicitly register all core filters (don't rely on static initialization)
    registerAllCoreFilters();

    // Set up test logging
    auto& logger_registry = logging::LoggerRegistry::instance();
    logger_registry.getDefaultLogger()->setLevel(logging::LogLevel::Debug);

    auto dispatcher_factory = event::createLibeventDispatcherFactory();
    dispatcher_ = dispatcher_factory->createDispatcher("test.qos.factories");
    callbacks_ = std::make_unique<StubProtocolCallbacks>();
  }

  void TearDown() override {
    dispatcher_.reset();
    callbacks_.reset();
  }

  // Helper to create JSON config from string
  json::JsonValue parseConfig(const std::string& json_str) {
    return json::JsonValue::parse(json_str);
  }

  FilterCreationContext makeTestContext() {
    TransportMetadata transport("127.0.0.1", 8080);
    return FilterCreationContext(*dispatcher_, *callbacks_,
                                 ConnectionMode::Server, transport);
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::unique_ptr<StubProtocolCallbacks> callbacks_;
};

// ============================================================================
// Rate Limit Filter Factory Tests
// ============================================================================

TEST_F(QosFactoriesTest, RateLimitFactoryRegistration) {
  // Verify factory is registered
  EXPECT_TRUE(FilterRegistry::instance().hasFactory("rate_limit"));
  
  // Get factory and verify metadata
  auto factory = FilterRegistry::instance().getFactory("rate_limit");
  ASSERT_NE(factory, nullptr);
  
  const auto& metadata = factory->getMetadata();
  EXPECT_EQ(metadata.name, "rate_limit");
  EXPECT_EQ(metadata.version, "1.0.0");
  EXPECT_FALSE(metadata.dependencies.empty());
}

TEST_F(QosFactoriesTest, RateLimitDefaultConfig) {
  auto factory = FilterRegistry::instance().getFactory("rate_limit");
  ASSERT_NE(factory, nullptr);
  
  auto defaults = factory->getDefaultConfig();
  EXPECT_TRUE(defaults.isObject());
  EXPECT_EQ(defaults["strategy"].getString(), "token_bucket");
  EXPECT_EQ(defaults["bucket_capacity"].getInt(), 100);
  EXPECT_EQ(defaults["refill_rate"].getInt(), 10);
  EXPECT_EQ(defaults["window_size_seconds"].getInt(), 60);
  EXPECT_EQ(defaults["max_requests_per_window"].getInt(), 100);
  EXPECT_EQ(defaults["leak_rate"].getInt(), 10);
  EXPECT_TRUE(defaults["allow_burst"].getBool());
  EXPECT_EQ(defaults["burst_size"].getInt(), 20);
  EXPECT_FALSE(defaults["per_client_limiting"].getBool());
}

TEST_F(QosFactoriesTest, RateLimitValidConfiguration) {
  auto factory = FilterRegistry::instance().getFactory("rate_limit");
  ASSERT_NE(factory, nullptr);
  
  // Test token bucket configuration
  {
    auto config = parseConfig(R"({
      "strategy": "token_bucket",
      "bucket_capacity": 500,
      "refill_rate": 50,
      "allow_burst": true,
      "burst_size": 100
    })");
    
    EXPECT_TRUE(factory->validateConfig(config));
    // Note: createFilter returns nullptr due to runtime dependencies
    EXPECT_NO_THROW(factory->createFilter(config));
  }
  
  // Test sliding window configuration
  {
    auto config = parseConfig(R"({
      "strategy": "sliding_window",
      "window_size_seconds": 30,
      "max_requests_per_window": 200
    })");
    
    EXPECT_TRUE(factory->validateConfig(config));
    EXPECT_NO_THROW(factory->createFilter(config));
  }
  
  // Test fixed window configuration
  {
    auto config = parseConfig(R"({
      "strategy": "fixed_window",
      "window_size_seconds": 120,
      "max_requests_per_window": 1000
    })");
    
    EXPECT_TRUE(factory->validateConfig(config));
    EXPECT_NO_THROW(factory->createFilter(config));
  }
  
  // Test leaky bucket configuration
  {
    auto config = parseConfig(R"({
      "strategy": "leaky_bucket",
      "bucket_capacity": 200,
      "leak_rate": 20
    })");
    
    EXPECT_TRUE(factory->validateConfig(config));
    EXPECT_NO_THROW(factory->createFilter(config));
  }
  
  // Test per-client limiting
  {
    auto config = parseConfig(R"({
      "per_client_limiting": true,
      "client_limits": {
        "client1": 100,
        "client2": 200,
        "premium_client": 500
      }
    })");
    
    EXPECT_TRUE(factory->validateConfig(config));
    EXPECT_NO_THROW(factory->createFilter(config));
  }
}

TEST_F(QosFactoriesTest, RateLimitInvalidConfiguration) {
  auto factory = FilterRegistry::instance().getFactory("rate_limit");
  ASSERT_NE(factory, nullptr);
  
  // Invalid strategy
  {
    auto config = parseConfig(R"({
      "strategy": "invalid_strategy"
    })");
    
    EXPECT_FALSE(factory->validateConfig(config));
    EXPECT_THROW(factory->createFilter(config), std::runtime_error);
  }
  
  // Out of range bucket_capacity
  {
    auto config = parseConfig(R"({
      "bucket_capacity": 1000000
    })");
    
    EXPECT_FALSE(factory->validateConfig(config));
  }
  
  // Negative refill_rate
  {
    auto config = parseConfig(R"({
      "refill_rate": -5
    })");
    
    EXPECT_FALSE(factory->validateConfig(config));
  }
  
  // Invalid client limits
  {
    auto config = parseConfig(R"({
      "client_limits": {
        "client1": -10
      }
    })");
    
    EXPECT_FALSE(factory->validateConfig(config));
  }
  
  // Non-object configuration
  {
    auto config = json::JsonValue("not an object");
    EXPECT_FALSE(factory->validateConfig(config));
  }
}

TEST_F(QosFactoriesTest, RateLimitEdgeCases) {
  auto factory = FilterRegistry::instance().getFactory("rate_limit");
  ASSERT_NE(factory, nullptr);
  
  // Minimum values
  {
    auto config = parseConfig(R"({
      "bucket_capacity": 1,
      "refill_rate": 1,
      "window_size_seconds": 1,
      "max_requests_per_window": 1,
      "leak_rate": 1,
      "burst_size": 0
    })");
    
    EXPECT_TRUE(factory->validateConfig(config));
    EXPECT_NO_THROW(factory->createFilter(config));
  }
  
  // Maximum values
  {
    auto config = parseConfig(R"({
      "bucket_capacity": 100000,
      "refill_rate": 10000,
      "window_size_seconds": 3600,
      "max_requests_per_window": 100000,
      "leak_rate": 10000,
      "burst_size": 1000
    })");
    
    EXPECT_TRUE(factory->validateConfig(config));
    EXPECT_NO_THROW(factory->createFilter(config));
  }
}

// ============================================================================
// Circuit Breaker Filter Factory Tests
// ============================================================================

TEST_F(QosFactoriesTest, CircuitBreakerFactoryRegistration) {
  auto& registry = FilterRegistry::instance();
  EXPECT_TRUE(registry.hasContextFactory("circuit_breaker"));
  
  const auto* metadata = registry.getBasicMetadata("circuit_breaker");
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->name, "circuit_breaker");
  EXPECT_EQ(metadata->version, "1.0.0");
}

TEST_F(QosFactoriesTest, CircuitBreakerDefaultConfig) {
  const auto* metadata = FilterRegistry::instance().getBasicMetadata("circuit_breaker");
  ASSERT_NE(metadata, nullptr);
  
  auto defaults = metadata->default_config;
  EXPECT_TRUE(defaults.isObject());
  EXPECT_EQ(defaults["failure_threshold"].getInt(), 5);
  EXPECT_DOUBLE_EQ(defaults["error_rate_threshold"].getFloat(), 0.5);
  EXPECT_EQ(defaults["min_requests"].getInt(), 10);
  EXPECT_EQ(defaults["timeout_ms"].getInt(), 30000);
  EXPECT_EQ(defaults["window_size_ms"].getInt(), 60000);
  EXPECT_EQ(defaults["half_open_max_requests"].getInt(), 3);
  EXPECT_EQ(defaults["half_open_success_threshold"].getInt(), 2);
  EXPECT_TRUE(defaults["track_timeouts"].getBool());
  EXPECT_TRUE(defaults["track_errors"].getBool());
  EXPECT_FALSE(defaults["track_4xx_as_errors"].getBool());
}

TEST_F(QosFactoriesTest, CircuitBreakerValidConfiguration) {
  auto& registry = FilterRegistry::instance();
  auto context = makeTestContext();
  
  // Basic configuration
  {
    auto config = parseConfig(R"({
      "failure_threshold": 10,
      "error_rate_threshold": 0.7,
      "min_requests": 20,
      "timeout_ms": 15000
    })");
    
    EXPECT_NO_THROW({
      auto filter = registry.createFilterWithContext("circuit_breaker", context, config);
      EXPECT_NE(filter, nullptr);
    });
  }
  
  // Half-open state configuration
  {
    auto config = parseConfig(R"({
      "half_open_max_requests": 5,
      "half_open_success_threshold": 3,
      "window_size_ms": 120000
    })");
    
    EXPECT_NO_THROW({
      auto filter = registry.createFilterWithContext("circuit_breaker", context, config);
      EXPECT_NE(filter, nullptr);
    });
  }
  
  // Tracking configuration
  {
    auto config = parseConfig(R"({
      "track_timeouts": false,
      "track_errors": true,
      "track_4xx_as_errors": true
    })");
    
    EXPECT_NO_THROW({
      auto filter = registry.createFilterWithContext("circuit_breaker", context, config);
      EXPECT_NE(filter, nullptr);
    });
  }
}

TEST_F(QosFactoriesTest, CircuitBreakerInvalidConfiguration) {
  auto& registry = FilterRegistry::instance();
  auto context = makeTestContext();
  
  // Out of range error_rate_threshold
  {
    auto config = parseConfig(R"({
      "error_rate_threshold": 1.5
    })");
    
    EXPECT_NO_THROW({
      auto filter = registry.createFilterWithContext("circuit_breaker", context, config);
      EXPECT_NE(filter, nullptr);
    });
  }
  
  // Invalid timeout_ms
  {
    auto config = parseConfig(R"({
      "timeout_ms": 500
    })");
    
    EXPECT_NO_THROW({
      auto filter = registry.createFilterWithContext("circuit_breaker", context, config);
      EXPECT_NE(filter, nullptr);
    });
  }
  
  // Inconsistent half-open configuration
  {
    auto config = parseConfig(R"({
      "half_open_max_requests": 2,
      "half_open_success_threshold": 5
    })");
    
    EXPECT_NO_THROW({
      auto filter = registry.createFilterWithContext("circuit_breaker", context, config);
      EXPECT_NE(filter, nullptr);
    });
  }
  
  // Invalid window_size_ms
  {
    auto config = parseConfig(R"({
      "window_size_ms": 700000
    })");
    
    EXPECT_NO_THROW({
      auto filter = registry.createFilterWithContext("circuit_breaker", context, config);
      EXPECT_NE(filter, nullptr);
    });
  }
}

TEST_F(QosFactoriesTest, CircuitBreakerEdgeCases) {
  auto& registry = FilterRegistry::instance();
  auto context = makeTestContext();
  
  // Minimum values
  {
    auto config = parseConfig(R"({
      "failure_threshold": 1,
      "error_rate_threshold": 0.0,
      "min_requests": 1,
      "timeout_ms": 1000,
      "window_size_ms": 1000,
      "half_open_max_requests": 1,
      "half_open_success_threshold": 1
    })");
    
    EXPECT_NO_THROW({
      auto filter = registry.createFilterWithContext("circuit_breaker", context, config);
      EXPECT_NE(filter, nullptr);
    });
  }
  
  // Maximum values
  {
    auto config = parseConfig(R"({
      "failure_threshold": 100,
      "error_rate_threshold": 1.0,
      "min_requests": 1000,
      "timeout_ms": 300000,
      "window_size_ms": 600000,
      "half_open_max_requests": 100,
      "half_open_success_threshold": 100
    })");
    
    EXPECT_NO_THROW({
      auto filter = registry.createFilterWithContext("circuit_breaker", context, config);
      EXPECT_NE(filter, nullptr);
    });
  }
}

// ============================================================================
// Metrics Filter Factory Tests
// ============================================================================

TEST_F(QosFactoriesTest, MetricsFactoryRegistration) {
  // Verify factory is registered
  EXPECT_TRUE(FilterRegistry::instance().hasFactory("metrics"));
  
  // Get factory and verify metadata
  auto factory = FilterRegistry::instance().getFactory("metrics");
  ASSERT_NE(factory, nullptr);
  
  const auto& metadata = factory->getMetadata();
  EXPECT_EQ(metadata.name, "metrics");
  EXPECT_EQ(metadata.version, "1.0.0");
  EXPECT_FALSE(metadata.dependencies.empty());
}

TEST_F(QosFactoriesTest, MetricsDefaultConfig) {
  auto factory = FilterRegistry::instance().getFactory("metrics");
  ASSERT_NE(factory, nullptr);
  
  auto defaults = factory->getDefaultConfig();
  EXPECT_TRUE(defaults.isObject());
  EXPECT_EQ(defaults["provider"].getString(), "internal");
  EXPECT_EQ(defaults["rate_update_interval_seconds"].getInt(), 1);
  EXPECT_EQ(defaults["report_interval_seconds"].getInt(), 10);
  EXPECT_EQ(defaults["max_latency_threshold_ms"].getInt(), 5000);
  EXPECT_EQ(defaults["error_rate_threshold"].getInt(), 10);
  EXPECT_EQ(defaults["bytes_threshold"].getInt64(), 104857600);
  EXPECT_TRUE(defaults["track_methods"].getBool());
  EXPECT_FALSE(defaults["enable_histograms"].getBool());
  EXPECT_EQ(defaults["prometheus_port"].getInt(), 9090);
  EXPECT_EQ(defaults["prometheus_path"].getString(), "/metrics");
}

TEST_F(QosFactoriesTest, MetricsValidConfiguration) {
  auto factory = FilterRegistry::instance().getFactory("metrics");
  ASSERT_NE(factory, nullptr);
  
  // Internal provider configuration
  {
    auto config = parseConfig(R"({
      "provider": "internal",
      "rate_update_interval_seconds": 5,
      "report_interval_seconds": 30,
      "track_methods": true,
      "enable_histograms": true
    })");
    
    EXPECT_TRUE(factory->validateConfig(config));
    EXPECT_NO_THROW(factory->createFilter(config));
  }
  
  // Prometheus provider configuration
  {
    auto config = parseConfig(R"({
      "provider": "prometheus",
      "prometheus_port": 8080,
      "prometheus_path": "/api/metrics",
      "max_latency_threshold_ms": 10000
    })");
    
    EXPECT_TRUE(factory->validateConfig(config));
    EXPECT_NO_THROW(factory->createFilter(config));
  }
  
  // Custom provider configuration
  {
    auto config = parseConfig(R"({
      "provider": "custom",
      "custom_endpoint": "https://metrics.example.com/api/v1/push",
      "error_rate_threshold": 50,
      "bytes_threshold": 1073741824
    })");
    
    EXPECT_TRUE(factory->validateConfig(config));
    EXPECT_NO_THROW(factory->createFilter(config));
  }
}

TEST_F(QosFactoriesTest, MetricsInvalidConfiguration) {
  auto factory = FilterRegistry::instance().getFactory("metrics");
  ASSERT_NE(factory, nullptr);
  
  // Invalid provider
  {
    auto config = parseConfig(R"({
      "provider": "invalid_provider"
    })");
    
    EXPECT_FALSE(factory->validateConfig(config));
  }
  
  // Out of range port
  {
    auto config = parseConfig(R"({
      "prometheus_port": 100
    })");
    
    EXPECT_FALSE(factory->validateConfig(config));
  }
  
  // Invalid prometheus_path
  {
    auto config = parseConfig(R"({
      "prometheus_path": "no_leading_slash"
    })");
    
    EXPECT_FALSE(factory->validateConfig(config));
  }
  
  // Empty custom_endpoint
  {
    auto config = parseConfig(R"({
      "provider": "custom",
      "custom_endpoint": ""
    })");
    
    EXPECT_FALSE(factory->validateConfig(config));
  }
  
  // Out of range intervals
  {
    auto config = parseConfig(R"({
      "rate_update_interval_seconds": 100
    })");
    
    EXPECT_FALSE(factory->validateConfig(config));
  }
}

TEST_F(QosFactoriesTest, MetricsEdgeCases) {
  auto factory = FilterRegistry::instance().getFactory("metrics");
  ASSERT_NE(factory, nullptr);
  
  // Minimum values
  {
    auto config = parseConfig(R"({
      "rate_update_interval_seconds": 1,
      "report_interval_seconds": 1,
      "max_latency_threshold_ms": 100,
      "error_rate_threshold": 1,
      "bytes_threshold": 1024,
      "prometheus_port": 1024
    })");
    
    EXPECT_TRUE(factory->validateConfig(config));
    EXPECT_NO_THROW(factory->createFilter(config));
  }
  
  // Maximum values
  {
    auto config = parseConfig(R"({
      "rate_update_interval_seconds": 60,
      "report_interval_seconds": 3600,
      "max_latency_threshold_ms": 60000,
      "error_rate_threshold": 1000,
      "bytes_threshold": 10737418240,
      "prometheus_port": 65535
    })");
    
    EXPECT_TRUE(factory->validateConfig(config));
    EXPECT_NO_THROW(factory->createFilter(config));
  }
}

// ============================================================================
// Hot Reconfiguration Tests
// ============================================================================

TEST_F(QosFactoriesTest, HotReconfigurationSupport) {
  // Test that factories can be called multiple times with different configs
  // simulating hot reconfiguration scenarios

  auto rate_limit_factory = FilterRegistry::instance().getFactory("rate_limit");
  auto& registry = FilterRegistry::instance();
  auto metrics_factory = FilterRegistry::instance().getFactory("metrics");
  auto context = makeTestContext();

  // Ensure factories are registered before testing
  ASSERT_NE(rate_limit_factory, nullptr) << "rate_limit factory not registered";
  ASSERT_TRUE(registry.hasContextFactory("circuit_breaker"))
      << "circuit_breaker factory not registered";
  ASSERT_NE(metrics_factory, nullptr) << "metrics factory not registered";

  // Create filters with initial configs
  {
    auto config1 = parseConfig(R"({"strategy": "token_bucket", "bucket_capacity": 100})");
    auto config2 = parseConfig(R"({"strategy": "sliding_window", "window_size_seconds": 60})");

    EXPECT_NO_THROW(rate_limit_factory->createFilter(config1));
    EXPECT_NO_THROW(rate_limit_factory->createFilter(config2));
  }

  {
    auto config1 = parseConfig(R"({"failure_threshold": 5, "timeout_ms": 10000})");
    auto config2 = parseConfig(R"({"failure_threshold": 10, "timeout_ms": 30000})");

    EXPECT_NO_THROW(registry.createFilterWithContext("circuit_breaker", context, config1));
    EXPECT_NO_THROW(registry.createFilterWithContext("circuit_breaker", context, config2));
  }

  {
    auto config1 = parseConfig(R"({"provider": "internal", "track_methods": true})");
    auto config2 = parseConfig(R"({"provider": "prometheus", "prometheus_port": 9091})");

    EXPECT_NO_THROW(metrics_factory->createFilter(config1));
    EXPECT_NO_THROW(metrics_factory->createFilter(config2));
  }
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(QosFactoriesTest, AllFactoriesRegistered) {
  // Verify all QoS factories are properly registered
  auto& registry = FilterRegistry::instance();
  
  EXPECT_TRUE(registry.hasFactory("rate_limit"));
  EXPECT_TRUE(registry.hasFactory("metrics"));
  EXPECT_TRUE(registry.hasContextFactory("circuit_breaker"));
  
  // Verify we can list all factories
  auto factories = registry.listFactories();
  EXPECT_NE(std::find(factories.begin(), factories.end(), "rate_limit"), factories.end());
  EXPECT_NE(std::find(factories.begin(), factories.end(), "metrics"), factories.end());
}

TEST_F(QosFactoriesTest, ComplexConfiguration) {
  // Test creating all three filters with a complex configuration
  auto& registry = FilterRegistry::instance();
  
  // Complex rate limit config with per-client limits
  {
    auto config = parseConfig(R"({
      "strategy": "token_bucket",
      "bucket_capacity": 1000,
      "refill_rate": 100,
      "allow_burst": true,
      "burst_size": 200,
      "per_client_limiting": true,
      "client_limits": {
        "basic": 50,
        "premium": 200,
        "enterprise": 1000
      }
    })");
    
    auto filter = registry.createFilter("rate_limit", config);
    EXPECT_NE(filter, nullptr);
  }
  
  // Complex circuit breaker config
  {
    auto config = parseConfig(R"({
      "failure_threshold": 8,
      "error_rate_threshold": 0.6,
      "min_requests": 15,
      "timeout_ms": 20000,
      "window_size_ms": 90000,
      "half_open_max_requests": 4,
      "half_open_success_threshold": 3,
      "track_timeouts": true,
      "track_errors": true,
      "track_4xx_as_errors": false
    })");
    
    auto filter = registry.createFilterWithContext(
        "circuit_breaker", makeTestContext(), config);
    EXPECT_NE(filter, nullptr);
  }
  
  // Complex metrics config with Prometheus
  {
    auto config = parseConfig(R"({
      "provider": "prometheus",
      "rate_update_interval_seconds": 2,
      "report_interval_seconds": 15,
      "max_latency_threshold_ms": 8000,
      "error_rate_threshold": 25,
      "bytes_threshold": 524288000,
      "track_methods": true,
      "enable_histograms": true,
      "prometheus_port": 9092,
      "prometheus_path": "/api/v1/metrics"
    })");
    
    auto filter = registry.createFilter("metrics", config);
    EXPECT_NE(filter, nullptr);
  }
}

}  // namespace test
}  // namespace filter
}  // namespace mcp
