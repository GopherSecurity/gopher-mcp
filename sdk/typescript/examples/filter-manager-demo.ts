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

  // Demo 4: Error-resilient configuration
  const resilientManager = new FilterManager({
    auth: {
      method: "jwt",
      secret: "resilient-secret",
    },
    rateLimit: {
      requestsPerMinute: 200,
      burstSize: 20,
    },
    logging: true,
    metrics: true,
    errorHandling: {
      stopOnError: false, // Continue processing even if one filter fails
      fallbackBehavior: "passthrough", // Return original message on error
    },
  });

  console.log("✅ Created 4 different FilterManager configurations");

  return {
    securityManager,
    performanceManager,
    minimalManager,
    resilientManager,
  };
}

/**
 * Demo: Error handling scenarios
 */
async function errorHandlingDemo() {
  console.log("\n🔧 Error Handling Demo");

  // Test invalid configuration
  try {
    new FilterManager({
      rateLimit: {
        requestsPerMinute: -10, // Invalid: negative value
      },
    });
  } catch (error) {
    console.log(
      "✅ Configuration validation caught invalid rate limit:",
      (error as Error).message
    );
  }

  // Test invalid message
  const resilientManager = new FilterManager({
    errorHandling: {
      fallbackBehavior: "default", // Return error response
    },
  });

  try {
    const invalidMessage = {
      jsonrpc: "1.0", // Invalid version
      method: "test",
    } as any;

    await resilientManager.process(invalidMessage);
  } catch (error) {
    console.log(
      "✅ Message validation caught invalid JSON-RPC version:",
      (error as Error).message
    );
  }

  console.log("✅ Error handling demos completed");
}

/**
 * Demo: Request-Response processing
 */
async function requestResponseDemo() {
  console.log("\n🔧 Request-Response Processing Demo");

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

  // Create a sample request
  const request: JSONRPCMessage = {
    jsonrpc: "2.0",
    id: "1",
    method: "filesystem/read",
    params: {
      path: "/tmp/test.txt",
    },
  };

  // Create a sample response
  const response: JSONRPCMessage = {
    jsonrpc: "2.0",
    id: "1",
    result: {
      content: "Hello, World!",
      size: 13,
    },
  };

  console.log("📥 Original request:", JSON.stringify(request, null, 2));
  console.log("📤 Original response:", JSON.stringify(response, null, 2));

  try {
    // Process request and response separately
    const processedRequest = await filterManager.process(request);
    const processedResponse = await filterManager.processResponse(response);

    console.log(
      "✅ Processed request:",
      JSON.stringify(processedRequest, null, 2)
    );
    console.log(
      "✅ Processed response:",
      JSON.stringify(processedResponse, null, 2)
    );

    // Process both together
    const { processedRequest: req, processedResponse: res } =
      await filterManager.processRequestResponse(request, response);

    console.log("✅ Combined processing completed");
    console.log("📥 Final request:", JSON.stringify(req, null, 2));
    console.log("📤 Final response:", JSON.stringify(res, null, 2));
  } catch (error) {
    console.error("❌ Request-Response processing failed:", error);
    throw error;
  }

  console.log("✅ Request-Response demos completed");
}

/**
 * Main demo function
 */
async function main() {
  console.log("🚀 FilterManager Demo\n");

  try {
    await basicFilterManagerDemo();
    await configurationDemo();
    await errorHandlingDemo();
    await requestResponseDemo();

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

export {
  basicFilterManagerDemo,
  configurationDemo,
  errorHandlingDemo,
  requestResponseDemo,
};
