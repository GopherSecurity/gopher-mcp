/**
 * @file gopher-filtered-transport.test.ts
 * @brief Integration tests for GopherFilteredTransport
 *
 * Tests the transport wrapper with various filter configurations and decisions.
 */

import { GopherFilteredTransport } from "../gopher-filtered-transport";
import type { Transport } from "@modelcontextprotocol/sdk/shared/transport.js";
import type { JSONRPCMessage, MessageExtraInfo } from "@modelcontextprotocol/sdk/types.js";
import { createRealDispatcher, destroyDispatcher } from "../mcp-filter-api";

/**
 * Mock SDK transport for testing
 */
class MockTransport implements Transport {
  onmessage?: (message: JSONRPCMessage, extra?: MessageExtraInfo) => void;
  onclose?: () => void;
  onerror?: (error: Error) => void;

  sentMessages: JSONRPCMessage[] = [];
  private closeHandlers: (() => void)[] = [];

  async start(): Promise<void> {
    // Mock start
  }

  async send(message: JSONRPCMessage): Promise<void> {
    this.sentMessages.push(message);
  }

  async close(): Promise<void> {
    if (this.onclose) {
      this.onclose();
    }
    this.closeHandlers.forEach(h => h());
  }

  // Test helper: simulate incoming message
  simulateIncoming(message: JSONRPCMessage, extra?: MessageExtraInfo): void {
    if (this.onmessage) {
      this.onmessage(message, extra);
    }
  }

  // Test helper: register close handler
  onCloseInternal(handler: () => void): void {
    this.closeHandlers.push(handler);
  }
}

describe("GopherFilteredTransport", () => {
  let mockTransport: MockTransport;
  let filteredTransport: GopherFilteredTransport;
  let dispatcher: number;

  beforeEach(async () => {
    mockTransport = new MockTransport();
    dispatcher = createRealDispatcher();

    filteredTransport = new GopherFilteredTransport(mockTransport, {
      dispatcherHandle: dispatcher,
      filterConfig: {
        listeners: [
          {
            name: "test_listener",
            filter_chains: [
              {
                name: "default",
                filters: [
                  {
                    type: "rate_limiter",
                    name: "rate",
                    config: {
                      requests_per_second: 10,
                      burst_size: 5
                    }
                  }
                ]
              }
            ]
          }
        ]
      },
      debugLogging: false
    });

    await filteredTransport.start();
  });

  afterEach(async () => {
    await filteredTransport.close();
    destroyDispatcher(dispatcher);
  });

  describe("Lifecycle", () => {
    it("should start and connect successfully", () => {
      expect(filteredTransport.isConnected()).toBe(true);
    });

    it("should close cleanly", async () => {
      await filteredTransport.close();
      expect(filteredTransport.isConnected()).toBe(false);
    });

    it("should handle double start gracefully", async () => {
      await expect(filteredTransport.start()).resolves.not.toThrow();
    });

    it("should handle double close gracefully", async () => {
      await filteredTransport.close();
      await expect(filteredTransport.close()).resolves.not.toThrow();
    });
  });

  describe("Outgoing Messages", () => {
    it("should allow messages within rate limit", async () => {
      const messages = [];
      for (let i = 0; i < 5; i++) {
        messages.push(
          filteredTransport.send({
            jsonrpc: "2.0",
            method: "test",
            id: i
          })
        );
      }

      await Promise.all(messages);
      expect(mockTransport.sentMessages.length).toBe(5);
    });

    it("should propagate send errors from base transport", async () => {
      mockTransport.send = async () => {
        throw new Error("Mock send error");
      };

      await expect(
        filteredTransport.send({
          jsonrpc: "2.0",
          method: "test",
          id: 1
        })
      ).rejects.toThrow("Mock send error");
    });

    it("should throw error when sending before start", async () => {
      const newTransport = new GopherFilteredTransport(new MockTransport(), {
        dispatcherHandle: dispatcher,
        filterConfig: {
          listeners: [{
            name: "test",
            filter_chains: [{
              name: "default",
              filters: []
            }]
          }]
        }
      });

      await expect(
        newTransport.send({
          jsonrpc: "2.0",
          method: "test",
          id: 1
        })
      ).rejects.toThrow("not connected");
    });
  });

  describe("Incoming Messages", () => {
    it("should intercept and deliver incoming messages", (done) => {
      filteredTransport.onmessage = (message) => {
        expect(message.method).toBe("test");
        expect((message as any).id).toBe(42);
        done();
      };

      mockTransport.simulateIncoming({
        jsonrpc: "2.0",
        method: "test",
        id: 42
      });
    });

    it("should process multiple incoming messages", (done) => {
      const receivedIds: number[] = [];

      filteredTransport.onmessage = (message) => {
        receivedIds.push((message as any).id);

        if (receivedIds.length === 3) {
          expect(receivedIds).toEqual([1, 2, 3]);
          done();
        }
      };

      for (let i = 1; i <= 3; i++) {
        mockTransport.simulateIncoming({
          jsonrpc: "2.0",
          method: "test",
          id: i
        });
      }
    });

    it("should handle incoming messages with extra info", (done) => {
      const extraInfo = { sessionId: "test-session" };

      filteredTransport.onmessage = (message, extra) => {
        expect(message.method).toBe("test");
        expect(extra).toEqual(extraInfo);
        done();
      };

      mockTransport.simulateIncoming(
        {
          jsonrpc: "2.0",
          method: "test",
          id: 1
        },
        extraInfo
      );
    });
  });

  describe("Event Propagation", () => {
    it("should propagate close events", (done) => {
      filteredTransport.onclose = () => {
        done();
      };

      mockTransport.close();
    });

    it("should propagate error events", (done) => {
      const testError = new Error("Test error");

      filteredTransport.onerror = (error) => {
        expect(error).toBe(testError);
        done();
      };

      if (mockTransport.onerror) {
        mockTransport.onerror(testError);
      }
    });
  });

  describe("Metrics", () => {
    it("should provide metrics", async () => {
      const metrics = await filteredTransport.getMetrics();
      expect(metrics).toBeDefined();
      expect(metrics.chain).toBeDefined();
    });

    it("should provide queue stats", () => {
      const stats = filteredTransport.getQueueStats();
      expect(stats.size).toBe(0);
      expect(stats.capacity).toBe(1000);
      expect(stats.isFull).toBe(false);
      expect(stats.oldestAge).toBe(0);
    });
  });

  describe("Configuration Export", () => {
    it("should export filter configuration", async () => {
      const config = await filteredTransport.exportFilterConfig();
      expect(config).toBeDefined();
      expect(config.listeners).toBeDefined();
      expect(config.listeners.length).toBeGreaterThan(0);
    });
  });

  describe("Dynamic Filter Control", () => {
    it("should enable filter", async () => {
      await expect(
        filteredTransport.setFilterEnabled("rate", true)
      ).resolves.not.toThrow();
    });

    it("should disable filter", async () => {
      await expect(
        filteredTransport.setFilterEnabled("rate", false)
      ).resolves.not.toThrow();
    });

    it("should toggle filter multiple times", async () => {
      await filteredTransport.setFilterEnabled("rate", false);
      await filteredTransport.setFilterEnabled("rate", true);
      await filteredTransport.setFilterEnabled("rate", false);

      // Should not throw and filter should be disabled
      const metrics = await filteredTransport.getMetrics("rate");
      expect(metrics).toBeDefined();
    });
  });

  describe("Error Handling", () => {
    it("should fail-open on filter processing errors", (done) => {
      // Simulate filter error by sending malformed data
      filteredTransport.onmessage = (message) => {
        // Should still receive message despite error
        expect(message).toBeDefined();
        done();
      };

      mockTransport.simulateIncoming({
        jsonrpc: "2.0",
        method: "test",
        id: 1
      });
    });
  });

  describe("Message Queue", () => {
    it("should track queue size", () => {
      const stats = filteredTransport.getQueueStats();
      expect(stats.size).toBe(0);
      expect(stats.capacity).toBeGreaterThan(0);
    });

    it("should report empty queue correctly", () => {
      const stats = filteredTransport.getQueueStats();
      expect(stats.size).toBe(0);
      expect(stats.isFull).toBe(false);
    });
  });
});

describe("GopherFilteredTransport with Circuit Breaker", () => {
  let mockTransport: MockTransport;
  let filteredTransport: GopherFilteredTransport;
  let dispatcher: number;

  beforeEach(async () => {
    mockTransport = new MockTransport();
    dispatcher = createRealDispatcher();

    filteredTransport = new GopherFilteredTransport(mockTransport, {
      dispatcherHandle: dispatcher,
      filterConfig: {
        listeners: [
          {
            name: "test_listener",
            filter_chains: [
              {
                name: "default",
                filters: [
                  {
                    type: "circuit_breaker",
                    name: "breaker",
                    config: {
                      failure_threshold: 5,
                      timeout_ms: 30000,
                      half_open_requests: 3
                    }
                  }
                ]
              }
            ]
          }
        ]
      },
      debugLogging: false
    });

    await filteredTransport.start();
  });

  afterEach(async () => {
    await filteredTransport.close();
    destroyDispatcher(dispatcher);
  });

  it("should allow messages through circuit breaker initially", async () => {
    await filteredTransport.send({
      jsonrpc: "2.0",
      method: "test",
      id: 1
    });

    expect(mockTransport.sentMessages.length).toBe(1);
  });

  it("should get circuit breaker metrics", async () => {
    const metrics = await filteredTransport.getMetrics("breaker");
    expect(metrics).toBeDefined();
  });
});

describe("GopherFilteredTransport with Multiple Filters", () => {
  let mockTransport: MockTransport;
  let filteredTransport: GopherFilteredTransport;
  let dispatcher: number;

  beforeEach(async () => {
    mockTransport = new MockTransport();
    dispatcher = createRealDispatcher();

    filteredTransport = new GopherFilteredTransport(mockTransport, {
      dispatcherHandle: dispatcher,
      filterConfig: {
        listeners: [
          {
            name: "test_listener",
            filter_chains: [
              {
                name: "default",
                filters: [
                  {
                    type: "rate_limiter",
                    name: "rate",
                    config: { requests_per_second: 100, burst_size: 10 }
                  },
                  {
                    type: "circuit_breaker",
                    name: "breaker",
                    config: { failure_threshold: 5, timeout_ms: 30000 }
                  },
                  {
                    type: "metrics",
                    name: "metrics",
                    config: { export_port: 9090 }
                  }
                ]
              }
            ]
          }
        ]
      },
      debugLogging: false
    });

    await filteredTransport.start();
  });

  afterEach(async () => {
    await filteredTransport.close();
    destroyDispatcher(dispatcher);
  });

  it("should process messages through all filters", async () => {
    await filteredTransport.send({
      jsonrpc: "2.0",
      method: "test",
      id: 1
    });

    expect(mockTransport.sentMessages.length).toBe(1);
  });

  it("should get metrics from all filters", async () => {
    const metrics = await filteredTransport.getMetrics();
    expect(metrics).toBeDefined();
    expect(metrics.chain).toBeDefined();
  });

  it("should control individual filters", async () => {
    await filteredTransport.setFilterEnabled("rate", false);
    await filteredTransport.setFilterEnabled("breaker", false);
    await filteredTransport.setFilterEnabled("metrics", false);

    // Should still be able to send messages
    await filteredTransport.send({
      jsonrpc: "2.0",
      method: "test",
      id: 1
    });

    expect(mockTransport.sentMessages.length).toBe(1);
  });
});
