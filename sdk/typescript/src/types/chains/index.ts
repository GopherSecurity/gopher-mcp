/**
 * @file chain-types.ts
 * @brief TypeScript type definitions for MCP Filter Chain API types
 *
 * This file provides TypeScript interfaces and types that map 1:1
 * with the filter chain API types defined in mcp_filter_chain.h
 */

import { McpFilter, McpFilterChain, McpProtocolMetadata } from "../filters";
import {
  McpBool,
  McpBufferHandle,
  McpJsonValue,
  McpMap,
  fromBoolean,
  toBoolean,
} from "../mcp-types";

// ============================================================================
// Chain Types and Enumerations
// ============================================================================

/**
 * Chain execution mode
 */
export enum McpChainExecutionMode {
  SEQUENTIAL = 0, // Execute filters in order
  PARALLEL = 1, // Execute filters in parallel
  CONDITIONAL = 2, // Execute based on conditions
  PIPELINE = 3, // Pipeline mode with buffering
}

/**
 * Chain routing strategy
 */
export enum McpRoutingStrategy {
  ROUND_ROBIN = 0, // Round-robin distribution
  LEAST_LOADED = 1, // Route to least loaded filter
  HASH_BASED = 2, // Hash-based routing
  PRIORITY = 3, // Priority-based routing
  CUSTOM = 99, // Custom routing function
}

/**
 * Filter match condition
 */
export enum McpMatchCondition {
  ALL = 0, // Match all conditions
  ANY = 1, // Match any condition
  NONE = 2, // Match no conditions
  CUSTOM = 99, // Custom match function
}

/**
 * Chain state
 */
export enum McpChainState {
  IDLE = 0,
  PROCESSING = 1,
  PAUSED = 2,
  ERROR = 3,
  COMPLETED = 4,
}

// ============================================================================
// Data Structures
// ============================================================================

/**
 * Filter node in chain
 */
export interface McpFilterNode {
  filter: McpFilter; // Filter handle
  name: string; // Filter name
  priority: number; // Execution priority
  enabled: McpBool; // Whether filter is enabled
  bypassOnError: McpBool; // Whether to bypass on error
  config: McpJsonValue; // Filter configuration
}

/**
 * Chain configuration
 */
export interface McpChainConfig {
  name: string; // Chain name
  mode: McpChainExecutionMode; // Execution mode
  routing: McpRoutingStrategy; // Routing strategy
  maxParallel: number; // Maximum parallel filters
  bufferSize: number; // Buffer size for pipeline mode
  timeoutMs: number; // Operation timeout
  stopOnError: McpBool; // Whether to stop on error
}

/**
 * Filter condition for conditional execution
 */
export interface McpFilterCondition {
  matchType: McpMatchCondition; // Match condition type
  field: string; // Field to match on
  value: string; // Value to match
  targetFilter: McpFilter; // Target filter for execution
}

/**
 * Chain statistics
 */
export interface McpChainStats {
  totalProcessed: number; // Total items processed
  totalErrors: number; // Total errors encountered
  totalBypassed: number; // Total filters bypassed
  avgLatencyMs: number; // Average latency in milliseconds
  maxLatencyMs: number; // Maximum latency in milliseconds
  throughputMbps: number; // Throughput in Mbps
  activeFilters: number; // Number of active filters
}

/**
 * Router configuration
 */
export interface McpRouterConfig {
  strategy: McpRoutingStrategy; // Routing strategy
  hashSeed: number; // Hash seed for hash-based routing
  routeTable: McpMap; // Map of conditions to chains
  customRouterData: any; // Custom router data
}

// ============================================================================
// Callback Types
// ============================================================================

/**
 * Custom routing function
 */
export type McpRoutingFunction = (
  buffer: McpBufferHandle,
  nodes: McpFilterNode[],
  nodeCount: number,
  userData: any
) => McpFilter;

/**
 * Chain event callback
 */
export type McpChainEventCallback = (
  chain: McpFilterChain,
  oldState: McpChainState,
  newState: McpChainState,
  userData: any
) => void;

/**
 * Filter match function
 */
export type McpFilterMatchCallback = (
  buffer: McpBufferHandle,
  metadata: McpProtocolMetadata,
  userData: any
) => McpBool;

// ============================================================================
// Advanced Chain Types
// ============================================================================

/**
 * Chain router handle
 */
export type McpChainRouter = number;

/**
 * Chain pool handle for load balancing
 */
export type McpChainPool = number;

/**
 * Chain pool statistics
 */
export interface McpChainPoolStats {
  active: number; // Number of active chains
  idle: number; // Number of idle chains
  totalProcessed: number; // Total items processed
}

/**
 * Chain performance profile result
 */
export interface McpChainProfileResult {
  executionTime: number; // Total execution time
  filterTimes: Map<string, number>; // Time per filter
  memoryUsage: number; // Memory usage
  throughput: number; // Throughput achieved
  bottlenecks: string[]; // Identified bottlenecks
}

/**
 * Chain validation error
 */
export interface McpChainValidationError {
  code: string; // Error code
  message: string; // Error message
  filter?: string; // Affected filter
  position?: number; // Position in chain
  severity: "warning" | "error"; // Error severity
}

/**
 * Chain validation result
 */
export interface McpChainValidationResult {
  valid: boolean; // Whether chain is valid
  errors: McpChainValidationError[]; // Validation errors
  warnings: McpChainValidationError[]; // Validation warnings
}

// ============================================================================
// Chain Optimization Types
// ============================================================================

/**
 * Chain optimization strategy
 */
export enum McpChainOptimizationStrategy {
  REMOVE_REDUNDANT = 0, // Remove redundant filters
  REORDER_FILTERS = 1, // Reorder for optimal performance
  MERGE_FILTERS = 2, // Merge compatible filters
  SPLIT_CHAINS = 3, // Split into parallel chains
  CUSTOM = 99, // Custom optimization
}

/**
 * Chain optimization result
 */
export interface McpChainOptimizationResult {
  originalChain: McpFilterChain; // Original chain
  optimizedChain: McpFilterChain; // Optimized chain
  improvements: string[]; // List of improvements
  performanceGain: number; // Performance improvement percentage
  memoryReduction: number; // Memory reduction percentage
}

// ============================================================================
// Chain Debugging Types
// ============================================================================

/**
 * Chain trace level
 */
export enum McpChainTraceLevel {
  OFF = 0, // No tracing
  BASIC = 1, // Basic tracing
  DETAILED = 2, // Detailed tracing
  VERBOSE = 3, // Verbose tracing
}

/**
 * Chain trace event
 */
export interface McpChainTraceEvent {
  timestamp: number; // Event timestamp
  filter: string; // Filter name
  event: string; // Event type
  data?: any; // Event data
  duration?: number; // Event duration
}

/**
 * Chain dump format
 */
export enum McpChainDumpFormat {
  TEXT = "text",
  JSON = "json",
  DOT = "dot",
}

// ============================================================================
// Type Guards and Utilities
// ============================================================================

/**
 * Check if chain state is active
 */
export function isChainActive(state: McpChainState): boolean {
  return state === McpChainState.PROCESSING;
}

/**
 * Check if chain state is idle
 */
export function isChainIdle(state: McpChainState): boolean {
  return state === McpChainState.IDLE;
}

/**
 * Check if chain state indicates error
 */
export function isChainError(state: McpChainState): boolean {
  return state === McpChainState.ERROR;
}

/**
 * Check if chain execution mode supports parallel processing
 */
export function supportsParallel(mode: McpChainExecutionMode): boolean {
  return mode === McpChainExecutionMode.PARALLEL || mode === McpChainExecutionMode.PIPELINE;
}

/**
 * Check if routing strategy is custom
 */
export function isCustomRouting(strategy: McpRoutingStrategy): boolean {
  return strategy === McpRoutingStrategy.CUSTOM;
}

/**
 * Check if match condition is custom
 */
export function isCustomMatch(condition: McpMatchCondition): boolean {
  return condition === McpMatchCondition.CUSTOM;
}

/**
 * Create a filter node with default values
 */
export function createFilterNode(
  filter: McpFilter,
  name: string,
  priority: number = 0,
  enabled: boolean = true,
  bypassOnError: boolean = false
): McpFilterNode {
  return {
    filter,
    name,
    priority,
    enabled: fromBoolean(enabled),
    bypassOnError: fromBoolean(bypassOnError),
    config: 0, // Will be set by user
  };
}

/**
 * Create a chain configuration with default values
 */
export function createChainConfig(
  name: string,
  mode: McpChainExecutionMode = McpChainExecutionMode.SEQUENTIAL,
  routing: McpRoutingStrategy = McpRoutingStrategy.ROUND_ROBIN
): McpChainConfig {
  return {
    name,
    mode,
    routing,
    maxParallel: 1,
    bufferSize: 1024,
    timeoutMs: 30000,
    stopOnError: fromBoolean(true),
  };
}

/**
 * Check if filter node is enabled
 */
export function isFilterNodeEnabled(node: McpFilterNode): boolean {
  return toBoolean(node.enabled);
}

/**
 * Check if filter node should bypass on error
 */
export function shouldBypassOnError(node: McpFilterNode): boolean {
  return toBoolean(node.bypassOnError);
}
