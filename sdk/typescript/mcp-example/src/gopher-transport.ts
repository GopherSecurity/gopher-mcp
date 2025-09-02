import {
  JSONRPCMessage as FilterJSONRPCMessage,
  FilterManager,
  FilterManagerConfig,
} from "@mcp/filter-sdk";
import {
  Transport,
  TransportSendOptions,
} from "@modelcontextprotocol/sdk/shared/transport";
import {
  JSONRPCMessage,
  MessageExtraInfo,
} from "@modelcontextprotocol/sdk/types";

// Type adapter to convert between MCP SDK and FilterManager JSONRPCMessage types
type MCPJSONRPCMessage = JSONRPCMessage;
type AdapterJSONRPCMessage = FilterJSONRPCMessage;

function adaptToFilterMessage(
  mcpMessage: MCPJSONRPCMessage
): AdapterJSONRPCMessage {
  return mcpMessage as AdapterJSONRPCMessage;
}

function adaptToMCPMessage(
  filterMessage: AdapterJSONRPCMessage
): MCPJSONRPCMessage {
  return filterMessage as MCPJSONRPCMessage;
}

export interface GopherTransportConfig {
  // Filter configuration for this transport
  filters?: FilterManagerConfig;

  // Transport-specific configuration
  name?: string;
  version?: string;

  // Connection configuration
  host?: string;
  port?: number;
  protocol?: "tcp" | "udp" | "stdio";

  // Timeout configuration
  connectTimeout?: number;
  sendTimeout?: number;
  receiveTimeout?: number;
}

export class GopherTransport implements Transport {
  private filterManager: FilterManager;
  private config: GopherTransportConfig;
  private isConnected: boolean = false;
  private isDestroyed: boolean = false;

  // Transport event handlers
  onclose?: (() => void) | undefined;
  onerror?: ((error: Error) => void) | undefined;
  onmessage?:
    | ((message: JSONRPCMessage, extra?: MessageExtraInfo) => void)
    | undefined;
  sessionId?: string | undefined;
  setProtocolVersion?: ((version: string) => void) | undefined;

  constructor(config: GopherTransportConfig = {}) {
    this.config = {
      name: "gopher-transport",
      version: "1.0.0",
      protocol: "stdio",
      connectTimeout: 30000, // 30 seconds
      sendTimeout: 10000, // 10 seconds
      receiveTimeout: 30000, // 30 seconds
      ...config,
    };

    // Initialize FilterManager with transport-specific configuration
    const filterConfig: FilterManagerConfig = {
      // Default security configuration
      security: {
        authentication: {
          method: "jwt",
          secret: "gopher-transport-secret",
        },
        authorization: {
          enabled: true,
          policy: "allow",
          rules: [
            {
              resource: "*",
              action: "read",
            },
          ],
        },
      },

      // Default observability configuration
      observability: {
        accessLog: {
          enabled: true,
          format: "json",
          fields: ["timestamp", "method", "sessionId", "duration"],
          output: "console",
        },
        metrics: {
          enabled: true,
          labels: {
            transport: this.config.name || "gopher-transport",
            version: this.config.version || "1.0.0",
          },
        },
        tracing: {
          enabled: true,
          serviceName: `gopher-transport-${this.config.name || "default"}`,
          samplingRate: 0.1, // 10% sampling
        },
      },

      // Default traffic management
      trafficManagement: {
        rateLimit: {
          enabled: true,
          requestsPerMinute: 1000,
          burstSize: 50,
          keyExtractor: "custom",
        },
        circuitBreaker: {
          enabled: true,
          failureThreshold: 5,
          timeout: 30000,
          resetTimeout: 60000,
        },
        retry: {
          enabled: true,
          maxAttempts: 3,
          backoffStrategy: "exponential",
          baseDelay: 1000,
          maxDelay: 5000,
        },
      },

      // Default error handling
      errorHandling: {
        stopOnError: false,
        retryAttempts: 2,
        fallbackBehavior: "passthrough",
      },

      // Merge with user-provided configuration
      ...this.config.filters,
    };

    this.filterManager = new FilterManager(filterConfig);
    console.log(
      `🔧 GopherTransport initialized with FilterManager (${this.config.name})`
    );
  }

  async start(): Promise<void> {
    if (this.isDestroyed) {
      throw new Error("GopherTransport has been destroyed");
    }

    if (this.isConnected) {
      console.warn("GopherTransport is already connected");
      return;
    }

    try {
      console.log(`🚀 Starting GopherTransport (${this.config.protocol})`);

      // Generate session ID
      this.sessionId = this.generateSessionId();
      console.log(`📋 Session ID: ${this.sessionId}`);

      // Set protocol version
      if (this.setProtocolVersion) {
        this.setProtocolVersion("2024-11-05");
      }

      this.isConnected = true;
      console.log("✅ GopherTransport started successfully");
    } catch (error) {
      this.isConnected = false;
      const errorMsg = `Failed to start GopherTransport: ${error}`;
      console.error("❌", errorMsg);

      if (this.onerror) {
        this.onerror(new Error(errorMsg));
      }
      throw error;
    }
  }

  async send(
    message: JSONRPCMessage,
    options?: TransportSendOptions
  ): Promise<void> {
    if (this.isDestroyed) {
      throw new Error("GopherTransport has been destroyed");
    }

    if (!this.isConnected) {
      throw new Error("GopherTransport is not connected");
    }

    try {
      const messageInfo =
        "method" in message
          ? `${message.method} (id: ${"id" in message ? message.id : "N/A"})`
          : "notification";
      console.log(`📤 Sending message: ${messageInfo}`);

      // Process message through FilterManager
      const filterMessage = adaptToFilterMessage(message);
      const processedFilterMessage = await this.filterManager.process(
        filterMessage
      );
      const processedMessage = adaptToMCPMessage(processedFilterMessage);

      const processedInfo =
        "method" in processedMessage
          ? `${processedMessage.method} (id: ${
              "id" in processedMessage ? processedMessage.id : "N/A"
            })`
          : "notification";
      console.log(`✅ Message processed through filters: ${processedInfo}`);

      // FilterManager processing complete - message ready for actual transport
      console.log(`✅ Message ready for transport: ${JSON.stringify(processedMessage, null, 2)}`);
      
      // In a real implementation, you would send the processedMessage over your chosen transport
      // (TCP, WebSocket, HTTP, etc.) here
    } catch (error) {
      const errorMsg = `Failed to send message: ${error}`;
      console.error("❌", errorMsg);

      if (this.onerror) {
        this.onerror(new Error(errorMsg));
      }
      throw error;
    }
  }

  async close(): Promise<void> {
    if (this.isDestroyed) {
      console.warn("GopherTransport is already destroyed");
      return;
    }

    try {
      console.log("🔌 Closing GopherTransport connection");

      this.isConnected = false;

      // Clean up FilterManager resources
      this.filterManager.destroy();

      this.isDestroyed = true;
      console.log("✅ GopherTransport closed successfully");

      // Notify close event
      if (this.onclose) {
        this.onclose();
      }
    } catch (error) {
      console.error("❌ Error closing GopherTransport:", error);
      this.isDestroyed = true; // Mark as destroyed even if cleanup failed
      throw error;
    }
  }

  /**
   * Process a received message through FilterManager
   * In a real implementation, this would be called when receiving data from the network
   */
  async processReceivedMessage(
    message: JSONRPCMessage,
    extra?: MessageExtraInfo
  ): Promise<void> {
    if (!this.isConnected || this.isDestroyed) {
      return;
    }

    try {
      const messageInfo =
        "method" in message
          ? `${message.method} (id: ${"id" in message ? message.id : "N/A"})`
          : "notification";
      console.log(`📥 Processing received message: ${messageInfo}`);

      // Process response through FilterManager
      const filterMessage = adaptToFilterMessage(message);
      const processedFilterMessage = await this.filterManager.processResponse(
        filterMessage
      );
      const processedMessage = adaptToMCPMessage(processedFilterMessage);

      const processedInfo =
        "method" in processedMessage
          ? `${processedMessage.method} (id: ${
              "id" in processedMessage ? processedMessage.id : "N/A"
            })`
          : "notification";
      console.log(`✅ Response processed through filters: ${processedInfo}`);

      // Notify message event
      if (this.onmessage) {
        this.onmessage(processedMessage, extra);
      }
    } catch (error) {
      console.error("❌ Error processing received message:", error);

      if (this.onerror) {
        this.onerror(new Error(`Failed to process received message: ${error}`));
      }
    }
  }



  /**
   * Generate a unique session ID
   */
  private generateSessionId(): string {
    const timestamp = Date.now().toString(36);
    const random = Math.random().toString(36).substring(2);
    return `gopher-${timestamp}-${random}`;
  }

  /**
   * Get transport statistics
   */
  getStats() {
    return {
      isConnected: this.isConnected,
      isDestroyed: this.isDestroyed,
      sessionId: this.sessionId,
      config: this.config,
    };
  }
}
