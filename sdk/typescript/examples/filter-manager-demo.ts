/**
 * @file filter-manager-demo.ts
 * @brief Demo of FilterManager processing JSONRPCMessage
 */

import { FilterManager, JSONRPCMessage } from "../src";

/**
 * Demo: Basic FilterManager usage
 */
async function basicFilterManagerDemo() {
  console.log("🔧 Basic FilterManager Demo");

  // Create FilterManager with basic configuration
  const filterManager = new FilterManager({
    auth: {
      method: "jwt",
      secret: "demo-secret",
    },
    rateLimit: {
      requestsPerMinute: 100,
      burstSize: 10,
    },
    logging: true,
    metrics: true,
  });

  // Create a sample JSON-RPC message
  const jsonrpcMessage: JSONRPCMessage = {
    jsonrpc: "2.0",
    id: "1",
    method: "filesystem/read",
    params: {
      path: "/tmp/test.txt",
    },
  };

  console.log("📥 Input message:", JSON.stringify(jsonrpcMessage, null, 2));

  try {
    // Process the message through filters
    const processedMessage = await filterManager.process(jsonrpcMessage);

    console.log(
      "📤 Processed message:",
      JSON.stringify(processedMessage, null, 2)
    );
    console.log("✅ FilterManager processing completed successfully!");

    return processedMessage;
  } catch (error) {
    console.error("❌ FilterManager processing failed:", error);
    throw error;
  }
}

/**
 * Demo: FilterManager with different configurations
 */
async function configurationDemo() {
  console.log("\n🔧 Configuration Demo");

  // Demo 1: Security-focused configuration
  const securityManager = new FilterManager({
    auth: {
      method: "api-key",
      key: "secure-api-key",
    },
    rateLimit: {
      requestsPerMinute: 50,
      burstSize: 5,
    },
    logging: true,
    metrics: true,
  });

  // Demo 2: Performance-focused configuration
  const performanceManager = new FilterManager({
    rateLimit: {
      requestsPerMinute: 1000,
      burstSize: 100,
    },
    metrics: true,
  });

  // Demo 3: Minimal configuration
  const minimalManager = new FilterManager({
    logging: true,
  });

  console.log("✅ Created 3 different FilterManager configurations");

  return { securityManager, performanceManager, minimalManager };
}

/**
 * Main demo function
 */
async function main() {
  console.log("🚀 FilterManager Demo\n");

  try {
    await basicFilterManagerDemo();
    await configurationDemo();

    console.log("\n🎉 All demos completed successfully!");
  } catch (error) {
    console.error("💥 Demo failed:", error);
    process.exit(1);
  }
}

// Run the demo if this file is executed directly
if (require.main === module) {
  main().catch(console.error);
}

export { basicFilterManagerDemo, configurationDemo };
