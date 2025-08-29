/**
 * @file resource-manager.test.ts
 * @brief Unit tests for RAII resource management classes
 */

import {
  RAII_CLEANUP,
  RAII_TRANSACTION,
  ResourceGuard,
  ResourceManager,
  ResourceTransaction,
  ResourceType,
  ScopedCleanup,
  checkResourceLeaks,
  getResourceStats,
  makeResourceGuard,
  reportResourceLeaks,
} from "../raii/resource-manager";

describe("ResourceGuard", () => {
  let guard: ResourceGuard<{ id: number; data: string }>;
  let mockResource: { id: number; data: string };
  let mockDeleter: jest.Mock;

  beforeEach(() => {
    mockResource = { id: 1, data: "test" };
    mockDeleter = jest.fn();
    guard = new ResourceGuard(mockResource, ResourceType.RESOURCE, mockDeleter);
  });

  afterEach(() => {
    if (guard) {
      guard.destroy();
    }
  });

  describe("construction and initialization", () => {
    it("should create resource guard with resource and deleter", () => {
      expect(guard).toBeDefined();
      expect(guard.get()).toBe(mockResource);
      expect(guard.isValid()).toBe(true);
    });

    it("should create guard with different resource types", () => {
      const bufferGuard = new ResourceGuard(
        Buffer.from("test"),
        ResourceType.BUFFER,
        jest.fn()
      );
      expect(bufferGuard).toBeDefined();
      expect(bufferGuard.getResourceType()).toBe(ResourceType.BUFFER);

      bufferGuard.destroy();
    });

    it("should track resource creation", () => {
      const stats = getResourceStats();
      expect(stats.guardsCreated).toBeGreaterThan(0);
    });
  });

  describe("resource access", () => {
    it("should provide access to guarded resource", () => {
      const resource = guard.get();
      expect(resource).toBe(mockResource);
      expect(resource.id).toBe(1);
      expect(resource.data).toBe("test");
    });

    it("should return undefined after destruction", () => {
      guard.destroy();
      expect(guard.get()).toBeUndefined();
    });

    it("should maintain resource reference", () => {
      const resource = guard.get();
      resource.data = "modified";

      expect(guard.get()?.data).toBe("modified");
    });
  });

  describe("guard validation", () => {
    it("should report valid state when resource exists", () => {
      expect(guard.isValid()).toBe(true);
    });

    it("should report invalid state after destruction", () => {
      guard.destroy();
      expect(guard.isValid()).toBe(false);
    });

    it("should report invalid state when resource is null", () => {
      const nullGuard = new ResourceGuard(
        null as any,
        ResourceType.RESOURCE,
        jest.fn()
      );
      expect(nullGuard.isValid()).toBe(false);
      nullGuard.destroy();
    });
  });

  describe("resource cleanup", () => {
    it("should call deleter function on destruction", () => {
      guard.destroy();
      expect(mockDeleter).toHaveBeenCalledWith(mockResource);
    });

    it("should call deleter only once", () => {
      guard.destroy();
      guard.destroy(); // Second call should not trigger deleter

      expect(mockDeleter).toHaveBeenCalledTimes(1);
    });

    it("should handle deleter exceptions gracefully", () => {
      const throwingDeleter = jest.fn().mockImplementation(() => {
        throw new Error("Deletion failed");
      });

      const throwingGuard = new ResourceGuard(
        { id: 2, data: "throwing" },
        ResourceType.RESOURCE,
        throwingDeleter
      );

      expect(() => throwingGuard.destroy()).not.toThrow();
      expect(throwingDeleter).toHaveBeenCalled();

      throwingGuard.destroy(); // Should not throw again
    });

    it("should cleanup resource on scope exit", () => {
      let cleanupCalled = false;

      {
        const scopedGuard = new ResourceGuard(
          { id: 3, data: "scoped" },
          ResourceType.RESOURCE,
          () => {
            cleanupCalled = true;
          }
        );
        expect(scopedGuard.isValid()).toBe(true);
      } // Automatic cleanup here

      expect(cleanupCalled).toBe(true);
    });
  });

  describe("guard lifecycle", () => {
    it("should handle multiple destroy calls gracefully", () => {
      expect(() => guard.destroy()).not.toThrow();
      expect(() => guard.destroy()).not.toThrow();
      expect(() => guard.destroy()).not.toThrow();
    });

    it("should track guard destruction in statistics", () => {
      const initialStats = getResourceStats();
      guard.destroy();
      const finalStats = getResourceStats();

      expect(finalStats.guardsDestroyed).toBe(initialStats.guardsDestroyed + 1);
    });
  });
});

describe("ResourceTransaction", () => {
  let transaction: ResourceTransaction;

  beforeEach(() => {
    transaction = RAII_TRANSACTION();
  });

  afterEach(() => {
    if (transaction && !transaction.isCommitted()) {
      transaction.rollback();
    }
  });

  describe("construction and initialization", () => {
    it("should create resource transaction", () => {
      expect(transaction).toBeDefined();
      expect(transaction.isCommitted()).toBe(false);
      expect(transaction.resourceCount()).toBe(0);
    });

    it("should track transaction creation in statistics", () => {
      const stats = getResourceStats();
      expect(stats.transactionsCreated).toBeGreaterThan(0);
    });
  });

  describe("resource tracking", () => {
    it("should track single resource", () => {
      const resource = { id: 1, data: "tracked" };
      const deleter = jest.fn();

      transaction.track(resource, deleter);

      expect(transaction.resourceCount()).toBe(1);
    });

    it("should track multiple resources", () => {
      const resources = [
        { id: 1, data: "resource-1" },
        { id: 2, data: "resource-2" },
        { id: 3, data: "resource-3" },
      ];

      resources.forEach((resource) => {
        transaction.track(resource, jest.fn());
      });

      expect(transaction.resourceCount()).toBe(3);
    });

    it("should not track null resources", () => {
      transaction.track(null as any, jest.fn());
      expect(transaction.resourceCount()).toBe(0);
    });

    it("should not track resources after commit", () => {
      transaction.commit();

      const resource = { id: 1, data: "post-commit" };
      transaction.track(resource, jest.fn());

      expect(transaction.resourceCount()).toBe(0);
    });
  });

  describe("transaction commitment", () => {
    it("should commit transaction successfully", () => {
      const resource = { id: 1, data: "committed" };
      const deleter = jest.fn();

      transaction.track(resource, deleter);
      transaction.commit();

      expect(transaction.isCommitted()).toBe(true);
    });

    it("should prevent rollback after commit", () => {
      const resource = { id: 1, data: "committed" };
      const deleter = jest.fn();

      transaction.track(resource, deleter);
      transaction.commit();

      expect(() => transaction.rollback()).not.toThrow();
      expect(deleter).not.toHaveBeenCalled(); // Should not cleanup committed resources
    });

    it("should track committed transactions in statistics", () => {
      const initialStats = getResourceStats();
      transaction.commit();
      const finalStats = getResourceStats();

      expect(finalStats.transactionsCommitted).toBe(
        initialStats.transactionsCommitted + 1
      );
    });
  });

  describe("transaction rollback", () => {
    it("should rollback tracked resources", () => {
      const resource1 = { id: 1, data: "resource-1" };
      const resource2 = { id: 2, data: "resource-2" };
      const deleter1 = jest.fn();
      const deleter2 = jest.fn();

      transaction.track(resource1, deleter1);
      transaction.track(resource2, deleter2);

      transaction.rollback();

      expect(deleter1).toHaveBeenCalledWith(resource1);
      expect(deleter2).toHaveBeenCalledWith(resource2);
      expect(transaction.resourceCount()).toBe(0);
    });

    it("should handle deleter exceptions during rollback", () => {
      const resource = { id: 1, data: "throwing" };
      const throwingDeleter = jest.fn().mockImplementation(() => {
        throw new Error("Rollback failed");
      });

      transaction.track(resource, throwingDeleter);

      expect(() => transaction.rollback()).not.toThrow();
      expect(throwingDeleter).toHaveBeenCalled();
    });

    it("should track rollback transactions in statistics", () => {
      const resource = { id: 1, data: "rolled-back" };
      transaction.track(resource, jest.fn());

      const initialStats = getResourceStats();
      transaction.rollback();
      const finalStats = getResourceStats();

      expect(finalStats.transactionsRolledBack).toBe(
        initialStats.transactionsRolledBack + 1
      );
    });
  });

  describe("transaction state", () => {
    it("should report correct committed state", () => {
      expect(transaction.isCommitted()).toBe(false);

      transaction.commit();
      expect(transaction.isCommitted()).toBe(true);
    });

    it("should report correct resource count", () => {
      expect(transaction.resourceCount()).toBe(0);

      transaction.track({ id: 1, data: "test" }, jest.fn());
      expect(transaction.resourceCount()).toBe(1);

      transaction.track({ id: 2, data: "test" }, jest.fn());
      expect(transaction.resourceCount()).toBe(2);
    });
  });
});

describe("ScopedCleanup", () => {
  let cleanup: ScopedCleanup;
  let mockCleanupFunction: jest.Mock;

  beforeEach(() => {
    mockCleanupFunction = jest.fn();
    cleanup = RAII_CLEANUP(mockCleanupFunction);
  });

  afterEach(() => {
    if (cleanup && cleanup.isActive()) {
      cleanup.execute();
    }
  });

  describe("construction and initialization", () => {
    it("should create scoped cleanup", () => {
      expect(cleanup).toBeDefined();
      expect(cleanup.isActive()).toBe(true);
    });

    it("should track cleanup creation in statistics", () => {
      const stats = getResourceStats();
      expect(stats.cleanupsCreated).toBeGreaterThan(0);
    });
  });

  describe("cleanup execution", () => {
    it("should execute cleanup function", () => {
      cleanup.execute();
      expect(mockCleanupFunction).toHaveBeenCalled();
    });

    it("should execute cleanup only once", () => {
      cleanup.execute();
      cleanup.execute(); // Second call should not trigger cleanup

      expect(mockCleanupFunction).toHaveBeenCalledTimes(1);
    });

    it("should handle cleanup function exceptions gracefully", () => {
      const throwingCleanup = jest.fn().mockImplementation(() => {
        throw new Error("Cleanup failed");
      });

      const throwingScopedCleanup = RAII_CLEANUP(throwingCleanup);

      expect(() => throwingScopedCleanup.execute()).not.toThrow();
      expect(throwingCleanup).toHaveBeenCalled();

      throwingScopedCleanup.execute(); // Should not throw again
    });
  });

  describe("cleanup state", () => {
    it("should report active state before execution", () => {
      expect(cleanup.isActive()).toBe(true);
    });

    it("should report inactive state after execution", () => {
      cleanup.execute();
      expect(cleanup.isActive()).toBe(false);
    });

    it("should track cleanup execution in statistics", () => {
      const initialStats = getResourceStats();
      cleanup.execute();
      const finalStats = getResourceStats();

      expect(finalStats.cleanupsExecuted).toBe(
        initialStats.cleanupsExecuted + 1
      );
    });
  });

  describe("automatic cleanup", () => {
    it("should execute cleanup on scope exit", () => {
      let cleanupExecuted = false;

      {
        const scopedCleanup = RAII_CLEANUP(() => {
          cleanupExecuted = true;
        });
        expect(scopedCleanup.isActive()).toBe(true);
      } // Automatic cleanup here

      expect(cleanupExecuted).toBe(true);
    });
  });
});

describe("ResourceManager", () => {
  let manager: ResourceManager;

  beforeEach(() => {
    manager = new ResourceManager();
  });

  afterEach(() => {
    if (manager) {
      manager.destroy();
    }
  });

  describe("construction and initialization", () => {
    it("should create resource manager", () => {
      expect(manager).toBeDefined();
      expect(manager.getResourceCount()).toBe(0);
    });

    it("should track manager creation in statistics", () => {
      const stats = getResourceStats();
      expect(stats.managersCreated).toBeGreaterThan(0);
    });
  });

  describe("resource tracking", () => {
    it("should track single resource", () => {
      const resource = { id: 1, data: "tracked" };
      const deleter = jest.fn();

      manager.track(resource, ResourceType.RESOURCE, deleter);

      expect(manager.getResourceCount()).toBe(1);
    });

    it("should track multiple resources", () => {
      const resources = [
        { id: 1, data: "resource-1" },
        { id: 2, data: "resource-2" },
        { id: 3, data: "resource-3" },
      ];

      resources.forEach((resource) => {
        manager.track(resource, ResourceType.RESOURCE, jest.fn());
      });

      expect(manager.getResourceCount()).toBe(3);
    });

    it("should track resources with different types", () => {
      const bufferResource = Buffer.from("buffer data");
      const filterResource = { id: 1, name: "filter" };

      manager.track(bufferResource, ResourceType.BUFFER, jest.fn());
      manager.track(filterResource, ResourceType.FILTER, jest.fn());

      expect(manager.getResourceCount()).toBe(2);
    });

    it("should not track null resources", () => {
      manager.track(null as any, ResourceType.RESOURCE, jest.fn());
      expect(manager.getResourceCount()).toBe(0);
    });
  });

  describe("resource cleanup", () => {
    it("should cleanup tracked resources", () => {
      const resource = { id: 1, data: "cleanup-test" };
      const deleter = jest.fn();

      manager.track(resource, ResourceType.RESOURCE, deleter);
      manager.cleanup();

      expect(deleter).toHaveBeenCalledWith(resource);
      expect(manager.getResourceCount()).toBe(0);
    });

    it("should cleanup multiple resources", () => {
      const resources = [
        { id: 1, data: "resource-1" },
        { id: 2, data: "resource-2" },
      ];
      const deleters = [jest.fn(), jest.fn()];

      resources.forEach((resource, index) => {
        manager.track(resource, ResourceType.RESOURCE, deleters[index]);
      });

      manager.cleanup();

      deleters.forEach((deleter) => {
        expect(deleter).toHaveBeenCalled();
      });
      expect(manager.getResourceCount()).toBe(0);
    });

    it("should handle deleter exceptions during cleanup", () => {
      const resource = { id: 1, data: "throwing" };
      const throwingDeleter = jest.fn().mockImplementation(() => {
        throw new Error("Cleanup failed");
      });

      manager.track(resource, ResourceType.RESOURCE, throwingDeleter);

      expect(() => manager.cleanup()).not.toThrow();
      expect(throwingDeleter).toHaveBeenCalled();
    });
  });

  describe("resource information", () => {
    it("should provide resource information", () => {
      const resource = { id: 1, data: "info-test" };
      manager.track(resource, ResourceType.RESOURCE, jest.fn());

      const info = manager.getResourceInfo();
      expect(info).toHaveLength(1);
      expect(info[0].resource).toBe(resource);
      expect(info[0].type).toBe(ResourceType.RESOURCE);
    });

    it("should provide resource statistics", () => {
      const bufferResource = Buffer.from("buffer");
      const filterResource = { id: 1, name: "filter" };

      manager.track(bufferResource, ResourceType.BUFFER, jest.fn());
      manager.track(filterResource, ResourceType.FILTER, jest.fn());

      const stats = manager.getStats();
      expect(stats.totalResources).toBe(2);
      expect(stats.resourceTypes).toHaveProperty(ResourceType.BUFFER);
      expect(stats.resourceTypes).toHaveProperty(ResourceType.FILTER);
    });
  });

  describe("leak detection", () => {
    it("should detect resource leaks", () => {
      const resource = { id: 1, data: "leak-test" };
      manager.track(resource, ResourceType.RESOURCE, jest.fn());

      const leaks = manager.detectLeaks();
      expect(leaks).toHaveLength(1);
      expect(leaks[0].resource).toBe(resource);
    });

    it("should report resource leaks", () => {
      const resource = { id: 1, data: "leak-report" };
      manager.track(resource, ResourceType.RESOURCE, jest.fn());

      const consoleSpy = jest.spyOn(console, "warn").mockImplementation();

      manager.reportLeaks();

      expect(consoleSpy).toHaveBeenCalled();
      consoleSpy.mockRestore();
    });

    it("should not detect leaks after cleanup", () => {
      const resource = { id: 1, data: "no-leak" };
      manager.track(resource, ResourceType.RESOURCE, jest.fn());

      manager.cleanup();

      const leaks = manager.detectLeaks();
      expect(leaks).toHaveLength(0);
    });
  });

  describe("manager lifecycle", () => {
    it("should destroy manager and cleanup resources", () => {
      const resource = { id: 1, data: "destroy-test" };
      const deleter = jest.fn();

      manager.track(resource, ResourceType.RESOURCE, deleter);
      manager.destroy();

      expect(deleter).toHaveBeenCalledWith(resource);
      expect(manager.getResourceCount()).toBe(0);
    });

    it("should handle multiple destroy calls gracefully", () => {
      expect(() => manager.destroy()).not.toThrow();
      expect(() => manager.destroy()).not.toThrow();
    });

    it("should track manager destruction in statistics", () => {
      const initialStats = getResourceStats();
      manager.destroy();
      const finalStats = getResourceStats();

      expect(finalStats.managersDestroyed).toBe(
        initialStats.managersDestroyed + 1
      );
    });
  });
});

describe("Global Resource Management Functions", () => {
  describe("makeResourceGuard", () => {
    it("should create resource guard with factory function", () => {
      const resource = { id: 1, data: "factory-test" };
      const deleter = jest.fn();

      const guard = makeResourceGuard(resource, ResourceType.RESOURCE, deleter);

      expect(guard).toBeDefined();
      expect(guard.get()).toBe(resource);
      expect(guard.isValid()).toBe(true);

      guard.destroy();
    });
  });

  describe("getResourceStats", () => {
    it("should provide global resource statistics", () => {
      const stats = getResourceStats();

      expect(stats).toBeDefined();
      expect(stats.guardsCreated).toBeGreaterThanOrEqual(0);
      expect(stats.guardsDestroyed).toBeGreaterThanOrEqual(0);
      expect(stats.transactionsCreated).toBeGreaterThanOrEqual(0);
      expect(stats.cleanupsCreated).toBeGreaterThanOrEqual(0);
    });
  });

  describe("checkResourceLeaks", () => {
    it("should check for resource leaks globally", () => {
      const leakCount = checkResourceLeaks();
      expect(leakCount).toBeGreaterThanOrEqual(0);
    });
  });

  describe("reportResourceLeaks", () => {
    it("should report resource leaks globally", () => {
      const consoleSpy = jest.spyOn(console, "warn").mockImplementation();

      reportResourceLeaks();

      expect(consoleSpy).toHaveBeenCalled();
      consoleSpy.mockRestore();
    });
  });
});

describe("Edge Cases and Error Handling", () => {
  describe("null and undefined handling", () => {
    it("should handle null resources gracefully", () => {
      const nullGuard = new ResourceGuard(
        null as any,
        ResourceType.RESOURCE,
        jest.fn()
      );
      expect(nullGuard.isValid()).toBe(false);
      nullGuard.destroy();
    });

    it("should handle undefined deleters gracefully", () => {
      const resource = { id: 1, data: "undefined-deleter" };
      const guard = new ResourceGuard(
        resource,
        ResourceType.RESOURCE,
        undefined as any
      );

      expect(() => guard.destroy()).not.toThrow();
      guard.destroy();
    });
  });

  describe("circular references", () => {
    it("should handle circular references in resources", () => {
      const circularResource: any = { id: 1, data: "circular" };
      circularResource.self = circularResource;

      const guard = new ResourceGuard(
        circularResource,
        ResourceType.RESOURCE,
        jest.fn()
      );
      expect(guard.isValid()).toBe(true);

      guard.destroy();
    });
  });

  describe("large resource collections", () => {
    it("should handle large numbers of resources", () => {
      const manager = new ResourceManager();
      const resources: any[] = [];

      // Create many resources
      for (let i = 0; i < 1000; i++) {
        const resource = { id: i, data: `resource-${i}` };
        resources.push(resource);
        manager.track(resource, ResourceType.RESOURCE, jest.fn());
      }

      expect(manager.getResourceCount()).toBe(1000);

      manager.cleanup();
      expect(manager.getResourceCount()).toBe(0);

      manager.destroy();
    });
  });

  describe("concurrent access", () => {
    it("should handle concurrent resource access", async () => {
      const manager = new ResourceManager();
      const promises = Array.from({ length: 100 }, (_, i) => {
        return new Promise<void>((resolve) => {
          const resource = { id: i, data: `concurrent-${i}` };
          manager.track(resource, ResourceType.RESOURCE, jest.fn());
          resolve();
        });
      });

      await Promise.all(promises);

      expect(manager.getResourceCount()).toBe(100);

      manager.cleanup();
      manager.destroy();
    });
  });
});
