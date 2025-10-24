/**
 * @file filter-api.ts
 * @brief TypeScript wrapper for MCP C Filter API (mcp_c_filter_api.h)
 *
 * This module provides TypeScript wrappers for the core MCP filter infrastructure
 * including filter lifecycle management, filter chain management, and basic buffer operations.
 * It uses the existing C++ RAII system through FFI calls.
 */

import * as koffi from "koffi";
import { mcpFilterLib, TransportType } from "./mcp-ffi-bindings";

const MCP_OK = 0;

const dispatcherPumpState = new Map<pointer, { active: boolean }>();

function startDispatcherPump(dispatcher: pointer): void {
  const state = { active: true };
  dispatcherPumpState.set(dispatcher, state);

  const pump = () => {
    if (!state.active) {
      return;
    }

    try {
      // Run dispatcher loop with zero timeout to process pending events
      mcpFilterLib.mcp_dispatcher_run_timeout(dispatcher, 0);
    } catch (error) {
      console.error('Dispatcher pump error:', error);
      state.active = false;
      return;
    }

    setImmediate(pump);
  };

  setImmediate(pump);
}
// C struct conversion utilities
import { createFilterCallbacksStruct } from "./mcp-c-structs";
import { McpFilterStats } from "./types";

// ============================================================================
// Global Callback Store (prevents garbage collection)
// ============================================================================

// Store callbacks to prevent garbage collection
let globalCallbackStore: Set<any> | null = null;

// ============================================================================
// MCP Library Initialization Helpers
// ============================================================================

/**
 * Ensure the native MCP library has been initialised. Safe to call multiple times.
 */
export function ensureMcpInitialized(): void {
  if (!mcpFilterLib) {
    throw new Error('MCP native library is not loaded');
  }

  console.log('🔍 [ensureMcpInitialized] Checking if library is initialized...');
  const isInitialized = mcpFilterLib.mcp_is_initialized
    ? mcpFilterLib.mcp_is_initialized() === 1
    : false;
  console.log(`🔍 [ensureMcpInitialized] Current initialization status: ${isInitialized}`);

  if (isInitialized) {
    console.log('🔍 [ensureMcpInitialized] Library already initialized, returning');
    return;
  }

  if (!mcpFilterLib.mcp_init) {
    throw new Error('mcp_init symbol not available in native library');
  }

  console.log('🔍 [ensureMcpInitialized] Calling mcp_init(null)...');
  const result = mcpFilterLib.mcp_init(null);
  console.log(`🔍 [ensureMcpInitialized] mcp_init returned: ${result} (0=success)`);

  if (result !== 0) {
    const afterInit = mcpFilterLib.mcp_is_initialized
      ? mcpFilterLib.mcp_is_initialized() === 1
      : false;
    console.log(`🔍 [ensureMcpInitialized] After failed init, mcp_is_initialized: ${afterInit}`);
    if (!afterInit) {
      throw new Error(`mcp_init failed with error code ${result}`);
    }
  } else {
    console.log('🔍 [ensureMcpInitialized] mcp_init succeeded');
    const verifyInit = mcpFilterLib.mcp_is_initialized
      ? mcpFilterLib.mcp_is_initialized() === 1
      : false;
    console.log(`🔍 [ensureMcpInitialized] Verification - mcp_is_initialized: ${verifyInit}`);
  }
}

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

export interface BasicFilterConfig {
  name: string;
  type: BuiltinFilterType;
  settings: any; // JSON configuration
  layer: ProtocolLayer;
  memoryPool: any; // mcp_memory_pool_t
}

export interface BufferSlice {
  data: Uint8Array | null;
  length: number;
  flags: number;
}

// BufferOwnership is exported from mcp-filter-buffer.ts to avoid conflicts

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
  onError?: (filter: number, error: FilterError, message: string, userData: any) => void;
  userData?: any;
}

// ============================================================================
// Filter Lifecycle Management
// ============================================================================

/**
 * Create a new filter
 */
export function createFilter(dispatcher: number, _config: BasicFilterConfig): number {
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
 * Create a dispatcher context for filter operations
 * @deprecated Use createRealDispatcher() for production or createStubDispatcher() for testing
 */
export function createDispatcher(): number {
  // Legacy function kept for backward compatibility
  // Will be removed in future version
  console.warn('createDispatcher() is deprecated. Use createRealDispatcher() or createStubDispatcher()');
  return 1; // Stub value for backward compatibility
}

/**
 * Create a custom filter with TypeScript callbacks (CApiFilter integration)
 */
export function createCustomFilter(
  dispatcher: number,
  _callbacks: {
    onData?: (buffer: number, endStream: boolean, userData: any) => number;
    onWrite?: (buffer: number, endStream: boolean, userData: any) => number;
    onNewConnection?: (state: number, userData: any) => number;
    onError?: (filter: number, error: number, message: string, userData: any) => void;
  },
  _userData?: any
): number {
  // 1. Create filter with a dummy config (the C function requires non-null config)
  // The actual implementation ignores the config and just creates a CApiFilter
  const dummyConfig = Buffer.alloc(1); // Minimal non-null buffer
  const filterHandle = mcpFilterLib.mcp_filter_create(dispatcher, dummyConfig);
  if (filterHandle === 0) {
    throw new Error("Failed to create filter");
  }

  // 2. Create callback structure
  // TODO: Store callbackStruct for future use when koffi issue is resolved
  // const callbackStruct = createFilterCallbacksStruct({
  //   on_data: callbacks.onData ?? undefined,
  //   on_write: callbacks.onWrite ?? undefined,
  //   on_new_connection: callbacks.onNewConnection ?? undefined,
  //   on_error: callbacks.onError ?? undefined,
  //   user_data: userData,
  // });

  // 3. Set callbacks (this makes it a CApiFilter)
  // Create callback structure using the working solution
  const callbackStruct = createFilterCallbacksStruct({
    on_data: _callbacks.onData ?? undefined,
    on_write: _callbacks.onWrite ?? undefined,
    on_new_connection: _callbacks.onNewConnection ?? undefined,
    on_error: _callbacks.onError ?? undefined,
    user_data: _userData,
  });

  // Set callbacks using the fixed C++ implementation
  // Convert the callback struct to a void* pointer for the C function
  const result = mcpFilterLib.mcp_filter_set_callbacks(
    filterHandle,
    koffi.as(callbackStruct, "void*")
  );
  if (result !== 0) {
    mcpFilterLib.mcp_filter_release(filterHandle);
    throw new Error(`Failed to set callbacks: ${result}`);
  }

  return filterHandle;
}

/**
 * Create a built-in filter with optional TypeScript callbacks
 */
export function createBuiltinFilterWithCallbacks(
  dispatcher: number,
  type: BuiltinFilterType,
  config: any,
  callbacks?: {
    onData?: (buffer: number, endStream: boolean, userData: any) => number;
    onWrite?: (buffer: number, endStream: boolean, userData: any) => number;
    onNewConnection?: (state: number, userData: any) => number;
    onError?: (filter: number, error: number, message: string, userData: any) => void;
  },
  _userData?: any
): number {
  // 1. Create built-in filter
  // Convert config to a buffer if it's not already
  const configBuffer = Buffer.isBuffer(config) ? config : Buffer.from(JSON.stringify(config || {}));
  const filterHandle = mcpFilterLib.mcp_filter_create_builtin(dispatcher, type, configBuffer);
  if (filterHandle === 0) {
    throw new Error(`Failed to create built-in filter of type ${type}`);
  }

  // 2. If callbacks provided, convert to CApiFilter
  if (callbacks) {
    // Create callback structure using the working solution
    const callbackStruct = createFilterCallbacksStruct({
      on_data: callbacks.onData ?? undefined,
      on_write: callbacks.onWrite ?? undefined,
      on_new_connection: callbacks.onNewConnection ?? undefined,
      on_error: callbacks.onError ?? undefined,
      user_data: _userData,
    });

    // Set callbacks using the fixed C++ implementation
    // Convert the callback struct to a void* pointer for the C function
    const result = mcpFilterLib.mcp_filter_set_callbacks(
      filterHandle,
      koffi.as(callbackStruct, "void*")
    );
    if (result !== 0) {
      mcpFilterLib.mcp_filter_release(filterHandle);
      throw new Error(`Failed to set callbacks on built-in filter: ${result}`);
    }
  }

  return filterHandle;
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
export function setFilterCallbacks(filter: number, callbacks: FilterCallbacks): number {
  // Create callback wrappers for each callback type
  const callbackWrappers: any = {};

  if (callbacks.onData) {
    const DataCallback = koffi.proto("int DataCallback(uint64_t, int, void*)");
    const jsOnData = (buffer: number, endStream: number, userData: any) => {
      try {
        return callbacks.onData!(buffer, endStream === 1, userData);
      } catch (error) {
        console.error("Error in onData callback:", error);
        return FilterStatus.STOP_ITERATION;
      }
    };
    callbackWrappers.onData = koffi.register(jsOnData, DataCallback);
  }

  if (callbacks.onWrite) {
    const WriteCallback = koffi.proto("int WriteCallback(uint64_t, int, void*)");
    const jsOnWrite = (buffer: number, endStream: number, userData: any) => {
      try {
        return callbacks.onWrite!(buffer, endStream === 1, userData);
      } catch (error) {
        console.error("Error in onWrite callback:", error);
        return FilterStatus.STOP_ITERATION;
      }
    };
    callbackWrappers.onWrite = koffi.register(jsOnWrite, WriteCallback);
  }

  if (callbacks.onNewConnection) {
    const EventCallback = koffi.proto("int EventCallback(int, void*)");
    const jsOnNewConnection = (state: number, userData: any) => {
      try {
        return callbacks.onNewConnection!(state, userData);
      } catch (error) {
        console.error("Error in onNewConnection callback:", error);
        return FilterStatus.STOP_ITERATION;
      }
    };
    callbackWrappers.onNewConnection = koffi.register(jsOnNewConnection, EventCallback);
  }

  if (callbacks.onError) {
    const ErrorCallback = koffi.proto("void ErrorCallback(uint64_t, int, string, void*)");
    const jsOnError = (filter: number, error: number, message: string, userData: any) => {
      try {
        callbacks.onError!(filter, error, message, userData);
      } catch (error) {
        console.error("Error in onError callback:", error);
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
export function setFilterProtocolMetadata(filter: number, _metadata: ProtocolMetadata): number {
  // For now, pass null as metadata since the C++ function expects a pointer to C struct
  // TODO: Implement proper C struct conversion when the C++ side is ready
  return mcpFilterLib.mcp_filter_set_protocol_metadata(filter, null) as number;
}

/**
 * Get protocol metadata from filter
 */
export function getFilterProtocolMetadata(filter: number, metadata: ProtocolMetadata): number {
  return mcpFilterLib.mcp_filter_get_protocol_metadata(filter, metadata) as number;
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
export function createFilterManager(connection: number, dispatcher: number): number {
  return mcpFilterLib.mcp_filter_manager_create(connection, dispatcher) as number;
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
export function getBufferSlices(buffer: number, slices: BufferSlice[], sliceCount: number): number {
  return mcpFilterLib.mcp_filter_get_buffer_slices(buffer, slices, sliceCount) as number;
}

/**
 * Reserve buffer space for writing
 */
export function reserveBuffer(buffer: number, size: number, slice: BufferSlice): number {
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
  return mcpFilterLib.mcp_filter_buffer_create(data, data.length, flags) as number;
}

// Buffer functions are exported from mcp-filter-buffer.ts to avoid conflicts

// releaseBuffer is exported from mcp-filter-buffer.ts to avoid conflicts

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
      console.error("Error in callback:", error);
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

// destroyBufferPool is exported from mcp-filter-buffer.ts to avoid conflicts

// ============================================================================
// Buffer Helper Functions for CApiFilter Callbacks
// ============================================================================

/**
 * Get buffer content as string (zero-copy operation)
 */
export function getBufferContent(bufferHandle: number): string {
  // Validate buffer handle
  if (bufferHandle === 0) {
    throw new Error("Invalid buffer handle: 0");
  }

  try {
    const slices: BufferSlice[] = [];
    const sliceCount = new Uint32Array(1);
    sliceCount[0] = 10; // Max slices

    const result = mcpFilterLib.mcp_filter_get_buffer_slices(bufferHandle, slices, sliceCount);

    if (result !== 0) {
      throw new Error(`Buffer read failed: ${result}`);
    }

    return slices
      .filter(slice => slice.data !== null)
      .map(slice => {
        const buffer = Buffer.allocUnsafe(slice.length);
        buffer.set(slice.data!.subarray(0, slice.length));
        return buffer.toString("utf8");
      })
      .join("");
  } catch (error) {
    // Re-throw the error instead of returning empty string
    throw error;
  }
}

/**
 * Update buffer content (zero-copy operation)
 */
export function updateBufferContent(bufferHandle: number, content: string): void {
  // Validate buffer handle
  if (bufferHandle === 0) {
    throw new Error("Invalid buffer handle: 0");
  }

  try {
    // Use mcp_filter_reserve_buffer for zero-copy writing
    const slice: BufferSlice = { data: null, length: 0, flags: 0 };
    const result = mcpFilterLib.mcp_filter_reserve_buffer(bufferHandle, content.length, slice);

    if (result === 0 && slice.data !== null) {
      const buffer = Buffer.from(content, "utf8");
      slice.data.set(buffer.subarray(0, Math.min(content.length, slice.length)));
      mcpFilterLib.mcp_filter_commit_buffer(bufferHandle, content.length);
    } else {
      throw new Error(`Buffer write failed: ${result}`);
    }
  } catch (error) {
    // Re-throw the error instead of swallowing it
    throw error;
  }
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

// ============================================================================
// Environment Detection
// ============================================================================

/**
 * Detect if running in test environment
 */
export const isTestEnvironment = (): boolean => {
  return process.env['NODE_ENV'] === 'test' && !process.env['USE_REAL_HANDLES'];
};

/**
 * Detect if running in production mode
 */
export const isProductionMode = (): boolean => {
  return !isTestEnvironment();
};

/**
 * Get current handle mode
 */
export const getHandleMode = (): 'real' | 'stub' => {
  return isProductionMode() ? 'real' : 'stub';
};

/**
 * Feature flags for gradual rollout
 */
export const FEATURE_FLAGS = {
  USE_REAL_HANDLES: process.env['USE_REAL_HANDLES'] !== 'false',
  ENABLE_HANDLE_VALIDATION: process.env['VALIDATE_HANDLES'] === 'true',
  VERBOSE_HANDLE_LOGGING: process.env['DEBUG_HANDLES'] === 'true',
};

// ============================================================================
// Dispatcher and Connection Lifecycle Helpers
// ============================================================================

// Type alias for pointer (opaque handle)
export type pointer = any;

// Track dispatcher workers (use Map, not WeakMap - keys are pointer numbers)

/**
 * Create a real dispatcher handle using the native C++ library
 * This function ALWAYS creates real handles, never stubs
 */
export function createRealDispatcher(): pointer {
  console.log('🔍 [createRealDispatcher] Checking initialization status...');
  const initBefore = mcpFilterLib.mcp_is_initialized ? mcpFilterLib.mcp_is_initialized() : -1;
  console.log(`🔍 [createRealDispatcher] mcp_is_initialized BEFORE ensureMcpInitialized: ${initBefore}`);

  ensureMcpInitialized();

  const initAfter = mcpFilterLib.mcp_is_initialized ? mcpFilterLib.mcp_is_initialized() : -1;
  console.log(`🔍 [createRealDispatcher] mcp_is_initialized AFTER ensureMcpInitialized: ${initAfter}`);

  // Check if native library is available
  if (!mcpFilterLib || !mcpFilterLib.mcp_dispatcher_create) {
    throw new Error(
      'Native library not available. Either:\n' +
      '1. Build the C++ library with "make build"\n' +
      '2. Use stubHandleFactory for testing\n' +
      'DO NOT return stub handles from this function!'
    );
  }

  // Call FFI function to create real dispatcher
  console.log('🔍 [createRealDispatcher] Calling mcp_dispatcher_create()...');
  const dispatcher = mcpFilterLib.mcp_dispatcher_create();

  // Check if dispatcher is valid before trying to convert to string
  if (!dispatcher || dispatcher === 0) {
    console.log('🔍 [createRealDispatcher] mcp_dispatcher_create returned null or 0');

    // Check initialization status again
    const initFinal = mcpFilterLib.mcp_is_initialized ? mcpFilterLib.mcp_is_initialized() : -1;
    console.log(`🔍 [createRealDispatcher] mcp_is_initialized when dispatcher creation failed: ${initFinal}`);

    // Try to get last error
    if (mcpFilterLib.mcp_get_last_error) {
      const errorPtr = mcpFilterLib.mcp_get_last_error();
      console.log(`🔍 [createRealDispatcher] Error pointer: ${errorPtr}`);
    }

    throw new Error('Failed to create dispatcher - native library returned null');
  }

  // Dispatcher is valid - now we can log it safely
  console.log('🔍 [createRealDispatcher] mcp_dispatcher_create succeeded');
  if (FEATURE_FLAGS.VERBOSE_HANDLE_LOGGING || true) {  // Always log for debugging
    try {
      console.log(`Created real dispatcher handle (pointer object): ${typeof dispatcher}`);
    } catch (e) {
      console.log(`Created real dispatcher handle (cannot convert to string): ${e}`);
    }
    console.log('🔍 [createRealDispatcher] dispatcher class:', Object.prototype.toString.call(dispatcher));
    console.log('🔍 [createRealDispatcher] Buffer.isBuffer:', Buffer.isBuffer(dispatcher));
    console.log('🔍 [createRealDispatcher] dispatcher keys:', Object.keys(dispatcher || {}));
    try {
      console.log('🔍 [createRealDispatcher] dispatcher valueOf:', dispatcher && typeof dispatcher.valueOf === 'function' ? dispatcher.valueOf() : 'n/a');
    } catch (err) {
      console.log('🔍 [createRealDispatcher] dispatcher valueOf error:', err);
    }
    try {
      const proto = Object.getPrototypeOf(dispatcher);
      console.log('🔍 [createRealDispatcher] dispatcher proto keys:', proto ? Object.getOwnPropertyNames(proto) : []);
    } catch (err) {
      console.log('🔍 [createRealDispatcher] dispatcher proto inspection error:', err);
    }
    console.log('🔍 [createRealDispatcher] dispatcher buffer property:', dispatcher && (dispatcher as any).buffer ? 'present' : 'missing');
    try {
      // eslint-disable-next-line @typescript-eslint/no-var-requires
      const koffiModule = require('koffi');
      if (typeof koffiModule.addressOf === 'function') {
        console.log('🔍 [createRealDispatcher] dispatcher addressOf:', koffiModule.addressOf(dispatcher));
      } else {
        console.log('🔍 [createRealDispatcher] koffi.addressOf not available');
      }
    } catch (err) {
      console.log('🔍 [createRealDispatcher] dispatcher addressOf error:', err);
    }
  }

  startDispatcherPump(dispatcher);

  if (FEATURE_FLAGS.VERBOSE_HANDLE_LOGGING) {
    console.log('✅ Dispatcher pump started');
  }

  return dispatcher;
}

/**
 * Destroy a dispatcher handle
 */
export function destroyDispatcher(dispatcher: pointer): void {
  if (!dispatcher || dispatcher === 0) {
    return; // Skip destroying null handles
  }

  if (dispatcher === 1 || dispatcher === 2) {
    console.warn('Warning: Attempted to destroy stub handle - ignoring');
    return;
  }

  const pump = dispatcherPumpState.get(dispatcher);
  if (pump) {
    pump.active = false;
    dispatcherPumpState.delete(dispatcher);
  }

  mcpFilterLib.mcp_dispatcher_stop(dispatcher);
  mcpFilterLib.mcp_dispatcher_destroy(dispatcher);

  if (FEATURE_FLAGS.VERBOSE_HANDLE_LOGGING) {
    console.log('✅ Destroyed dispatcher');
  }
}

/**
 * Create a real connection handle using the native C++ library
  if (FEATURE_FLAGS.VERBOSE_HANDLE_LOGGING) {
    console.log('✅ Destroyed dispatcher and worker thread');
  }
}

/**
 * Create a real connection handle using the native C++ library
 * This function ALWAYS creates real handles, never stubs
 */
export function createConnection(dispatcher: pointer, transportType: TransportType = TransportType.MCP_TRANSPORT_HTTP_SSE): pointer {
  // Validate dispatcher handle
  if (!dispatcher || dispatcher === 0) {
    throw new Error('Invalid dispatcher handle');
  }

  // Check if native library is available
  if (!mcpFilterLib || !mcpFilterLib.mcp_connection_create_client) {
    throw new Error(
      'Native library not available for connection creation. Either:\n' +
      '1. Build the C++ library with "make build"\n' +
      '2. Use stubHandleFactory for testing\n' +
      'DO NOT return stub handles from this function!'
    );
  }

  // Create client connection with specified transport type
  const connection = mcpFilterLib.mcp_connection_create_client(dispatcher, transportType);
  if (!connection || connection === 0) {
    throw new Error('Failed to create connection - native library returned null');
  }

  if (FEATURE_FLAGS.VERBOSE_HANDLE_LOGGING) {
    console.log(`Created real connection: 0x${connection.toString(16)} (transport: ${TransportType[transportType]})`);
  }
  return connection;
}

/**
 * Destroy a connection handle
 */
export function destroyConnection(connection: pointer): void {
  if (!connection || connection === 0) {
    return; // Skip destroying null handles
  }

  // Check if this is a stub handle (should never be passed here)
  if (connection === 1 || connection === 2) {
    console.warn('Warning: Attempted to destroy stub connection handle - ignoring');
    return;
  }

  mcpFilterLib.mcp_connection_destroy(connection);
  if (FEATURE_FLAGS.VERBOSE_HANDLE_LOGGING) {
    console.log(`Destroyed connection: 0x${connection.toString(16)}`);
  }
}

/**
 * Configure a connection with address and options
 * @param connection Connection handle
 * @param address Pointer to address structure (can be null)
 * @param options Pointer to socket options (can be null)
 * @param sslConfig Pointer to SSL configuration (can be null)
 * @returns 0 on success, error code otherwise
 */
export function configureConnection(
  connection: pointer,
  address: pointer = null,
  options: pointer = null,
  sslConfig: pointer = null
): number {
  if (!connection || connection === 0) {
    throw new Error('Invalid connection handle');
  }

  return mcpFilterLib.mcp_connection_configure(connection, address, options, sslConfig) as number;
}

/**
 * Run dispatcher event loop
 * @param dispatcher Dispatcher handle
 * @returns 0 on success, error code otherwise
 */
export function runDispatcher(dispatcher: pointer): number {
  if (!dispatcher || dispatcher === 0) {
    throw new Error('Invalid dispatcher handle');
  }

  return mcpFilterLib.mcp_dispatcher_run(dispatcher) as number;
}

/**
 * Run dispatcher event loop with timeout
 * @param dispatcher Dispatcher handle
 * @param timeoutMs Timeout in milliseconds
 * @returns 0 on success, error code otherwise
 */
export function runDispatcherWithTimeout(dispatcher: pointer, timeoutMs: number): number {
  if (!dispatcher || dispatcher === 0) {
    throw new Error('Invalid dispatcher handle');
  }

  return mcpFilterLib.mcp_dispatcher_run_timeout(dispatcher, timeoutMs) as number;
}

/**
 * Stop dispatcher event loop
 * @param dispatcher Dispatcher handle
 */
export function stopDispatcher(dispatcher: pointer): void {
  if (!dispatcher || dispatcher === 0) {
    return;
  }

  mcpFilterLib.mcp_dispatcher_stop(dispatcher);
  if (FEATURE_FLAGS.VERBOSE_HANDLE_LOGGING) {
    console.log(`Stopped dispatcher: 0x${dispatcher.toString(16)}`);
  }
}

// Export TransportType for convenience
export { TransportType } from "./mcp-ffi-bindings";

// ============================================================================
// Factory Pattern for Testing
// ============================================================================

/**
 * Handle factory interface for dependency injection
 */
export interface HandleFactory {
  createDispatcher: () => pointer;
  createConnection: (dispatcher: pointer, transportType?: TransportType) => pointer;
  destroyDispatcher: (dispatcher: pointer) => void;
  destroyConnection: (connection: pointer) => void;
}

/**
 * Default factory that creates real handles
 */
export const defaultHandleFactory: HandleFactory = {
  createDispatcher: createRealDispatcher,
  createConnection,
  destroyDispatcher,
  destroyConnection,
};

/**
 * Stub factory for testing - THROWS instead of returning fake pointers
 *
 * IMPORTANT: Stub handles must NOT be passed to C++ code. The old approach of
 * returning literal integers (1, 2) causes crashes when C++ reinterprets them
 * as pointers. Tests must use Jest mocks, not stub handles.
 */
export const stubHandleFactory: HandleFactory = {
  createDispatcher: () => {
    throw new Error(
      'Stub dispatcher handles cannot be used with native C++ code.\n' +
      'Either:\n' +
      '1. Build the native library (make build) and use real handles\n' +
      '2. Use Jest mocks to intercept FFI calls in tests\n' +
      'Stub handles returning literal integers will crash when passed to C++.'
    );
  },
  createConnection: (_dispatcher: pointer, _transportType?: TransportType) => {
    throw new Error(
      'Stub connection handles cannot be used with native C++ code.\n' +
      'Either:\n' +
      '1. Build the native library (make build) and use real handles\n' +
      '2. Use Jest mocks to intercept FFI calls in tests\n' +
      'Stub handles returning literal integers will crash when passed to C++.'
    );
  },
  destroyDispatcher: (_dispatcher: pointer) => {
    // Safe to no-op for cleanup
  },
  destroyConnection: (_connection: pointer) => {
    // Safe to no-op for cleanup
  },
};

/**
 * Get appropriate handle factory based on environment
 */
export function getHandleFactory(): HandleFactory {
  return isTestEnvironment() ? stubHandleFactory : defaultHandleFactory;
}

/**
 * Create a custom handle factory with specific implementations
 */
export function createHandleFactory(overrides: Partial<HandleFactory>): HandleFactory {
  const baseFactory = getHandleFactory();
  return {
    ...baseFactory,
    ...overrides,
  };
}
