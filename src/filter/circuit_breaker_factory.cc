/**
 * @file circuit_breaker_factory.cc
 * @brief Factory implementation for circuit breaker filter
 */

#include "mcp/filter/filter_registry.h"
#include "mcp/filter/circuit_breaker_filter.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"

#define GOPHER_LOG_COMPONENT "filter.factory.qos"

namespace mcp {
namespace filter {

/**
 * Factory for creating CircuitBreakerFilter instances
 * 
 * Configuration schema:
 * {
 *   "failure_threshold": number,          // Consecutive failures to open (default: 5)
 *   "error_rate_threshold": number,       // Error rate to open (0.0-1.0, default: 0.5)
 *   "min_requests": number,               // Min requests before checking error rate (default: 10)
 *   "timeout_ms": number,                 // Time before trying half-open (default: 30000)
 *   "window_size_ms": number,             // Sliding window for metrics (default: 60000)
 *   "half_open_max_requests": number,     // Max requests in half-open (default: 3)
 *   "half_open_success_threshold": number, // Successes to close circuit (default: 2)
 *   "track_timeouts": boolean,            // Track timeouts as failures (default: true)
 *   "track_errors": boolean,              // Track errors as failures (default: true)
 *   "track_4xx_as_errors": boolean        // Count client errors as failures (default: false)
 * }
 */
class CircuitBreakerFilterFactory : public FilterFactory {
 public:
  CircuitBreakerFilterFactory() {
    // Initialize metadata
    metadata_.name = "circuit_breaker";
    metadata_.version = "1.0.0";
    metadata_.description = "Circuit breaker filter for cascading failure protection";
    metadata_.dependencies = {"network", "json_rpc_protocol"};
    
    // Define configuration schema
    metadata_.config_schema = json::JsonObjectBuilder()
        .add("type", "object")
        .add("properties", json::JsonObjectBuilder()
            .add("failure_threshold", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1)
                .add("maximum", 100)
                .add("default", 5)
                .add("description", "Consecutive failures to open circuit")
                .build())
            .add("error_rate_threshold", json::JsonObjectBuilder()
                .add("type", "number")
                .add("minimum", 0.0)
                .add("maximum", 1.0)
                .add("default", 0.5)
                .add("description", "Error rate threshold to open circuit (0.0-1.0)")
                .build())
            .add("min_requests", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1)
                .add("maximum", 1000)
                .add("default", 10)
                .add("description", "Minimum requests before checking error rate")
                .build())
            .add("timeout_ms", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1000)
                .add("maximum", 300000)
                .add("default", 30000)
                .add("description", "Time in milliseconds before attempting recovery")
                .build())
            .add("window_size_ms", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1000)
                .add("maximum", 600000)
                .add("default", 60000)
                .add("description", "Sliding window size for metrics in milliseconds")
                .build())
            .add("half_open_max_requests", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1)
                .add("maximum", 100)
                .add("default", 3)
                .add("description", "Maximum requests allowed in half-open state")
                .build())
            .add("half_open_success_threshold", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1)
                .add("maximum", 100)
                .add("default", 2)
                .add("description", "Successful requests needed to close circuit")
                .build())
            .add("track_timeouts", json::JsonObjectBuilder()
                .add("type", "boolean")
                .add("default", true)
                .add("description", "Track timeouts as failures")
                .build())
            .add("track_errors", json::JsonObjectBuilder()
                .add("type", "boolean")
                .add("default", true)
                .add("description", "Track errors as failures")
                .build())
            .add("track_4xx_as_errors", json::JsonObjectBuilder()
                .add("type", "boolean")
                .add("default", false)
                .add("description", "Count 4xx client errors as failures")
                .build())
            .build())
        .add("additionalProperties", false)
        .build();
    
    GOPHER_LOG(Debug, "CircuitBreakerFilterFactory initialized");
  }

  ~CircuitBreakerFilterFactory() {
    GOPHER_LOG(Debug, "CircuitBreakerFilterFactory destroyed");
  }

  network::FilterSharedPtr createFilter(const json::JsonValue& config) const override {
    GOPHER_LOG(Info, "Creating CircuitBreakerFilter instance");
    
    // Apply defaults if needed
    auto final_config = applyDefaults(config);
    
    // Validate configuration
    if (!validateConfig(final_config)) {
      GOPHER_LOG(Error, "Invalid configuration for CircuitBreakerFilter");
      throw std::runtime_error("Invalid CircuitBreakerFilter configuration");
    }
    
    // Extract configuration values
    int failure_threshold = final_config["failure_threshold"].getInt(5);
    double error_rate_threshold = final_config["error_rate_threshold"].getFloat(0.5);
    int min_requests = final_config["min_requests"].getInt(10);
    int timeout_ms = final_config["timeout_ms"].getInt(30000);
    int window_size_ms = final_config["window_size_ms"].getInt(60000);
    int half_open_max_requests = final_config["half_open_max_requests"].getInt(3);
    int half_open_success_threshold = final_config["half_open_success_threshold"].getInt(2);
    bool track_timeouts = final_config["track_timeouts"].getBool(true);
    bool track_errors = final_config["track_errors"].getBool(true);
    bool track_4xx_as_errors = final_config["track_4xx_as_errors"].getBool(false);
    
    // Log configuration
    GOPHER_LOG(Debug, "CircuitBreakerFilter config: failure_threshold=%d error_rate=%.2f "
               "min_requests=%d timeout=%dms window=%dms half_open_max=%d half_open_success=%d",
               failure_threshold, error_rate_threshold, min_requests,
               timeout_ms, window_size_ms, half_open_max_requests, half_open_success_threshold);
    
    GOPHER_LOG(Debug, "CircuitBreakerFilter tracking: timeouts=%s errors=%s 4xx_as_errors=%s",
               track_timeouts ? "yes" : "no",
               track_errors ? "yes" : "no",
               track_4xx_as_errors ? "yes" : "no");
    
    // Validate logical consistency
    if (half_open_success_threshold > half_open_max_requests) {
      GOPHER_LOG(Warning, "half_open_success_threshold (%d) > half_open_max_requests (%d) - circuit may never close",
                 half_open_success_threshold, half_open_max_requests);
    }
    
    // Note: The actual filter creation requires CircuitBreakerFilter::Callbacks
    // which should be injected through a different mechanism (e.g., connection context)
    // For now, we return nullptr as a placeholder - the real implementation
    // would get these dependencies from the filter chain context
    
    GOPHER_LOG(Warning, "CircuitBreakerFilter creation requires runtime dependencies (callbacks)");
    GOPHER_LOG(Warning, "Returning placeholder - actual filter creation should be done by chain builder");
    
    // In a real implementation, the filter chain builder would provide these dependencies
    // and create the filter properly. This factory just validates and prepares the config.
    return nullptr;
  }

  const FilterFactoryMetadata& getMetadata() const override {
    return metadata_;
  }

  json::JsonValue getDefaultConfig() const override {
    return json::JsonObjectBuilder()
        .add("failure_threshold", 5)
        .add("error_rate_threshold", 0.5)
        .add("min_requests", 10)
        .add("timeout_ms", 30000)
        .add("window_size_ms", 60000)
        .add("half_open_max_requests", 3)
        .add("half_open_success_threshold", 2)
        .add("track_timeouts", true)
        .add("track_errors", true)
        .add("track_4xx_as_errors", false)
        .build();
  }

  bool validateConfig(const json::JsonValue& config) const override {
    if (!config.isObject()) {
      GOPHER_LOG(Error, "CircuitBreakerFilter config must be an object");
      return false;
    }
    
    // Validate failure_threshold if present
    if (config.contains("failure_threshold")) {
      if (!config["failure_threshold"].isInteger()) {
        GOPHER_LOG(Error, "failure_threshold must be an integer");
        return false;
      }
      int threshold = config["failure_threshold"].getInt();
      if (threshold < 1 || threshold > 100) {
        GOPHER_LOG(Error, "failure_threshold %d out of range [1, 100]", threshold);
        return false;
      }
    }
    
    // Validate error_rate_threshold if present
    if (config.contains("error_rate_threshold")) {
      if (!config["error_rate_threshold"].isNumber()) {
        GOPHER_LOG(Error, "error_rate_threshold must be a number");
        return false;
      }
      double rate = config["error_rate_threshold"].getFloat();
      if (rate < 0.0 || rate > 1.0) {
        GOPHER_LOG(Error, "error_rate_threshold %.2f out of range [0.0, 1.0]", rate);
        return false;
      }
    }
    
    // Validate min_requests if present
    if (config.contains("min_requests")) {
      if (!config["min_requests"].isInteger()) {
        GOPHER_LOG(Error, "min_requests must be an integer");
        return false;
      }
      int min_req = config["min_requests"].getInt();
      if (min_req < 1 || min_req > 1000) {
        GOPHER_LOG(Error, "min_requests %d out of range [1, 1000]", min_req);
        return false;
      }
    }
    
    // Validate timeout_ms if present
    if (config.contains("timeout_ms")) {
      if (!config["timeout_ms"].isInteger()) {
        GOPHER_LOG(Error, "timeout_ms must be an integer");
        return false;
      }
      int timeout = config["timeout_ms"].getInt();
      if (timeout < 1000 || timeout > 300000) {
        GOPHER_LOG(Error, "timeout_ms %d out of range [1000, 300000]", timeout);
        return false;
      }
    }
    
    // Validate window_size_ms if present
    if (config.contains("window_size_ms")) {
      if (!config["window_size_ms"].isInteger()) {
        GOPHER_LOG(Error, "window_size_ms must be an integer");
        return false;
      }
      int window = config["window_size_ms"].getInt();
      if (window < 1000 || window > 600000) {
        GOPHER_LOG(Error, "window_size_ms %d out of range [1000, 600000]", window);
        return false;
      }
    }
    
    // Validate half_open_max_requests if present
    if (config.contains("half_open_max_requests")) {
      if (!config["half_open_max_requests"].isInteger()) {
        GOPHER_LOG(Error, "half_open_max_requests must be an integer");
        return false;
      }
      int max_req = config["half_open_max_requests"].getInt();
      if (max_req < 1 || max_req > 100) {
        GOPHER_LOG(Error, "half_open_max_requests %d out of range [1, 100]", max_req);
        return false;
      }
    }
    
    // Validate half_open_success_threshold if present
    if (config.contains("half_open_success_threshold")) {
      if (!config["half_open_success_threshold"].isInteger()) {
        GOPHER_LOG(Error, "half_open_success_threshold must be an integer");
        return false;
      }
      int success = config["half_open_success_threshold"].getInt();
      if (success < 1 || success > 100) {
        GOPHER_LOG(Error, "half_open_success_threshold %d out of range [1, 100]", success);
        return false;
      }
    }
    
    // Validate boolean fields
    if (config.contains("track_timeouts") && !config["track_timeouts"].isBoolean()) {
      GOPHER_LOG(Error, "track_timeouts must be a boolean");
      return false;
    }
    
    if (config.contains("track_errors") && !config["track_errors"].isBoolean()) {
      GOPHER_LOG(Error, "track_errors must be a boolean");
      return false;
    }
    
    if (config.contains("track_4xx_as_errors") && !config["track_4xx_as_errors"].isBoolean()) {
      GOPHER_LOG(Error, "track_4xx_as_errors must be a boolean");
      return false;
    }
    
    // Warn about boundary values
    if (config.contains("error_rate_threshold")) {
      double rate = config["error_rate_threshold"].getFloat();
      if (rate < 0.1) {
        GOPHER_LOG(Warning, "error_rate_threshold %.2f is very low - circuit may open frequently", rate);
      } else if (rate > 0.9) {
        GOPHER_LOG(Warning, "error_rate_threshold %.2f is very high - circuit may never open", rate);
      }
    }
    
    if (config.contains("timeout_ms")) {
      int timeout = config["timeout_ms"].getInt();
      if (timeout < 5000) {
        GOPHER_LOG(Warning, "timeout_ms %dms is very short - circuit may oscillate", timeout);
      } else if (timeout > 120000) {
        GOPHER_LOG(Warning, "timeout_ms %dms is very long - recovery may be delayed", timeout);
      }
    }
    
    if (config.contains("failure_threshold")) {
      int threshold = config["failure_threshold"].getInt();
      if (threshold == 1) {
        GOPHER_LOG(Warning, "failure_threshold is 1 - circuit will open on first failure");
      }
    }
    
    // Check for logical consistency
    if (config.contains("half_open_max_requests") && config.contains("half_open_success_threshold")) {
      int max_req = config["half_open_max_requests"].getInt();
      int success = config["half_open_success_threshold"].getInt();
      if (success > max_req) {
        GOPHER_LOG(Error, "half_open_success_threshold (%d) cannot exceed half_open_max_requests (%d)",
                   success, max_req);
        return false;
      }
    }
    
    GOPHER_LOG(Debug, "CircuitBreakerFilter configuration validated successfully");
    return true;
  }

 private:
  json::JsonValue applyDefaults(const json::JsonValue& config) const {
    auto defaults = getDefaultConfig();
    if (!config.isObject()) {
      GOPHER_LOG(Debug, "Using all defaults for CircuitBreakerFilter");
      return defaults;
    }
    
    // Merge config with defaults
    json::JsonValue result = defaults;
    for (const auto& key : config.keys()) {
      result.set(key, config[key]);
    }
    
    GOPHER_LOG(Debug, "Applied defaults to CircuitBreakerFilter configuration");
    return result;
  }

  mutable FilterFactoryMetadata metadata_;
};

/**
 * Explicit registration function for static linking support
 * This ensures the factory is registered even when static initializers don't run
 */
void registerCircuitBreakerFilterFactory() {
  static bool registered = false;
  if (!registered) {
    FilterRegistry::instance().registerFactory(
        "circuit_breaker",
        std::make_shared<CircuitBreakerFilterFactory>());
    registered = true;
    GOPHER_LOG(Debug, "CircuitBreakerFilterFactory explicitly registered");
  }
}

// Register the factory with the filter registry via static initializer
REGISTER_FILTER_FACTORY(CircuitBreakerFilterFactory, "circuit_breaker")

// Export for static linking - using magic number as sentinel value
extern "C" {
  void* circuit_breaker_filter_registrar_ref = (void*)0xDEADBEEF;
}

}  // namespace filter
}  // namespace mcp