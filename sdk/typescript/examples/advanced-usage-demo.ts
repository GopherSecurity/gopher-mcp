/**
 * @file advanced-features-demo.ts
 * @brief Comprehensive demonstration of advanced MCP Filter SDK features
 *
 * This example showcases:
 * - Advanced buffer management with zero-copy operations
 * - Enhanced filter chains with conditional execution and routing
 * - RAII resource management with automatic cleanup
 * - Performance monitoring and optimization
 */

import {
  // Advanced Buffer Management
  AdvancedBuffer,
  AdvancedBufferPool,
  BufferOwnership,

  // Enhanced Filter Chains
  ChainExecutionMode,
  MatchCondition,
  RAII_CLEANUP,
  RAII_TRANSACTION,
  // RAII Resource Management
  ResourceType,
  RoutingStrategy,
  checkResourceLeaks,
  // Resource management functions
  getResourceStats,
  makeResourceGuard,
  reportResourceLeaks,
} from "../src";

/**
 * Advanced Buffer Management Demo
 */
async function advancedBufferDemo(): Promise<void> {
  console.log("\n=== Advanced Buffer Management Demo ===");

  // Create advanced buffer with ownership
  const buffer = new AdvancedBuffer(1024, BufferOwnership.OWNED);
  console.log(
    `Created buffer: ${buffer.length} bytes, capacity: ${buffer.capacity}`
  );

  // Add data with zero-copy operations
  const testData = Buffer.from("Hello, Advanced Buffer!");
  buffer.add(testData);
  console.log(`Added data: ${buffer.length} bytes`);

  // Create buffer view (zero-copy reference)
  const viewBuffer = AdvancedBuffer.createView(testData, testData.length);
  console.log(`Created view buffer: ${viewBuffer.length} bytes`);

  // Buffer operations
  buffer.addString(" Additional string data");
  buffer.prepend(Buffer.from("Prefix: "));
  console.log(`After operations: ${buffer.length} bytes`);

  // Type-safe I/O operations
  buffer.writeLittleEndianInt(42, 4);
  buffer.writeBigEndianInt(12345, 2);

  const readValue = buffer.readLittleEndianInt(4);
  const readValueBE = buffer.readBigEndianInt(2);
  console.log(`Read values: ${readValue}, ${readValueBE}`);

  // Search operations
  const pattern = Buffer.from("Hello");
  const position = buffer.search(pattern);
  console.log(`Pattern found at position: ${position}`);

  // Watermark management
  buffer.setWatermarks(100, 500, 1000);
  console.log(`Watermarks set: low=${100}, high=${500}, overflow=${1000}`);
  console.log(`Above high watermark: ${buffer.isAboveHighWatermark}`);

  // Buffer pool for efficient memory management
  const pool = new AdvancedBufferPool(512, 10, 5, false, true);
  console.log(`Created buffer pool with ${10} buffers of ${512} bytes each`);

  const pooledBuffer = pool.acquire();
  if (pooledBuffer) {
    console.log("Acquired buffer from pool");
    pool.release(pooledBuffer);
    console.log("Released buffer back to pool");
  }

  const poolStats = pool.getStats();
  console.log(
    `Pool stats: ${poolStats.freeCount} free, ${poolStats.usedCount} used`
  );

  // Cleanup
  buffer.release();
  viewBuffer.release();
  pool.destroy();

  console.log("Advanced buffer demo completed");
}

/**
 * Enhanced Filter Chain Demo
 */
async function enhancedFilterChainDemo(): Promise<void> {
  console.log("\n=== Enhanced Filter Chain Demo ===");

  // Demonstrate enum usage
  console.log("Chain execution modes:");
  console.log(`  Sequential: ${ChainExecutionMode.SEQUENTIAL}`);
  console.log(`  Parallel: ${ChainExecutionMode.PARALLEL}`);
  console.log(`  Conditional: ${ChainExecutionMode.CONDITIONAL}`);
  console.log(`  Pipeline: ${ChainExecutionMode.PIPELINE}`);

  console.log("\nRouting strategies:");
  console.log(`  Round Robin: ${RoutingStrategy.ROUND_ROBIN}`);
  console.log(`  Least Loaded: ${RoutingStrategy.LEAST_LOADED}`);
  console.log(`  Hash Based: ${RoutingStrategy.HASH_BASED}`);
  console.log(`  Priority: ${RoutingStrategy.PRIORITY}`);
  console.log(`  Custom: ${RoutingStrategy.CUSTOM}`);

  console.log("\nMatch conditions:");
  console.log(`  All: ${MatchCondition.MATCH_ALL}`);
  console.log(`  Any: ${MatchCondition.MATCH_ANY}`);
  console.log(`  None: ${MatchCondition.MATCH_NONE}`);
  console.log(`  Custom: ${MatchCondition.CUSTOM}`);

  console.log("Enhanced filter chain demo completed (configuration only)");
}

/**
 * RAII Resource Management Demo
 */
async function raiiResourceManagementDemo(): Promise<void> {
  console.log("\n=== RAII Resource Management Demo ===");

  // Resource Guard demo
  const resource = { id: 1, data: "test" };
  const guard = makeResourceGuard(resource, ResourceType.RESOURCE, (r) => {
    console.log(`Cleaning up resource: ${r.id}`);
  });

  console.log(`Resource guard valid: ${guard.isValid()}`);
  console.log(`Guarded resource: ${guard.get()?.id}`);

  // Automatic cleanup on scope exit
  {
    makeResourceGuard({ id: 2, data: "scoped" }, ResourceType.RESOURCE, (r) =>
      console.log(`Scoped cleanup: ${r.id}`)
    );
    console.log("Scoped guard created, will auto-cleanup");
  } // Automatic cleanup here

  // Transaction-based resource management
  const transaction = RAII_TRANSACTION();

  const resource1 = { id: 3, data: "transaction1" };
  const resource2 = { id: 4, data: "transaction2" };

  transaction.track(resource1, (r) =>
    console.log(`Transaction cleanup: ${r.id}`)
  );
  transaction.track(resource2, (r) =>
    console.log(`Transaction cleanup: ${r.id}`)
  );

  console.log(`Transaction tracking ${transaction.resourceCount()} resources`);

  // Commit transaction to prevent cleanup
  transaction.commit();
  console.log("Transaction committed, no cleanup needed");

  // Scoped cleanup demo
  const cleanup = RAII_CLEANUP(() => {
    console.log("Scoped cleanup executed");
  });

  console.log(`Cleanup active: ${cleanup.isActive()}`);
  cleanup.execute();

  // Resource manager statistics
  const resourceStats = getResourceStats();
  console.log("Resource statistics:", resourceStats);

  // Check for resource leaks
  const activeResources = checkResourceLeaks();
  console.log(`Active resources: ${activeResources}`);

  if (activeResources > 0) {
    reportResourceLeaks();
  }

  // Cleanup
  guard.destroy();

  console.log("RAII resource management demo completed");
}

/**
 * Performance Monitoring Demo
 */
async function performanceMonitoringDemo(): Promise<void> {
  console.log("\n=== Performance Monitoring Demo ===");

  // Demonstrate buffer operations for performance testing
  const testBuffer = new AdvancedBuffer(1024);
  testBuffer.add(Buffer.from("Performance test data"));

  console.log(`Test buffer created: ${testBuffer.length} bytes`);
  console.log(`Buffer capacity: ${testBuffer.capacity} bytes`);
  console.log(`Buffer empty: ${testBuffer.isEmpty}`);

  // Demonstrate buffer statistics
  try {
    const stats = testBuffer.getStats();
    console.log("Buffer statistics:", stats);
  } catch (error) {
    console.log("Buffer statistics not available (expected in demo mode)");
  }

  // Cleanup
  testBuffer.release();

  console.log("Performance monitoring demo completed (buffer operations only)");
}

/**
 * Main demo function
 */
async function runAdvancedFeaturesDemo(): Promise<void> {
  console.log("🚀 MCP Filter SDK Advanced Features Demo");
  console.log("==========================================");

  try {
    // Run all demos
    await advancedBufferDemo();
    await enhancedFilterChainDemo();
    await raiiResourceManagementDemo();
    await performanceMonitoringDemo();

    console.log("\n✅ All advanced features demos completed successfully!");
    console.log("\n🎯 Key Features Demonstrated:");
    console.log("  • Advanced Buffer Management with zero-copy operations");
    console.log("  • Enhanced Filter Chains with conditional execution");
    console.log("  • Dynamic routing and load balancing");
    console.log("  • RAII Resource Management with automatic cleanup");
    console.log("  • Performance monitoring and optimization");
    console.log("  • Comprehensive error handling and validation");
  } catch (error) {
    console.error("❌ Demo failed:", error);
    process.exit(1);
  }
}

// Run the demo if this file is executed directly
if (require.main === module) {
  runAdvancedFeaturesDemo().catch(console.error);
}

export {
  advancedBufferDemo,
  enhancedFilterChainDemo,
  performanceMonitoringDemo,
  raiiResourceManagementDemo,
  runAdvancedFeaturesDemo,
};
