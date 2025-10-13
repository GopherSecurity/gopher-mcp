/**
 * @file rate_limit_factory.cc
 * @brief Factory implementation for rate limiting filter
 */

#include "mcp/filter/filter_registry.h"
#include "mcp/filter/rate_limit_filter.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"

#define GOPHER_LOG_COMPONENT "filter.factory.qos"

namespace mcp {
namespace filter {

/**
 * Factory for creating RateLimitFilter instances
 * 
 * Configuration schema:
 * {
 *   "strategy": "token_bucket" | "sliding_window" | "fixed_window" | "leaky_bucket",
 *   "bucket_capacity": number,       // For token bucket: max tokens (default: 100)
 *   "refill_rate": number,           // For token bucket: tokens per second (default: 10)
 *   "window_size_seconds": number,   // For window strategies: window duration (default: 60)
 *   "max_requests_per_window": number, // For window strategies: max requests (default: 100)
 *   "leak_rate": number,             // For leaky bucket: requests per second (default: 10)
 *   "allow_burst": boolean,          // Allow burst traffic (default: true)
 *   "burst_size": number,            // Extra capacity for bursts (default: 20)
 *   "per_client_limiting": boolean,  // Enable per-client limits (default: false)
 *   "client_limits": object          // Map of client ID to limit (optional)
 * }
 */
class RateLimitFilterFactory : public FilterFactory {
 public:
  RateLimitFilterFactory() {
    // Initialize metadata
    metadata_.name = "rate_limit";
    metadata_.version = "1.0.0";
    metadata_.description = "Rate limiting filter with multiple algorithm support";
    metadata_.dependencies = {"network"};
    
    // Define configuration schema
    metadata_.config_schema = json::JsonObjectBuilder()
        .add("type", "object")
        .add("properties", json::JsonObjectBuilder()
            .add("strategy", json::JsonObjectBuilder()
                .add("type", "string")
                .add("enum", json::JsonArrayBuilder()
                    .add("token_bucket")
                    .add("sliding_window")
                    .add("fixed_window")
                    .add("leaky_bucket")
                    .build())
                .add("default", "token_bucket")
                .add("description", "Rate limiting algorithm")
                .build())
            .add("bucket_capacity", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1)
                .add("maximum", 100000)
                .add("default", 100)
                .add("description", "Maximum tokens in bucket (token bucket strategy)")
                .build())
            .add("refill_rate", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1)
                .add("maximum", 10000)
                .add("default", 10)
                .add("description", "Tokens added per second (token bucket strategy)")
                .build())
            .add("window_size_seconds", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1)
                .add("maximum", 3600)
                .add("default", 60)
                .add("description", "Time window size in seconds (window strategies)")
                .build())
            .add("max_requests_per_window", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1)
                .add("maximum", 100000)
                .add("default", 100)
                .add("description", "Maximum requests per window (window strategies)")
                .build())
            .add("leak_rate", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1)
                .add("maximum", 10000)
                .add("default", 10)
                .add("description", "Requests processed per second (leaky bucket strategy)")
                .build())
            .add("allow_burst", json::JsonObjectBuilder()
                .add("type", "boolean")
                .add("default", true)
                .add("description", "Allow burst traffic beyond normal limits")
                .build())
            .add("burst_size", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 0)
                .add("maximum", 1000)
                .add("default", 20)
                .add("description", "Extra capacity for burst traffic")
                .build())
            .add("per_client_limiting", json::JsonObjectBuilder()
                .add("type", "boolean")
                .add("default", false)
                .add("description", "Enable per-client rate limiting")
                .build())
            .add("client_limits", json::JsonObjectBuilder()
                .add("type", "object")
                .add("additionalProperties", json::JsonObjectBuilder()
                    .add("type", "integer")
                    .add("minimum", 1)
                    .build())
                .add("description", "Map of client ID to rate limit")
                .build())
            .build())
        .add("additionalProperties", false)
        .build();
    
    GOPHER_LOG(Debug, "RateLimitFilterFactory initialized");
  }

  ~RateLimitFilterFactory() {
    GOPHER_LOG(Debug, "RateLimitFilterFactory destroyed");
  }

  network::FilterSharedPtr createFilter(const json::JsonValue& config) const override {
    GOPHER_LOG(Info, "Creating RateLimitFilter instance");
    
    // Apply defaults if needed
    auto final_config = applyDefaults(config);
    
    // Validate configuration
    if (!validateConfig(final_config)) {
      GOPHER_LOG(Error, "Invalid configuration for RateLimitFilter");
      throw std::runtime_error("Invalid RateLimitFilter configuration");
    }
    
    // Extract configuration values
    std::string strategy = final_config["strategy"].getString("token_bucket");
    
    // Token bucket parameters
    int bucket_capacity = final_config["bucket_capacity"].getInt(100);
    int refill_rate = final_config["refill_rate"].getInt(10);
    
    // Window parameters
    int window_size_seconds = final_config["window_size_seconds"].getInt(60);
    int max_requests_per_window = final_config["max_requests_per_window"].getInt(100);
    
    // Leaky bucket parameters
    int leak_rate = final_config["leak_rate"].getInt(10);
    
    // Burst handling
    bool allow_burst = final_config["allow_burst"].getBool(true);
    int burst_size = final_config["burst_size"].getInt(20);
    
    // Per-client limiting
    bool per_client_limiting = final_config["per_client_limiting"].getBool(false);
    
    // Log configuration
    GOPHER_LOG(Debug, "RateLimitFilter config: strategy=%s bucket_capacity=%d refill_rate=%d "
               "window_size=%ds max_requests=%d leak_rate=%d burst=%s burst_size=%d per_client=%s",
               strategy.c_str(), bucket_capacity, refill_rate,
               window_size_seconds, max_requests_per_window, leak_rate,
               allow_burst ? "enabled" : "disabled", burst_size,
               per_client_limiting ? "enabled" : "disabled");
    
    // Log client limits if present
    if (final_config.contains("client_limits") && final_config["client_limits"].isObject()) {
      auto client_limits = final_config["client_limits"];
      GOPHER_LOG(Debug, "Client-specific rate limits configured for %zu clients", 
                 client_limits.keys().size());
      for (const auto& client_id : client_limits.keys()) {
        int limit = client_limits[client_id].getInt(0);
        GOPHER_LOG(Debug, "  Client %s: limit=%d", client_id.c_str(), limit);
      }
    }
    
    // Note: The actual filter creation requires RateLimitFilter::Callbacks
    // which should be injected through a different mechanism (e.g., connection context)
    // For now, we return nullptr as a placeholder - the real implementation
    // would get these dependencies from the filter chain context
    
    GOPHER_LOG(Warning, "RateLimitFilter creation requires runtime dependencies (callbacks)");
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
        .add("strategy", "token_bucket")
        .add("bucket_capacity", 100)
        .add("refill_rate", 10)
        .add("window_size_seconds", 60)
        .add("max_requests_per_window", 100)
        .add("leak_rate", 10)
        .add("allow_burst", true)
        .add("burst_size", 20)
        .add("per_client_limiting", false)
        .build();
  }

  bool validateConfig(const json::JsonValue& config) const override {
    if (!config.isObject()) {
      GOPHER_LOG(Error, "RateLimitFilter config must be an object");
      return false;
    }
    
    // Validate strategy if present
    if (config.contains("strategy")) {
      std::string strategy = config["strategy"].getString("");
      if (strategy != "token_bucket" && strategy != "sliding_window" && 
          strategy != "fixed_window" && strategy != "leaky_bucket") {
        GOPHER_LOG(Error, "Invalid strategy '%s' - must be one of: token_bucket, sliding_window, fixed_window, leaky_bucket", 
                   strategy.c_str());
        return false;
      }
    }
    
    // Validate bucket_capacity if present
    if (config.contains("bucket_capacity")) {
      if (!config["bucket_capacity"].isInteger()) {
        GOPHER_LOG(Error, "bucket_capacity must be an integer");
        return false;
      }
      int capacity = config["bucket_capacity"].getInt();
      if (capacity < 1 || capacity > 100000) {
        GOPHER_LOG(Error, "bucket_capacity %d out of range [1, 100000]", capacity);
        return false;
      }
    }
    
    // Validate refill_rate if present
    if (config.contains("refill_rate")) {
      if (!config["refill_rate"].isInteger()) {
        GOPHER_LOG(Error, "refill_rate must be an integer");
        return false;
      }
      int rate = config["refill_rate"].getInt();
      if (rate < 1 || rate > 10000) {
        GOPHER_LOG(Error, "refill_rate %d out of range [1, 10000]", rate);
        return false;
      }
    }
    
    // Validate window_size_seconds if present
    if (config.contains("window_size_seconds")) {
      if (!config["window_size_seconds"].isInteger()) {
        GOPHER_LOG(Error, "window_size_seconds must be an integer");
        return false;
      }
      int window = config["window_size_seconds"].getInt();
      if (window < 1 || window > 3600) {
        GOPHER_LOG(Error, "window_size_seconds %d out of range [1, 3600]", window);
        return false;
      }
    }
    
    // Validate max_requests_per_window if present
    if (config.contains("max_requests_per_window")) {
      if (!config["max_requests_per_window"].isInteger()) {
        GOPHER_LOG(Error, "max_requests_per_window must be an integer");
        return false;
      }
      int max_requests = config["max_requests_per_window"].getInt();
      if (max_requests < 1 || max_requests > 100000) {
        GOPHER_LOG(Error, "max_requests_per_window %d out of range [1, 100000]", max_requests);
        return false;
      }
    }
    
    // Validate leak_rate if present
    if (config.contains("leak_rate")) {
      if (!config["leak_rate"].isInteger()) {
        GOPHER_LOG(Error, "leak_rate must be an integer");
        return false;
      }
      int rate = config["leak_rate"].getInt();
      if (rate < 1 || rate > 10000) {
        GOPHER_LOG(Error, "leak_rate %d out of range [1, 10000]", rate);
        return false;
      }
    }
    
    // Validate burst_size if present
    if (config.contains("burst_size")) {
      if (!config["burst_size"].isInteger()) {
        GOPHER_LOG(Error, "burst_size must be an integer");
        return false;
      }
      int size = config["burst_size"].getInt();
      if (size < 0 || size > 1000) {
        GOPHER_LOG(Error, "burst_size %d out of range [0, 1000]", size);
        return false;
      }
    }
    
    // Validate boolean fields
    if (config.contains("allow_burst") && !config["allow_burst"].isBoolean()) {
      GOPHER_LOG(Error, "allow_burst must be a boolean");
      return false;
    }
    
    if (config.contains("per_client_limiting") && !config["per_client_limiting"].isBoolean()) {
      GOPHER_LOG(Error, "per_client_limiting must be a boolean");
      return false;
    }
    
    // Validate client_limits if present
    if (config.contains("client_limits")) {
      if (!config["client_limits"].isObject()) {
        GOPHER_LOG(Error, "client_limits must be an object");
        return false;
      }
      
      // Validate each client limit
      auto client_limits = config["client_limits"];
      for (const auto& client_id : client_limits.keys()) {
        if (!client_limits[client_id].isInteger()) {
          GOPHER_LOG(Error, "Client limit for '%s' must be an integer", client_id.c_str());
          return false;
        }
        int limit = client_limits[client_id].getInt();
        if (limit < 1) {
          GOPHER_LOG(Error, "Client limit for '%s' must be positive (got %d)", 
                     client_id.c_str(), limit);
          return false;
        }
      }
    }
    
    // Warn about boundary values
    if (config.contains("burst_size")) {
      int burst_size = config["burst_size"].getInt();
      if (burst_size == 0) {
        GOPHER_LOG(Warning, "burst_size is 0 - burst traffic will be disabled");
      } else if (burst_size > 500) {
        GOPHER_LOG(Warning, "burst_size %d is very high - may allow excessive burst traffic", burst_size);
      }
    }
    
    if (config.contains("refill_rate")) {
      int refill_rate = config["refill_rate"].getInt();
      if (refill_rate > 1000) {
        GOPHER_LOG(Warning, "refill_rate %d is very high - rate limiting may be ineffective", refill_rate);
      }
    }
    
    GOPHER_LOG(Debug, "RateLimitFilter configuration validated successfully");
    return true;
  }

 private:
  json::JsonValue applyDefaults(const json::JsonValue& config) const {
    auto defaults = getDefaultConfig();
    if (!config.isObject()) {
      GOPHER_LOG(Debug, "Using all defaults for RateLimitFilter");
      return defaults;
    }
    
    // Merge config with defaults
    json::JsonValue result = defaults;
    for (const auto& key : config.keys()) {
      result.set(key, config[key]);
    }
    
    GOPHER_LOG(Debug, "Applied defaults to RateLimitFilter configuration");
    return result;
  }

  mutable FilterFactoryMetadata metadata_;
};

/**
 * Explicit registration function for static linking support
 * This ensures the factory is registered even when static initializers don't run
 */
void registerRateLimitFilterFactory() {
  static bool registered = false;
  if (!registered) {
    FilterRegistry::instance().registerFactory(
        "rate_limit",
        std::make_shared<RateLimitFilterFactory>());
    registered = true;
    GOPHER_LOG(Debug, "RateLimitFilterFactory explicitly registered");
  }
}

// Register the factory with the filter registry via static initializer
REGISTER_FILTER_FACTORY(RateLimitFilterFactory, "rate_limit")

// Export for static linking - using magic number as sentinel value
extern "C" {
  void* rate_limit_filter_registrar_ref = (void*)0xDEADBEEF;
}

}  // namespace filter
}  // namespace mcp