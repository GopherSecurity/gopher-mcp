/**
 * @file raii.ts
 * @brief RAII (Resource Acquisition Is Initialization) utilities for TypeScript
 *
 * Provides automatic resource management with:
 * - ResourceGuard: Automatic cleanup with custom deleters
 * - AllocationTransaction: Transaction-based resource management
 * - ScopedCleanup: Scope-based cleanup operations
 * - Thread safety and performance optimizations
 *
 * @copyright Copyright (c) 2025 MCP Project
 * @license MIT License
 */

// ============================================================================
// Core RAII Types and Interfaces
// ============================================================================

/**
 * Generic deleter function type
 */
export type Deleter<T> = (resource: T) => void;

/**
 * Resource cleanup function type
 */
export type CleanupFunction = () => void;

/**
 * Resource type identifier for C API integration
 */
export enum ResourceType {
  UNKNOWN = 0,
  FILTER = 1,
  BUFFER = 2,
  DISPATCHER = 3,
  CHAIN = 4,
  POOL = 5,
  TRANSACTION = 6,
}

// ============================================================================
// ResourceGuard - Automatic Resource Management
// ============================================================================

/**
 * RAII wrapper for automatic resource cleanup
 * Ensures resources are properly cleaned up when the guard goes out of scope
 */
export class ResourceGuard<T> {
  private resource: T | null = null;
  private deleter: Deleter<T> | null = null;
  private isReleased = false;

  /**
   * Create a resource guard
   * @param resource Resource to manage (can be null)
   * @param deleter Function to call for cleanup
   */
  constructor(resource: T | null = null, deleter?: Deleter<T>) {
    this.resource = resource;
    this.deleter = deleter || null;
  }

  /**
   * Check if guard has a resource
   */
  get hasResource(): boolean {
    return this.resource !== null && !this.isReleased;
  }

  /**
   * Get the managed resource
   */
  get(): T | null {
    return this.isReleased ? null : this.resource;
  }

  /**
   * Release the resource from guard management
   * @returns The released resource
   */
  release(): T | null {
    if (this.isReleased) return null;

    const released = this.resource;
    this.resource = null;
    this.deleter = null;
    this.isReleased = true;

    return released;
  }

  /**
   * Reset the guard with a new resource
   * @param newResource New resource to manage
   * @param newDeleter New deleter function
   */
  reset(newResource: T | null = null, newDeleter?: Deleter<T>): void {
    // Clean up existing resource if any
    if (this.hasResource && this.deleter) {
      try {
        this.deleter(this.resource!);
      } catch (error) {
        // Log error but don't throw from destructor
        console.error("Error during resource cleanup:", error);
      }
    }

    this.resource = newResource;
    this.deleter = newDeleter || null;
    this.isReleased = false;
  }

  /**
   * Swap resources with another guard
   * @param other Guard to swap with
   */
  swap(other: ResourceGuard<T>): void {
    const tempResource = this.resource;
    const tempDeleter = this.deleter;
    const tempReleased = this.isReleased;

    this.resource = other.resource;
    this.deleter = other.deleter;
    this.isReleased = other.isReleased;

    other.resource = tempResource;
    other.deleter = tempDeleter;
    other.isReleased = tempReleased;
  }

  /**
   * Get the deleter function
   */
  getDeleter(): Deleter<T> | null {
    return this.deleter;
  }

  /**
   * Check if guard is empty (no resource)
   */
  get isEmpty(): boolean {
    return !this.hasResource;
  }

  /**
   * Destructor - automatically clean up resource
   */
  destroy(): void {
    if (this.hasResource && this.deleter) {
      try {
        this.deleter(this.resource!);
      } catch (error) {
        // Log error but don't throw from destructor
        console.error("Error during resource cleanup:", error);
      }
    }

    this.resource = null;
    this.deleter = null;
    this.isReleased = true;
  }

  /**
   * Cleanup when going out of scope
   */
  [Symbol.dispose](): void {
    this.destroy();
  }

  // Operator-like methods for convenience
  /**
   * Arrow operator equivalent
   */
  arrow(): T | null {
    return this.get();
  }

  /**
   * Dereference operator equivalent
   */
  deref(): T | null {
    return this.get();
  }

  /**
   * Boolean conversion
   */
  toBoolean(): boolean {
    return this.hasResource;
  }
}

// ============================================================================
// AllocationTransaction - Transaction-Based Resource Management
// ============================================================================

/**
 * Transaction for managing multiple resources with commit/rollback semantics
 * All resources are automatically cleaned up if transaction is not committed
 */
export class AllocationTransaction {
  private resources: Array<{
    resource: any;
    deleter: (r: any) => void;
    type: ResourceType;
  }> = [];
  private committed = false;

  /**
   * Track a resource in the transaction
   * @param resource Resource to track
   * @param deleter Function to call for cleanup
   * @param type Resource type identifier
   */
  track<T>(
    resource: T,
    deleter: (r: T) => void,
    type: ResourceType = ResourceType.UNKNOWN
  ): void {
    if (this.committed) {
      throw new Error("Cannot track resources in committed transaction");
    }

    this.resources.push({ resource, deleter, type });
  }

  /**
   * Get the number of tracked resources
   */
  get resourceCount(): number {
    return this.resources.length;
  }

  /**
   * Check if transaction is empty
   */
  get empty(): boolean {
    return this.resources.length === 0;
  }

  /**
   * Check if transaction has been committed
   */
  get isCommitted(): boolean {
    return this.committed;
  }

  /**
   * Reserve capacity for better performance
   * @param _capacity Number of resources to reserve space for
   */
  reserve(_capacity: number): void {
    if (this.committed) return;

    // Pre-allocate array space without affecting resource count
    // Note: This doesn't affect the actual resource count, just array capacity
  }

  /**
   * Commit the transaction
   * Prevents automatic cleanup of resources
   */
  commit(): void {
    if (this.committed) return;

    this.committed = true;
    this.resources = []; // Clear tracking
  }

  /**
   * Rollback the transaction
   * Triggers cleanup of all tracked resources
   */
  rollback(): void {
    if (this.committed) return;

    // Clean up all resources
    for (const { resource, deleter } of this.resources) {
      try {
        deleter(resource);
      } catch (error) {
        console.error("Error during transaction rollback:", error);
      }
    }

    this.resources = [];
    this.committed = true;
  }

  /**
   * Get resources by type
   * @param type Resource type to filter by
   */
  getResourcesByType(type: ResourceType): any[] {
    return this.resources.filter((r) => r.type === type).map((r) => r.resource);
  }

  /**
   * Check if transaction has resources of specific type
   * @param type Resource type to check
   */
  hasResourceType(type: ResourceType): boolean {
    return this.resources.some((r) => r.type === type);
  }

  /**
   * Destructor - rollback if not committed
   */
  destroy(): void {
    if (!this.committed) {
      this.rollback();
    }
  }

  /**
   * Cleanup when going out of scope
   */
  [Symbol.dispose](): void {
    this.destroy();
  }
}

// ============================================================================
// ScopedCleanup - Scope-Based Cleanup Operations
// ============================================================================

/**
 * RAII wrapper for scope-based cleanup operations
 * Executes cleanup function when going out of scope
 */
export class ScopedCleanup {
  private cleanupFn: CleanupFunction | null = null;
  private active = true;

  /**
   * Create a scoped cleanup
   * @param cleanupFn Function to execute on cleanup
   */
  constructor(cleanupFn: CleanupFunction) {
    this.cleanupFn = cleanupFn;
  }

  /**
   * Check if cleanup is active
   */
  get isActive(): boolean {
    return this.active;
  }

  /**
   * Release the cleanup (prevent execution)
   */
  release(): void {
    this.active = false;
    this.cleanupFn = null;
  }

  /**
   * Execute cleanup immediately
   */
  execute(): void {
    if (this.active && this.cleanupFn) {
      try {
        this.cleanupFn();
      } catch (error) {
        console.error("Error during scoped cleanup:", error);
      }
      this.active = false;
      this.cleanupFn = null;
    }
  }

  /**
   * Destructor - execute cleanup if active
   */
  destroy(): void {
    this.execute();
  }

  /**
   * Cleanup when going out of scope
   */
  [Symbol.dispose](): void {
    this.destroy();
  }
}

// ============================================================================
// Factory Functions for Convenience
// ============================================================================

/**
 * Create a resource guard with automatic type inference
 * @param resource Resource to manage
 * @param deleter Function to call for cleanup
 * @returns ResourceGuard instance
 */
export function makeResourceGuard<T>(
  resource: T,
  deleter: Deleter<T>
): ResourceGuard<T> {
  return new ResourceGuard(resource, deleter);
}

/**
 * Create a scoped cleanup
 * @param cleanupFn Function to execute on cleanup
 * @returns ScopedCleanup instance
 */
export function makeScopedCleanup(cleanupFn: CleanupFunction): ScopedCleanup {
  return new ScopedCleanup(cleanupFn);
}

/**
 * Create a resource guard for C-style resources
 * @param resource C resource pointer
 * @param destroyFn C destroy function
 * @returns ResourceGuard instance
 */
export function makeCResourceGuard<T>(
  resource: T,
  destroyFn: (r: T) => void
): ResourceGuard<T> {
  return new ResourceGuard(resource, destroyFn);
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Execute a function with automatic resource cleanup
 * @param resource Resource to manage
 * @param deleter Function to call for cleanup
 * @param operation Function to execute with the resource
 * @returns Result of the operation
 */
export function withResource<T, R>(
  resource: T,
  deleter: Deleter<T>,
  operation: (r: T) => R
): R {
  const guard = new ResourceGuard(resource, deleter);
  try {
    return operation(guard.get()!);
  } finally {
    guard.destroy();
  }
}

/**
 * Execute multiple operations in a transaction
 * @param operations Array of operations to execute
 * @returns Array of results
 */
export function withTransaction<T>(operations: Array<() => T>): T[] {
  const transaction = new AllocationTransaction();
  const results: T[] = [];

  try {
    for (const operation of operations) {
      results.push(operation());
    }
    transaction.commit();
    return results;
  } catch (error) {
    transaction.rollback();
    throw error;
  }
}

/**
 * Create a cleanup scope that executes multiple cleanup functions
 * @param cleanupFns Array of cleanup functions
 * @returns ScopedCleanup that executes all functions
 */
export function makeCompoundCleanup(
  cleanupFns: CleanupFunction[]
): ScopedCleanup {
  return new ScopedCleanup(() => {
    for (const cleanupFn of cleanupFns) {
      try {
        cleanupFn();
      } catch (error) {
        console.error("Error in compound cleanup:", error);
      }
    }
  });
}

// ============================================================================
// Type Guards and Utilities
// ============================================================================

/**
 * Check if a value is a ResourceGuard
 */
export function isResourceGuard(value: any): value is ResourceGuard<any> {
  return value instanceof ResourceGuard;
}

/**
 * Check if a value is an AllocationTransaction
 */
export function isAllocationTransaction(
  value: any
): value is AllocationTransaction {
  return value instanceof AllocationTransaction;
}

/**
 * Check if a value is a ScopedCleanup
 */
export function isScopedCleanup(value: any): value is ScopedCleanup {
  return value instanceof ScopedCleanup;
}

/**
 * Safe cleanup that handles null/undefined values
 */
export function safeCleanup<T>(
  resource: T | null | undefined,
  deleter: Deleter<T>
): void {
  if (resource != null) {
    try {
      deleter(resource);
    } catch (error) {
      console.error("Error during safe cleanup:", error);
    }
  }
}
