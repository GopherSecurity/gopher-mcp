/**
 * @file mcp_c_filter_chain.h
 * @brief Advanced filter chain management for MCP Filter API
 *
 * This header provides comprehensive filter chain composition and management,
 * including dynamic routing, conditional execution, and performance
 * optimization.
 *
 * Features:
 * - Dynamic filter composition
 * - Conditional filter execution
 * - Parallel filter processing
 * - Filter routing and branching
 * - Performance monitoring
 */

#ifndef MCP_FILTER_CHAIN_H
#define MCP_FILTER_CHAIN_H

#include "mcp_c_filter_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Chain Types and Enumerations
 * ============================================================================
 */

// Chain execution mode
typedef enum {
  MCP_CHAIN_MODE_SEQUENTIAL = 0,   // Execute filters in order
  MCP_CHAIN_MODE_PARALLEL = 1,     // Execute filters in parallel
  MCP_CHAIN_MODE_CONDITIONAL = 2,  // Execute based on conditions
  MCP_CHAIN_MODE_PIPELINE = 3      // Pipeline mode with buffering
} mcp_chain_execution_mode_t;

// Chain routing strategy
typedef enum {
  MCP_ROUTING_ROUND_ROBIN = 0,   // Round-robin distribution
  MCP_ROUTING_LEAST_LOADED = 1,  // Route to least loaded filter
  MCP_ROUTING_HASH_BASED = 2,    // Hash-based routing
  MCP_ROUTING_PRIORITY = 3,      // Priority-based routing
  MCP_ROUTING_CUSTOM = 99        // Custom routing function
} mcp_routing_strategy_t;

// Filter match condition
typedef enum {
  MCP_MATCH_ALL = 0,     // Match all conditions
  MCP_MATCH_ANY = 1,     // Match any condition
  MCP_MATCH_NONE = 2,    // Match no conditions
  MCP_MATCH_CUSTOM = 99  // Custom match function
} mcp_match_condition_t;

// Chain state
typedef enum {
  MCP_CHAIN_STATE_IDLE = 0,
  MCP_CHAIN_STATE_PROCESSING = 1,
  MCP_CHAIN_STATE_PAUSED = 2,
  MCP_CHAIN_STATE_ERROR = 3,
  MCP_CHAIN_STATE_COMPLETED = 4
} mcp_chain_state_t;

/* ============================================================================
 * Data Structures
 * ============================================================================
 */

// Filter node in chain
typedef struct mcp_filter_node {
  mcp_filter_t filter;
  const char* name;
  uint32_t priority;
  mcp_bool_t enabled;
  mcp_bool_t bypass_on_error;
  mcp_json_value_t config;
} mcp_filter_node_t;

// Chain configuration
typedef struct mcp_chain_config {
  const char* name;
  mcp_chain_execution_mode_t mode;
  mcp_routing_strategy_t routing;
  uint32_t max_parallel;
  uint32_t buffer_size;
  uint32_t timeout_ms;
  mcp_bool_t stop_on_error;
} mcp_chain_config_t;

// Filter condition for conditional execution
typedef struct mcp_filter_condition {
  mcp_match_condition_t match_type;
  const char* field;
  const char* value;
  mcp_filter_t target_filter;
} mcp_filter_condition_t;

// Chain statistics
typedef struct mcp_chain_stats {
  uint64_t total_processed;
  uint64_t total_errors;
  uint64_t total_bypassed;
  double avg_latency_ms;
  double max_latency_ms;
  double throughput_mbps;
  uint32_t active_filters;
} mcp_chain_stats_t;

// Filter configuration entry used for assembler driven creation
typedef struct mcp_chain_filter_config_struct {
  const char* type;
  const char* name;
  mcp_json_value_t config;
  mcp_bool_t enabled;
  mcp_json_value_t enabled_when;
} mcp_chain_filter_config_t;

// High-level filter chain configuration for assembler consumption
typedef struct mcp_filter_chain_config {
  const char* name;
  const char* transport_type;
  const mcp_chain_filter_config_t* filters;
  size_t filter_count;
} mcp_filter_chain_config_t;

// Validation result exposed through the C ABI
typedef struct mcp_chain_validation_result {
  mcp_bool_t valid;
  size_t error_count;
  char** errors;
  size_t warning_count;
  char** warnings;
} mcp_chain_validation_result_t;

// Assembly result capturing diagnostics and the produced chain
typedef struct mcp_chain_assembly_result {
  mcp_bool_t success;
  mcp_filter_chain_t chain;
  char* error_message;
  size_t created_filter_count;
  char** created_filters;
  size_t warning_count;
  char** warnings;
} mcp_chain_assembly_result_t;

// Router configuration
typedef struct mcp_router_config {
  mcp_routing_strategy_t strategy;
  uint32_t hash_seed;
  mcp_map_t route_table;  // Map of conditions to chains
  void* custom_router_data;
} mcp_router_config_t;

/* ============================================================================
 * Callback Types
 * ============================================================================
 */

// Custom routing function
typedef mcp_filter_t (*mcp_routing_function_t)(mcp_buffer_handle_t buffer,
                                               const mcp_filter_node_t* nodes,
                                               size_t node_count,
                                               void* user_data);

// Chain event callback
typedef void (*mcp_chain_event_cb)(mcp_filter_chain_t chain,
                                   mcp_chain_state_t old_state,
                                   mcp_chain_state_t new_state,
                                   void* user_data);

// Filter match function
typedef mcp_bool_t (*mcp_filter_match_cb)(
    mcp_buffer_handle_t buffer,
    const mcp_protocol_metadata_t* metadata,
    void* user_data);

/* ============================================================================
 * Filter Result Types
 * ============================================================================
 */

// Filter decision enum
typedef enum {
  MCP_FILTER_DECISION_ALLOW = 0,      // Continue processing
  MCP_FILTER_DECISION_DENY = 1,       // Reject the message
  MCP_FILTER_DECISION_DELAY = 2,      // Delay processing
  MCP_FILTER_DECISION_QUEUE = 3,      // Queue for later
  MCP_FILTER_DECISION_TRANSFORM = 4   // Message was transformed
} mcp_filter_decision_t;

// Filter result structure
typedef struct mcp_filter_result {
  mcp_filter_decision_t decision;      // Filter decision
  char* transformed_message;           // Transformed message (NULL if unchanged)
  char* reason;                        // Reason for decision (NULL if not provided)
  uint32_t delay_ms;                   // Delay in milliseconds (0 if no delay)
  void* metadata;                      // Additional metadata (reserved for future use)
} mcp_filter_result_t;

/* ============================================================================
 * Async Request Queue Types
 * ============================================================================
 */

/**
 * Callback invoked when async filter processing completes
 * @param user_data User-provided data (e.g., callback ID encoded in Buffer)
 * @param result Filter result (NULL on error)
 * @param error Error information (NULL on success)
 */
typedef void (*mcp_filter_callback_t)(
    void* user_data,
    mcp_filter_result_t* result,
    mcp_error_t* error);

/* Status codes for async operations */
typedef enum {
  MCP_STATUS_OK = 0,                   // Request queued successfully
  MCP_STATUS_QUEUE_FULL = 1,          // Queue at capacity, try again
  MCP_STATUS_INVALID_ARGUMENT = 2,    // Invalid parameters
  MCP_STATUS_NOT_INITIALIZED = 3      // Chain not initialized (CRITICAL: must be present)
} mcp_status_t;

/* ============================================================================
 * Chain Management
 * ============================================================================
 */

/**
 * Get chain state
 * @param chain Filter chain
 * @return Current chain state
 */
MCP_API mcp_chain_state_t mcp_chain_get_state(mcp_filter_chain_t chain)
    MCP_NOEXCEPT;

/**
 * Pause chain execution
 * @param chain Filter chain
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_chain_pause(mcp_filter_chain_t chain) MCP_NOEXCEPT;

/**
 * Resume chain execution
 * @param chain Filter chain
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_chain_resume(mcp_filter_chain_t chain) MCP_NOEXCEPT;

/**
 * Reset chain to initial state
 * @param chain Filter chain
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_chain_reset(mcp_filter_chain_t chain) MCP_NOEXCEPT;

/**
 * Enable/disable filter in chain
 * @param chain Filter chain
 * @param filter_name Name of filter to enable/disable
 * @param enabled Enable flag
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_chain_set_filter_enabled(mcp_filter_chain_t chain,
                                                  const char* filter_name,
                                                  mcp_bool_t enabled)
    MCP_NOEXCEPT;

/**
 * Get chain statistics
 * @param chain Filter chain
 * @param stats Output statistics
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_chain_get_stats(mcp_filter_chain_t chain,
                                         mcp_chain_stats_t* stats) MCP_NOEXCEPT;

/**
 * Set chain event callback
 * @param chain Filter chain
 * @param callback Event callback
 * @param user_data User data
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_chain_set_event_callback(mcp_filter_chain_t chain,
                                                  mcp_chain_event_cb callback,
                                                  void* user_data) MCP_NOEXCEPT;

/**
 * Update filter chain dependencies with runtime services
 *
 * This function allows updating the callbacks and services that were injected
 * during chain creation. Use this when the chain was created with temporary
 * callbacks (like NullProtocolCallbacks) and you want to provide the real ones.
 *
 * Thread Safety: Must be called from dispatcher thread
 *
 * @param chain Filter chain handle
 * @param callbacks Protocol callbacks for filter event emission (BORROWED)
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_chain_update_dependencies(
    mcp_filter_chain_t chain,
    void* callbacks) MCP_NOEXCEPT;

/* ============================================================================
 * Filter Chain Lifecycle Management
 * ============================================================================
 */

/**
 * Initialize filter chain (must be called before processing)
 * @param chain Filter chain handle
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_filter_chain_initialize(
    mcp_filter_chain_t chain) MCP_NOEXCEPT;

/**
 * Shutdown filter chain (cleanup resources)
 * @param chain Filter chain handle
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_filter_chain_shutdown(
    mcp_filter_chain_t chain) MCP_NOEXCEPT;

/* ============================================================================
 * Async Filter Processing
 * ============================================================================
 */

/**
 * Submit incoming message for async processing
 * @param chain Filter chain handle
 * @param message_json JSON-encoded message
 * @param user_data User data passed to callback (e.g., Buffer with callback ID)
 * @param callback Callback invoked when processing completes
 * @param error Output error information
 * @return MCP_STATUS_OK if submitted, error code otherwise
 */
MCP_API mcp_status_t mcp_chain_submit_incoming(
    mcp_filter_chain_t chain,
    const char* message_json,
    void* user_data,
    mcp_filter_callback_t callback,
    mcp_error_t* error) MCP_NOEXCEPT;

/**
 * Submit outgoing message for async processing
 * @param chain Filter chain handle
 * @param message_json JSON-encoded message
 * @param user_data User data passed to callback (e.g., Buffer with callback ID)
 * @param callback Callback invoked when processing completes
 * @param error Output error information
 * @return MCP_STATUS_OK if submitted, error code otherwise
 */
MCP_API mcp_status_t mcp_chain_submit_outgoing(
    mcp_filter_chain_t chain,
    const char* message_json,
    void* user_data,
    mcp_filter_callback_t callback,
    mcp_error_t* error) MCP_NOEXCEPT;

/* ============================================================================
 * Dynamic Chain Composition
 * ============================================================================
 */

/**
 * Create dynamic chain from JSON configuration
 * @param dispatcher Event dispatcher
 * @param json_config JSON configuration
 * @return Chain handle or 0 on error
 */
MCP_API mcp_filter_chain_t mcp_chain_create_from_json(
    mcp_dispatcher_t dispatcher, mcp_json_value_t json_config) MCP_NOEXCEPT;

/**
 * Create filter chain from JSON configuration asynchronously.
 *
 * This function posts the chain creation to the dispatcher thread and invokes
 * the callback when complete. This is required because mcp_chain_create_from_json
 * enforces thread affinity (must be called on dispatcher thread).
 *
 * Thread-safe: Can be called from any thread.
 * The callback will be invoked on the dispatcher thread.
 *
 * @param dispatcher Dispatcher handle
 * @param config JSON configuration handle
 * @param callback Completion callback (chain_handle=0 indicates failure)
 *                 Signature: void callback(uint64_t chain_handle,
 *                                         int32_t error_code,
 *                                         const char* error_msg,
 *                                         void* user_data)
 * @param user_data User data passed to callback
 */
MCP_API void mcp_chain_create_from_json_async(
    mcp_dispatcher_t dispatcher,
    mcp_json_value_t config,
    void (*callback)(uint64_t chain_handle, int32_t error_code,
                     const char* error_msg, void* user_data),
    void* user_data
) MCP_NOEXCEPT;

/**
 * Export chain configuration to JSON
 * @param chain Filter chain
 * @return JSON configuration or NULL on error
 */
MCP_API mcp_json_value_t mcp_chain_export_to_json(mcp_filter_chain_t chain)
    MCP_NOEXCEPT;

/**
 * Clone a filter chain
 * @param chain Source chain
 * @return Cloned chain handle or 0 on error
 */
MCP_API mcp_filter_chain_t mcp_chain_clone(mcp_filter_chain_t chain)
    MCP_NOEXCEPT;

/**
 * Merge two chains
 * @param chain1 First chain
 * @param chain2 Second chain
 * @param mode Merge mode (sequential, parallel)
 * @return Merged chain handle or 0 on error
 */
MCP_API mcp_filter_chain_t mcp_chain_merge(mcp_filter_chain_t chain1,
                                           mcp_filter_chain_t chain2,
                                           mcp_chain_execution_mode_t mode)
    MCP_NOEXCEPT;

/* ============================================================================
 * Config-driven Assembler Helpers
 * ============================================================================
 */

/**
 * Validate a JSON configuration using the assembler semantics.
 */
MCP_API mcp_result_t mcp_chain_validate_json(
    mcp_json_value_t json_config,
    mcp_chain_validation_result_t* result) MCP_NOEXCEPT;

/**
 * Validate an in-memory configuration using the assembler semantics.
 */
MCP_API mcp_result_t mcp_chain_validate_config(
    const mcp_filter_chain_config_t* config,
    mcp_chain_validation_result_t* result) MCP_NOEXCEPT;

/**
 * Release memory associated with a validation result structure.
 */
MCP_API void mcp_chain_validation_result_free(
    mcp_chain_validation_result_t* result) MCP_NOEXCEPT;

/**
 * Assemble a chain from JSON configuration while capturing diagnostics.
 */
MCP_API mcp_result_t mcp_chain_assemble_from_json(
    mcp_dispatcher_t dispatcher,
    mcp_json_value_t json_config,
    mcp_chain_assembly_result_t* result) MCP_NOEXCEPT;

/**
 * Assemble a chain from in-memory configuration while capturing diagnostics.
 */
MCP_API mcp_result_t mcp_chain_assemble_from_config(
    mcp_dispatcher_t dispatcher,
    const mcp_filter_chain_config_t* config,
    mcp_chain_assembly_result_t* result) MCP_NOEXCEPT;

/**
 * Release memory associated with an assembly result structure. The caller
 * retains ownership of the produced chain handle.
 */
MCP_API void mcp_chain_assembly_result_free(
    mcp_chain_assembly_result_t* result) MCP_NOEXCEPT;

/* ============================================================================
 * Chain Router
 * ============================================================================
 */

typedef struct mcp_chain_router* mcp_chain_router_t;

/**
 * Create chain router
 * @param config Router configuration
 * @return Router handle or NULL on error
 */
MCP_API mcp_chain_router_t
mcp_chain_router_create(const mcp_router_config_t* config) MCP_NOEXCEPT;

/**
 * Add route to router
 * @param router Chain router
 * @param condition Match condition
 * @param chain Target chain
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_chain_router_add_route(mcp_chain_router_t router,
                                                mcp_filter_match_cb condition,
                                                mcp_filter_chain_t chain)
    MCP_NOEXCEPT;

/**
 * Route buffer through appropriate chain
 * @param router Chain router
 * @param buffer Buffer to route
 * @param metadata Protocol metadata
 * @return Selected chain or 0 if no match
 */
MCP_API mcp_filter_chain_t
mcp_chain_router_route(mcp_chain_router_t router,
                       mcp_buffer_handle_t buffer,
                       const mcp_protocol_metadata_t* metadata) MCP_NOEXCEPT;

/**
 * Destroy chain router
 * @param router Chain router
 */
MCP_API void mcp_chain_router_destroy(mcp_chain_router_t router) MCP_NOEXCEPT;

/* ============================================================================
 * Chain Pool for Load Balancing
 * ============================================================================
 */

typedef struct mcp_chain_pool* mcp_chain_pool_t;

/**
 * Create chain pool for load balancing
 * @param base_chain Template chain
 * @param pool_size Number of chain instances
 * @param strategy Load balancing strategy
 * @return Pool handle or NULL on error
 */
MCP_API mcp_chain_pool_t mcp_chain_pool_create(mcp_filter_chain_t base_chain,
                                               size_t pool_size,
                                               mcp_routing_strategy_t strategy)
    MCP_NOEXCEPT;

/**
 * Get next chain from pool
 * @param pool Chain pool
 * @return Next chain based on strategy
 */
MCP_API mcp_filter_chain_t mcp_chain_pool_get_next(mcp_chain_pool_t pool)
    MCP_NOEXCEPT;

/**
 * Return chain to pool
 * @param pool Chain pool
 * @param chain Chain to return
 */
MCP_API void mcp_chain_pool_return(mcp_chain_pool_t pool,
                                   mcp_filter_chain_t chain) MCP_NOEXCEPT;

/**
 * Get pool statistics
 * @param pool Chain pool
 * @param active Output: active chains
 * @param idle Output: idle chains
 * @param total_processed Output: total processed
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_chain_pool_get_stats(mcp_chain_pool_t pool,
                                              size_t* active,
                                              size_t* idle,
                                              uint64_t* total_processed)
    MCP_NOEXCEPT;

/**
 * Destroy chain pool
 * @param pool Chain pool
 */
MCP_API void mcp_chain_pool_destroy(mcp_chain_pool_t pool) MCP_NOEXCEPT;

/* ============================================================================
 * Chain Optimization
 * ============================================================================
 */

/**
 * Optimize chain by removing redundant filters
 * @param chain Filter chain
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_chain_optimize(mcp_filter_chain_t chain) MCP_NOEXCEPT;

/**
 * Reorder filters for optimal performance
 * @param chain Filter chain
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_chain_reorder_filters(mcp_filter_chain_t chain)
    MCP_NOEXCEPT;

/**
 * Profile chain performance
 * @param chain Filter chain
 * @param test_buffer Test buffer for profiling
 * @param iterations Number of test iterations
 * @param report Output: performance report (JSON)
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_chain_profile(mcp_filter_chain_t chain,
                                       mcp_buffer_handle_t test_buffer,
                                       size_t iterations,
                                       mcp_json_value_t* report) MCP_NOEXCEPT;

/* ============================================================================
 * Chain Debugging
 * ============================================================================
 */

/**
 * Enable chain tracing
 * @param chain Filter chain
 * @param trace_level Trace level (0=off, 1=basic, 2=detailed)
 * @return MCP_OK on success
 */
MCP_API mcp_result_t mcp_chain_set_trace_level(
    mcp_filter_chain_t chain, uint32_t trace_level) MCP_NOEXCEPT;

/**
 * Dump chain structure
 * @param chain Filter chain
 * @param format Output format ("text", "json", "dot")
 * @return String representation (must be freed)
 */
MCP_API char* mcp_chain_dump(mcp_filter_chain_t chain,
                             const char* format) MCP_NOEXCEPT;

/**
 * Validate chain configuration
 * @param chain Filter chain
 * @param errors Output: validation errors (JSON)
 * @return MCP_OK if valid
 */
MCP_API mcp_result_t mcp_chain_validate(mcp_filter_chain_t chain,
                                        mcp_json_value_t* errors) MCP_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* MCP_FILTER_CHAIN_H */
