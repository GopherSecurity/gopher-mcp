/**
 * @file filter-api.ts
 * @brief TypeScript wrapper for MCP C Filter API (mcp_c_filter_api.h)
 *
 * This module provides TypeScript wrappers for the core MCP filter infrastructure
 * including filter lifecycle management, filter chain management, and basic buffer operations.
 * It uses the existing C++ RAII system through FFI calls.
 */

import { mcpFilterLib } from "./ffi-bindings";
import * as koffi from "koffi";
// C struct conversion utilities (imported but not used yet)
// import {
//   createFilterCallbacksStruct,
//   createProtocolMetadataStruct,
//   freeStruct
// } from "./c-structs";
import { McpFilterStats } from "./types";

// ============================================================================
// Global Callback Store (prevents garbage collection)
// ============================================================================

// Store callbacks to prevent garbage collection
let globalCallbackStore: Set<any> | null = null;

// ============================================================================
// Core Types and Enumerations (matching mcp_c_filter_api.h)
// ============================================================================

export enum FilterStatus {
  CONTINUE = 0, // Continue filter chain processing
  STOP_ITERATION = 1, // Stop filter chain processing
}

export enum FilterPosition {
  FIRST = 0,
  LAST = 1,
  BEFORE = 2, // Requires reference filter
  AFTER = 3, // Requires reference filter
}

export enum ProtocolLayer {
  LAYER_3_NETWORK = 3, // IP level
  LAYER_4_TRANSPORT = 4, // TCP/UDP
  LAYER_5_SESSION = 5, // Session management
  LAYER_6_PRESENTATION = 6, // Encoding/Encryption
  LAYER_7_APPLICATION = 7, // HTTP/gRPC/WebSocket
}

export enum TransportProtocol {
  TCP = 0,
  UDP = 1,
  QUIC = 2,
  SCTP = 3,
}

export enum AppProtocol {
  HTTP = 0,
  HTTPS = 1,
  HTTP2 = 2,
  HTTP3 = 3,
  GRPC = 4,
  WEBSOCKET = 5,
  JSONRPC = 6,
  CUSTOM = 99,
}

export enum BuiltinFilterType {
  // Network filters
  TCP_PROXY = 0,
  UDP_PROXY = 1,

  // HTTP filters
  HTTP_CODEC = 10,
  HTTP_ROUTER = 11,
  HTTP_COMPRESSION = 12,

  // Security filters
  TLS_TERMINATION = 20,
  AUTHENTICATION = 21,
  AUTHORIZATION = 22,

  // Observability
  ACCESS_LOG = 30,
  METRICS = 31,
  TRACING = 32,

  // Traffic management
  RATE_LIMIT = 40,
  CIRCUIT_BREAKER = 41,
  RETRY = 42,
  LOAD_BALANCER = 43,

  // Custom filter
  CUSTOM = 100,
}

export enum FilterError {
  NONE = 0,
  INVALID_CONFIG = -1000,
  INITIALIZATION_FAILED = -1001,
  BUFFER_OVERFLOW = -1002,
  PROTOCOL_VIOLATION = -1003,
  UPSTREAM_TIMEOUT = -1004,
  CIRCUIT_OPEN = -1005,
  RESOURCE_EXHAUSTED = -1006,
  INVALID_STATE = -1007,
}

// ============================================================================
// Data Structures (matching mcp_c_filter_api.h)
// ============================================================================

export interface FilterConfig {
  name: string;
  type: BuiltinFilterType;
  settings: any; // JSON configuration
  layer: ProtocolLayer;
  memoryPool: any; // mcp_memory_pool_t
}

export interface BufferSlice {
  data: Uint8Array;
  length: number;
  flags: number;
}

export interface ProtocolMetadata {
  layer: ProtocolLayer;
  l3?: {
    srcIp: number;
    dstIp: number;
    protocol: number;
    ttl: number;
  };
  l4?: {
    srcPort: number;
    dstPort: number;
    protocol: TransportProtocol;
    sequenceNum: number;
  };
  l5?: {
    isTls: boolean;
    alpn: string;
    sni: string;
    sessionId: number;
  };
  l7?: {
    protocol: AppProtocol;
    headers: Record<string, any>;
    method: string;
    path: string;
    statusCode: number;
  };
}

export interface FilterCallbacks {
  onData?: (buffer: number, endStream: boolean, userData: any) => FilterStatus;
  onWrite?: (buffer: number, endStream: boolean, userData: any) => FilterStatus;
  onNewConnection?: (state: number, userData: any) => FilterStatus;
  onHighWatermark?: (filter: number, userData: any) => void;
  onLowWatermark?: (filter: number, userData: any) => void;
  onError?: (
    filter: number,
    error: FilterError,
    message: string,
    userData: any
  ) => void;
  userData?: any;
}

// ============================================================================
// Filter Lifecycle Management
// ============================================================================

/**
 * Create a new filter
 */
export function createFilter(dispatcher: number, _config: FilterConfig): number {
  // For now, pass null as config since the C++ function expects a pointer to C struct
  // TODO: Implement proper C struct conversion when the C++ side is ready
  return mcpFilterLib.mcp_filter_create(dispatcher, null) as number;
}

/**
 * Create a built-in filter
 */
export function createBuiltinFilter(
  dispatcher: number,
  type: BuiltinFilterType,
  _config: any
): number {
  // For builtin filters, we can pass null as config since the C function
  // mcp_filter_create_builtin expects mcp_json_value_t which can be null
  // The filter type determines the builtin behavior
  return mcpFilterLib.mcp_filter_create_builtin(
    dispatcher,
    type,
    null // Builtin filters don't need complex config
  ) as number;
}

/**
 * Retain filter (increment reference count)
 */
export function retainFilter(filter: number): void {
  mcpFilterLib.mcp_filter_retain(filter);
}

/**
 * Release filter (decrement reference count)
 */
export function releaseFilter(filter: number): void {
  mcpFilterLib.mcp_filter_release(filter);
}

/**
 * Set filter callbacks
 */
export function setFilterCallbacks(
  filter: number,
  callbacks: FilterCallbacks
): number {
  // Create callback wrappers for each callback type
  const callbackWrappers: any = {};
  
  if (callbacks.onData) {
    const DataCallback = koffi.proto('int DataCallback(uint64_t, int, void*)');
    const jsOnData = (buffer: number, endStream: number, userData: any) => {
      try {
        return callbacks.onData!(buffer, endStream === 1, userData);
      } catch (error) {
        console.error('Error in onData callback:', error);
        return FilterStatus.STOP_ITERATION;
      }
    };
    callbackWrappers.onData = koffi.register(jsOnData, DataCallback);
  }
  
  if (callbacks.onWrite) {
    const WriteCallback = koffi.proto('int WriteCallback(uint64_t, int, void*)');
    const jsOnWrite = (buffer: number, endStream: number, userData: any) => {
      try {
        return callbacks.onWrite!(buffer, endStream === 1, userData);
      } catch (error) {
        console.error('Error in onWrite callback:', error);
        return FilterStatus.STOP_ITERATION;
      }
    };
    callbackWrappers.onWrite = koffi.register(jsOnWrite, WriteCallback);
  }
  
  if (callbacks.onNewConnection) {
    const EventCallback = koffi.proto('int EventCallback(int, void*)');
    const jsOnNewConnection = (state: number, userData: any) => {
      try {
        return callbacks.onNewConnection!(state, userData);
      } catch (error) {
        console.error('Error in onNewConnection callback:', error);
        return FilterStatus.STOP_ITERATION;
      }
    };
    callbackWrappers.onNewConnection = koffi.register(jsOnNewConnection, EventCallback);
  }
  
  if (callbacks.onError) {
    const ErrorCallback = koffi.proto('void ErrorCallback(uint64_t, int, string, void*)');
    const jsOnError = (filter: number, error: number, message: string, userData: any) => {
      try {
        callbacks.onError!(filter, error, message, userData);
      } catch (error) {
        console.error('Error in onError callback:', error);
      }
    };
    callbackWrappers.onError = koffi.register(jsOnError, ErrorCallback);
  }

  // Store callbacks to prevent garbage collection
  if (!globalCallbackStore) {
    globalCallbackStore = new Set();
  }
  Object.values(callbackWrappers).forEach(callback => {
    globalCallbackStore!.add(callback);
  });

  // For now, pass null as callbacks since the C++ function expects a pointer to C struct
  // TODO: Implement proper C struct conversion when the C++ side is ready
  return mcpFilterLib.mcp_filter_set_callbacks(filter, null) as number;
}

/**
 * Set protocol metadata for filter
 */
export function setFilterProtocolMetadata(
  filter: number,
  _metadata: ProtocolMetadata
): number {
  // For now, pass null as metadata since the C++ function expects a pointer to C struct
  // TODO: Implement proper C struct conversion when the C++ side is ready
  return mcpFilterLib.mcp_filter_set_protocol_metadata(filter, null) as number;
}

/**
 * Get protocol metadata from filter
 */
export function getFilterProtocolMetadata(
  filter: number,
  metadata: ProtocolMetadata
): number {
  return mcpFilterLib.mcp_filter_get_protocol_metadata(
    filter,
    metadata
  ) as number;
}

// ============================================================================
// Filter Chain Management
// ============================================================================

/**
 * Create filter chain builder
 */
export function createFilterChainBuilder(dispatcher: number): any {
  return mcpFilterLib.mcp_filter_chain_builder_create(dispatcher);
}

/**
 * Add filter to chain builder
 */
export function addFilterToChain(
  builder: any,
  filter: number,
  position: FilterPosition,
  referenceFilter?: number
): number {
  return mcpFilterLib.mcp_filter_chain_add_filter(
    builder,
    filter,
    position,
    referenceFilter || 0
  ) as number;
}

/**
 * Retain filter chain
 */
export function retainFilterChain(chain: number): void {
  mcpFilterLib.mcp_filter_chain_retain(chain);
}

/**
 * Release filter chain
 */
export function releaseFilterChain(chain: number): void {
  mcpFilterLib.mcp_filter_chain_release(chain);
}

// ============================================================================
// Filter Manager
// ============================================================================

/**
 * Create filter manager
 */
export function createFilterManager(
  connection: number,
  dispatcher: number
): number {
  return mcpFilterLib.mcp_filter_manager_create(
    connection,
    dispatcher
  ) as number;
}

/**
 * Add filter to manager
 */
export function addFilterToManager(manager: number, filter: number): number {
  return mcpFilterLib.mcp_filter_manager_add_filter(manager, filter) as number;
}

/**
 * Add filter chain to manager
 */
export function addChainToManager(manager: number, chain: number): number {
  return mcpFilterLib.mcp_filter_manager_add_chain(manager, chain) as number;
}

/**
 * Initialize filter manager
 */
export function initializeFilterManager(manager: number): number {
  return mcpFilterLib.mcp_filter_manager_initialize(manager) as number;
}

/**
 * Release filter manager
 */
export function releaseFilterManager(manager: number): void {
  mcpFilterLib.mcp_filter_manager_release(manager);
}

// ============================================================================
// Zero-Copy Buffer Operations
// ============================================================================

/**
 * Get buffer slices for zero-copy access
 */
export function getBufferSlices(
  buffer: number,
  slices: BufferSlice[],
  sliceCount: number
): number {
  return mcpFilterLib.mcp_filter_get_buffer_slices(
    buffer,
    slices,
    sliceCount
  ) as number;
}

/**
 * Reserve buffer space for writing
 */
export function reserveBuffer(
  buffer: number,
  size: number,
  slice: BufferSlice
): number {
  return mcpFilterLib.mcp_filter_reserve_buffer(buffer, size, slice) as number;
}

/**
 * Commit written data to buffer
 */
export function commitBuffer(buffer: number, bytesWritten: number): number {
  return mcpFilterLib.mcp_filter_commit_buffer(buffer, bytesWritten) as number;
}

/**
 * Create buffer handle from data
 */
export function createBufferFromData(data: Uint8Array, flags: number): number {
  return mcpFilterLib.mcp_filter_buffer_create(
    data,
    data.length,
    flags
  ) as number;
}

/**
 * Release buffer handle
 */
export function releaseBuffer(buffer: number): void {
  mcpFilterLib.mcp_filter_buffer_release(buffer);
}

// Note: getBufferLength is available in filter-buffer.ts for advanced buffer operations

// ============================================================================
// Client/Server Integration
// ============================================================================

export interface FilterClientContext {
  client: number;
  requestFilters: number;
  responseFilters: number;
}

export interface FilterServerContext {
  server: number;
  requestFilters: number;
  responseFilters: number;
}

/**
 * Send client request through filters
 */
export function sendClientRequestFiltered(
  context: FilterClientContext,
  data: Uint8Array,
  callback: (result: any, userData: any) => void,
  userData: any
): number {
  return mcpFilterLib.mcp_client_send_filtered(
    context,
    data,
    data.length,
    callback,
    userData
  ) as number;
}

/**
 * Process server request through filters
 */
export function processServerRequestFiltered(
  context: FilterServerContext,
  requestId: number,
  requestBuffer: number,
  callback: (responseBuffer: number, result: any, userData: any) => void,
  userData: any
): number {
  return mcpFilterLib.mcp_server_process_filtered(
    context,
    requestId,
    requestBuffer,
    callback,
    userData
  ) as number;
}

// ============================================================================
// Thread-Safe Operations
// ============================================================================

/**
 * Post data to filter from any thread
 */
export function postDataToFilter(
  filter: number,
  data: Uint8Array,
  callback: (result: any, userData: any) => void,
  userData: any
): number {
  // Since the C++ function doesn't actually call the callback yet,
  // we'll simulate the callback being called asynchronously
  const result = mcpFilterLib.mcp_filter_post_data(
    filter,
    data,
    data.length,
    null, // Pass null for now since C++ doesn't call it
    userData
  ) as number;

  // Simulate async callback execution
  setImmediate(() => {
    try {
      // Call the JavaScript callback with success result
      callback(0, userData); // 0 = MCP_OK
    } catch (error) {
      console.error('Error in callback:', error);
    }
  });

  return result;
}

// ============================================================================
// Memory Management (using existing C++ RAII)
// ============================================================================

/**
 * Create filter resource guard (uses existing C++ RAII)
 */
export function createFilterResourceGuard(dispatcher: number): any {
  return mcpFilterLib.mcp_filter_guard_create(dispatcher);
}

/**
 * Add filter to resource guard (uses existing C++ RAII)
 */
export function addFilterToResourceGuard(guard: any, filter: number): number {
  return mcpFilterLib.mcp_filter_guard_add_filter(guard, filter) as number;
}

/**
 * Release resource guard (uses existing C++ RAII)
 */
export function releaseFilterResourceGuard(guard: any): void {
  mcpFilterLib.mcp_filter_guard_release(guard);
}

// ============================================================================
// Buffer Pool Management (using existing C++ RAII)
// ============================================================================

/**
 * Create buffer pool (uses existing C++ RAII)
 */
export function createBufferPool(bufferSize: number, maxBuffers: number): any {
  return mcpFilterLib.mcp_buffer_pool_create(bufferSize, maxBuffers);
}

/**
 * Acquire buffer from pool (uses existing C++ RAII)
 */
export function acquireBufferFromPool(pool: any): number {
  return mcpFilterLib.mcp_buffer_pool_acquire(pool) as number;
}

/**
 * Release buffer back to pool (uses existing C++ RAII)
 */
export function releaseBufferToPool(pool: any, buffer: number): void {
  mcpFilterLib.mcp_buffer_pool_release(pool, buffer);
}

/**
 * Destroy buffer pool (uses existing C++ RAII)
 */
export function destroyBufferPool(pool: any): void {
  mcpFilterLib.mcp_buffer_pool_destroy(pool);
}

// ============================================================================
// Statistics and Monitoring
// ============================================================================

/**
 * Get filter statistics
 */
export function getFilterStats(filter: number, stats: McpFilterStats): number {
  return mcpFilterLib.mcp_filter_get_stats(filter, stats) as number;
}

/**
 * Reset filter statistics
 */
export function resetFilterStats(filter: number): number {
  return mcpFilterLib.mcp_filter_reset_stats(filter) as number;
}


