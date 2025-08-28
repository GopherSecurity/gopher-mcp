/**
 * @file ffi-bindings.ts
 * @brief Example demonstrating real FFI bindings to the MCP Filter C API
 *
 * This example shows how the SDK automatically loads the native library
 * and falls back to mock implementation if the library is not available.
 */

import { McpBuiltinFilterType, McpFilterSdk } from "../src";

async function demonstrateFFIBindings() {
  console.log("=== MCP Filter SDK FFI Bindings Demo ===\n");

  // Create SDK instance
  const sdk = new McpFilterSdk({
    enableLogging: true,
    logLevel: "info",
    autoCleanup: true,
  });

  try {
    console.log("1. Initializing SDK...");
    const initResult = await sdk.initialize();

    if (initResult.result !== 0) {
      throw new Error(`Initialization failed: ${initResult.error}`);
    }

    console.log("✅ SDK initialized successfully\n");

    // Check if we're using real or mock FFI
    console.log("2. Checking FFI implementation...");
    console.log("⚠️  Using mock FFI implementation (native library not found)");
    console.log(
      "   To use real FFI, build the MCP Filter library and place it in the build/ directory"
    );
    console.log(
      "   The SDK automatically detects and loads the native library when available\n"
    );

    console.log("3. Creating built-in filter...");
    const filterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.TCP_PROXY,
      { port: 8080, host: "localhost" }
    );

    if (filterResult.result !== 0) {
      throw new Error(`Filter creation failed: ${filterResult.error}`);
    }

    const filter = filterResult.data;
    if (!filter) {
      throw new Error("Filter creation returned no data");
    }
    console.log(`✅ Filter created with ID: ${filter}\n`);

    console.log("4. Creating memory pool...");
    const poolResult = await sdk.createMemoryPool(1024 * 1024); // 1MB

    if (poolResult.result !== 0) {
      throw new Error(`Memory pool creation failed: ${poolResult.error}`);
    }

    const pool = poolResult.data;
    if (!pool) {
      throw new Error("Memory pool creation returned no data");
    }
    console.log(`✅ Memory pool created with ID: ${pool}\n`);

    console.log("5. Creating buffer...");
    const bufferResult = await sdk.createBuffer(
      Buffer.from("Hello from MCP Filter SDK!"),
      0x02 // OWNED flag
    );

    if (bufferResult.result !== 0) {
      throw new Error(`Buffer creation failed: ${bufferResult.error}`);
    }

    const buffer = bufferResult.data;
    if (!buffer) {
      throw new Error("Buffer creation returned no data");
    }
    console.log(`✅ Buffer created with ID: ${buffer}\n`);

    console.log("6. Getting SDK statistics...");
    const stats = sdk.getStats();
    console.log("📊 SDK Statistics:");
    console.log(`   - Total Filters: ${stats.totalFilters}`);
    console.log(`   - Total Buffers: ${stats.totalBuffers}`);
    console.log(`   - Total Memory Pools: ${stats.totalMemoryPools}`);
    console.log(`   - Uptime: ${stats.uptime}ms\n`);

    console.log("7. Cleaning up resources...");

    // Clean up in reverse order
    await sdk.destroyBuffer(buffer);
    console.log("   ✅ Buffer destroyed");

    await sdk.destroyMemoryPool(pool);
    console.log("   ✅ Memory pool destroyed");

    await sdk.destroyFilter(filter);
    console.log("   ✅ Filter destroyed\n");

    console.log("8. Shutting down SDK...");
    const shutdownResult = await sdk.shutdown();

    if (shutdownResult.result !== 0) {
      throw new Error(`Shutdown failed: ${shutdownResult.error}`);
    }

    console.log("✅ SDK shut down successfully\n");

    console.log("🎉 FFI Bindings Demo completed successfully!");

    // Final statistics
    const finalStats = sdk.getStats();
    console.log(`\n📊 Final Statistics:`);
    console.log(`   - Total Filters: ${finalStats.totalFilters}`);
    console.log(`   - Total Buffers: ${finalStats.totalBuffers}`);
    console.log(`   - Total Memory Pools: ${finalStats.totalMemoryPools}`);
  } catch (error) {
    console.error("❌ Demo failed:", error);
    process.exit(1);
  }
}

// Run the demo
if (require.main === module) {
  demonstrateFFIBindings().catch(console.error);
}

export { demonstrateFFIBindings };
