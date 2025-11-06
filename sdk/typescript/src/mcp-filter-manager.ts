/**
 * @file filter-manager.ts
 * @brief Filter Manager for processing JSONRPCMessage through C++ filters
 *
 * This provides a simple interface to process MCP JSON-RPC messages
 * through our C++ filter infrastructure using FFI wrappers.
 */

import {
  addChainToManager,
  createFilterManager,
  ensureMcpInitialized,
  FilterStatus,
  initializeFilterManager,
  postDataToFilter,
  releaseFilterChain,
  releaseFilterManager,
} from "./mcp-filter-api";


// Import the three filter modules as requested
import {
  ChainExecutionMode,
  RoutingStrategy,
  createFilterChainFromConfig,
  CanonicalConfig,
} from "./mcp-filter-chain";

import * as BufferModule from "./mcp-filter-buffer";


/**
 * JSON-RPC Message interface (compatible with MCP)
 */
export interface JSONRPCMessage {
  jsonrpc: "2.0";
  id?: string | number;
  method?: string;
  params?: any;
  result?: any;
  error?: {
    code: number;
    message: string;
    data?: any;
  };
}

/**
 * Filter Manager configuration
 */
export interface FilterManagerConfig {
  // Network filters
  network?: {
    tcpProxy?: boolean | {
      enabled: boolean;
      upstreamHost?: string;
      upstreamPort?: number;
      bindAddress?: string;
      bindPort?: number;
    };
    udpProxy?: boolean;
  };

  // HTTP filters
  http?: {
    codec?: boolean | {
      enabled: boolean;
      compressionLevel?: number;
      maxRequestSize?: number;
      maxResponseSize?: number;
    };
    routing?: boolean;
  };

  // Security filters
  security?: {
    authentication?: boolean | {
      method?: string;
      secret?: string;
      issuer?: string;
      audience?: string;
    };
    authorization?: boolean | {
      enabled: boolean;
      policy?: string;
      rules?: Array<{
        resource: string;
        action: string;
        conditions?: any;
      }>;
    };
    rateLimiting?: boolean;
  };

  // Observability filters
  observability?: {
    accessLog?: boolean | {
      enabled: boolean;
      format?: string;
      fields?: string[];
      output?: string;
    };
    metrics?: boolean | {
      enabled: boolean;
      endpoint?: string;
      interval?: number;
      labels?: Record<string, string>;
    };
    tracing?: boolean | {
      enabled: boolean;
      serviceName?: string;
      endpoint?: string;
      samplingRate?: number;
    };
  };

  // Traffic management
  trafficManagement?: {
    circuitBreaker?: boolean | {
      enabled: boolean;
      failureThreshold?: number;
      timeout?: number;
      resetTimeout?: number;
    };
    retry?: boolean | {
      enabled: boolean;
      maxAttempts?: number;
      backoffStrategy?: string;
      baseDelay?: number;
      maxDelay?: number;
    };
    timeout?: boolean;
  };

  // MCP-specific filters
  mcp?: {
    jsonRpcProtocol?: boolean;
    sseCodec?: boolean;
  };

  // Chain configuration
  chain?: {
    executionMode?: ChainExecutionMode;
    routingStrategy?: RoutingStrategy;
    maxParallel?: number;
    bufferSize?: number;
    timeoutMs?: number;
  };

  // Error handling
  errorHandling?: {
    stopOnError?: boolean;
    retryAttempts?: number;
    retryDelayMs?: number;
    fallbackBehavior?: 'passthrough' | 'default' | 'error';
  };

  // Custom filters
  customFilters?: Array<{
    type: string;
    enabled?: boolean;
    config?: any;
  }>;

  // Top-level shortcuts for common filters (for backward compatibility)
  auth?: boolean | {
    method?: string;
    secret?: string;
    issuer?: string;
    audience?: string;
  };
  rateLimit?: boolean | {
    requestsPerSecond?: number;
    burstSize?: number;
  };
  logging?: boolean | {
    level?: string;
    format?: string;
  };
  metrics?: boolean | {
    enabled?: boolean;
    endpoint?: string;
  };
}

/**
 * Options for FilterManager initialization
 */
export interface FilterManagerOptions {
  dispatcherHandle?: any;  // Opaque dispatcher handle
  connectionHandle?: any;  // Opaque connection handle
}

/**
 * Filter Manager for processing JSON-RPC messages through filter chain
 */
export class FilterManager {
  private filterManager: number;
  private filterChain: number | null = null;
  private bufferPool: number;
  private _isDestroyed: boolean = false;
  private dispatcherHandle: any = 0;
  private connectionHandle: any = 0;

  constructor(config: FilterManagerConfig = {}, options: FilterManagerOptions = {}) {
    // Initialize MCP library if not already initialized
    console.log("🔍 FilterManager constructor: ensuring MCP library is initialized");
    ensureMcpInitialized();

    // Store handles from options or use defaults (stub)
    this.dispatcherHandle = options.dispatcherHandle || 0;
    this.connectionHandle = options.connectionHandle || 0;

    // Create filter manager with provided handles
    console.log("🔍 Creating filter manager...");
    console.log(`🔍 Using dispatcher handle (type: ${typeof this.dispatcherHandle})`);
    console.log(`🔍 Using connection handle (type: ${typeof this.connectionHandle})`);
    this.filterManager = createFilterManager(this.connectionHandle, this.dispatcherHandle);
    if (!this.filterManager) {
      throw new Error("Failed to create filter manager");
    }
    console.log(`🔍 Filter manager created: ${this.filterManager}`);

    // Initialize filter manager
    console.log("🔍 Initializing filter manager...");
    const initResult = initializeFilterManager(this.filterManager);
    console.log(`🔍 Initialize result: ${initResult}`);
    if (initResult !== FilterStatus.CONTINUE) {
      throw new Error(`Failed to initialize filter manager: ${initResult}`);
    }

    // Create buffer pool for message processing
    this.bufferPool = BufferModule.createBufferPoolSimple(1024 * 1024, 10, 0); // 1MB buffers, 10 max

    // Setup filter chain based on config
    this.setupFilterChain(config);
  }

  /**
   * Setup filter chain using canonical configuration
   */
  private setupFilterChain(config: FilterManagerConfig): void {
    // Collect all filter types from the configuration
    const filterTypes: string[] = [];

    // Add filters based on configuration
    if (config.network?.tcpProxy) filterTypes.push("tcp.proxy");
    if (config.network?.udpProxy) filterTypes.push("udp.proxy");
    if (config.http?.codec) filterTypes.push("http.codec");
    if (config.http?.routing) filterTypes.push("http.router");
    if (config.security?.rateLimiting) filterTypes.push("rate_limit");
    if (config.security?.authentication) filterTypes.push("auth");
    if (config.security?.authorization) filterTypes.push("authz");
    if (config.mcp?.jsonRpcProtocol) filterTypes.push("json_rpc.dispatcher");
    if (config.mcp?.sseCodec) filterTypes.push("sse.codec");
    if (config.observability?.accessLog) filterTypes.push("access_log");
    if (config.observability?.metrics) filterTypes.push("metrics");
    if (config.observability?.tracing) filterTypes.push("tracing");
    if (config.trafficManagement?.circuitBreaker) filterTypes.push("circuit_breaker");
    if (config.trafficManagement?.retry) filterTypes.push("retry");
    if (config.trafficManagement?.timeout) filterTypes.push("timeout");

    // Add custom filters
    if (config.customFilters) {
      for (const custom of config.customFilters) {
        filterTypes.push(custom.type);
      }
    }

    // If no filters configured, add a basic set
    if (filterTypes.length === 0) {
      filterTypes.push("http.codec", "json_rpc.dispatcher");
    }

    // Create canonical configuration
    const canonicalConfig: CanonicalConfig = {
      listeners: [
        {
          name: "mcp_filter_manager_listener",
          address: {
            socket_address: {
              address: "127.0.0.1",
              port_value: 8080,
            },
          },
          filter_chains: [
            {
              filters: filterTypes.map((type, index) => ({
                name: `filter_${index}`,
                type: type,
                config: config.customFilters?.find(f => f.type === type)?.config,
              })),
            },
          ],
        },
      ],
    };

    // Create filter chain using canonical configuration
    this.filterChain = createFilterChainFromConfig(this.filterManager, canonicalConfig);
    if (!this.filterChain) {
      throw new Error("Failed to create filter chain");
    }

    // Add chain to manager
    addChainToManager(this.filterManager, this.filterChain);
  }

  /**
   * Process a JSON-RPC message through the filter chain
   */
  async processMessage(message: JSONRPCMessage): Promise<JSONRPCMessage> {
    this.checkNotDestroyed();

    // Serialize message to JSON
    const messageStr = JSON.stringify(message);
    const messageBytes = new TextEncoder().encode(messageStr);

    // Create a buffer for the message
    const buffer = BufferModule.createBuffer(messageBytes.length);
    if (!buffer) {
      throw new Error("Failed to create buffer");
    }

    try {
      // Write message to buffer
      BufferModule.writeBufferData(buffer, messageBytes);

      // Process through filter chain - pass messageBytes directly
      const status = postDataToFilter(this.filterManager, messageBytes, () => {}, null);
      if (status !== FilterStatus.CONTINUE) {
        throw new Error(`Filter processing failed with status: ${status}`);
      }

      // Read processed message from buffer
      const bufferSize = BufferModule.getBufferLength(buffer);
      const processedBytes = BufferModule.readBufferData(buffer, bufferSize);
      const processedStr = new TextDecoder().decode(processedBytes);

      // Parse and return processed message
      return JSON.parse(processedStr) as JSONRPCMessage;
    } finally {
      // Release buffer
      BufferModule.releaseBuffer(buffer);
    }
  }

  /**
   * Process multiple messages in batch
   */
  async processBatch(messages: JSONRPCMessage[]): Promise<JSONRPCMessage[]> {
    const results: JSONRPCMessage[] = [];

    for (const message of messages) {
      try {
        const processed = await this.processMessage(message);
        results.push(processed);
      } catch (error) {
        // Create error response
        const errorResponse: JSONRPCMessage = {
          jsonrpc: "2.0",
          error: {
            code: -32603,
            message: "Internal error",
            data: error instanceof Error ? error.message : String(error),
          },
        };

        // Only add id if the original message had one
        if (message.id !== undefined) {
          errorResponse.id = message.id;
        }

        results.push(errorResponse);
      }
    }

    return results;
  }

  /**
   * Update configuration and rebuild filter chain
   */
  async updateConfig(config: FilterManagerConfig): Promise<void> {
    this.checkNotDestroyed();

    // Release old chain if exists
    if (this.filterChain) {
      releaseFilterChain(this.filterChain);
      this.filterChain = null;
    }

    // Setup new chain
    this.setupFilterChain(config);
  }

  /**
   * Get filter chain statistics
   */
  getStats(): any {
    this.checkNotDestroyed();

    return {
      isActive: this.filterChain !== null,
      // Buffer pool stats would go here if available
      // Add more stats as needed
    };
  }

  /**
   * Process method alias for backward compatibility
   * @deprecated Use processMessage instead
   */
  async process(message: JSONRPCMessage): Promise<JSONRPCMessage> {
    return this.processMessage(message);
  }

  /**
   * Process response method for handling responses
   */
  async processResponse(message: JSONRPCMessage): Promise<JSONRPCMessage> {
    return this.processMessage(message);
  }

  /**
   * Process both request and response in sequence
   * @param request - The request message to process
   * @param response - The response message to process
   * @returns Tuple of [processed request, processed response]
   */
  async processRequestResponse(
    request: JSONRPCMessage,
    response: JSONRPCMessage
  ): Promise<[JSONRPCMessage, JSONRPCMessage]> {
    const processedRequest = await this.processMessage(request);
    const processedResponse = await this.processMessage(response);
    return [processedRequest, processedResponse];
  }

  /**
   * Check if the FilterManager has been destroyed
   * @returns true if destroyed, false otherwise
   */
  isDestroyed(): boolean {
    return this._isDestroyed;
  }

  /**
   * Destroy the filter manager and release resources
   */
  destroy(): void {
    if (this._isDestroyed) return;

    // Release filter chain
    if (this.filterChain) {
      releaseFilterChain(this.filterChain);
      this.filterChain = null;
    }

    // Release filter manager
    releaseFilterManager(this.filterManager);

    // Destroy buffer pool
    BufferModule.destroyBufferPool(this.bufferPool);

    this._isDestroyed = true;
  }

  /**
   * Check if manager has been destroyed
   */
  private checkNotDestroyed(): void {
    if (this._isDestroyed) {
      throw new Error("FilterManager has been destroyed and cannot process messages");
    }
  }
}

/**
 * Create a pre-configured filter manager for MCP
 */
export function createMcpFilterManager(): FilterManager {
  return new FilterManager({
    mcp: {
      jsonRpcProtocol: true,
      sseCodec: true,
    },
    http: {
      codec: true,
    },
    observability: {
      metrics: true,
    },
    errorHandling: {
      stopOnError: false,
      retryAttempts: 3,
      retryDelayMs: 100,
    },
  });
}

/**
 * Create a filter manager with custom configuration
 */
export function createCustomFilterManager(config: FilterManagerConfig): FilterManager {
  return new FilterManager(config);
}

// Export types for external use
export { ChainExecutionMode, RoutingStrategy, FilterStatus };
