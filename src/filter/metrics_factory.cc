/**
 * @file metrics_factory.cc
 * @brief Factory implementation for metrics collection filter
 */

#include "mcp/filter/filter_registry.h"
#include "mcp/filter/metrics_filter.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"

#define GOPHER_LOG_COMPONENT "filter.factory.qos"

namespace mcp {
namespace filter {

/**
 * Factory for creating MetricsFilter instances
 * 
 * Configuration schema:
 * {
 *   "provider": "internal" | "prometheus" | "custom",  // Metrics provider (default: "internal")
 *   "rate_update_interval_seconds": number,   // Rate calculation interval (default: 1)
 *   "report_interval_seconds": number,        // Reporting interval (default: 10)
 *   "max_latency_threshold_ms": number,       // Latency alert threshold (default: 5000)
 *   "error_rate_threshold": number,           // Errors per minute threshold (default: 10)
 *   "bytes_threshold": number,                // Bytes threshold for alerts (default: 104857600)
 *   "track_methods": boolean,                 // Enable method-level tracking (default: true)
 *   "enable_histograms": boolean,             // Enable latency histograms (default: false)
 *   "prometheus_port": number,                // Prometheus metrics port (default: 9090)
 *   "prometheus_path": string,                // Prometheus metrics path (default: "/metrics")
 *   "custom_endpoint": string                 // Custom metrics endpoint URL (optional)
 * }
 */
class MetricsFilterFactory : public FilterFactory {
 public:
  MetricsFilterFactory() {
    // Initialize metadata
    metadata_.name = "metrics";
    metadata_.version = "1.0.0";
    metadata_.description = "Comprehensive metrics collection filter with multiple provider support";
    metadata_.dependencies = {"network", "json_rpc_protocol"};
    
    // Define configuration schema
    metadata_.config_schema = json::JsonObjectBuilder()
        .add("type", "object")
        .add("properties", json::JsonObjectBuilder()
            .add("provider", json::JsonObjectBuilder()
                .add("type", "string")
                .add("enum", json::JsonArrayBuilder()
                    .add("internal")
                    .add("prometheus")
                    .add("custom")
                    .build())
                .add("default", "internal")
                .add("description", "Metrics provider backend")
                .build())
            .add("rate_update_interval_seconds", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1)
                .add("maximum", 60)
                .add("default", 1)
                .add("description", "Interval for rate calculations in seconds")
                .build())
            .add("report_interval_seconds", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1)
                .add("maximum", 3600)
                .add("default", 10)
                .add("description", "Metrics reporting interval in seconds")
                .build())
            .add("max_latency_threshold_ms", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 100)
                .add("maximum", 60000)
                .add("default", 5000)
                .add("description", "Maximum latency threshold in milliseconds")
                .build())
            .add("error_rate_threshold", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1)
                .add("maximum", 1000)
                .add("default", 10)
                .add("description", "Error rate threshold (errors per minute)")
                .build())
            .add("bytes_threshold", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1024)
                .add("maximum", 10737418240.0)  // 10GB
                .add("default", 104857600)  // 100MB
                .add("description", "Bytes threshold for alerts")
                .build())
            .add("track_methods", json::JsonObjectBuilder()
                .add("type", "boolean")
                .add("default", true)
                .add("description", "Enable per-method metrics tracking")
                .build())
            .add("enable_histograms", json::JsonObjectBuilder()
                .add("type", "boolean")
                .add("default", false)
                .add("description", "Enable latency histogram collection")
                .build())
            .add("prometheus_port", json::JsonObjectBuilder()
                .add("type", "integer")
                .add("minimum", 1024)
                .add("maximum", 65535)
                .add("default", 9090)
                .add("description", "Port for Prometheus metrics endpoint")
                .build())
            .add("prometheus_path", json::JsonObjectBuilder()
                .add("type", "string")
                .add("default", "/metrics")
                .add("description", "Path for Prometheus metrics endpoint")
                .build())
            .add("custom_endpoint", json::JsonObjectBuilder()
                .add("type", "string")
                .add("description", "Custom metrics endpoint URL")
                .build())
            .build())
        .add("additionalProperties", false)
        .build();
    
    GOPHER_LOG(Debug, "MetricsFilterFactory initialized");
  }

  ~MetricsFilterFactory() {
    GOPHER_LOG(Debug, "MetricsFilterFactory destroyed");
  }

  network::FilterSharedPtr createFilter(const json::JsonValue& config) const override {
    GOPHER_LOG(Info, "Creating MetricsFilter instance");
    
    // Apply defaults if needed
    auto final_config = applyDefaults(config);
    
    // Validate configuration
    if (!validateConfig(final_config)) {
      GOPHER_LOG(Error, "Invalid configuration for MetricsFilter");
      throw std::runtime_error("Invalid MetricsFilter configuration");
    }
    
    // Extract configuration values
    std::string provider = final_config["provider"].getString("internal");
    int rate_update_interval = final_config["rate_update_interval_seconds"].getInt(1);
    int report_interval = final_config["report_interval_seconds"].getInt(10);
    int max_latency_threshold = final_config["max_latency_threshold_ms"].getInt(5000);
    int error_rate_threshold = final_config["error_rate_threshold"].getInt(10);
    int64_t bytes_threshold = final_config["bytes_threshold"].getInt64(104857600);
    bool track_methods = final_config["track_methods"].getBool(true);
    bool enable_histograms = final_config["enable_histograms"].getBool(false);
    
    // Log basic configuration
    GOPHER_LOG(Debug, "MetricsFilter config: provider=%s rate_update=%ds report=%ds "
               "latency_threshold=%dms error_threshold=%d/min bytes_threshold=%lld",
               provider.c_str(), rate_update_interval, report_interval,
               max_latency_threshold, error_rate_threshold, bytes_threshold);
    
    GOPHER_LOG(Debug, "MetricsFilter features: track_methods=%s histograms=%s",
               track_methods ? "enabled" : "disabled",
               enable_histograms ? "enabled" : "disabled");
    
    // Provider-specific configuration
    if (provider == "prometheus") {
      int prometheus_port = final_config["prometheus_port"].getInt(9090);
      std::string prometheus_path = final_config["prometheus_path"].getString("/metrics");
      GOPHER_LOG(Debug, "Prometheus provider config: port=%d path=%s", 
                 prometheus_port, prometheus_path.c_str());
      
      if (prometheus_port < 1024) {
        GOPHER_LOG(Warning, "Prometheus port %d is privileged - may require elevated permissions", 
                   prometheus_port);
      }
    } else if (provider == "custom") {
      if (final_config.contains("custom_endpoint")) {
        std::string custom_endpoint = final_config["custom_endpoint"].getString("");
        GOPHER_LOG(Debug, "Custom provider endpoint: %s", custom_endpoint.c_str());
      } else {
        GOPHER_LOG(Warning, "Custom provider selected but no custom_endpoint specified");
      }
    }
    
    // Performance considerations
    if (enable_histograms && report_interval < 10) {
      GOPHER_LOG(Warning, "Histograms enabled with short report interval (%ds) - may impact performance",
                 report_interval);
    }
    
    if (track_methods && rate_update_interval == 1) {
      GOPHER_LOG(Debug, "Method-level tracking with 1s rate updates - suitable for detailed monitoring");
    }
    
    // Note: The actual filter creation requires MetricsFilter::MetricsCallbacks
    // which should be injected through a different mechanism (e.g., connection context)
    // For now, we return nullptr as a placeholder - the real implementation
    // would get these dependencies from the filter chain context
    
    GOPHER_LOG(Warning, "MetricsFilter creation requires runtime dependencies (callbacks)");
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
        .add("provider", "internal")
        .add("rate_update_interval_seconds", 1)
        .add("report_interval_seconds", 10)
        .add("max_latency_threshold_ms", 5000)
        .add("error_rate_threshold", 10)
        .add("bytes_threshold", 104857600)  // 100MB
        .add("track_methods", true)
        .add("enable_histograms", false)
        .add("prometheus_port", 9090)
        .add("prometheus_path", "/metrics")
        .build();
  }

  bool validateConfig(const json::JsonValue& config) const override {
    if (!config.isObject()) {
      GOPHER_LOG(Error, "MetricsFilter config must be an object");
      return false;
    }
    
    // Validate provider if present
    if (config.contains("provider")) {
      std::string provider = config["provider"].getString("");
      if (provider != "internal" && provider != "prometheus" && provider != "custom") {
        GOPHER_LOG(Error, "Invalid provider '%s' - must be one of: internal, prometheus, custom", 
                   provider.c_str());
        return false;
      }
      
      // Validate provider-specific fields
      if (provider == "custom" && !config.contains("custom_endpoint")) {
        GOPHER_LOG(Warning, "Custom provider selected but custom_endpoint not specified");
      }
    }
    
    // Validate rate_update_interval_seconds if present
    if (config.contains("rate_update_interval_seconds")) {
      if (!config["rate_update_interval_seconds"].isInteger()) {
        GOPHER_LOG(Error, "rate_update_interval_seconds must be an integer");
        return false;
      }
      int interval = config["rate_update_interval_seconds"].getInt();
      if (interval < 1 || interval > 60) {
        GOPHER_LOG(Error, "rate_update_interval_seconds %d out of range [1, 60]", interval);
        return false;
      }
    }
    
    // Validate report_interval_seconds if present
    if (config.contains("report_interval_seconds")) {
      if (!config["report_interval_seconds"].isInteger()) {
        GOPHER_LOG(Error, "report_interval_seconds must be an integer");
        return false;
      }
      int interval = config["report_interval_seconds"].getInt();
      if (interval < 1 || interval > 3600) {
        GOPHER_LOG(Error, "report_interval_seconds %d out of range [1, 3600]", interval);
        return false;
      }
    }
    
    // Validate max_latency_threshold_ms if present
    if (config.contains("max_latency_threshold_ms")) {
      if (!config["max_latency_threshold_ms"].isInteger()) {
        GOPHER_LOG(Error, "max_latency_threshold_ms must be an integer");
        return false;
      }
      int threshold = config["max_latency_threshold_ms"].getInt();
      if (threshold < 100 || threshold > 60000) {
        GOPHER_LOG(Error, "max_latency_threshold_ms %d out of range [100, 60000]", threshold);
        return false;
      }
    }
    
    // Validate error_rate_threshold if present
    if (config.contains("error_rate_threshold")) {
      if (!config["error_rate_threshold"].isInteger()) {
        GOPHER_LOG(Error, "error_rate_threshold must be an integer");
        return false;
      }
      int threshold = config["error_rate_threshold"].getInt();
      if (threshold < 1 || threshold > 1000) {
        GOPHER_LOG(Error, "error_rate_threshold %d out of range [1, 1000]", threshold);
        return false;
      }
    }
    
    // Validate bytes_threshold if present
    if (config.contains("bytes_threshold")) {
      if (!config["bytes_threshold"].isInteger()) {
        GOPHER_LOG(Error, "bytes_threshold must be an integer");
        return false;
      }
      int64_t threshold = config["bytes_threshold"].getInt64();
      if (threshold < 1024 || threshold > 10737418240LL) {  // 1KB to 10GB
        GOPHER_LOG(Error, "bytes_threshold %lld out of range [1024, 10737418240]", threshold);
        return false;
      }
    }
    
    // Validate prometheus_port if present
    if (config.contains("prometheus_port")) {
      if (!config["prometheus_port"].isInteger()) {
        GOPHER_LOG(Error, "prometheus_port must be an integer");
        return false;
      }
      int port = config["prometheus_port"].getInt();
      if (port < 1024 || port > 65535) {
        GOPHER_LOG(Error, "prometheus_port %d out of range [1024, 65535]", port);
        return false;
      }
    }
    
    // Validate prometheus_path if present
    if (config.contains("prometheus_path")) {
      if (!config["prometheus_path"].isString()) {
        GOPHER_LOG(Error, "prometheus_path must be a string");
        return false;
      }
      std::string path = config["prometheus_path"].getString();
      if (path.empty() || path[0] != '/') {
        GOPHER_LOG(Error, "prometheus_path must start with '/' (got '%s')", path.c_str());
        return false;
      }
    }
    
    // Validate custom_endpoint if present
    if (config.contains("custom_endpoint")) {
      if (!config["custom_endpoint"].isString()) {
        GOPHER_LOG(Error, "custom_endpoint must be a string");
        return false;
      }
      std::string endpoint = config["custom_endpoint"].getString();
      if (endpoint.empty()) {
        GOPHER_LOG(Error, "custom_endpoint cannot be empty");
        return false;
      }
    }
    
    // Validate boolean fields
    if (config.contains("track_methods") && !config["track_methods"].isBoolean()) {
      GOPHER_LOG(Error, "track_methods must be a boolean");
      return false;
    }
    
    if (config.contains("enable_histograms") && !config["enable_histograms"].isBoolean()) {
      GOPHER_LOG(Error, "enable_histograms must be a boolean");
      return false;
    }
    
    // Warn about boundary values
    if (config.contains("max_latency_threshold_ms")) {
      int threshold = config["max_latency_threshold_ms"].getInt();
      if (threshold < 1000) {
        GOPHER_LOG(Warning, "max_latency_threshold_ms %dms is very low - may trigger frequent alerts", 
                   threshold);
      } else if (threshold > 30000) {
        GOPHER_LOG(Warning, "max_latency_threshold_ms %dms is very high - may miss performance issues", 
                   threshold);
      }
    }
    
    if (config.contains("report_interval_seconds")) {
      int interval = config["report_interval_seconds"].getInt();
      if (interval < 5) {
        GOPHER_LOG(Warning, "report_interval_seconds %d is very short - may impact performance", interval);
      }
    }
    
    if (config.contains("bytes_threshold")) {
      int64_t threshold = config["bytes_threshold"].getInt64();
      if (threshold > 1073741824LL) {  // 1GB
        GOPHER_LOG(Warning, "bytes_threshold %lld is very high - alerts may be delayed", threshold);
      }
    }
    
    // Check logical consistency
    if (config.contains("rate_update_interval_seconds") && config.contains("report_interval_seconds")) {
      int rate_update = config["rate_update_interval_seconds"].getInt();
      int report = config["report_interval_seconds"].getInt();
      if (rate_update > report) {
        GOPHER_LOG(Warning, "rate_update_interval_seconds (%d) > report_interval_seconds (%d) - rate calculations may be inaccurate",
                   rate_update, report);
      }
    }
    
    GOPHER_LOG(Debug, "MetricsFilter configuration validated successfully");
    return true;
  }

 private:
  json::JsonValue applyDefaults(const json::JsonValue& config) const {
    auto defaults = getDefaultConfig();
    if (!config.isObject()) {
      GOPHER_LOG(Debug, "Using all defaults for MetricsFilter");
      return defaults;
    }
    
    // Merge config with defaults
    json::JsonValue result = defaults;
    for (const auto& key : config.keys()) {
      result.set(key, config[key]);
    }
    
    GOPHER_LOG(Debug, "Applied defaults to MetricsFilter configuration");
    return result;
  }

  mutable FilterFactoryMetadata metadata_;
};

/**
 * Explicit registration function for static linking support
 * This ensures the factory is registered even when static initializers don't run
 */
void registerMetricsFilterFactory() {
  static bool registered = false;
  if (!registered) {
    FilterRegistry::instance().registerFactory(
        "metrics",
        std::make_shared<MetricsFilterFactory>());
    registered = true;
    GOPHER_LOG(Debug, "MetricsFilterFactory explicitly registered");
  }
}

// Register the factory with the filter registry via static initializer
REGISTER_FILTER_FACTORY(MetricsFilterFactory, "metrics")

// Export for static linking - using magic number as sentinel value
extern "C" {
  void* metrics_filter_registrar_ref = (void*)0xDEADBEEF;
}

}  // namespace filter
}  // namespace mcp
