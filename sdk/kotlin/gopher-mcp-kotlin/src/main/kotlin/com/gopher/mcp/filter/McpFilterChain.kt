package com.gopher.mcp.filter

import com.gopher.mcp.filter.type.ProtocolMetadata
import com.gopher.mcp.filter.type.buffer.FilterCondition
import com.gopher.mcp.filter.type.chain.*
import com.gopher.mcp.jna.McpFilterChainLibrary
import com.gopher.mcp.jna.type.filter.McpProtocolMetadata
import com.gopher.mcp.jna.type.filter.chain.*
import com.sun.jna.Native
import com.sun.jna.NativeLong
import com.sun.jna.Pointer
import com.sun.jna.ptr.LongByReference
import com.sun.jna.ptr.PointerByReference

/**
 * Kotlin wrapper for the MCP Filter Chain API. Provides one-to-one method mapping to
 * McpFilterChainLibrary.
 *
 * This wrapper provides comprehensive filter chain composition and management, including dynamic
 * routing, conditional execution, and performance optimization.
 */
class McpFilterChain : AutoCloseable {

    private val lib: McpFilterChainLibrary = McpFilterChainLibrary.INSTANCE
    private var primaryChainHandle: Long? = null

    // ============================================================================
    // Advanced Chain Builder Methods (one-to-one mapping)
    // ============================================================================

    /** Create chain builder with configuration Maps to: mcp_chain_builder_create_ex */
    fun chainBuilderCreateEx(dispatcher: Long, config: ChainConfig): Long {
        val dispatcherPtr = if (dispatcher != 0L) Pointer(dispatcher) else null
        val nativeConfig = McpChainConfig.ByReference().apply {
            this.name = config.name
            this.mode = config.mode
            this.routing = config.routing
            this.max_parallel = config.maxParallel
            this.buffer_size = config.bufferSize
            this.timeout_ms = config.timeoutMs
            this.stop_on_error = (if (config.stopOnError) 1 else 0).toByte()
        }

        val builder = lib.mcp_chain_builder_create_ex(dispatcherPtr, nativeConfig)
        return Pointer.nativeValue(builder)
    }

    /** Add filter node to chain Maps to: mcp_chain_builder_add_node */
    fun chainBuilderAddNode(builder: Long, node: FilterNode): Int {
        val nativeNode = McpFilterNode.ByReference().apply {
            this.filter = if (node.filterHandle != 0L) Pointer(node.filterHandle) else null
            this.name = node.name
            this.priority = node.priority
            this.enabled = (if (node.enabled) 1 else 0).toByte()
            this.bypass_on_error = (if (node.bypassOnError) 1 else 0).toByte()
            this.config = if (node.configHandle != 0L) Pointer(node.configHandle) else null
        }

        return lib.mcp_chain_builder_add_node(Pointer(builder), nativeNode)
    }

    /** Add conditional filter Maps to: mcp_chain_builder_add_conditional */
    fun chainBuilderAddConditional(builder: Long, condition: FilterCondition, filter: Long): Int {
        val nativeCondition = McpFilterCondition.ByReference().apply {
            this.match_type = condition.matchType
            this.field = condition.field
            this.value = condition.value
            this.target_filter = if (condition.targetFilter != 0L) Pointer(condition.targetFilter) else null
        }

        return lib.mcp_chain_builder_add_conditional(Pointer(builder), nativeCondition, filter)
    }

    /** Add parallel filter group Maps to: mcp_chain_builder_add_parallel_group */
    fun chainBuilderAddParallelGroup(builder: Long, filters: LongArray): Int {
        return lib.mcp_chain_builder_add_parallel_group(
            Pointer(builder), filters, NativeLong(filters.size.toLong())
        )
    }

    /** Set custom routing function Maps to: mcp_chain_builder_set_router */
    fun chainBuilderSetRouter(
        builder: Long,
        router: McpFilterChainLibrary.MCP_ROUTING_FUNCTION_T,
        userData: Long
    ): Int {
        val userDataPtr = if (userData != 0L) Pointer(userData) else null
        return lib.mcp_chain_builder_set_router(Pointer(builder), router, userDataPtr)
    }

    // ============================================================================
    // Chain Management Methods (one-to-one mapping)
    // ============================================================================

    /** Get chain state Maps to: mcp_chain_get_state */
    fun chainGetState(chain: Long): ChainState {
        val state = lib.mcp_chain_get_state(chain)
        return ChainState.fromValue(state)
    }

    /** Pause chain execution Maps to: mcp_chain_pause */
    fun chainPause(chain: Long): Int {
        return lib.mcp_chain_pause(chain)
    }

    /** Resume chain execution Maps to: mcp_chain_resume */
    fun chainResume(chain: Long): Int {
        return lib.mcp_chain_resume(chain)
    }

    /** Reset chain to initial state Maps to: mcp_chain_reset */
    fun chainReset(chain: Long): Int {
        return lib.mcp_chain_reset(chain)
    }

    /** Enable/disable filter in chain Maps to: mcp_chain_set_filter_enabled */
    fun chainSetFilterEnabled(chain: Long, filterName: String?, enabled: Boolean): Int {
        return lib.mcp_chain_set_filter_enabled(chain, filterName, (if (enabled) 1 else 0).toByte())
    }

    /** Get chain statistics Maps to: mcp_chain_get_stats */
    fun chainGetStats(chain: Long, stats: ChainStats): Int {
        val nativeStats = McpChainStats.ByReference()
        val result = lib.mcp_chain_get_stats(chain, nativeStats)

        if (result == 0) {
            stats.totalProcessed = nativeStats.total_processed
            stats.totalErrors = nativeStats.total_errors
            stats.totalBypassed = nativeStats.total_bypassed
            stats.avgLatencyMs = nativeStats.avg_latency_ms
            stats.maxLatencyMs = nativeStats.max_latency_ms
            stats.throughputMbps = nativeStats.throughput_mbps
            stats.activeFilters = nativeStats.active_filters
        }

        return result
    }

    /** Set chain event callback Maps to: mcp_chain_set_event_callback */
    fun chainSetEventCallback(
        chain: Long,
        callback: McpFilterChainLibrary.MCP_CHAIN_EVENT_CB,
        userData: Long
    ): Int {
        val userDataPtr = if (userData != 0L) Pointer(userData) else null
        return lib.mcp_chain_set_event_callback(chain, callback, userDataPtr)
    }

    // ============================================================================
    // Dynamic Chain Composition Methods (one-to-one mapping)
    // ============================================================================

    /** Create dynamic chain from JSON configuration Maps to: mcp_chain_create_from_json */
    fun chainCreateFromJson(dispatcher: Long, jsonConfig: Long): Long {
        val dispatcherPtr = if (dispatcher != 0L) Pointer(dispatcher) else null
        val jsonPtr = if (jsonConfig != 0L) Pointer(jsonConfig) else null

        val handle = lib.mcp_chain_create_from_json(dispatcherPtr, jsonPtr)
        if (primaryChainHandle == null && handle != 0L) {
            primaryChainHandle = handle
        }
        return handle
    }

    /** Export chain configuration to JSON Maps to: mcp_chain_export_to_json */
    fun chainExportToJson(chain: Long): Long {
        val json = lib.mcp_chain_export_to_json(chain)
        return Pointer.nativeValue(json)
    }

    /** Clone a filter chain Maps to: mcp_chain_clone */
    fun chainClone(chain: Long): Long {
        return lib.mcp_chain_clone(chain)
    }

    /** Merge two chains Maps to: mcp_chain_merge */
    fun chainMerge(chain1: Long, chain2: Long, mode: ChainExecutionMode): Long {
        return lib.mcp_chain_merge(chain1, chain2, mode.value)
    }

    // ============================================================================
    // Chain Router Methods (one-to-one mapping)
    // ============================================================================

    /** Create chain router Maps to: mcp_chain_router_create */
    fun chainRouterCreate(config: RouterConfig): Long {
        val nativeConfig = McpRouterConfig.ByReference().apply {
            this.strategy = config.strategy
            this.hash_seed = config.hashSeed
            this.route_table = if (config.routeTable != 0L) Pointer(config.routeTable) else null
            this.custom_router_data = config.customRouterData?.let { Pointer(Native.malloc(8)) }
        }

        val router = lib.mcp_chain_router_create(nativeConfig)
        return Pointer.nativeValue(router)
    }

    /** Add route to router Maps to: mcp_chain_router_add_route */
    fun chainRouterAddRoute(
        router: Long,
        condition: McpFilterChainLibrary.MCP_FILTER_MATCH_CB,
        chain: Long
    ): Int {
        return lib.mcp_chain_router_add_route(Pointer(router), condition, chain)
    }

    /** Route buffer through appropriate chain Maps to: mcp_chain_router_route */
    fun chainRouterRoute(router: Long, buffer: Long, metadata: ProtocolMetadata?): Long {
        val nativeMeta = if (metadata != null) {
            McpProtocolMetadata.ByReference().apply {
                this.layer = metadata.layer
                // Note: Union fields would need proper handling based on layer
            }
        } else {
            null
        }

        return lib.mcp_chain_router_route(Pointer(router), buffer, nativeMeta)
    }

    /** Destroy chain router Maps to: mcp_chain_router_destroy */
    fun chainRouterDestroy(router: Long) {
        lib.mcp_chain_router_destroy(Pointer(router))
    }

    // ============================================================================
    // Chain Pool Methods (one-to-one mapping)
    // ============================================================================

    /** Create chain pool for load balancing Maps to: mcp_chain_pool_create */
    fun chainPoolCreate(baseChain: Long, poolSize: Long, strategy: ChainRoutingStrategy): Long {
        val pool = lib.mcp_chain_pool_create(baseChain, NativeLong(poolSize), strategy.getValue())
        return Pointer.nativeValue(pool)
    }

    /** Get next chain from pool Maps to: mcp_chain_pool_get_next */
    fun chainPoolGetNext(pool: Long): Long {
        return lib.mcp_chain_pool_get_next(Pointer(pool))
    }

    /** Return chain to pool Maps to: mcp_chain_pool_return */
    fun chainPoolReturn(pool: Long, chain: Long) {
        lib.mcp_chain_pool_return(Pointer(pool), chain)
    }

    /** Get pool statistics Maps to: mcp_chain_pool_get_stats */
    fun chainPoolGetStats(
        pool: Long,
        active: PointerByReference,
        idle: PointerByReference,
        totalProcessed: LongByReference
    ): Int {
        return lib.mcp_chain_pool_get_stats(Pointer(pool), active, idle, totalProcessed)
    }

    /** Destroy chain pool Maps to: mcp_chain_pool_destroy */
    fun chainPoolDestroy(pool: Long) {
        lib.mcp_chain_pool_destroy(Pointer(pool))
    }

    // ============================================================================
    // Chain Optimization Methods (one-to-one mapping)
    // ============================================================================

    /** Optimize chain by removing redundant filters Maps to: mcp_chain_optimize */
    fun chainOptimize(chain: Long): Int {
        return lib.mcp_chain_optimize(chain)
    }

    /** Reorder filters for optimal performance Maps to: mcp_chain_reorder_filters */
    fun chainReorderFilters(chain: Long): Int {
        return lib.mcp_chain_reorder_filters(chain)
    }

    /** Profile chain performance Maps to: mcp_chain_profile */
    fun chainProfile(chain: Long, testBuffer: Long, iterations: Long, report: PointerByReference): Int {
        return lib.mcp_chain_profile(chain, testBuffer, NativeLong(iterations), report)
    }

    // ============================================================================
    // Chain Debugging Methods (one-to-one mapping)
    // ============================================================================

    /** Enable chain tracing Maps to: mcp_chain_set_trace_level */
    fun chainSetTraceLevel(chain: Long, traceLevel: Int): Int {
        return lib.mcp_chain_set_trace_level(chain, traceLevel)
    }

    /** Dump chain structure Maps to: mcp_chain_dump */
    fun chainDump(chain: Long, format: String?): String? {
        return lib.mcp_chain_dump(chain, format)
    }

    /** Validate chain configuration Maps to: mcp_chain_validate */
    fun chainValidate(chain: Long, errors: PointerByReference): Int {
        return lib.mcp_chain_validate(chain, errors)
    }

    // ============================================================================
    // AutoCloseable Implementation
    // ============================================================================

    override fun close() {
        // Chain handles are typically managed through reference counting
        // No explicit destroy needed
        primaryChainHandle?.let {
            primaryChainHandle = null
        }
    }

    // ============================================================================
    // Utility Methods
    // ============================================================================

    /** Get the primary chain handle */
    fun getPrimaryChainHandle(): Long? {
        return primaryChainHandle
    }

    /** Set the primary chain handle */
    fun setPrimaryChainHandle(handle: Long) {
        primaryChainHandle = handle
    }
}