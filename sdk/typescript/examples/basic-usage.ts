/**
 * @file basic-usage.ts
 * @brief Basic usage example for MCP Filter SDK
 *
 * This example demonstrates how to use the core filter infrastructure:
 * - Creating filters
 * - Building filter chains
 * - Managing buffers
 * - Using the existing C++ RAII system
 */

import {
  BufferOwnership,
  BuiltinFilterType,
  FilterPosition,
  addFilterToChain,
  buildFilterChain,
  createBufferFromString,
  // Filter Buffer
  createBufferOwned,
  // Filter API
  createBuiltinFilter,
  createFilterChainBuilder,
  createParallelChain,
  // Filter Chain
  createSimpleChain,
  destroyFilterChainBuilder,
  readStringFromBuffer,
} from "../src";

/**
 * Example: Create a simple HTTP processing pipeline
 */
async function createHttpPipeline() {
  console.log("🔧 Creating HTTP processing pipeline...");

  try {
    // Create filters for different stages
    const authFilter = createBuiltinFilter(0, BuiltinFilterType.AUTHENTICATION, {});
    const rateLimitFilter = createBuiltinFilter(0, BuiltinFilterType.RATE_LIMIT, {});
    const accessLogFilter = createBuiltinFilter(0, BuiltinFilterType.ACCESS_LOG, {});

    console.log(
      `✅ Created filters: auth=${authFilter}, rateLimit=${rateLimitFilter}, accessLog=${accessLogFilter}`
    );

    // Create a simple sequential chain
    const chain = createSimpleChain(
      0,
      [authFilter, rateLimitFilter, accessLogFilter],
      "http-pipeline"
    );
    console.log(`✅ Created filter chain: ${chain}`);

    return { authFilter, rateLimitFilter, accessLogFilter, chain };
  } catch (error) {
    console.error("❌ Failed to create HTTP pipeline:", error);
    throw error;
  }
}

/**
 * Example: Create a parallel processing pipeline
 */
async function createParallelPipeline() {
  console.log("🔧 Creating parallel processing pipeline...");

  try {
    // Create multiple filters for parallel processing
    const filters = [
      createBuiltinFilter(0, BuiltinFilterType.METRICS, {}),
      createBuiltinFilter(0, BuiltinFilterType.TRACING, {}),
      createBuiltinFilter(0, BuiltinFilterType.ACCESS_LOG, {}),
    ];

    console.log(`✅ Created ${filters.length} filters for parallel processing`);

    // Create parallel chain with max 2 concurrent filters
    const chain = createParallelChain(0, filters, 2, "parallel-pipeline");
    console.log(`✅ Created parallel filter chain: ${chain}`);

    return { filters, chain };
  } catch (error) {
    console.error("❌ Failed to create parallel pipeline:", error);
    throw error;
  }
}

/**
 * Example: Buffer management with zero-copy operations
 */
async function demonstrateBufferOperations() {
  console.log("🔧 Demonstrating buffer operations...");

  try {
    // Create a buffer from string data
    const buffer = createBufferFromString("Hello, MCP Filter SDK!", BufferOwnership.SHARED);
    console.log(`✅ Created buffer: ${buffer}`);

    // Read the string back from the buffer
    const content = readStringFromBuffer(buffer);
    console.log(`📖 Buffer content: "${content}"`);

    // Create a buffer with specific capacity
    const largeBuffer = createBufferOwned(1024, BufferOwnership.EXCLUSIVE);
    console.log(`✅ Created large buffer: ${largeBuffer}`);

    return { buffer, largeBuffer, content };
  } catch (error) {
    console.error("❌ Failed to demonstrate buffer operations:", error);
    throw error;
  }
}

/**
 * Example: Advanced chain composition
 */
async function demonstrateAdvancedChains() {
  console.log("🔧 Demonstrating advanced chain composition...");

  try {
    // Create different types of filters
    const tcpFilter = createBuiltinFilter(0, BuiltinFilterType.TCP_PROXY, {});
    const tlsFilter = createBuiltinFilter(0, BuiltinFilterType.TLS_TERMINATION, {});
    const httpFilter = createBuiltinFilter(0, BuiltinFilterType.HTTP_CODEC, {});

    console.log(
      `✅ Created protocol filters: tcp=${tcpFilter}, tls=${tlsFilter}, http=${httpFilter}`
    );

    // Create a chain builder manually for more control
    const builder = createFilterChainBuilder(0);
    console.log(`✅ Created chain builder: ${builder}`);

    // Add filters in specific order
    addFilterToChain(builder, tcpFilter, FilterPosition.FIRST);
    addFilterToChain(builder, tlsFilter, FilterPosition.AFTER, tcpFilter);
    addFilterToChain(builder, httpFilter, FilterPosition.LAST);

    // Build the chain
    const chain = buildFilterChain(builder);
    console.log(`✅ Built custom filter chain: ${chain}`);

    // Clean up builder
    destroyFilterChainBuilder(builder);
    console.log(`🧹 Cleaned up chain builder`);

    return { tcpFilter, tlsFilter, httpFilter, chain };
  } catch (error) {
    console.error("❌ Failed to demonstrate advanced chains:", error);
    throw error;
  }
}

/**
 * Main example function
 */
async function main() {
  console.log("🚀 MCP Filter SDK - Basic Usage Example\n");

  try {
    // Demonstrate different pipeline types
    const httpPipeline = await createHttpPipeline();
    console.log("\n---\n");

    const parallelPipeline = await createParallelPipeline();
    console.log("\n---\n");

    const bufferOps = await demonstrateBufferOperations();
    console.log("\n---\n");

    const advancedChains = await demonstrateAdvancedChains();
    console.log("\n---\n");

    console.log("🎉 All examples completed successfully!");
    console.log("\n📊 Summary:");
    console.log(`- HTTP Pipeline: ${httpPipeline.chain} filters`);
    console.log(`- Parallel Pipeline: ${parallelPipeline.filters.length} filters`);
    console.log(`- Buffer Operations: ${bufferOps.content}`);
    console.log(`- Advanced Chains: ${advancedChains.chain} filters`);
  } catch (error) {
    console.error("💥 Example failed:", error);
    process.exit(1);
  }
}

// Run the example if this file is executed directly
if (require.main === module) {
  main().catch(console.error);
}

export {
  createHttpPipeline,
  createParallelPipeline,
  demonstrateAdvancedChains,
  demonstrateBufferOperations,
};
