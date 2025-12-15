/**
 * @file core_filter_factories.h
 * @brief Core filter factory registration functions
 *
 * This header declares the registration functions for the three core filters
 * (HTTP codec, SSE codec, JSON-RPC dispatcher) used in config-driven filter
 * chains.
 */

#pragma once

namespace mcp {
namespace filter {

/**
 * Register HTTP codec filter factory with the global registry
 * This function should be called during application initialization
 */
void registerHttpCodecFilterFactory();

/**
 * Register SSE codec filter factory with the global registry
 * This function should be called during application initialization
 */
void registerSseCodecFilterFactory();

/**
 * Register JSON-RPC dispatcher filter factory with the global registry
 * This function should be called during application initialization
 */
void registerJsonRpcDispatcherFilterFactory();

/**
 * Register rate limit filter factory with the global registry
 * This function should be called during application initialization
 */
void registerRateLimitFilterFactory();

/**
 * Register circuit breaker filter factory with the global registry
 * This function should be called during application initialization
 */
void registerCircuitBreakerFilterFactory();

/**
 * Register metrics filter factory with the global registry
 * This function should be called during application initialization
 */
void registerMetricsFilterFactory();

/**
 * Register all built-in core filters
 *
 * This function ensures filter factory registrars are referenced,
 * preventing linker dead-code elimination in static builds.
 * The actual registration happens via static initializers.
 *
 * Safe to call multiple times.
 */
void registerAllCoreFilters();

}  // namespace filter
}  // namespace mcp