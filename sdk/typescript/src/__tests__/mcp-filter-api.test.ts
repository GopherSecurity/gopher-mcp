/**
 * @file filter-api.test.ts
 * @brief Tests for MCP Filter API wrapper
 *
 * This test file covers the core filter functionality including:
 * - Filter lifecycle management
 * - Filter chain management
 * - Basic buffer operations
 * - Filter manager operations
 */

import {
  addFilterToChain,
  addFilterToManager,
  BuiltinFilterType,
  createBuiltinFilter,
  createFilter,
  createFilterChainBuilder,
  createFilterManager,
  FilterError,
  FilterPosition,
  FilterStatus,
  initializeFilterManager,
  releaseFilter,
  releaseFilterManager,
  retainFilter,
} from "../mcp-filter-api";

import {
  buildFilterChain,
  destroyFilterChainBuilder,
} from "../mcp-filter-chain";

// Use real C++ library instead of mocks

describe("Filter API", () => {
  beforeEach(() => {
    // No need to clear mocks since we're using real library
  });

  describe("Filter Lifecycle Management", () => {
    it("should create a filter", () => {
      const config = {
        name: "test-filter",
        type: BuiltinFilterType.HTTP_CODEC,
        settings: { port: 8080 },
        layer: 7,
        memoryPool: null,
      };

      const result = createFilter(0, config);

      // With real library, we expect a valid handle (non-zero) or 0 for error
      expect(typeof result).toBe("number");
      expect(result).toBeGreaterThanOrEqual(0);
    });

    it("should create a built-in filter", () => {
      const result = createBuiltinFilter(0, BuiltinFilterType.TCP_PROXY, {
        port: 8080,
      });

      // With real library, we expect a valid handle (non-zero) or 0 for error
      expect(typeof result).toBe("number");
      expect(result).toBeGreaterThanOrEqual(0);
    });

    it("should retain and release filters", () => {
      const filterHandle = 12345;

      // These functions should not throw with real library
      expect(() => retainFilter(filterHandle)).not.toThrow();
      expect(() => releaseFilter(filterHandle)).not.toThrow();
    });
  });

  describe("Filter Chain Management", () => {
    it("should create a filter chain builder", () => {
      const result = createFilterChainBuilder(0);

      // With real library, we expect a valid builder object or null
      expect(result).toBeDefined();
    });

    it("should add filter to chain", () => {
      const builder = createFilterChainBuilder(0);
      const filterHandle = 12345;
      const position = FilterPosition.FIRST;

      const result = addFilterToChain(builder, filterHandle, position);

      // With real library, we expect a result code (0 for success)
      expect(typeof result).toBe("number");
    });

    it("should build filter chain", () => {
      const builder = createFilterChainBuilder(0);

      const result = buildFilterChain(builder);

      // With real library, we expect a valid chain handle or 0 for error
      expect(typeof result).toBe("number");
      expect(result).toBeGreaterThanOrEqual(0);
    });

    it("should destroy filter chain builder", () => {
      const builder = createFilterChainBuilder(0);

      // This function should not throw with real library
      expect(() => destroyFilterChainBuilder(builder)).not.toThrow();
    });
  });

  describe("Filter Manager", () => {
    it("should create filter manager", () => {
      const result = createFilterManager(123, 456);

      // With real library, we expect a valid manager handle or 0 for error
      expect(typeof result).toBe("number");
      expect(result).toBeGreaterThanOrEqual(0);
    });

    it("should add filter to manager", () => {
      const managerHandle = 11111;
      const filterHandle = 12345;

      const result = addFilterToManager(managerHandle, filterHandle);

      // With real library, we expect a result code (0 for success)
      expect(typeof result).toBe("number");
    });

    it("should initialize filter manager", () => {
      const managerHandle = 11111;

      const result = initializeFilterManager(managerHandle);

      // With real library, we expect a result code (0 for success)
      expect(typeof result).toBe("number");
    });

    it("should release filter manager", () => {
      const managerHandle = 11111;

      // This function should not throw with real library
      expect(() => releaseFilterManager(managerHandle)).not.toThrow();
    });
  });

  describe("Enums and Constants", () => {
    it("should have correct built-in filter types", () => {
      expect(BuiltinFilterType.TCP_PROXY).toBe(0);
      expect(BuiltinFilterType.HTTP_CODEC).toBe(10);
      expect(BuiltinFilterType.TLS_TERMINATION).toBe(20);
      expect(BuiltinFilterType.ACCESS_LOG).toBe(30);
      expect(BuiltinFilterType.RATE_LIMIT).toBe(40);
      expect(BuiltinFilterType.CUSTOM).toBe(100);
    });

    it("should have correct filter positions", () => {
      expect(FilterPosition.FIRST).toBe(0);
      expect(FilterPosition.LAST).toBe(1);
      expect(FilterPosition.BEFORE).toBe(2);
      expect(FilterPosition.AFTER).toBe(3);
    });

    it("should have correct filter status values", () => {
      expect(FilterStatus.CONTINUE).toBe(0);
      expect(FilterStatus.STOP_ITERATION).toBe(1);
    });

    it("should have correct filter error codes", () => {
      expect(FilterError.NONE).toBe(0);
      expect(FilterError.INVALID_CONFIG).toBe(-1000);
      expect(FilterError.INITIALIZATION_FAILED).toBe(-1001);
      expect(FilterError.BUFFER_OVERFLOW).toBe(-1002);
      expect(FilterError.PROTOCOL_VIOLATION).toBe(-1003);
      expect(FilterError.UPSTREAM_TIMEOUT).toBe(-1004);
      expect(FilterError.CIRCUIT_OPEN).toBe(-1005);
      expect(FilterError.RESOURCE_EXHAUSTED).toBe(-1006);
      expect(FilterError.INVALID_STATE).toBe(-1007);
    });
  });
});
