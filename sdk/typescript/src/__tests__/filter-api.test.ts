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
} from "../filter-api";

import { buildFilterChain, destroyFilterChainBuilder } from "../filter-chain";

// Mock the FFI library
jest.mock("../ffi-bindings", () => ({
  mcpFilterLib: {
    mcp_filter_create: jest.fn(),
    mcp_filter_create_builtin: jest.fn(),
    mcp_filter_retain: jest.fn(),
    mcp_filter_release: jest.fn(),
    mcp_filter_chain_builder_create: jest.fn(),
    mcp_filter_chain_add_filter: jest.fn(),
    mcp_filter_chain_build: jest.fn(),
    mcp_filter_chain_builder_destroy: jest.fn(),
    mcp_filter_manager_create: jest.fn(),
    mcp_filter_manager_add_filter: jest.fn(),
    mcp_filter_manager_initialize: jest.fn(),
    mcp_filter_manager_release: jest.fn(),
  },
}));

import { mcpFilterLib } from "../ffi-bindings";

describe("Filter API", () => {
  beforeEach(() => {
    jest.clearAllMocks();
  });

  describe("Filter Lifecycle Management", () => {
    it("should create a filter", () => {
      const mockFilterHandle = 12345;
      (mcpFilterLib.mcp_filter_create as jest.Mock).mockReturnValue(
        mockFilterHandle
      );

      const config = {
        name: "test-filter",
        type: BuiltinFilterType.HTTP_CODEC,
        settings: { port: 8080 },
        layer: 7,
        memoryPool: null,
      };

      const result = createFilter(0, config);

      expect(result).toBe(mockFilterHandle);
      expect(mcpFilterLib.mcp_filter_create).toHaveBeenCalledWith(0, config);
    });

    it("should create a built-in filter", () => {
      const mockFilterHandle = 67890;
      (mcpFilterLib.mcp_filter_create_builtin as jest.Mock).mockReturnValue(
        mockFilterHandle
      );

      const result = createBuiltinFilter(0, BuiltinFilterType.TCP_PROXY, {
        port: 8080,
      });

      expect(result).toBe(mockFilterHandle);
      expect(mcpFilterLib.mcp_filter_create_builtin).toHaveBeenCalledWith(
        0,
        BuiltinFilterType.TCP_PROXY,
        { port: 8080 }
      );
    });

    it("should retain and release filters", () => {
      const filterHandle = 12345;

      retainFilter(filterHandle);
      expect(mcpFilterLib.mcp_filter_retain).toHaveBeenCalledWith(filterHandle);

      releaseFilter(filterHandle);
      expect(mcpFilterLib.mcp_filter_release).toHaveBeenCalledWith(
        filterHandle
      );
    });
  });

  describe("Filter Chain Management", () => {
    it("should create a filter chain builder", () => {
      const mockBuilder = { id: "builder-1" };
      (
        mcpFilterLib.mcp_filter_chain_builder_create as jest.Mock
      ).mockReturnValue(mockBuilder);

      const result = createFilterChainBuilder(0);

      expect(result).toBe(mockBuilder);
      expect(mcpFilterLib.mcp_filter_chain_builder_create).toHaveBeenCalledWith(
        0
      );
    });

    it("should add filter to chain", () => {
      const builder = { id: "builder-1" };
      const filterHandle = 12345;
      const position = FilterPosition.FIRST;

      (mcpFilterLib.mcp_filter_chain_add_filter as jest.Mock).mockReturnValue(
        0
      );

      const result = addFilterToChain(builder, filterHandle, position);

      expect(result).toBe(0);
      expect(mcpFilterLib.mcp_filter_chain_add_filter).toHaveBeenCalledWith(
        builder,
        filterHandle,
        position,
        0
      );
    });

    it("should build filter chain", () => {
      const builder = { id: "builder-1" };
      const mockChainHandle = 99999;

      (mcpFilterLib.mcp_filter_chain_build as jest.Mock).mockReturnValue(
        mockChainHandle
      );

      const result = buildFilterChain(builder);

      expect(result).toBe(mockChainHandle);
      expect(mcpFilterLib.mcp_filter_chain_build).toHaveBeenCalledWith(builder);
    });

    it("should destroy filter chain builder", () => {
      const builder = { id: "builder-1" };

      destroyFilterChainBuilder(builder);

      expect(
        mcpFilterLib.mcp_filter_chain_builder_destroy
      ).toHaveBeenCalledWith(builder);
    });
  });

  describe("Filter Manager", () => {
    it("should create filter manager", () => {
      const mockManagerHandle = 11111;
      (mcpFilterLib.mcp_filter_manager_create as jest.Mock).mockReturnValue(
        mockManagerHandle
      );

      const result = createFilterManager(123, 456);

      expect(result).toBe(mockManagerHandle);
      expect(mcpFilterLib.mcp_filter_manager_create).toHaveBeenCalledWith(
        123,
        456
      );
    });

    it("should add filter to manager", () => {
      const managerHandle = 11111;
      const filterHandle = 12345;

      (mcpFilterLib.mcp_filter_manager_add_filter as jest.Mock).mockReturnValue(
        0
      );

      const result = addFilterToManager(managerHandle, filterHandle);

      expect(result).toBe(0);
      expect(mcpFilterLib.mcp_filter_manager_add_filter).toHaveBeenCalledWith(
        managerHandle,
        filterHandle
      );
    });

    it("should initialize filter manager", () => {
      const managerHandle = 11111;

      (mcpFilterLib.mcp_filter_manager_initialize as jest.Mock).mockReturnValue(
        0
      );

      const result = initializeFilterManager(managerHandle);

      expect(result).toBe(0);
      expect(mcpFilterLib.mcp_filter_manager_initialize).toHaveBeenCalledWith(
        managerHandle
      );
    });

    it("should release filter manager", () => {
      const managerHandle = 11111;

      releaseFilterManager(managerHandle);

      expect(mcpFilterLib.mcp_filter_manager_release).toHaveBeenCalledWith(
        managerHandle
      );
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
