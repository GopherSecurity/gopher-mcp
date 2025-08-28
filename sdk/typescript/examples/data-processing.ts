/**
 * @file data-processing.ts
 * @brief Example demonstrating enhanced data processing capabilities
 */

import { McpFilterSdk, McpBuiltinFilterType, McpProtocolLayer } from "../src";

async function dataProcessingExample() {
  console.log("=== MCP Filter SDK Data Processing Example ===\n");

  const sdk = new McpFilterSdk({
    enableLogging: true,
    logLevel: "info",
  });

  try {
    // 1. Initialize SDK
    console.log("1. Initializing SDK...");
    const initResult = await sdk.initialize();
    if (initResult.result !== 0) {
      throw new Error(`Failed to initialize SDK: ${initResult.error}`);
    }
    console.log("✓ SDK initialized successfully\n");

    // 2. Create a custom filter
    console.log("2. Creating custom filter...");
    const filterResult = await sdk.createFilter({
      name: "data_processor",
      type: 1,
      settings: 0,
      layer: McpProtocolLayer.LAYER_7_APPLICATION,
      memoryPool: 0,
    });
    if (filterResult.result !== 0) {
      throw new Error(`Failed to create filter: ${filterResult.error}`);
    }
    const filter = filterResult.data!;
    console.log(`✓ Custom filter created: ${filter}\n`);

    // 3. Test data processing with valid data
    console.log("3. Testing data processing with valid data...");
    const validData = Buffer.from("Hello, World! This is valid data.");
    const processResult = await sdk.processFilterData(filter, validData);
    if (processResult.result !== 0) {
      throw new Error(`Failed to process valid data: ${processResult.error}`);
    }
    console.log(`✓ Valid data processed successfully (${validData.length} bytes)\n`);

    // 4. Test data processing with invalid data (consecutive 0xFF bytes)
    console.log("4. Testing data processing with invalid data...");
    const invalidData = Buffer.from([0x01, 0x02, 0xFF, 0xFF, 0x03, 0x04]);
    const invalidProcessResult = await sdk.processFilterData(filter, invalidData);
    if (invalidProcessResult.result === 0) {
      console.log("⚠️  Invalid data was processed (expected to fail)");
    } else {
      console.log("✓ Invalid data correctly rejected");
    }
    console.log("");

    // 5. Test data processing with edge cases
    console.log("5. Testing edge cases...");
    
    // Empty buffer
    const emptyData = Buffer.alloc(0);
    const emptyResult = await sdk.processFilterData(filter, emptyData);
    if (emptyResult.result !== 0) {
      console.log("✓ Empty buffer correctly rejected");
    } else {
      console.log("⚠️  Empty buffer was processed (may be valid)");
    }

    // Large buffer
    const largeData = Buffer.alloc(1024 * 1024); // 1MB
    largeData.fill(0x42); // Fill with valid bytes
    const largeResult = await sdk.processFilterData(filter, largeData);
    if (largeResult.result !== 0) {
      throw new Error(`Failed to process large data: ${largeResult.error}`);
    }
    console.log(`✓ Large data processed successfully (${largeData.length} bytes)\n`);

    // 6. Create and test a built-in filter
    console.log("6. Testing built-in filter data processing...");
    const builtinFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.HTTP_CODEC
    );
    if (builtinFilterResult.result !== 0) {
      throw new Error(`Failed to create built-in filter: ${builtinFilterResult.error}`);
    }
    const builtinFilter = builtinFilterResult.data!;
    console.log(`✓ Built-in filter created: ${builtinFilter}`);

    // Test HTTP-like data
    const httpData = Buffer.from("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    const httpResult = await sdk.processFilterData(builtinFilter, httpData);
    if (httpResult.result !== 0) {
      throw new Error(`Failed to process HTTP data: ${httpResult.error}`);
    }
    console.log(`✓ HTTP data processed successfully (${httpData.length} bytes)\n`);

    // 7. Cleanup
    console.log("7. Cleaning up resources...");
    await sdk.destroyFilter(filter);
    await sdk.destroyFilter(builtinFilter);
    console.log("✓ Resources cleaned up\n");

    // 8. Shutdown
    console.log("8. Shutting down SDK...");
    await sdk.shutdown();
    console.log("✓ SDK shut down successfully");

    console.log("\n🎉 Data processing example completed successfully!");
  } catch (error) {
    console.error("\n💥 Example failed:", error);
    process.exit(1);
  }
}

// Run the example
if (require.main === module) {
  dataProcessingExample();
}
