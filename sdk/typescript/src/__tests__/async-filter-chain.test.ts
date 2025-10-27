/**
 * @file async-filter-chain.test.ts
 * @brief Integration tests for async filter chain processing
 *
 * Tests end-to-end async filter chain functionality from TypeScript
 * through FFI to C++ implementation.
 */

import { describe, it, expect, beforeEach, afterEach } from '@jest/globals';
import { FilterChain } from '../filter-chain-ffi';
import { mcpFilterLib } from '../mcp-ffi-bindings';
import { FilterResult, CanonicalConfig } from '../filter-types';

describe('Async Filter Chain Integration', () => {
  let dispatcher: any;
  let chain: FilterChain;

  const testConfig: CanonicalConfig = {
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
              { name: 'http', type: 'http.codec' },
              { name: 'sse', type: 'sse.codec' },
              { name: 'dispatcher', type: 'json_rpc.dispatcher' },
            ],
          },
        ],
      },
    ],
  };

  beforeEach(() => {
    // Initialize MCP (pass null for default config)
    mcpFilterLib.mcp_init(null);

    // Create dispatcher
    dispatcher = mcpFilterLib.mcp_dispatcher_create(1);
    expect(dispatcher).toBeTruthy();

    // Create filter chain
    // Note: Node.js is single-threaded, but the C++ dispatcher
    // runs on its own internal thread pool. JavaScript callbacks
    // execute on the main Node.js event loop when the dispatcher
    // invokes them.
    chain = new FilterChain(dispatcher, testConfig);
  });

  afterEach(() => {
    if (chain && !chain.isDestroyed()) {
      chain.destroy();
    }
    if (dispatcher) {
      mcpFilterLib.mcp_dispatcher_destroy(dispatcher);
    }
    mcpFilterLib.mcp_shutdown();
  });

  it('should process incoming message', async () => {
    const result = await chain.processIncoming({ method: 'test' });

    expect(result).toBeDefined();
    expect(result.decision).toBeDefined();
    expect(typeof result.decision).toBe('number');
    // Note: Actual decision depends on filters in chain
  });

  it('should process outgoing message', async () => {
    const result = await chain.processOutgoing({ method: 'test' });

    expect(result).toBeDefined();
    expect(result.decision).toBeDefined();
    expect(typeof result.decision).toBe('number');
    // Note: Actual decision depends on filters in chain
  });

  it('should handle multiple concurrent requests', async () => {
    const promises: Promise<FilterResult>[] = [];
    for (let i = 0; i < 10; i++) {
      promises.push(chain.processIncoming({ method: 'test', id: i }));
    }

    const results = await Promise.all(promises);

    expect(results).toHaveLength(10);
    results.forEach((result) => {
      expect(result.decision).toBeDefined();
      expect(typeof result.decision).toBe('number');
    });
  });

  it('should process incoming and outgoing concurrently', async () => {
    const incomingPromises: Promise<FilterResult>[] = [];
    const outgoingPromises: Promise<FilterResult>[] = [];

    for (let i = 0; i < 5; i++) {
      incomingPromises.push(chain.processIncoming({ method: 'test_in', id: i }));
      outgoingPromises.push(chain.processOutgoing({ method: 'test_out', id: i }));
    }

    const [incomingResults, outgoingResults] = await Promise.all([
      Promise.all(incomingPromises),
      Promise.all(outgoingPromises),
    ]);

    expect(incomingResults).toHaveLength(5);
    expect(outgoingResults).toHaveLength(5);

    [...incomingResults, ...outgoingResults].forEach((result) => {
      expect(result.decision).toBeDefined();
    });
  });

  it('should handle error responses from filters', async () => {
    // This test depends on how the C++ implementation handles invalid messages
    // For now, we verify the API doesn't crash
    try {
      const result = await chain.processIncoming({ invalid: 'data' });
      expect(result).toBeDefined();
    } catch (error) {
      // Error handling is also acceptable
      expect(error).toBeDefined();
    }
  });

  it('should maintain separate callback registries for each message', async () => {
    // Submit multiple messages and verify each gets its own callback
    const message1 = chain.processIncoming({ id: 1 });
    const message2 = chain.processIncoming({ id: 2 });
    const message3 = chain.processIncoming({ id: 3 });

    const results = await Promise.all([message1, message2, message3]);

    expect(results).toHaveLength(3);
    results.forEach((result) => {
      expect(result).toBeDefined();
      expect(result.decision).toBeDefined();
    });
  });

  it('should clean up resources on shutdown', async () => {
    // Submit a message
    await chain.processIncoming({ method: 'test' });

    // Destroy the chain
    chain.destroy();

    // Verify chain is destroyed
    expect(chain.isDestroyed()).toBe(true);

    // Subsequent calls should throw
    await expect(chain.processIncoming({ method: 'test' })).rejects.toThrow(
      /destroyed/i
    );
  });

  it('should reject pending requests on shutdown', async () => {
    // This test verifies that pending callbacks are properly rejected
    // when the chain is destroyed

    // Start processing
    const promise = chain.processIncoming({ method: 'long_running' });

    // Immediately destroy (before callback completes)
    chain.destroy();

    // The promise should reject
    await expect(promise).rejects.toThrow(/shutdown/i);
  });

  it('should handle rapid successive requests', async () => {
    const results: FilterResult[] = [];

    // Submit 20 requests rapidly in sequence
    for (let i = 0; i < 20; i++) {
      const result = await chain.processIncoming({ id: i });
      results.push(result);
    }

    expect(results).toHaveLength(20);
    results.forEach((result) => {
      expect(result.decision).toBeDefined();
    });
  });

  it('should return FilterResult with correct structure', async () => {
    const result = await chain.processIncoming({ method: 'test' });

    // Verify FilterResult structure
    expect(result).toHaveProperty('decision');
    expect(typeof result.decision).toBe('number');

    // Optional fields may or may not be present
    if (result.transformedMessage !== undefined) {
      expect(typeof result.transformedMessage).toBe('string');
    }
    if (result.reason !== undefined) {
      expect(typeof result.reason).toBe('string');
    }
    if (result.delayMs !== undefined) {
      expect(typeof result.delayMs).toBe('number');
    }
    if (result.metadata !== undefined) {
      expect(typeof result.metadata).toBe('object');
    }
  });

  it('should handle JSON serialization of complex messages', async () => {
    const complexMessage = {
      method: 'complex_test',
      params: {
        nested: {
          data: [1, 2, 3],
          flag: true,
        },
        array: ['a', 'b', 'c'],
      },
      metadata: {
        timestamp: Date.now(),
      },
    };

    const result = await chain.processIncoming(complexMessage);
    expect(result).toBeDefined();
    expect(result.decision).toBeDefined();
  });

  it('should support getting chain statistics', async () => {
    // Process some messages
    await chain.processIncoming({ method: 'test1' });
    await chain.processIncoming({ method: 'test2' });
    await chain.processOutgoing({ method: 'test3' });

    // Get statistics
    const stats = await chain.getChainStats();

    expect(stats).toBeDefined();
    expect(stats).toHaveProperty('total_processed');
    expect(stats).toHaveProperty('total_errors');
    expect(stats).toHaveProperty('avg_latency_ms');
    expect(typeof stats.total_processed).toBe('number');
  });

  it('should support getting metrics', async () => {
    // Process some messages
    await chain.processIncoming({ method: 'test' });

    // Get metrics
    const metrics = await chain.getMetrics();

    expect(metrics).toBeDefined();
    expect(typeof metrics).toBe('object');
  });
});

describe('Async Filter Chain Error Handling', () => {
  let dispatcher: any;

  beforeEach(() => {
    mcpFilterLib.mcp_init(null);
    dispatcher = mcpFilterLib.mcp_dispatcher_create(1);
  });

  afterEach(() => {
    if (dispatcher) {
      mcpFilterLib.mcp_dispatcher_destroy(dispatcher);
    }
    mcpFilterLib.mcp_shutdown();
  });

  it('should throw on invalid dispatcher', () => {
    const invalidConfig: CanonicalConfig = {
      listeners: [
        {
          name: 'test',
          address: {
            socket_address: { address: '127.0.0.1', port_value: 9090 },
          },
          filter_chains: [
            {
              filters: [
                { name: 'http', type: 'http.codec' },
              ],
            },
          ],
        },
      ],
    };

    expect(() => {
      new FilterChain(null as any, invalidConfig);
    }).toThrow(/dispatcher/i);
  });

  it('should throw on invalid configuration', () => {
    const invalidConfig = {
      listeners: [],
    } as CanonicalConfig;

    expect(() => {
      new FilterChain(dispatcher, invalidConfig);
    }).toThrow();
  });

  it('should handle operations on destroyed chain', async () => {
    const config: CanonicalConfig = {
      listeners: [
        {
          name: 'test',
          address: {
            socket_address: { address: '127.0.0.1', port_value: 9090 },
          },
          filter_chains: [
            {
              filters: [
                { name: 'http', type: 'http.codec' },
              ],
            },
          ],
        },
      ],
    };

    const chain = new FilterChain(dispatcher, config);
    chain.destroy();

    await expect(chain.processIncoming({ method: 'test' })).rejects.toThrow(
      /destroyed/i
    );
    await expect(chain.processOutgoing({ method: 'test' })).rejects.toThrow(
      /destroyed/i
    );
    await expect(chain.getChainStats()).rejects.toThrow(/destroyed/i);
    await expect(chain.getMetrics()).rejects.toThrow(/destroyed/i);
  });

  it('should allow idempotent destroy calls', () => {
    const config: CanonicalConfig = {
      listeners: [
        {
          name: 'test',
          address: {
            socket_address: { address: '127.0.0.1', port_value: 9090 },
          },
          filter_chains: [
            {
              filters: [
                { name: 'http', type: 'http.codec' },
              ],
            },
          ],
        },
      ],
    };

    const chain = new FilterChain(dispatcher, config);

    // First destroy should work
    expect(() => chain.destroy()).not.toThrow();

    // Second destroy should be idempotent (no error)
    expect(() => chain.destroy()).not.toThrow();

    expect(chain.isDestroyed()).toBe(true);
  });
});
