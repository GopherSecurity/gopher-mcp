/**
 * @file enhanced-filter-chain.ts
 * @brief Enhanced filter chain management using MCP C API chain functions
 *
 * This implementation provides advanced filter chain features including:
 * - Dynamic filter composition
 * - Conditional filter execution
 * - Parallel filter processing
 * - Filter routing and branching
 * - Performance monitoring and optimization
 */

import { AdvancedBuffer } from "../buffers/advanced-buffer";
import { mcpFilterLib } from "../core/ffi-bindings";

// Chain execution modes from mcp_filter_chain.h
export enum ChainExecutionMode {
  SEQUENTIAL = 0, // Execute filters in order
  PARALLEL = 1, // Execute filters in parallel
  CONDITIONAL = 2, // Execute based on conditions
  PIPELINE = 3, // Pipeline mode with buffering
}

// Chain routing strategies
export enum RoutingStrategy {
  ROUND_ROBIN = 0, // Round-robin distribution
  LEAST_LOADED = 1, // Route to least loaded filter
  HASH_BASED = 2, // Hash-based routing
  PRIORITY = 3, // Priority-based routing
  CUSTOM = 99, // Custom routing function
}

// Filter match conditions
export enum MatchCondition {
  ALL = 0, // Match all conditions
  ANY = 1, // Match any condition
  NONE = 2, // Match no conditions
  CUSTOM = 99, // Custom match function
}

// Chain states
export enum ChainState {
  IDLE = 0,
  PROCESSING = 1,
  PAUSED = 2,
  ERROR = 3,
  COMPLETED = 4,
}

// Filter node configuration
export interface FilterNode {
  filter: number; // Filter handle
  name: string; // Filter name
  priority: number; // Execution priority
  enabled: boolean; // Whether filter is enabled
  bypassOnError: boolean; // Bypass filter on error
  config: any; // JSON configuration
}

// Chain configuration
export interface ChainConfig {
  name: string;
  mode: ChainExecutionMode;
  routing: RoutingStrategy;
  maxParallel: number;
  bufferSize: number;
  timeoutMs: number;
  stopOnError: boolean;
}

// Filter condition for conditional execution
export interface FilterCondition {
  matchType: MatchCondition;
  field: string;
  value: string;
  targetFilter: number;
}

// Chain statistics
export interface ChainStats {
  totalProcessed: number;
  totalErrors: number;
  totalBypassed: number;
  avgLatencyMs: number;
  maxLatencyMs: number;
  throughputMbps: number;
  activeFilters: number;
}

// Router configuration
export interface RouterConfig {
  strategy: RoutingStrategy;
  hashSeed: number;
  routeTable: Map<string, number>; // Map of conditions to chains
  customRouterData?: any;
}

// Custom routing function type
export type RoutingFunction = (
  buffer: AdvancedBuffer,
  nodes: FilterNode[],
  nodeCount: number,
  userData: any
) => number;

// Chain event callback type
export type ChainEventCallback = (
  chain: number,
  oldState: ChainState,
  newState: ChainState,
  userData: any
) => void;

// Filter match callback type
export type FilterMatchCallback = (
  buffer: AdvancedBuffer,
  metadata: any,
  userData: any
) => boolean;

/**
 * Enhanced filter chain with advanced features
 */
export class EnhancedFilterChain {
  private _chainHandle: number;
  private config: ChainConfig;
  private nodes: FilterNode[] = [];
  // private router?: any; // Unused for now
  // private eventCallback?: ChainEventCallback; // Unused for now
  // private userData?: any; // Unused for now

  constructor(dispatcher: number, config: ChainConfig) {
    this.config = config;

    // Create chain builder with configuration
    const configStruct = {
      name: this.config.name,
      mode: this.config.mode,
      routing: this.config.routing,
      maxParallel: this.config.maxParallel,
      bufferSize: this.config.bufferSize,
      timeoutMs: this.config.timeoutMs,
      stopOnError: this.config.stopOnError ? 1 : 0,
    };

    const builder = mcpFilterLib.mcp_chain_builder_create_ex(
      dispatcher,
      configStruct as any
    );

    if (!builder) {
      throw new Error("Failed to create chain builder");
    }

    // Build the chain
    this._chainHandle = mcpFilterLib.mcp_filter_chain_build(builder);

    if (!this._chainHandle) {
      throw new Error("Failed to build filter chain");
    }

    // Clean up builder
    mcpFilterLib.mcp_filter_chain_builder_destroy(builder);
  }

  /**
   * Add filter node to chain
   */
  addNode(node: FilterNode): void {
    const nodeStruct = {
      filter: node.filter,
      name: node.name,
      priority: node.priority,
      enabled: node.enabled ? 1 : 0,
      bypassOnError: node.bypassOnError ? 1 : 0,
      config: node.config,
    };

    const result = mcpFilterLib.mcp_chain_builder_add_node(
      this._chainHandle,
      nodeStruct as any
    );

    if (result !== 0) {
      throw new Error("Failed to add filter node to chain");
    }

    this.nodes.push(node);
  }

  /**
   * Add conditional filter
   */
  addConditional(condition: FilterCondition, filter: number): void {
    const conditionStruct = {
      matchType: condition.matchType,
      field: condition.field,
      value: condition.value,
      targetFilter: condition.targetFilter,
    };

    const result = mcpFilterLib.mcp_chain_builder_add_conditional(
      this._chainHandle,
      conditionStruct as any,
      filter
    );

    if (result !== 0) {
      throw new Error("Failed to add conditional filter");
    }
  }

  /**
   * Add parallel filter group
   */
  addParallelGroup(filters: number[]): void {
    const result = mcpFilterLib.mcp_chain_builder_add_parallel_group(
      this._chainHandle,
      filters,
      filters.length
    );

    if (result !== 0) {
      throw new Error("Failed to add parallel filter group");
    }
  }

  /**
   * Set custom routing function
   */
  setCustomRouter(router: RoutingFunction, userData?: any): void {
    // Create C callback wrapper
    const cRouter = (
      buffer: number,
      nodes: any,
      nodeCount: number,
      userData: any
    ) => {
      // Convert C types to TypeScript types
      const tsBuffer = new AdvancedBuffer(0);
      (tsBuffer as any).handle = buffer;

      const tsNodes: FilterNode[] = [];
      for (let i = 0; i < nodeCount; i++) {
        // Convert C node to TypeScript node (simplified)
        tsNodes.push({
          filter: nodes[i].filter,
          name: nodes[i].name || "",
          priority: nodes[i].priority || 0,
          enabled: nodes[i].enabled !== 0,
          bypassOnError: nodes[i].bypassOnError !== 0,
          config: nodes[i].config || {},
        });
      }

      return router(tsBuffer, tsNodes, nodeCount, userData);
    };

    const result = mcpFilterLib.mcp_chain_builder_set_router(
      this._chainHandle,
      cRouter as any,
      userData
    );

    if (result !== 0) {
      throw new Error("Failed to set custom router");
    }

    // this.router = router;
    // this.userData = userData;
  }

  /**
   * Get current chain state
   */
  getState(): ChainState {
    return mcpFilterLib.mcp_chain_get_state(this._chainHandle);
  }

  /**
   * Pause chain execution
   */
  pause(): void {
    const result = mcpFilterLib.mcp_chain_pause(this._chainHandle);

    if (result !== 0) {
      throw new Error("Failed to pause chain");
    }
  }

  /**
   * Resume chain execution
   */
  resume(): void {
    const result = mcpFilterLib.mcp_chain_resume(this._chainHandle);

    if (result !== 0) {
      throw new Error("Failed to resume chain");
    }
  }

  /**
   * Reset chain to initial state
   */
  reset(): void {
    const result = mcpFilterLib.mcp_chain_reset(this._chainHandle);

    if (result !== 0) {
      throw new Error("Failed to reset chain");
    }
  }

  /**
   * Enable/disable filter in chain
   */
  setFilterEnabled(filterName: string, enabled: boolean): void {
    const result = mcpFilterLib.mcp_chain_set_filter_enabled(
      this._chainHandle,
      filterName,
      enabled ? 1 : 0
    );

    if (result !== 0) {
      throw new Error("Failed to set filter enabled state");
    }
  }

  /**
   * Get chain statistics
   */
  getStats(): ChainStats {
    const stats = {
      totalProcessed: 0,
      totalErrors: 0,
      totalBypassed: 0,
      avgLatencyMs: 0,
      maxLatencyMs: 0,
      throughputMbps: 0,
      activeFilters: 0,
    };

    const result = mcpFilterLib.mcp_chain_get_stats(
      this._chainHandle,
      stats as any
    );

    if (result !== 0) {
      throw new Error("Failed to get chain statistics");
    }

    return stats;
  }

  /**
   * Set chain event callback
   */
  setEventCallback(callback: ChainEventCallback, userData?: any): void {
    const result = mcpFilterLib.mcp_chain_set_event_callback(
      this._chainHandle,
      callback as any,
      userData
    );

    if (result !== 0) {
      throw new Error("Failed to set event callback");
    }

    // this.eventCallback = callback;
    // this.userData = userData;
  }

  /**
   * Optimize chain by removing redundant filters
   */
  optimize(): void {
    const result = mcpFilterLib.mcp_chain_optimize(this._chainHandle);

    if (result !== 0) {
      throw new Error("Failed to optimize chain");
    }
  }

  /**
   * Reorder filters for optimal performance
   */
  reorderFilters(): void {
    const result = mcpFilterLib.mcp_chain_reorder_filters(this._chainHandle);

    if (result !== 0) {
      throw new Error("Failed to reorder filters");
    }
  }

  /**
   * Profile chain performance
   */
  profile(testBuffer: AdvancedBuffer, iterations: number): any {
    const report = { value: null as any };
    const result = mcpFilterLib.mcp_chain_profile(
      this._chainHandle,
      testBuffer.getHandle(),
      iterations,
      report as any
    );

    if (result !== 0) {
      throw new Error("Failed to profile chain");
    }

    return report.value;
  }

  /**
   * Enable chain tracing
   */
  setTraceLevel(traceLevel: number): void {
    const result = mcpFilterLib.mcp_chain_set_trace_level(
      this._chainHandle,
      traceLevel
    );

    if (result !== 0) {
      throw new Error("Failed to set trace level");
    }
  }

  /**
   * Dump chain structure
   */
  dump(format: "text" | "json" | "dot" = "text"): string {
    const result = mcpFilterLib.mcp_chain_dump(this._chainHandle, format);

    if (!result) {
      throw new Error("Failed to dump chain structure");
    }

    return result;
  }

  /**
   * Validate chain configuration
   */
  validate(): { isValid: boolean; errors: any } {
    const errors = { value: null as any };
    const result = mcpFilterLib.mcp_chain_validate(
      this._chainHandle,
      errors as any
    );

    return {
      isValid: result === 0,
      errors: errors.value,
    };
  }

  /**
   * Get the underlying chain handle
   */
  getHandle(): number {
    return this._chainHandle;
  }

  /**
   * Retain the chain (increment reference count)
   */
  retain(): void {
    mcpFilterLib.mcp_filter_chain_retain(this._chainHandle);
  }

  /**
   * Release the chain (decrement reference count)
   */
  release(): void {
    if (this._chainHandle) {
      mcpFilterLib.mcp_filter_chain_release(this._chainHandle);
      this._chainHandle = 0;
    }
  }

  /**
   * Destroy the chain
   */
  destroy(): void {
    this.release();
  }
}

/**
 * Chain router for dynamic routing
 */
export class ChainRouter {
  private routerHandle: number;
  private config: RouterConfig;

  constructor(config: RouterConfig) {
    this.config = config;

    const configStruct = {
      strategy: this.config.strategy,
      hashSeed: this.config.hashSeed,
      routeTable: this.config.routeTable,
      customRouterData: this.config.customRouterData || null,
    };

    this.routerHandle = mcpFilterLib.mcp_chain_router_create(
      configStruct as any
    );

    if (!this.routerHandle) {
      throw new Error("Failed to create chain router");
    }
  }

  /**
   * Add route to router
   */
  addRoute(condition: FilterMatchCallback, chain: number): void {
    const result = mcpFilterLib.mcp_chain_router_add_route(
      this.routerHandle,
      condition as any,
      chain
    );

    if (result !== 0) {
      throw new Error("Failed to add route to router");
    }
  }

  /**
   * Route buffer through appropriate chain
   */
  route(buffer: AdvancedBuffer, metadata: any): number {
    return mcpFilterLib.mcp_chain_router_route(
      this.routerHandle,
      buffer.getHandle(),
      metadata
    );
  }

  /**
   * Destroy the router
   */
  destroy(): void {
    if (this.routerHandle) {
      mcpFilterLib.mcp_chain_router_destroy(this.routerHandle);
      this.routerHandle = 0;
    }
  }
}

/**
 * Chain pool for load balancing
 */
export class ChainPool {
  private poolHandle: number;
  // private baseChain: number; // Unused for now
  // private poolSize: number; // Unused for now
  // private strategy: RoutingStrategy; // Unused for now

  constructor(baseChain: number, poolSize: number, strategy: RoutingStrategy) {
    // this.baseChain = baseChain; // Unused for now
    // this.poolSize = poolSize; // Unused for now
    // this.strategy = strategy; // Unused for now

    this.poolHandle = mcpFilterLib.mcp_chain_pool_create(
      baseChain,
      poolSize,
      strategy
    );

    if (!this.poolHandle) {
      throw new Error("Failed to create chain pool");
    }
  }

  /**
   * Get next chain from pool
   */
  getNext(): number {
    return mcpFilterLib.mcp_chain_pool_get_next(this.poolHandle);
  }

  /**
   * Return chain to pool
   */
  returnChain(chain: number): void {
    mcpFilterLib.mcp_chain_pool_return(this.poolHandle, chain);
  }

  /**
   * Get pool statistics
   */
  getStats(): { active: number; idle: number; totalProcessed: number } {
    const active = { value: 0 };
    const idle = { value: 0 };
    const totalProcessed = { value: 0 };

    const result = mcpFilterLib.mcp_chain_pool_get_stats(
      this.poolHandle,
      active as any,
      idle as any,
      totalProcessed as any
    );

    if (result !== 0) {
      throw new Error("Failed to get pool statistics");
    }

    return {
      active: active.value,
      idle: idle.value,
      totalProcessed: totalProcessed.value,
    };
  }

  /**
   * Destroy the pool
   */
  destroy(): void {
    if (this.poolHandle) {
      mcpFilterLib.mcp_chain_pool_destroy(this.poolHandle);
      this.poolHandle = 0;
    }
  }
}

/**
 * Create dynamic chain from JSON configuration
 */
export function createChainFromJson(
  dispatcher: number,
  jsonConfig: any
): EnhancedFilterChain {
  const chainHandle = mcpFilterLib.mcp_chain_create_from_json(
    dispatcher,
    jsonConfig
  );

  if (!chainHandle) {
    throw new Error("Failed to create chain from JSON");
  }

  // Create a minimal config for the chain
  const config: ChainConfig = {
    name: "dynamic-chain",
    mode: ChainExecutionMode.SEQUENTIAL,
    routing: RoutingStrategy.ROUND_ROBIN,
    maxParallel: 1,
    bufferSize: 1024,
    timeoutMs: 5000,
    stopOnError: false,
  };

  const chain = new EnhancedFilterChain(dispatcher, config);
  (chain as any)._chainHandle = chainHandle;

  return chain;
}

/**
 * Export chain configuration to JSON
 */
export function exportChainToJson(chain: EnhancedFilterChain): any {
  return mcpFilterLib.mcp_chain_export_to_json(chain.getHandle());
}

/**
 * Clone a filter chain
 */
export function cloneChain(chain: EnhancedFilterChain): EnhancedFilterChain {
  const clonedHandle = mcpFilterLib.mcp_chain_clone(chain.getHandle());

  if (!clonedHandle) {
    throw new Error("Failed to clone chain");
  }

  // Create a new chain with the same config
  const config: ChainConfig = {
    name: "cloned-chain",
    mode: ChainExecutionMode.SEQUENTIAL,
    routing: RoutingStrategy.ROUND_ROBIN,
    maxParallel: 1,
    bufferSize: 1024,
    timeoutMs: 5000,
    stopOnError: false,
  };

  const cloned = new EnhancedFilterChain(0, config);
  (cloned as any)._chainHandle = clonedHandle;

  return cloned;
}

/**
 * Merge two chains
 */
export function mergeChains(
  chain1: EnhancedFilterChain,
  chain2: EnhancedFilterChain,
  mode: ChainExecutionMode
): EnhancedFilterChain {
  const mergedHandle = mcpFilterLib.mcp_chain_merge(
    chain1.getHandle(),
    chain2.getHandle(),
    mode
  );

  if (!mergedHandle) {
    throw new Error("Failed to merge chains");
  }

  // Create a new chain with merged config
  const config: ChainConfig = {
    name: "merged-chain",
    mode: mode,
    routing: RoutingStrategy.ROUND_ROBIN,
    maxParallel: 1,
    bufferSize: 1024,
    timeoutMs: 5000,
    stopOnError: false,
  };

  const merged = new EnhancedFilterChain(0, config);
  (merged as any)._chainHandle = mergedHandle;

  return merged;
}
