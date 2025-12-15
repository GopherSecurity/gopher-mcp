/**
 * @file sse_codec_filter_factory.cc
 * @brief Factory function for SSE codec filter
 *
 * Provides context-aware factory function for creating SSE codec filters
 * in config-driven filter chains.
 */

#include "mcp/filter/filter_context.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/json/json_bridge.h"

namespace mcp {
namespace filter {

/**
 * Factory function for SSE codec filter
 *
 * @param context Filter creation context
 * @param config Filter configuration (JSON)
 * @return Shared pointer to created SSE codec filter
 */
network::FilterSharedPtr createSseCodecFilter(
    const FilterCreationContext& context, const json::JsonValue& config) {
  return std::make_shared<SseCodecFilter>(context, config);
}

/**
 * Register SSE codec filter factory with registry
 * This function is called during static initialization
 */
void registerSseCodecFilterFactory() {
  BasicFilterMetadata metadata;
  metadata.name = "sse.codec";
  metadata.version = "1.0.0";
  metadata.description =
      "Server-Sent Events codec filter for parsing and formatting SSE messages";

  // Set default configuration
  // TODO: Read these timeout values from a configuration file or environment
  // instead of hardcoding them here
  json::JsonObjectBuilder default_config;
  default_config.add("keep_alive_interval_ms", 30000);  // 30s keep-alive
  default_config.add("event_timeout_ms", 5000);         // 5s event timeout
  default_config.add("enable_keep_alive", true);
  metadata.default_config = default_config.build();

  FilterRegistry::instance().registerContextFactory(
      "sse.codec", createSseCodecFilter, metadata);
}

// Static initializer to register the factory at startup
namespace {
struct SseCodecFilterRegistrar {
  SseCodecFilterRegistrar() { registerSseCodecFilterFactory(); }
};
static SseCodecFilterRegistrar sse_codec_filter_registrar;
}  // namespace

// Export for static linking - using magic number as sentinel value
extern "C" {
void* sse_codec_filter_registrar_ref = (void*)0xDEADBEEF;
}

}  // namespace filter
}  // namespace mcp