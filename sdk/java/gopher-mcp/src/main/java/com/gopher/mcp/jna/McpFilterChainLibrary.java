package com.gopher.mcp.jna;

import com.gopher.mcp.jna.type.filter.McpProtocolMetadata;
import com.gopher.mcp.jna.type.filter.chain.*;
import com.sun.jna.Callback;
import com.sun.jna.Library;
import com.sun.jna.NativeLong;
import com.sun.jna.Pointer;
import com.sun.jna.ptr.LongByReference;
import com.sun.jna.ptr.PointerByReference;

/**
 * JNA interface for the MCP Filter Chain API (mcp_c_filter_chain.h).
 * This interface provides comprehensive filter chain composition and management,
 * including dynamic routing, conditional execution, and performance optimization.
 *
 * Features:
 * - Dynamic filter composition
 * - Conditional filter execution
 * - Parallel filter processing
 * - Filter routing and branching
 * - Performance monitoring
 *
 * All methods are ordered exactly as they appear in mcp_c_filter_chain.h
 */
public interface McpFilterChainLibrary extends Library {

    // Load the native library
    McpFilterChainLibrary INSTANCE = NativeLibraryLoader.loadLibrary(McpFilterChainLibrary.class);

    /* ============================================================================
     * Chain Types and Enumerations (from mcp_c_filter_chain.h lines 32-63)
     * ============================================================================
     */

    // Chain execution mode
    int MCP_CHAIN_MODE_SEQUENTIAL = 0;   // Execute filters in order
    int MCP_CHAIN_MODE_PARALLEL = 1;     // Execute filters in parallel
    int MCP_CHAIN_MODE_CONDITIONAL = 2;  // Execute based on conditions
    int MCP_CHAIN_MODE_PIPELINE = 3;     // Pipeline mode with buffering

    // Chain routing strategy
    int MCP_ROUTING_ROUND_ROBIN = 0;   // Round-robin distribution
    int MCP_ROUTING_LEAST_LOADED = 1;  // Route to least loaded filter
    int MCP_ROUTING_HASH_BASED = 2;    // Hash-based routing
    int MCP_ROUTING_PRIORITY = 3;      // Priority-based routing
    int MCP_ROUTING_CUSTOM = 99;       // Custom routing function

    // Filter match condition
    int MCP_MATCH_ALL = 0;     // Match all conditions
    int MCP_MATCH_ANY = 1;     // Match any condition
    int MCP_MATCH_NONE = 2;    // Match no conditions
    int MCP_MATCH_CUSTOM = 99; // Custom match function

    // Chain state
    int MCP_CHAIN_STATE_IDLE = 0;
    int MCP_CHAIN_STATE_PROCESSING = 1;
    int MCP_CHAIN_STATE_PAUSED = 2;
    int MCP_CHAIN_STATE_ERROR = 3;
    int MCP_CHAIN_STATE_COMPLETED = 4;

    /* ============================================================================
     * Callback Types (lines 123-140)
     * ============================================================================
     */

    /**
     * Custom routing function callback
     */
    interface MCP_ROUTING_FUNCTION_T extends Callback {
        long invoke(long buffer, McpFilterNode[] nodes, NativeLong node_count, Pointer user_data);
    }

    /**
     * Chain event callback
     */
    interface MCP_CHAIN_EVENT_CB extends Callback {
        void invoke(long chain, int old_state, int new_state, Pointer user_data);
    }

    /**
     * Filter match function callback
     */
    interface MCP_FILTER_MATCH_CB extends Callback {
        byte invoke(long buffer, McpProtocolMetadata.ByReference metadata, Pointer user_data);
    }

    /* ============================================================================
     * Advanced Chain Builder (lines 145-200)
     * ============================================================================
     */

    /**
     * Create chain builder with configuration (line 152)
     * @param dispatcher Event dispatcher
     * @param config Chain configuration
     * @return Builder handle or NULL on error
     */
    Pointer mcp_chain_builder_create_ex(Pointer dispatcher, McpChainConfig.ByReference config);

    /**
     * Add filter node to chain (line 161)
     * @param builder Chain builder
     * @param node Filter node configuration
     * @return MCP_OK on success
     */
    int mcp_chain_builder_add_node(Pointer builder, McpFilterNode.ByReference node);

    /**
     * Add conditional filter (line 172)
     * @param builder Chain builder
     * @param condition Condition for filter execution
     * @param filter Filter to execute if condition met
     * @return MCP_OK on success
     */
    int mcp_chain_builder_add_conditional(Pointer builder,
                                          McpFilterCondition.ByReference condition,
                                          long filter);

    /**
     * Add parallel filter group (line 184)
     * @param builder Chain builder
     * @param filters Array of filters to run in parallel
     * @param count Number of filters
     * @return MCP_OK on success
     */
    int mcp_chain_builder_add_parallel_group(Pointer builder, long[] filters, NativeLong count);

    /**
     * Set custom routing function (line 196)
     * @param builder Chain builder
     * @param router Custom routing function
     * @param user_data User data for router
     * @return MCP_OK on success
     */
    int mcp_chain_builder_set_router(Pointer builder, MCP_ROUTING_FUNCTION_T router, Pointer user_data);

    /* ============================================================================
     * Chain Management (lines 205-266)
     * ============================================================================
     */

    /**
     * Get chain state (line 211)
     * @param chain Filter chain
     * @return Current chain state
     */
    int mcp_chain_get_state(long chain);

    /**
     * Pause chain execution (line 219)
     * @param chain Filter chain
     * @return MCP_OK on success
     */
    int mcp_chain_pause(long chain);

    /**
     * Resume chain execution (line 226)
     * @param chain Filter chain
     * @return MCP_OK on success
     */
    int mcp_chain_resume(long chain);

    /**
     * Reset chain to initial state (line 233)
     * @param chain Filter chain
     * @return MCP_OK on success
     */
    int mcp_chain_reset(long chain);

    /**
     * Enable/disable filter in chain (line 242)
     * @param chain Filter chain
     * @param filter_name Name of filter to enable/disable
     * @param enabled Enable flag
     * @return MCP_OK on success
     */
    int mcp_chain_set_filter_enabled(long chain, String filter_name, byte enabled);

    /**
     * Get chain statistics (line 253)
     * @param chain Filter chain
     * @param stats Output statistics
     * @return MCP_OK on success
     */
    int mcp_chain_get_stats(long chain, McpChainStats.ByReference stats);

    /**
     * Set chain event callback (line 263)
     * @param chain Filter chain
     * @param callback Event callback
     * @param user_data User data
     * @return MCP_OK on success
     */
    int mcp_chain_set_event_callback(long chain, MCP_CHAIN_EVENT_CB callback, Pointer user_data);

    /* ============================================================================
     * Dynamic Chain Composition (lines 270-308)
     * ============================================================================
     */

    /**
     * Create dynamic chain from JSON configuration (line 278)
     * @param dispatcher Event dispatcher
     * @param json_config JSON configuration
     * @return Chain handle or 0 on error
     */
    long mcp_chain_create_from_json(Pointer dispatcher, Pointer json_config);

    /**
     * Export chain configuration to JSON (line 286)
     * @param chain Filter chain
     * @return JSON configuration or NULL on error
     */
    Pointer mcp_chain_export_to_json(long chain);

    /**
     * Clone a filter chain (line 294)
     * @param chain Source chain
     * @return Cloned chain handle or 0 on error
     */
    long mcp_chain_clone(long chain);

    /**
     * Merge two chains (line 304)
     * @param chain1 First chain
     * @param chain2 Second chain
     * @param mode Merge mode (sequential, parallel)
     * @return Merged chain handle or 0 on error
     */
    long mcp_chain_merge(long chain1, long chain2, int mode);

    /* ============================================================================
     * Chain Router (lines 312-353)
     * ============================================================================
     */

    /**
     * Create chain router (line 321)
     * @param config Router configuration
     * @return Router handle or NULL on error
     */
    Pointer mcp_chain_router_create(McpRouterConfig.ByReference config);

    /**
     * Add route to router (line 331)
     * @param router Chain router
     * @param condition Match condition
     * @param chain Target chain
     * @return MCP_OK on success
     */
    int mcp_chain_router_add_route(Pointer router, MCP_FILTER_MATCH_CB condition, long chain);

    /**
     * Route buffer through appropriate chain (line 343)
     * @param router Chain router
     * @param buffer Buffer to route
     * @param metadata Protocol metadata
     * @return Selected chain or 0 if no match
     */
    long mcp_chain_router_route(Pointer router, long buffer, McpProtocolMetadata.ByReference metadata);

    /**
     * Destroy chain router (line 352)
     * @param router Chain router
     */
    void mcp_chain_router_destroy(Pointer router);

    /* ============================================================================
     * Chain Pool for Load Balancing (lines 357-408)
     * ============================================================================
     */

    /**
     * Create chain pool for load balancing (line 368)
     * @param base_chain Template chain
     * @param pool_size Number of chain instances
     * @param strategy Load balancing strategy
     * @return Pool handle or NULL on error
     */
    Pointer mcp_chain_pool_create(long base_chain, NativeLong pool_size, int strategy);

    /**
     * Get next chain from pool (line 378)
     * @param pool Chain pool
     * @return Next chain based on strategy
     */
    long mcp_chain_pool_get_next(Pointer pool);

    /**
     * Return chain to pool (line 386)
     * @param pool Chain pool
     * @param chain Chain to return
     */
    void mcp_chain_pool_return(Pointer pool, long chain);

    /**
     * Get pool statistics (line 397)
     * @param pool Chain pool
     * @param active Output: active chains
     * @param idle Output: idle chains
     * @param total_processed Output: total processed
     * @return MCP_OK on success
     */
    int mcp_chain_pool_get_stats(Pointer pool, PointerByReference active,
                                 PointerByReference idle, LongByReference total_processed);

    /**
     * Destroy chain pool (line 407)
     * @param pool Chain pool
     */
    void mcp_chain_pool_destroy(Pointer pool);

    /* ============================================================================
     * Chain Optimization (lines 412-441)
     * ============================================================================
     */

    /**
     * Optimize chain by removing redundant filters (line 419)
     * @param chain Filter chain
     * @return MCP_OK on success
     */
    int mcp_chain_optimize(long chain);

    /**
     * Reorder filters for optimal performance (line 426)
     * @param chain Filter chain
     * @return MCP_OK on success
     */
    int mcp_chain_reorder_filters(long chain);

    /**
     * Profile chain performance (line 437)
     * @param chain Filter chain
     * @param test_buffer Test buffer for profiling
     * @param iterations Number of test iterations
     * @param report Output: performance report (JSON)
     * @return MCP_OK on success
     */
    int mcp_chain_profile(long chain, long test_buffer, NativeLong iterations,
                         PointerByReference report);

    /* ============================================================================
     * Chain Debugging (lines 445-473)
     * ============================================================================
     */

    /**
     * Enable chain tracing (line 453)
     * @param chain Filter chain
     * @param trace_level Trace level (0=off, 1=basic, 2=detailed)
     * @return MCP_OK on success
     */
    int mcp_chain_set_trace_level(long chain, int trace_level);

    /**
     * Dump chain structure (line 462)
     * @param chain Filter chain
     * @param format Output format ("text", "json", "dot")
     * @return String representation (must be freed)
     */
    String mcp_chain_dump(long chain, String format);

    /**
     * Validate chain configuration (line 471)
     * @param chain Filter chain
     * @param errors Output: validation errors (JSON)
     * @return MCP_OK if valid
     */
    int mcp_chain_validate(long chain, PointerByReference errors);
}
