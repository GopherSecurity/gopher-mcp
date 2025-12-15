/**
 * @file http_codec_filter_factory.cc
 * @brief Factory function for HTTP codec filter
 *
 * Provides context-aware factory function for creating HTTP codec filters
 * in config-driven filter chains.
 */

#include "mcp/filter/filter_context.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/json/json_bridge.h"

namespace mcp {
namespace filter {

/**
 * Factory function for HTTP codec filter
 *
 * @param context Filter creation context
 * @param config Filter configuration (JSON)
 * @return Shared pointer to created HTTP codec filter
 */
network::FilterSharedPtr createHttpCodecFilter(
    const FilterCreationContext& context, const json::JsonValue& config) {
  return std::make_shared<HttpCodecFilter>(context, config);
}

/**
 * Register HTTP codec filter factory with registry
 * This function is called during static initialization
 */
void registerHttpCodecFilterFactory() {
  BasicFilterMetadata metadata;
  metadata.name = "http.codec";
  metadata.version = "1.0.0";
  metadata.description =
      "HTTP/1.1 codec filter for parsing and formatting HTTP messages";

  // Set default configuration
  // TODO: Read these timeout values from a configuration file or environment
  // instead of hardcoding them here
  json::JsonObjectBuilder default_config;
  default_config.add("header_timeout_ms", 30000);
  default_config.add("body_timeout_ms", 60000);
  default_config.add("idle_timeout_ms", 120000);
  default_config.add("enable_keep_alive", true);
  metadata.default_config = default_config.build();

  FilterRegistry::instance().registerContextFactory(
      "http.codec", createHttpCodecFilter, metadata);
}

// Static initializer to register the factory at startup
namespace {
struct HttpCodecFilterRegistrar {
  HttpCodecFilterRegistrar() { registerHttpCodecFilterFactory(); }
};
static HttpCodecFilterRegistrar http_codec_filter_registrar;
}  // namespace

// Export for static linking - using magic number as sentinel value
extern "C" {
void* http_codec_filter_registrar_ref = (void*)0xDEADBEEF;
}

}  // namespace filter
}  // namespace mcp