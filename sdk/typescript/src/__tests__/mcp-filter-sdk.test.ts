/**
 * @file mcp-filter-sdk.test.ts
 * @brief Basic tests for the MCP Filter SDK
 */

import { McpFilterSdk } from "../core/mcp-filter-sdk";
import { McpBuiltinFilterType, McpResult } from "../types";

describe("McpFilterSdk", () => {
  let sdk: McpFilterSdk;

  beforeEach(() => {
    sdk = new McpFilterSdk();
  });

  afterEach(() => {
    if (sdk.isInitialized()) {
      sdk.shutdown();
    }
  });

  describe("initialization", () => {
    it("should initialize successfully", async () => {
      const result = await sdk.initialize();
      expect(result.result).toBe(McpResult.OK);
      expect(sdk.isInitialized()).toBe(true);
    });

    it("should not initialize twice", async () => {
      await sdk.initialize();
      const result = await sdk.initialize();
      expect(result.result).toBe(McpResult.ERROR_ALREADY_INITIALIZED);
    });

    it("should shutdown successfully", async () => {
      await sdk.initialize();
      const result = await sdk.shutdown();
      expect(result.result).toBe(McpResult.OK);
      expect(sdk.isInitialized()).toBe(false);
    });
  });

  describe("memory pool management", () => {
    beforeEach(async () => {
      await sdk.initialize();
    });

    it("should create memory pool", async () => {
      const result = await sdk.createMemoryPool(1024);
      expect(result.result).toBe(McpResult.OK);
      expect(result.data).toBeDefined();
      expect(result.data).toBeGreaterThan(0);
    });

    it("should destroy memory pool", async () => {
      const createResult = await sdk.createMemoryPool(1024);
      expect(createResult.result).toBe(McpResult.OK);

      const destroyResult = await sdk.destroyMemoryPool(createResult.data!);
      expect(destroyResult.result).toBe(McpResult.OK);
    });
  });

  describe("filter management", () => {
    beforeEach(async () => {
      await sdk.initialize();
    });

    it("should create builtin filter", async () => {
      const result = await sdk.createBuiltinFilter(
        McpBuiltinFilterType.TCP_PROXY,
        {}
      );
      expect(result.result).toBe(McpResult.OK);
      expect(result.data).toBeDefined();
      expect(result.data).toBeGreaterThan(0);
    });

    it("should destroy filter", async () => {
      const createResult = await sdk.createBuiltinFilter(
        McpBuiltinFilterType.TCP_PROXY,
        {}
      );
      expect(createResult.result).toBe(McpResult.OK);

      const destroyResult = await sdk.destroyFilter(createResult.data!);
      expect(destroyResult.result).toBe(McpResult.OK);
    });
  });

  describe("buffer management", () => {
    beforeEach(async () => {
      await sdk.initialize();
    });

    it("should create buffer", async () => {
      const result = await sdk.createBuffer(Buffer.from("test data"));
      expect(result.result).toBe(McpResult.OK);
      expect(result.data).toBeDefined();
      expect(result.data).toBeGreaterThan(0);
    });

    it("should destroy buffer", async () => {
      const createResult = await sdk.createBuffer(Buffer.from("test data"));
      expect(createResult.result).toBe(McpResult.OK);

      const destroyResult = await sdk.destroyBuffer(createResult.data!);
      expect(destroyResult.result).toBe(McpResult.OK);
    });
  });

  describe("statistics", () => {
    beforeEach(async () => {
      await sdk.initialize();
    });

    it("should return SDK statistics", () => {
      const stats = sdk.getStats();
      expect(stats).toBeDefined();
      expect(stats.totalFilters).toBe(0);
      expect(stats.totalChains).toBe(0);
      expect(stats.totalBuffers).toBe(0);
      expect(stats.totalMemoryPools).toBe(1); // Default memory pool is created during initialization
      expect(stats.uptime).toBeGreaterThan(0);
    });
  });
});
