/**
 * @file filter-dispatcher.ts
 * @brief Dispatcher helper functions for Hybrid SDK
 *
 * This module provides simplified wrapper functions for creating and managing
 * dispatchers in the Hybrid SDK approach. These dispatchers are needed for
 * the C++ filter chain to process messages asynchronously.
 *
 * ## Usage
 * ```typescript
 * import { createHybridDispatcher, destroyHybridDispatcher } from './filter-dispatcher';
 *
 * // Create dispatcher for filter chain
 * const dispatcher = createHybridDispatcher();
 *
 * // Use dispatcher with filter chain
 * const chain = new FilterChain(dispatcher, config);
 *
 * // Clean up when done
 * destroyHybridDispatcher(dispatcher);
 * ```
 */

import { createRealDispatcher, destroyDispatcher } from './mcp-filter-api';

/**
 * Create a new dispatcher for hybrid SDK filter chains
 *
 * In the hybrid SDK approach, the official MCP SDK handles the main protocol and transport,
 * but we still need a dispatcher for the C++ filter chain to process messages
 * asynchronously. This dispatcher runs on a separate thread and handles filter
 * operations without interfering with the SDK's event loop.
 *
 * @returns Dispatcher handle (opaque pointer)
 * @throws Error if dispatcher creation fails
 *
 * @example
 * ```typescript
 * const dispatcher = createHybridDispatcher();
 * const transport = new GopherFilteredTransport(stdioTransport, {
 *   dispatcherHandle: dispatcher,
 *   filterConfig: myConfig
 * });
 * ```
 */
export function createHybridDispatcher(): number {
  const handle = createRealDispatcher();

  if (!handle || handle === 0) {
    throw new Error('Failed to create hybrid dispatcher');
  }

  return handle;
}

/**
 * Destroy a hybrid dispatcher and release resources
 *
 * This should be called when shutting down the application to ensure
 * clean cleanup of the dispatcher thread and associated resources.
 *
 * @param handle - Dispatcher handle from createHybridDispatcher()
 *
 * @example
 * ```typescript
 * const shutdown = async () => {
 *   await transport.close();
 *   destroyHybridDispatcher(dispatcher);
 * };
 * ```
 */
export function destroyHybridDispatcher(handle: number): void {
  if (!handle || handle === 0) {
    console.warn('Attempted to destroy invalid dispatcher handle');
    return;
  }

  destroyDispatcher(handle);
}
