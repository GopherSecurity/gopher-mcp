/**
 * @file rate_limit_factory.cc
 * @brief Factory implementation for rate limiting filter
 */

#include "mcp/filter/filter_chain_event_hub.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/filter/rate_limit_filter.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#define GOPHER_LOG_COMPONENT "filter.factory.qos"

#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "mcp/logging/log_macros.h"

namespace mcp {
namespace filter {
namespace {

json::JsonValue makeDefaultConfig() {
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

json::JsonValue mergeWithDefaults(const json::JsonValue& config) {
  auto defaults = makeDefaultConfig();
  if (!config.isObject()) {
    GOPHER_LOG(Debug, "Using all defaults for RateLimitFilter");
    return defaults;
  }

  json::JsonValue result = defaults;
  for (const auto& key : config.keys()) {
    const auto& value = config.at(key);

    if (value.isBoolean()) {
      result.set(key, json::JsonValue(value.getBool()));
    } else if (value.isInteger()) {
      result.set(key, json::JsonValue(value.getInt64()));
    } else if (value.isNumber()) {
      result.set(key, json::JsonValue(value.getFloat()));
    } else if (value.isString()) {
      result.set(key, json::JsonValue(value.getString()));
    } else {
      result.set(key, value);
    }
  }

  GOPHER_LOG(Debug, "Applied defaults to RateLimitFilter configuration");
  return result;
}

RateLimitConfig parseRateLimitConfig(const json::JsonValue& config) {
  RateLimitConfig rl_config;

  const std::string strategy = config["strategy"].getString("token_bucket");
  if (strategy == "token_bucket") {
    rl_config.strategy = RateLimitStrategy::TokenBucket;
  } else if (strategy == "sliding_window") {
    rl_config.strategy = RateLimitStrategy::SlidingWindow;
  } else if (strategy == "fixed_window") {
    rl_config.strategy = RateLimitStrategy::FixedWindow;
  } else if (strategy == "leaky_bucket") {
    rl_config.strategy = RateLimitStrategy::LeakyBucket;
  }

  rl_config.bucket_capacity =
      static_cast<size_t>(config["bucket_capacity"].getInt(100));
  rl_config.refill_rate = static_cast<size_t>(config["refill_rate"].getInt(10));
  rl_config.window_size =
      std::chrono::seconds(config["window_size_seconds"].getInt(60));
  rl_config.max_requests_per_window =
      static_cast<size_t>(config["max_requests_per_window"].getInt(100));
  rl_config.leak_rate = static_cast<size_t>(config["leak_rate"].getInt(10));
  rl_config.allow_burst = config["allow_burst"].getBool(true);
  rl_config.burst_size = static_cast<size_t>(config["burst_size"].getInt(20));
  rl_config.per_client_limiting = config["per_client_limiting"].getBool(false);

  if (config.contains("client_limits") && config["client_limits"].isObject()) {
    const auto client_limits = config["client_limits"];
    for (const auto& client_id : client_limits.keys()) {
      if (client_limits[client_id].isNumber()) {
        rl_config.client_limits[client_id] =
            static_cast<size_t>(client_limits[client_id].getInt());
      }
    }
  }

  return rl_config;
}

// Legacy callback code removed - using unified chain-level events instead

std::string describeContext(const FilterCreationContext& context) {
  std::string scope = context.getModeString();

  if (!context.transport.remote_address.empty()) {
    scope += "@" + context.transport.remote_address;
    if (context.transport.remote_port > 0) {
      scope += ":" + std::to_string(context.transport.remote_port);
    }
  } else if (!context.transport.local_address.empty()) {
    scope += "@" + context.transport.local_address;
    if (context.transport.local_port > 0) {
      scope += ":" + std::to_string(context.transport.local_port);
    }
  }

  return scope;
}

}  // namespace

class RateLimitFilterFactory : public FilterFactory {
 public:
  RateLimitFilterFactory() {
    metadata_.name = "rate_limit";
    metadata_.version = "1.0.0";
    metadata_.description =
        "Rate limiting filter with multiple algorithm support";
    metadata_.dependencies = {"network"};

    metadata_.config_schema =
        json::JsonObjectBuilder()
            .add("type", "object")
            .add(
                "properties",
                json::JsonObjectBuilder()
                    .add("strategy",
                         json::JsonObjectBuilder()
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
                                                .add("description",
                                                     "Maximum tokens in bucket "
                                                     "(token bucket strategy)")
                                                .build())
                    .add("refill_rate", json::JsonObjectBuilder()
                                            .add("type", "integer")
                                            .add("minimum", 1)
                                            .add("maximum", 10000)
                                            .add("default", 10)
                                            .add("description",
                                                 "Tokens added per second "
                                                 "(token bucket strategy)")
                                            .build())
                    .add("window_size_seconds",
                         json::JsonObjectBuilder()
                             .add("type", "integer")
                             .add("minimum", 1)
                             .add("maximum", 3600)
                             .add("default", 60)
                             .add("description",
                                  "Time window size in seconds (window "
                                  "strategies)")
                             .build())
                    .add("max_requests_per_window",
                         json::JsonObjectBuilder()
                             .add("type", "integer")
                             .add("minimum", 1)
                             .add("maximum", 100000)
                             .add("default", 100)
                             .add("description",
                                  "Maximum requests per window (window "
                                  "strategies)")
                             .build())
                    .add("leak_rate", json::JsonObjectBuilder()
                                          .add("type", "integer")
                                          .add("minimum", 1)
                                          .add("maximum", 10000)
                                          .add("default", 10)
                                          .add("description",
                                               "Requests processed per second "
                                               "(leaky bucket strategy)")
                                          .build())
                    .add("allow_burst",
                         json::JsonObjectBuilder()
                             .add("type", "boolean")
                             .add("default", true)
                             .add("description",
                                  "Allow burst traffic beyond normal limits")
                             .build())
                    .add("burst_size",
                         json::JsonObjectBuilder()
                             .add("type", "integer")
                             .add("minimum", 0)
                             .add("maximum", 1000)
                             .add("default", 20)
                             .add("description",
                                  "Extra capacity for burst traffic")
                             .build())
                    .add("per_client_limiting",
                         json::JsonObjectBuilder()
                             .add("type", "boolean")
                             .add("default", false)
                             .add("description",
                                  "Enable per-client rate limiting")
                             .build())
                    .add("client_limits",
                         json::JsonObjectBuilder()
                             .add("type", "object")
                             .add("additionalProperties",
                                  json::JsonObjectBuilder()
                                      .add("type", "integer")
                                      .add("minimum", 1)
                                      .build())
                             .add("description",
                                  "Map of client ID to rate limit")
                             .build())
                    .build())
            .add("additionalProperties", false)
            .build();

    GOPHER_LOG(Debug, "RateLimitFilterFactory initialized");
  }

  ~RateLimitFilterFactory() {
    GOPHER_LOG(Debug, "RateLimitFilterFactory destroyed");
  }

  network::FilterSharedPtr createFilter(
      const json::JsonValue& config) const override {
    GOPHER_LOG(Info, "Creating RateLimitFilter instance");

    const auto final_config = mergeWithDefaults(config);

    if (!validateConfig(final_config)) {
      GOPHER_LOG(Error, "Invalid configuration for RateLimitFilter");
      throw std::runtime_error("Invalid RateLimitFilter configuration");
    }

    const auto rl_config = parseRateLimitConfig(final_config);

    GOPHER_LOG(
        Debug,
        "RateLimitFilter config: strategy=%s bucket_capacity=%d refill_rate=%d "
        "window_size=%ds max_requests=%d leak_rate=%d burst=%s burst_size=%d "
        "per_client=%s",
        final_config["strategy"].getString("token_bucket").c_str(),
        final_config["bucket_capacity"].getInt(100),
        final_config["refill_rate"].getInt(10),
        final_config["window_size_seconds"].getInt(60),
        final_config["max_requests_per_window"].getInt(100),
        final_config["leak_rate"].getInt(10),
        final_config["allow_burst"].getBool(true) ? "enabled" : "disabled",
        final_config["burst_size"].getInt(20),
        final_config["per_client_limiting"].getBool(false) ? "enabled"
                                                           : "disabled");

    // Create with nullptr event emitter (for standalone usage without chain)
    return std::make_shared<RateLimitFilter>(nullptr, rl_config);
  }

  const FilterFactoryMetadata& getMetadata() const override {
    return metadata_;
  }

  json::JsonValue getDefaultConfig() const override {
    return makeDefaultConfig();
  }

  bool validateConfig(const json::JsonValue& config) const override {
    if (!config.isObject()) {
      GOPHER_LOG(Error, "RateLimitFilter config must be an object");
      return false;
    }

    if (config.contains("strategy")) {
      std::string strategy = config["strategy"].getString("");
      if (strategy != "token_bucket" && strategy != "sliding_window" &&
          strategy != "fixed_window" && strategy != "leaky_bucket") {
        GOPHER_LOG(Error,
                   "Invalid strategy '%s' - must be one of: token_bucket, "
                   "sliding_window, fixed_window, leaky_bucket",
                   strategy.c_str());
        return false;
      }
    }

    auto validateRange = [](const json::JsonValue& value, const char* key,
                            int min, int max) -> bool {
      if (!value.isNumber()) {
        GOPHER_LOG(Error, "%s must be numeric", key);
        return false;
      }

      const double numeric = value.getFloat();
      const double rounded = std::round(numeric);
      if (std::fabs(numeric - rounded) > 1e-6) {
        GOPHER_LOG(Error, "%s must be a whole number (got %.6f)", key, numeric);
        return false;
      }

      const int current = static_cast<int>(rounded);
      if (current < min || current > max) {
        GOPHER_LOG(Error, "%s %d out of range [%d, %d]", key, current, min,
                   max);
        return false;
      }
      return true;
    };

    if (config.contains("bucket_capacity") &&
        !validateRange(config["bucket_capacity"], "bucket_capacity", 1,
                       100000)) {
      return false;
    }

    if (config.contains("refill_rate") &&
        !validateRange(config["refill_rate"], "refill_rate", 1, 10000)) {
      return false;
    }

    if (config.contains("window_size_seconds") &&
        !validateRange(config["window_size_seconds"], "window_size_seconds", 1,
                       3600)) {
      return false;
    }

    if (config.contains("max_requests_per_window") &&
        !validateRange(config["max_requests_per_window"],
                       "max_requests_per_window", 1, 100000)) {
      return false;
    }

    if (config.contains("leak_rate") &&
        !validateRange(config["leak_rate"], "leak_rate", 1, 10000)) {
      return false;
    }

    if (config.contains("burst_size") &&
        !validateRange(config["burst_size"], "burst_size", 0, 1000)) {
      return false;
    }

    if (config.contains("per_client_limiting") &&
        !config["per_client_limiting"].isBoolean()) {
      GOPHER_LOG(Error, "per_client_limiting must be a boolean");
      return false;
    }

    if (config.contains("client_limits")) {
      if (!config["client_limits"].isObject()) {
        GOPHER_LOG(Error, "client_limits must be an object");
        return false;
      }
      const auto client_limits = config["client_limits"];
      for (const auto& client_id : client_limits.keys()) {
        const auto& value = client_limits[client_id];
        const std::string key_name = "client_limits[" + client_id + "]";
        if (!validateRange(value, key_name.c_str(), 1, 100000)) {
          return false;
        }
      }
    }

    return true;
  }

 private:
  json::JsonValue applyDefaults(const json::JsonValue& config) const {
    return mergeWithDefaults(config);
  }

  mutable FilterFactoryMetadata metadata_;
};

network::FilterSharedPtr createRateLimitFilter(
    const FilterCreationContext& context, const json::JsonValue& config) {
  RateLimitFilterFactory factory;

  const auto final_config = mergeWithDefaults(config);
  if (!factory.validateConfig(final_config)) {
    GOPHER_LOG(Error, "RateLimitFilter configuration failed validation");
    throw std::runtime_error("Invalid RateLimitFilter configuration");
  }

  const auto rl_config = parseRateLimitConfig(final_config);
  const std::string scope = describeContext(context);
  GOPHER_LOG(Debug, "Creating rate_limit filter for scope=%s", scope.c_str());

  // Get event emitter from context
  std::shared_ptr<FilterEventEmitter> event_emitter;
  if (context.event_emitter) {
    try {
      event_emitter =
          std::static_pointer_cast<FilterEventEmitter>(context.event_emitter);
      GOPHER_LOG(
          Info,
          "[RATE_LIMIT] 🔧 Filter created with EVENT EMITTER from context");
    } catch (const std::exception& e) {
      GOPHER_LOG(Warning, "Failed to cast event_emitter: %s", e.what());
    }
  }

  if (!event_emitter && context.event_hub) {
    try {
      auto hub =
          std::static_pointer_cast<FilterChainEventHub>(context.event_hub);
      if (hub) {
        GOPHER_LOG(Info, "[RATE_LIMIT] Creating event emitter from event hub");
        event_emitter = std::make_shared<FilterEventEmitter>(
            hub, "rate_limit", std::string{},  // instance id
            scope);
        GOPHER_LOG(Info, "[RATE_LIMIT] ✅ Event emitter created and connected");
      }
    } catch (const std::exception& e) {
      GOPHER_LOG(Warning, "Failed to create event emitter from hub: %s",
                 e.what());
    }
  }

  if (!event_emitter) {
    GOPHER_LOG(Warning,
               "[RATE_LIMIT] ⚠️  Filter created WITHOUT event emitter - events "
               "will NOT be emitted");
  }

  return std::make_shared<RateLimitFilter>(event_emitter, rl_config);
}

BasicFilterMetadata makeMetadata() {
  BasicFilterMetadata metadata;
  metadata.name = "rate_limit";
  metadata.version = "1.0.0";
  metadata.description =
      "Rate limiting filter with optional per-client controls";
  metadata.default_config = makeDefaultConfig();
  return metadata;
}

void registerRateLimitFilterFactory() {
  static bool registered = false;
  if (registered) {
    return;
  }

  auto& registry = FilterRegistry::instance();
  if (!registry.registerFactory("rate_limit",
                                std::make_shared<RateLimitFilterFactory>())) {
    GOPHER_LOG(
        Error,
        "Failed to register traditional rate_limit factory (duplicate?)");
  }

  if (!registry.registerContextFactory("rate_limit", createRateLimitFilter,
                                       makeMetadata())) {
    GOPHER_LOG(
        Error,
        "Failed to register context-aware rate_limit factory (duplicate?)");
  }

  registered = true;
  GOPHER_LOG(Debug,
             "RateLimitFilterFactory registered (traditional + context)");
}

struct RateLimitFilterRegistrar {
  RateLimitFilterRegistrar() { registerRateLimitFilterFactory(); }
};

static RateLimitFilterRegistrar rate_limit_filter_registrar_instance;

extern "C" {
void* rate_limit_filter_registrar_ref = reinterpret_cast<void*>(0xDEADBEEF);
}

}  // namespace filter
}  // namespace mcp
