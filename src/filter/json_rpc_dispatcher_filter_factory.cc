/**
 * @file json_rpc_dispatcher_filter_factory.cc
 * @brief Factory function for JSON-RPC dispatcher filter
 *
 * Provides context-aware factory function for creating JSON-RPC dispatcher filters
 * in config-driven filter chains.
 */

#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/filter/filter_context.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/json/json_bridge.h"

namespace mcp {
namespace filter {

/**
 * Factory function for JSON-RPC dispatcher filter
 *
 * @param context Filter creation context
 * @param config Filter configuration (JSON)
 * @return Shared pointer to created JSON-RPC dispatcher filter
 */
network::FilterSharedPtr createJsonRpcDispatcherFilter(
    const FilterCreationContext& context,
    const json::JsonValue& config) {

  return std::make_shared<JsonRpcProtocolFilter>(context, config);
}

/**
 * Register JSON-RPC dispatcher filter factory with registry
 * This function is called during static initialization
 */
void registerJsonRpcDispatcherFilterFactory() {
  BasicFilterMetadata metadata;
  metadata.name = "json_rpc.dispatcher";
  metadata.version = "1.0.0";
  metadata.description = "JSON-RPC dispatcher filter for parsing and routing JSON-RPC messages";

  // Set default configuration
  // TODO: Read these configuration values from a configuration file or environment
  // instead of hardcoding them here
  json::JsonObjectBuilder default_config;
  default_config.add("use_framing", false);  // Whether to use length-prefixed framing
  default_config.add("max_message_size", 1048576);  // 1MB max message size
  default_config.add("enable_batching", true);       // Support JSON-RPC batch requests
  metadata.default_config = default_config.build();

  FilterRegistry::instance().registerContextFactory(
      "json_rpc.dispatcher",
      createJsonRpcDispatcherFilter,
      metadata);
}

// Static initializer to register the factory at startup
namespace {
struct JsonRpcDispatcherFilterRegistrar {
  JsonRpcDispatcherFilterRegistrar() {
    registerJsonRpcDispatcherFilterFactory();
  }
};
static JsonRpcDispatcherFilterRegistrar json_rpc_dispatcher_filter_registrar;
}

// Export for static linking - using magic number as sentinel value
extern "C" {
  void* json_rpc_dispatcher_registrar_ref = (void*)0xDEADBEEF;
}

}  // namespace filter
}  // namespace mcp