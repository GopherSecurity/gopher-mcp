/**
 * @file gopher-filtered-transport.ts
 * @brief Transport wrapper adding C++ filters to MCP SDK transports
 *
 * This module provides GopherFilteredTransport, a wrapper that implements
 * the MCP SDK Transport interface while injecting Gopher-MCP C++ filters
 * into the message flow. This enables Hybrid SDK + Gopher Filters.
 *
 * ## Architecture
 * ```
 * MCP SDK (@modelcontextprotocol/sdk)
 *     ↓
 * GopherFilteredTransport (this file)
 *     ↓
 * FilterChain (filter-chain-ffi.ts) ← Koffi FFI
 *     ↓
 * C++ Gopher-MCP Filters
 * ```
 *
 * ## Usage
 * ```typescript
 * import { Server } from "@modelcontextprotocol/sdk/server/mcp.js";
 * import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
 * import { GopherFilteredTransport } from "./gopher-filtered-transport";
 *
 * const server = new Server({ name: "my-server", version: "1.0.0" });
 * const baseTransport = new StdioServerTransport();
 * const transport = new GopherFilteredTransport(baseTransport, {
 *   dispatcherHandle: dispatcher,
 *   filterConfig: {
 *     listeners: [{
 *       name: "filter_chain",
 *       filter_chains: [{
 *         filters: [
 *           { type: "rate_limiter", name: "limiter", config: { rps: 100 } },
 *           { type: "circuit_breaker", name: "breaker", config: { threshold: 5 } }
 *         ]
 *       }]
 *     }]
 *   }
 * });
 * await server.connect(transport);
 * ```
 *
 * Note: In hybrid SDK mode, the address configuration is optional since the SDK
 * handles actual transport. The filter chain is purely for message processing.
 */

import type { Transport } from "@modelcontextprotocol/sdk/shared/transport.js";
import type { JSONRPCMessage, MessageExtraInfo } from "@modelcontextprotocol/sdk/types.js";
import { FilterChain } from "./filter-chain-ffi.js";
import { FilterDecision } from "./filter-types.js";
import type { CanonicalConfig, FilterMetrics } from "./filter-types.js";
import { MessageQueue } from "./message-queue.js";

/**
 * Error thrown when a filter denies a message
 */
export class FilterDeniedError extends Error {
  constructor(public reason: string) {
    super(`Message denied by filter: ${reason}`);
    this.name = "FilterDeniedError";
  }
}

/**
 * Configuration for GopherFilteredTransport
 */
export interface GopherFilteredTransportConfig {
  /** Canonical filter chain configuration (address not required for hybrid SDK mode) */
  filterConfig: CanonicalConfig;

  /** Dispatcher handle from createRealDispatcher() */
  dispatcherHandle: number;

  /** Maximum queue size for delayed messages (default: 1000) */
  queueSize?: number;

  /** Callback for validation warnings */
  onValidationWarning?: (warnings: string[]) => void;

  /** Whether to log filter decisions (default: false) */
  debugLogging?: boolean;
}

/**
 * Transport wrapper that adds Gopher C++ filters to any SDK transport
 *
 * This class implements the MCP SDK Transport interface while intercepting
 * all incoming and outgoing messages to apply C++ filter chains. Filters
 * can allow, deny, delay, queue, or transform messages.
 *
 * ## Features
 * - **Transparent Wrapping**: Works with any SDK transport (stdio, HTTP, SSE)
 * - **Filter Decisions**: Handles ALLOW, DENY, DELAY, QUEUE, TRANSFORM
 * - **Fail-Open**: On filter errors, allows messages through for safety
 * - **Dynamic Config**: Enable/disable filters, reload config at runtime
 * - **Metrics**: Expose filter statistics and performance data
 * - **Backpressure**: Queue messages when filters indicate backpressure
 *
 * ## Filter Decisions
 * - **ALLOW**: Pass message through unchanged (or with transformations)
 * - **DENY**: Block message and throw FilterDeniedError
 * - **DELAY**: Hold message for specified milliseconds then retry
 * - **QUEUE**: Add message to queue for later processing
 * - **TRANSFORM**: Pass through with modified message content
 *
 * ## Lifecycle
 * 1. Create: `new GopherFilteredTransport(baseTransport, config)`
 * 2. Start: `await transport.start()` - initializes filter chain
 * 3. Process: Messages automatically filtered on send/receive
 * 4. Close: `await transport.close()` - shuts down filters and transport
 */
export class GopherFilteredTransport implements Transport {
  private readonly sdkTransport: Transport;
  private readonly filterChain: FilterChain;
  private readonly messageQueue: MessageQueue;
  private readonly config: GopherFilteredTransportConfig;
  private connected: boolean = false;

  // Transport event handlers (from SDK interface)
  onclose?: () => void;
  onerror?: (error: Error) => void;
  onmessage?: (message: JSONRPCMessage, extra?: MessageExtraInfo) => void;

  /**
   * Create a new GopherFilteredTransport
   *
   * @param sdkTransport - Base SDK transport to wrap (e.g., StdioServerTransport)
   * @param config - Filter configuration and options
   * @throws Error if dispatcher handle is invalid or filter chain creation fails
   */
  constructor(
    sdkTransport: Transport,
    config: GopherFilteredTransportConfig
  ) {
    this.sdkTransport = sdkTransport;
    this.config = {
      queueSize: 1000,
      debugLogging: false,
      ...config
    };

    // Create filter chain (this validates config and creates C++ chain)
    this.filterChain = new FilterChain(
      config.dispatcherHandle,
      config.filterConfig
    );

    // Create message queue for delayed/queued messages
    this.messageQueue = new MessageQueue(this.config.queueSize || 1000);

    // Wrap SDK transport callbacks
    this.wrapTransportCallbacks();

    if (this.config.debugLogging) {
      console.log("✅ GopherFilteredTransport created");
    }
  }

  /**
   * Wrap SDK transport callbacks to intercept messages
   *
   * This sets up interception on the base transport so we can filter
   * incoming messages before they reach the application.
   */
  private wrapTransportCallbacks(): void {
    // Intercept incoming messages
    this.sdkTransport.onmessage = async (message, extra) => {
      try {
        if (this.config.debugLogging) {
          console.log("📥 Incoming message:", this.getMessageInfo(message));
        }

        // Process through filter chain - pass message object directly
        // FilterChain will handle JSON serialization internally
        const result = await this.filterChain.processIncoming(message);

        if (this.config.debugLogging) {
          console.log(`🔍 Filter decision: ${FilterDecision[result.decision]}`);
        }

        switch (result.decision) {
          case FilterDecision.ALLOW:
          case FilterDecision.TRANSFORM:
            // Use transformed message if available, otherwise original
            const finalMessage = result.transformedMessage
              ? JSON.parse(result.transformedMessage)
              : message;

            if (this.onmessage) {
              this.onmessage(finalMessage, extra);
            }
            break;

          case FilterDecision.DENY:
            if (this.config.debugLogging) {
              console.warn(`❌ Incoming message denied: ${result.reason}`);
            }
            // Don't propagate denied messages
            break;

          case FilterDecision.DELAY:
            // Delay and then deliver
            setTimeout(() => {
              if (this.onmessage) {
                this.onmessage(message, extra);
              }
            }, result.delayMs || 0);
            break;

          case FilterDecision.QUEUE:
            // Queue for later processing
            this.messageQueue.enqueue(message);
            if (this.config.debugLogging) {
              console.log(`📦 Message queued (queue size: ${this.messageQueue.size()})`);
            }
            break;
        }
      } catch (error) {
        console.error("❌ Filter processing error (fail-open):", error);
        // Fail-open: allow message through on error
        if (this.onmessage) {
          this.onmessage(message, extra);
        }
      }
    };

    // Propagate close events
    this.sdkTransport.onclose = () => {
      this.connected = false;
      if (this.config.debugLogging) {
        console.log("🔌 Transport closed");
      }
      if (this.onclose) {
        this.onclose();
      }
    };

    // Propagate error events
    this.sdkTransport.onerror = (error) => {
      if (this.config.debugLogging) {
        console.error("❌ Transport error:", error);
      }
      if (this.onerror) {
        this.onerror(error);
      }
    };
  }

  /**
   * Start the transport and initialize filters
   *
   * This must be called before sending/receiving messages. It initializes
   * the filter chain and starts the underlying SDK transport.
   */
  async start(): Promise<void> {
    if (this.connected) {
      console.warn("Transport already connected");
      return;
    }

    try {
      if (this.config.debugLogging) {
        console.log("🚀 Starting GopherFilteredTransport");
      }

      // Initialize filter chain
      await this.filterChain.initialize();

      if (this.config.debugLogging) {
        console.log("✅ Filter chain initialized");
      }

      // Start underlying transport if it has start method
      if ('start' in this.sdkTransport && typeof this.sdkTransport.start === 'function') {
        await (this.sdkTransport as any).start();
      }

      this.connected = true;

      if (this.config.debugLogging) {
        console.log("✅ Transport started");
      }

      // Process any queued messages
      await this.processQueue();
    } catch (error) {
      console.error("❌ Failed to start transport:", error);
      throw error;
    }
  }

  /**
   * Send a message through the transport with filter processing
   *
   * Messages are filtered before being sent to the underlying transport.
   * Filter decisions can block, delay, queue, or transform messages.
   *
   * @param message - JSON-RPC message to send
   * @throws FilterDeniedError if filter denies the message
   * @throws Error if transport is not connected or send fails
   */
  async send(message: JSONRPCMessage): Promise<void> {
    if (!this.connected) {
      throw new Error("Transport not connected - call start() first");
    }

    try {
      if (this.config.debugLogging) {
        console.log("📤 Sending message:", this.getMessageInfo(message));
      }

      // Apply outgoing filters - pass message object directly
      // FilterChain will handle JSON serialization internally
      const result = await this.filterChain.processOutgoing(message);

      if (this.config.debugLogging) {
        console.log(`🔍 Filter decision: ${FilterDecision[result.decision]}`);
      }

      switch (result.decision) {
        case FilterDecision.ALLOW:
        case FilterDecision.TRANSFORM:
          // Use transformed message if available, otherwise original
          const finalMessage = result.transformedMessage
            ? JSON.parse(result.transformedMessage)
            : message;

          return await this.sdkTransport.send(finalMessage);

        case FilterDecision.DENY:
          throw new FilterDeniedError(result.reason || "Unknown reason");

        case FilterDecision.DELAY:
          // Delay and then retry send
          await this.delay(result.delayMs || 0);
          return await this.send(message);

        case FilterDecision.QUEUE:
          // Queue message for later
          this.messageQueue.enqueue(message);
          if (this.config.debugLogging) {
            console.log(`📦 Message queued (queue size: ${this.messageQueue.size()})`);
          }
          return;
      }
    } catch (error) {
      if (error instanceof FilterDeniedError) {
        throw error;
      }

      // Log and re-throw other errors
      console.error("❌ Error sending message:", error);
      throw error;
    }
  }

  /**
   * Close the transport and clean up resources
   *
   * This shuts down the filter chain, clears the message queue,
   * and closes the underlying SDK transport.
   */
  async close(): Promise<void> {
    if (!this.connected) {
      if (this.config.debugLogging) {
        console.warn("Transport already closed");
      }
      return;
    }

    try {
      if (this.config.debugLogging) {
        console.log("🔌 Closing GopherFilteredTransport");
      }

      this.connected = false;

      // Shutdown filter chain
      await this.filterChain.shutdown();
      this.filterChain.destroy();

      if (this.config.debugLogging) {
        console.log("✅ Filter chain shut down");
      }

      // Clear message queue
      this.messageQueue.clear();

      // Close underlying transport
      await this.sdkTransport.close();

      if (this.config.debugLogging) {
        console.log("✅ Transport closed");
      }
    } catch (error) {
      console.error("❌ Error closing transport:", error);
      throw error;
    }
  }

  /**
   * Handle HTTP request (for StreamableHTTPServerTransport compatibility)
   *
   * This method proxies HTTP requests to the underlying StreamableHTTPServerTransport
   * when wrapping HTTP-based transports. Messages flowing through this method are
   * automatically filtered via the onmessage/send wrappers.
   *
   * @param req - HTTP IncomingMessage
   * @param res - HTTP ServerResponse
   * @throws Error if underlying transport doesn't support handleRequest
   */
  async handleRequest(req: any, res: any): Promise<void> {
    // Check if the underlying transport has handleRequest method
    if ('handleRequest' in this.sdkTransport && typeof (this.sdkTransport as any).handleRequest === 'function') {
      if (this.config.debugLogging) {
        console.log("🌐 [GopherFilteredTransport] Proxying handleRequest to wrapped transport");
      }

      // Proxy to underlying transport - filters are already applied via onmessage/send wrappers
      return await (this.sdkTransport as any).handleRequest(req, res);
    } else {
      throw new Error('Underlying transport does not support handleRequest method');
    }
  }

  /**
   * Get metrics from filter chain
   *
   * @param filterName - Optional filter name to get specific filter metrics
   * @returns Filter metrics object
   */
  async getMetrics(filterName?: string): Promise<FilterMetrics> {
    return await this.filterChain.getMetrics(filterName);
  }

  /**
   * Export the active canonical filter configuration
   *
   * @returns Current filter chain configuration
   */
  async exportFilterConfig(): Promise<CanonicalConfig> {
    return await this.filterChain.exportConfig();
  }

  /**
   * Replace the active configuration with a new canonical config
   *
   * Note: Currently not fully implemented - requires chain rebuild.
   * Use enableFilter/disableFilter for runtime changes instead.
   *
   * @param config - New canonical configuration
   * @throws Error indicating operation not supported
   */
  async reloadFilterConfig(config: CanonicalConfig): Promise<void> {
    const warnings = await this.filterChain.reconfigure(config);
    if (warnings?.length && this.config.onValidationWarning) {
      this.config.onValidationWarning(warnings);
    }
  }

  /**
   * Append a filter to an existing chain
   *
   * Note: Currently not fully implemented - requires chain rebuild.
   * Create a new GopherFilteredTransport with updated config instead.
   *
   * @param chainName - Name of the filter chain to add to
   * @param filter - Filter specification to add
   * @throws Error indicating operation not supported
   */
  async addFilter(
    chainName: string,
    filter: CanonicalConfig["listeners"][number]["filter_chains"][number]["filters"][number]
  ): Promise<void> {
    const warnings = await this.filterChain.addFilter(chainName, filter);
    if (warnings?.length && this.config.onValidationWarning) {
      this.config.onValidationWarning(warnings);
    }
  }

  /**
   * Enable or disable a filter at runtime
   *
   * This allows toggling filters on/off without recreating the chain.
   *
   * @param filterName - Name of the filter to enable/disable
   * @param enabled - true to enable, false to disable
   */
  async setFilterEnabled(filterName: string, enabled: boolean): Promise<void> {
    if (this.config.debugLogging) {
      console.log(`🔧 ${enabled ? "Enabling" : "Disabling"} filter: ${filterName}`);
    }

    if (enabled) {
      const warnings = await this.filterChain.enableFilter(filterName);
      if (warnings?.length && this.config.onValidationWarning) {
        this.config.onValidationWarning(warnings);
      }
    } else {
      const warnings = await this.filterChain.disableFilter(filterName);
      if (warnings?.length && this.config.onValidationWarning) {
        this.config.onValidationWarning(warnings);
      }
    }
  }

  /**
   * Get current queue statistics
   *
   * @returns Queue size and capacity info
   */
  getQueueStats(): { size: number; capacity: number; isFull: boolean; oldestAge: number } {
    return {
      size: this.messageQueue.size(),
      capacity: this.messageQueue.capacity(),
      isFull: this.messageQueue.isFull(),
      oldestAge: this.messageQueue.oldestMessageAge()
    };
  }

  /**
   * Check if transport is connected
   *
   * @returns true if transport has been started and not closed
   */
  isConnected(): boolean {
    return this.connected;
  }

  /**
   * Process queued messages
   *
   * Attempts to send all queued messages through the filter chain.
   * Called automatically after start().
   */
  private async processQueue(): Promise<void> {
    while (this.messageQueue.hasMessages()) {
      const message = this.messageQueue.dequeue();
      if (message) {
        try {
          await this.send(message);
        } catch (error) {
          console.error("❌ Error processing queued message:", error);
          // Continue processing other messages even if one fails
        }
      }
    }
  }

  /**
   * Delay helper
   *
   * @param ms - Milliseconds to delay
   */
  private delay(ms: number): Promise<void> {
    return new Promise(resolve => setTimeout(resolve, ms));
  }

  /**
   * Get human-readable message info for logging
   *
   * @param message - JSON-RPC message
   * @returns String describing the message
   */
  private getMessageInfo(message: JSONRPCMessage): string {
    if ("method" in message) {
      const id = "id" in message ? message.id : "N/A";
      return `${message.method} (id: ${id})`;
    }
    return "response/notification";
  }
}
