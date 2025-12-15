/**
 * @file core_filter_factories.cc
 * @brief Implementation of core filter factory registration
 *
 * This file provides explicit registration functions for all core filters
 * to ensure they are linked correctly in static builds. The registration
 * itself happens via static initializers in each factory file, but this
 * file ensures the linker includes those translation units.
 */

#include "mcp/filter/core_filter_factories.h"

#include "mcp/filter/filter_registry.h"
#include "mcp/logging/log_macros.h"

#define GOPHER_LOG_COMPONENT "filter.registry"

namespace mcp {
namespace filter {

// External references to force linker to include filter factory TUs.
// Use weak linkage where available so optional filters can be omitted
// without triggering unresolved symbol errors.
extern "C" {
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) void* http_codec_filter_registrar_ref;
__attribute__((weak)) void* sse_codec_filter_registrar_ref;
__attribute__((weak)) void* json_rpc_dispatcher_registrar_ref;
__attribute__((weak)) void* rate_limit_filter_registrar_ref;
__attribute__((weak)) void* circuit_breaker_filter_registrar_ref;
__attribute__((weak)) void* metrics_filter_registrar_ref;
__attribute__((weak)) void* request_logger_registrar_ref;
#else
extern void* http_codec_filter_registrar_ref;
extern void* sse_codec_filter_registrar_ref;
extern void* json_rpc_dispatcher_registrar_ref;
extern void* rate_limit_filter_registrar_ref;
extern void* circuit_breaker_filter_registrar_ref;
extern void* metrics_filter_registrar_ref;
extern void* request_logger_registrar_ref;
#endif
}

void registerRequestLoggerFactory();

void registerAllCoreFilters() {
  // Reference the sentinel symbols to force linker to include the translation
  // units
  volatile void* refs[] = {
      http_codec_filter_registrar_ref,      sse_codec_filter_registrar_ref,
      json_rpc_dispatcher_registrar_ref,    rate_limit_filter_registrar_ref,
      circuit_breaker_filter_registrar_ref, metrics_filter_registrar_ref,
      request_logger_registrar_ref};
  (void)refs;  // Prevent compiler optimization

  // Explicitly call all registration functions
  // This ensures filters are registered even when static initializers don't run
  registerHttpCodecFilterFactory();
  registerSseCodecFilterFactory();
  registerJsonRpcDispatcherFilterFactory();
  registerRateLimitFilterFactory();
  registerCircuitBreakerFilterFactory();
  registerMetricsFilterFactory();
  registerRequestLoggerFactory();

  // Count how many factories are registered
  auto factory_count = FilterRegistry::instance().listContextFactories().size();
  GOPHER_LOG(
      Debug,
      "Core filter registration complete, registry has {} context factories",
      factory_count);

  // Also log traditional factories if any
  auto traditional_count = FilterRegistry::instance().listFactories().size();
  if (traditional_count > 0) {
    GOPHER_LOG(Debug, "Registry also has {} traditional factories",
               traditional_count);
  }
}

}  // namespace filter
}  // namespace mcp
