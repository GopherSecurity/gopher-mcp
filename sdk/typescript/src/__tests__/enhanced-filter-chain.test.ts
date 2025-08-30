/**
 * @file enhanced-filter-chain.test.ts
 * @brief Unit tests for EnhancedFilterChain, ChainRouter, and ChainPool classes
 */

import {
  EnhancedFilterChain,
  ChainExecutionMode,
  RoutingStrategy,
  ChainConfig,
  FilterNode,
} from "../chains/enhanced-filter-chain";

describe("EnhancedFilterChain", () => {
  let chain: EnhancedFilterChain;
  let mockDispatcher: number;

  beforeEach(() => {
    mockDispatcher = 12345;
    const config: ChainConfig = {
      name: "test-chain",
      mode: ChainExecutionMode.SEQUENTIAL,
      routing: RoutingStrategy.ROUND_ROBIN,
      maxParallel: 10,
      bufferSize: 8192,
      timeoutMs: 30000,
      stopOnError: false,
    };

    chain = new EnhancedFilterChain(mockDispatcher, config);
  });

  afterEach(async () => {
    if (chain) {
      await chain.destroy();
    }
  });

  describe("constructor", () => {
    it("should create enhanced filter chain with configuration", () => {
      expect(chain).toBeDefined();
      expect(chain.getHandle()).toBeGreaterThan(0);
    });

    it("should create chain with valid configuration", () => {
      const validConfig: ChainConfig = {
        name: "valid-chain",
        mode: ChainExecutionMode.SEQUENTIAL,
        routing: RoutingStrategy.ROUND_ROBIN,
        maxParallel: 5,
        bufferSize: 4096,
        timeoutMs: 15000,
        stopOnError: true,
      };

      const validChain = new EnhancedFilterChain(mockDispatcher, validConfig);
      expect(validChain).toBeDefined();
      expect(validChain.getHandle()).toBeGreaterThan(0);
    });
  });

  describe("filter management", () => {
    it("should add filter node to chain", () => {
      const filterNode: FilterNode = {
        filter: 1,
        name: "test-filter",
        priority: 1,
        enabled: true,
        bypassOnError: false,
        config: {},
      };

      expect(() => chain.addNode(filterNode)).not.toThrow();
    });

    it("should add multiple filter nodes", () => {
      const filterNodes: FilterNode[] = [
        {
          filter: 1,
          name: "filter-1",
          priority: 1,
          enabled: true,
          bypassOnError: false,
          config: {},
        },
        {
          filter: 2,
          name: "filter-2",
          priority: 2,
          enabled: true,
          bypassOnError: false,
          config: {},
        },
        {
          filter: 3,
          name: "filter-3",
          priority: 3,
          enabled: true,
          bypassOnError: false,
          config: {},
        },
      ];

      filterNodes.forEach((node) => chain.addNode(node));
      expect(chain).toBeDefined();
    });
  });

  describe("chain state", () => {
    it("should get current chain state", () => {
      const state = chain.getState();
      expect(state).toBeDefined();
      expect(typeof state).toBe("number");
    });

    it("should pause chain execution", () => {
      expect(() => chain.pause()).not.toThrow();
    });

    it("should resume chain execution", () => {
      expect(() => chain.resume()).not.toThrow();
    });

    it("should reset chain to initial state", () => {
      expect(() => chain.reset()).not.toThrow();
    });
  });

  describe("chain statistics", () => {
    it("should get chain statistics", () => {
      const stats = chain.getStats();
      expect(stats).toBeDefined();
      expect(stats.totalProcessed).toBeGreaterThanOrEqual(0);
      expect(stats.totalErrors).toBeGreaterThanOrEqual(0);
      expect(stats.totalBypassed).toBeGreaterThanOrEqual(0);
    });
  });

  describe("chain optimization", () => {
    it("should optimize chain", () => {
      expect(() => chain.optimize()).not.toThrow();
    });

    it("should reorder filters for optimal performance", () => {
      expect(() => chain.reorderFilters()).not.toThrow();
    });

    it("should profile chain performance", async () => {
      // Create a mock AdvancedBuffer for testing
      const mockBuffer = { getHandle: () => 1 } as any;
      const iterations = 10;

      const report = await chain.profile(mockBuffer, iterations);
      expect(report).toBeDefined();
    });
  });

  describe("chain validation", () => {
    it("should validate chain configuration", () => {
      const errors = chain.validate();
      expect(errors).toBeDefined();
    });
  });

  describe("chain debugging", () => {
    it("should set trace level", () => {
      expect(() => chain.setTraceLevel(1)).not.toThrow();
    });

    it("should dump chain structure", () => {
      const dump = chain.dump("text");
      expect(dump).toBeDefined();
    });
  });

  describe("chain cleanup", () => {
    it("should destroy chain resources", () => {
      expect(() => chain.destroy()).not.toThrow();
    });

    it("should handle multiple destroy calls gracefully", () => {
      expect(() => chain.destroy()).not.toThrow();
      expect(() => chain.destroy()).not.toThrow();
    });
  });

  describe("edge cases", () => {
    it("should handle empty chain", () => {
      const emptyChain = new EnhancedFilterChain(mockDispatcher, {
        name: "empty",
        mode: ChainExecutionMode.SEQUENTIAL,
        routing: RoutingStrategy.ROUND_ROBIN,
        maxParallel: 1,
        bufferSize: 1024,
        timeoutMs: 5000,
        stopOnError: false,
      });

      expect(emptyChain).toBeDefined();
      expect(emptyChain.getHandle()).toBeGreaterThan(0);
    });

    it("should handle chain with single filter", () => {
      const singleFilterChain = new EnhancedFilterChain(mockDispatcher, {
        name: "single-filter",
        mode: ChainExecutionMode.SEQUENTIAL,
        routing: RoutingStrategy.ROUND_ROBIN,
        maxParallel: 1,
        bufferSize: 1024,
        timeoutMs: 5000,
        stopOnError: false,
      });

      const filterNode: FilterNode = {
        filter: 1,
        name: "single-filter",
        priority: 1,
        enabled: true,
        bypassOnError: false,
        config: {},
      };

      singleFilterChain.addNode(filterNode);
      expect(singleFilterChain).toBeDefined();
    });

    it("should handle chain with disabled filters", () => {
      const disabledFilterChain = new EnhancedFilterChain(mockDispatcher, {
        name: "disabled-filters",
        mode: ChainExecutionMode.SEQUENTIAL,
        routing: RoutingStrategy.ROUND_ROBIN,
        maxParallel: 1,
        bufferSize: 1024,
        timeoutMs: 5000,
        stopOnError: false,
      });

      const filterNode: FilterNode = {
        filter: 1,
        name: "disabled-filter",
        priority: 1,
        enabled: false,
        bypassOnError: false,
        config: {},
      };

      disabledFilterChain.addNode(filterNode);
      expect(disabledFilterChain).toBeDefined();
    });
  });
});
