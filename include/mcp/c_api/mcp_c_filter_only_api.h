/**
 * @file mcp_c_filter_only_api.h
 * @brief Filter-Only C API for Hybrid MCP SDK Integration
 *
 * This header provides a minimal, filter-only C API designed for hybrid
 * integration where the official MCP SDK handles protocol operations and
 * Gopher-MCP C++ filters provide enterprise-grade filtering capabilities.
 *
 * Key Features:
 * - Canonical filter chain configuration (shared with native stack)
 * - Zero protocol dependencies (SDK handles all protocol logic)
 * - Validation with detailed diagnostics
 * - Dynamic filter enable/disable
 * - Statistics and metrics export
 * - Thread-safe dispatcher-based execution
 *
 * Architecture:
 * ```
 * ┌──────────────────────────────────────┐
 * │  Official MCP SDK (TypeScript)       │  ← Protocol Layer
 * │  - JSON-RPC handling                 │
 * │  - Transport management              │
 * └────────────┬─────────────────────────┘
 *              │ FFI (Koffi)
 * ┌────────────▼─────────────────────────┐
 * │  Filter-Only C API (this file)       │  ← Filter Layer
 * │  - Rate limiting                     │
 * │  - Circuit breaker                   │
 * │  - Metrics                           │
 * └────────────┬─────────────────────────┘
 *              │
 * ┌────────────▼─────────────────────────┐
 * │  Gopher-MCP C++ Filters              │  ← Implementation
 * └──────────────────────────────────────┘
 * ```
 *
 * Configuration Format (Canonical):
 * ```json
 * {
 *   "listeners": [{
 *     "name": "hybrid_listener",
 *     "address": {"socket_address": {"address": "127.0.0.1", "port_value": 0}},
 *     "filter_chains": [{
 *       "name": "default",
 *       "filters": [
 *         {"name": "rate", "type": "rate_limiter", "config": {"requests_per_second": 100}},
 *         {"name": "breaker", "type": "circuit_breaker", "config": {"failure_threshold": 5}},
 *         {"name": "metrics", "type": "metrics", "config": {"export_port": 9090}}
 *       ]
 *     }]
 *   }]
 * }
 * ```
 *
 * Thread Safety:
 * - All operations execute in dispatcher thread context
 * - Callbacks are invoked in dispatcher thread
 * - No manual synchronization needed
 *
 * Memory Management:
 * - Chains created with mcp_chain_* functions must be released
 * - Validation/assembly result structs must be freed with *_free() functions
 * - JSON values must be freed with mcp_json_free()
 *
 * @see docs/filter_only_api_guide.md for detailed usage
 * @see AGENT_PHASE1_CAPI.md for design rationale
 */

#ifndef MCP_C_FILTER_ONLY_API_H
#define MCP_C_FILTER_ONLY_API_H

#include "mcp_c_filter_chain.h"
#include "mcp_c_filter_api.h"
#include "mcp_c_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Core Types for Filter-Only Operations
 * ============================================================================
 *
 * These types are re-exported from mcp_c_filter_chain.h for convenience.
 */

/** Opaque handle to filter chain (re-exported) */
typedef mcp_filter_chain_t mcp_filter_only_chain_t;

/** Chain statistics (re-exported) */
typedef mcp_chain_stats_t mcp_filter_only_stats_t;

/** Validation result with errors and warnings (re-exported) */
typedef mcp_chain_validation_result_t mcp_filter_only_validation_result_t;

/** Assembly result with diagnostics (re-exported) */
typedef mcp_chain_assembly_result_t mcp_filter_only_assembly_result_t;

/* ============================================================================
 * Configuration Validation
 * ============================================================================
 *
 * Validate filter chain configurations before creating chains. This allows
 * catching configuration errors early and surfacing validation warnings to
 * the application layer.
 */

/**
 * Validate filter chain configuration from JSON
 *
 * Validates a canonical filter chain configuration using assembler semantics.
 * Returns detailed validation diagnostics including errors and warnings.
 *
 * @param json_config   Canonical filter chain JSON configuration
 * @param result        Output validation result (must be freed with
 *                      mcp_filter_only_validation_result_free)
 *
 * @return MCP_OK if validation succeeded (check result->valid for outcome),
 *         error code otherwise
 *
 * @note This function does NOT require a dispatcher handle
 * @note Caller must free result with mcp_filter_only_validation_result_free()
 *
 * Example:
 * ```c
 * mcp_json_value_t config = load_config_from_file("filters.json");
 * mcp_filter_only_validation_result_t result;
 *
 * if (mcp_filter_only_validate_json(config, &result) == MCP_OK) {
 *     if (result.valid) {
 *         printf("Config is valid\n");
 *         if (result.warning_count > 0) {
 *             printf("Warnings:\n");
 *             for (size_t i = 0; i < result.warning_count; i++) {
 *                 printf("  - %s\n", result.warnings[i]);
 *             }
 *         }
 *     } else {
 *         printf("Config is invalid:\n");
 *         for (size_t i = 0; i < result.error_count; i++) {
 *             printf("  - %s\n", result.errors[i]);
 *         }
 *     }
 * }
 *
 * mcp_filter_only_validation_result_free(&result);
 * mcp_json_free(config);
 * ```
 */
static inline mcp_result_t mcp_filter_only_validate_json(
    mcp_json_value_t json_config,
    mcp_filter_only_validation_result_t* result) MCP_NOEXCEPT {
  return mcp_chain_validate_json(json_config, result);
}

/**
 * Validate filter chain configuration from C struct
 *
 * @param config  Filter chain configuration structure
 * @param result  Output validation result (must be freed)
 *
 * @return MCP_OK if validation succeeded, error code otherwise
 *
 * @see mcp_filter_only_validate_json for usage example
 */
static inline mcp_result_t mcp_filter_only_validate_config(
    const mcp_filter_chain_config_t* config,
    mcp_filter_only_validation_result_t* result) MCP_NOEXCEPT {
  return mcp_chain_validate_config(config, result);
}

/**
 * Free validation result structure
 *
 * Releases all memory associated with a validation result, including error
 * and warning arrays.
 *
 * @param result  Validation result to free (can be NULL)
 *
 * @note Safe to call with NULL pointer
 * @note After calling, result struct is zeroed and invalid
 */
static inline void mcp_filter_only_validation_result_free(
    mcp_filter_only_validation_result_t* result) MCP_NOEXCEPT {
  mcp_chain_validation_result_free(result);
}

/* ============================================================================
 * Filter Chain Creation and Assembly
 * ============================================================================
 *
 * Create filter chains from canonical configurations. Assembly functions
 * provide detailed diagnostics about what filters were created and any
 * warnings encountered during assembly.
 */

/**
 * Assemble filter chain from JSON configuration with diagnostics
 *
 * Creates a filter chain from canonical JSON configuration while capturing
 * detailed assembly diagnostics. This is the recommended way to create chains
 * as it provides visibility into what filters were created and any warnings.
 *
 * @param dispatcher   Event dispatcher handle (required)
 * @param json_config  Canonical filter chain JSON configuration
 * @param result       Output assembly result (must be freed with
 *                     mcp_filter_only_assembly_result_free)
 *
 * @return MCP_OK if assembly was attempted (check result->success for outcome),
 *         error code otherwise
 *
 * @note MUST be called from dispatcher thread
 * @note Caller must free result with mcp_filter_only_assembly_result_free()
 * @note On success, caller owns result->chain and must release it
 *
 * Example:
 * ```c
 * mcp_filter_only_assembly_result_t result;
 * mcp_json_value_t config = load_config();
 *
 * if (mcp_filter_only_assemble_from_json(dispatcher, config, &result) == MCP_OK) {
 *     if (result.success) {
 *         printf("Created chain with %zu filters:\n", result.created_filter_count);
 *         for (size_t i = 0; i < result.created_filter_count; i++) {
 *             printf("  - %s\n", result.created_filters[i]);
 *         }
 *
 *         // Use the chain
 *         mcp_filter_only_chain_t chain = result.chain;
 *         result.chain = 0;  // Transfer ownership
 *
 *         // ... use chain ...
 *
 *         mcp_filter_only_chain_release(chain);
 *     } else {
 *         printf("Assembly failed: %s\n", result.error_message);
 *     }
 * }
 *
 * mcp_filter_only_assembly_result_free(&result);
 * mcp_json_free(config);
 * ```
 */
static inline mcp_result_t mcp_filter_only_assemble_from_json(
    mcp_dispatcher_t dispatcher,
    mcp_json_value_t json_config,
    mcp_filter_only_assembly_result_t* result) MCP_NOEXCEPT {
  return mcp_chain_assemble_from_json(dispatcher, json_config, result);
}

/**
 * Assemble filter chain from C struct configuration with diagnostics
 *
 * @param dispatcher  Event dispatcher handle
 * @param config      Filter chain configuration structure
 * @param result      Output assembly result (must be freed)
 *
 * @return MCP_OK if assembly was attempted, error code otherwise
 *
 * @see mcp_filter_only_assemble_from_json for usage example
 */
static inline mcp_result_t mcp_filter_only_assemble_from_config(
    mcp_dispatcher_t dispatcher,
    const mcp_filter_chain_config_t* config,
    mcp_filter_only_assembly_result_t* result) MCP_NOEXCEPT {
  return mcp_chain_assemble_from_config(dispatcher, config, result);
}

/**
 * Create filter chain from JSON configuration (simple version)
 *
 * Simplified chain creation without detailed diagnostics. Use this when you
 * don't need assembly details.
 *
 * @param dispatcher   Event dispatcher handle
 * @param json_config  Canonical filter chain JSON configuration
 *
 * @return Filter chain handle on success, 0 on error
 *
 * @note MUST be called from dispatcher thread
 * @note Caller must release returned chain with mcp_filter_only_chain_release()
 *
 * Example:
 * ```c
 * mcp_json_value_t config = load_config();
 * mcp_filter_only_chain_t chain = mcp_filter_only_chain_create_from_json(dispatcher, config);
 *
 * if (chain) {
 *     // Use chain
 *     // ...
 *
 *     mcp_filter_only_chain_release(chain);
 * }
 *
 * mcp_json_free(config);
 * ```
 */
static inline mcp_filter_only_chain_t mcp_filter_only_chain_create_from_json(
    mcp_dispatcher_t dispatcher,
    mcp_json_value_t json_config) MCP_NOEXCEPT {
  return mcp_chain_create_from_json(dispatcher, json_config);
}

/**
 * Free assembly result structure
 *
 * Releases all memory associated with an assembly result. The chain handle
 * ownership is NOT transferred - caller must still release the chain separately.
 *
 * @param result  Assembly result to free (can be NULL)
 *
 * @note Safe to call with NULL pointer
 * @note Does NOT release result->chain (caller retains ownership)
 */
static inline void mcp_filter_only_assembly_result_free(
    mcp_filter_only_assembly_result_t* result) MCP_NOEXCEPT {
  mcp_chain_assembly_result_free(result);
}

/* ============================================================================
 * Filter Chain Lifecycle Management
 * ============================================================================
 *
 * Manage filter chain lifetime. Chains use reference counting internally,
 * so release decrements the reference count and destroys when count reaches zero.
 */

/**
 * Retain filter chain (increment reference count)
 *
 * Increments the reference count on a filter chain. Use when sharing chains
 * across multiple owners.
 *
 * @param chain  Filter chain handle
 *
 * @note Safe to call with 0 handle (no-op)
 */
static inline void mcp_filter_only_chain_retain(
    mcp_filter_only_chain_t chain) MCP_NOEXCEPT {
  mcp_filter_chain_retain(chain);
}

/**
 * Release filter chain (decrement reference count)
 *
 * Decrements the reference count on a filter chain. When count reaches zero,
 * the chain is destroyed and all associated resources are freed.
 *
 * @param chain  Filter chain handle
 *
 * @note Safe to call with 0 handle (no-op)
 * @note After the last release, the handle is invalid
 */
static inline void mcp_filter_only_chain_release(
    mcp_filter_only_chain_t chain) MCP_NOEXCEPT {
  mcp_filter_chain_release(chain);
}

/**
 * Clone filter chain
 *
 * Creates a deep copy of a filter chain, including all filters and configuration.
 * Useful for creating per-connection chains from a template.
 *
 * @param chain  Filter chain handle to clone
 *
 * @return New filter chain handle on success, 0 on error
 *
 * @note MUST be called from dispatcher thread
 * @note Caller must release returned chain with mcp_filter_only_chain_release()
 *
 * Example:
 * ```c
 * // Create template chain once
 * mcp_filter_only_chain_t template_chain = create_template_chain();
 *
 * // Clone for each connection
 * for (int i = 0; i < num_connections; i++) {
 *     mcp_filter_only_chain_t conn_chain = mcp_filter_only_chain_clone(template_chain);
 *     if (conn_chain) {
 *         // Use connection-specific chain
 *         attach_to_connection(conn_chain);
 *     }
 * }
 *
 * mcp_filter_only_chain_release(template_chain);
 * ```
 */
static inline mcp_filter_only_chain_t mcp_filter_only_chain_clone(
    mcp_filter_only_chain_t chain) MCP_NOEXCEPT {
  return mcp_chain_clone(chain);
}

/* ============================================================================
 * Runtime Filter Control
 * ============================================================================
 *
 * Enable or disable individual filters at runtime without recreating the chain.
 * Useful for A/B testing, gradual rollouts, or dynamic configuration changes.
 */

/**
 * Enable or disable a specific filter in the chain
 *
 * Dynamically enables or disables a filter without recreating the chain.
 * Disabled filters are skipped during message processing.
 *
 * @param chain        Filter chain handle
 * @param filter_name  Name of filter to enable/disable
 * @param enabled      MCP_TRUE to enable, MCP_FALSE to disable
 *
 * @return MCP_OK on success, error code otherwise
 *
 * @note MUST be called from dispatcher thread
 * @note Filter name must match the name in the configuration
 *
 * Example:
 * ```c
 * // Temporarily disable rate limiting for a VIP user
 * mcp_filter_only_set_filter_enabled(chain, "rate", MCP_FALSE);
 *
 * // Process VIP request
 * // ...
 *
 * // Re-enable for normal users
 * mcp_filter_only_set_filter_enabled(chain, "rate", MCP_TRUE);
 * ```
 */
static inline mcp_result_t mcp_filter_only_set_filter_enabled(
    mcp_filter_only_chain_t chain,
    const char* filter_name,
    mcp_bool_t enabled) MCP_NOEXCEPT {
  return mcp_chain_set_filter_enabled(chain, filter_name, enabled);
}

/* ============================================================================
 * Configuration Export/Import
 * ============================================================================
 *
 * Export filter chain configuration to JSON for persistence, logging, or
 * creating derivative configurations.
 */

/**
 * Export filter chain configuration to JSON
 *
 * Exports the current filter chain configuration as canonical JSON. Useful for:
 * - Persisting runtime configuration
 * - Debugging and logging
 * - Creating derivative configurations
 * - Cloning chains to different dispatchers
 *
 * @param chain  Filter chain handle
 *
 * @return JSON configuration value on success, NULL on error
 *
 * @note Caller must free returned JSON with mcp_json_free()
 * @note Exported JSON uses canonical format (listeners → filter_chains → filters)
 *
 * Example:
 * ```c
 * // Export chain configuration for logging
 * mcp_json_value_t exported = mcp_filter_only_chain_export_to_json(chain);
 * if (exported) {
 *     char* json_str = mcp_json_to_string(exported);
 *     printf("Current chain config: %s\n", json_str);
 *     free(json_str);
 *     mcp_json_free(exported);
 * }
 * ```
 */
static inline mcp_json_value_t mcp_filter_only_chain_export_to_json(
    mcp_filter_only_chain_t chain) MCP_NOEXCEPT {
  return mcp_chain_export_to_json(chain);
}

/* ============================================================================
 * Statistics and Metrics
 * ============================================================================
 *
 * Retrieve operational statistics and metrics from filter chains. Metrics
 * include throughput, latency, error counts, and filter-specific stats.
 */

/**
 * Get filter chain statistics
 *
 * Retrieves aggregate statistics for all filters in the chain, including:
 * - Total messages processed
 * - Error counts
 * - Bypassed messages (disabled filters)
 * - Average and max latency
 * - Throughput
 * - Active filter count
 *
 * @param chain  Filter chain handle
 * @param stats  Output statistics structure
 *
 * @return MCP_OK on success, error code otherwise
 *
 * @note Thread-safe (can be called from any thread)
 *
 * Example:
 * ```c
 * mcp_filter_only_stats_t stats;
 * if (mcp_filter_only_get_stats(chain, &stats) == MCP_OK) {
 *     printf("Chain Statistics:\n");
 *     printf("  Total processed: %lu\n", stats.total_processed);
 *     printf("  Errors: %lu\n", stats.total_errors);
 *     printf("  Bypassed: %lu\n", stats.total_bypassed);
 *     printf("  Avg latency: %.2f ms\n", stats.avg_latency_ms);
 *     printf("  Max latency: %.2f ms\n", stats.max_latency_ms);
 *     printf("  Throughput: %.2f Mbps\n", stats.throughput_mbps);
 *     printf("  Active filters: %u\n", stats.active_filters);
 * }
 * ```
 */
static inline mcp_result_t mcp_filter_only_get_stats(
    mcp_filter_only_chain_t chain,
    mcp_filter_only_stats_t* stats) MCP_NOEXCEPT {
  return mcp_chain_get_stats(chain, stats);
}

/* ============================================================================
 * Advanced Operations (Optional)
 * ============================================================================
 *
 * Advanced chain operations for sophisticated use cases. Most applications
 * won't need these functions.
 */

/**
 * Pause chain execution
 *
 * Pauses all message processing in the chain. Messages are queued until resumed.
 *
 * @param chain  Filter chain handle
 * @return MCP_OK on success
 *
 * @note MUST be called from dispatcher thread
 * @note Use with caution - can cause message backlog
 */
static inline mcp_result_t mcp_filter_only_chain_pause(
    mcp_filter_only_chain_t chain) MCP_NOEXCEPT {
  return mcp_chain_pause(chain);
}

/**
 * Resume chain execution
 *
 * Resumes message processing after a pause.
 *
 * @param chain  Filter chain handle
 * @return MCP_OK on success
 *
 * @note MUST be called from dispatcher thread
 */
static inline mcp_result_t mcp_filter_only_chain_resume(
    mcp_filter_only_chain_t chain) MCP_NOEXCEPT {
  return mcp_chain_resume(chain);
}

/**
 * Reset chain to initial state
 *
 * Resets all chain statistics and state to initial values.
 *
 * @param chain  Filter chain handle
 * @return MCP_OK on success
 *
 * @note MUST be called from dispatcher thread
 */
static inline mcp_result_t mcp_filter_only_chain_reset(
    mcp_filter_only_chain_t chain) MCP_NOEXCEPT {
  return mcp_chain_reset(chain);
}

/**
 * Get chain state
 *
 * Returns the current state of the filter chain.
 *
 * @param chain  Filter chain handle
 * @return Current chain state
 *
 * Possible states:
 * - MCP_CHAIN_STATE_IDLE: Chain is idle, ready to process
 * - MCP_CHAIN_STATE_PROCESSING: Chain is actively processing messages
 * - MCP_CHAIN_STATE_PAUSED: Chain is paused
 * - MCP_CHAIN_STATE_ERROR: Chain encountered an error
 * - MCP_CHAIN_STATE_COMPLETED: Chain completed processing
 */
static inline mcp_chain_state_t mcp_filter_only_chain_get_state(
    mcp_filter_only_chain_t chain) MCP_NOEXCEPT {
  return mcp_chain_get_state(chain);
}

#ifdef __cplusplus
}
#endif

#endif /* MCP_C_FILTER_ONLY_API_H */
