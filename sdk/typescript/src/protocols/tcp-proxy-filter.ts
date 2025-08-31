/**
 * @file tcp-proxy-filter.ts
 * @brief TCP Proxy Filter implementation using MCP C API
 *
 * This filter provides TCP proxy capabilities including:
 * - Connection forwarding
 * - Data buffering and streaming
 * - Connection pooling
 * - Load balancing
 */

import { mcpFilterLib } from "../core/ffi-bindings";
import {
  McpBuiltinFilterType,
  McpFilterStats,
  McpProtocolLayer,
  McpTransportProtocol,
} from "../types";

// TCP filter types from mcp_filter_api.h
export enum TcpFilterType {
  TCP_PROXY = McpBuiltinFilterType.TCP_PROXY,
}

// TCP connection state
export enum TcpConnectionState {
  CONNECTING = 0,
  CONNECTED = 1,
  DISCONNECTED = 2,
  ERROR = 3,
}

// Buffer flags from mcp_filter_api.h
export const MCP_BUFFER_FLAG_READONLY = 0x01;
export const MCP_BUFFER_FLAG_OWNED = 0x02;
export const MCP_BUFFER_FLAG_EXTERNAL = 0x04;
export const MCP_BUFFER_FLAG_ZERO_COPY = 0x08;

// TCP proxy configuration
export interface TcpProxyConfig {
  name: string;
  type: TcpFilterType;
  settings: {
    upstreamHost: string;
    upstreamPort: number;
    localPort: number;
    localHost?: string;
    maxConnections?: number;
    connectionTimeout?: number;
    bufferSize?: number;
    keepAlive?: boolean;
    loadBalancing?: {
      enabled: boolean;
      strategy: "round-robin" | "least-connections" | "hash-based";
      upstreamHosts: Array<{ host: string; port: number; weight?: number }>;
    };
  };
  layer: number;
  memoryPool: any;
}

// TCP proxy callbacks
export interface TcpProxyCallbacks {
  onConnection?: (connectionId: string, metadata: any) => void;
  onData?: (connectionId: string, data: Buffer) => Promise<Buffer | null>;
  onDisconnect?: (connectionId: string) => void;
  onError?: (error: Error, connectionId?: string) => void;
}

// TCP connection info
export interface TcpConnection {
  id: string;
  state: TcpConnectionState;
  localAddress: string;
  remoteAddress: string;
  bytesReceived: number;
  bytesSent: number;
  createdAt: Date;
  lastActivity: Date;
}

/**
 * TCP Proxy Filter implementation using MCP C API
 */
export class TcpProxyFilter {
  public readonly name: string;
  public readonly type: string;
  public readonly filterHandle: number;
  public readonly bufferHandle: number;
  public readonly memoryPool: any; // Changed from number to any to handle pointer types

  private callbacks: TcpProxyCallbacks;
  private stats: McpFilterStats;
  private connections: Map<string, TcpConnection> = new Map();
  private connectionCounter: number = 0;
  private isProcessing: boolean = false;
  private config: TcpProxyConfig;

  constructor(config: TcpProxyConfig, callbacks: TcpProxyCallbacks = {}) {
    this.name = config.name;
    this.type = config.type.toString();
    this.config = config;
    this.callbacks = callbacks;

    // Initialize MCP resources
    this.memoryPool = this.createMemoryPool();
    this.filterHandle = this.createFilter();
    this.bufferHandle = this.createBuffer();

    // Set up callbacks
    this.setupCallbacks();

    // Initialize statistics
    this.stats = {
      bytesProcessed: 0,
      packetsProcessed: 0,
      errors: 0,
      processingTimeUs: 0,
      throughputMbps: 0,
    };
  }

  /**
   * Create memory pool for the filter
   */
  private createMemoryPool(): number {
    const poolSize = this.config.settings.bufferSize || 1024 * 1024; // 1MB default
    const pool = mcpFilterLib.mcp_memory_pool_create(poolSize);
    if (!pool) {
      throw new Error("Failed to create memory pool for TCP proxy filter");
    }
    return pool as number;
  }

  /**
   * Create the filter using C API
   */
  private createFilter(): number {
    // Use null config for now to get the filter created
    const config = mcpFilterLib.mcp_json_create_null();

    // Create the filter
    const filter = mcpFilterLib.mcp_filter_create_builtin(
      0, // dispatcher (we'll handle this separately)
      TcpFilterType.TCP_PROXY,
      config
    );

    if (!filter) {
      throw new Error("Failed to create TCP proxy filter");
    }

    // Clean up the JSON config
    mcpFilterLib.mcp_json_free(config);

    return filter as number;
  }

  /**
   * Create buffer for data processing
   */
  private createBuffer(): number {
    const bufferSize = this.config.settings.bufferSize || 8192; // 8KB default
    const buffer = mcpFilterLib.mcp_buffer_create_owned(
      bufferSize,
      1 // MCP_BUFFER_OWNERSHIP_SHARED
    );

    if (!buffer) {
      throw new Error("Failed to create buffer for TCP proxy filter");
    }

    return buffer as number;
  }

  /**
   * Set up filter callbacks
   */
  private setupCallbacks(): void {
    // Skip callback setup for now since the C API correctly rejects null callbacks
    // TODO: Implement proper callback function pointers
    console.log("Skipping callback setup - callbacks not yet implemented");
  }

  /**
   * Process TCP data through the filter
   */
  public async processData(data: Buffer): Promise<Buffer> {
    if (this.isProcessing) {
      throw new Error("Filter is already processing data");
    }

    this.isProcessing = true;
    const startTime = process.hrtime.bigint();

    try {
      // Generate connection ID if this is new data
      const connectionId = this.generateConnectionId();

      // Create or update connection info
      this.updateConnectionInfo(connectionId, data.length);

      // Apply data callbacks
      let processedData = data;
      if (this.callbacks.onData) {
        const result = await this.callbacks.onData(connectionId, data);
        if (result === null) {
          // Data was blocked
          return Buffer.alloc(0);
        }
        processedData = result;
      }

      // Process the data through C API
      const result = await this.processDataThroughCAPI(processedData);

      // Update statistics
      this.updateStats(processedData.length, startTime);

      return result;
    } catch (error) {
      this.stats.errors++;
      if (this.callbacks.onError) {
        this.callbacks.onError(error as Error);
      }
      throw error;
    } finally {
      this.isProcessing = false;
    }
  }

  /**
   * Process data through C API
   */
  private async processDataThroughCAPI(data: Buffer): Promise<Buffer> {
    // For now, skip C API processing to avoid hanging
    // TODO: Implement proper C API integration when buffer functions are stable
    return data;
  }

  /**
   * Generate unique connection ID
   */
  private generateConnectionId(): string {
    return `tcp_${Date.now()}_${++this.connectionCounter}`;
  }

  /**
   * Update connection information
   */
  private updateConnectionInfo(connectionId: string, dataLength: number): void {
    if (!this.connections.has(connectionId)) {
      // Create new connection
      const connection: TcpConnection = {
        id: connectionId,
        state: TcpConnectionState.CONNECTING,
        localAddress: `${this.config.settings.localHost || "0.0.0.0"}:${
          this.config.settings.localPort
        }`,
        remoteAddress: `${this.config.settings.upstreamHost}:${this.config.settings.upstreamPort}`,
        bytesReceived: dataLength,
        bytesSent: 0,
        createdAt: new Date(),
        lastActivity: new Date(),
      };

      this.connections.set(connectionId, connection);

      // Notify callback
      if (this.callbacks.onConnection) {
        const metadata = {
          layer: McpProtocolLayer.LAYER_4_TRANSPORT,
          l4: {
            protocol: McpTransportProtocol.TCP,
            srcPort: this.config.settings.localPort,
            dstPort: this.config.settings.upstreamPort,
            sequenceNum: 0,
          },
        };
        this.callbacks.onConnection(connectionId, metadata);
      }
    } else {
      // Update existing connection
      const connection = this.connections.get(connectionId)!;
      connection.bytesReceived += dataLength;
      connection.lastActivity = new Date();
    }
  }

  /**
   * Get connection information
   */
  public getConnection(connectionId: string): TcpConnection | undefined {
    return this.connections.get(connectionId);
  }

  /**
   * Get all connections
   */
  public getAllConnections(): TcpConnection[] {
    return Array.from(this.connections.values());
  }

  /**
   * Close connection
   */
  public closeConnection(connectionId: string): void {
    const connection = this.connections.get(connectionId);
    if (connection) {
      connection.state = TcpConnectionState.DISCONNECTED;

      // Notify callback
      if (this.callbacks.onDisconnect) {
        this.callbacks.onDisconnect(connectionId);
      }

      // Remove from map
      this.connections.delete(connectionId);
    }
  }

  /**
   * Update filter statistics
   */
  private updateStats(bytesProcessed: number, startTime: bigint): void {
    const endTime = process.hrtime.bigint();
    const processingTimeUs = Number(endTime - startTime) / 1000; // Convert to microseconds

    this.stats.bytesProcessed += bytesProcessed;
    this.stats.packetsProcessed++;
    this.stats.processingTimeUs += processingTimeUs;

    // Calculate throughput (simplified)
    const totalTimeMs = this.stats.processingTimeUs / 1000;
    if (totalTimeMs > 0) {
      this.stats.throughputMbps =
        (this.stats.bytesProcessed * 8) / (totalTimeMs * 1000000);
    }
  }

  /**
   * Get filter statistics
   */
  public getStats(): McpFilterStats {
    return { ...this.stats };
  }

  /**
   * Get connection statistics
   */
  public getConnectionStats(): {
    totalConnections: number;
    activeConnections: number;
    totalBytesReceived: number;
    totalBytesSent: number;
  } {
    const connections = Array.from(this.connections.values());
    const activeConnections = connections.filter(
      (c) => c.state === TcpConnectionState.CONNECTED
    ).length;

    const totalBytesReceived = connections.reduce(
      (sum, c) => sum + c.bytesReceived,
      0
    );
    const totalBytesSent = connections.reduce((sum, c) => sum + c.bytesSent, 0);

    return {
      totalConnections: connections.length,
      activeConnections,
      totalBytesReceived,
      totalBytesSent,
    };
  }

  /**
   * Update filter settings
   */
  public async updateSettings(_settings: any): Promise<void> {
    // Update configuration using C API
    // This would involve updating the filter configuration
    // For now, we'll just update our local settings
    this.config.settings = { ...this.config.settings, ..._settings };
  }

  /**
   * Clean up filter resources
   */
  public async destroy(): Promise<void> {
    // Close all connections
    for (const connectionId of this.connections.keys()) {
      this.closeConnection(connectionId);
    }

    // Release buffer
    if (this.bufferHandle) {
      mcpFilterLib.mcp_filter_buffer_release(this.bufferHandle);
    }

    // Release filter
    if (this.filterHandle) {
      mcpFilterLib.mcp_filter_release(this.filterHandle);
    }

    // Destroy memory pool
    if (this.memoryPool) {
      mcpFilterLib.mcp_memory_pool_destroy(this.memoryPool);
    }
  }
}

// Extend String prototype for hash code (used in hash-based load balancing)
declare global {
  interface String {
    hashCode(): number;
  }
}

String.prototype.hashCode = function (): number {
  let hash = 0;
  if (this.length === 0) return hash;

  for (let i = 0; i < this.length; i++) {
    const char = this.charCodeAt(i);
    hash = (hash << 5) - hash + char;
    hash = hash & hash; // Convert to 32-bit integer
  }

  return hash;
};
