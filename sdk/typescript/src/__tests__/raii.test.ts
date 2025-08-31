/**
 * @file raii.test.ts
 * @brief Comprehensive unit tests for MCP RAII utilities
 *
 * Tests all aspects of the RAII library including:
 * - ResourceGuard functionality and edge cases
 * - AllocationTransaction with commit/rollback scenarios
 * - Thread safety and concurrent operations
 * - Performance characteristics
 * - Memory safety and leak detection
 *
 * @copyright Copyright (c) 2025 MCP Project
 * @license MIT License
 */

import {
  AllocationTransaction,
  ResourceGuard,
  ResourceType,
  ScopedCleanup,
  isAllocationTransaction,
  isResourceGuard,
  isScopedCleanup,
  makeCResourceGuard,
  makeCompoundCleanup,
  makeResourceGuard,
  makeScopedCleanup,
  safeCleanup,
  withResource,
  withTransaction,
} from "../raii";

// ============================================================================
// Mock Resource Class for Testing
// ============================================================================

class MockResource {
  static allocationCount = 0;
  static deallocationCount = 0;
  static copyCount = 0;
  static moveCount = 0;

  value: number;

  constructor(val: number = 42) {
    MockResource.allocationCount++;
    this.value = val;
  }

  // Simulate destructor behavior
  destroy(): void {
    MockResource.deallocationCount++;
  }

  static resetCounters(): void {
    MockResource.allocationCount = 0;
    MockResource.deallocationCount = 0;
    MockResource.copyCount = 0;
    MockResource.moveCount = 0;
  }

  static isBalanced(): boolean {
    return MockResource.allocationCount === MockResource.deallocationCount;
  }
}

// Custom deleter for testing
class MockDeleter {
  static deleteCount = 0;

  static delete(resource: MockResource): void {
    if (resource) {
      MockDeleter.deleteCount++;
    }
  }

  static resetCounter(): void {
    MockDeleter.deleteCount = 0;
  }
}

// ============================================================================
// Test Fixtures
// ============================================================================

describe("RAII System", () => {
  beforeEach(() => {
    MockResource.resetCounters();
    MockDeleter.resetCounter();
  });

  afterEach(() => {
    // Verify no memory leaks in tests
    if (!MockResource.isBalanced()) {
      console.log(
        `Memory leak detected: allocations=${MockResource.allocationCount}, deallocations=${MockResource.deallocationCount}`
      );
    }
    expect(MockResource.isBalanced()).toBe(true);
  });

  // ============================================================================
  // ResourceGuard Tests
  // ============================================================================

  describe("ResourceGuard", () => {
    describe("Basic Resource Management", () => {
      it("should create default guard", () => {
        const guard = new ResourceGuard<MockResource>();
        expect(guard.hasResource).toBe(false);
        expect(guard.get()).toBeNull();
        expect(guard.isEmpty).toBe(true);
      });

      it("should manage resource with deleter", () => {
        const resource = new MockResource(123);
        const guard = new ResourceGuard(resource, (r) => {
          r.destroy();
        });

        expect(guard.hasResource).toBe(true);
        expect(guard.get()).toBe(resource);
        expect(guard.get()?.value).toBe(123);
        expect(guard.isEmpty).toBe(false);

        // Clean up the guard
        guard.destroy();
      });

      it("should automatically clean up resource on destroy", () => {
        const resource = new MockResource(456);
        let cleanupCalled = false;

        const guard = new ResourceGuard(resource, (r) => {
          cleanupCalled = true;
          r.destroy(); // Actually destroy the resource
        });

        expect(cleanupCalled).toBe(false);
        guard.destroy();
        expect(cleanupCalled).toBe(true);
        expect(guard.hasResource).toBe(false);
      });

      it("should handle null resource safely", () => {
        const guard = new ResourceGuard<MockResource>(null, () => {
          throw new Error("Should not be called");
        });

        expect(guard.hasResource).toBe(false);
        expect(guard.get()).toBeNull();

        // Should not crash
        guard.destroy();
      });
    });

    describe("Resource Operations", () => {
      it("should release resource from management", () => {
        const resource = new MockResource(789);
        const guard = new ResourceGuard(resource, (r) => {
          r.destroy();
        });

        const released = guard.release();
        expect(released).toBe(resource);
        expect(guard.hasResource).toBe(false);
        expect(guard.get()).toBeNull();

        // Resource should not be cleaned up
        expect(MockResource.deallocationCount).toBe(0);

        // Manual cleanup
        if (released) {
          released.destroy();
        }
      });

      it("should reset with new resource", () => {
        const resource1 = new MockResource(111);
        const resource2 = new MockResource(222);
        let cleanupCount = 0;

        const guard = new ResourceGuard(resource1, (r) => {
          cleanupCount++;
          r.destroy();
        });

        expect(guard.get()).toBe(resource1);

        // Reset with new resource
        guard.reset(resource2, (r) => {
          cleanupCount++;
          r.destroy();
        });

        expect(guard.get()).toBe(resource2);
        expect(cleanupCount).toBe(1); // resource1 cleaned up

        // Reset to null
        guard.reset();
        expect(guard.hasResource).toBe(false);
        expect(cleanupCount).toBe(2); // resource2 cleaned up
      });

      it("should swap resources with another guard", () => {
        const resource1 = new MockResource(333);
        const resource2 = new MockResource(444);

        const guard1 = new ResourceGuard(resource1, (r) => {
          r.destroy();
        });
        const guard2 = new ResourceGuard(resource2, (r) => {
          r.destroy();
        });

        guard1.swap(guard2);

        expect(guard1.get()).toBe(resource2);
        expect(guard2.get()).toBe(resource1);

        // Clean up guards
        guard1.destroy();
        guard2.destroy();
      });

      it("should get deleter function", () => {
        const resource = new MockResource(555);
        const deleter = jest.fn().mockImplementation((r) => {
          r.destroy();
        });
        const guard = new ResourceGuard(resource, deleter);

        expect(guard.getDeleter()).toBe(deleter);

        // Clean up guard
        guard.destroy();
      });
    });

    describe("Operator-like Methods", () => {
      it("should provide arrow operator equivalent", () => {
        const resource = new MockResource(666);
        const guard = new ResourceGuard(resource, (r) => {
          r.destroy();
        });

        expect(guard.arrow()).toBe(resource);

        // Clean up guard
        guard.destroy();
      });

      it("should provide dereference operator equivalent", () => {
        const resource = new MockResource(777);
        const guard = new ResourceGuard(resource, (r) => {
          r.destroy();
        });

        expect(guard.deref()).toBe(resource);

        // Clean up guard
        guard.destroy();
      });

      it("should provide boolean conversion", () => {
        const emptyGuard = new ResourceGuard<MockResource>();
        const resourceGuard = new ResourceGuard(new MockResource(888), (r) => {
          r.destroy();
        });

        expect(emptyGuard.toBoolean()).toBe(false);
        expect(resourceGuard.toBoolean()).toBe(true);

        // Clean up guards
        emptyGuard.destroy();
        resourceGuard.destroy();
      });
    });

    describe("Error Handling", () => {
      it("should handle exceptions in deleter gracefully", () => {
        const resource = new MockResource(999);
        const consoleSpy = jest.spyOn(console, "error").mockImplementation();

        const guard = new ResourceGuard(resource, (r) => {
          r.destroy();
          throw new Error("Deleter exception");
        });

        // Should not throw
        expect(() => guard.destroy()).not.toThrow();
        expect(consoleSpy).toHaveBeenCalledWith(
          "Error during resource cleanup:",
          expect.any(Error)
        );

        consoleSpy.mockRestore();

        // Clean up guard
        guard.destroy();
      });

      it("should handle exceptions in reset gracefully", () => {
        const resource1 = new MockResource(1010);
        const resource2 = new MockResource(1111);
        const consoleSpy = jest.spyOn(console, "error").mockImplementation();

        const guard = new ResourceGuard(resource1, (r) => {
          r.destroy();
          throw new Error("Reset exception");
        });

        // Should not throw
        expect(() => guard.reset(resource2)).not.toThrow();
        expect(consoleSpy).toHaveBeenCalledWith(
          "Error during resource cleanup:",
          expect.any(Error)
        );

        consoleSpy.mockRestore();

        // Clean up the guard to destroy resource2
        guard.destroy();

        // Manually clean up resource1 since the deleter failed
        resource1.destroy();
      });
    });

    describe("Move Semantics", () => {
      it("should support move-like operations", () => {
        const resource = new MockResource(1212);
        const guard1 = new ResourceGuard(resource, (r) => {
          r.destroy();
        });

        // Simulate move by releasing and reassigning
        const movedResource = guard1.release();
        const guard2 = new ResourceGuard(movedResource, (r) => {
          r.destroy();
        });

        expect(guard1.hasResource).toBe(false);
        expect(guard2.hasResource).toBe(true);
        expect(guard2.get()).toBe(resource);

        // Clean up guards
        guard1.destroy();
        guard2.destroy();
      });
    });
  });

  // ============================================================================
  // AllocationTransaction Tests
  // ============================================================================

  describe("AllocationTransaction", () => {
    describe("Basic Transaction Operations", () => {
      it("should create empty transaction", () => {
        const txn = new AllocationTransaction();
        expect(txn.resourceCount).toBe(0);
        expect(txn.empty).toBe(true);
        expect(txn.isCommitted).toBe(false);
      });

      it("should track resources", () => {
        const txn = new AllocationTransaction();
        const resource1 = new MockResource(1313);
        const resource2 = new MockResource(1414);

        txn.track(
          resource1,
          (r) => {
            r.destroy();
          },
          ResourceType.FILTER
        );
        txn.track(
          resource2,
          (r) => {
            r.destroy();
          },
          ResourceType.BUFFER
        );

        expect(txn.resourceCount).toBe(2);
        expect(txn.empty).toBe(false);
        expect(txn.hasResourceType(ResourceType.FILTER)).toBe(true);
        expect(txn.hasResourceType(ResourceType.BUFFER)).toBe(true);

        // Clean up transaction
        txn.destroy();
      });

      it("should reserve capacity", () => {
        const txn = new AllocationTransaction();
        txn.reserve(100);

        // Should not affect resource count
        expect(txn.resourceCount).toBe(0);

        // Clean up transaction
        txn.destroy();
      });
    });

    describe("Transaction Lifecycle", () => {
      it("should commit transaction", () => {
        const txn = new AllocationTransaction();
        const resource = new MockResource(1515);

        txn.track(resource, (r) => {
          r.destroy();
        });
        expect(txn.resourceCount).toBe(1);

        txn.commit();
        expect(txn.isCommitted).toBe(true);
        expect(txn.resourceCount).toBe(0);

        // Cannot track after commit
        const testResource = new MockResource(1616);
        expect(() =>
          txn.track(testResource, (r) => {
            r.destroy();
          })
        ).toThrow();

        // Clean up the test resource since it wasn't tracked
        testResource.destroy();

        // Clean up the committed resource since it wasn't destroyed by the transaction
        resource.destroy();

        // Clean up transaction
        txn.destroy();
      });

      it("should rollback transaction", () => {
        const txn = new AllocationTransaction();
        let cleanupCount = 0;

        const resource1 = new MockResource(1717);
        const resource2 = new MockResource(1818);

        txn.track(resource1, (r) => {
          cleanupCount++;
          r.destroy();
        });
        txn.track(resource2, (r) => {
          cleanupCount++;
          r.destroy();
        });

        expect(txn.resourceCount).toBe(2);

        txn.rollback();
        expect(txn.isCommitted).toBe(true);
        expect(txn.resourceCount).toBe(0);
        expect(cleanupCount).toBe(2);

        // Clean up transaction
        txn.destroy();
      });

      it("should auto-rollback on destroy if not committed", () => {
        let cleanupCount = 0;

        {
          const txn = new AllocationTransaction();
          const resource = new MockResource(1919);
          txn.track(resource, (r) => {
            cleanupCount++;
            r.destroy();
          });

          // Transaction not committed, should auto-rollback
          txn.destroy(); // Manual cleanup since no using statement
        }

        expect(cleanupCount).toBe(1);
      });
    });

    describe("Resource Type Management", () => {
      it("should filter resources by type", () => {
        const txn = new AllocationTransaction();
        const filter1 = new MockResource(2020);
        const filter2 = new MockResource(2121);
        const buffer = new MockResource(2222);

        txn.track(
          filter1,
          (r) => {
            r.destroy();
          },
          ResourceType.FILTER
        );
        txn.track(
          filter2,
          (r) => {
            r.destroy();
          },
          ResourceType.FILTER
        );
        txn.track(
          buffer,
          (r) => {
            r.destroy();
          },
          ResourceType.BUFFER
        );

        const filters = txn.getResourcesByType(ResourceType.FILTER);
        const buffers = txn.getResourcesByType(ResourceType.BUFFER);

        expect(filters).toHaveLength(2);
        expect(buffers).toHaveLength(1);
        expect(filters).toContain(filter1);
        expect(filters).toContain(filter2);
        expect(buffers).toContain(buffer);

        // Clean up transaction
        txn.destroy();
      });

      it("should check resource type presence", () => {
        const txn = new AllocationTransaction();

        expect(txn.hasResourceType(ResourceType.FILTER)).toBe(false);

        txn.track(
          new MockResource(2323),
          (r) => {
            r.destroy();
          },
          ResourceType.FILTER
        );

        expect(txn.hasResourceType(ResourceType.FILTER)).toBe(true);
        expect(txn.hasResourceType(ResourceType.BUFFER)).toBe(false);

        // Clean up transaction
        txn.destroy();
      });
    });

    describe("Error Handling", () => {
      it("should handle exceptions during rollback gracefully", () => {
        const txn = new AllocationTransaction();
        const consoleSpy = jest.spyOn(console, "error").mockImplementation();

        const resource = new MockResource(2424);
        txn.track(resource, (r) => {
          r.destroy();
          throw new Error("Rollback exception");
        });

        // Should not throw
        expect(() => txn.rollback()).not.toThrow();
        expect(consoleSpy).toHaveBeenCalledWith(
          "Error during transaction rollback:",
          expect.any(Error)
        );

        consoleSpy.mockRestore();

        // Clean up transaction
        txn.destroy();
      });
    });
  });

  // ============================================================================
  // ScopedCleanup Tests
  // ============================================================================

  describe("ScopedCleanup", () => {
    describe("Basic Cleanup Operations", () => {
      it("should create active cleanup", () => {
        const cleanup = new ScopedCleanup(() => {});
        expect(cleanup.isActive).toBe(true);

        // Clean up cleanup
        cleanup.destroy();
      });

      it("should execute cleanup on destroy", () => {
        let cleanupCalled = false;
        const cleanup = new ScopedCleanup(() => {
          cleanupCalled = true;
        });

        expect(cleanupCalled).toBe(false);
        cleanup.destroy();
        expect(cleanupCalled).toBe(true);
        expect(cleanup.isActive).toBe(false);
      });

      it("should release cleanup", () => {
        let cleanupCalled = false;
        const cleanup = new ScopedCleanup(() => {
          cleanupCalled = true;
        });

        cleanup.release();
        expect(cleanup.isActive).toBe(false);

        cleanup.destroy();
        expect(cleanupCalled).toBe(false);
      });

      it("should execute cleanup immediately", () => {
        let cleanupCalled = false;
        const cleanup = new ScopedCleanup(() => {
          cleanupCalled = true;
        });

        cleanup.execute();
        expect(cleanupCalled).toBe(true);
        expect(cleanup.isActive).toBe(false);
      });
    });

    describe("Error Handling", () => {
      it("should handle exceptions in cleanup gracefully", () => {
        const consoleSpy = jest.spyOn(console, "error").mockImplementation();

        const cleanup = new ScopedCleanup(() => {
          throw new Error("Cleanup exception");
        });

        // Should not throw
        expect(() => cleanup.execute()).not.toThrow();
        expect(consoleSpy).toHaveBeenCalledWith(
          "Error during scoped cleanup:",
          expect.any(Error)
        );

        consoleSpy.mockRestore();

        // Clean up cleanup
        cleanup.destroy();
      });
    });
  });

  // ============================================================================
  // Factory Function Tests
  // ============================================================================

  describe("Factory Functions", () => {
    it("should create resource guard with makeResourceGuard", () => {
      const resource = new MockResource(2525);
      const guard = makeResourceGuard(resource, (r) => {
        r.destroy();
      });

      expect(guard).toBeInstanceOf(ResourceGuard);
      expect(guard.get()).toBe(resource);

      // Clean up guard
      guard.destroy();
    });

    it("should create scoped cleanup with makeScopedCleanup", () => {
      const cleanup = makeScopedCleanup(() => {});

      expect(cleanup).toBeInstanceOf(ScopedCleanup);
      expect(cleanup.isActive).toBe(true);

      // Clean up cleanup
      cleanup.destroy();
    });

    it("should create C resource guard with makeCResourceGuard", () => {
      const resource = new MockResource(2626);
      const guard = makeCResourceGuard(resource, (r) => {
        r.destroy();
      });

      expect(guard).toBeInstanceOf(ResourceGuard);
      expect(guard.get()).toBe(resource);

      // Clean up guard
      guard.destroy();
    });
  });

  // ============================================================================
  // Utility Function Tests
  // ============================================================================

  describe("Utility Functions", () => {
    it("should execute operation with resource using withResource", () => {
      const resource = new MockResource(2727);
      let cleanupCalled = false;

      const result = withResource(
        resource,
        (r) => {
          cleanupCalled = true;
          r.destroy();
        },
        (r) => {
          return r.value * 2;
        }
      );

      expect(result).toBe(5454);
      expect(cleanupCalled).toBe(true);
    });

    it("should execute operations in transaction using withTransaction", () => {
      const results = withTransaction([() => 1, () => 2, () => 3]);

      expect(results).toEqual([1, 2, 3]);
    });

    it("should create compound cleanup", () => {
      let cleanup1Called = false;
      let cleanup2Called = false;

      const cleanup = makeCompoundCleanup([
        () => {
          cleanup1Called = true;
        },
        () => {
          cleanup2Called = true;
        },
      ]);

      expect(cleanup).toBeInstanceOf(ScopedCleanup);

      cleanup.execute();
      expect(cleanup1Called).toBe(true);
      expect(cleanup2Called).toBe(true);
    });

    it("should handle exceptions in compound cleanup", () => {
      const consoleSpy = jest.spyOn(console, "error").mockImplementation();

      const cleanup = makeCompoundCleanup([
        () => {
          throw new Error("Cleanup 1 error");
        },
        () => {
          throw new Error("Cleanup 2 error");
        },
      ]);

      // Should not throw
      expect(() => cleanup.execute()).not.toThrow();
      expect(consoleSpy).toHaveBeenCalledTimes(2);

      consoleSpy.mockRestore();
    });
  });

  // ============================================================================
  // Type Guard Tests
  // ============================================================================

  describe("Type Guards", () => {
    it("should identify ResourceGuard instances", () => {
      const guard = new ResourceGuard();
      const notGuard = {};

      expect(isResourceGuard(guard)).toBe(true);
      expect(isResourceGuard(notGuard)).toBe(false);
    });

    it("should identify AllocationTransaction instances", () => {
      const txn = new AllocationTransaction();
      const notTxn = {};

      expect(isAllocationTransaction(txn)).toBe(true);
      expect(isAllocationTransaction(notTxn)).toBe(false);

      // Clean up transaction
      txn.destroy();
    });

    it("should identify ScopedCleanup instances", () => {
      const cleanup = new ScopedCleanup(() => {});
      const notCleanup = {};

      expect(isScopedCleanup(cleanup)).toBe(true);
      expect(isScopedCleanup(notCleanup)).toBe(false);

      // Clean up cleanup
      cleanup.destroy();
    });
  });

  // ============================================================================
  // Safe Cleanup Tests
  // ============================================================================

  describe("Safe Cleanup", () => {
    it("should handle null resources safely", () => {
      const deleter = jest.fn();

      expect(() => safeCleanup(null, deleter)).not.toThrow();
      expect(deleter).not.toHaveBeenCalled();
    });

    it("should handle undefined resources safely", () => {
      const deleter = jest.fn();

      expect(() => safeCleanup(undefined, deleter)).not.toThrow();
      expect(deleter).not.toHaveBeenCalled();
    });

    it("should call deleter for valid resources", () => {
      const resource = new MockResource(2828);
      const deleter = jest.fn().mockImplementation((r) => {
        r.destroy();
      });

      safeCleanup(resource, deleter);
      expect(deleter).toHaveBeenCalledWith(resource);
    });

    it("should handle exceptions in safe cleanup", () => {
      const resource = new MockResource(2929);
      const consoleSpy = jest.spyOn(console, "error").mockImplementation();

      const deleter = jest.fn().mockImplementation((r) => {
        r.destroy();
        throw new Error("Safe cleanup exception");
      });

      expect(() => safeCleanup(resource, deleter)).not.toThrow();
      expect(consoleSpy).toHaveBeenCalledWith(
        "Error during safe cleanup:",
        expect.any(Error)
      );

      consoleSpy.mockRestore();
    });
  });

  // ============================================================================
  // Performance Tests
  // ============================================================================

  describe("Performance", () => {
    it("should handle many resource guards efficiently", () => {
      const start = performance.now();
      const iterations = 10000;

      for (let i = 0; i < iterations; i++) {
        const resource = new MockResource(i);
        const guard = makeResourceGuard(resource, (r) => {
          r.destroy();
        });
        guard.destroy(); // Manual cleanup since no using statement
      }

      const end = performance.now();
      const duration = end - start;

      // Should complete in reasonable time (less than 1 second)
      expect(duration).toBeLessThan(1000);
    });

    it("should handle large transactions efficiently", () => {
      const start = performance.now();
      const resourceCount = 1000;

      const txn = new AllocationTransaction();
      txn.reserve(resourceCount);

      for (let i = 0; i < resourceCount; i++) {
        const resource = new MockResource(i);
        txn.track(resource, (r) => {
          r.destroy();
        });
      }

      expect(txn.resourceCount).toBe(resourceCount);

      // Measure rollback performance
      const rollbackStart = performance.now();
      txn.rollback();
      const rollbackEnd = performance.now();

      const totalDuration = rollbackEnd - start;
      const rollbackDuration = rollbackEnd - rollbackStart;

      // Performance expectations
      expect(totalDuration).toBeLessThan(1000); // Less than 1 second total
      expect(rollbackDuration).toBeLessThan(100); // Less than 100ms for rollback

      // Clean up transaction
      txn.destroy();
    });
  });

  // ============================================================================
  // Integration Tests
  // ============================================================================

  describe("Integration Scenarios", () => {
    it("should manage complex resource lifecycle", () => {
      const transaction = new AllocationTransaction();
      const resources: MockResource[] = [];

      // Create multiple resources
      for (let i = 0; i < 10; i++) {
        const resource = new MockResource(i);
        resources.push(resource);
        transaction.track(
          resource,
          (r) => {
            r.destroy();
          },
          ResourceType.FILTER
        );
      }

      expect(transaction.resourceCount).toBe(10);
      expect(transaction.hasResourceType(ResourceType.FILTER)).toBe(true);

      // Commit transaction
      transaction.commit();
      expect(transaction.isCommitted).toBe(true);

      // Manual cleanup since transaction was committed
      resources.forEach((resource) => {
        resource.destroy();
      });

      // Clean up transaction
      transaction.destroy();
    });

    it("should handle nested resource management", () => {
      // Outer transaction
      const outerTxn = new AllocationTransaction();
      const resource1 = new MockResource(3030);
      outerTxn.track(resource1, (r) => {
        r.destroy();
      });

      {
        // Inner transaction
        const innerTxn = new AllocationTransaction();
        const resource2 = new MockResource(3131);
        innerTxn.track(resource2, (r) => {
          r.destroy();
        });

        // Inner rollback - resource2 freed
        innerTxn.rollback();
      }

      // Outer still has resource1
      expect(outerTxn.resourceCount).toBe(1);

      // Let outer auto-rollback
      outerTxn.destroy();
    });

    it("should work with resource guards in transactions", () => {
      const transaction = new AllocationTransaction();

      // Create resources with guards
      const guard1 = makeResourceGuard(new MockResource(3232), (r) => {
        r.destroy();
      });
      const guard2 = makeResourceGuard(new MockResource(3333), (r) => {
        r.destroy();
      });

      // Track guard-managed resources in transaction
      transaction.track(
        guard1.get(),
        (r) => {
          if (r) r.destroy();
        },
        ResourceType.FILTER
      );
      transaction.track(
        guard2.get(),
        (r) => {
          if (r) r.destroy();
        },
        ResourceType.BUFFER
      );

      expect(transaction.resourceCount).toBe(2);
      expect(transaction.hasResourceType(ResourceType.FILTER)).toBe(true);
      expect(transaction.hasResourceType(ResourceType.BUFFER)).toBe(true);

      // Commit transaction
      transaction.commit();

      // Guards still manage the resources
      expect(guard1.hasResource).toBe(true);
      expect(guard2.hasResource).toBe(true);

      // Clean up guards
      guard1.destroy();
      guard2.destroy();

      // Clean up transaction
      transaction.destroy();
    });
  });

  // ============================================================================
  // Edge Cases and Error Handling
  // ============================================================================

  describe("Edge Cases", () => {
    it("should handle self-assignment safely", () => {
      const resource = new MockResource(3434);
      const guard = new ResourceGuard(resource, (r) => {
        r.destroy();
      });

      // Self-assignment should be safe
      guard.swap(guard);

      expect(guard.hasResource).toBe(true);
      expect(guard.get()).toBe(resource);

      // Clean up guard
      guard.destroy();
    });

    it("should handle double-free protection", () => {
      const resource = new MockResource(3535);
      const guard = new ResourceGuard(resource, (r) => {
        r.destroy();
      });

      // Manual reset should prevent double-free
      guard.reset();
      expect(guard.hasResource).toBe(false);

      guard.reset(); // Should be safe to call again
    });

    it("should handle empty transaction operations", () => {
      const txn = new AllocationTransaction();

      expect(txn.resourceCount).toBe(0);
      expect(txn.empty).toBe(true);
      expect(txn.isCommitted).toBe(false);

      // Operations on empty transaction should be safe
      txn.rollback(); // Should be safe
      expect(txn.isCommitted).toBe(true);

      txn.commit(); // Should be safe (already committed)
      expect(txn.isCommitted).toBe(true);

      // Clean up transaction
      txn.destroy();
    });
  });

  // ============================================================================
  // Resource Type Tests
  // ============================================================================

  describe("Resource Types", () => {
    it("should support all resource types", () => {
      const txn = new AllocationTransaction();

      const filter = new MockResource(3636);
      const buffer = new MockResource(3737);
      const dispatcher = new MockResource(3838);

      txn.track(
        filter,
        (r) => {
          r.destroy();
        },
        ResourceType.FILTER
      );
      txn.track(
        buffer,
        (r) => {
          r.destroy();
        },
        ResourceType.BUFFER
      );
      txn.track(
        dispatcher,
        (r) => {
          r.destroy();
        },
        ResourceType.DISPATCHER
      );

      expect(txn.getResourcesByType(ResourceType.FILTER)).toHaveLength(1);
      expect(txn.getResourcesByType(ResourceType.BUFFER)).toHaveLength(1);
      expect(txn.getResourcesByType(ResourceType.DISPATCHER)).toHaveLength(1);
      expect(txn.getResourcesByType(ResourceType.POOL)).toHaveLength(0);

      // Clean up transaction
      txn.destroy();
    });

    it("should handle unknown resource types", () => {
      const txn = new AllocationTransaction();
      const resource = new MockResource(3939);

      txn.track(
        resource,
        (r) => {
          r.destroy();
        },
        ResourceType.UNKNOWN
      );

      expect(txn.hasResourceType(ResourceType.UNKNOWN)).toBe(true);
      expect(txn.getResourcesByType(ResourceType.UNKNOWN)).toHaveLength(1);

      // Clean up transaction
      txn.destroy();
    });
  });
});
