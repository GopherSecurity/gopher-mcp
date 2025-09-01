/**
 * @file filter-chain.test.ts
 * @brief Tests for MCP Filter Chain API wrapper
 *
 * This test file covers the advanced filter chain functionality including:
 * - Chain execution modes
 * - Routing strategies
 * - Conditional execution
 * - Parallel processing
 * - Chain optimization
 */

import {
  addConditionalFilter,
  addFilterNodeToChain,
  addParallelFilterGroup,
  ChainExecutionMode,
  ChainState,
  createChainBuilderEx,
  createConditionalChain,
  createParallelChain,
  createSimpleChain,
  getChainState,
  MatchCondition,
  pauseChain,
  resetChain,
  resumeChain,
  RoutingStrategy,
} from "../filter-chain";

// Mock the FFI library
jest.mock("../ffi-bindings", () => ({
  mcpFilterLib: {
    mcp_chain_builder_create_ex: jest.fn(),
    mcp_chain_builder_add_node: jest.fn(),
    mcp_chain_builder_add_conditional: jest.fn(),
    mcp_chain_builder_add_parallel_group: jest.fn(),
    mcp_chain_get_state: jest.fn(),
    mcp_chain_pause: jest.fn(),
    mcp_chain_resume: jest.fn(),
    mcp_chain_reset: jest.fn(),
    mcp_filter_chain_build: jest.fn(),
    mcp_filter_chain_builder_destroy: jest.fn(),
  },
}));

import { mcpFilterLib } from "../ffi-bindings";

describe("Filter Chain API", () => {
  beforeEach(() => {
    jest.clearAllMocks();
  });

  describe("Chain Builder", () => {
    it("should create chain builder with configuration", () => {
      const mockBuilder = { id: "builder-1" };
      (mcpFilterLib.mcp_chain_builder_create_ex as jest.Mock).mockReturnValue(
        mockBuilder
      );

      const config = {
        name: "test-chain",
        mode: ChainExecutionMode.SEQUENTIAL,
        routing: RoutingStrategy.ROUND_ROBIN,
        maxParallel: 4,
        bufferSize: 8192,
        timeoutMs: 5000,
        stopOnError: false,
      };

      const result = createChainBuilderEx(0, config);

      expect(result).toBe(mockBuilder);
      expect(mcpFilterLib.mcp_chain_builder_create_ex).toHaveBeenCalledWith(
        0,
        config
      );
    });

    it("should add filter node to chain", () => {
      const builder = { id: "builder-1" };
      const node = {
        filter: 12345,
        name: "test-filter",
        priority: 1,
        enabled: true,
        bypassOnError: false,
        config: null,
      };

      (mcpFilterLib.mcp_chain_builder_add_node as jest.Mock).mockReturnValue(0);

      const result = addFilterNodeToChain(builder, node);

      expect(result).toBe(0);
      expect(mcpFilterLib.mcp_chain_builder_add_node).toHaveBeenCalledWith(
        builder,
        node
      );
    });

    it("should add conditional filter", () => {
      const builder = { id: "builder-1" };
      const condition = {
        matchType: MatchCondition.ALL,
        field: "method",
        value: "GET",
        targetFilter: 12345,
      };

      (
        mcpFilterLib.mcp_chain_builder_add_conditional as jest.Mock
      ).mockReturnValue(0);

      const result = addConditionalFilter(builder, condition, 12345);

      expect(result).toBe(0);
      expect(
        mcpFilterLib.mcp_chain_builder_add_conditional
      ).toHaveBeenCalledWith(builder, condition, 12345);
    });

    it("should add parallel filter group", () => {
      const builder = { id: "builder-1" };
      const filters = [12345, 67890];

      (
        mcpFilterLib.mcp_chain_builder_add_parallel_group as jest.Mock
      ).mockReturnValue(0);

      const result = addParallelFilterGroup(builder, filters, filters.length);

      expect(result).toBe(0);
      expect(
        mcpFilterLib.mcp_chain_builder_add_parallel_group
      ).toHaveBeenCalledWith(builder, filters, filters.length);
    });
  });

  describe("Chain Management", () => {
    it("should get chain state", () => {
      const chainHandle = 12345;
      const mockState = ChainState.PROCESSING;

      (mcpFilterLib.mcp_chain_get_state as jest.Mock).mockReturnValue(
        mockState
      );

      const result = getChainState(chainHandle);

      expect(result).toBe(mockState);
      expect(mcpFilterLib.mcp_chain_get_state).toHaveBeenCalledWith(
        chainHandle
      );
    });

    it("should pause chain execution", () => {
      const chainHandle = 12345;

      (mcpFilterLib.mcp_chain_pause as jest.Mock).mockReturnValue(0);

      const result = pauseChain(chainHandle);

      expect(result).toBe(0);
      expect(mcpFilterLib.mcp_chain_pause).toHaveBeenCalledWith(chainHandle);
    });

    it("should resume chain execution", () => {
      const chainHandle = 12345;

      (mcpFilterLib.mcp_chain_resume as jest.Mock).mockReturnValue(0);

      const result = resumeChain(chainHandle);

      expect(result).toBe(0);
      expect(mcpFilterLib.mcp_chain_resume).toHaveBeenCalledWith(chainHandle);
    });

    it("should reset chain to initial state", () => {
      const chainHandle = 12345;

      (mcpFilterLib.mcp_chain_reset as jest.Mock).mockReturnValue(0);

      const result = resetChain(chainHandle);

      expect(result).toBe(0);
      expect(mcpFilterLib.mcp_chain_reset).toHaveBeenCalledWith(chainHandle);
    });
  });

  describe("Utility Functions", () => {
    it("should create simple sequential chain", () => {
      const mockBuilder = { id: "builder-1" };
      const mockChain = 99999;

      (mcpFilterLib.mcp_chain_builder_create_ex as jest.Mock).mockReturnValue(
        mockBuilder
      );
      (mcpFilterLib.mcp_chain_builder_add_node as jest.Mock).mockReturnValue(0);
      (mcpFilterLib.mcp_filter_chain_build as jest.Mock).mockReturnValue(
        mockChain
      );

      const filters = [12345, 67890];
      const result = createSimpleChain(0, filters, "test-chain");

      expect(result).toBe(mockChain);
      expect(mcpFilterLib.mcp_filter_chain_build).toHaveBeenCalledWith(
        mockBuilder
      );
      expect(
        mcpFilterLib.mcp_filter_chain_builder_destroy
      ).toHaveBeenCalledWith(mockBuilder);
    });

    it("should create parallel processing chain", () => {
      const mockBuilder = { id: "builder-1" };
      const mockChain = 99999;

      (mcpFilterLib.mcp_chain_builder_create_ex as jest.Mock).mockReturnValue(
        mockBuilder
      );
      (
        mcpFilterLib.mcp_chain_builder_add_parallel_group as jest.Mock
      ).mockReturnValue(0);
      (mcpFilterLib.mcp_filter_chain_build as jest.Mock).mockReturnValue(
        mockChain
      );

      const filters = [12345, 67890];
      const result = createParallelChain(0, filters, 2, "parallel-chain");

      expect(result).toBe(mockChain);
      expect(mcpFilterLib.mcp_filter_chain_build).toHaveBeenCalledWith(
        mockBuilder
      );
      expect(
        mcpFilterLib.mcp_filter_chain_builder_destroy
      ).toHaveBeenCalledWith(mockBuilder);
    });

    it("should create conditional chain with routing", () => {
      const mockBuilder = { id: "builder-1" };
      const mockChain = 99999;

      (mcpFilterLib.mcp_chain_builder_create_ex as jest.Mock).mockReturnValue(
        mockBuilder
      );
      (
        mcpFilterLib.mcp_chain_builder_add_conditional as jest.Mock
      ).mockReturnValue(0);
      (mcpFilterLib.mcp_filter_chain_build as jest.Mock).mockReturnValue(
        mockChain
      );

      const conditions = [
        {
          condition: {
            matchType: MatchCondition.ALL,
            field: "method",
            value: "GET",
            targetFilter: 12345,
          },
          chain: 12345,
        },
      ];

      const result = createConditionalChain(0, conditions, "conditional-chain");

      expect(result).toBe(mockChain);
      expect(mcpFilterLib.mcp_filter_chain_build).toHaveBeenCalledWith(
        mockBuilder
      );
      expect(
        mcpFilterLib.mcp_filter_chain_builder_destroy
      ).toHaveBeenCalledWith(mockBuilder);
    });
  });

  describe("Enums and Constants", () => {
    it("should have correct chain execution modes", () => {
      expect(ChainExecutionMode.SEQUENTIAL).toBe(0);
      expect(ChainExecutionMode.PARALLEL).toBe(1);
      expect(ChainExecutionMode.CONDITIONAL).toBe(2);
      expect(ChainExecutionMode.PIPELINE).toBe(3);
    });

    it("should have correct routing strategies", () => {
      expect(RoutingStrategy.ROUND_ROBIN).toBe(0);
      expect(RoutingStrategy.LEAST_LOADED).toBe(1);
      expect(RoutingStrategy.HASH_BASED).toBe(2);
      expect(RoutingStrategy.PRIORITY).toBe(3);
      expect(RoutingStrategy.CUSTOM).toBe(99);
    });

    it("should have correct match conditions", () => {
      expect(MatchCondition.ALL).toBe(0);
      expect(MatchCondition.ANY).toBe(1);
      expect(MatchCondition.NONE).toBe(2);
      expect(MatchCondition.CUSTOM).toBe(99);
    });

    it("should have correct chain states", () => {
      expect(ChainState.IDLE).toBe(0);
      expect(ChainState.PROCESSING).toBe(1);
      expect(ChainState.PAUSED).toBe(2);
      expect(ChainState.ERROR).toBe(3);
      expect(ChainState.COMPLETED).toBe(4);
    });
  });
});
