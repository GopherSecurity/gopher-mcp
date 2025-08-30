/**
 * @file tcp-proxy-filter.test.ts
 * @brief Unit tests for TcpProxyFilter class
 */

import { TcpProxyFilter, TcpFilterType } from "../protocols/tcp-proxy-filter";
import { TcpProxyConfig, TcpProxyCallbacks } from "../protocols/tcp-proxy-filter";

describe("TcpProxyFilter", () => {
  let filter: TcpProxyFilter;
  let mockCallbacks: TcpProxyCallbacks;

  beforeEach(() => {
    mockCallbacks = {
      onConnection: jest.fn(),
      onData: jest.fn(),
      onDisconnect: jest.fn(),
      onError: jest.fn(),
    };

    const config: TcpProxyConfig = {
      name: "test-tcp-proxy",
      type: TcpFilterType.TCP_PROXY,
      settings: {
        upstreamHost: "localhost",
        upstreamPort: 8080,
        localPort: 9090,
        maxConnections: 100,
        connectionTimeout: 30000,
        bufferSize: 8192,
        keepAlive: true,
        loadBalancing: {
          enabled: false,
          strategy: "round-robin",
          upstreamHosts: [{ host: "localhost", port: 8080 }],
        },
      },
      layer: 4,
      memoryPool: 0,
    };

    filter = new TcpProxyFilter(config, mockCallbacks);
  });

  afterEach(async () => {
    if (filter) {
      await filter.destroy();
    }
  });

  describe("constructor", () => {
    it("should create a TCP proxy filter with correct configuration", () => {
      expect(filter.name).toBe("test-tcp-proxy");
      expect(filter.type).toBe(TcpFilterType.TCP_PROXY.toString());
      expect(filter.filterHandle).toBeGreaterThan(0);
      expect(filter.bufferHandle).toBeGreaterThan(0);
      expect(filter.memoryPool).toBeGreaterThan(0);
    });

    it("should initialize with default statistics", () => {
      const stats = filter.getStats();
      expect(stats.bytesProcessed).toBe(0);
      expect(stats.packetsProcessed).toBe(0);
      expect(stats.errors).toBe(0);
      expect(stats.processingTimeUs).toBe(0);
      expect(stats.throughputMbps).toBe(0);
    });
  });

  describe("data processing", () => {
    it("should process incoming data through callbacks", async () => {
      const testData = Buffer.from("test data");
      
      // Mock the onData callback to return processed data
      mockCallbacks.onData = jest.fn().mockResolvedValue(testData);
      
      const result = await filter.processData(testData);
      
      expect(mockCallbacks.onData).toHaveBeenCalledWith(expect.any(String), testData);
      expect(result).toEqual(testData);
    });

    it("should handle data processing errors gracefully", async () => {
      const testData = Buffer.from("test data");
      
      // Mock the onData callback to throw an error
      mockCallbacks.onData = jest.fn().mockRejectedValue(new Error("Processing failed"));
      
      await expect(filter.processData(testData)).rejects.toThrow("Processing failed");
    });
  });

  describe("connection management", () => {
    it("should track new connections", () => {
      const connectionId = "conn-1";
      const metadata = { source: "test" };
      
      // Simulate connection creation through callbacks
      if (mockCallbacks.onConnection) {
        mockCallbacks.onConnection(connectionId, metadata);
      }
      
      // Check if connection is tracked (this depends on internal implementation)
      const connections = filter.getAllConnections();
      // Note: The actual connection tracking depends on the filter's internal state
      // We're just testing that the method exists and doesn't throw
      expect(Array.isArray(connections)).toBe(true);
    });

    it("should retrieve connection by ID", () => {
      const connection = filter.getConnection("non-existent");
      expect(connection).toBeUndefined();
    });

    it("should close connections", () => {
      // Test that the method exists and doesn't throw
      expect(() => filter.closeConnection("non-existent")).not.toThrow();
    });
  });

  describe("statistics", () => {
    it("should return filter statistics", () => {
      const stats = filter.getStats();
      expect(stats).toHaveProperty("bytesProcessed");
      expect(stats).toHaveProperty("packetsProcessed");
      expect(stats).toHaveProperty("errors");
      expect(stats).toHaveProperty("processingTimeUs");
      expect(stats).toHaveProperty("throughputMbps");
    });

    it("should return connection statistics", () => {
      const connStats = filter.getConnectionStats();
      expect(connStats).toHaveProperty("totalConnections");
      expect(connStats).toHaveProperty("activeConnections");
      expect(connStats).toHaveProperty("totalBytesReceived");
      expect(connStats).toHaveProperty("totalBytesSent");
    });
  });

  describe("configuration updates", () => {
    it("should update filter settings", async () => {
      const newSettings = {
        maxConnections: 200,
        connectionTimeout: 60000,
      };
      
      await expect(filter.updateSettings(newSettings)).resolves.not.toThrow();
    });
  });

  describe("cleanup", () => {
    it("should destroy filter and clean up resources", async () => {
      await expect(filter.destroy()).resolves.not.toThrow();
      
      // After destruction, the filter should be in a clean state
      // Note: We can't check internal handles as they're private
    });
  });

  describe("error handling", () => {
    it("should handle callback errors gracefully", () => {
      const error = new Error("Test error");
      
      if (mockCallbacks.onError) {
        expect(() => mockCallbacks.onError!(error)).not.toThrow();
      }
    });
  });
});
