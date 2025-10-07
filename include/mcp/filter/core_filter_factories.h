/**
 * @file core_filter_factories.h
 * @brief Core filter factory registration functions
 *
 * This header declares the registration functions for the three core filters
 * (HTTP codec, SSE codec, JSON-RPC dispatcher) used in config-driven filter chains.
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

}  // namespace filter
}  // namespace mcp