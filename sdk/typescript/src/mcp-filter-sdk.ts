/**
 * @file mcp-filter-sdk.ts
 * @brief Main MCP Filter SDK class
 *
 * This class provides a high-level TypeScript interface to the MCP Filter C API,
 * handling resource management, error handling, and providing a developer-friendly
 * API for working with filters, buffers, and filter chains.
 */

import { EventEmitter } from "events";
import { mcpFilterLib } from "./ffi-bindings";
import {
  McpBufferFlag,
  McpBufferHandle,
  McpBuiltinFilterType,
  McpFilter,
  McpFilterChain,
  McpFilterConfig,
  McpMemoryPool,
  McpProtocolLayer,
  McpResult,
  McpResultWrapper,
  createError,
  createSuccess,
  isSuccess,
} from "./types";

// ============================================================================
// SDK Events
// ============================================================================

export interface McpSdkEvents {
  "filter:created": (filter: McpFilter, config: McpFilterConfig) => void;
  "filter:destroyed": (filter: McpFilter) => void;
  "filter:error": (filter: McpFilter, error: string) => void;
  "chain:created": (chain: McpFilterChain) => void;
  "chain:destroyed": (chain: McpFilterChain) => void;
  "buffer:created": (buffer: McpBufferHandle, size: number) => void;
  "buffer:destroyed": (buffer: McpBufferHandle) => void;
  "memory:allocated": (size: number) => void;
  "memory:freed": (size: number) => void;
}

// ============================================================================
// SDK Configuration
// ============================================================================

export interface McpSdkConfig {
  autoCleanup: boolean; // Automatically clean up resources
  memoryPoolSize: number; // Default memory pool size
  enableLogging: boolean; // Enable SDK logging
  logLevel: "debug" | "info" | "warn" | "error";
  maxFilters: number; // Maximum number of filters
  maxChains: number; // Maximum number of chains
  maxBuffers: number; // Maximum number of buffers
}

// ============================================================================
// SDK Statistics
// ============================================================================

export interface McpSdkStats {
  totalFilters: number;
  totalChains: number;
  totalBuffers: number;
  totalMemoryPools: number;
  totalMemoryAllocated: number;
  totalOperations: number;
  totalErrors: number;
  uptime: number;
}

// ============================================================================
// Main SDK Class
// ============================================================================

export class McpFilterSdk extends EventEmitter {
  private _initialized: boolean = false;
  private _dispatcher: number = 0;
  private _defaultMemoryPool: McpMemoryPool = 0;
  private _filters: Set<McpFilter> = new Set();
  private _chains: Set<McpFilterChain> = new Set();
  private _buffers: Set<McpBufferHandle> = new Set();
  private _memoryPools: Set<McpMemoryPool> = new Set();
  private _startTime: number = Date.now();
  private _config: McpSdkConfig;

  constructor(config: Partial<McpSdkConfig> = {}) {
    super();

    this._config = {
      autoCleanup: true,
      memoryPoolSize: 1024 * 1024, // 1MB
      enableLogging: true,
      logLevel: "info",
      maxFilters: 1000,
      maxChains: 100,
      maxBuffers: 10000,
      ...config,
    };
  }

  // ============================================================================
  // Initialization and Cleanup
  // ============================================================================

  /**
   * Initialize the MCP Filter SDK
   */
  async initialize(): Promise<McpResultWrapper<void>> {
    try {
      if (this._initialized) {
        return createError(
          McpResult.ERROR_ALREADY_INITIALIZED,
          "SDK already initialized"
        );
      }

      this.log("info", "Initializing MCP Filter SDK...");

      // Initialize MCP library
      const initResult = mcpFilterLib.mcp_init(null);
      if (!isSuccess(initResult)) {
        return createError(
          McpResult.ERROR_INITIALIZATION_FAILED,
          "Failed to initialize MCP library"
        );
      }

      // Create dispatcher
      this._dispatcher = mcpFilterLib.mcp_dispatcher_create();
      if (this._dispatcher === 0) {
        return createError(
          McpResult.ERROR_INITIALIZATION_FAILED,
          "Failed to create dispatcher"
        );
      }

      // Create default memory pool
      this._defaultMemoryPool = mcpFilterLib.mcp_memory_pool_create(
        this._config.memoryPoolSize
      );
      if (this._defaultMemoryPool === 0) {
        return createError(
          McpResult.ERROR_INITIALIZATION_FAILED,
          "Failed to create default memory pool"
        );
      }

      this._memoryPools.add(this._defaultMemoryPool);
      this._initialized = true;

      this.log("info", "MCP Filter SDK initialized successfully");
      this.emit("memory:allocated", this._config.memoryPoolSize);

      return createSuccess(undefined);
    } catch (error) {
      const errorMessage =
        error instanceof Error ? error.message : "Unknown error";
      return createError(
        McpResult.ERROR_INITIALIZATION_FAILED,
        `Initialization failed: ${errorMessage}`
      );
    }
  }

  /**
   * Shutdown the MCP Filter SDK
   */
  async shutdown(): Promise<McpResultWrapper<void>> {
    try {
      if (!this._initialized) {
        return createError(
          McpResult.ERROR_NOT_INITIALIZED,
          "SDK not initialized"
        );
      }

      this.log("info", "Shutting down MCP Filter SDK...");

      // Clean up all resources
      if (this._config.autoCleanup) {
        await this.cleanupAll();
      }

      // Destroy default memory pool
      if (this._defaultMemoryPool !== 0) {
        mcpFilterLib.mcp_memory_pool_destroy(this._defaultMemoryPool);
        this._memoryPools.delete(this._defaultMemoryPool);
        this._defaultMemoryPool = 0;
      }

      // Destroy dispatcher
      if (this._dispatcher !== 0) {
        mcpFilterLib.mcp_dispatcher_destroy(this._dispatcher);
        this._dispatcher = 0;
      }

      // Shutdown MCP library
      mcpFilterLib.mcp_shutdown();

      this._initialized = false;
      this.log("info", "MCP Filter SDK shut down successfully");

      return createSuccess(undefined);
    } catch (error) {
      const errorMessage =
        error instanceof Error ? error.message : "Unknown error";
      return createError(
        McpResult.ERROR_CLEANUP_FAILED,
        `Shutdown failed: ${errorMessage}`
      );
    }
  }

  /**
   * Check if SDK is initialized
   */
  isInitialized(): boolean {
    return this._initialized;
  }

  // ============================================================================
  // Filter Management
  // ============================================================================

  /**
   * Create a new filter
   */
  async createFilter(
    _config: McpFilterConfig
  ): Promise<McpResultWrapper<McpFilter>> {
    // TODO: Implement proper C struct serialization
    // For now, return an error to avoid struct issues
    return createError(
      McpResult.ERROR_INITIALIZATION_FAILED,
      "Filter creation temporarily disabled - struct serialization not implemented"
    );
  }

  /**
   * Create a built-in filter
   */
  async createBuiltinFilter(
    type: McpBuiltinFilterType,
    settings?: any
  ): Promise<McpResultWrapper<McpFilter>> {
    try {
      if (!this._initialized) {
        return createError(
          McpResult.ERROR_NOT_INITIALIZED,
          "SDK not initialized"
        );
      }

      // Create JSON settings if provided
      let jsonSettings = 0;
      if (settings) {
        jsonSettings = mcpFilterLib.mcp_json_create_object();
        // TODO: Add settings to JSON object
      }

      // Create built-in filter
      const filter = mcpFilterLib.mcp_filter_create_builtin(
        this._dispatcher,
        type,
        jsonSettings
      );
      if (filter === 0) {
        return createError(
          McpResult.ERROR_INITIALIZATION_FAILED,
          "Failed to create built-in filter"
        );
      }

      this._filters.add(filter);
      this.emit("filter:created", filter, {
        name: `builtin_${type}`,
        type,
        layer: McpProtocolLayer.LAYER_7_APPLICATION,
      });
      this.log("info", `Built-in filter created: ${type} (${filter})`);

      return createSuccess(filter);
    } catch (error) {
      const errorMessage =
        error instanceof Error ? error.message : "Unknown error";
      return createError(
        McpResult.ERROR_INITIALIZATION_FAILED,
        `Built-in filter creation failed: ${errorMessage}`
      );
    }
  }

  /**
   * Process data through a filter
   */
  async processFilterData(
    filter: McpFilter,
    data: Buffer
  ): Promise<McpResultWrapper<boolean>> {
    try {
      if (!this._filters.has(filter)) {
        return createError(McpResult.ERROR_NOT_FOUND, "Filter not found");
      }

      const result = mcpFilterLib.mcp_filter_process_data(
        filter,
        data,
        data.length
      );
      if (result === 0) {
        return createError(
          McpResult.ERROR_IO_ERROR,
          "Filter data processing failed"
        );
      }

      this.log(
        "info",
        `Filter data processed: ${filter} (${data.length} bytes)`
      );
      return createSuccess(true);
    } catch (error) {
      const errorMessage =
        error instanceof Error ? error.message : "Unknown error";
      return createError(
        McpResult.ERROR_IO_ERROR,
        `Filter data processing failed: ${errorMessage}`
      );
    }
  }

  /**
   * Destroy a filter
   */
  async destroyFilter(filter: McpFilter): Promise<McpResultWrapper<void>> {
    try {
      if (!this._filters.has(filter)) {
        return createError(McpResult.ERROR_NOT_FOUND, "Filter not found");
      }

      mcpFilterLib.mcp_filter_release(filter);
      this._filters.delete(filter);
      this.emit("filter:destroyed", filter);
      this.log("info", `Filter destroyed: ${filter}`);

      return createSuccess(undefined);
    } catch (error) {
      const errorMessage =
        error instanceof Error ? error.message : "Unknown error";
      return createError(
        McpResult.ERROR_CLEANUP_FAILED,
        `Filter destruction failed: ${errorMessage}`
      );
    }
  }

  // ============================================================================
  // Buffer Management
  // ============================================================================

  /**
   * Create a buffer
   */
  async createBuffer(
    data?: Buffer,
    flags: number = McpBufferFlag.OWNED
  ): Promise<McpResultWrapper<McpBufferHandle>> {
    try {
      if (!this._initialized) {
        return createError(
          McpResult.ERROR_NOT_INITIALIZED,
          "SDK not initialized"
        );
      }

      if (this._buffers.size >= this._config.maxBuffers) {
        return createError(
          McpResult.ERROR_RESOURCE_LIMIT,
          "Maximum number of buffers reached"
        );
      }

      const dataPtr = data ? data : Buffer.alloc(0);
      const buffer = mcpFilterLib.mcp_filter_buffer_create(
        dataPtr,
        dataPtr.length,
        flags
      );

      if (buffer === 0) {
        return createError(
          McpResult.ERROR_INITIALIZATION_FAILED,
          "Failed to create buffer"
        );
      }

      this._buffers.add(buffer);
      this.emit("buffer:created", buffer, dataPtr.length);
      this.log("info", `Buffer created: ${buffer} (${dataPtr.length} bytes)`);

      return createSuccess(buffer);
    } catch (error) {
      const errorMessage =
        error instanceof Error ? error.message : "Unknown error";
      return createError(
        McpResult.ERROR_INITIALIZATION_FAILED,
        `Buffer creation failed: ${errorMessage}`
      );
    }
  }

  /**
   * Destroy a buffer
   */
  async destroyBuffer(
    buffer: McpBufferHandle
  ): Promise<McpResultWrapper<void>> {
    try {
      if (!this._buffers.has(buffer)) {
        return createError(McpResult.ERROR_NOT_FOUND, "Buffer not found");
      }

      mcpFilterLib.mcp_filter_buffer_release(buffer);
      this._buffers.delete(buffer);
      this.emit("buffer:destroyed", buffer);
      this.log("info", `Buffer destroyed: ${buffer}`);

      return createSuccess(undefined);
    } catch (error) {
      const errorMessage =
        error instanceof Error ? error.message : "Unknown error";
      return createError(
        McpResult.ERROR_CLEANUP_FAILED,
        `Buffer destruction failed: ${errorMessage}`
      );
    }
  }

  // ============================================================================
  // Memory Pool Management
  // ============================================================================

  /**
   * Create a memory pool
   */
  async createMemoryPool(
    size: number
  ): Promise<McpResultWrapper<McpMemoryPool>> {
    try {
      if (!this._initialized) {
        return createError(
          McpResult.ERROR_NOT_INITIALIZED,
          "SDK not initialized"
        );
      }

      const pool = mcpFilterLib.mcp_memory_pool_create(size);
      if (pool === 0) {
        return createError(
          McpResult.ERROR_INITIALIZATION_FAILED,
          "Failed to create memory pool"
        );
      }

      this._memoryPools.add(pool);
      this.emit("memory:allocated", size);
      this.log("info", `Memory pool created: ${pool} (${size} bytes)`);

      return createSuccess(pool);
    } catch (error) {
      const errorMessage =
        error instanceof Error ? error.message : "Unknown error";
      return createError(
        McpResult.ERROR_INITIALIZATION_FAILED,
        `Memory pool creation failed: ${errorMessage}`
      );
    }
  }

  /**
   * Destroy a memory pool
   */
  async destroyMemoryPool(
    pool: McpMemoryPool
  ): Promise<McpResultWrapper<void>> {
    try {
      if (!this._memoryPools.has(pool)) {
        return createError(McpResult.ERROR_NOT_FOUND, "Memory pool not found");
      }

      mcpFilterLib.mcp_memory_pool_destroy(pool);
      this._memoryPools.delete(pool);
      this.emit("memory:freed", 0); // TODO: Get actual size
      this.log("info", `Memory pool destroyed: ${pool}`);

      return createSuccess(undefined);
    } catch (error) {
      const errorMessage =
        error instanceof Error ? error.message : "Unknown error";
      return createError(
        McpResult.ERROR_CLEANUP_FAILED,
        `Memory pool destruction failed: ${errorMessage}`
      );
    }
  }

  // ============================================================================
  // Utility Methods
  // ============================================================================

  /**
   * Get SDK statistics
   */
  getStats(): McpSdkStats {
    return {
      totalFilters: this._filters.size,
      totalChains: this._chains.size,
      totalBuffers: this._buffers.size,
      totalMemoryPools: this._memoryPools.size,
      totalMemoryAllocated:
        this._config.memoryPoolSize * this._memoryPools.size,
      totalOperations: 0, // TODO: Track operations
      totalErrors: 0, // TODO: Track errors
      uptime: Date.now() - this._startTime,
    };
  }

  /**
   * Clean up all resources
   */
  async cleanupAll(): Promise<void> {
    this.log("info", "Cleaning up all resources...");

    // Clean up filters
    for (const filter of this._filters) {
      try {
        mcpFilterLib.mcp_filter_release(filter);
      } catch (error) {
        this.log("error", `Failed to release filter ${filter}: ${error}`);
      }
    }
    this._filters.clear();

    // Clean up chains
    for (const chain of this._chains) {
      try {
        mcpFilterLib.mcp_filter_chain_release(chain);
      } catch (error) {
        this.log("error", `Failed to release chain ${chain}: ${error}`);
      }
    }
    this._chains.clear();

    // Clean up buffers
    for (const buffer of this._buffers) {
      try {
        mcpFilterLib.mcp_filter_buffer_release(buffer);
      } catch (error) {
        this.log("error", `Failed to release buffer ${buffer}: ${error}`);
      }
    }
    this._buffers.clear();

    // Clean up memory pools (except default)
    for (const pool of this._memoryPools) {
      if (pool !== this._defaultMemoryPool) {
        try {
          mcpFilterLib.mcp_memory_pool_destroy(pool);
        } catch (error) {
          this.log("error", `Failed to destroy memory pool ${pool}: ${error}`);
        }
      }
    }
    this._memoryPools.clear();
    if (this._defaultMemoryPool !== 0) {
      this._memoryPools.add(this._defaultMemoryPool);
    }

    this.log("info", "All resources cleaned up");
  }

  /**
   * Log a message
   */
  private log(level: string, message: string): void {
    if (!this._config.enableLogging) return;

    const timestamp = new Date().toISOString();
    const logMessage = `[${timestamp}] [${level.toUpperCase()}] MCP Filter SDK: ${message}`;

    switch (level) {
      case "debug":
        console.debug(logMessage);
        break;
      case "info":
        console.info(logMessage);
        break;
      case "warn":
        console.warn(logMessage);
        break;
      case "error":
        console.error(logMessage);
        break;
    }
  }
}

// ============================================================================
// Default Export
// ============================================================================

export default McpFilterSdk;
