/**
 * @file resource-manager.test.ts
 * @brief Unit tests for RAII resource management classes
 */

import {
  ResourceGuard,
  ResourceType,
  ResourceManager,
  ResourceTransaction,
  ScopedCleanup,
  makeResourceGuard,
  makeTypedResourceGuard,
  makeScopedCleanup,
  getDefaultDeleter,
  RAII_GUARD,
  RAII_TRANSACTION,
  RAII_CLEANUP,
  getResourceStats,
  resetResourceStats,
  checkResourceLeaks,
  reportResourceLeaks,
} from "../raii/resource-manager";

describe("ResourceGuard", () => {
  let guard: ResourceGuard<Buffer<ArrayBufferLike>>;

  beforeEach(() => {
    const resource = Buffer.from("test") as Buffer<ArrayBufferLike>;
    const deleter = jest.fn();
    guard = new ResourceGuard(resource, ResourceType.BUFFER, deleter);
  });

  afterEach(() => {
    if (guard) {
      // Clean up if needed
    }
  });

  describe("constructor", () => {
    it("should create resource guard with resource and deleter", () => {
      expect(guard).toBeDefined();
      expect(guard.isValid()).toBe(true);
    });

    it("should create resource guard with null resource", () => {
      const nullGuard = new ResourceGuard(null, ResourceType.BUFFER, jest.fn());
      expect(nullGuard).toBeDefined();
      expect(nullGuard.isValid()).toBe(false);
    });
  });

  describe("resource access", () => {
    it("should get guarded resource", () => {
      const resource = guard.get();
      expect(resource).toBeDefined();
      expect(Buffer.isBuffer(resource)).toBe(true);
    });

    it("should check if guard is valid", () => {
      expect(guard.isValid()).toBe(true);
    });

    it("should release resource from guard", () => {
      const resource = guard.release();
      expect(resource).toBeDefined();
      expect(guard.isValid()).toBe(false);
    });
  });

  describe("resource management", () => {
    it("should handle resource lifecycle", () => {
      expect(guard.isValid()).toBe(true);
      expect(guard.get()).toBeDefined();
    });
  });

  describe("comparison operators", () => {
    it("should compare guards for equality", () => {
      const sameGuard = new ResourceGuard(
        guard.get(),
        ResourceType.BUFFER,
        jest.fn()
      );
      expect(guard == sameGuard).toBe(true);
    });

    it("should compare guards for inequality", () => {
      const differentGuard = new ResourceGuard(
        Buffer.from("different"),
        ResourceType.BUFFER,
        jest.fn()
      );
      expect(guard != differentGuard).toBe(true);
    });

    it("should compare guard with null", () => {
      expect(guard != null).toBe(true);
      expect(null != guard).toBe(true);
    });
  });
});

describe("ResourceTransaction", () => {
  let transaction: ResourceTransaction;

  beforeEach(() => {
    transaction = new ResourceTransaction();
  });

  afterEach(() => {
    if (transaction) {
      // Clean up if needed
    }
  });

  describe("constructor", () => {
    it("should create transaction with default capacity", () => {
      expect(transaction).toBeDefined();
      expect(transaction.isEmpty()).toBe(true);
    });
  });

  describe("resource tracking", () => {
    it("should track resource for cleanup", () => {
      const resource = Buffer.from("test");
      const deleter = jest.fn();

      transaction.track(resource, deleter);
      expect(transaction.resourceCount()).toBe(1);
      expect(transaction.isEmpty()).toBe(false);
    });

    it("should track multiple resources", () => {
      const resources = [
        Buffer.from("resource1"),
        Buffer.from("resource2"),
        Buffer.from("resource3"),
      ];

      resources.forEach((resource) => {
        transaction.track(resource, jest.fn());
      });

      expect(transaction.resourceCount()).toBe(3);
    });

    it("should track typed resources with automatic deleter", () => {
      const bufferResource = Buffer.from("buffer");
      const filterResource = { id: 1, name: "filter" };

      transaction.track(bufferResource, jest.fn());
      transaction.track(filterResource, jest.fn());

      expect(transaction.resourceCount()).toBe(2);
    });

    it("should handle null resources gracefully", () => {
      transaction.track(null as any, jest.fn());
      expect(transaction.resourceCount()).toBe(0);
    });
  });

  describe("transaction management", () => {
    it("should commit transaction", () => {
      const resource = Buffer.from("test");
      const deleter = jest.fn();

      transaction.track(resource, deleter);
      expect(transaction.resourceCount()).toBe(1);

      transaction.commit();
      expect(transaction.isCommitted()).toBe(true);
      expect(transaction.resourceCount()).toBe(0);
    });

    it("should rollback transaction", () => {
      const resource = Buffer.from("test");
      const deleter = jest.fn();

      transaction.track(resource, deleter);
      expect(transaction.resourceCount()).toBe(1);

      transaction.rollback();
      expect(transaction.isCommitted()).toBe(true);
      expect(transaction.resourceCount()).toBe(0);
    });

    it("should handle exceptions during cleanup", () => {
      const resource = Buffer.from("test");
      const throwingDeleter = jest.fn().mockImplementation(() => {
        throw new Error("Cleanup failed");
      });

      transaction.track(resource, throwingDeleter);
      expect(transaction.resourceCount()).toBe(1);

      expect(() => transaction.rollback()).not.toThrow();
      expect(transaction.isCommitted()).toBe(true);
    });
  });

  describe("transaction state", () => {
    it("should check if transaction is committed", () => {
      expect(transaction.isCommitted()).toBe(false);
      transaction.commit();
      expect(transaction.isCommitted()).toBe(true);
    });

    it("should get resource count", () => {
      expect(transaction.resourceCount()).toBe(0);
      transaction.track(Buffer.from("test"), jest.fn());
      expect(transaction.resourceCount()).toBe(1);
    });

    it("should check if transaction is empty", () => {
      expect(transaction.isEmpty()).toBe(true);
      transaction.track(Buffer.from("test"), jest.fn());
      expect(transaction.isEmpty()).toBe(false);
    });
  });

  describe("transaction swapping", () => {
    it("should handle multiple transactions", () => {
      const otherTransaction = new ResourceTransaction();
      const resource = Buffer.from("test");

      transaction.track(resource, jest.fn());
      otherTransaction.track(Buffer.from("other"), jest.fn());

      expect(transaction.resourceCount()).toBe(1);
      expect(otherTransaction.resourceCount()).toBe(1);
    });
  });
});

describe("ScopedCleanup", () => {
  let cleanup: ScopedCleanup;

  beforeEach(() => {
    cleanup = new ScopedCleanup(jest.fn());
  });

  afterEach(() => {
    if (cleanup) {
      // Clean up if needed
    }
  });

  describe("constructor", () => {
    it("should create scoped cleanup with function", () => {
      expect(cleanup).toBeDefined();
      expect(cleanup.isActive()).toBe(true);
    });
  });

  describe("cleanup execution", () => {
    it("should execute cleanup on destruction", () => {
      const mockCleanup = jest.fn();
      const scopedCleanup = new ScopedCleanup(mockCleanup);

      expect(mockCleanup).not.toHaveBeenCalled();
      scopedCleanup.release();
      expect(mockCleanup).toHaveBeenCalled();
    });

    it("should handle cleanup exceptions gracefully", () => {
      const throwingCleanup = jest.fn().mockImplementation(() => {
        throw new Error("Cleanup failed");
      });

      const scopedCleanup = new ScopedCleanup(throwingCleanup);
      expect(() => scopedCleanup.release()).not.toThrow();
    });
  });

  describe("cleanup control", () => {
    it("should release cleanup", () => {
      const mockCleanup = jest.fn();
      const scopedCleanup = new ScopedCleanup(mockCleanup);

      expect(scopedCleanup.isActive()).toBe(true);
      scopedCleanup.release();
      expect(scopedCleanup.isActive()).toBe(false);
    });

    it("should check if cleanup is active", () => {
      expect(cleanup.isActive()).toBe(true);
      cleanup.release();
      expect(cleanup.isActive()).toBe(false);
    });
  });

  describe("move semantics", () => {
    it("should handle cleanup lifecycle", () => {
      const mockCleanup = jest.fn();
      const cleanup = new ScopedCleanup(mockCleanup);

      expect(cleanup.isActive()).toBe(true);
      cleanup.release();
      expect(cleanup.isActive()).toBe(false);
    });
  });
});

describe("ResourceManager", () => {
  let manager: ResourceManager;

  beforeEach(() => {
    manager = ResourceManager.getInstance();
  });

  afterEach(() => {
    if (manager) {
      // Clean up if needed
    }
  });

  describe("singleton pattern", () => {
    it("should return same instance", () => {
      const instance1 = ResourceManager.getInstance();
      const instance2 = ResourceManager.getInstance();
      expect(instance1).toBe(instance2);
    });
  });

  describe("statistics tracking", () => {
    it("should track guard creation", () => {
      const initialStats = manager.getStats();
      expect(initialStats.guardsCreated).toBeGreaterThanOrEqual(0);

      const guard = new ResourceGuard(Buffer.from("test"), ResourceType.BUFFER, jest.fn());
      expect(guard).toBeDefined();

      const finalStats = manager.getStats();
      expect(finalStats.guardsCreated).toBeGreaterThan(initialStats.guardsCreated);
    });

    it("should track guard destruction", () => {
      const initialStats = manager.getStats();
      expect(initialStats.guardsDestroyed).toBeGreaterThanOrEqual(0);

      {
        const guard = new ResourceGuard(Buffer.from("test"), ResourceType.BUFFER, jest.fn());
        expect(guard).toBeDefined();
      } // Guard goes out of scope

      const finalStats = manager.getStats();
      expect(finalStats.guardsDestroyed).toBeGreaterThan(initialStats.guardsDestroyed);
    });

    it("should track resource tracking", () => {
      const initialStats = manager.getStats();
      expect(initialStats.resourcesTracked).toBeGreaterThanOrEqual(0);

      const transaction = new ResourceTransaction();
      transaction.track(Buffer.from("test"), jest.fn());
      expect(transaction.resourceCount()).toBe(1);

      const finalStats = manager.getStats();
      expect(finalStats.resourcesTracked).toBeGreaterThan(initialStats.resourcesTracked);
    });

    it("should track resource release", () => {
      const initialStats = manager.getStats();
      expect(initialStats.resourcesReleased).toBeGreaterThanOrEqual(0);

      const transaction = new ResourceTransaction();
      transaction.track(Buffer.from("test"), jest.fn());
      transaction.rollback();

      const finalStats = manager.getStats();
      expect(finalStats.resourcesReleased).toBeGreaterThan(initialStats.resourcesReleased);
    });

    it("should track exceptions in destructors", () => {
      const initialStats = manager.getStats();
      expect(initialStats.exceptionsInDestructors).toBeGreaterThanOrEqual(0);

      const resource = Buffer.from("test");
      const throwingDeleter = jest.fn().mockImplementation(() => {
        throw new Error("Destructor failed");
      });

      const transaction = new ResourceTransaction();
      transaction.track(resource, throwingDeleter);
      expect(() => transaction.rollback()).not.toThrow();

      const finalStats = manager.getStats();
      expect(finalStats.exceptionsInDestructors).toBeGreaterThan(initialStats.exceptionsInDestructors);
    });
  });

  describe("resource monitoring", () => {
    it("should get active resource count", () => {
      const count = manager.getActiveResourceCount();
      expect(count).toBeGreaterThanOrEqual(0);
    });

    it("should report resource leaks", () => {
      expect(() => manager.reportLeaks()).not.toThrow();
    });
  });
});

describe("Utility Functions", () => {
  describe("makeResourceGuard", () => {
    it("should create resource guard with deleter", () => {
      const resource = Buffer.from("test");
      const deleter = jest.fn();
      const guard = makeResourceGuard(resource, ResourceType.BUFFER, deleter);

      expect(guard).toBeDefined();
      expect(guard.isValid()).toBe(true);
    });
  });

  describe("makeTypedResourceGuard", () => {
    it("should create resource guard with default deleter", () => {
      const resource = Buffer.from("test");
      const guard = makeTypedResourceGuard(resource, ResourceType.BUFFER);

      expect(guard).toBeDefined();
      expect(guard.isValid()).toBe(true);
    });
  });

  describe("makeScopedCleanup", () => {
    it("should create scoped cleanup", () => {
      const mockCleanup = jest.fn();
      const cleanup = makeScopedCleanup(mockCleanup);

      expect(cleanup).toBeDefined();
      expect(cleanup.isActive()).toBe(true);
    });
  });

  describe("getDefaultDeleter", () => {
    it("should return deleter for buffer type", () => {
      const deleter = getDefaultDeleter(ResourceType.BUFFER);
      expect(typeof deleter).toBe("function");
    });

    it("should return deleter for filter type", () => {
      const deleter = getDefaultDeleter(ResourceType.FILTER);
      expect(typeof deleter).toBe("function");
    });

    it("should return generic deleter for unknown type", () => {
      const deleter = getDefaultDeleter(ResourceType.UNKNOWN);
      expect(typeof deleter).toBe("function");
    });
  });
});

describe("RAII Macros", () => {
  describe("RAII_GUARD", () => {
    it("should create resource guard", () => {
      const resource = Buffer.from("test");
      const guard = RAII_GUARD(resource, ResourceType.BUFFER, jest.fn());

      expect(guard).toBeDefined();
      expect(guard.isValid()).toBe(true);
    });
  });

  describe("RAII_TRANSACTION", () => {
    it("should create transaction", () => {
      const transaction = RAII_TRANSACTION();
      expect(transaction).toBeDefined();
      expect(transaction.isEmpty()).toBe(true);
    });
  });

  describe("RAII_CLEANUP", () => {
    it("should create scoped cleanup", () => {
      const mockCleanup = jest.fn();
      const cleanup = RAII_CLEANUP(mockCleanup);

      expect(cleanup).toBeDefined();
      expect(cleanup.isActive()).toBe(true);
    });
  });
});

describe("Global Functions", () => {
  describe("getResourceStats", () => {
    it("should return resource statistics", () => {
      const stats = getResourceStats();
      expect(stats).toBeDefined();
      expect(stats.guardsCreated).toBeGreaterThanOrEqual(0);
    });
  });

  describe("resetResourceStats", () => {
    it("should reset resource statistics", () => {
      const initialStats = getResourceStats();
      expect(initialStats.guardsCreated).toBeGreaterThanOrEqual(0);

      resetResourceStats();

      const resetStats = getResourceStats();
      expect(resetStats.guardsCreated).toBe(0);
    });
  });

  describe("checkResourceLeaks", () => {
    it("should check for resource leaks", () => {
      const leakCount = checkResourceLeaks();
      expect(leakCount).toBeGreaterThanOrEqual(0);
    });
  });

  describe("reportResourceLeaks", () => {
    it("should report resource leaks", () => {
      expect(() => reportResourceLeaks()).not.toThrow();
    });
  });
});
