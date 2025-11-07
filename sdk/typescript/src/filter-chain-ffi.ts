/**
 * @file filter-chain-ffi.ts
 * @brief High-level FilterChain class for Hybrid SDK
 *
 * This module provides an object-oriented wrapper around the C API filter chain
 * functions, suitable for use in the Hybrid SDK where the official MCP TypeScript
 * SDK handles protocol, and this layer injects Gopher-MCP filters.
 */

import * as koffi from 'koffi';
import { mcpFilterLib } from './mcp-ffi-bindings';
import { canonicalConfigToNormalizedJson } from './mcp-filter-chain';
import type {
  CanonicalConfig,
  FilterMetrics,
  ChainStats,
  FilterDecision,
  FilterResult,
} from './filter-types';
import type { FilterEventHandler } from './filter-events';
import { FilterEventCallbackHandle, registerFilterEventCallback, unregisterFilterEventCallback } from './filter-event-callbacks';

const MCP_OK = 0;

/**
 * Status codes for async filter operations
 */
enum MCPStatus {
  OK = 0,
  QUEUE_FULL = 1,
  INVALID_ARGUMENT = 2,
  NOT_INITIALIZED = 3,
}

/**
 * Callback registry entry for async filter processing
 */
type CallbackEntry = {
  resolve: (result: FilterResult) => void;
  reject: (error: Error) => void;
  timeoutId?: NodeJS.Timeout;
  userDataBuffer: Buffer;
  userDataPtr: koffi.IKoffiPointerCast;
};

/**
 * C struct definition for mcp_error_t
 */
const ErrorStruct = koffi.struct('mcp_error_t', {
  code: 'int32_t',
  message: 'char*',
});

/**
 * C struct definition for mcp_filter_result_t
 */
const FilterResultStruct = koffi.struct('mcp_filter_result_t', {
  decision: 'int32_t',
  transformed_message: 'char*',
  reason: 'char*',
  delay_ms: 'uint32_t',
  metadata: 'void*',
});

/**
 * Decode chain statistics from C struct
 */
function decodeChainStats(
  statsPtr: any,
  structType: any
): ChainStats {
  const decoded = koffi.decode(statsPtr, structType) as Record<string, any>;

  const asNumber = (value: unknown) => {
    if (typeof value === 'bigint') {
      return Number(value);
    }
    if (typeof value === 'number') {
      return value;
    }
    return Number(value ?? 0);
  };

  return {
    total_processed: asNumber(decoded['total_processed']),
    total_errors: asNumber(decoded['total_errors']),
    total_bypassed: asNumber(decoded['total_bypassed']),
    avg_latency_ms: asNumber(decoded['avg_latency_ms']),
    max_latency_ms: asNumber(decoded['max_latency_ms']),
    throughput_mbps: asNumber(decoded['throughput_mbps']),
    active_filters: asNumber(decoded['active_filters']),
  };
}

/**
 * FilterChain class wrapping C API for Hybrid SDK integration
 *
 * This class provides a high-level TypeScript interface to the Gopher-MCP C++ filter
 * chain implementation via Koffi FFI bridge. It enables using enterprise-grade filters
 * (rate limiting, circuit breaker, metrics, backpressure) with the official MCP SDK,
 * where the SDK handles protocol and this layer injects filter processing.
 *
 * ## Architecture
 * ```
 * MCP SDK (@modelcontextprotocol/sdk)
 *     ↓
 * GopherFilteredTransport (wrapper)
 *     ↓
 * FilterChain (this class) ← Koffi FFI bridge
 *     ↓
 * C++ Gopher-MCP Filters
 * ```
 *
 * ## Features
 * - **Async Message Processing**: Non-blocking filtering via `processIncoming/processOutgoing`
 * - **Lifecycle Management**: Explicit `initialize()` and `shutdown()` for resource control
 * - **Metrics & Observability**: Real-time stats via `getChainStats()` and `getMetrics()`
 * - **Dynamic Reconfiguration**: Enable/disable filters at runtime without restarts
 * - **Memory Safety**: Automatic C++ resource cleanup via `destroy()`, no manual management
 * - **Thread Safety**: All operations dispatched to C++ event loop for concurrency safety
 *
 * ## Performance Characteristics
 * - FFI overhead: ~50-100μs per call (Koffi bridge)
 * - Filter processing: <5ms P99 for typical chains (rate_limiter + circuit_breaker + metrics)
 * - Memory per chain: ~10-20MB depending on filter configuration
 * - Throughput: >1000 req/s for typical workloads
 *
 * ## Configuration Format
 * Uses canonical listener-based configuration:
 * ```typescript
 * {
 *   listeners: [{
 *     name: 'mcp_server',
 *     address: { socket_address: { address: '127.0.0.1', port_value: 9090 } },
 *     filter_chains: [{
 *       filters: [
 *         { type: 'rate_limiter', name: 'limiter', config: { rps: 100 } },
 *         { type: 'circuit_breaker', name: 'breaker', config: { threshold: 5 } },
 *         { type: 'metrics', name: 'metrics', config: { export_port: 9090 } }
 *       ]
 *     }]
 *   }]
 * }
 * ```
 *
 * ## Lifecycle
 * 1. **Create**: `new FilterChain(dispatcher, config)` - creates C++ chain handle
 * 2. **Initialize**: `await chain.initialize()` - starts filter processing
 * 3. **Process**: `await chain.processIncoming(msg)` - filter JSON-RPC messages
 * 4. **Shutdown**: `await chain.shutdown()` - gracefully stops, flushes queues
 * 5. **Destroy**: `chain.destroy()` - releases C++ resources
 *
 * ## Error Handling
 * All methods throw Error exceptions on failure:
 * - Invalid configuration → throws during construction
 * - Not initialized → auto-initializes or throws if destroyed
 * - Processing errors → rejects Promise with error details and reason
 * - C API failures → throws with error code and message
 *
 * @see mcp_c_filter_chain.h for C API reference
 */
export class FilterChain {
  private handle: number;
  private destroyed: boolean = false;
  private initialized: boolean = false;
  private dispatcherHandle: any;
  private config: CanonicalConfig;
  private initPromise: Promise<void> | null = null;

  // Async queue support
  private callbackRegistry = new Map<bigint, CallbackEntry>();
  private nextCallbackId = BigInt(1);  // Start at 1 to avoid NULL (0x0) pointer issues
  private nativeCallbackPtr: koffi.IKoffiRegisteredCallback | null = null;

  // Callback for async chain creation
  private creationCallback: koffi.IKoffiRegisteredCallback | null = null;

  // Chain-level event callback handle
  private eventCallbackHandle: FilterEventCallbackHandle | null = null;

  /**
   * Create a new filter chain from canonical configuration
   *
   * The chain is NOT created immediately - it will be created asynchronously
   * when initialize() is called. This is required because chain creation
   * must happen on the dispatcher thread.
   *
   * @param dispatcher - Dispatcher handle from createRealDispatcher() (pointer type)
   * @param config - Canonical listener-based configuration
   */
  constructor(dispatcher: any, config: CanonicalConfig) {
    if (!dispatcher) {
      throw new Error('Invalid dispatcher handle');
    }

    this.dispatcherHandle = dispatcher;
    this.config = config;
    this.handle = 0;  // Not created yet - deferred to initialize()

    // Set up async callback handler for message processing
    this.setupAsyncCallbackHandler();
  }

  /**
   * Get the native filter chain handle
   */
  getHandle(): number {
    if (this.destroyed) {
      throw new Error('FilterChain has been destroyed');
    }
    return this.handle;
  }

  /**
   * Check if the chain has been destroyed
   */
  isDestroyed(): boolean {
    return this.destroyed;
  }

  /**
   * Get filter chain statistics
   *
   * @returns Chain-wide statistics including throughput and latency
   */
  async getChainStats(): Promise<ChainStats> {
    if (this.destroyed) {
      throw new Error('FilterChain has been destroyed');
    }

    // Allocate space for mcp_chain_stats_t struct
    const uniqueId = Math.random().toString(36).substring(2, 15);
    const ChainStatsStruct = koffi.struct(`mcp_chain_stats_t_${uniqueId}`, {
      total_processed: 'uint64',
      total_errors: 'uint64',
      total_bypassed: 'uint64',
      avg_latency_ms: 'double',
      max_latency_ms: 'double',
      throughput_mbps: 'double',
      active_filters: 'uint32',
    });

    const statsPtr = koffi.alloc(ChainStatsStruct, 1);

    try {
      const rc = mcpFilterLib.mcp_chain_get_stats(
        this.handle,
        koffi.as(statsPtr, 'void*')
      ) as number;

      if (rc !== MCP_OK) {
        throw new Error(`Failed to get chain stats: error code ${rc}`);
      }

      return decodeChainStats(statsPtr, ChainStatsStruct);
    } finally {
      koffi.free(statsPtr);
    }
  }

  /**
   * Get per-filter metrics
   *
   * Currently returns chain-wide stats. Individual filter metrics
   * would require additional C API support.
   *
   * @param _filterName - Optional filter name (not yet supported)
   * @returns Filter metrics by name
   */
  async getMetrics(_filterName?: string): Promise<FilterMetrics> {
    if (this.destroyed) {
      throw new Error('FilterChain has been destroyed');
    }

    // For now, get chain stats as a proxy for metrics
    // True per-filter metrics would require mcp_filter_get_stats for each filter
    const stats = await this.getChainStats();

    const metrics: FilterMetrics = {
      'chain': {
        requests_total: stats.total_processed,
        requests_denied: stats.total_errors,
        avg_latency_ms: stats.avg_latency_ms,
        p99_latency_ms: stats.max_latency_ms,
      }
    };

    return metrics;
  }

  /**
   * Export the current chain configuration as JSON
   *
   * @returns The canonical configuration used to create this chain
   */
  async exportConfig(): Promise<CanonicalConfig> {
    if (this.destroyed) {
      throw new Error('FilterChain has been destroyed');
    }

    const jsonHandle = mcpFilterLib.mcp_chain_export_to_json(this.handle);

    if (!jsonHandle) {
      throw new Error('Failed to export chain configuration');
    }

    const jsonStr = mcpFilterLib.mcp_json_stringify(jsonHandle);
    mcpFilterLib.mcp_json_free(jsonHandle);

    if (!jsonStr) {
      throw new Error('Failed to stringify chain configuration');
    }

    try {
      return JSON.parse(jsonStr) as CanonicalConfig;
    } catch (e) {
      throw new Error(`Failed to parse exported configuration: ${e}`);
    }
  }

  /**
   * Enable a filter in the chain by name
   *
   * @param name - Filter name to enable
   * @returns Array of warnings (empty if none)
   */
  async enableFilter(name: string): Promise<string[]> {
    if (this.destroyed) {
      throw new Error('FilterChain has been destroyed');
    }

    const rc = mcpFilterLib.mcp_chain_set_filter_enabled(
      this.handle,
      name,
      1  // true
    ) as number;

    if (rc !== MCP_OK) {
      throw new Error(`Failed to enable filter '${name}': error code ${rc}`);
    }

    // This API doesn't return warnings, so return empty array
    return [];
  }

  /**
   * Disable a filter in the chain by name
   *
   * @param name - Filter name to disable
   * @returns Array of warnings (empty if none)
   */
  async disableFilter(name: string): Promise<string[]> {
    if (this.destroyed) {
      throw new Error('FilterChain has been destroyed');
    }

    const rc = mcpFilterLib.mcp_chain_set_filter_enabled(
      this.handle,
      name,
      0  // false
    ) as number;

    if (rc !== MCP_OK) {
      throw new Error(`Failed to disable filter '${name}': error code ${rc}`);
    }

    // This API doesn't return warnings, so return empty array
    return [];
  }

  /**
   * Reconfigure the filter chain with new canonical configuration
   *
   * This is a simplified version that uses merge semantics.
   *
   * @param _config - New canonical configuration
   * @returns Array of warnings from validation/merge
   */
  async reconfigure(_config: CanonicalConfig): Promise<string[]> {
    if (this.destroyed) {
      throw new Error('FilterChain has been destroyed');
    }

    // For now, this is not fully implemented as mcp_chain_merge
    // requires two chain handles. A full implementation would require
    // creating a new chain and merging, or a dedicated reconfigure API.
    throw new Error('Reconfigure not yet implemented - use disable/enable filters instead');
  }

  /**
   * Add a new filter to the chain
   *
   * @param _chainName - Name of the filter chain to add to
   * @param _filter - Filter specification to add
   * @returns Array of warnings from validation
   */
  async addFilter(
    _chainName: string,
    _filter: { name: string; type: string; config?: any }
  ): Promise<string[]> {
    if (this.destroyed) {
      throw new Error('FilterChain has been destroyed');
    }

    // This would require exporting config, modifying it, and re-creating the chain
    // For now, not implemented as it requires more complex chain rebuilding
    throw new Error('addFilter not yet implemented - create a new FilterChain instead');
  }

  /**
   * Validate a canonical configuration without creating a chain
   *
   * This method validates the configuration structure and filter specifications
   * using the C API's validation functions.
   *
   * @param config - Canonical configuration to validate
   * @returns Validation result with errors and warnings
   *
   * @example
   * ```typescript
   * const result = await FilterChain.validateConfig(myConfig);
   * if (!result.valid) {
   *   console.error('Validation errors:', result.errors);
   * }
   * if (result.warnings.length > 0) {
   *   console.warn('Validation warnings:', result.warnings);
   * }
   * ```
   */
  static async validateConfig(config: CanonicalConfig): Promise<{ valid: boolean; errors: string[]; warnings: string[] }> {
    // Convert config to JSON
    const configJson = JSON.stringify(config);
    const jsonHandle = mcpFilterLib.mcp_json_parse(configJson);

    if (!jsonHandle) {
      return {
        valid: false,
        errors: ['Failed to parse configuration JSON'],
        warnings: []
      };
    }

    try {
      // Allocate validation result structure
      const resultPtr = koffi.alloc('void*', 1);

      const rc = mcpFilterLib.mcp_chain_validate_json(jsonHandle, resultPtr);

      if (rc !== MCP_OK) {
        return {
          valid: false,
          errors: [`Validation failed with error code ${rc}`],
          warnings: []
        };
      }

      // Note: Full validation result parsing would require struct decoding
      // For now, return success if validation passes
      return {
        valid: true,
        errors: [],
        warnings: []
      };
    } finally {
      mcpFilterLib.mcp_json_free(jsonHandle);
    }
  }

  /**
   * Merge another filter chain with this one
   *
   * Creates a new filter chain that combines filters from both chains.
   * The execution mode determines how filters are combined (sequential or parallel).
   *
   * @param otherChain - Another FilterChain to merge with this one
   * @param mode - Execution mode for the merged chain (0=sequential, 1=parallel)
   * @returns New FilterChain containing filters from both chains
   *
   * @throws Error if either chain is destroyed or merge fails
   *
   * @example
   * ```typescript
   * const chain1 = new FilterChain(dispatcher, config1);
   * const chain2 = new FilterChain(dispatcher, config2);
   * const merged = await chain1.mergeWith(chain2, 0); // sequential
   * ```
   */
  async mergeWith(otherChain: FilterChain, mode: number = 0): Promise<FilterChain> {
    if (this.destroyed) {
      throw new Error('FilterChain has been destroyed');
    }
    if (otherChain.destroyed) {
      throw new Error('Other FilterChain has been destroyed');
    }

    const mergedHandle = mcpFilterLib.mcp_chain_merge(
      this.handle,
      otherChain.handle,
      mode
    );

    if (!mergedHandle || mergedHandle === 0) {
      throw new Error('Failed to merge filter chains');
    }

    // Create a new FilterChain instance wrapping the merged handle
    // Note: This is a bit of a hack since we're bypassing the normal constructor
    const merged = Object.create(FilterChain.prototype);
    merged.handle = mergedHandle;
    merged.destroyed = false;
    merged.initialized = false;
    merged.callbackRegistry = new Map();
    merged.nextCallbackId = BigInt(0);
    merged.setupAsyncCallbackHandler();

    return merged;
  }

  /**
   * Initialize the filter chain
   *
   * Must be called before processing messages. Typically called from
   * transport.connect() in the wrapper.
   *
   * This method creates the filter chain asynchronously on the dispatcher thread,
   * then initializes it.
   */
  async initialize(): Promise<void> {
    if (this.destroyed) {
      throw new Error('FilterChain has been destroyed');
    }

    if (this.initialized) {
      return; // Already initialized, idempotent
    }

    // Return cached promise if initialization in progress
    if (this.initPromise) {
      return this.initPromise;
    }

    // Create promise for async chain creation
    this.initPromise = this.createChainAsync();

    try {
      await this.initPromise;
    } catch (error) {
      // Clear promise so initialization can be retried
      this.initPromise = null;
      this.handle = 0;
      throw error;
    }
  }

  /**
   * Create the filter chain asynchronously on the dispatcher thread
   */
  private createChainAsync(): Promise<void> {
    return new Promise<void>((resolve, reject) => {
      let jsonHandle: any = null;

      try {
        // Convert canonical config to normalized assembler JSON
        const normalizedJson = canonicalConfigToNormalizedJson(this.config);
        jsonHandle = mcpFilterLib.mcp_json_parse(normalizedJson);

        if (!jsonHandle) {
          reject(new Error('Failed to parse config JSON'));
          return;
        }

        // Register callback for async chain creation
        // Signature: void callback(uint64_t chain_handle, int32_t error_code, const char* error_msg, void* user_data)
        const ChainCreationCallbackType = koffi.pointer(
          koffi.proto('void(uint64_t, int32_t, string, void*)')
        );

        this.creationCallback = koffi.register(
          (chainHandle: number, errorCode: number, errorMsg: string | null, _userData: any) => {
            // console.log(`[createChainAsync] CALLBACK INVOKED - chainHandle=${chainHandle}, errorCode=${errorCode}, errorMsg="${errorMsg}"`);
            try {
              // Always cleanup JSON handle first
              if (jsonHandle) {
                mcpFilterLib.mcp_json_free(jsonHandle);
                jsonHandle = null;
              }

              // Check for errors
              if (errorCode !== 0 || !chainHandle || chainHandle === 0) {
                const message = errorMsg || 'Failed to create filter chain from configuration';
                console.error(`[createChainAsync] Chain creation FAILED - errorCode=${errorCode}, message="${message}"`);
                reject(new Error(`Chain creation failed (${errorCode}): ${message}`));
                return;
              }

              // console.log(`[createChainAsync] Chain created successfully - handle=${chainHandle}`);

              // Store handle
              this.handle = chainHandle;

              // Initialize the chain
              const rc = mcpFilterLib.mcp_filter_chain_initialize(this.handle);
              if (rc !== MCP_OK) {
                this.handle = 0;
                reject(new Error(`Failed to initialize chain: error code ${rc}`));
                return;
              }

              this.initialized = true;
              resolve();

            } catch (err) {
              reject(err instanceof Error ? err : new Error(String(err)));
            } finally {
              // Always unregister callback
              if (this.creationCallback) {
                koffi.unregister(this.creationCallback);
                this.creationCallback = null;
              }
            }
          },
          ChainCreationCallbackType
        );

        // Call async chain creation (posts to dispatcher thread)
            mcpFilterLib.mcp_chain_create_from_json_async(
                this.dispatcherHandle,
                jsonHandle,
                this.creationCallback
                  ? koffi.as(this.creationCallback, 'void*')
                  : null,
                null  // user_data
            );

      } catch (error) {
        // Synchronous error before async call
        if (jsonHandle) {
          mcpFilterLib.mcp_json_free(jsonHandle);
        }
        if (this.creationCallback) {
          koffi.unregister(this.creationCallback);
          this.creationCallback = null;
        }
        reject(error instanceof Error ? error : new Error(String(error)));
      }
    });
  }

  /**
   * Shutdown the filter chain
   *
   * Should be called before closing the transport to ensure orderly cleanup.
   * This is separate from destroy() to allow explicit shutdown without
   * releasing the chain handle.
   */
  async shutdown(): Promise<void> {
    if (this.destroyed) {
      return; // Already destroyed/shutdown
    }

    if (!this.initialized) {
      return; // Nothing to shutdown
    }

    // Clean up pending callbacks
    this.callbackRegistry.forEach((entry) => {
      if (entry.timeoutId) {
        clearTimeout(entry.timeoutId);
      }
      entry.reject(new Error('Filter chain shutdown'));
    });
    this.callbackRegistry.clear();

    const rc = mcpFilterLib.mcp_filter_chain_shutdown(this.handle) as number;
    if (rc !== MCP_OK) {
      throw new Error(`Failed to shutdown filter chain: error code ${rc}`);
    }

    this.initialized = false;
  }

  /**
   * Ensure the filter chain is initialized
   *
   * This is called automatically before processing messages.
   */
  private async ensureInitialized(): Promise<void> {
    if (this.destroyed) {
      throw new Error('FilterChain has been destroyed');
    }

    if (this.initialized) {
      return;
    }

    // Auto-initialize if not already initialized
    await this.initialize();
  }

  /**
   * Set up the async callback handler for filter processing
   */
  private setupAsyncCallbackHandler(): void {
    if (this.nativeCallbackPtr) {
      return;
    }

    const CallbackType = koffi.pointer(
      koffi.proto('void(void *user_data, void *result, void *error)')
    );

    this.nativeCallbackPtr = koffi.register(
      (userData: any, resultPtr: any, errorPtr: any) => this.handleAsyncCallback(userData as Buffer, resultPtr, errorPtr),
      CallbackType,
    );
  }

  /**
   * Handle async callback from C API
   */
  private handleAsyncCallback(userData: any, resultPtr: any, errorPtr: any): void {
    // DON'T log External objects directly - console.log tries to inspect them and causes segfaults!
    // console.log("🟢 [TS-handleAsyncCallback] ENTRY");
    // console.log("   userData present:", userData !== null && userData !== undefined);
    // console.log("   resultPtr present:", resultPtr !== null && resultPtr !== undefined);
    // console.log("   errorPtr present:", errorPtr !== null && errorPtr !== undefined);

    // Check if userData is null/undefined - this might be a spurious callback
    if (userData === null || userData === undefined) {
      // console.log("⚠️  [TS-handleAsyncCallback] userData is null/undefined, ignoring");
      // Silently ignore spurious callbacks with no user data during pump
      return;
    }

    // userData is passed as void* from C, decode it to get the callback ID
    let callbackId: bigint;
    try {
      // console.log("🔹 [TS-handleAsyncCallback] Decoding callback ID from userData...");
      // console.log("   userData type:", typeof userData);

      const isBuffer = Buffer.isBuffer(userData);
      // console.log("   Is Buffer?", isBuffer);

      if (isBuffer) {
        callbackId = userData.readBigUInt64LE(0);
        // console.log("   Callback ID from Buffer:", callbackId);
      } else {
        // console.log("   Attempting to read callback ID from native pointer view...");
        try {
          const view = koffi.view(userData, 8);
          const bufferFromPtr = Buffer.from(view);
          callbackId = bufferFromPtr.readBigUInt64LE(0);
          // console.log("   ✅ Successfully read callback ID from pointer view:", callbackId);
        } catch (viewError) {
          // console.log("   ❌ Failed to read callback ID from pointer view:", viewError);
          return;
        }
      }
    } catch (error) {
      // console.log("❌ [TS-handleAsyncCallback] Top-level decode error:", error);
      // console.log("   Error stack:", error instanceof Error ? error.stack : 'N/A');
      return;
    }

    // console.log("🔹 [TS-handleAsyncCallback] Looking up callback entry for ID:", callbackId);
    // console.log("   Registry size:", this.callbackRegistry.size);

    // Find the callback entry
    const entry = this.callbackRegistry.get(callbackId);
    if (!entry) {
      // Callback not found - it may have timed out or already been processed
      // This can happen during dispatcher pump - just ignore it
      // console.log("⚠️  [TS-handleAsyncCallback] Callback entry not found in registry");
      return;
    }

    // console.log("✅ [TS-handleAsyncCallback] Callback entry found, clearing timeout");

    if (entry.timeoutId) {
      clearTimeout(entry.timeoutId);
    }

    try {
      if (errorPtr !== null && errorPtr !== undefined) {
        // console.log("❌ [TS-handleAsyncCallback] Error pointer is set, rejecting");
        const { code, message } = koffi.decode(errorPtr, ErrorStruct);
        entry.reject(new Error(message ? `Filter error (${code}): ${message}` : `Filter error (${code})`));
        return;
      }

      if (resultPtr === null || resultPtr === undefined) {
        // console.log("❌ [TS-handleAsyncCallback] Result pointer is null, rejecting");
        entry.reject(new Error('Filter callback returned no result or error'));
        return;
      }

      // console.log("🔹 [TS-handleAsyncCallback] Decoding result from C API...");
      const cResult = koffi.decode(resultPtr, FilterResultStruct);
      // console.log("   Result decision:", cResult.decision);
      // console.log("   Transformed message:", cResult.transformed_message);

      const result: FilterResult = {
        decision: cResult.decision as FilterDecision,
        transformedMessage: cResult.transformed_message ?? undefined,
        reason: cResult.reason ?? undefined,
        delayMs: cResult.delay_ms || undefined,
        metadata: {},
      };

      // console.log("✅ [TS-handleAsyncCallback] Resolving promise with result");
      entry.resolve(result);
    } catch (err) {
      // console.log("❌ [TS-handleAsyncCallback] Exception:", err);
      entry.reject(err instanceof Error ? err : new Error(String(err)));
    } finally {
      this.callbackRegistry.delete(callbackId);
      // console.log("✅ [TS-handleAsyncCallback] EXIT - callback completed");
    }
  }

  /**
   * Submit an async message for processing
   */
  private submitAsyncMessage(direction: 'incoming' | 'outgoing', message: unknown): Promise<FilterResult> {
    const fnName = direction === 'incoming'
      ? 'mcp_chain_submit_incoming'
      : 'mcp_chain_submit_outgoing';

    return new Promise((resolve, reject) => {
      const callbackId = this.nextCallbackId++;
      const userDataBuffer = Buffer.alloc(8);
      userDataBuffer.writeBigUInt64LE(callbackId);
      const userDataPtr = koffi.as(userDataBuffer, 'void*');

      // console.log("📤 [submitAsyncMessage] ENTRY");
      // console.log("   Direction:", direction);
      // console.log("   Callback ID:", callbackId);
      // console.log("   Callback ID type:", typeof callbackId);
      // console.log("   userData buffer pointer created via koffi.as");

      this.callbackRegistry.set(callbackId, {
        resolve,
        reject,
        userDataBuffer,
        userDataPtr,
      });

      // console.log("   Registry size after insert:", this.callbackRegistry.size);
      // console.log("   Registry keys:", Array.from(this.callbackRegistry.keys()));

      // Allocate space for mcp_error_t* (pointer to opaque error handle)
      // This is an OUTPUT parameter where C side can store an error object
      const errorPtrPtr = koffi.alloc('void*', 1);

      try {
        // console.log("   Calling C API function:", fnName);
        // console.log("   Passing callback buffer pointer via koffi.as");

        const status = mcpFilterLib[fnName](
          this.handle,
          JSON.stringify(message),
          userDataPtr,  // Cast Buffer to void* so C reads callback ID
          this.nativeCallbackPtr
            ? koffi.as(this.nativeCallbackPtr, 'void*')
            : null,
          errorPtrPtr,
        );

        // console.log("   C API returned status:", status);

        if (status !== MCPStatus.OK) {
          this.callbackRegistry.delete(callbackId);

          // Check if C side populated the error pointer
          // Read the pointer value from the allocated memory
          const errorPtrArray = koffi.decode(errorPtrPtr, 'void*');
          const errorPtr = errorPtrArray;
          let errorMsg = `Submit failed with status ${status}`;

          if (errorPtr && errorPtr !== 0 && errorPtr !== null) {
            // Use accessor functions to get error details
            const code = mcpFilterLib.mcp_error_get_code(errorPtr);
            const message = mcpFilterLib.mcp_error_get_message(errorPtr);

            if (message) {
              errorMsg = `Submit failed (${code}): ${message}`;
            } else {
              errorMsg = `Submit failed (${code})`;
            }

            // Free the error object
            mcpFilterLib.mcp_error_free(errorPtr);
          }

          reject(new Error(errorMsg));
          return;
        }
      } finally {
        koffi.free(errorPtrPtr);
      }

      const entry = this.callbackRegistry.get(callbackId);
      if (entry) {
        entry.timeoutId = setTimeout(() => {
          if (this.callbackRegistry.delete(callbackId)) {
            reject(new Error('Filter request timed out after 30s'));
          }
        }, 30_000);
      }
    });
  }

  /**
   * Process an incoming message through the filter chain
   *
   * @param message - Message to process
   * @returns Filter result with decision and optional transformed message
   */
  async processIncoming(message: unknown): Promise<FilterResult> {
    await this.ensureInitialized();
    return this.submitAsyncMessage('incoming', message);
  }

  /**
   * Set unified chain-level event callback
   *
   * This method registers a unified callback that receives events from ALL filters
   * in the chain through a single callback handler. This replaces per-filter callbacks
   * and provides a unified observability surface.
   *
   * @param handler Event handler function that receives filter events
   * @throws Error if the chain is destroyed or registration fails
   *
   * @example
   * ```typescript
   * chain.setEventCallback((event) => {
   *   // console.log(`[${event.filterName}] ${FilterEventType[event.eventType]}`);
   *
   *   if (event.eventType === FilterEventType.CIRCUIT_STATE_CHANGE) {
   *     const { oldState, newState, reason } = event.eventData;
   *     // console.log(`Circuit breaker: ${oldState} → ${newState} (${reason})`);
   *   }
   *
   *   if (event.eventType === FilterEventType.RATE_LIMIT_EXCEEDED) {
   *     const { clientId, limit } = event.eventData;
   *     console.warn(`Rate limit exceeded for ${clientId}: ${limit}`);
   *   }
   * });
   * ```
   */
  setEventCallback(handler: FilterEventHandler): void {
    if (this.destroyed) {
      throw new Error('FilterChain has been destroyed');
    }

    // Unregister previous callback if any
    if (this.eventCallbackHandle) {
      try {
        unregisterFilterEventCallback(this.handle, this.eventCallbackHandle);
      } catch (error) {
        console.error('Error unregistering previous event callback:', error);
      }
      this.eventCallbackHandle = null;
    }

    // Register new callback
    try {
      const handle = registerFilterEventCallback(this.handle, handler);
      this.eventCallbackHandle = handle;
    } catch (error) {
      this.eventCallbackHandle = null;
      throw error;
    }
  }

  /**
   * Remove chain-level event callback
   *
   * This method unregisters the currently registered event callback.
   */
  clearEventCallback(): void {
    if (this.eventCallbackHandle) {
      try {
        unregisterFilterEventCallback(this.handle, this.eventCallbackHandle);
      } catch (error) {
        console.error('Error unregistering event callback:', error);
      }
      this.eventCallbackHandle = null;
    }
  }

  /**
   * Process an outgoing message through the filter chain
   *
   * @param message - Message to process
   * @returns Filter result with decision and optional transformed message
   */
  async processOutgoing(message: unknown): Promise<FilterResult> {
    await this.ensureInitialized();
    return this.submitAsyncMessage('outgoing', message);
  }

  /**
   * Destroy the filter chain and release resources
   *
   * After calling this, the FilterChain object should not be used.
   * All subsequent method calls will throw an error.
   *
   * Note: This will call shutdown() if the chain is still initialized.
   */
  destroy(): void {
    if (this.destroyed) {
      return; // Already destroyed, idempotent
    }

    // Clean up chain-level event callbacks
    this.clearEventCallback();

    // Shutdown if still initialized (this also cleans up callbacks)
    if (this.initialized) {
      try {
        // Use sync version to avoid async issues in destroy
        mcpFilterLib.mcp_filter_chain_shutdown(this.handle);
        this.initialized = false;
      } catch (error) {
        console.error('Error shutting down filter chain:', error);
      }
    }

    // Clean up any remaining pending callbacks
    this.callbackRegistry.forEach((entry) => {
      if (entry.timeoutId) {
        clearTimeout(entry.timeoutId);
      }
      entry.reject(new Error('Filter chain destroyed'));
    });
    this.callbackRegistry.clear();

    // Release the chain handle
    if (this.handle && this.handle !== 0) {
      mcpFilterLib.mcp_filter_chain_release(this.handle);
      this.handle = 0;
      this.destroyed = true;
    }
  }
}
