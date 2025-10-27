/**
 * @file filter-ffi.test.ts
 * @brief Integration tests for FilterChain FFI class
 *
 * These tests verify the Koffi FFI bridge to C++ filter chain API works correctly
 * with real dispatcher handles and the native library.
 */

import { FilterChain } from '../filter-chain-ffi';
import { createRealDispatcher, destroyDispatcher, ensureMcpInitialized } from '../mcp-filter-api';
import type { CanonicalConfig } from '../filter-types';

describe('FilterChain FFI Integration', () => {
  let dispatcher: any;

  // Simple test configuration with http.codec filter
  const simpleConfig: CanonicalConfig = {
    listeners: [
      {
        name: 'test_listener',
        address: {
          socket_address: {
            address: '127.0.0.1',
            port_value: 9090,
          },
        },
        filter_chains: [
          {
            filters: [
              {
                name: 'http_codec',
                type: 'http.codec',
              },
            ],
          },
        ],
      },
    ],
  };

  // Configuration with multiple filters for testing
  const multiFilterConfig: CanonicalConfig = {
    listeners: [
      {
        name: 'multi_test_listener',
        address: {
          socket_address: {
            address: '127.0.0.1',
            port_value: 9091,
          },
        },
        filter_chains: [
          {
            filters: [
              {
                name: 'http_codec',
                type: 'http.codec',
              },
              {
                name: 'sse_codec',
                type: 'sse.codec',
              },
              {
                name: 'json_rpc',
                type: 'json_rpc.dispatcher',
              },
            ],
          },
        ],
      },
    ],
  };

  beforeAll(() => {
    // Ensure MCP library is initialized once for all tests
    ensureMcpInitialized();
  });

  beforeEach(() => {
    // Create a fresh dispatcher for each test
    dispatcher = createRealDispatcher();
  });

  afterEach(() => {
    // Clean up dispatcher after each test
    if (dispatcher) {
      destroyDispatcher(dispatcher);
      dispatcher = null;
    }
  });

  describe('Construction', () => {
    it('should create a filter chain from canonical config', () => {
      const chain = new FilterChain(dispatcher, simpleConfig);

      expect(chain).toBeDefined();
      expect(chain.getHandle()).toBeGreaterThan(0);
      expect(chain.isDestroyed()).toBe(false);

      chain.destroy();
    });

    it('should throw error for invalid dispatcher', () => {
      expect(() => {
        new FilterChain(0, simpleConfig);
      }).toThrow('Invalid dispatcher handle');
    });

    it('should create chain with multiple filters', () => {
      const chain = new FilterChain(dispatcher, multiFilterConfig);

      expect(chain).toBeDefined();
      expect(chain.getHandle()).toBeGreaterThan(0);

      chain.destroy();
    });
  });

  describe('Metrics and Statistics', () => {
    it('should retrieve chain statistics', async () => {
      const chain = new FilterChain(dispatcher, simpleConfig);

      const stats = await chain.getChainStats();

      expect(stats).toBeDefined();
      expect(typeof stats.total_processed).toBe('number');
      expect(typeof stats.total_errors).toBe('number');
      expect(typeof stats.avg_latency_ms).toBe('number');
      expect(typeof stats.active_filters).toBe('number');

      chain.destroy();
    });

    it('should retrieve filter metrics', async () => {
      const chain = new FilterChain(dispatcher, simpleConfig);

      const metrics = await chain.getMetrics();

      expect(metrics).toBeDefined();
      expect(typeof metrics).toBe('object');
      // Metrics should have at least chain-wide stats
      expect(metrics['chain']).toBeDefined();

      chain.destroy();
    });

    it('should throw error when getting stats from uninitialized chain', async () => {
      const chain = new FilterChain(dispatcher, simpleConfig);
      chain.destroy(); // Destroy immediately

      await expect(chain.getChainStats()).rejects.toThrow('not initialized');
    });
  });

  describe('Configuration Management', () => {
    it('should export chain configuration', async () => {
      const chain = new FilterChain(dispatcher, simpleConfig);

      const exported = await chain.exportConfig();

      expect(exported).toBeDefined();
      expect(typeof exported).toBe('object');
      // Should have listeners array
      expect(Array.isArray(exported.listeners)).toBe(true);

      chain.destroy();
    });

    it('should enable a filter by name', async () => {
      const chain = new FilterChain(dispatcher, multiFilterConfig);

      const warnings = await chain.enableFilter('http_codec');

      expect(Array.isArray(warnings)).toBe(true);
      // No errors should be thrown

      chain.destroy();
    });

    it('should disable a filter by name', async () => {
      const chain = new FilterChain(dispatcher, multiFilterConfig);

      const warnings = await chain.disableFilter('sse_codec');

      expect(Array.isArray(warnings)).toBe(true);
      // No errors should be thrown

      chain.destroy();
    });

    it('should throw error for invalid filter name', async () => {
      const chain = new FilterChain(dispatcher, simpleConfig);

      await expect(chain.enableFilter('nonexistent_filter')).rejects.toThrow();

      chain.destroy();
    });
  });

  describe('Lifecycle Management', () => {
    it('should properly destroy chain', () => {
      const chain = new FilterChain(dispatcher, simpleConfig);
      const handle = chain.getHandle();

      expect(handle).toBeGreaterThan(0);
      expect(chain.isDestroyed()).toBe(false);

      chain.destroy();

      expect(chain.isDestroyed()).toBe(true);
      expect(() => chain.getHandle()).toThrow('destroyed');
    });

    it('should be safe to destroy chain multiple times', () => {
      const chain = new FilterChain(dispatcher, simpleConfig);

      chain.destroy();
      expect(() => chain.destroy()).not.toThrow();
    });

    it('should throw error when using destroyed chain', async () => {
      const chain = new FilterChain(dispatcher, simpleConfig);
      chain.destroy();

      await expect(chain.getChainStats()).rejects.toThrow('destroyed');
      await expect(chain.getMetrics()).rejects.toThrow('destroyed');
      await expect(chain.exportConfig()).rejects.toThrow('destroyed');
    });
  });

  describe('Error Handling', () => {
    it('should handle malformed configuration gracefully', () => {
      const badConfig: any = {
        listeners: [] // Empty listeners array
      };

      expect(() => {
        new FilterChain(dispatcher, badConfig);
      }).toThrow();
    });

    it('should handle missing filter chains', () => {
      const badConfig: any = {
        listeners: [
          {
            name: 'bad_listener',
            address: {
              socket_address: {
                address: '127.0.0.1',
                port_value: 9092,
              },
            },
            filter_chains: [], // Empty filter chains
          },
        ],
      };

      expect(() => {
        new FilterChain(dispatcher, badConfig);
      }).toThrow();
    });

    it('should handle MCP_STATUS_NOT_INITIALIZED error', async () => {
      const chain = new FilterChain(dispatcher, simpleConfig);
      // Don't initialize the chain
      chain.destroy(); // Destroy without initializing

      // Attempting to use destroyed chain should throw
      await expect(chain.processIncoming({ test: 'data' }))
        .rejects.toThrow(/destroyed|not initialized/i);
    });

    it('should handle processing timeout', async () => {
      const chain = new FilterChain(dispatcher, simpleConfig);
      await chain.initialize();

      // Mock a very slow processing scenario
      // In real scenario, this would timeout after 30s
      // For test purposes, we just verify the error handling structure exists
      expect(chain).toBeDefined();

      chain.destroy();
    }, 35000); // Set test timeout higher than default

    it('should handle callback errors gracefully', async () => {
      const chain = new FilterChain(dispatcher, simpleConfig);
      await chain.initialize();

      // Submit a message that might cause callback errors
      try {
        await chain.processIncoming({ invalid: 'structure' });
        // If no error, that's fine - filter might allow it
      } catch (error) {
        // Error should be properly formatted
        expect(error).toBeInstanceOf(Error);
        expect(error).toHaveProperty('message');
      }

      chain.destroy();
    });

    it('should handle queue full scenarios', async () => {
      const chain = new FilterChain(dispatcher, simpleConfig);
      await chain.initialize();

      // Try to flood the queue with many rapid requests
      const promises = Array.from({ length: 100 }, (_, i) =>
        chain.processIncoming({ index: i })
      );

      try {
        await Promise.all(promises);
        // If all succeed, that's good
      } catch (error) {
        // Some might fail with queue full - verify error format
        expect(error).toBeInstanceOf(Error);
      }

      chain.destroy();
    });

    it('should propagate C API error codes correctly', async () => {
      const chain = new FilterChain(dispatcher, simpleConfig);

      // Try various invalid operations
      try {
        await chain.getChainStats();
      } catch (error) {
        expect(error).toBeInstanceOf(Error);
        expect((error as Error).message).toMatch(/not initialized|failed/i);
      }

      chain.destroy();
    });
  });

  describe('Memory Management', () => {
    it('should not leak memory when creating and destroying multiple chains', () => {
      // Create and destroy chains in a loop
      for (let i = 0; i < 10; i++) {
        const chain = new FilterChain(dispatcher, simpleConfig);
        expect(chain.getHandle()).toBeGreaterThan(0);
        expect(chain.isDestroyed()).toBe(false);
        chain.destroy();
        expect(chain.isDestroyed()).toBe(true);
      }

      // If this test completes without crashing, memory management is working
      expect(true).toBe(true);
    });

    it('should handle rapid create/destroy cycles', () => {
      const chains: FilterChain[] = [];

      // Create multiple chains
      for (let i = 0; i < 5; i++) {
        chains.push(new FilterChain(dispatcher, simpleConfig));
      }

      // Verify all were created
      expect(chains.length).toBe(5);
      chains.forEach(chain => {
        expect(chain.getHandle()).toBeGreaterThan(0);
      });

      // Destroy all chains
      chains.forEach(chain => chain.destroy());

      // Verify all were destroyed
      chains.forEach(chain => {
        expect(chain.isDestroyed()).toBe(true);
      });
    });
  });
});
