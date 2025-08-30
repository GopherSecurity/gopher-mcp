/**
 * @file enhanced-filter-chain.test.ts
 * @brief Unit tests for EnhancedFilterChain, ChainRouter, and ChainPool classes
 */

import {
  ChainConfig,
  ChainExecutionMode,
  ChainState,
  EnhancedFilterChain,
  FilterNode,
  RoutingStrategy,
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

  describe("construction and initialization", () => {
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

    it("should initialize with empty filter list", () => {
      expect(chain).toBeDefined();
      expect(chain.getHandle()).toBeGreaterThan(0);
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

    it("should add conditional filter", () => {
      const condition = {
        matchType: MatchCondition.ALL,
        field: "test-field",
        value: "test-value",
        targetFilter: 1,
      };

      expect(() => chain.addConditional(condition, 1)).not.toThrow();
    });

    it("should add parallel filter group", () => {
      const filters = [1, 2, 3];
      expect(() => chain.addParallelGroup(filters)).not.toThrow();
    });
      const filter2 = { id: 2, name: "filter-2" };
      const filter3 = { id: 3, name: "filter-3" };

      chain.addFilter(filter1);
      chain.addFilter(filter3);
      chain.insertFilter(filter2, 1);

      const filters = chain.getFilters();
      expect(filters[1]).toBe(filter2);
    });

    it("should replace filter in chain", () => {
      const oldFilter = { id: 1, name: "old-filter" };
      const newFilter = { id: 1, name: "new-filter" };

      chain.addFilter(oldFilter);
      chain.replaceFilter(1, newFilter);

      const filters = chain.getFilters();
      expect(filters[0]).toBe(newFilter);
    });
  });

  describe("chain execution", () => {
    beforeEach(() => {
      // Add some mock filters
      const filters = [
        { id: 1, name: "filter-1" },
        { id: 2, name: "filter-2" },
        { id: 3, name: "filter-3" },
      ];
      filters.forEach((filter) => chain.addFilter(filter));
    });

    it("should execute chain in sequential mode", () => {
      chain.config.executionMode = ChainExecutionMode.SEQUENTIAL;

      const result = chain.process(Buffer.from("test data"));
      expect(result).toBeDefined();
    });

    it("should execute chain in parallel mode", () => {
      chain.config.executionMode = ChainExecutionMode.PARALLEL;

      const result = chain.process(Buffer.from("test data"));
      expect(result).toBeDefined();
    });

    it("should execute chain in conditional mode", () => {
      chain.config.executionMode = ChainExecutionMode.CONDITIONAL;

      const result = chain.process(Buffer.from("test data"));
      expect(result).toBeDefined();
    });

    it("should execute chain in pipeline mode", () => {
      chain.config.executionMode = ChainExecutionMode.PIPELINE;

      const result = chain.process(Buffer.from("test data"));
      expect(result).toBeDefined();
    });
  });

  describe("chain state management", () => {
    it("should start chain", () => {
      expect(() => chain.start()).not.toThrow();
      expect(chain.getState()).toBe(ChainState.RUNNING);
    });

    it("should stop chain", () => {
      chain.start();

      expect(() => chain.stop()).not.toThrow();
      expect(chain.getState()).toBe(ChainState.STOPPED);
    });

    it("should pause chain", () => {
      chain.start();

      expect(() => chain.pause()).not.toThrow();
      expect(chain.getState()).toBe(ChainState.PAUSED);
    });

    it("should resume chain", () => {
      chain.start();
      chain.pause();

      expect(() => chain.resume()).not.toThrow();
      expect(chain.getState()).toBe(ChainState.RUNNING);
    });

    it("should reset chain", () => {
      chain.start();
      chain.stop();

      expect(() => chain.reset()).not.toThrow();
      expect(chain.getState()).toBe(ChainState.IDLE);
    });
  });

  describe("chain optimization", () => {
    it("should optimize chain when enabled", () => {
      chain.config.enableOptimization = true;

      const result = chain.optimize();
      expect(result).toBeDefined();
    });

    it("should reorder filters for better performance", () => {
      const filters = [
        { id: 1, name: "slow-filter", priority: 1 },
        { id: 2, name: "fast-filter", priority: 10 },
        { id: 3, name: "medium-filter", priority: 5 },
      ];

      filters.forEach((filter) => chain.addFilter(filter));

      const result = chain.reorderFilters();
      expect(result).toBeDefined();
    });

    it("should profile chain performance", () => {
      chain.config.enableProfiling = true;

      const result = chain.profile();
      expect(result).toBeDefined();
    });
  });

  describe("chain statistics", () => {
    it("should provide chain statistics", () => {
      const stats = chain.getStats();
      expect(stats).toBeDefined();
      expect(stats.filterCount).toBe(0);
      expect(stats.executionCount).toBe(0);
    });

    it("should track execution statistics", () => {
      chain.process(Buffer.from("test data"));
      chain.process(Buffer.from("more data"));

      const stats = chain.getStats();
      expect(stats.executionCount).toBe(2);
    });

    it("should reset statistics", () => {
      chain.process(Buffer.from("test data"));
      chain.resetStats();

      const stats = chain.getStats();
      expect(stats.executionCount).toBe(0);
    });
  });

  describe("chain validation", () => {
    it("should validate chain configuration", () => {
      const result = chain.validate();
      expect(result).toBeDefined();
    });

    it("should detect invalid configurations", () => {
      chain.config.maxFilters = -1;

      const result = chain.validate();
      expect(result.isValid).toBe(false);
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
});

describe("ChainRouter", () => {
  let router: ChainRouter;
  let mockChain: EnhancedFilterChain;

  beforeEach(() => {
    mockChain = new EnhancedFilterChain(12345, { name: "test-chain" });
    router = new ChainRouter(mockChain);
  });

  afterEach(() => {
    if (router) {
      router.destroy();
    }
    if (mockChain) {
      mockChain.destroy();
    }
  });

  describe("construction and initialization", () => {
    it("should create chain router", () => {
      expect(router).toBeDefined();
      expect(router.getChain()).toBe(mockChain);
    });

    it("should initialize with default routing strategy", () => {
      expect(router.getStrategy()).toBe(RoutingStrategy.ROUND_ROBIN);
    });
  });

  describe("route management", () => {
    it("should add route rule", () => {
      const rule: RouteRule = {
        condition: MatchCondition.MATCH_ALL,
        target: "target-chain",
        priority: 1,
      };

      router.addRoute(rule);

      const routes = router.getRoutes();
      expect(routes).toContain(rule);
    });

    it("should add multiple route rules", () => {
      const rules: RouteRule[] = [
        { condition: MatchCondition.MATCH_ALL, target: "chain-1", priority: 1 },
        { condition: MatchCondition.MATCH_ANY, target: "chain-2", priority: 2 },
        {
          condition: MatchCondition.MATCH_NONE,
          target: "chain-3",
          priority: 3,
        },
      ];

      rules.forEach((rule) => router.addRoute(rule));

      const routes = router.getRoutes();
      expect(routes).toHaveLength(3);
    });

    it("should remove route rule", () => {
      const rule: RouteRule = {
        condition: MatchCondition.MATCH_ALL,
        target: "target-chain",
        priority: 1,
      };

      router.addRoute(rule);
      router.removeRoute(rule);

      const routes = router.getRoutes();
      expect(routes).not.toContain(rule);
    });
  });

  describe("routing strategies", () => {
    it("should use round-robin strategy", () => {
      router.setStrategy(RoutingStrategy.ROUND_ROBIN);

      const result = router.route(Buffer.from("test data"));
      expect(result).toBeDefined();
    });

    it("should use least-loaded strategy", () => {
      router.setStrategy(RoutingStrategy.LEAST_LOADED);

      const result = router.route(Buffer.from("test data"));
      expect(result).toBeDefined();
    });

    it("should use hash-based strategy", () => {
      router.setStrategy(RoutingStrategy.HASH_BASED);

      const result = router.route(Buffer.from("test data"));
      expect(result).toBeDefined();
    });

    it("should use priority strategy", () => {
      router.setStrategy(RoutingStrategy.PRIORITY);

      const result = router.route(Buffer.from("test data"));
      expect(result).toBeDefined();
    });

    it("should use custom strategy", () => {
      router.setStrategy(RoutingStrategy.CUSTOM);

      const result = router.route(Buffer.from("test data"));
      expect(result).toBeDefined();
    });
  });

  describe("route execution", () => {
    beforeEach(() => {
      const rules: RouteRule[] = [
        { condition: MatchCondition.MATCH_ALL, target: "chain-1", priority: 1 },
        { condition: MatchCondition.MATCH_ANY, target: "chain-2", priority: 2 },
      ];
      rules.forEach((rule) => router.addRoute(rule));
    });

    it("should route data based on conditions", () => {
      const result = router.route(Buffer.from("test data"));
      expect(result).toBeDefined();
    });

    it("should handle multiple matching routes", () => {
      const result = router.route(Buffer.from("test data"));
      expect(result).toBeDefined();
    });

    it("should handle no matching routes", () => {
      router.clearRoutes();

      const result = router.route(Buffer.from("test data"));
      expect(result).toBeDefined();
    });
  });

  describe("router cleanup", () => {
    it("should destroy router resources", () => {
      expect(() => router.destroy()).not.toThrow();
    });
  });
});

describe("ChainPool", () => {
  let pool: ChainPool;
  let mockChain: EnhancedFilterChain;

  beforeEach(() => {
    mockChain = new EnhancedFilterChain(12345, { name: "base-chain" });
    pool = new ChainPool(mockChain, 5, RoutingStrategy.ROUND_ROBIN);
  });

  afterEach(() => {
    if (pool) {
      pool.destroy();
    }
    if (mockChain) {
      mockChain.destroy();
    }
  });

  describe("construction and initialization", () => {
    it("should create chain pool", () => {
      expect(pool).toBeDefined();
      expect(pool.getBaseChain()).toBe(mockChain);
      expect(pool.getPoolSize()).toBe(5);
    });

    it("should initialize with specified routing strategy", () => {
      expect(pool.getStrategy()).toBe(RoutingStrategy.ROUND_ROBIN);
    });
  });

  describe("chain management", () => {
    it("should get next available chain", () => {
      const chain = pool.getNext();
      expect(chain).toBeDefined();
    });

    it("should return chain to pool", () => {
      const chain = pool.getNext();
      expect(chain).toBeDefined();

      pool.return(chain);
      expect(pool.getAvailableCount()).toBe(5);
    });

    it("should handle multiple chain acquisitions", () => {
      const chains: EnhancedFilterChain[] = [];

      for (let i = 0; i < 3; i++) {
        const chain = pool.getNext();
        if (chain) chains.push(chain);
      }

      expect(chains).toHaveLength(3);
      expect(pool.getAvailableCount()).toBe(2);

      chains.forEach((chain) => pool.return(chain));
    });
  });

  describe("pool statistics", () => {
    it("should provide pool statistics", () => {
      const stats = pool.getStats();
      expect(stats).toBeDefined();
      expect(stats.totalChains).toBe(5);
      expect(stats.availableChains).toBe(5);
      expect(stats.usedChains).toBe(0);
    });

    it("should track chain usage", () => {
      const chain = pool.getNext();
      expect(chain).toBeDefined();

      const stats = pool.getStats();
      expect(stats.usedChains).toBe(1);
      expect(stats.availableChains).toBe(4);

      pool.return(chain);
    });
  });

  describe("pool optimization", () => {
    it("should resize pool", () => {
      pool.resize(10);
      expect(pool.getPoolSize()).toBe(10);
    });

    it("should trim pool", () => {
      pool.trim(3);
      expect(pool.getPoolSize()).toBe(3);
    });
  });

  describe("pool cleanup", () => {
    it("should destroy pool resources", () => {
      expect(() => pool.destroy()).not.toThrow();
    });
  });
});

describe("Utility Functions", () => {
  let mockDispatcher: number;

  beforeEach(() => {
    mockDispatcher = 12345;
  });

  describe("createChainFromJson", () => {
    it("should create chain from JSON configuration", () => {
      const jsonConfig = {
        name: "json-chain",
        executionMode: "sequential",
        maxFilters: 5,
      };

      const chain = createChainFromJson(mockDispatcher, jsonConfig);
      expect(chain).toBeDefined();
      expect(chain.config.name).toBe("json-chain");

      chain.destroy();
    });
  });

  describe("cloneChain", () => {
    it("should clone existing chain", () => {
      const originalChain = new EnhancedFilterChain(mockDispatcher, {
        name: "original",
      });
      const clonedChain = cloneChain(originalChain);

      expect(clonedChain).toBeDefined();
      expect(clonedChain.config.name).toBe("original");
      expect(clonedChain).not.toBe(originalChain);

      originalChain.destroy();
      clonedChain.destroy();
    });
  });

  describe("mergeChains", () => {
    it("should merge multiple chains", () => {
      const chain1 = new EnhancedFilterChain(mockDispatcher, {
        name: "chain-1",
      });
      const chain2 = new EnhancedFilterChain(mockDispatcher, {
        name: "chain-2",
      });

      const mergedChain = mergeChains([chain1, chain2], { name: "merged" });
      expect(mergedChain).toBeDefined();
      expect(mergedChain.config.name).toBe("merged");

      chain1.destroy();
      chain2.destroy();
      mergedChain.destroy();
    });
  });
});
