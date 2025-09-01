/**
 * @file filter-manager.ts
 * @brief Filter Manager for processing JSONRPCMessage through C++ filters
 *
 * This provides a simple interface to process MCP JSON-RPC messages
 * through our C++ filter infrastructure using FFI wrappers.
 */

import {
  addFilterToManager,
  BufferOwnership,
  BuiltinFilterType,
  createBufferFromString,
  createBuiltinFilter,
  createFilterManager,
  initializeFilterManager,
  postDataToFilter,
  readStringFromBuffer,
  releaseFilter,
  releaseFilterManager,
} from "./index";

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
 * Filter Manager Configuration
 */
export interface FilterManagerConfig {
  auth?: {
    method: "jwt" | "api-key" | "basic";
    secret?: string;
    key?: string;
  };
  rateLimit?: {
    requestsPerMinute: number;
    burstSize?: number;
  };
  logging?: boolean;
  metrics?: boolean;
  // Error handling configuration
  errorHandling?: {
    stopOnError?: boolean; // Stop processing on first filter error
    retryAttempts?: number; // Number of retry attempts for failed filters
    fallbackBehavior?: "reject" | "passthrough" | "default"; // What to do on error
  };
}

/**
 * Filter Manager for processing JSONRPCMessage
 */
export class FilterManager {
  private filterManager: number;
  private filters: {
    auth?: number;
    rateLimit?: number;
    logging?: number;
    metrics?: number;
  } = {};
  private config: FilterManagerConfig;
  private _isDestroyed: boolean = false;

  constructor(config: FilterManagerConfig = {}) {
    // Store configuration
    this.config = config;

    // Validate configuration
    this.validateConfig(config);

    // Create filter manager using FFI wrapper
    this.filterManager = createFilterManager(0, 0);

    // Setup filters based on configuration
    this.setupFilters(config);

    // Initialize filter manager
    initializeFilterManager(this.filterManager);
  }

  /**
   * Process JSON-RPC message through filters
   */
  async process(message: JSONRPCMessage): Promise<JSONRPCMessage> {
    this.ensureNotDestroyed();

    // Validate input message
    this.validateMessage(message);

    try {
      // Convert message to buffer using FFI wrapper
      const messageBuffer = createBufferFromString(
        JSON.stringify(message),
        BufferOwnership.SHARED
      );

      // Process through filters with error handling
      const processedBuffer = await this.processThroughFilters(messageBuffer);

      // Convert back to JSON-RPC message
      const processedMessage = JSON.parse(
        readStringFromBuffer(processedBuffer)
      );

      return processedMessage;
    } catch (error) {
      return this.handleProcessingError(error, message);
    }
  }

  /**
   * Process JSON-RPC response through filters
   * Optimized for response processing (typically logging, metrics, compression)
   */
  async processResponse(response: JSONRPCMessage): Promise<JSONRPCMessage> {
    this.ensureNotDestroyed();

    // Validate input response
    this.validateMessage(response);

    try {
      // Convert response to buffer using FFI wrapper
      const responseBuffer = createBufferFromString(
        JSON.stringify(response),
        BufferOwnership.SHARED
      );

      // Process through response-specific filters
      const processedBuffer = await this.processThroughResponseFilters(
        responseBuffer
      );

      // Convert back to JSON-RPC message
      const processedResponse = JSON.parse(
        readStringFromBuffer(processedBuffer)
      );

      return processedResponse;
    } catch (error) {
      return this.handleProcessingError(error, response);
    }
  }

  /**
   * Process a complete request-response cycle
   * Useful for MCP transport layer integration
   */
  async processRequestResponse(
    request: JSONRPCMessage,
    response: JSONRPCMessage
  ): Promise<{
    processedRequest: JSONRPCMessage;
    processedResponse: JSONRPCMessage;
  }> {
    this.ensureNotDestroyed();

    // Process request first
    const processedRequest = await this.process(request);

    // Process response
    const processedResponse = await this.processResponse(response);

    return { processedRequest, processedResponse };
  }

  /**
   * Destroy the FilterManager and release all C++ resources
   * This should be called when the FilterManager is no longer needed
   */
  destroy(): void {
    if (this._isDestroyed) {
      console.warn("FilterManager is already destroyed");
      return;
    }

    try {
      // Release all individual filters
      if (this.filters.auth) {
        releaseFilter(this.filters.auth);
        delete this.filters.auth;
      }

      if (this.filters.rateLimit) {
        releaseFilter(this.filters.rateLimit);
        delete this.filters.rateLimit;
      }

      if (this.filters.logging) {
        releaseFilter(this.filters.logging);
        delete this.filters.logging;
      }

      if (this.filters.metrics) {
        releaseFilter(this.filters.metrics);
        delete this.filters.metrics;
      }

      // Release the filter manager
      if (this.filterManager) {
        releaseFilterManager(this.filterManager);
        this.filterManager = 0;
      }

      this._isDestroyed = true;
      console.log("FilterManager destroyed and resources released");
    } catch (error) {
      console.error("Error during FilterManager destruction:", error);
      this._isDestroyed = true; // Mark as destroyed even if cleanup failed
    }
  }

  /**
   * Check if the FilterManager has been destroyed
   */
  isDestroyed(): boolean {
    return this._isDestroyed;
  }

  /**
   * Finalizer method for automatic cleanup
   * This will be called by the garbage collector
   */
  [Symbol.dispose](): void {
    if (!this._isDestroyed) {
      console.warn(
        "FilterManager was not properly destroyed, cleaning up automatically"
      );
      this.destroy();
    }
  }

  /**
   * Ensure the FilterManager is not destroyed before processing
   */
  private ensureNotDestroyed(): void {
    if (this._isDestroyed) {
      throw new Error(
        "FilterManager has been destroyed and cannot process messages"
      );
    }
  }

  /**
   * Setup filters based on configuration
   */
  private setupFilters(config: FilterManagerConfig): void {
    // Authentication filter
    if (config.auth) {
      this.filters.auth = createBuiltinFilter(
        0,
        BuiltinFilterType.AUTHENTICATION,
        {
          method: config.auth.method,
          secret: config.auth.secret,
          key: config.auth.key,
        }
      );
      addFilterToManager(this.filterManager, this.filters.auth);
    }

    // Rate limiting filter
    if (config.rateLimit) {
      this.filters.rateLimit = createBuiltinFilter(
        0,
        BuiltinFilterType.RATE_LIMIT,
        {
          requestsPerMinute: config.rateLimit.requestsPerMinute,
          burstSize: config.rateLimit.burstSize || 10,
        }
      );
      addFilterToManager(this.filterManager, this.filters.rateLimit);
    }

    // Logging filter
    if (config.logging) {
      this.filters.logging = createBuiltinFilter(
        0,
        BuiltinFilterType.ACCESS_LOG,
        {}
      );
      addFilterToManager(this.filterManager, this.filters.logging);
    }

    // Metrics filter
    if (config.metrics) {
      this.filters.metrics = createBuiltinFilter(
        0,
        BuiltinFilterType.METRICS,
        {}
      );
      addFilterToManager(this.filterManager, this.filters.metrics);
    }
  }

  /**
   * Process message through filters using C++ filter chain
   */
  private async processThroughFilters(buffer: number): Promise<number> {
    // Process through each filter using our FFI wrapper
    let processedBuffer = buffer;

    const filterChain = [
      { name: "auth", filter: this.filters.auth },
      { name: "rateLimit", filter: this.filters.rateLimit },
      { name: "logging", filter: this.filters.logging },
      { name: "metrics", filter: this.filters.metrics },
    ];

    for (const { name, filter } of filterChain) {
      if (filter) {
        try {
          processedBuffer = await this.processThroughFilter(
            filter,
            processedBuffer
          );
        } catch (error) {
          const shouldStop = this.config.errorHandling?.stopOnError ?? true;
          if (shouldStop) {
            throw new Error(`Filter '${name}' processing failed: ${error}`);
          }
          // Continue processing other filters if stopOnError is false
          console.warn(`Filter '${name}' failed, continuing: ${error}`);
        }
      }
    }

    return processedBuffer;
  }

  /**
   * Process response through response-specific filters
   * Typically includes logging, metrics, and compression (not auth/rate limiting)
   */
  private async processThroughResponseFilters(buffer: number): Promise<number> {
    // Process through response-specific filters only
    let processedBuffer = buffer;

    const responseFilterChain = [
      { name: "logging", filter: this.filters.logging },
      { name: "metrics", filter: this.filters.metrics },
    ];

    for (const { name, filter } of responseFilterChain) {
      if (filter) {
        try {
          processedBuffer = await this.processThroughFilter(
            filter,
            processedBuffer
          );
        } catch (error) {
          const shouldStop = this.config.errorHandling?.stopOnError ?? true;
          if (shouldStop) {
            throw new Error(
              `Response filter '${name}' processing failed: ${error}`
            );
          }
          // Continue processing other filters if stopOnError is false
          console.warn(
            `Response filter '${name}' failed, continuing: ${error}`
          );
        }
      }
    }

    return processedBuffer;
  }

  /**
   * Process message through a single filter using FFI
   */
  private async processThroughFilter(
    filter: number,
    buffer: number
  ): Promise<number> {
    // Use postDataToFilter to process through the specific filter
    // This will call the C++ filter's processing logic
    return new Promise((resolve, reject) => {
      postDataToFilter(
        filter,
        new Uint8Array(), // Empty data since we're using buffer
        (result: any, _userData: any) => {
          if (result) {
            resolve(buffer); // Return the processed buffer
          } else {
            reject(new Error("Filter processing failed"));
          }
        },
        null // No user data
      );
    });
  }

  /**
   * Validate configuration
   */
  private validateConfig(config: FilterManagerConfig): void {
    // Validate rate limit configuration
    if (config.rateLimit) {
      if (config.rateLimit.requestsPerMinute <= 0) {
        throw new Error("Rate limit requestsPerMinute must be positive");
      }
      if (config.rateLimit.burstSize && config.rateLimit.burstSize <= 0) {
        throw new Error("Rate limit burstSize must be positive");
      }
    }

    // Validate auth configuration
    if (config.auth) {
      if (config.auth.method === "jwt" && !config.auth.secret) {
        throw new Error("JWT authentication requires a secret");
      }
      if (config.auth.method === "api-key" && !config.auth.key) {
        throw new Error("API key authentication requires a key");
      }
    }

    // Validate error handling configuration
    if (config.errorHandling) {
      if (
        config.errorHandling.retryAttempts &&
        config.errorHandling.retryAttempts < 0
      ) {
        throw new Error("Retry attempts must be non-negative");
      }
    }
  }

  /**
   * Validate JSON-RPC message
   */
  private validateMessage(message: JSONRPCMessage): void {
    if (!message) {
      throw new Error("Message cannot be null or undefined");
    }

    if (message.jsonrpc !== "2.0") {
      throw new Error("Invalid JSON-RPC version. Must be '2.0'");
    }

    // Check if it's a request or response
    const isRequest = message.method !== undefined;
    const isResponse =
      message.result !== undefined || message.error !== undefined;

    if (!isRequest && !isResponse) {
      throw new Error(
        "Message must be either a request (with method) or response (with result/error)"
      );
    }

    if (isRequest && isResponse) {
      throw new Error("Message cannot be both a request and response");
    }
  }

  /**
   * Handle processing errors based on configuration
   */
  private handleProcessingError(
    error: any,
    originalMessage: JSONRPCMessage
  ): JSONRPCMessage {
    const fallbackBehavior =
      this.config.errorHandling?.fallbackBehavior ?? "reject";

    switch (fallbackBehavior) {
      case "reject":
        throw new Error(`Filter processing failed: ${error}`);

      case "passthrough":
        console.warn(
          `Filter processing failed, returning original message: ${error}`
        );
        return originalMessage;

      case "default":
        // Return a default error response
        return {
          jsonrpc: "2.0",
          id: originalMessage.id || "unknown",
          error: {
            code: -32603, // Internal error
            message: "Filter processing failed",
            data: { originalError: error.toString() },
          },
        };

      default:
        throw new Error(`Unknown fallback behavior: ${fallbackBehavior}`);
    }
  }
}
