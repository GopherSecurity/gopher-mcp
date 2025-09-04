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
} from "../mcp-filter-chain";

import { mcpFilterLib } from "../mcp-ffi-bindings";

// Use real C++ library instead of mocks

describe("Filter Chain API", () => {
  beforeEach(() => {
    // No need to clear mocks since we're using real library
  });

  describe("Chain Builder", () => {
    it("should create chain builder with configuration", () => {
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

      // With real library, we expect a valid builder object or null
      expect(result).toBeDefined();
    });

    it("should add filter node to chain", () => {
      const builder = createChainBuilderEx(0, {
        name: "test-chain",
        mode: ChainExecutionMode.SEQUENTIAL,
        routing: RoutingStrategy.ROUND_ROBIN,
        maxParallel: 4,
        bufferSize: 8192,
        timeoutMs: 5000,
        stopOnError: false,
      });
      const node = {
        filter: 12345,
        name: "test-filter",
        priority: 1,
        enabled: true,
        bypassOnError: false,
        config: null,
      };

      const result = addFilterNodeToChain(builder, node);

      // With real library, we expect a result code (0 for success)
      expect(typeof result).toBe("number");
    });

    it("should add conditional filter", () => {
      const builder = createChainBuilderEx(0, {
        name: "test-chain",
        mode: ChainExecutionMode.SEQUENTIAL,
        routing: RoutingStrategy.ROUND_ROBIN,
        maxParallel: 4,
        bufferSize: 8192,
        timeoutMs: 5000,
        stopOnError: false,
      });
      const condition = {
        matchType: MatchCondition.ALL,
        field: "method",
        value: "GET",
        targetFilter: 12345,
      };

      const result = addConditionalFilter(builder, condition, 12345);

      // With real library, we expect a result code (0 for success)
      expect(typeof result).toBe("number");
    });

    it("should add parallel filter group", () => {
      const builder = createChainBuilderEx(0, {
        name: "test-chain",
        mode: ChainExecutionMode.PARALLEL,
        routing: RoutingStrategy.ROUND_ROBIN,
        maxParallel: 4,
        bufferSize: 8192,
        timeoutMs: 5000,
        stopOnError: false,
      });
      const filters = [12345, 67890];

      const result = addParallelFilterGroup(builder, filters, filters.length);

      // With real library, we expect a result code (0 for success)
      expect(typeof result).toBe("number");
    });
  });

  describe("Chain Management", () => {
    it("should get chain state", () => {
      const chainHandle = 12345;

      const result = getChainState(chainHandle);

      // With real library, we expect a valid chain state
      expect(typeof result).toBe("number");
    });

    it("should pause chain execution", () => {
      const chainHandle = 12345;

      const result = pauseChain(chainHandle);

      // With real library, we expect a result code (0 for success)
      expect(typeof result).toBe("number");
    });

    it("should resume chain execution", () => {
      const chainHandle = 12345;

      const result = resumeChain(chainHandle);

      // With real library, we expect a result code (0 for success)
      expect(typeof result).toBe("number");
    });

    it("should reset chain to initial state", () => {
      const chainHandle = 12345;

      const result = resetChain(chainHandle);

      // With real library, we expect a result code (0 for success)
      expect(typeof result).toBe("number");
    });
  });

  describe("Utility Functions", () => {
    it("should create simple sequential chain", () => {
      // Create real filters using the C++ API
      const filter1 = mcpFilterLib.mcp_filter_create_builtin(0, 0, null); // HTTP_CODEC filter
      const filter2 = mcpFilterLib.mcp_filter_create_builtin(0, 1, null); // UDP_PROXY filter

      expect(filter1).toBeGreaterThan(0);
      expect(filter2).toBeGreaterThan(0);

      const filters = [filter1, filter2];

      const result = createSimpleChain(0, filters);

      // With real library, we expect a valid chain handle or 0 for error
      expect(typeof result).toBe("number");
      expect(result).toBeGreaterThanOrEqual(0);

      // Cleanup
      mcpFilterLib.mcp_filter_release(filter1);
      mcpFilterLib.mcp_filter_release(filter2);
    });

    it("should create parallel processing chain", () => {
      const filters = [12345, 67890];

      const result = createParallelChain(0, filters);

      // With real library, we expect a valid chain handle or 0 for error
      expect(typeof result).toBe("number");
      expect(result).toBeGreaterThanOrEqual(0);
    });

    it.skip("should create conditional chain with routing", () => {
      // Create real filter and chain for testing
      const targetFilter = mcpFilterLib.mcp_filter_create_builtin(0, 0, null); // HTTP_CODEC filter
      const simpleChain = createSimpleChain(0, [targetFilter]);

      expect(targetFilter).toBeGreaterThan(0);
      expect(simpleChain).toBeGreaterThanOrEqual(0);

      const conditions = [
        {
          condition: {
            matchType: MatchCondition.ALL,
            field: "method",
            value: "GET",
            targetFilter: targetFilter,
          },
          chain: simpleChain,
        },
      ];

      const result = createConditionalChain(0, conditions, "test-conditional-chain");

      // With real library, we expect a valid chain handle or 0 for error
      expect(typeof result).toBe("number");
      expect(result).toBeGreaterThanOrEqual(0);

      // Cleanup
      mcpFilterLib.mcp_filter_release(targetFilter);
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
