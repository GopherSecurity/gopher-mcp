/**
 * @file json_value_converter.h
 * @brief Internal helper for converting between C API JSON and internal JsonValue
 * 
 * This is an internal header not exposed in public API. It provides conversion
 * between mcp_json_value_t (opaque handle) and mcp::json::JsonValue.
 */

#pragma once

#include "mcp/c_api/mcp_c_collections.h"
#include "mcp/json/json_bridge.h"

namespace mcp {
namespace c_api {
namespace internal {

/**
 * Convert C API JSON handle to internal JsonValue
 * @param json C API JSON handle
 * @return Internal JsonValue representation
 * @throws std::runtime_error if conversion fails
 */
json::JsonValue convertFromCApi(mcp_json_value_t json);

/**
 * Convert internal JsonValue to C API JSON handle
 * @param json Internal JsonValue
 * @return C API JSON handle (caller owns)
 */
mcp_json_value_t convertToCApi(const json::JsonValue& json);

/**
 * Normalize typed_config to config format
 * 
 * If a filter entry has typed_config with @type, this function:
 * - Removes the @type field
 * - Moves remaining fields to config
 * - Removes typed_config field
 * 
 * @param filter Filter configuration object to normalize (modified in place)
 * @return true if normalization was performed, false if no typed_config found
 */
bool normalizeTypedConfig(json::JsonValue& filter);

/**
 * Normalize an entire filter chain configuration
 * 
 * Processes all filters in the chain:
 * - Normalizes typed_config to config
 * - Ensures 'type' field exists (uses 'name' if needed)
 * - Validates required fields
 * 
 * @param config Chain configuration with 'filters' array
 * @return Number of filters normalized
 * @throws std::runtime_error if configuration is invalid
 */
size_t normalizeFilterChain(json::JsonValue& config);

}  // namespace internal
}  // namespace c_api
}  // namespace mcp