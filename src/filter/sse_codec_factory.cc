/**
 * @file sse_codec_factory.cc
 * @brief Factory implementation for Server-Sent Events codec filter
 */

#include "mcp/filter/filter_registry.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"

#define GOPHER_LOG_COMPONENT "filter.factory.core"

namespace mcp {
namespace filter {

/**
 * Factory for creating SseCodecFilter instances
 * 
 * Configuration schema:
 * {
 *   "mode": "server" | "client",     // Operation mode (default: "server")
 *   "max_event_size": number,        // Max event size in bytes (default: 65536)
 *   "retry_ms": number,              // Default retry timeout (default: 3000)
 *   "keep_alive_ms": number,         // Keep-alive interval (default: 30000)
 *   "enable_compression": boolean,   // Enable event compression (default: false)
 *   "event_buffer_limit": number     // Max buffered events (default: 100)
 * }
 */
class SseCodecFilterFactory : public FilterFactory {
 public:
  SseCodecFilterFactory() {
    // Initialize metadata
    metadata_.name = "sse_codec";
    metadata_.version = "1.0.0";
    metadata_.description = "Server-Sent Events codec filter for real-time event streaming";
    metadata_.dependencies = {"network", "event", "http_codec"};
    
    // Define configuration schema
    metadata_.config_schema = json::JsonObjectBuilder()
        .add("type", "object")
        .add("properties", json::JsonObjectBuilder()
            .add("mode", json::JsonObjectBuilder()
                .add("type", "string")
                .add("enum", json::JsonArrayBuilder()
                    .add("server")
                    .add("client")
                    .build())
                .add("default", "server")
                .add("description", "Operation mode: server (send events) or client (receive events)")
                .build())
            .add("max_event_size", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1024)
                .add("maximum", 1048576)  // 1MB
                .add("default", 65536)  // 64KB
                .add("description", "Maximum event size in bytes")
                .build())
            .add("retry_ms", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 100)
                .add("maximum", 60000)
                .add("default", 3000)
                .add("description", "Default retry timeout in milliseconds")
                .build())
            .add("keep_alive_ms", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1000)
                .add("maximum", 300000)
                .add("default", 30000)
                .add("description", "Keep-alive comment interval in milliseconds")
                .build())
            .add("enable_compression", json::JsonObjectBuilder()
                .add("type", "boolean")
                .add("default", false)
                .add("description", "Enable event data compression")
                .build())
            .add("event_buffer_limit", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1)
                .add("maximum", 10000)
                .add("default", 100)
                .add("description", "Maximum number of buffered events")
                .build())
            .build())
        .add("additionalProperties", false)
        .build();
    
    GOPHER_LOG(Debug, "SseCodecFilterFactory initialized");
  }

  ~SseCodecFilterFactory() {
    GOPHER_LOG(Debug, "SseCodecFilterFactory destroyed");
  }

  network::FilterSharedPtr createFilter(const json::JsonValue& config) const override {
    GOPHER_LOG(Info, "Creating SseCodecFilter instance");
    
    // Apply defaults if needed
    auto final_config = applyDefaults(config);
    
    // Validate configuration
    if (!validateConfig(final_config)) {
      GOPHER_LOG(Error, "Invalid configuration for SseCodecFilter");
      throw std::runtime_error("Invalid SseCodecFilter configuration");
    }
    
    // Extract configuration values
    std::string mode = final_config["mode"].getString("server");
    bool is_server = (mode == "server");
    int max_event_size = final_config["max_event_size"].getInt(65536);
    int retry_ms = final_config["retry_ms"].getInt(3000);
    int keep_alive_ms = final_config["keep_alive_ms"].getInt(30000);
    bool enable_compression = final_config["enable_compression"].getBool(false);
    int event_buffer_limit = final_config["event_buffer_limit"].getInt(100);
    
    // Log configuration
    GOPHER_LOG(Debug, "SseCodecFilter config: mode=%s max_event_size=%d retry_ms=%d keep_alive_ms=%d compression=%s buffer_limit=%d",
               mode.c_str(), max_event_size, retry_ms, keep_alive_ms,
               enable_compression ? "enabled" : "disabled", event_buffer_limit);
    
    // Note: The actual filter creation requires EventCallbacks and Dispatcher
    // which should be injected through a different mechanism (e.g., connection context)
    // For now, we return nullptr as a placeholder - the real implementation
    // would get these dependencies from the filter chain context
    
    GOPHER_LOG(Warning, "SseCodecFilter creation requires runtime dependencies (callbacks, dispatcher)");
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
        .add("mode", "server")
        .add("max_event_size", 65536)
        .add("retry_ms", 3000)
        .add("keep_alive_ms", 30000)
        .add("enable_compression", false)
        .add("event_buffer_limit", 100)
        .build();
  }

  bool validateConfig(const json::JsonValue& config) const override {
    if (!config.isObject()) {
      GOPHER_LOG(Error, "SseCodecFilter config must be an object");
      return false;
    }
    
    // Validate mode if present
    if (config.contains("mode")) {
      std::string mode = config["mode"].getString("");
      if (mode != "server" && mode != "client") {
        GOPHER_LOG(Error, "Invalid mode '%s' - must be 'server' or 'client'", mode.c_str());
        return false;
      }
    }
    
    // Validate max_event_size if present
    if (config.contains("max_event_size")) {
      if (!config["max_event_size"].isInteger()) {
        GOPHER_LOG(Error, "max_event_size must be an integer");
        return false;
      }
      int size = config["max_event_size"].getInt();
      if (size < 1024 || size > 1048576) {
        GOPHER_LOG(Error, "max_event_size %d out of range [1024, 1048576]", size);
        return false;
      }
    }
    
    // Validate retry_ms if present
    if (config.contains("retry_ms")) {
      if (!config["retry_ms"].isInteger()) {
        GOPHER_LOG(Error, "retry_ms must be an integer");
        return false;
      }
      int retry = config["retry_ms"].getInt();
      if (retry < 100 || retry > 60000) {
        GOPHER_LOG(Error, "retry_ms %d out of range [100, 60000]", retry);
        return false;
      }
    }
    
    // Validate keep_alive_ms if present
    if (config.contains("keep_alive_ms")) {
      if (!config["keep_alive_ms"].isInteger()) {
        GOPHER_LOG(Error, "keep_alive_ms must be an integer");
        return false;
      }
      int keep_alive = config["keep_alive_ms"].getInt();
      if (keep_alive < 1000 || keep_alive > 300000) {
        GOPHER_LOG(Error, "keep_alive_ms %d out of range [1000, 300000]", keep_alive);
        return false;
      }
    }
    
    // Validate event_buffer_limit if present
    if (config.contains("event_buffer_limit")) {
      if (!config["event_buffer_limit"].isInteger()) {
        GOPHER_LOG(Error, "event_buffer_limit must be an integer");
        return false;
      }
      int limit = config["event_buffer_limit"].getInt();
      if (limit < 1 || limit > 10000) {
        GOPHER_LOG(Error, "event_buffer_limit %d out of range [1, 10000]", limit);
        return false;
      }
    }
    
    // Validate boolean fields
    if (config.contains("enable_compression") && !config["enable_compression"].isBoolean()) {
      GOPHER_LOG(Error, "enable_compression must be a boolean");
      return false;
    }
    
    GOPHER_LOG(Debug, "SseCodecFilter configuration validated successfully");
    return true;
  }

 private:
  json::JsonValue applyDefaults(const json::JsonValue& config) const {
    auto defaults = getDefaultConfig();
    if (!config.isObject()) {
      GOPHER_LOG(Debug, "Using all defaults for SseCodecFilter");
      return defaults;
    }
    
    // Merge config with defaults
    json::JsonValue result = defaults;
    for (const auto& key : config.keys()) {
      result.set(key, config[key]);
    }
    
    GOPHER_LOG(Debug, "Applied defaults to SseCodecFilter configuration");
    return result;
  }

  mutable FilterFactoryMetadata metadata_;
};

// Register the factory with the filter registry
REGISTER_FILTER_FACTORY(SseCodecFilterFactory, "sse_codec")

}  // namespace filter
}  // namespace mcp