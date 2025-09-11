package com.gopher.mcp.jna

import com.gopher.mcp.jna.type.filter.McpProtocolMetadata
import com.gopher.mcp.jna.type.filter.chain.*
import com.sun.jna.Callback
import com.sun.jna.Library
import com.sun.jna.NativeLong
import com.sun.jna.Pointer
import com.sun.jna.ptr.LongByReference
import com.sun.jna.ptr.PointerByReference

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
interface McpFilterChainLibrary : Library {

    companion object {
        // Load the native library
        @JvmStatic
        val INSTANCE: McpFilterChainLibrary = NativeLibraryLoader.loadLibrary(McpFilterChainLibrary::class.java)

        /* ============================================================================
         * Chain Types and Enumerations (from mcp_c_filter_chain.h lines 32-63)
         * ============================================================================
         */

        // Chain execution mode
        const val MCP_CHAIN_MODE_SEQUENTIAL = 0 // Execute filters in order
        const val MCP_CHAIN_MODE_PARALLEL = 1 // Execute filters in parallel
        const val MCP_CHAIN_MODE_CONDITIONAL = 2 // Execute based on conditions
        const val MCP_CHAIN_MODE_PIPELINE = 3 // Pipeline mode with buffering

        // Chain routing strategy
        const val MCP_ROUTING_ROUND_ROBIN = 0 // Round-robin distribution
        const val MCP_ROUTING_LEAST_LOADED = 1 // Route to least loaded filter
        const val MCP_ROUTING_HASH_BASED = 2 // Hash-based routing
        const val MCP_ROUTING_PRIORITY = 3 // Priority-based routing
        const val MCP_ROUTING_CUSTOM = 99 // Custom routing function

        // Filter match condition
        const val MCP_MATCH_ALL = 0 // Match all conditions
        const val MCP_MATCH_ANY = 1 // Match any condition
        const val MCP_MATCH_NONE = 2 // Match no conditions
        const val MCP_MATCH_CUSTOM = 99 // Custom match function

        // Chain state
        const val MCP_CHAIN_STATE_IDLE = 0
        const val MCP_CHAIN_STATE_PROCESSING = 1
        const val MCP_CHAIN_STATE_PAUSED = 2
        const val MCP_CHAIN_STATE_ERROR = 3
        const val MCP_CHAIN_STATE_COMPLETED = 4
    }

    /* ============================================================================
     * Callback Types (lines 123-140)
     * ============================================================================
     */

    /**
     * Custom routing function callback
     */
    interface MCP_ROUTING_FUNCTION_T : Callback {
        fun invoke(buffer: Long, nodes: Array<McpFilterNode>?, node_count: NativeLong, user_data: Pointer?): Long
    }

    /**
     * Chain event callback
     */
    interface MCP_CHAIN_EVENT_CB : Callback {
        fun invoke(chain: Long, old_state: Int, new_state: Int, user_data: Pointer?)
    }

    /**
     * Filter match function callback
     */
    interface MCP_FILTER_MATCH_CB : Callback {
        fun invoke(buffer: Long, metadata: McpProtocolMetadata.ByReference?, user_data: Pointer?): Byte
    }

    /* ============================================================================
     * Advanced Chain Builder (lines 145-200)
     * ============================================================================
     */

    /**
     * Create chain builder with configuration (line 152)
     *
     * @param dispatcher Event dispatcher
     * @param config Chain configuration
     * @return Builder handle or NULL on error
     */
    fun mcp_chain_builder_create_ex(dispatcher: Pointer?, config: McpChainConfig.ByReference?): Pointer?

    /**
     * Add filter node to chain (line 161)
     *
     * @param builder Chain builder
     * @param node Filter node configuration
     * @return MCP_OK on success
     */
    fun mcp_chain_builder_add_node(builder: Pointer?, node: McpFilterNode.ByReference?): Int

    /**
     * Add conditional filter (line 172)
     *
     * @param builder Chain builder
     * @param condition Condition for filter execution
     * @param filter Filter to execute if condition met
     * @return MCP_OK on success
     */
    fun mcp_chain_builder_add_conditional(
        builder: Pointer?,
        condition: McpFilterCondition.ByReference?,
        filter: Long
    ): Int

    /**
     * Add parallel filter group (line 184)
     *
     * @param builder Chain builder
     * @param filters Array of filters to run in parallel
     * @param count Number of filters
     * @return MCP_OK on success
     */
    fun mcp_chain_builder_add_parallel_group(builder: Pointer?, filters: LongArray?, count: NativeLong): Int

    /**
     * Set custom routing function (line 196)
     *
     * @param builder Chain builder
     * @param router Custom routing function
     * @param user_data User data for router
     * @return MCP_OK on success
     */
    fun mcp_chain_builder_set_router(
        builder: Pointer?,
        router: MCP_ROUTING_FUNCTION_T?,
        user_data: Pointer?
    ): Int

    /* ============================================================================
     * Chain Management (lines 205-266)
     * ============================================================================
     */

    /**
     * Get chain state (line 211)
     *
     * @param chain Filter chain
     * @return Current chain state
     */
    fun mcp_chain_get_state(chain: Long): Int

    /**
     * Pause chain execution (line 219)
     *
     * @param chain Filter chain
     * @return MCP_OK on success
     */
    fun mcp_chain_pause(chain: Long): Int

    /**
     * Resume chain execution (line 226)
     *
     * @param chain Filter chain
     * @return MCP_OK on success
     */
    fun mcp_chain_resume(chain: Long): Int

    /**
     * Reset chain to initial state (line 233)
     *
     * @param chain Filter chain
     * @return MCP_OK on success
     */
    fun mcp_chain_reset(chain: Long): Int

    /**
     * Enable/disable filter in chain (line 242)
     *
     * @param chain Filter chain
     * @param filter_name Name of filter to enable/disable
     * @param enabled Enable flag
     * @return MCP_OK on success
     */
    fun mcp_chain_set_filter_enabled(chain: Long, filter_name: String?, enabled: Byte): Int

    /**
     * Get chain statistics (line 253)
     *
     * @param chain Filter chain
     * @param stats Output statistics
     * @return MCP_OK on success
     */
    fun mcp_chain_get_stats(chain: Long, stats: McpChainStats.ByReference?): Int

    /**
     * Set chain event callback (line 263)
     *
     * @param chain Filter chain
     * @param callback Event callback
     * @param user_data User data
     * @return MCP_OK on success
     */
    fun mcp_chain_set_event_callback(chain: Long, callback: MCP_CHAIN_EVENT_CB?, user_data: Pointer?): Int

    /* ============================================================================
     * Dynamic Chain Composition (lines 270-308)
     * ============================================================================
     */

    /**
     * Create dynamic chain from JSON configuration (line 278)
     *
     * @param dispatcher Event dispatcher
     * @param json_config JSON configuration
     * @return Chain handle or 0 on error
     */
    fun mcp_chain_create_from_json(dispatcher: Pointer?, json_config: Pointer?): Long

    /**
     * Export chain configuration to JSON (line 286)
     *
     * @param chain Filter chain
     * @return JSON configuration or NULL on error
     */
    fun mcp_chain_export_to_json(chain: Long): Pointer?

    /**
     * Clone a filter chain (line 294)
     *
     * @param chain Source chain
     * @return Cloned chain handle or 0 on error
     */
    fun mcp_chain_clone(chain: Long): Long

    /**
     * Merge two chains (line 304)
     *
     * @param chain1 First chain
     * @param chain2 Second chain
     * @param mode Merge mode (sequential, parallel)
     * @return Merged chain handle or 0 on error
     */
    fun mcp_chain_merge(chain1: Long, chain2: Long, mode: Int): Long

    /* ============================================================================
     * Chain Router (lines 312-353)
     * ============================================================================
     */

    /**
     * Create chain router (line 321)
     *
     * @param config Router configuration
     * @return Router handle or NULL on error
     */
    fun mcp_chain_router_create(config: McpRouterConfig.ByReference?): Pointer?

    /**
     * Add route to router (line 331)
     *
     * @param router Chain router
     * @param condition Match condition
     * @param chain Target chain
     * @return MCP_OK on success
     */
    fun mcp_chain_router_add_route(router: Pointer?, condition: MCP_FILTER_MATCH_CB?, chain: Long): Int

    /**
     * Route buffer through appropriate chain (line 343)
     *
     * @param router Chain router
     * @param buffer Buffer to route
     * @param metadata Protocol metadata
     * @return Selected chain or 0 if no match
     */
    fun mcp_chain_router_route(
        router: Pointer?,
        buffer: Long,
        metadata: McpProtocolMetadata.ByReference?
    ): Long

    /**
     * Destroy chain router (line 352)
     *
     * @param router Chain router
     */
    fun mcp_chain_router_destroy(router: Pointer?)

    /* ============================================================================
     * Chain Pool for Load Balancing (lines 357-408)
     * ============================================================================
     */

    /**
     * Create chain pool for load balancing (line 368)
     *
     * @param base_chain Template chain
     * @param pool_size Number of chain instances
     * @param strategy Load balancing strategy
     * @return Pool handle or NULL on error
     */
    fun mcp_chain_pool_create(base_chain: Long, pool_size: NativeLong, strategy: Int): Pointer?

    /**
     * Get next chain from pool (line 378)
     *
     * @param pool Chain pool
     * @return Next chain based on strategy
     */
    fun mcp_chain_pool_get_next(pool: Pointer?): Long

    /**
     * Return chain to pool (line 386)
     *
     * @param pool Chain pool
     * @param chain Chain to return
     */
    fun mcp_chain_pool_return(pool: Pointer?, chain: Long)

    /**
     * Get pool statistics (line 397)
     *
     * @param pool Chain pool
     * @param active Output: active chains
     * @param idle Output: idle chains
     * @param total_processed Output: total processed
     * @return MCP_OK on success
     */
    fun mcp_chain_pool_get_stats(
        pool: Pointer?,
        active: PointerByReference?,
        idle: PointerByReference?,
        total_processed: LongByReference?
    ): Int

    /**
     * Destroy chain pool (line 407)
     *
     * @param pool Chain pool
     */
    fun mcp_chain_pool_destroy(pool: Pointer?)

    /* ============================================================================
     * Chain Optimization (lines 412-441)
     * ============================================================================
     */

    /**
     * Optimize chain by removing redundant filters (line 419)
     *
     * @param chain Filter chain
     * @return MCP_OK on success
     */
    fun mcp_chain_optimize(chain: Long): Int

    /**
     * Reorder filters for optimal performance (line 426)
     *
     * @param chain Filter chain
     * @return MCP_OK on success
     */
    fun mcp_chain_reorder_filters(chain: Long): Int

    /**
     * Profile chain performance (line 437)
     *
     * @param chain Filter chain
     * @param test_buffer Test buffer for profiling
     * @param iterations Number of test iterations
     * @param report Output: performance report (JSON)
     * @return MCP_OK on success
     */
    fun mcp_chain_profile(
        chain: Long,
        test_buffer: Long,
        iterations: NativeLong,
        report: PointerByReference?
    ): Int

    /* ============================================================================
     * Chain Debugging (lines 445-473)
     * ============================================================================
     */

    /**
     * Enable chain tracing (line 453)
     *
     * @param chain Filter chain
     * @param trace_level Trace level (0=off, 1=basic, 2=detailed)
     * @return MCP_OK on success
     */
    fun mcp_chain_set_trace_level(chain: Long, trace_level: Int): Int

    /**
     * Dump chain structure (line 462)
     *
     * @param chain Filter chain
     * @param format Output format ("text", "json", "dot")
     * @return String representation (must be freed)
     */
    fun mcp_chain_dump(chain: Long, format: String?): String?

    /**
     * Validate chain configuration (line 471)
     *
     * @param chain Filter chain
     * @param errors Output: validation errors (JSON)
     * @return MCP_OK if valid
     */
    fun mcp_chain_validate(chain: Long, errors: PointerByReference?): Int
}