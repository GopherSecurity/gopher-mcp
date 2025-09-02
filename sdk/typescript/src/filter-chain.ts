/**
 * @file filter-chain.ts
 * @brief TypeScript wrapper for MCP C Filter Chain API (mcp_c_filter_chain.h)
 *
 * This module provides TypeScript wrappers for advanced filter chain management
 * including dynamic composition, conditional execution, parallel processing,
 * routing, and optimization. It uses the existing C++ RAII system through FFI calls.
 */

import { mcpFilterLib } from "./ffi-bindings";
// C struct conversion utilities (imported but not used yet)
// import {
//   createChainConfigStruct,
//   createFilterNodeStruct,
//   createFilterConditionStruct,
//   freeStruct
// } from "./c-structs";
import { ProtocolMetadata } from "./filter-api";

// ============================================================================
// Chain Types and Enumerations (matching mcp_c_filter_chain.h)
// ============================================================================

export enum ChainExecutionMode {
  SEQUENTIAL = 0, // Execute filters in order
  PARALLEL = 1, // Execute filters in parallel
  CONDITIONAL = 2, // Execute based on conditions
  PIPELINE = 3, // Pipeline mode with buffering
}

export enum RoutingStrategy {
  ROUND_ROBIN = 0, // Round-robin distribution
  LEAST_LOADED = 1, // Route to least loaded filter
  HASH_BASED = 2, // Hash-based routing
  PRIORITY = 3, // Priority-based routing
  CUSTOM = 99, // Custom routing function
}

export enum MatchCondition {
  ALL = 0, // Match all conditions
  ANY = 1, // Match any condition
  NONE = 2, // Match no conditions
  CUSTOM = 99, // Custom match function
}

export enum ChainState {
  IDLE = 0,
  PROCESSING = 1,
  PAUSED = 2,
  ERROR = 3,
  COMPLETED = 4,
}

// ============================================================================
// Data Structures (matching mcp_c_filter_chain.h)
// ============================================================================

export interface FilterNode {
  filter: number;
  name: string;
  priority: number;
  enabled: boolean;
  bypassOnError: boolean;
  config: any; // JSON configuration
}

export interface ChainConfig {
  name: string;
  mode: ChainExecutionMode;
  routing: RoutingStrategy;
  maxParallel: number;
  bufferSize: number;
  timeoutMs: number;
  stopOnError: boolean;
}

export interface FilterCondition {
  matchType: MatchCondition;
  field: string;
  value: string;
  targetFilter: number;
}

export interface ChainStats {
  totalProcessed: number;
  totalErrors: number;
  totalBypassed: number;
  avgLatencyMs: number;
  maxLatencyMs: number;
  throughputMbps: number;
  activeFilters: number;
}

export interface RouterConfig {
  strategy: RoutingStrategy;
  hashSeed: number;
  routeTable: Record<string, any>; // Map of conditions to chains
  customRouterData: any;
}

// ============================================================================
// Callback Types (matching mcp_c_filter_chain.h)
// ============================================================================

export type RoutingFunction = (
  buffer: number,
  nodes: FilterNode[],
  nodeCount: number,
  userData: any
) => number;

export type ChainEventCallback = (
  chain: number,
  oldState: ChainState,
  newState: ChainState,
  userData: any
) => void;

export type FilterMatchCallback = (
  buffer: number,
  metadata: ProtocolMetadata,
  userData: any
) => boolean;

// ============================================================================
// Advanced Chain Builder
// ============================================================================

/**
 * Create chain builder with configuration
 */
export function createChainBuilderEx(
  dispatcher: number,
  _config: ChainConfig
): any {
  // For now, use the basic chain builder since the advanced one requires a valid C struct
  // The advanced config will be implemented later when proper C struct conversion is ready
  // TODO: Implement proper C struct conversion when the C++ side is ready
  const builder = mcpFilterLib.mcp_filter_chain_builder_create(dispatcher);
  return builder;
}

export function buildFilterChain(builder: any): number {
  return mcpFilterLib.mcp_filter_chain_build(builder);
}

export function destroyFilterChainBuilder(builder: any): void {
  mcpFilterLib.mcp_filter_chain_builder_destroy(builder);
}

/**
 * Add filter node to chain
 */
export function addFilterNodeToChain(builder: any, _node: FilterNode): number {
  // For now, pass null as node since the C++ function expects a pointer to C struct
  // TODO: Implement proper C struct conversion when the C++ side is ready
  return mcpFilterLib.mcp_chain_builder_add_node(builder, null) as number;
}

/**
 * Add conditional filter
 */
export function addConditionalFilter(
  builder: any,
  _condition: FilterCondition,
  filter: number
): number {
  // For now, pass null as condition since the C++ function expects a pointer to C struct
  // TODO: Implement proper C struct conversion when the C++ side is ready
  return mcpFilterLib.mcp_chain_builder_add_conditional(
    builder,
    null,
    filter
  ) as number;
}

/**
 * Add parallel filter group
 */
export function addParallelFilterGroup(
  builder: any,
  filters: number[],
  count: number
): number {
  return mcpFilterLib.mcp_chain_builder_add_parallel_group(
    builder,
    filters,
    count
  ) as number;
}

/**
 * Set custom routing function
 */
export function setChainBuilderRouter(
  builder: any,
  router: RoutingFunction,
  userData: any
): number {
  return mcpFilterLib.mcp_chain_builder_set_router(
    builder,
    router,
    userData
  ) as number;
}

// ============================================================================
// Chain Management
// ============================================================================

/**
 * Get chain state
 */
export function getChainState(chain: number): ChainState {
  return mcpFilterLib.mcp_chain_get_state(chain) as ChainState;
}

/**
 * Pause chain execution
 */
export function pauseChain(chain: number): number {
  return mcpFilterLib.mcp_chain_pause(chain) as number;
}

/**
 * Resume chain execution
 */
export function resumeChain(chain: number): number {
  return mcpFilterLib.mcp_chain_resume(chain) as number;
}

/**
 * Reset chain to initial state
 */
export function resetChain(chain: number): number {
  return mcpFilterLib.mcp_chain_reset(chain) as number;
}

/**
 * Enable/disable filter in chain
 */
export function setFilterEnabled(
  chain: number,
  filterName: string,
  enabled: boolean
): number {
  return mcpFilterLib.mcp_chain_set_filter_enabled(
    chain,
    filterName,
    enabled
  ) as number;
}

/**
 * Get chain statistics
 */
export function getChainStats(chain: number, stats: ChainStats): number {
  return mcpFilterLib.mcp_chain_get_stats(chain, stats) as number;
}

/**
 * Set chain event callback
 */
export function setChainEventCallback(
  chain: number,
  callback: ChainEventCallback,
  userData: any
): number {
  return mcpFilterLib.mcp_chain_set_event_callback(
    chain,
    callback,
    userData
  ) as number;
}

// ============================================================================
// Dynamic Chain Composition
// ============================================================================

/**
 * Create dynamic chain from JSON configuration
 */
export function createChainFromJson(
  dispatcher: number,
  jsonConfig: any
): number {
  return mcpFilterLib.mcp_chain_create_from_json(
    dispatcher,
    jsonConfig
  ) as number;
}

/**
 * Export chain configuration to JSON
 */
export function exportChainToJson(chain: number): any {
  return mcpFilterLib.mcp_chain_export_to_json(chain);
}

/**
 * Clone a filter chain
 */
export function cloneChain(chain: number): number {
  return mcpFilterLib.mcp_chain_clone(chain) as number;
}

/**
 * Merge two chains
 */
export function mergeChains(
  chain1: number,
  chain2: number,
  mode: ChainExecutionMode
): number {
  return mcpFilterLib.mcp_chain_merge(chain1, chain2, mode) as number;
}

// ============================================================================
// Chain Router
// ============================================================================

/**
 * Create chain router
 */
export function createChainRouter(config: RouterConfig): any {
  return mcpFilterLib.mcp_chain_router_create(config);
}

/**
 * Add route to router
 */
export function addRouteToRouter(
  router: any,
  condition: FilterMatchCallback,
  chain: number
): number {
  return mcpFilterLib.mcp_chain_router_add_route(
    router,
    condition,
    chain
  ) as number;
}

/**
 * Route buffer through appropriate chain
 */
export function routeBuffer(
  router: any,
  buffer: number,
  metadata: ProtocolMetadata
): number {
  return mcpFilterLib.mcp_chain_router_route(
    router,
    buffer,
    metadata
  ) as number;
}

/**
 * Destroy chain router
 */
export function destroyChainRouter(router: any): void {
  mcpFilterLib.mcp_chain_router_destroy(router);
}

// ============================================================================
// Chain Pool for Load Balancing
// ============================================================================

/**
 * Create chain pool for load balancing
 */
export function createChainPool(
  baseChain: number,
  poolSize: number,
  strategy: RoutingStrategy
): any {
  return mcpFilterLib.mcp_chain_pool_create(baseChain, poolSize, strategy);
}

/**
 * Get next chain from pool
 */
export function getNextChainFromPool(pool: any): number {
  return mcpFilterLib.mcp_chain_pool_get_next(pool) as number;
}

/**
 * Return chain to pool
 */
export function returnChainToPool(pool: any, chain: number): void {
  mcpFilterLib.mcp_chain_pool_return(pool, chain);
}

/**
 * Get pool statistics
 */
export function getChainPoolStats(
  pool: any,
  active: number,
  idle: number,
  totalProcessed: number
): number {
  return mcpFilterLib.mcp_chain_pool_get_stats(
    pool,
    active,
    idle,
    totalProcessed
  ) as number;
}

/**
 * Destroy chain pool
 */
export function destroyChainPool(pool: any): void {
  mcpFilterLib.mcp_chain_pool_destroy(pool);
}

// ============================================================================
// Chain Optimization
// ============================================================================

/**
 * Optimize chain by removing redundant filters
 */
export function optimizeChain(chain: number): number {
  return mcpFilterLib.mcp_chain_optimize(chain) as number;
}

/**
 * Reorder filters for optimal performance
 */
export function reorderFilters(chain: number): number {
  return mcpFilterLib.mcp_chain_reorder_filters(chain) as number;
}

/**
 * Profile chain performance
 */
export function profileChain(
  chain: number,
  testBuffer: number,
  iterations: number,
  report: any
): number {
  return mcpFilterLib.mcp_chain_profile(
    chain,
    testBuffer,
    iterations,
    report
  ) as number;
}

// ============================================================================
// Chain Debugging
// ============================================================================

/**
 * Enable chain tracing
 */
export function setChainTraceLevel(chain: number, traceLevel: number): number {
  return mcpFilterLib.mcp_chain_set_trace_level(chain, traceLevel) as number;
}

/**
 * Dump chain structure
 */
export function dumpChain(chain: number, format: string): string {
  const result = mcpFilterLib.mcp_chain_dump(chain, format);
  return result ? result.toString() : "";
}

/**
 * Validate chain configuration
 */
export function validateChain(chain: number, errors: any): number {
  return mcpFilterLib.mcp_chain_validate(chain, errors) as number;
}

// ============================================================================
// Utility Functions for Chain Management
// ============================================================================

/**
 * Create a simple sequential chain
 */
export function createSimpleChain(
  dispatcher: number,
  filters: number[],
  name: string = "simple-chain"
): number {
  const config: ChainConfig = {
    name,
    mode: ChainExecutionMode.SEQUENTIAL,
    routing: RoutingStrategy.ROUND_ROBIN,
    maxParallel: 1,
    bufferSize: 8192,
    timeoutMs: 5000,
    stopOnError: false,
  };

  const builder = createChainBuilderEx(dispatcher, config);
  if (!builder) {
    throw new Error("Failed to create chain builder");
  }

  try {
    // Add all filters in sequence
    for (const filter of filters) {
      const result = addFilterNodeToChain(builder, {
        filter,
        name: `filter-${filter}`,
        priority: 0,
        enabled: true,
        bypassOnError: false,
        config: null,
      });

      if (result !== 0) {
        throw new Error(`Failed to add filter ${filter} to chain`);
      }
    }

    // Build the chain
    const chain = mcpFilterLib.mcp_filter_chain_build(builder);
    if (!chain) {
      throw new Error("Failed to build filter chain");
    }

    return chain as number;
  } finally {
    // Clean up builder
    mcpFilterLib.mcp_filter_chain_builder_destroy(builder);
  }
}

/**
 * Create a parallel processing chain
 */
export function createParallelChain(
  dispatcher: number,
  filters: number[],
  maxParallel: number = 4,
  name: string = "parallel-chain"
): number {
  const config: ChainConfig = {
    name,
    mode: ChainExecutionMode.PARALLEL,
    routing: RoutingStrategy.ROUND_ROBIN,
    maxParallel,
    bufferSize: 8192,
    timeoutMs: 5000,
    stopOnError: false,
  };

  const builder = createChainBuilderEx(dispatcher, config);
  if (!builder) {
    throw new Error("Failed to create parallel chain builder");
  }

  try {
    // Add parallel filter group
    const result = addParallelFilterGroup(builder, filters, filters.length);
    if (result !== 0) {
      throw new Error("Failed to add parallel filter group");
    }

    // Build the chain
    const chain = mcpFilterLib.mcp_filter_chain_build(builder);
    if (!chain) {
      throw new Error("Failed to build parallel filter chain");
    }

    return chain as number;
  } finally {
    // Clean up builder
    mcpFilterLib.mcp_filter_chain_builder_destroy(builder);
  }
}

/**
 * Create a conditional chain with routing
 */
export function createConditionalChain(
  dispatcher: number,
  conditions: Array<{ condition: FilterCondition; chain: number }>,
  name: string = "conditional-chain"
): number {
  const config: ChainConfig = {
    name,
    mode: ChainExecutionMode.CONDITIONAL,
    routing: RoutingStrategy.CUSTOM,
    maxParallel: 1,
    bufferSize: 8192,
    timeoutMs: 5000,
    stopOnError: false,
  };

  const builder = createChainBuilderEx(dispatcher, config);
  if (!builder) {
    throw new Error("Failed to create conditional chain builder");
  }

  try {
    // Add conditional filters
    for (const { condition, chain } of conditions) {
      const result = addConditionalFilter(builder, condition, chain);
      if (result !== 0) {
        throw new Error(`Failed to add conditional filter for chain ${chain}`);
      }
    }

    // Build the chain
    const chainHandle = mcpFilterLib.mcp_filter_chain_build(builder);
    if (!chainHandle) {
      throw new Error("Failed to build conditional filter chain");
    }

    return chainHandle as number;
  } finally {
    // Clean up builder
    mcpFilterLib.mcp_filter_chain_builder_destroy(builder);
  }
}
