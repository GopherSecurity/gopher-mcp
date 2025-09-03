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
  private server: any = null; // For server mode
  private client: any = null; // For client mode
  private connections: Map<string, any> = new Map(); // Active connections
  private messageBuffers: Map<string, string> = new Map(); // Message buffers for each connection

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

      // Start transport based on protocol
      await this.startTransport();

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
      console.log(
        `✅ Message ready for transport: ${JSON.stringify(
          processedMessage,
          null,
          2
        )}`
      );

      // Send the processed message through the actual transport
      await this.sendThroughTransport(processedMessage);
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

      // Close transport connections
      if (this.client) {
        this.client.destroy();
        this.client = null;
      }

      if (this.server) {
        this.server.close();
        this.server = null;
      }

      // Close all active connections
      for (const [connectionId, connection] of this.connections) {
        if (connection.destroy) {
          connection.destroy();
        }
      }
      this.connections.clear();
      this.messageBuffers.clear();

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
      connections: this.connections.size,
    };
  }

  /**
   * Start the actual transport layer based on protocol
   */
  private async startTransport(): Promise<void> {
    switch (this.config.protocol) {
      case "stdio":
        await this.startStdioTransport();
        break;
      case "tcp":
        await this.startTcpTransport();
        break;
      case "udp":
        await this.startUdpTransport();
        break;
      default:
        throw new Error(`Unsupported protocol: ${this.config.protocol}`);
    }
  }

  /**
   * Start stdio transport (for direct communication)
   */
  private async startStdioTransport(): Promise<void> {
    console.log("📡 Starting stdio transport");

    // For stdio, we'll use process.stdin/stdout for real communication
    process.stdin.setEncoding("utf8");

    let buffer = "";

    process.stdin.on("data", async (chunk: string) => {
      buffer += chunk;

      // Process complete messages (delimited by newlines)
      const lines = buffer.split("\n");
      buffer = lines.pop() || ""; // Keep incomplete line in buffer

      for (const line of lines) {
        if (line.trim()) {
          try {
            const message = JSON.parse(line.trim());
            await this.processReceivedMessage(message);
          } catch (error) {
            console.error("❌ Error processing stdio message:", error);
          }
        }
      }
    });

    process.stdin.on("end", () => {
      console.log("📡 Stdio input ended");
      if (this.onclose) {
        this.onclose();
      }
    });

    console.log("✅ Stdio transport ready for input");
  }

  /**
   * Start TCP transport
   */
  private async startTcpTransport(): Promise<void> {
    console.log(
      `📡 Starting TCP transport on ${this.config.host}:${this.config.port}`
    );

    // Import net module for TCP
    const net = await import("net");

    if (this.config.host && this.config.port) {
      // Client mode - connect to server
      this.client = new net.Socket();

      this.client.on("connect", () => {
        console.log("🔗 Connected to TCP server");
      });

      this.client.on("data", async (data: Buffer) => {
        await this.processTcpData(data, "client");
      });

      this.client.on("close", () => {
        console.log("🔌 TCP connection closed");
        if (this.onclose) {
          this.onclose();
        }
      });

      this.client.on("error", (error: Error) => {
        console.error("❌ TCP client error:", error);
        if (this.onerror) {
          this.onerror(error);
        }
      });

      // Connect to server
      await new Promise<void>((resolve, reject) => {
        this.client.connect(
          this.config.port!,
          this.config.host!,
          (error?: Error) => {
            if (error) {
              reject(error);
            } else {
              resolve();
            }
          }
        );
      });
    } else {
      // Server mode - listen for connections
      this.server = net.createServer((socket) => {
        const connectionId = `${socket.remoteAddress}:${socket.remotePort}`;
        console.log(`🔗 New TCP connection: ${connectionId}`);

        this.connections.set(connectionId, socket);

        socket.on("data", async (data: Buffer) => {
          await this.processTcpData(data, connectionId);
        });

        socket.on("close", () => {
          console.log(`🔌 TCP connection closed: ${connectionId}`);
          this.connections.delete(connectionId);
          this.messageBuffers.delete(connectionId);
        });

        socket.on("error", (error: Error) => {
          console.error(`❌ TCP connection error (${connectionId}):`, error);
          this.connections.delete(connectionId);
          this.messageBuffers.delete(connectionId);
        });
      });

      const port = this.config.port || 8080;
      await new Promise<void>((resolve, reject) => {
        this.server.listen(port, (error?: Error) => {
          if (error) {
            reject(error);
          } else {
            console.log(`🚀 TCP server listening on port ${port}`);
            resolve();
          }
        });
      });
    }
  }

  /**
   * Start UDP transport
   */
  private async startUdpTransport(): Promise<void> {
    console.log(
      `📡 Starting UDP transport on ${this.config.host}:${this.config.port}`
    );

    // Import dgram module for UDP
    const dgram = await import("dgram");

    if (this.config.host && this.config.port) {
      // Client mode
      this.client = dgram.createSocket("udp4");

      this.client.on("message", async (msg: Buffer, rinfo: any) => {
        try {
          const message = JSON.parse(msg.toString());
          await this.processReceivedMessage(message);
        } catch (error) {
          console.error("❌ Error processing received UDP data:", error);
        }
      });

      this.client.on("error", (error: Error) => {
        console.error("❌ UDP client error:", error);
        if (this.onerror) {
          this.onerror(error);
        }
      });
    } else {
      // Server mode
      this.server = dgram.createSocket("udp4");

      this.server.on("message", async (msg: Buffer, rinfo: any) => {
        try {
          const message = JSON.parse(msg.toString());
          const connectionId = `${rinfo.address}:${rinfo.port}`;
          this.connections.set(connectionId, rinfo);
          await this.processReceivedMessage(message);
        } catch (error) {
          console.error("❌ Error processing received UDP data:", error);
        }
      });

      this.server.on("error", (error: Error) => {
        console.error("❌ UDP server error:", error);
        if (this.onerror) {
          this.onerror(error);
        }
      });

      const port = this.config.port || 8080;
      this.server.bind(port, () => {
        console.log(`🚀 UDP server listening on port ${port}`);
      });
    }
  }

  /**
   * Process TCP data with proper message delimiters
   */
  private async processTcpData(
    data: Buffer,
    connectionId: string
  ): Promise<void> {
    // Get or create buffer for this connection
    let buffer = this.messageBuffers.get(connectionId) || "";
    buffer += data.toString();

    // Process complete messages (delimited by newlines)
    const lines = buffer.split("\n");
    buffer = lines.pop() || ""; // Keep incomplete line in buffer
    this.messageBuffers.set(connectionId, buffer);

    for (const line of lines) {
      if (line.trim()) {
        try {
          const message = JSON.parse(line.trim());
          await this.processReceivedMessage(message);
        } catch (error) {
          console.error(
            `❌ Error processing TCP message from ${connectionId}:`,
            error
          );
        }
      }
    }
  }

  /**
   * Send message through the actual transport
   */
  private async sendThroughTransport(message: JSONRPCMessage): Promise<void> {
    const messageData = JSON.stringify(message) + "\n"; // Add newline delimiter

    switch (this.config.protocol) {
      case "stdio":
        // For stdio, write to stdout
        process.stdout.write(messageData);
        break;

      case "tcp":
        if (this.client) {
          // Client mode - send to server
          this.client.write(messageData);
        } else if (this.server) {
          // Server mode - broadcast to all connections
          for (const [connectionId, socket] of this.connections) {
            socket.write(messageData);
          }
        }
        break;

      case "udp":
        if (this.client) {
          // Client mode - send to server
          this.client.send(messageData, this.config.port!, this.config.host!);
        } else if (this.server) {
          // Server mode - broadcast to all connections
          for (const [connectionId, rinfo] of this.connections) {
            this.server.send(messageData, rinfo.port, rinfo.address);
          }
        }
        break;

      default:
        throw new Error(
          `Unsupported protocol for sending: ${this.config.protocol}`
        );
    }
  }
}
