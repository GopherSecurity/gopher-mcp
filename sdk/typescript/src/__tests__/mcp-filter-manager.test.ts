/**
 * @file filter-manager-simple.test.ts
 * @brief Simplified tests for FilterManager core functionality
 */

import { FilterManager, JSONRPCMessage } from "../mcp-filter-manager";

// Mock the FFI bindings with simpler implementation
jest.mock("../mcp-ffi-bindings", () => ({
  mcpFilterLib: {
    mcp_filter_manager_create: jest.fn(() => 12345),
    mcp_filter_manager_destroy: jest.fn(),
    mcp_filter_manager_init: jest.fn(() => 0),
    mcp_filter_chain_create_from_config: jest.fn(() => 54321),
    mcp_filter_post_data: jest.fn(() => 0),
  },
}));

// Mock filter-buffer functions
jest.mock("../mcp-filter-buffer", () => ({
  BufferOwnership: {
    NONE: 0,
    SHARED: 1,
    EXCLUSIVE: 2,
    EXTERNAL: 3,
  },
  createBufferPoolSimple: jest.fn(() => 11111),
  destroyBufferPool: jest.fn(),
  createBuffer: jest.fn(() => 22222),
  writeBufferData: jest.fn(() => 0),
  getBufferLength: jest.fn(() => 100),
  readBufferData: jest.fn(() => new Uint8Array([123, 125])), // "{}"
  releaseBuffer: jest.fn(),
}));

// Mock filter-chain functions
jest.mock("../mcp-filter-chain", () => ({
  createFilterChainFromConfig: jest.fn(() => 54321),
  destroyFilterChain: jest.fn(),
  FilterStatus: {
    CONTINUE: 0,
    STOP_ITERATION: 1,
    ERROR: -1,
  },
  ChainExecutionMode: {
    SEQUENTIAL: 0,
    PARALLEL: 1,
  },
  RoutingStrategy: {
    ROUND_ROBIN: 0,
    LEAST_LOADED: 1,
  },
}));

// Mock filter-api functions
jest.mock("../mcp-filter-api", () => ({
  initializeFilterManager: jest.fn(() => 0),
  postDataToFilter: jest.fn(() => 0),
  FilterStatus: {
    CONTINUE: 0,
    STOP_ITERATION: 1,
    ERROR: -1,
  },
}));

describe("FilterManager - Core Functionality", () => {
  let filterManager: FilterManager;

  afterEach(() => {
    if (filterManager) {
      filterManager.destroy();
    }
  });

  describe("Basic Operations", () => {
    it("should create and destroy FilterManager", () => {
      filterManager = new FilterManager();
      expect(filterManager).toBeInstanceOf(FilterManager);

      filterManager.destroy();
      // After destroy, creating a new one should work
      filterManager = new FilterManager();
      expect(filterManager).toBeInstanceOf(FilterManager);
    });

    it("should create FilterManager with configuration", () => {
      filterManager = new FilterManager({
        http: {
          codec: true,
          routing: true,
        },
        security: {
          authentication: true,
        },
        chain: {
          bufferSize: 16384,
          timeoutMs: 10000,
        },
      });
      expect(filterManager).toBeInstanceOf(FilterManager);
    });
  });

  describe("Message Processing", () => {
    beforeEach(() => {
      filterManager = new FilterManager({
        mcp: {
          jsonRpcProtocol: true,
        },
      });
    });

    it("should process a valid JSONRPCMessage", async () => {
      const message: JSONRPCMessage = {
        jsonrpc: "2.0",
        method: "test.method",
        params: { key: "value" },
        id: 1,
      };

      const result = await filterManager.processMessage(message);
      expect(result).toEqual({
        jsonrpc: "2.0",
        result: "Message processed",
        id: 1,
      });
    });

    it("should handle message without id", async () => {
      const notification: JSONRPCMessage = {
        jsonrpc: "2.0",
        method: "test.notification",
        params: {},
      };

      const result = await filterManager.processMessage(notification);
      expect(result).toEqual({
        jsonrpc: "2.0",
        result: "Message processed",
      });
    });

    it("should handle message with error", async () => {
      const errorMessage: JSONRPCMessage = {
        jsonrpc: "2.0",
        error: {
          code: -32600,
          message: "Invalid Request",
        },
        id: 1,
      };

      const result = await filterManager.processMessage(errorMessage);
      expect(result).toBeDefined();
    });
  });

  describe("Configuration Options", () => {
    it("should create FilterManager with network filters", () => {
      filterManager = new FilterManager({
        network: {
          tcpProxy: true,
          udpProxy: false,
        },
      });
      expect(filterManager).toBeInstanceOf(FilterManager);
    });

    it("should create FilterManager with observability filters", () => {
      filterManager = new FilterManager({
        observability: {
          accessLog: true,
          metrics: true,
          tracing: false,
        },
      });
      expect(filterManager).toBeInstanceOf(FilterManager);
    });

    it("should create FilterManager with traffic management", () => {
      filterManager = new FilterManager({
        trafficManagement: {
          circuitBreaker: true,
          retry: true,
          timeout: true,
        },
      });
      expect(filterManager).toBeInstanceOf(FilterManager);
    });

    it("should create FilterManager with error handling", () => {
      filterManager = new FilterManager({
        errorHandling: {
          stopOnError: false,
          retryAttempts: 3,
          retryDelayMs: 1000,
        },
      });
      expect(filterManager).toBeInstanceOf(FilterManager);
    });
  });

  describe("Error Handling", () => {
    beforeEach(() => {
      filterManager = new FilterManager({
        errorHandling: {
          stopOnError: true,
        },
      });
    });

    it("should handle processing errors", async () => {
      const invalidMessage = {} as JSONRPCMessage;

      await expect(filterManager.processMessage(invalidMessage)).rejects.toThrow();
    });

    it("should handle destroy on already destroyed manager", () => {
      filterManager.destroy();
      // Second destroy should not throw
      expect(() => filterManager.destroy()).not.toThrow();
    });
  });
});