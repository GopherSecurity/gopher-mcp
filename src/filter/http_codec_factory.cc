/**
 * @file http_codec_factory.cc
 * @brief Factory implementation for HTTP codec filter
 */

#include "mcp/filter/filter_registry.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"

#define GOPHER_LOG_COMPONENT "filter.factory.core"

namespace mcp {
namespace filter {

/**
 * Factory for creating HttpCodecFilter instances
 *
 * Configuration schema:
 * {
 *   "mode": "server" | "client",     // Operation mode (default: "server")
 *   "max_header_size": number,       // Max header size in bytes (default:
 * 8192) "max_body_size": number,         // Max body size in bytes (default:
 * 1048576) "keep_alive": boolean,           // Enable keep-alive (default:
 * true) "timeout_ms": number,            // Connection timeout in ms (default:
 * 30000) "strict_mode": boolean           // Strict HTTP parsing (default:
 * false)
 * }
 */
class HttpCodecFilterFactory : public FilterFactory {
 public:
  HttpCodecFilterFactory() {
    // Initialize metadata
    metadata_.name = "http_codec";
    metadata_.version = "1.0.0";
    metadata_.description =
        "HTTP/1.1 codec filter for request/response handling";
    metadata_.dependencies = {"network", "event"};

    // Define configuration schema
    metadata_.config_schema =
        json::JsonObjectBuilder()
            .add("type", "object")
            .add(
                "properties",
                json::JsonObjectBuilder()
                    .add("mode", json::JsonObjectBuilder()
                                     .add("type", "string")
                                     .add("enum", json::JsonArrayBuilder()
                                                      .add("server")
                                                      .add("client")
                                                      .build())
                                     .add("default", "server")
                                     .add("description",
                                          "Operation mode: server or client")
                                     .build())
                    .add("max_header_size",
                         json::JsonObjectBuilder()
                             .add("type", "integer")
                             .add("minimum", 1024)
                             .add("maximum", 65536)
                             .add("default", 8192)
                             .add("description", "Maximum header size in bytes")
                             .build())
                    .add("max_body_size",
                         json::JsonObjectBuilder()
                             .add("type", "integer")
                             .add("minimum", 0)
                             .add("maximum", 104857600)  // 100MB
                             .add("default", 1048576)    // 1MB
                             .add("description", "Maximum body size in bytes")
                             .build())
                    .add("keep_alive",
                         json::JsonObjectBuilder()
                             .add("type", "boolean")
                             .add("default", true)
                             .add("description", "Enable HTTP keep-alive")
                             .build())
                    .add("timeout_ms",
                         json::JsonObjectBuilder()
                             .add("type", "integer")
                             .add("minimum", 1000)
                             .add("maximum", 300000)
                             .add("default", 30000)
                             .add("description",
                                  "Connection timeout in milliseconds")
                             .build())
                    .add("strict_mode",
                         json::JsonObjectBuilder()
                             .add("type", "boolean")
                             .add("default", false)
                             .add("description", "Enable strict HTTP parsing")
                             .build())
                    .build())
            .add("additionalProperties", false)
            .build();

    GOPHER_LOG(Debug, "HttpCodecFilterFactory initialized");
  }

  ~HttpCodecFilterFactory() {
    GOPHER_LOG(Debug, "HttpCodecFilterFactory destroyed");
  }

  network::FilterSharedPtr createFilter(
      const json::JsonValue& config) const override {
    GOPHER_LOG(Info, "Creating HttpCodecFilter instance");

    // Apply defaults if needed
    auto final_config = applyDefaults(config);

    // Validate configuration
    if (!validateConfig(final_config)) {
      GOPHER_LOG(Error, "Invalid configuration for HttpCodecFilter");
      throw std::runtime_error("Invalid HttpCodecFilter configuration");
    }

    // Extract configuration values
    std::string mode = final_config["mode"].getString("server");
    bool is_server = (mode == "server");

    // Log configuration
    GOPHER_LOG(Debug,
               "HttpCodecFilter config: mode=%s max_header_size=%d "
               "max_body_size=%d keep_alive=%s",
               mode.c_str(), final_config["max_header_size"].getInt(8192),
               final_config["max_body_size"].getInt(1048576),
               final_config["keep_alive"].getBool(true) ? "true" : "false");

    // Note: The actual filter creation requires MessageCallbacks and Dispatcher
    // which should be injected through a different mechanism (e.g., connection
    // context) For now, we return nullptr as a placeholder - the real
    // implementation would get these dependencies from the filter chain context

    GOPHER_LOG(Warning,
               "HttpCodecFilter creation requires runtime dependencies "
               "(callbacks, dispatcher)");
    GOPHER_LOG(Warning,
               "Returning placeholder - actual filter creation should be done "
               "by chain builder");

    // In a real implementation, the filter chain builder would provide these
    // dependencies and create the filter properly. This factory just validates
    // and prepares the config.
    return nullptr;
  }

  const FilterFactoryMetadata& getMetadata() const override {
    return metadata_;
  }

  json::JsonValue getDefaultConfig() const override {
    return json::JsonObjectBuilder()
        .add("mode", "server")
        .add("max_header_size", 8192)
        .add("max_body_size", 1048576)
        .add("keep_alive", true)
        .add("timeout_ms", 30000)
        .add("strict_mode", false)
        .build();
  }

  bool validateConfig(const json::JsonValue& config) const override {
    if (!config.isObject()) {
      GOPHER_LOG(Error, "HttpCodecFilter config must be an object");
      return false;
    }

    // Validate mode if present
    if (config.contains("mode")) {
      std::string mode = config["mode"].getString("");
      if (mode != "server" && mode != "client") {
        GOPHER_LOG(Error, "Invalid mode '%s' - must be 'server' or 'client'",
                   mode.c_str());
        return false;
      }
    }

    // Validate max_header_size if present
    if (config.contains("max_header_size")) {
      if (!config["max_header_size"].isInteger()) {
        GOPHER_LOG(Error, "max_header_size must be an integer");
        return false;
      }
      int size = config["max_header_size"].getInt();
      if (size < 1024 || size > 65536) {
        GOPHER_LOG(Error, "max_header_size %d out of range [1024, 65536]",
                   size);
        return false;
      }
    }

    // Validate max_body_size if present
    if (config.contains("max_body_size")) {
      if (!config["max_body_size"].isInteger()) {
        GOPHER_LOG(Error, "max_body_size must be an integer");
        return false;
      }
      int size = config["max_body_size"].getInt();
      if (size < 0 || size > 104857600) {
        GOPHER_LOG(Error, "max_body_size %d out of range [0, 104857600]", size);
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

    // Validate boolean fields
    if (config.contains("keep_alive") && !config["keep_alive"].isBoolean()) {
      GOPHER_LOG(Error, "keep_alive must be a boolean");
      return false;
    }

    if (config.contains("strict_mode") && !config["strict_mode"].isBoolean()) {
      GOPHER_LOG(Error, "strict_mode must be a boolean");
      return false;
    }

    GOPHER_LOG(Debug, "HttpCodecFilter configuration validated successfully");
    return true;
  }

 private:
  json::JsonValue applyDefaults(const json::JsonValue& config) const {
    auto defaults = getDefaultConfig();
    if (!config.isObject()) {
      GOPHER_LOG(Debug, "Using all defaults for HttpCodecFilter");
      return defaults;
    }

    // Merge config with defaults
    json::JsonValue result = defaults;
    for (const auto& key : config.keys()) {
      result.set(key, config[key]);
    }

    GOPHER_LOG(Debug, "Applied defaults to HttpCodecFilter configuration");
    return result;
  }

  mutable FilterFactoryMetadata metadata_;
};

// Register the factory with the filter registry
REGISTER_FILTER_FACTORY(HttpCodecFilterFactory, "http_codec")

}  // namespace filter
}  // namespace mcp