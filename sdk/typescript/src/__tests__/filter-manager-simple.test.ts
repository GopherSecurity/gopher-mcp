/**
 * @file filter-manager-simple.test.ts
 * @brief Simplified tests for FilterManager core functionality using real C++ library
 */

import { FilterManager, JSONRPCMessage } from "../filter-manager";

// Use real C++ library instead of mocks

describe("FilterManager Simple Tests", () => {
  let filterManager: FilterManager;

  beforeEach(() => {
    // Create a new FilterManager for each test
    filterManager = new FilterManager();
  });

  afterEach(() => {
    // Clean up the FilterManager
    if (filterManager) {
      filterManager.destroy();
    }
  });

  describe("Basic Functionality", () => {
    it("should create and destroy FilterManager", () => {
      expect(filterManager).toBeDefined();
      expect(() => filterManager.destroy()).not.toThrow();
    });

    it("should process a simple JSON-RPC message", async () => {
      const message: JSONRPCMessage = {
        jsonrpc: "2.0",
        id: "1",
        method: "test/method",
        params: { test: true },
      };

      // Process the message
      const result = await filterManager.process(message);

      // With real library, we expect a result
      expect(result).toBeDefined();
      expect(result.jsonrpc).toBe("2.0");
      expect(result.id).toBe("1");
    });

    it("should handle empty message", async () => {
      const message: JSONRPCMessage = {
        jsonrpc: "2.0",
        id: "1",
        method: "",
        params: {},
      };

      // Process the message
      const result = await filterManager.process(message);

      // With real library, we expect a result
      expect(result).toBeDefined();
      expect(result.jsonrpc).toBe("2.0");
      expect(result.id).toBe("1");
    });

    it("should handle message with null params", async () => {
      const message: JSONRPCMessage = {
        jsonrpc: "2.0",
        id: "1",
        method: "test/method",
        params: null,
      };

      // Process the message
      const result = await filterManager.process(message);

      // With real library, we expect a result
      expect(result).toBeDefined();
      expect(result.jsonrpc).toBe("2.0");
      expect(result.id).toBe("1");
    });
  });

  describe("Error Handling", () => {
    it("should handle invalid JSON-RPC message", async () => {
      const invalidMessage = {
        // Missing required fields
        method: "test/method",
      } as any;

      // Process the message - should not throw
      await expect(filterManager.process(invalidMessage)).rejects.toThrow();
    });

    it("should handle destroyed manager", () => {
      // Destroy the manager
      filterManager.destroy();

      const message: JSONRPCMessage = {
        jsonrpc: "2.0",
        id: "1",
        method: "test/method",
        params: {},
      };

      // Should throw when trying to process after destruction
      expect(() => filterManager.process(message)).rejects.toThrow();
    });
  });

  describe("Response Processing", () => {
    it("should process response messages", async () => {
      const response: JSONRPCMessage = {
        jsonrpc: "2.0",
        id: "1",
        result: { success: true },
      };

      // Process the response
      const result = await filterManager.processResponse(response);

      // With real library, we expect a result
      expect(result).toBeDefined();
      expect(result.jsonrpc).toBe("2.0");
      expect(result.id).toBe("1");
    });
  });
});