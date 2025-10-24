/**
 * @file filter-chain.ts
 * @brief TypeScript wrapper for MCP C Filter Chain API (mcp_c_filter_chain.h)
 *
 * This module provides TypeScript wrappers for advanced filter chain management
 * including dynamic composition, conditional execution, parallel processing,
 * routing, and optimization. It uses the existing C++ RAII system through FFI calls.
 */

import * as koffi from "koffi";
import { mcpFilterLib } from "./mcp-ffi-bindings";
import { ValidationResultStruct, AssemblyResultStruct } from "./mcp-c-structs";
import { ProtocolMetadata } from "./mcp-filter-api";

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

const MCP_OK = 0;

type JsonHandle = any;

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
// Assembler-based Chain Configuration
// ============================================================================

export interface FilterConfig {
  type: string;
  name?: string;
  config?: any; // JSON configuration
  enabled?: boolean;
  enabledWhen?: any; // JSON condition
}

export interface FilterChainConfig {
  name?: string;
  transportType?: string;
  filters: FilterConfig[];
}

// ============================================================================
// Canonical Listener-based Configuration (matching C++ ListenerConfig)
// ============================================================================

export interface SocketAddress {
  address: string;
  port_value: number;
}

export interface Address {
  socket_address: SocketAddress;
}

export interface FilterSpec {
  name: string;
  type: string;
  config?: any; // Optional filter-specific configuration
}

export interface FilterChain {
  filters: FilterSpec[];
  name?: string;
  transport_socket?: any; // Optional TLS configuration
}

export interface ListenerConfig {
  name: string;
  address: Address;
  filter_chains: FilterChain[];
  per_connection_buffer_limit_bytes?: number;
  metadata?: any;
}

export interface CanonicalConfig {
  listeners: ListenerConfig[];
}

export interface ValidationResult {
  valid: boolean;
  errors: string[];
  warnings: string[];
}

export interface AssemblyResult {
  success: boolean;
  chain?: number;
  errorMessage?: string;
  createdFilters: string[];
  warnings: string[];
}

function normalizeFilterChainConfig(config: FilterChainConfig): Record<string, unknown> {
  const filters = config.filters || [];
  return {
    name: config.name ?? "default",
    transport_type: config.transportType ?? "tcp",
    filters: filters.map((filter, index) => ({
      type: filter.type,
      name: filter.name ?? `${filter.type || "filter"}:${index}`,
      config: filter.config ?? {},
      // Note: enabled and enabled_when fields removed - they're not supported by C API during chain creation
      // These can be set later via mcp_chain_set_filter_enabled() if needed
    })),
  };
}

/**
 * Convert canonical listener configuration to assembler format
 */
function canonicalToAssemblerConfig(config: CanonicalConfig): FilterChainConfig {
  // For now, we'll take the first listener's first filter chain
  // In a full implementation, this could be more sophisticated
  if (!config.listeners || config.listeners.length === 0) {
    throw new Error("No listeners defined in canonical configuration");
  }

  const listener = config.listeners[0];
  if (!listener || !listener.filter_chains || listener.filter_chains.length === 0) {
    throw new Error("No filter chains defined in listener");
  }

  const filterChain = listener.filter_chains[0];
  if (!filterChain) {
    throw new Error("No filter chain at index 0");
  }

  return {
    name: filterChain.name || listener.name,
    transportType: "tcp", // Default, could be inferred from listener type
    filters: (filterChain.filters || []).map(filter => ({
      type: filter.type,
      name: filter.name,
      config: filter.config,
      enabled: true,
    })),
  };
}

/**
 * Check if configuration is in canonical format
 */
function isCanonicalConfig(config: any): config is CanonicalConfig {
  return config &&
         typeof config === 'object' &&
         'listeners' in config &&
         Array.isArray(config.listeners);
}

function createJsonHandleFromConfig(config: FilterChainConfig): JsonHandle {
  const normalized = normalizeFilterChainConfig(config);
  const jsonStr = JSON.stringify(normalized);
  const handle = mcpFilterLib.mcp_json_parse(jsonStr);
  if (!handle) {
    throw new Error("Failed to parse filter chain configuration JSON");
  }
  return handle;
}

/**
 * Validate filter chain configuration using the assembler-aware API
 */
export function validateFilterChainConfig(config: FilterChainConfig): ValidationResult {
  const jsonHandle = createJsonHandleFromConfig(config);
  const resultPtr = koffi.alloc(ValidationResultStruct, 1);

  try {
    const status = mcpFilterLib.mcp_chain_validate_json(
      jsonHandle,
      koffi.as(resultPtr, "void*")
    ) as number;

    if (status !== MCP_OK) {
      return { valid: false, errors: [`Validation failed with code: ${status}`], warnings: [] };
    }

    const buffer: Buffer = resultPtr;
    const valid = buffer.readUInt8(0) !== 0;

    return { valid, errors: [], warnings: [] };
  } finally {
    mcpFilterLib.mcp_chain_validation_result_free(koffi.as(resultPtr, "void*"));
    koffi.free(resultPtr);
    mcpFilterLib.mcp_json_free(jsonHandle);
  }
}

/**
 * Assemble filter chain from configuration
 */
export function assembleFilterChain(dispatcher: any, config: FilterChainConfig): AssemblyResult {
  const jsonHandle = createJsonHandleFromConfig(config);
  const resultPtr = koffi.alloc(AssemblyResultStruct, 1);

  try {
    const status = mcpFilterLib.mcp_chain_assemble_from_json(
      dispatcher,
      jsonHandle,
      koffi.as(resultPtr, "void*")
    ) as number;

    const buffer: Buffer = resultPtr;
    const successFlag = buffer.readUInt8(0) !== 0;
    const chainHandle = Number(buffer.readBigUInt64LE(8));

    const success = status === MCP_OK && successFlag;
    let errorMessage: string | undefined;

    if (!success) {
      errorMessage = status !== MCP_OK ? `Assembly failed with code: ${status}` : "Assembler reported failure";
    }

    const assemblyResult: AssemblyResult = {
      success,
      createdFilters: [],
      warnings: [],
    };

    if (success && chainHandle !== 0) {
      assemblyResult.chain = chainHandle;
    }

    if (errorMessage) {
      assemblyResult.errorMessage = errorMessage;
    }

    return assemblyResult;
  } finally {
    mcpFilterLib.mcp_chain_assembly_result_free(koffi.as(resultPtr, "void*"));
    koffi.free(resultPtr);
    mcpFilterLib.mcp_json_free(jsonHandle);
  }
}

/**
 * Create filter chain directly from canonical configuration using assembler
 */
export function canonicalConfigToNormalizedJson(config: CanonicalConfig): string {
  const chainConfig = canonicalToAssemblerConfig(config);
  const normalized = normalizeFilterChainConfig(chainConfig);
  return JSON.stringify(normalized);
}

export function createFilterChainFromConfig(
  dispatcher: any,  // mcp_dispatcher_t (pointer type)
  config: CanonicalConfig
): number {
  if (!isCanonicalConfig(config)) {
    throw new Error("Configuration must be in canonical format with 'listeners' array");
  }

  const jsonStr = canonicalConfigToNormalizedJson(config);
  const jsonHandle = mcpFilterLib.mcp_json_parse(jsonStr);
  try {
    const rawHandle = mcpFilterLib.mcp_chain_create_from_json(dispatcher, jsonHandle);
    // Convert koffi pointer/uint64_t to number
    const handle = typeof rawHandle === 'number' ? rawHandle : Number(rawHandle);
    if (!handle) {
      throw new Error("Failed to create filter chain from configuration");
    }
    return handle;
  } finally {
    mcpFilterLib.mcp_json_free(jsonHandle);
  }
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
export function setFilterEnabled(chain: number, filterName: string, enabled: boolean): number {
  return mcpFilterLib.mcp_chain_set_filter_enabled(chain, filterName, enabled) as number;
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
  return mcpFilterLib.mcp_chain_set_event_callback(chain, callback, userData) as number;
}

// ============================================================================
// Dynamic Chain Composition
// ============================================================================

/**
 * Create dynamic chain from JSON configuration
 */
export function createChainFromJson(dispatcher: number, jsonConfig: any): number {
  return mcpFilterLib.mcp_chain_create_from_json(dispatcher, jsonConfig) as number;
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
export function mergeChains(chain1: number, chain2: number, mode: ChainExecutionMode): number {
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
  return mcpFilterLib.mcp_chain_router_add_route(router, condition, chain) as number;
}

/**
 * Route buffer through appropriate chain
 */
export function routeBuffer(router: any, buffer: number, metadata: ProtocolMetadata): number {
  return mcpFilterLib.mcp_chain_router_route(router, buffer, metadata) as number;
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
  return mcpFilterLib.mcp_chain_pool_get_stats(pool, active, idle, totalProcessed) as number;
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
  return mcpFilterLib.mcp_chain_profile(chain, testBuffer, iterations, report) as number;
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
 * Create a simple sequential chain using canonical configuration
 */
export function createSimpleChain(
  dispatcher: any,  // mcp_dispatcher_t (pointer type)
  filterTypes: string[],
  name: string = "simple-chain",
  port: number = 8080
): number {
  // Build canonical configuration
  const config: CanonicalConfig = {
    listeners: [
      {
        name: `${name}_listener`,
        address: {
          socket_address: {
            address: "127.0.0.1",
            port_value: port,
          },
        },
        filter_chains: [
          {
            filters: filterTypes.map((type, index) => ({
              name: `filter_${index}`,
              type: type,
            })),
          },
        ],
      },
    ],
  };

  return createFilterChainFromConfig(dispatcher, config);
}

/**
 * Create a parallel processing chain
 * Note: Parallel execution is handled by the filter chain implementation
 */
export function createParallelChain(
  dispatcher: any,  // mcp_dispatcher_t (pointer type)
  filterTypes: string[],
  maxParallel: number = 4,
  name: string = "parallel-chain",
  port: number = 8080
): number {
  // For parallel chains, we still use the canonical config
  // The parallel execution mode would be specified in the filter config
  const config: CanonicalConfig = {
    listeners: [
      {
        name: `${name}_listener`,
        address: {
          socket_address: {
            address: "127.0.0.1",
            port_value: port,
          },
        },
        filter_chains: [
          {
            filters: filterTypes.map((type, index) => ({
              name: `parallel_filter_${index}`,
              type: type,
              config: {
                execution_mode: "parallel",
                max_parallel: maxParallel,
              },
            })),
          },
        ],
      },
    ],
  };

  return createFilterChainFromConfig(dispatcher, config);
}

/**
 * Create a conditional chain with routing
 * Note: Conditional routing would be implemented via filter configuration
 */
export function createConditionalChain(
  dispatcher: any,  // mcp_dispatcher_t (pointer type)
  filterConfigs: Array<{ type: string; condition?: any }>,
  name: string = "conditional-chain",
  port: number = 8080
): number {
  const config: CanonicalConfig = {
    listeners: [
      {
        name: `${name}_listener`,
        address: {
          socket_address: {
            address: "127.0.0.1",
            port_value: port,
          },
        },
        filter_chains: [
          {
            filters: filterConfigs.map((fc, index) => ({
              name: `conditional_filter_${index}`,
              type: fc.type,
              config: fc.condition ? { condition: fc.condition } : undefined,
            })),
          },
        ],
      },
    ],
  };

  return createFilterChainFromConfig(dispatcher, config);
}
