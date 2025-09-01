/**
 * @file filter-manager-demo.ts
 * @brief Demo of FilterManager processing JSONRPCMessage
 */

import { FilterManager, JSONRPCMessage } from "../src";

/**
 * Demo: Comprehensive FilterManager with all filter types
 */
async function comprehensiveFilterManagerDemo() {
  console.log("🔧 Comprehensive FilterManager Demo");

  // Create FilterManager with comprehensive configuration
  const filterManager = new FilterManager({
    // Network filters
    network: {
      tcpProxy: {
        enabled: true,
        upstreamHost: "localhost",
        upstreamPort: 8080,
        bindAddress: "0.0.0.0",
        bindPort: 3000,
      },
    },

    // HTTP filters
    http: {
      codec: {
        enabled: true,
        compressionLevel: 6,
        maxRequestSize: 1024 * 1024, // 1MB
        maxResponseSize: 1024 * 1024, // 1MB
      },
      router: {
        enabled: true,
        routes: [
          {
            path: "/api/v1/*",
            method: "GET",
            target: "http://backend:8080",
            headers: { "X-Forwarded-For": "client" },
          },
        ],
      },
      compression: {
        enabled: true,
        algorithms: ["gzip", "deflate", "brotli"],
        minSize: 1024, // 1KB
      },
    },

    // Security filters
    security: {
      tlsTermination: {
        enabled: true,
        certPath: "/path/to/cert.pem",
        keyPath: "/path/to/key.pem",
        protocols: ["TLSv1.2", "TLSv1.3"],
      },
      authentication: {
        method: "jwt",
        secret: "your-jwt-secret",
        issuer: "your-app",
        audience: "your-users",
      },
      authorization: {
        enabled: true,
        policy: "allow",
        rules: [
          {
            resource: "/api/admin/*",
            action: "read",
            conditions: { role: "admin" },
          },
        ],
      },
    },

    // Observability filters
    observability: {
      accessLog: {
        enabled: true,
        format: "json",
        fields: ["timestamp", "method", "path", "status", "duration"],
        output: "console",
      },
      metrics: {
        enabled: true,
        endpoint: "http://prometheus:9090",
        interval: 60000, // 1 minute
        labels: { service: "mcp-filter-manager" },
      },
      tracing: {
        enabled: true,
        serviceName: "mcp-filter-manager",
        endpoint: "http://jaeger:14268",
        samplingRate: 0.1, // 10% sampling
      },
    },

    // Traffic management filters
    trafficManagement: {
      rateLimit: {
        enabled: true,
        requestsPerMinute: 1000,
        burstSize: 50,
        keyExtractor: "ip",
      },
      circuitBreaker: {
        enabled: true,
        failureThreshold: 5,
        timeout: 30000, // 30 seconds
        resetTimeout: 60000, // 1 minute
      },
      retry: {
        enabled: true,
        maxAttempts: 3,
        backoffStrategy: "exponential",
        baseDelay: 1000, // 1 second
        maxDelay: 10000, // 10 seconds
      },
      loadBalancer: {
        enabled: true,
        strategy: "round-robin",
        upstreams: [
          { host: "backend1", port: 8080, weight: 1, healthCheck: true },
          { host: "backend2", port: 8080, weight: 1, healthCheck: true },
          { host: "backend3", port: 8080, weight: 2, healthCheck: true },
        ],
      },
    },

    // Custom filters
    customFilters: [
      {
        enabled: true,
        name: "customValidation",
        config: {
          validateSchema: true,
          schemaPath: "/schemas/request.json",
        },
        position: "first",
      },
      {
        enabled: true,
        name: "customTransform",
        config: {
          transformResponse: true,
          addHeaders: { "X-Processed-By": "custom-filter" },
        },
        position: "last",
      },
    ],

    // Error handling
    errorHandling: {
      stopOnError: false,
      retryAttempts: 2,
      fallbackBehavior: "passthrough",
    },
  });

  console.log("📊 Configured filters:");
  console.log("  - Network: TCP Proxy");
  console.log("  - HTTP: Codec, Router, Compression");
  console.log("  - Security: TLS Termination, Authentication, Authorization");
  console.log("  - Observability: Access Log, Metrics, Tracing");
  console.log("  - Traffic Management: Rate Limit, Circuit Breaker, Retry, Load Balancer");
  console.log("  - Custom: Validation, Transform");

  // Create a sample JSON-RPC message
  const jsonrpcMessage: JSONRPCMessage = {
    jsonrpc: "2.0",
    id: "comprehensive-test",
    method: "comprehensive/process",
    params: {
      data: "comprehensive test data",
      headers: { "Authorization": "Bearer token123" },
    },
  };

  console.log("📥 Input message:", JSON.stringify(jsonrpcMessage, null, 2));

  try {
    // Process the message through all filters
    const processedMessage = await filterManager.process(jsonrpcMessage);

    console.log(
      "📤 Processed message:",
      JSON.stringify(processedMessage, null, 2)
    );
    console.log("✅ Comprehensive FilterManager processing completed successfully!");

    return processedMessage;
  } catch (error) {
    console.error("❌ Comprehensive FilterManager processing failed:", error);
    throw error;
  } finally {
    // Clean up resources
    filterManager.destroy();
    console.log("🧹 FilterManager resources cleaned up");
  }
}

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
 * Demo: Resource cleanup and lifecycle management
 */
async function resourceCleanupDemo() {
  console.log("\n🔧 Resource Cleanup Demo");

  // Create a FilterManager
  const filterManager = new FilterManager({
    auth: {
      method: "jwt",
      secret: "cleanup-demo-secret",
    },
    rateLimit: {
      requestsPerMinute: 50,
      burstSize: 5,
    },
    logging: true,
    metrics: true,
  });

  console.log("✅ FilterManager created");

  // Check if it's destroyed (should be false)
  console.log("Is destroyed:", filterManager.isDestroyed());

  // Process a message
  const testMessage: JSONRPCMessage = {
    jsonrpc: "2.0",
    id: "cleanup-test",
    method: "test/cleanup",
    params: { test: true },
  };

  try {
    await filterManager.process(testMessage);
    console.log("✅ Message processed successfully");
  } catch (error) {
    console.error("❌ Message processing failed:", error);
  }

  // Destroy the FilterManager
  console.log("🗑️ Destroying FilterManager...");
  filterManager.destroy();

  // Check if it's destroyed (should be true)
  console.log("Is destroyed:", filterManager.isDestroyed());

  // Try to process a message after destruction (should fail)
  try {
    await filterManager.process(testMessage);
    console.log("❌ This should not happen - processing after destruction");
  } catch (error) {
    console.log(
      "✅ Correctly prevented processing after destruction:",
      (error as Error).message
    );
  }

  // Try to destroy again (should warn)
  console.log("🗑️ Attempting to destroy again...");
  filterManager.destroy();

  console.log("✅ Resource cleanup demos completed");
}

/**
 * Main demo function
 */
async function main() {
  console.log("🚀 FilterManager Demo\n");

  try {
    await basicFilterManagerDemo();
    await comprehensiveFilterManagerDemo();
    await configurationDemo();
    await errorHandlingDemo();
    await requestResponseDemo();
    await resourceCleanupDemo();

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
  comprehensiveFilterManagerDemo,
  configurationDemo,
  errorHandlingDemo,
  requestResponseDemo,
  resourceCleanupDemo,
};
