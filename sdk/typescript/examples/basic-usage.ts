/**
 * @file basic-usage.ts
 * @brief Basic usage example for MCP Filter SDK
 *
 * This example demonstrates the basic usage of the MCP Filter SDK,
 * including initialization, filter creation, buffer operations, and cleanup.
 */

import {
  McpBufferFlag,
  McpBuiltinFilterType,
  McpFilterConfig,
  McpFilterSdk,
  McpProtocolLayer,
} from "../src";

async function basicUsageExample() {
  console.log("=== MCP Filter SDK Basic Usage Example ===\n");

  // Create SDK instance with configuration
  const sdk = new McpFilterSdk({
    enableLogging: true,
    logLevel: "info",
    memoryPoolSize: 2 * 1024 * 1024, // 2MB
    maxFilters: 100,
    maxChains: 10,
    maxBuffers: 1000,
  });

  try {
    // Step 1: Initialize the SDK
    console.log("1. Initializing SDK...");
    const initResult = await sdk.initialize();
    if (initResult.result !== 0) {
      throw new Error(`Failed to initialize SDK: ${initResult.error}`);
    }
    console.log("✓ SDK initialized successfully\n");

    // Step 2: Create a custom filter
    console.log("2. Creating custom filter...");
    const filterConfig: McpFilterConfig = {
      name: "example_custom_filter",
      type: McpBuiltinFilterType.CUSTOM,
      layer: McpProtocolLayer.LAYER_7_APPLICATION,
      settings: 0, // No JSON settings for this example
      memoryPool: null, // Use default memory pool
    };

    const filterResult = await sdk.createFilter(filterConfig);
    if (filterResult.result !== 0) {
      throw new Error(`Failed to create filter: ${filterResult.error}`);
    }

    const filter = filterResult.data;
    if (!filter) {
      throw new Error("Filter creation returned no data");
    }
    console.log(`✓ Custom filter created: ${filter}\n`);

    // Step 3: Create a built-in filter
    console.log("3. Creating built-in HTTP codec filter...");
    const httpFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.HTTP_CODEC
    );
    if (httpFilterResult.result !== 0) {
      throw new Error(
        `Failed to create HTTP filter: ${httpFilterResult.error}`
      );
    }

    const httpFilter = httpFilterResult.data;
    if (!httpFilter) {
      throw new Error("HTTP filter creation returned no data");
    }
    console.log(`✓ HTTP codec filter created: ${httpFilter}\n`);

    // Step 4: Create buffers with different flags
    console.log("4. Creating buffers...");

    // Create a readonly buffer with external data
    const readonlyBufferResult = await sdk.createBuffer(
      Buffer.from("This is readonly data"),
      McpBufferFlag.READONLY | McpBufferFlag.EXTERNAL
    );
    if (readonlyBufferResult.result !== 0) {
      throw new Error(
        `Failed to create readonly buffer: ${readonlyBufferResult.error}`
      );
    }
    const readonlyBuffer = readonlyBufferResult.data;
    if (!readonlyBuffer) {
      throw new Error("Readonly buffer creation returned no data");
    }
    console.log(`✓ Readonly buffer created: ${readonlyBuffer}`);

    // Create an owned buffer for writing
    const ownedBufferResult = await sdk.createBuffer(
      Buffer.from("This is writable data"),
      McpBufferFlag.OWNED
    );
    if (ownedBufferResult.result !== 0) {
      throw new Error(
        `Failed to create owned buffer: ${ownedBufferResult.error}`
      );
    }
    const ownedBuffer = ownedBufferResult.data;
    if (!ownedBuffer) {
      throw new Error("Owned buffer creation returned no data");
    }
    console.log(`✓ Owned buffer created: ${ownedBuffer}\n`);

    // Step 5: Create a memory pool
    console.log("5. Creating custom memory pool...");
    const poolResult = await sdk.createMemoryPool(1024 * 1024); // 1MB
    if (poolResult.result !== 0) {
      throw new Error(`Failed to create memory pool: ${poolResult.error}`);
    }
    const memoryPool = poolResult.data;
    if (!memoryPool) {
      throw new Error("Memory pool creation returned no data");
    }
    console.log(`✓ Memory pool created: ${memoryPool}\n`);

    // Step 6: Display SDK statistics
    console.log("6. SDK Statistics:");
    const stats = sdk.getStats();
    console.log(`   Total Filters: ${stats.totalFilters}`);
    console.log(`   Total Chains: ${stats.totalChains}`);
    console.log(`   Total Buffers: ${stats.totalBuffers}`);
    console.log(`   Total Memory Pools: ${stats.totalMemoryPools}`);
    console.log(
      `   Total Memory Allocated: ${(
        stats.totalMemoryAllocated /
        1024 /
        1024
      ).toFixed(2)} MB`
    );
    console.log(`   Uptime: ${(stats.uptime / 1000).toFixed(2)} seconds\n`);

    // Step 7: Clean up individual resources
    console.log("7. Cleaning up individual resources...");

    await sdk.destroyBuffer(readonlyBuffer);
    console.log("✓ Readonly buffer destroyed");

    await sdk.destroyBuffer(ownedBuffer);
    console.log("✓ Owned buffer destroyed");

    await sdk.destroyMemoryPool(memoryPool);
    console.log("✓ Memory pool destroyed");

    await sdk.destroyFilter(httpFilter);
    console.log("✓ HTTP filter destroyed");

    await sdk.destroyFilter(filter);
    console.log("✓ Custom filter destroyed\n");

    // Step 8: Display final statistics
    console.log("8. Final SDK Statistics:");
    const finalStats = sdk.getStats();
    console.log(`   Total Filters: ${finalStats.totalFilters}`);
    console.log(`   Total Buffers: ${finalStats.totalBuffers}`);
    console.log(`   Total Memory Pools: ${finalStats.totalMemoryPools}\n`);

    console.log("✓ Basic usage example completed successfully!");
  } catch (error) {
    console.error("❌ Example failed:", error);
    throw error;
  } finally {
    // Step 9: Shutdown the SDK
    console.log("\n9. Shutting down SDK...");
    const shutdownResult = await sdk.shutdown();
    if (shutdownResult.result === 0) {
      console.log("✓ SDK shut down successfully");
    } else {
      console.error(`❌ SDK shutdown failed: ${shutdownResult.error}`);
    }
  }
}

// Run the example
if (require.main === module) {
  basicUsageExample()
    .then(() => {
      console.log("\n🎉 Example completed successfully!");
      process.exit(0);
    })
    .catch((error) => {
      console.error("\n💥 Example failed:", error);
      process.exit(1);
    });
}

export { basicUsageExample };
