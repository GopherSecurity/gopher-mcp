package com.gopher.mcp.filter;

import com.gopher.mcp.filter.type.ProtocolMetadata;
import com.gopher.mcp.filter.type.buffer.FilterCondition;
import com.gopher.mcp.filter.type.chain.*;
import com.gopher.mcp.jna.McpFilterChainLibrary;
import com.gopher.mcp.jna.type.filter.McpProtocolMetadata;
import com.gopher.mcp.jna.type.filter.chain.*;
import com.sun.jna.Native;
import com.sun.jna.NativeLong;
import com.sun.jna.Pointer;
import com.sun.jna.ptr.LongByReference;
import com.sun.jna.ptr.PointerByReference;

/**
 * Java wrapper for the MCP Filter Chain API. Provides one-to-one method mapping to
 * McpFilterChainLibrary.
 *
 * <p>This wrapper provides comprehensive filter chain composition and management, including dynamic
 * routing, conditional execution, and performance optimization.
 */
public class McpFilterChain implements AutoCloseable {

  private final McpFilterChainLibrary lib;
  private Long primaryChainHandle;

  public McpFilterChain() {
    this.lib = McpFilterChainLibrary.INSTANCE;
  }

  // ============================================================================
  // Advanced Chain Builder Methods (one-to-one mapping)
  // ============================================================================

  /** Create chain builder with configuration Maps to: mcp_chain_builder_create_ex */
  public long chainBuilderCreateEx(long dispatcher, ChainConfig config) {
    Pointer dispatcherPtr = dispatcher != 0 ? new Pointer(dispatcher) : null;
    McpChainConfig.ByReference nativeConfig = new McpChainConfig.ByReference();
    nativeConfig.name = config.getName();
    nativeConfig.mode = config.getMode();
    nativeConfig.routing = config.getRouting();
    nativeConfig.max_parallel = config.getMaxParallel();
    nativeConfig.buffer_size = config.getBufferSize();
    nativeConfig.timeout_ms = config.getTimeoutMs();
    nativeConfig.stop_on_error = (byte) (config.isStopOnError() ? 1 : 0);

    Pointer builder = lib.mcp_chain_builder_create_ex(dispatcherPtr, nativeConfig);
    return Pointer.nativeValue(builder);
  }

  /** Add filter node to chain Maps to: mcp_chain_builder_add_node */
  public int chainBuilderAddNode(long builder, FilterNode node) {
    McpFilterNode.ByReference nativeNode = new McpFilterNode.ByReference();
    nativeNode.filter = node.getFilterHandle() != 0 ? new Pointer(node.getFilterHandle()) : null;
    nativeNode.name = node.getName();
    nativeNode.priority = node.getPriority();
    nativeNode.enabled = (byte) (node.isEnabled() ? 1 : 0);
    nativeNode.bypass_on_error = (byte) (node.isBypassOnError() ? 1 : 0);
    nativeNode.config = node.getConfigHandle() != 0 ? new Pointer(node.getConfigHandle()) : null;

    return lib.mcp_chain_builder_add_node(new Pointer(builder), nativeNode);
  }

  /** Add conditional filter Maps to: mcp_chain_builder_add_conditional */
  public int chainBuilderAddConditional(long builder, FilterCondition condition, long filter) {
    McpFilterCondition.ByReference nativeCondition = new McpFilterCondition.ByReference();
    nativeCondition.match_type = condition.getMatchType();
    nativeCondition.field = condition.getField();
    nativeCondition.value = condition.getValue();
    nativeCondition.target_filter =
        condition.getTargetFilter() != 0 ? new Pointer(condition.getTargetFilter()) : null;

    return lib.mcp_chain_builder_add_conditional(new Pointer(builder), nativeCondition, filter);
  }

  /** Add parallel filter group Maps to: mcp_chain_builder_add_parallel_group */
  public int chainBuilderAddParallelGroup(long builder, long[] filters) {
    return lib.mcp_chain_builder_add_parallel_group(
        new Pointer(builder), filters, new NativeLong(filters.length));
  }

  /** Set custom routing function Maps to: mcp_chain_builder_set_router */
  public int chainBuilderSetRouter(
      long builder, McpFilterChainLibrary.MCP_ROUTING_FUNCTION_T router, long userData) {
    Pointer userDataPtr = userData != 0 ? new Pointer(userData) : null;
    return lib.mcp_chain_builder_set_router(new Pointer(builder), router, userDataPtr);
  }

  // ============================================================================
  // Chain Management Methods (one-to-one mapping)
  // ============================================================================

  /** Get chain state Maps to: mcp_chain_get_state */
  public ChainState chainGetState(long chain) {
    int state = lib.mcp_chain_get_state(chain);
    return ChainState.fromValue(state);
  }

  /** Pause chain execution Maps to: mcp_chain_pause */
  public int chainPause(long chain) {
    return lib.mcp_chain_pause(chain);
  }

  /** Resume chain execution Maps to: mcp_chain_resume */
  public int chainResume(long chain) {
    return lib.mcp_chain_resume(chain);
  }

  /** Reset chain to initial state Maps to: mcp_chain_reset */
  public int chainReset(long chain) {
    return lib.mcp_chain_reset(chain);
  }

  /** Enable/disable filter in chain Maps to: mcp_chain_set_filter_enabled */
  public int chainSetFilterEnabled(long chain, String filterName, boolean enabled) {
    return lib.mcp_chain_set_filter_enabled(chain, filterName, (byte) (enabled ? 1 : 0));
  }

  /** Get chain statistics Maps to: mcp_chain_get_stats */
  public int chainGetStats(long chain, ChainStats stats) {
    McpChainStats.ByReference nativeStats = new McpChainStats.ByReference();
    int result = lib.mcp_chain_get_stats(chain, nativeStats);

    if (result == 0) {
      stats.setTotalProcessed(nativeStats.total_processed);
      stats.setTotalErrors(nativeStats.total_errors);
      stats.setTotalBypassed(nativeStats.total_bypassed);
      stats.setAvgLatencyMs(nativeStats.avg_latency_ms);
      stats.setMaxLatencyMs(nativeStats.max_latency_ms);
      stats.setThroughputMbps(nativeStats.throughput_mbps);
      stats.setActiveFilters(nativeStats.active_filters);
    }

    return result;
  }

  /** Set chain event callback Maps to: mcp_chain_set_event_callback */
  public int chainSetEventCallback(
      long chain, McpFilterChainLibrary.MCP_CHAIN_EVENT_CB callback, long userData) {
    Pointer userDataPtr = userData != 0 ? new Pointer(userData) : null;
    return lib.mcp_chain_set_event_callback(chain, callback, userDataPtr);
  }

  // ============================================================================
  // Dynamic Chain Composition Methods (one-to-one mapping)
  // ============================================================================

  /** Create dynamic chain from JSON configuration Maps to: mcp_chain_create_from_json */
  public long chainCreateFromJson(long dispatcher, long jsonConfig) {
    Pointer dispatcherPtr = dispatcher != 0 ? new Pointer(dispatcher) : null;
    Pointer jsonPtr = jsonConfig != 0 ? new Pointer(jsonConfig) : null;

    long handle = lib.mcp_chain_create_from_json(dispatcherPtr, jsonPtr);
    if (this.primaryChainHandle == null && handle != 0) {
      this.primaryChainHandle = handle;
    }
    return handle;
  }

  /** Export chain configuration to JSON Maps to: mcp_chain_export_to_json */
  public long chainExportToJson(long chain) {
    Pointer json = lib.mcp_chain_export_to_json(chain);
    return Pointer.nativeValue(json);
  }

  /** Clone a filter chain Maps to: mcp_chain_clone */
  public long chainClone(long chain) {
    return lib.mcp_chain_clone(chain);
  }

  /** Merge two chains Maps to: mcp_chain_merge */
  public long chainMerge(long chain1, long chain2, ChainExecutionMode mode) {
    return lib.mcp_chain_merge(chain1, chain2, mode.getValue());
  }

  // ============================================================================
  // Chain Router Methods (one-to-one mapping)
  // ============================================================================

  /** Create chain router Maps to: mcp_chain_router_create */
  public long chainRouterCreate(RouterConfig config) {
    McpRouterConfig.ByReference nativeConfig = new McpRouterConfig.ByReference();
    nativeConfig.strategy = config.getStrategy();
    nativeConfig.hash_seed = config.getHashSeed();
    nativeConfig.route_table =
        config.getRouteTable() != 0 ? new Pointer(config.getRouteTable()) : null;
    nativeConfig.custom_router_data =
        config.getCustomRouterData() != null ? new Pointer(Native.malloc(8)) : null;

    Pointer router = lib.mcp_chain_router_create(nativeConfig);
    return Pointer.nativeValue(router);
  }

  /** Add route to router Maps to: mcp_chain_router_add_route */
  public int chainRouterAddRoute(
      long router, McpFilterChainLibrary.MCP_FILTER_MATCH_CB condition, long chain) {
    return lib.mcp_chain_router_add_route(new Pointer(router), condition, chain);
  }

  /** Route buffer through appropriate chain Maps to: mcp_chain_router_route */
  public long chainRouterRoute(long router, long buffer, ProtocolMetadata metadata) {
    McpProtocolMetadata.ByReference nativeMeta = null;
    if (metadata != null) {
      nativeMeta = new McpProtocolMetadata.ByReference();
      nativeMeta.layer = metadata.getLayer();
      // Note: Union fields would need proper handling based on layer
    }

    return lib.mcp_chain_router_route(new Pointer(router), buffer, nativeMeta);
  }

  /** Destroy chain router Maps to: mcp_chain_router_destroy */
  public void chainRouterDestroy(long router) {
    lib.mcp_chain_router_destroy(new Pointer(router));
  }

  // ============================================================================
  // Chain Pool Methods (one-to-one mapping)
  // ============================================================================

  /** Create chain pool for load balancing Maps to: mcp_chain_pool_create */
  public long chainPoolCreate(long baseChain, long poolSize, ChainRoutingStrategy strategy) {
    Pointer pool =
        lib.mcp_chain_pool_create(baseChain, new NativeLong(poolSize), strategy.getValue());
    return Pointer.nativeValue(pool);
  }

  /** Get next chain from pool Maps to: mcp_chain_pool_get_next */
  public long chainPoolGetNext(long pool) {
    return lib.mcp_chain_pool_get_next(new Pointer(pool));
  }

  /** Return chain to pool Maps to: mcp_chain_pool_return */
  public void chainPoolReturn(long pool, long chain) {
    lib.mcp_chain_pool_return(new Pointer(pool), chain);
  }

  /** Get pool statistics Maps to: mcp_chain_pool_get_stats */
  public int chainPoolGetStats(
      long pool,
      PointerByReference active,
      PointerByReference idle,
      LongByReference totalProcessed) {
    return lib.mcp_chain_pool_get_stats(new Pointer(pool), active, idle, totalProcessed);
  }

  /** Destroy chain pool Maps to: mcp_chain_pool_destroy */
  public void chainPoolDestroy(long pool) {
    lib.mcp_chain_pool_destroy(new Pointer(pool));
  }

  // ============================================================================
  // Chain Optimization Methods (one-to-one mapping)
  // ============================================================================

  /** Optimize chain by removing redundant filters Maps to: mcp_chain_optimize */
  public int chainOptimize(long chain) {
    return lib.mcp_chain_optimize(chain);
  }

  /** Reorder filters for optimal performance Maps to: mcp_chain_reorder_filters */
  public int chainReorderFilters(long chain) {
    return lib.mcp_chain_reorder_filters(chain);
  }

  /** Profile chain performance Maps to: mcp_chain_profile */
  public int chainProfile(long chain, long testBuffer, long iterations, PointerByReference report) {
    return lib.mcp_chain_profile(chain, testBuffer, new NativeLong(iterations), report);
  }

  // ============================================================================
  // Chain Debugging Methods (one-to-one mapping)
  // ============================================================================

  /** Enable chain tracing Maps to: mcp_chain_set_trace_level */
  public int chainSetTraceLevel(long chain, int traceLevel) {
    return lib.mcp_chain_set_trace_level(chain, traceLevel);
  }

  /** Dump chain structure Maps to: mcp_chain_dump */
  public String chainDump(long chain, String format) {
    return lib.mcp_chain_dump(chain, format);
  }

  /** Validate chain configuration Maps to: mcp_chain_validate */
  public int chainValidate(long chain, PointerByReference errors) {
    return lib.mcp_chain_validate(chain, errors);
  }

  // ============================================================================
  // AutoCloseable Implementation
  // ============================================================================

  @Override
  public void close() {
    // Chain handles are typically managed through reference counting
    // No explicit destroy needed
    if (primaryChainHandle != null) {
      primaryChainHandle = null;
    }
  }

  // ============================================================================
  // Utility Methods
  // ============================================================================

  /** Get the primary chain handle */
  public Long getPrimaryChainHandle() {
    return primaryChainHandle;
  }

  /** Set the primary chain handle */
  public void setPrimaryChainHandle(long handle) {
    this.primaryChainHandle = handle;
  }
}
