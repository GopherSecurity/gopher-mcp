/**
 * @file filter-manager-simple.test.ts
 * @brief Simplified tests for FilterManager core functionality
 */

import { FilterManager, JSONRPCMessage } from "../filter-manager";

// Mock the FFI bindings with simpler implementation
jest.mock("../index", () => ({
  createFilterManager: jest.fn(() => 12345),
  createBuiltinFilter: jest.fn(() => 67890),
  addFilterToManager: jest.fn(() => 0),
  initializeFilterManager: jest.fn(() => 0),
  releaseFilter: jest.fn(),
  releaseFilterManager: jest.fn(),
  createBufferFromString: jest.fn(() => 11111),
  readStringFromBuffer: jest.fn(() => '{"jsonrpc":"2.0","id":"1","method":"test/method","params":{"test":true}}'),
  postDataToFilter: jest.fn((_filter: any, _data: any, callback: any) => {
    // Always succeed for basic tests
    setImmediate(() => callback(0, null));
    return 0;
  }),
  BufferOwnership: {
    SHARED: 1,
  },
  BuiltinFilterType: {
    AUTHENTICATION: 21,
    RATE_LIMIT: 40,
    ACCESS_LOG: 30,
    METRICS: 31,
  },
}));

describe("FilterManager - Core Functionality", () => {
  let filterManager: FilterManager;

  afterEach(() => {
    if (filterManager && !filterManager.isDestroyed()) {
      filterManager.destroy();
    }
  });

  describe("Basic Operations", () => {
    it("should create and destroy FilterManager", () => {
      filterManager = new FilterManager();
      expect(filterManager).toBeInstanceOf(FilterManager);
      expect(filterManager.isDestroyed()).toBe(false);
      
      filterManager.destroy();
      expect(filterManager.isDestroyed()).toBe(true);
    });

    it("should validate configuration", () => {
      expect(() => {
        new FilterManager({
          rateLimit: {
            requestsPerMinute: -10, // Invalid
          },
        });
      }).toThrow("Rate limit requestsPerMinute must be positive");
    });

    it("should validate JSON-RPC messages", async () => {
      filterManager = new FilterManager();

      // Test invalid version
      await expect(filterManager.process({
        jsonrpc: "1.0" as any,
        method: "test",
      })).rejects.toThrow("Invalid JSON-RPC version");

      // Test null message
      await expect(filterManager.process(null as any)).rejects.toThrow("Message cannot be null");
    });
  });

  describe("Resource Management", () => {
    it("should prevent use after destruction", async () => {
      filterManager = new FilterManager();
      filterManager.destroy();

      await expect(filterManager.process({
        jsonrpc: "2.0",
        method: "test",
      })).rejects.toThrow("FilterManager has been destroyed");
    });

    it("should handle double destruction", () => {
      const consoleSpy = jest.spyOn(console, "warn").mockImplementation();
      
      filterManager = new FilterManager();
      filterManager.destroy();
      filterManager.destroy(); // Second destruction

      expect(consoleSpy).toHaveBeenCalledWith("FilterManager is already destroyed");
      consoleSpy.mockRestore();
    });
  });

  describe("Filter Configuration", () => {
    it("should create filters based on configuration", () => {
      const { createBuiltinFilter } = require("../index");

      filterManager = new FilterManager({
        auth: {
          method: "jwt",
          secret: "test-secret",
        },
        logging: true,
      });

      expect(createBuiltinFilter).toHaveBeenCalledWith(
        0,
        21, // AUTHENTICATION
        expect.objectContaining({
          method: "jwt",
          secret: "test-secret",
        })
      );

      expect(createBuiltinFilter).toHaveBeenCalledWith(
        0,
        30, // ACCESS_LOG
        {}
      );
    });
  });

  describe("Message Processing", () => {
    beforeEach(() => {
      filterManager = new FilterManager({
        errorHandling: {
          fallbackBehavior: "passthrough", // Use passthrough to avoid errors
        },
      });
    });

    it("should process valid request", async () => {
      const message: JSONRPCMessage = {
        jsonrpc: "2.0",
        id: "1",
        method: "test/method",
        params: { test: true },
      };

      const result = await filterManager.process(message);
      expect(result).toBeDefined();
      expect(result.jsonrpc).toBe("2.0");
    });

    it("should process valid response", async () => {
      const response: JSONRPCMessage = {
        jsonrpc: "2.0",
        id: "1",
        result: { success: true },
      };

      const result = await filterManager.processResponse(response);
      expect(result).toBeDefined();
      expect(result.jsonrpc).toBe("2.0");
    });

    it("should process request-response cycle", async () => {
      const request: JSONRPCMessage = {
        jsonrpc: "2.0",
        id: "1",
        method: "test/method",
      };

      const response: JSONRPCMessage = {
        jsonrpc: "2.0",
        id: "1",
        result: { success: true },
      };

      const result = await filterManager.processRequestResponse(request, response);
      expect(result.processedRequest).toBeDefined();
      expect(result.processedResponse).toBeDefined();
    });
  });

  describe("Error Handling", () => {
    it("should handle passthrough behavior", async () => {
      filterManager = new FilterManager({
        errorHandling: {
          fallbackBehavior: "passthrough",
        },
      });

      const message: JSONRPCMessage = {
        jsonrpc: "2.0",
        id: "1",
        method: "test/method",
      };

      const result = await filterManager.process(message);
      expect(result).toBeDefined();
    });

    it("should handle default error behavior", async () => {
      filterManager = new FilterManager({
        errorHandling: {
          fallbackBehavior: "default",
        },
      });

      const message: JSONRPCMessage = {
        jsonrpc: "2.0",
        id: "1",
        method: "test/method",
      };

      const result = await filterManager.process(message);
      expect(result).toBeDefined();
    });
  });

  describe("Edge Cases", () => {
    it("should handle empty configuration", () => {
      filterManager = new FilterManager();
      expect(filterManager).toBeInstanceOf(FilterManager);
    });

    it("should handle large messages", async () => {
      filterManager = new FilterManager();

      const largeParams = new Array(100).fill(0).map((_, i) => ({ id: i }));
      const message: JSONRPCMessage = {
        jsonrpc: "2.0",
        id: "1",
        method: "test/large",
        params: largeParams,
      };

      const result = await filterManager.process(message);
      expect(result).toBeDefined();
    });
  });
});
