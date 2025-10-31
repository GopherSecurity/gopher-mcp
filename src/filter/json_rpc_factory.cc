/**
 * @file json_rpc_factory.cc
 * @brief Factory implementation for JSON-RPC protocol filter
 */

#include "mcp/filter/filter_registry.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"

#define GOPHER_LOG_COMPONENT "filter.factory.core"

namespace mcp {
namespace filter {

/**
 * Factory for creating JsonRpcProtocolFilter instances
 * 
 * Configuration schema:
 * {
 *   "mode": "server" | "client",        // Operation mode (default: "server")
 *   "use_framing": boolean,             // Enable message framing (default: true)
 *   "max_message_size": number,         // Max message size in bytes (default: 1048576)
 *   "batch_enabled": boolean,           // Enable batch requests (default: true)
 *   "batch_limit": number,              // Max requests in batch (default: 100)
 *   "strict_mode": boolean,             // Strict JSON-RPC 2.0 compliance (default: true)
 *   "timeout_ms": number,               // Request timeout in ms (default: 30000)
 *   "validate_params": boolean          // Validate method parameters (default: true)
 * }
 */
class JsonRpcProtocolFilterFactory : public FilterFactory {
 public:
  JsonRpcProtocolFilterFactory() {
    // Initialize metadata
    metadata_.name = "json_rpc";
    metadata_.version = "1.0.0";
    metadata_.description = "JSON-RPC 2.0 protocol filter for MCP communication";
    metadata_.dependencies = {"network", "event"};
    
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
                .add("description", "Operation mode: server or client")
                .build())
            .add("use_framing", json::JsonObjectBuilder()
                .add("type", "boolean")
                .add("default", true)
                .add("description", "Enable message framing for stdio transport")
                .build())
            .add("max_message_size", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1024)
                .add("maximum", 10485760)  // 10MB
                .add("default", 1048576)  // 1MB
                .add("description", "Maximum message size in bytes")
                .build())
            .add("batch_enabled", json::JsonObjectBuilder()
                .add("type", "boolean")
                .add("default", true)
                .add("description", "Enable JSON-RPC batch requests")
                .build())
            .add("batch_limit", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1)
                .add("maximum", 1000)
                .add("default", 100)
                .add("description", "Maximum requests in a batch")
                .build())
            .add("strict_mode", json::JsonObjectBuilder()
                .add("type", "boolean")
                .add("default", true)
                .add("description", "Enforce strict JSON-RPC 2.0 compliance")
                .build())
            .add("timeout_ms", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1000)
                .add("maximum", 300000)
                .add("default", 30000)
                .add("description", "Request timeout in milliseconds")
                .build())
            .add("validate_params", json::JsonObjectBuilder()
                .add("type", "boolean")
                .add("default", true)
                .add("description", "Validate method parameters against schema")
                .build())
            .build())
        .add("additionalProperties", false)
        .build();
    
    GOPHER_LOG(Debug, "JsonRpcProtocolFilterFactory initialized");
  }

  ~JsonRpcProtocolFilterFactory() {
    GOPHER_LOG(Debug, "JsonRpcProtocolFilterFactory destroyed");
  }

  network::FilterSharedPtr createFilter(const json::JsonValue& config) const override {
    GOPHER_LOG(Info, "Creating JsonRpcProtocolFilter instance");
    
    // Apply defaults if needed
    auto final_config = applyDefaults(config);
    
    // Validate configuration
    if (!validateConfig(final_config)) {
      GOPHER_LOG(Error, "Invalid configuration for JsonRpcProtocolFilter");
      throw std::runtime_error("Invalid JsonRpcProtocolFilter configuration");
    }
    
    // Extract configuration values
    std::string mode = final_config["mode"].getString("server");
    bool is_server = (mode == "server");
    bool use_framing = final_config["use_framing"].getBool(true);
    int max_message_size = final_config["max_message_size"].getInt(1048576);
    bool batch_enabled = final_config["batch_enabled"].getBool(true);
    int batch_limit = final_config["batch_limit"].getInt(100);
    bool strict_mode = final_config["strict_mode"].getBool(true);
    int timeout_ms = final_config["timeout_ms"].getInt(30000);
    bool validate_params = final_config["validate_params"].getBool(true);
    
    // Log configuration
    GOPHER_LOG(Debug, "JsonRpcProtocolFilter config: mode=%s framing=%s max_size=%d batch=%s/%d strict=%s timeout=%dms validate=%s",
               mode.c_str(),
               use_framing ? "enabled" : "disabled",
               max_message_size,
               batch_enabled ? "enabled" : "disabled",
               batch_limit,
               strict_mode ? "yes" : "no",
               timeout_ms,
               validate_params ? "yes" : "no");
    
    // Note: The actual filter creation requires MessageHandler and Dispatcher
    // which should be injected through a different mechanism (e.g., connection context)
    // For now, we return nullptr as a placeholder - the real implementation
    // would get these dependencies from the filter chain context
    
    GOPHER_LOG(Warning, "JsonRpcProtocolFilter creation requires runtime dependencies (handler, dispatcher)");
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
        .add("use_framing", true)
        .add("max_message_size", 1048576)
        .add("batch_enabled", true)
        .add("batch_limit", 100)
        .add("strict_mode", true)
        .add("timeout_ms", 30000)
        .add("validate_params", true)
        .build();
  }

  bool validateConfig(const json::JsonValue& config) const override {
    if (!config.isObject()) {
      GOPHER_LOG(Error, "JsonRpcProtocolFilter config must be an object");
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
    
    // Validate max_message_size if present
    if (config.contains("max_message_size")) {
      if (!config["max_message_size"].isInteger()) {
        GOPHER_LOG(Error, "max_message_size must be an integer");
        return false;
      }
      int size = config["max_message_size"].getInt();
      if (size < 1024 || size > 10485760) {
        GOPHER_LOG(Error, "max_message_size %d out of range [1024, 10485760]", size);
        return false;
      }
    }
    
    // Validate batch_limit if present
    if (config.contains("batch_limit")) {
      if (!config["batch_limit"].isInteger()) {
        GOPHER_LOG(Error, "batch_limit must be an integer");
        return false;
      }
      int limit = config["batch_limit"].getInt();
      if (limit < 1 || limit > 1000) {
        GOPHER_LOG(Error, "batch_limit %d out of range [1, 1000]", limit);
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
    const char* bool_fields[] = {"use_framing", "batch_enabled", "strict_mode", "validate_params"};
    for (const char* field : bool_fields) {
      if (config.contains(field) && !config[field].isBoolean()) {
        GOPHER_LOG(Error, "%s must be a boolean", field);
        return false;
      }
    }
    
    GOPHER_LOG(Debug, "JsonRpcProtocolFilter configuration validated successfully");
    return true;
  }

 private:
  json::JsonValue applyDefaults(const json::JsonValue& config) const {
    auto defaults = getDefaultConfig();
    if (!config.isObject()) {
      GOPHER_LOG(Debug, "Using all defaults for JsonRpcProtocolFilter");
      return defaults;
    }
    
    // Merge config with defaults
    json::JsonValue result = defaults;
    for (const auto& key : config.keys()) {
      result.set(key, config[key]);
    }
    
    GOPHER_LOG(Debug, "Applied defaults to JsonRpcProtocolFilter configuration");
    return result;
  }

  mutable FilterFactoryMetadata metadata_;
};

// Register the factory with the filter registry
REGISTER_FILTER_FACTORY(JsonRpcProtocolFilterFactory, "json_rpc")

}  // namespace filter
}  // namespace mcp