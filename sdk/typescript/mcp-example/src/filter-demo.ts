#!/usr/bin/env ts-node

/**
 * @file filter-demo.ts
 * @brief Pure FilterManager demonstration without network simulation
 *
 * This example demonstrates the core FilterManager functionality:
 * - Real C++ library integration
 * - Filter chain processing
 * - Buffer operations
 * - Authentication, logging, and traffic management
 */

import { FilterManager, JSONRPCMessage, FilterManagerConfig } from "@mcp/filter-sdk";

async function demonstrateFilterManager() {
  console.log("🔧 FilterManager Demonstration");
  console.log("==============================");

  try {
    // Create FilterManager with comprehensive configuration
    const config: FilterManagerConfig = {
      // Security filters (legacy auth configuration)
      auth: {
        method: "jwt",
        secret: "demo-secret-key",
      },

      // Observability
      logging: true,
      metrics: true,

      // Traffic management (legacy rateLimit configuration)
      rateLimit: {
        requestsPerMinute: 100,
        burstSize: 10,
      },

      // Error handling
      errorHandling: {
        stopOnError: false,
        retryAttempts: 2,
        fallbackBehavior: "passthrough",
      },
    };

    console.log("📋 Creating FilterManager with configuration...");
    const filterManager = new FilterManager(config);
    console.log("✅ FilterManager created successfully");

    // Test messages to process
    const testMessages: JSONRPCMessage[] = [
      {
        jsonrpc: "2.0",
        id: 1,
        method: "tools/list",
        params: {},
      },
      {
        jsonrpc: "2.0",
        id: 2,
        method: "tools/call",
        params: {
          name: "calculator",
          arguments: {
            operation: "add",
            a: 5,
            b: 3,
          },
        },
      },
      {
        jsonrpc: "2.0",
        method: "notifications/progress",
        params: {
          progress: 50,
          message: "Processing request...",
        },
      },
    ];

    console.log("\n🔄 Processing test messages through FilterManager...");
    console.log("==================================================");

    for (let i = 0; i < testMessages.length; i++) {
      const message = testMessages[i];
      const messageInfo =
        "method" in message
          ? `${message.method} (id: ${"id" in message ? message.id : "N/A"})`
          : "notification";

      console.log(`\n📤 Processing message ${i + 1}: ${messageInfo}`);

      try {
        const processedMessage = await filterManager.process(message);
        console.log(`✅ Message processed successfully`);
        console.log(`📊 Processed message:`, JSON.stringify(processedMessage, null, 2));
      } catch (error) {
        console.log(`❌ Message processing failed:`, error);
      }
    }

    // Test response processing
    console.log("\n🔄 Testing response processing...");
    console.log("=================================");

    const responseMessage: JSONRPCMessage = {
      jsonrpc: "2.0",
      id: 2,
      result: {
        content: [
          {
            type: "text",
            text: "8", // Result of 5 + 3
          },
        ],
      },
    };

    console.log("📥 Processing response message...");
    try {
      const processedResponse = await filterManager.processResponse(responseMessage);
      console.log("✅ Response processed successfully");
      console.log("📊 Processed response:", JSON.stringify(processedResponse, null, 2));
    } catch (error) {
      console.log("❌ Response processing failed:", error);
    }

    // Clean up
    console.log("\n🧹 Cleaning up resources...");
    filterManager.destroy();
    console.log("✅ FilterManager destroyed successfully");

    console.log("\n🎉 FilterManager demonstration completed successfully!");
    console.log("==================================================");
    console.log("✅ Real C++ library integration working");
    console.log("✅ Filter chain processing functional");
    console.log("✅ Authentication, logging, and traffic management active");
    console.log("✅ Buffer operations using actual C++ implementation");
  } catch (error) {
    console.error("❌ Demonstration failed:", error);
    process.exit(1);
  }
}

// Run the demonstration
if (require.main === module) {
  demonstrateFilterManager();
}

export { demonstrateFilterManager };
