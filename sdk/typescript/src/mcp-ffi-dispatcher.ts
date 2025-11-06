/**
 * @file mcp-ffi-dispatcher.ts
 * @brief Dispatcher class wrapper for filter chain tests
 *
 * This module provides a Dispatcher class that wraps the FFI dispatcher
 * handle for easier use in tests and examples.
 */

import { createHybridDispatcher, destroyHybridDispatcher } from './filter-dispatcher';

/**
 * Dispatcher class for managing C++ filter chain dispatchers
 *
 * This class provides a convenient object-oriented wrapper around the
 * low-level dispatcher handle, with automatic resource cleanup.
 *
 * @example
 * ```typescript
 * const dispatcher = new Dispatcher();
 * const chain = new FilterChain(dispatcher.handle, config);
 * // ... use chain ...
 * dispatcher.destroy();
 * ```
 */
export class Dispatcher {
  private _handle: number;
  private _destroyed: boolean = false;
  private _name?: string;

  /**
   * Create a new dispatcher
   * @param name - Optional name for the dispatcher (for debugging)
   * @throws Error if dispatcher creation fails
   */
  constructor(name?: string) {
    if (name) {
      this._name = name;
    }
    this._handle = createHybridDispatcher();
  }

  /**
   * Get the underlying FFI handle
   */
  get handle(): number {
    if (this._destroyed) {
      throw new Error('Cannot access handle of destroyed dispatcher');
    }
    return this._handle;
  }

  /**
   * Get the underlying FFI handle (method form for compatibility)
   */
  getHandle(): number {
    return this.handle;
  }

  /**
   * Check if the dispatcher has been destroyed (property form)
   */
  get destroyed(): boolean {
    return this._destroyed;
  }

  /**
   * Check if the dispatcher has been destroyed (method form for compatibility)
   */
  isDestroyed(): boolean {
    return this._destroyed;
  }

  /**
   * Get the dispatcher name
   */
  get name(): string | undefined {
    return this._name;
  }

  /**
   * Destroy the dispatcher and release resources
   *
   * This method is idempotent - calling it multiple times is safe.
   */
  destroy(): void {
    if (!this._destroyed) {
      destroyHybridDispatcher(this._handle);
      this._destroyed = true;
    }
  }

  /**
   * Run cleanup when the object is garbage collected
   *
   * Note: This is a backup mechanism. Explicit destroy() calls are preferred.
   */
  [Symbol.dispose](): void {
    this.destroy();
  }
}
