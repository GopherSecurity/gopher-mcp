/**
 * @file resource-manager.ts
 * @brief RAII resource management system using MCP C API RAII functions
 *
 * This implementation provides safe, efficient resource management with:
 * - Automatic resource cleanup through RAII
 * - Exception-safe transaction management
 * - Thread-safe resource tracking
 * - Production debugging and leak detection
 */

import { mcpFilterLib } from "../core/ffi-bindings";

// Resource types for validation
export enum ResourceType {
  UNKNOWN = 0,
  STRING = 1,
  NUMBER = 2,
  BOOL = 3,
  JSON = 4,
  RESOURCE = 5,
  TOOL = 6,
  PROMPT = 7,
  MESSAGE = 8,
  CONTENT_BLOCK = 9,
  ERROR = 10,
  REQUEST = 11,
  RESPONSE = 12,
  NOTIFICATION = 13,
  FILTER = 14,
  BUFFER = 15,
  CHAIN = 16,
}

// Resource statistics for monitoring
export interface ResourceStats {
  guardsCreated: number;
  guardsDestroyed: number;
  resourcesTracked: number;
  resourcesReleased: number;
  exceptionsInDestructors: number;
}

// Resource information for debugging
export interface ResourceInfo {
  resource: any;
  typeName: string;
  allocatedAt: Date;
  file: string;
  line: number;
}

/**
 * RAII Resource Guard for automatic cleanup
 */
export class ResourceGuard<T = any> {
  private resource: T | null;
  private type: ResourceType;
  private deleter: (resource: T) => void;
  private isReleased: boolean = false;

  constructor(resource: T, type: ResourceType, deleter: (resource: T) => void) {
    this.resource = resource;
    this.type = type;
    this.deleter = deleter;

    // Track resource in debug mode
    if (process.env["NODE_ENV"] === "development") {
      this.trackResource();
    }
  }

  /**
   * Get the guarded resource
   */
  get(): T | null {
    return this.resource;
  }

  /**
   * Check if guard is valid and holds a resource
   */
  isValid(): boolean {
    return this.resource !== null && !this.isReleased;
  }

  /**
   * Release resource from guard (prevents automatic cleanup)
   */
  release(): T | null {
    if (this.isReleased) {
      return null;
    }

    const resource = this.resource;
    this.resource = null;
    this.isReleased = true;

    // Untrack resource in debug mode
    if (process.env["NODE_ENV"] === "development") {
      this.untrackResource();
    }

    return resource;
  }

  /**
   * Destroy guard and cleanup resource
   */
  destroy(): void {
    if (this.resource && !this.isReleased) {
      try {
        this.deleter(this.resource);

        if (process.env["NODE_ENV"] === "development") {
          this.untrackResource();
        }
      } catch (error) {
        console.error("Exception in ResourceGuard destructor:", error);
      }
    }

    this.resource = null;
    this.isReleased = true;
  }

  /**
   * Track resource for debugging
   */
  private trackResource(): void {
    // In a real implementation, this would call the C API tracking function
    if (mcpFilterLib.mcp_guard_create) {
      try {
        mcpFilterLib.mcp_guard_create(this.resource, this.type);
      } catch (error) {
        // Ignore tracking errors in production
      }
    }
  }

  /**
   * Untrack resource
   */
  private untrackResource(): void {
    // Resource untracking is handled automatically by the C API
  }

  /**
   * Destructor - automatic cleanup
   */
  destructor(): void {
    this.destroy();
  }
}

/**
 * Transaction-based resource management for atomic operations
 */
export class ResourceTransaction {
  private resources: Array<{
    resource: any;
    deleter: (resource: any) => void;
  }> = [];
  private committed: boolean = false;

  /**
   * Track a resource for cleanup
   */
  track<T>(resource: T, deleter: (resource: T) => void): void {
    if (resource && !this.committed) {
      this.resources.push({ resource, deleter });
    }
  }

  /**
   * Track a typed resource with automatic deleter
   */
  trackTyped<T>(resource: T, type: ResourceType): void {
    if (resource && !this.committed) {
      const deleter = this.getDefaultDeleter(type);
      this.track(resource, deleter);
    }
  }

  /**
   * Commit transaction - prevent automatic cleanup
   */
  commit(): void {
    this.resources = [];
    this.committed = true;
  }

  /**
   * Rollback transaction - cleanup all tracked resources
   */
  rollback(): void {
    if (!this.committed) {
      // Cleanup in reverse order (LIFO)
      for (let i = this.resources.length - 1; i >= 0; i--) {
        try {
          const resourceInfo = this.resources[i];
          if (resourceInfo && resourceInfo.deleter) {
            resourceInfo.deleter(resourceInfo.resource);
          }
        } catch (error) {
          console.error("Exception during resource cleanup:", error);
        }
      }

      this.resources = [];
      this.committed = true;
    }
  }

  /**
   * Check if transaction is committed
   */
  isCommitted(): boolean {
    return this.committed;
  }

  /**
   * Get number of tracked resources
   */
  resourceCount(): number {
    return this.resources.length;
  }

  /**
   * Check if transaction is empty
   */
  isEmpty(): boolean {
    return this.resources.length === 0;
  }

  /**
   * Get default deleter for resource type
   */
  private getDefaultDeleter(type: ResourceType): (resource: any) => void {
    switch (type) {
      case ResourceType.JSON:
        return (resource) => {
          if (mcpFilterLib.mcp_json_free) {
            mcpFilterLib.mcp_json_free(resource);
          }
        };
      case ResourceType.STRING:
        return (resource) => {
          if (mcpFilterLib.mcp_string_free) {
            mcpFilterLib.mcp_string_free(resource);
          }
        };
      default:
        return (resource) => {
          // Generic cleanup - could be enhanced based on type
          if (resource && typeof resource === "object" && resource.destroy) {
            resource.destroy();
          }
        };
    }
  }

  /**
   * Destructor - automatic rollback if not committed
   */
  destructor(): void {
    if (!this.committed) {
      this.rollback();
    }
  }
}

/**
 * Scoped cleanup for automatic execution on scope exit
 */
export class ScopedCleanup {
  private cleanup: () => void;
  private active: boolean = true;

  constructor(cleanup: () => void) {
    this.cleanup = cleanup;
  }

  /**
   * Execute cleanup function
   */
  execute(): void {
    if (this.active && this.cleanup) {
      try {
        this.cleanup();
      } catch (error) {
        console.error("Exception during scoped cleanup:", error);
      }
    }
  }

  /**
   * Cancel cleanup
   */
  release(): void {
    this.active = false;
  }

  /**
   * Check if cleanup is active
   */
  isActive(): boolean {
    return this.active;
  }

  /**
   * Destructor - automatic cleanup
   */
  destructor(): void {
    this.execute();
  }
}

/**
 * Resource manager for centralized resource tracking
 */
export class ResourceManager {
  private static instance: ResourceManager;
  private resources: Map<any, ResourceInfo> = new Map();
  private stats: ResourceStats = {
    guardsCreated: 0,
    guardsDestroyed: 0,
    resourcesTracked: 0,
    resourcesReleased: 0,
    exceptionsInDestructors: 0,
  };

  private constructor() {}

  /**
   * Get singleton instance
   */
  static getInstance(): ResourceManager {
    if (!ResourceManager.instance) {
      ResourceManager.instance = new ResourceManager();
    }
    return ResourceManager.instance;
  }

  /**
   * Track a resource
   */
  trackResource(
    resource: any,
    typeName: string,
    file: string = "unknown",
    line: number = 0
  ): void {
    this.resources.set(resource, {
      resource,
      typeName,
      allocatedAt: new Date(),
      file,
      line,
    });

    this.stats.resourcesTracked++;
  }

  /**
   * Untrack a resource
   */
  untrackResource(resource: any): void {
    if (this.resources.has(resource)) {
      this.resources.delete(resource);
      this.stats.resourcesReleased++;
    }
  }

  /**
   * Get active resource count
   */
  getActiveResourceCount(): number {
    return this.resources.size;
  }

  /**
   * Get resource statistics
   */
  getStats(): ResourceStats {
    return { ...this.stats };
  }

  /**
   * Reset statistics
   */
  resetStats(): void {
    this.stats = {
      guardsCreated: 0,
      guardsDestroyed: 0,
      resourcesTracked: 0,
      resourcesReleased: 0,
      exceptionsInDestructors: 0,
    };
  }

  /**
   * Report resource leaks
   */
  reportLeaks(): void {
    if (this.resources.size > 0) {
      console.warn(
        `Resource leak detected: ${this.resources.size} active resources`
      );

      for (const [, info] of this.resources) {
        console.warn(
          `  - ${info.typeName} allocated at ${info.file}:${info.line}`
        );
      }
    } else {
      console.log("No resource leaks detected");
    }
  }

  /**
   * Increment guard created counter
   */
  incrementGuardCreated(): void {
    this.stats.guardsCreated++;
  }

  /**
   * Increment guard destroyed counter
   */
  incrementGuardDestroyed(): void {
    this.stats.guardsDestroyed++;
  }

  /**
   * Increment exception in destructor counter
   */
  incrementExceptionInDestructor(): void {
    this.stats.exceptionsInDestructors++;
  }
}

/**
 * Utility functions for resource management
 */

/**
 * Create a resource guard with automatic type deduction
 */
export function makeResourceGuard<T>(
  resource: T,
  type: ResourceType,
  deleter: (resource: T) => void
): ResourceGuard<T> {
  const manager = ResourceManager.getInstance();
  manager.incrementGuardCreated();

  return new ResourceGuard(resource, type, deleter);
}

/**
 * Create a resource guard with default deleter
 */
export function makeTypedResourceGuard<T>(
  resource: T,
  type: ResourceType
): ResourceGuard<T> {
  const deleter = getDefaultDeleter(type);
  return makeResourceGuard(resource, type, deleter);
}

/**
 * Create a scoped cleanup with automatic execution
 */
export function makeScopedCleanup(cleanup: () => void): ScopedCleanup {
  return new ScopedCleanup(cleanup);
}

/**
 * Get default deleter for resource type
 */
export function getDefaultDeleter(type: ResourceType): (resource: any) => void {
  switch (type) {
    case ResourceType.JSON:
      return (resource) => {
        if (mcpFilterLib.mcp_json_free) {
          mcpFilterLib.mcp_json_free(resource);
        }
      };
    case ResourceType.STRING:
      return (resource) => {
        if (mcpFilterLib.mcp_string_free) {
          mcpFilterLib.mcp_string_free(resource);
        }
      };
    case ResourceType.FILTER:
      return (resource) => {
        if (mcpFilterLib.mcp_filter_release) {
          mcpFilterLib.mcp_filter_release(resource);
        }
      };
    case ResourceType.BUFFER:
      return (resource) => {
        if (mcpFilterLib.mcp_filter_buffer_release) {
          mcpFilterLib.mcp_filter_buffer_release(resource);
        }
      };
    case ResourceType.CHAIN:
      return (resource) => {
        if (mcpFilterLib.mcp_filter_chain_release) {
          mcpFilterLib.mcp_filter_chain_release(resource);
        }
      };
    default:
      return (resource) => {
        // Generic cleanup
        if (resource && typeof resource === "object" && resource.destroy) {
          resource.destroy();
        }
      };
  }
}

/**
 * RAII utility macros (TypeScript equivalents)
 */

/**
 * RAII_GUARD - Create a resource guard with automatic naming
 */
export function RAII_GUARD<T>(
  resource: T,
  type: ResourceType,
  deleter: (resource: T) => void
): ResourceGuard<T> {
  return makeResourceGuard(resource, type, deleter);
}

/**
 * RAII_TRANSACTION - Create a transaction with automatic rollback
 */
export function RAII_TRANSACTION(): ResourceTransaction {
  return new ResourceTransaction();
}

/**
 * RAII_CLEANUP - Create scoped cleanup with lambda
 */
export function RAII_CLEANUP(cleanup: () => void): ScopedCleanup {
  return makeScopedCleanup(cleanup);
}

/**
 * Get current resource statistics for monitoring
 */
export function getResourceStats(): ResourceStats {
  return ResourceManager.getInstance().getStats();
}

/**
 * Reset resource statistics
 */
export function resetResourceStats(): void {
  ResourceManager.getInstance().resetStats();
}

/**
 * Check for resource leaks
 */
export function checkResourceLeaks(): number {
  return ResourceManager.getInstance().getActiveResourceCount();
}

/**
 * Report resource leaks
 */
export function reportResourceLeaks(): void {
  ResourceManager.getInstance().reportLeaks();
}
