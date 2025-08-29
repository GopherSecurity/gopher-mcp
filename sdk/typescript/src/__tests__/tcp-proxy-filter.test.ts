/**
 * @file tcp-proxy-filter.test.ts
 * @brief Unit tests for TcpProxyFilter class
 */

import {
  TcpConnection,
  TcpConnectionState,
  TcpFilterType,
  TcpProxyCallbacks,
  TcpProxyConfig,
  TcpProxyFilter,
} from "../protocols/tcp-proxy-filter";

describe("TcpProxyFilter", () => {
  let tcpFilter: TcpProxyFilter;
  let mockCallbacks: TcpProxyCallbacks;

  beforeEach(() => {
    mockCallbacks = {
      onConnection: jest.fn(),
      onData: jest.fn(),
      onDisconnect: jest.fn(),
      onError: jest.fn(),
      onConnect: jest.fn(),
    };

    const config: TcpProxyConfig = {
      listenPort: 8080,
      targetHost: "localhost",
      targetPort: 9090,
      maxConnections: 100,
      connectionTimeout: 30000,
      enableTls: false,
      bufferSize: 8192,
    };

    tcpFilter = new TcpProxyFilter(config);
  });

  afterEach(() => {
    if (tcpFilter) {
      tcpFilter.destroy();
    }
  });

  describe("construction and initialization", () => {
    it("should create TCP proxy filter with configuration", () => {
      expect(tcpFilter).toBeDefined();
      expect(tcpFilter.config.listenPort).toBe(8080);
      expect(tcpFilter.config.targetHost).toBe("localhost");
      expect(tcpFilter.config.targetPort).toBe(9090);
    });

    it("should create TCP proxy filter with default values", () => {
      const defaultFilter = new TcpProxyFilter({});
      expect(defaultFilter).toBeDefined();
      expect(defaultFilter.config.listenPort).toBe(80);
      expect(defaultFilter.config.targetHost).toBe("127.0.0.1");

      defaultFilter.destroy();
    });

    it("should create TCP proxy filter with custom type", () => {
      const customFilter = new TcpProxyFilter({}, TcpFilterType.LOAD_BALANCER);
      expect(customFilter.type).toBe(TcpFilterType.LOAD_BALANCER);

      customFilter.destroy();
    });
  });

  describe("callback management", () => {
    it("should set callbacks correctly", () => {
      tcpFilter.setCallbacks(mockCallbacks);

      expect(tcpFilter.callbacks).toBe(mockCallbacks);
    });

    it("should handle missing callbacks gracefully", () => {
      const filterWithoutCallbacks = new TcpProxyFilter({});
      expect(() =>
        filterWithoutCallbacks.setCallbacks(undefined)
      ).not.toThrow();

      filterWithoutCallbacks.destroy();
    });

    it("should validate callback functions", () => {
      const invalidCallbacks = {
        onConnection: "not a function" as any,
        onData: null as any,
        onDisconnect: undefined as any,
        onError: jest.fn(),
        onConnect: jest.fn(),
      };

      expect(() => tcpFilter.setCallbacks(invalidCallbacks)).not.toThrow();
    });
  });

  describe("connection management", () => {
    beforeEach(() => {
      tcpFilter.setCallbacks(mockCallbacks);
    });

    it("should create new connection", () => {
      const connection = tcpFilter.createConnection("192.168.1.1", 12345);
      expect(connection).toBeDefined();
      expect(connection.remoteAddress).toBe("192.168.1.1");
      expect(connection.remotePort).toBe(12345);
      expect(connection.state).toBe(TcpConnectionState.CONNECTING);
    });

    it("should handle multiple connections", () => {
      const connections: TcpConnection[] = [];

      for (let i = 0; i < 5; i++) {
        const conn = tcpFilter.createConnection(`192.168.1.${i}`, 12345 + i);
        connections.push(conn);
      }

      expect(connections).toHaveLength(5);
      connections.forEach((conn) => expect(conn).toBeDefined());
    });

    it("should track connection state changes", () => {
      const connection = tcpFilter.createConnection("192.168.1.1", 12345);

      expect(connection.state).toBe(TcpConnectionState.CONNECTING);

      tcpFilter.updateConnectionState(
        connection.id,
        TcpConnectionState.CONNECTED
      );
      expect(connection.state).toBe(TcpConnectionState.CONNECTED);

      tcpFilter.updateConnectionState(
        connection.id,
        TcpConnectionState.DISCONNECTED
      );
      expect(connection.state).toBe(TcpConnectionState.DISCONNECTED);
    });

    it("should close connection", () => {
      const connection = tcpFilter.createConnection("192.168.1.1", 12345);

      expect(() => tcpFilter.closeConnection(connection.id)).not.toThrow();
      expect(connection.state).toBe(TcpConnectionState.DISCONNECTED);
    });

    it("should handle connection cleanup", () => {
      const connection = tcpFilter.createConnection("192.168.1.1", 12345);
      const connectionId = connection.id;

      tcpFilter.closeConnection(connectionId);
      expect(() => tcpFilter.cleanupConnection(connectionId)).not.toThrow();
    });
  });

  describe("data processing", () => {
    let connection: TcpConnection;

    beforeEach(() => {
      tcpFilter.setCallbacks(mockCallbacks);
      connection = tcpFilter.createConnection("192.168.1.1", 12345);
    });

    it("should process incoming data", () => {
      const data = Buffer.from("Hello, TCP Proxy!");

      const result = tcpFilter.processIncomingData(connection.id, data);
      expect(result).toBeDefined();
      expect(mockCallbacks.onData).toHaveBeenCalledWith(connection.id, data);
    });

    it("should process outgoing data", () => {
      const data = Buffer.from("Response data");

      const result = tcpFilter.processOutgoingData(connection.id, data);
      expect(result).toBeDefined();
    });

    it("should handle large data chunks", () => {
      const largeData = Buffer.alloc(1024 * 1024); // 1MB

      const result = tcpFilter.processIncomingData(connection.id, largeData);
      expect(result).toBeDefined();
    });

    it("should handle binary data", () => {
      const binaryData = Buffer.from([0x01, 0x02, 0x03, 0x04, 0x05]);

      const result = tcpFilter.processIncomingData(connection.id, binaryData);
      expect(result).toBeDefined();
    });

    it("should handle fragmented data", () => {
      const fragments = [
        Buffer.from("Fragmented "),
        Buffer.from("data "),
        Buffer.from("message"),
      ];

      fragments.forEach((fragment) => {
        const result = tcpFilter.processIncomingData(connection.id, fragment);
        expect(result).toBeDefined();
      });
    });
  });

  describe("proxy functionality", () => {
    let connection: TcpConnection;

    beforeEach(() => {
      tcpFilter.setCallbacks(mockCallbacks);
      connection = tcpFilter.createConnection("192.168.1.1", 12345);
    });

    it("should forward data to target", () => {
      const data = Buffer.from("Forward this data");

      const result = tcpFilter.forwardToTarget(connection.id, data);
      expect(result).toBeDefined();
    });

    it("should forward data to client", () => {
      const data = Buffer.from("Response from target");

      const result = tcpFilter.forwardToClient(connection.id, data);
      expect(result).toBeDefined();
    });

    it("should handle bidirectional data flow", () => {
      const clientData = Buffer.from("Client request");
      const targetData = Buffer.from("Target response");

      tcpFilter.forwardToTarget(connection.id, clientData);
      tcpFilter.forwardToClient(connection.id, targetData);

      expect(mockCallbacks.onData).toHaveBeenCalledTimes(2);
    });

    it("should maintain connection state during data flow", () => {
      const data = Buffer.from("Test data");

      tcpFilter.processIncomingData(connection.id, data);
      expect(connection.state).toBe(TcpConnectionState.CONNECTED);

      tcpFilter.forwardToTarget(connection.id, data);
      expect(connection.state).toBe(TcpConnectionState.CONNECTED);
    });
  });

  describe("error handling", () => {
    let connection: TcpConnection;

    beforeEach(() => {
      tcpFilter.setCallbacks(mockCallbacks);
      connection = tcpFilter.createConnection("192.168.1.1", 12345);
    });

    it("should handle connection errors", () => {
      const error = new Error("Connection failed");

      tcpFilter.handleConnectionError(connection.id, error);
      expect(mockCallbacks.onError).toHaveBeenCalledWith(connection.id, error);
    });

    it("should handle data processing errors", () => {
      const malformedData = Buffer.from("INVALID DATA");

      const result = tcpFilter.processIncomingData(
        connection.id,
        malformedData
      );
      expect(result).toBeDefined();
      expect(mockCallbacks.onError).toHaveBeenCalled();
    });

    it("should handle network timeouts", () => {
      tcpFilter.updateConnectionState(
        connection.id,
        TcpConnectionState.CONNECTING
      );

      // Simulate timeout
      setTimeout(() => {
        if (connection.state === TcpConnectionState.CONNECTING) {
          tcpFilter.handleConnectionError(
            connection.id,
            new Error("Connection timeout")
          );
        }
      }, 100);

      expect(mockCallbacks.onError).toHaveBeenCalled();
    });

    it("should handle invalid connection IDs", () => {
      const invalidId = 99999;

      expect(() =>
        tcpFilter.processIncomingData(invalidId, Buffer.from("test"))
      ).not.toThrow();
      expect(() => tcpFilter.closeConnection(invalidId)).not.toThrow();
    });
  });

  describe("filter statistics and monitoring", () => {
    let connection: TcpConnection;

    beforeEach(() => {
      tcpFilter.setCallbacks(mockCallbacks);
      connection = tcpFilter.createConnection("192.168.1.1", 12345);
    });

    it("should track connection count", () => {
      const initialStats = tcpFilter.getStats();

      const newConnection = tcpFilter.createConnection("192.168.1.2", 12346);
      const updatedStats = tcpFilter.getStats();

      expect(updatedStats.connectionCount).toBe(
        initialStats.connectionCount + 1
      );

      tcpFilter.closeConnection(newConnection.id);
    });

    it("should track data throughput", () => {
      const data = Buffer.alloc(1024);
      tcpFilter.processIncomingData(connection.id, data);

      const stats = tcpFilter.getStats();
      expect(stats.bytesProcessed).toBeGreaterThan(0);
    });

    it("should track active connections", () => {
      const stats = tcpFilter.getStats();
      expect(stats.activeConnections).toBeGreaterThan(0);
    });

    it("should track connection errors", () => {
      const error = new Error("Test error");
      tcpFilter.handleConnectionError(connection.id, error);

      const stats = tcpFilter.getStats();
      expect(stats.errorCount).toBeGreaterThan(0);
    });

    it("should reset statistics", () => {
      const data = Buffer.from("Test data");
      tcpFilter.processIncomingData(connection.id, data);

      tcpFilter.resetStats();

      const stats = tcpFilter.getStats();
      expect(stats.bytesProcessed).toBe(0);
      expect(stats.connectionCount).toBe(0);
    });
  });

  describe("filter configuration", () => {
    it("should update configuration", () => {
      const newConfig: TcpProxyConfig = {
        listenPort: 9090,
        targetHost: "127.0.0.1",
        targetPort: 8080,
        maxConnections: 200,
        connectionTimeout: 60000,
        enableTls: true,
        bufferSize: 16384,
      };

      tcpFilter.updateConfig(newConfig);
      expect(tcpFilter.config.listenPort).toBe(9090);
      expect(tcpFilter.config.targetHost).toBe("127.0.0.1");
      expect(tcpFilter.config.enableTls).toBe(true);
    });

    it("should validate configuration values", () => {
      const invalidConfig = {
        listenPort: -1,
        targetHost: "",
        targetPort: 0,
        maxConnections: 0,
        connectionTimeout: -1000,
      } as any;

      expect(() => tcpFilter.updateConfig(invalidConfig)).not.toThrow();
    });

    it("should handle partial configuration updates", () => {
      const partialConfig = {
        listenPort: 9090,
        enableTls: true,
      };

      tcpFilter.updateConfig(partialConfig);
      expect(tcpFilter.config.listenPort).toBe(9090);
      expect(tcpFilter.config.enableTls).toBe(true);
      expect(tcpFilter.config.targetHost).toBe("localhost"); // Should remain unchanged
    });
  });

  describe("filter lifecycle", () => {
    it("should start and stop filter", () => {
      expect(() => tcpFilter.start()).not.toThrow();
      expect(tcpFilter.isRunning()).toBe(true);

      expect(() => tcpFilter.stop()).not.toThrow();
      expect(tcpFilter.isRunning()).toBe(false);
    });

    it("should pause and resume filter", () => {
      tcpFilter.start();

      expect(() => tcpFilter.pause()).not.toThrow();
      expect(tcpFilter.isPaused()).toBe(true);

      expect(() => tcpFilter.resume()).not.toThrow();
      expect(tcpFilter.isPaused()).toBe(false);
    });

    it("should handle multiple start/stop cycles", () => {
      for (let i = 0; i < 3; i++) {
        tcpFilter.start();
        expect(tcpFilter.isRunning()).toBe(true);

        tcpFilter.stop();
        expect(tcpFilter.isRunning()).toBe(false);
      }
    });
  });

  describe("connection pooling", () => {
    it("should manage connection pool", () => {
      const poolSize = 5;
      const connections: TcpConnection[] = [];

      // Create connections up to pool size
      for (let i = 0; i < poolSize; i++) {
        const conn = tcpFilter.createConnection(`192.168.1.${i}`, 12345 + i);
        connections.push(conn);
      }

      const stats = tcpFilter.getStats();
      expect(stats.connectionCount).toBe(poolSize);

      // Close all connections
      connections.forEach((conn) => tcpFilter.closeConnection(conn.id));
    });

    it("should handle connection limits", () => {
      const maxConnections = 3;
      const config: TcpProxyConfig = {
        ...tcpFilter.config,
        maxConnections,
      };

      tcpFilter.updateConfig(config);

      const connections: TcpConnection[] = [];
      for (let i = 0; i < maxConnections + 2; i++) {
        const conn = tcpFilter.createConnection(`192.168.1.${i}`, 12345 + i);
        if (conn) connections.push(conn);
      }

      expect(connections.length).toBeLessThanOrEqual(maxConnections);

      connections.forEach((conn) => tcpFilter.closeConnection(conn.id));
    });
  });

  describe("filter cleanup", () => {
    it("should destroy filter resources", () => {
      expect(() => tcpFilter.destroy()).not.toThrow();
    });

    it("should handle multiple destroy calls gracefully", () => {
      expect(() => tcpFilter.destroy()).not.toThrow();
      expect(() => tcpFilter.destroy()).not.toThrow(); // Should be safe to call multiple times
    });

    it("should cleanup callbacks on destroy", () => {
      tcpFilter.setCallbacks(mockCallbacks);
      tcpFilter.destroy();

      expect(tcpFilter.callbacks).toBeUndefined();
    });

    it("should cleanup all connections on destroy", () => {
      const connection = tcpFilter.createConnection("192.168.1.1", 12345);

      tcpFilter.destroy();

      expect(connection.state).toBe(TcpConnectionState.DISCONNECTED);
    });
  });

  describe("edge cases", () => {
    it("should handle rapid connection creation and destruction", () => {
      for (let i = 0; i < 100; i++) {
        const conn = tcpFilter.createConnection(`192.168.1.${i}`, 12345 + i);
        tcpFilter.closeConnection(conn.id);
      }

      const stats = tcpFilter.getStats();
      expect(stats.connectionCount).toBe(0);
    });

    it("should handle concurrent data processing", async () => {
      const connection = tcpFilter.createConnection("192.168.1.1", 12345);
      const promises = Array.from({ length: 10 }, (_, i) => {
        const data = Buffer.from(`Concurrent data ${i}`);
        return tcpFilter.processIncomingData(connection.id, data);
      });

      const results = await Promise.all(promises);
      results.forEach((result) => {
        expect(result).toBeDefined();
      });
    });

    it("should handle very large data chunks", () => {
      const connection = tcpFilter.createConnection("192.168.1.1", 12345);
      const largeData = Buffer.alloc(1024 * 1024 * 10); // 10MB

      const result = tcpFilter.processIncomingData(connection.id, largeData);
      expect(result).toBeDefined();
    });

    it("should handle empty data buffers", () => {
      const connection = tcpFilter.createConnection("192.168.1.1", 12345);
      const emptyData = Buffer.alloc(0);

      const result = tcpFilter.processIncomingData(connection.id, emptyData);
      expect(result).toBeDefined();
    });
  });
});
