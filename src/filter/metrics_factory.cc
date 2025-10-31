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
    std::cout << "[MetricsFactory] Creating MetricsFilter instance - START" << std::endl;
    GOPHER_LOG(Info, "Creating MetricsFilter instance");

    // Apply defaults if needed
    std::cout << "[MetricsFactory] Applying defaults to config" << std::endl;
    auto final_config = applyDefaults(config);
    std::cout << "[MetricsFactory] Defaults applied successfully" << std::endl;
    
    // Validate configuration
    std::cout << "[MetricsFactory] Validating configuration" << std::endl;
    if (!validateConfig(final_config)) {
      std::cout << "[MetricsFactory] Configuration validation FAILED!" << std::endl;
      GOPHER_LOG(Error, "Invalid configuration for MetricsFilter");
      throw std::runtime_error("Invalid MetricsFilter configuration");
    }
    std::cout << "[MetricsFactory] Configuration validated successfully" << std::endl;
    
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

    // Create default no-op callbacks that will be replaced by actual callbacks
    // set via setMetricsCallbacks() on the AdvancedFilterChain
    class DefaultMetricsCallbacks : public filter::MetricsFilter::MetricsCallbacks {
     public:
      void onMetricsUpdate(const filter::ConnectionMetrics& metrics) override {
        // No-op by default - callbacks will be set later via C API
      }
      void onThresholdExceeded(const std::string& metric_name,
                               uint64_t value,
                               uint64_t threshold) override {
        // No-op by default - callbacks will be set later via C API
      }
    };

    auto callbacks = std::make_shared<DefaultMetricsCallbacks>();

    // Build MetricsFilter::Config struct
    filter::MetricsFilter::Config metrics_config;
    metrics_config.rate_update_interval = std::chrono::seconds(rate_update_interval);
    metrics_config.report_interval = std::chrono::seconds(report_interval);
    metrics_config.max_latency_threshold_ms = max_latency_threshold;
    metrics_config.error_rate_threshold = error_rate_threshold;
    metrics_config.bytes_threshold = bytes_threshold;
    metrics_config.track_methods = track_methods;
    metrics_config.enable_histograms = enable_histograms;

    // Create the filter with the prepared config and default callbacks
    std::cout << "[MetricsFactory] Creating MetricsFilter instance with callbacks" << std::endl;
    auto filter = std::make_shared<filter::MetricsFilter>(callbacks, metrics_config);
    std::cout << "[MetricsFactory] MetricsFilter instance created successfully!" << std::endl;
    return filter;
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

    auto validateIntegerField = [&](const char* field_name,
                                    int min_value,
                                    int max_value) -> bool {
      if (!config.contains(field_name)) {
        return true;
      }

      const auto& field = config[field_name];
      double numeric_value = 0.0;

      if (field.isInteger()) {
        numeric_value = static_cast<double>(field.getInt64());
      } else if (field.isNumber()) {
        numeric_value = field.getFloat();
      } else {
        GOPHER_LOG(Error, "%s must be numeric", field_name);
        return false;
      }

      double rounded = std::round(numeric_value);
      if (std::fabs(numeric_value - rounded) > 1e-6) {
        GOPHER_LOG(Error, "%s must be an integer value (got %f)",
                   field_name, numeric_value);
        return false;
      }

      int value = static_cast<int>(rounded);
      if (value < min_value || value > max_value) {
        GOPHER_LOG(Error, "%s %d out of range [%d, %d]",
                   field_name, value, min_value, max_value);
        return false;
      }

      return true;
    };

    auto validateInt64Field = [&](const char* field_name,
                                  int64_t min_value,
                                  int64_t max_value) -> bool {
      if (!config.contains(field_name)) {
        return true;
      }

      const auto& field = config[field_name];
      long double numeric_value = 0.0L;

      if (field.isInteger()) {
        numeric_value = static_cast<long double>(field.getInt64());
      } else if (field.isNumber()) {
        numeric_value = static_cast<long double>(field.getFloat());
      } else {
        GOPHER_LOG(Error, "%s must be numeric", field_name);
        return false;
      }

      long double rounded = std::round(numeric_value);
      if (std::fabsl(numeric_value - rounded) > 1e-6L) {
        GOPHER_LOG(Error, "%s must be an integer value (got %Lf)",
                   field_name, numeric_value);
        return false;
      }

      int64_t value = static_cast<int64_t>(rounded);
      if (value < min_value || value > max_value) {
        GOPHER_LOG(Error,
                   "%s %lld out of range [%lld, %lld]",
                   field_name,
                   static_cast<long long>(value),
                   static_cast<long long>(min_value),
                   static_cast<long long>(max_value));
        return false;
      }

      return true;
    };

    if (config.contains("provider")) {
      std::string provider = config["provider"].getString("");
      if (provider != "internal" && provider != "prometheus" &&
          provider != "custom") {
        GOPHER_LOG(Error,
                   "Invalid provider '%s' - must be one of: internal, prometheus, custom",
                   provider.c_str());
        return false;
      }

      if (provider == "custom" && !config.contains("custom_endpoint")) {
        GOPHER_LOG(Warning,
                   "Custom provider selected but custom_endpoint not specified");
      }
    }

    if (!validateIntegerField("rate_update_interval_seconds", 1, 60)) {
      return false;
    }

    if (!validateIntegerField("report_interval_seconds", 1, 3600)) {
      return false;
    }

    if (!validateIntegerField("max_latency_threshold_ms", 100, 60000)) {
      return false;
    }

    if (!validateIntegerField("error_rate_threshold", 1, 1000)) {
      return false;
    }

    if (!validateInt64Field("bytes_threshold", 1024, 10737418240LL)) {
      return false;
    }

    if (config.contains("track_methods") &&
        !config["track_methods"].isBoolean()) {
      GOPHER_LOG(Error, "track_methods must be a boolean");
      return false;
    }

    if (config.contains("enable_histograms") &&
        !config["enable_histograms"].isBoolean()) {
      GOPHER_LOG(Error, "enable_histograms must be a boolean");
      return false;
    }

    if (!validateIntegerField("prometheus_port", 1024, 65535)) {
      return false;
    }

    if (config.contains("prometheus_path")) {
      if (!config["prometheus_path"].isString()) {
        GOPHER_LOG(Error, "prometheus_path must be a string");
        return false;
      }
      std::string path = config["prometheus_path"].getString();
      if (path.empty() || path.front() != '/') {
        GOPHER_LOG(Error,
                   "prometheus_path must start with '/' (got '%s')",
                   path.c_str());
        return false;
      }
    }

    if (config.contains("custom_endpoint") &&
        !config["custom_endpoint"].isString()) {
      GOPHER_LOG(Error, "custom_endpoint must be a string");
      return false;
    }

    return true;
  }


 private:
  json::JsonValue applyDefaults(const json::JsonValue& config) const {
    auto defaults = getDefaultConfig();
    if (!config.isObject()) {
      GOPHER_LOG(Debug, "Using all defaults for MetricsFilter");
      return defaults;
    }

    json::JsonValue result = defaults;
    for (const auto& key : config.keys()) {
      const auto& value = config[key];
      if (value.isBoolean()) {
        result.set(key, json::JsonValue(value.getBool()));
      } else if (value.isInteger()) {
        result.set(key, json::JsonValue(value.getInt64()));
      } else if (value.isNumber()) {
        // Preserve floating point precision if provided
        result.set(key, json::JsonValue(value.getFloat()));
      } else if (value.isString()) {
        result.set(key, json::JsonValue(value.getString()));
      } else {
        result.set(key, value);
      }
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
    auto& registry = FilterRegistry::instance();

    auto factory = std::make_shared<MetricsFilterFactory>();
    if (!registry.registerFactory("metrics", factory)) {
      GOPHER_LOG(Error, "Failed to register traditional metrics factory (duplicate?)");
    }

    BasicFilterMetadata basic_metadata;
    basic_metadata.name = "metrics";
    basic_metadata.version = factory->getMetadata().version;
    basic_metadata.description = factory->getMetadata().description;
    basic_metadata.default_config = factory->getDefaultConfig();

    if (!registry.registerContextFactory(
            "metrics",
            [factory](const FilterCreationContext& context,
                      const json::JsonValue& cfg) -> network::FilterSharedPtr {
              (void)context;  // Currently no context-specific behaviour needed
              return factory->createFilter(cfg);
            },
            basic_metadata)) {
      GOPHER_LOG(Error, "Failed to register context-aware metrics factory (duplicate?)");
    }

    registered = true;
    GOPHER_LOG(Debug, "MetricsFilterFactory explicitly registered");
  }
}

struct MetricsFilterRegistrar {
  MetricsFilterRegistrar() { registerMetricsFilterFactory(); }
};

static MetricsFilterRegistrar metrics_filter_registrar_instance;

// Register the factory with the filter registry via static initializer
REGISTER_FILTER_FACTORY(MetricsFilterFactory, "metrics")

// Export for static linking - using magic number as sentinel value
extern "C" {
  void* metrics_filter_registrar_ref = (void*)0xDEADBEEF;
}

}  // namespace filter
}  // namespace mcp
