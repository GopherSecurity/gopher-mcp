/**
 * @file mcp-calculator-client.ts
 * @brief Real working MCP Calculator Client with GopherTransport
 *
 * This is a complete working MCP client that connects to the calculator server
 * using GopherTransport with real C++ filter infrastructure.
 *
 */

import { ChainExecutionMode, RoutingStrategy } from "@mcp/filter-sdk";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { CallToolResultSchema, ListToolsResultSchema } from "@modelcontextprotocol/sdk/types.js";
import { GopherTransport, GopherTransportConfig } from "./gopher-transport";

async function main() {
  let transport: GopherTransport | null = null;

  try {
    console.log("🚀 Starting Calculator MCP Client with GopherTransport");

    // Create comprehensive GopherTransport configuration for client
    const transportConfig: GopherTransportConfig = {
      name: "calculator-client-transport",
      version: "1.0.0",
      protocol: "tcp", // Use TCP for real network communication
      host: "localhost", // Connect to localhost
      port: 8080, // Connect to server port 8080
      connectTimeout: 10000, // 10 second connection timeout
      sendTimeout: 5000, // 5 second send timeout
      receiveTimeout: 10000, // 10 second receive timeout

      // Client-specific filter configuration
      filters: {
        // Security filters for client
        security: {
          authentication: {
            method: "jwt",
            secret: "calculator-server-secret-key-2024", // Must match server secret
            issuer: "calculator-client",
            audience: "calculator-server",
          },
          authorization: {
            enabled: true,
            policy: "allow",
            rules: [
              {
                resource: "tools/*",
                action: "call",
                conditions: { authenticated: true },
              },
            ],
          },
        },

        // Observability for client
        observability: {
          accessLog: {
            enabled: true,
            format: "json",
            fields: ["timestamp", "method", "sessionId", "duration", "clientId", "operation"],
            output: "console",
          },
          metrics: {
            enabled: true,
            labels: {
              component: "mcp-client",
              transport: "gopher",
              service: "calculator",
              environment: "production",
            },
          },
          tracing: {
            enabled: true,
            serviceName: "calculator-client",
            samplingRate: 0.3, // 30% sampling for client
          },
        },

        // Traffic management for client
        trafficManagement: {
          rateLimit: {
            enabled: true,
            requestsPerMinute: 1000, // Lower rate limit for client
            burstSize: 50,
            keyExtractor: "custom",
          },
          circuitBreaker: {
            enabled: true,
            failureThreshold: 5, // Lower threshold for client
            timeout: 30000,
            resetTimeout: 60000,
          },
          retry: {
            enabled: true,
            maxAttempts: 3,
            backoffStrategy: "exponential",
            baseDelay: 1000,
            maxDelay: 5000,
          },
        },

        // Error handling
        errorHandling: {
          stopOnError: false,
          retryAttempts: 2,
          fallbackBehavior: "passthrough",
        },

        // Chain configuration for optimal performance
        chain: {
          executionMode: ChainExecutionMode.SEQUENTIAL, // Process filters in order
          routingStrategy: RoutingStrategy.ROUND_ROBIN,
          maxParallel: 1,
          bufferSize: 8192, // 8KB buffer
          timeoutMs: 15000, // 15 second timeout
        },

        // Buffer pool configuration (disabled for now)
        // bufferPool: {
        //   bufferSize: 4096,
        //   maxBuffers: 50,
        //   preallocCount: 5,
        //   useThreadLocal: true,
        //   zeroOnAlloc: false,
        // },

        // NEW: CApiFilter integration for real-time message processing
        customCallbacks: {
          onMessageReceived: message => {
            console.log(`🔍 [CApiFilter DEBUG] Client onMessageReceived called!`);
            console.log(
              `🔍 [CApiFilter DEBUG] Original message:`,
              JSON.stringify(message, null, 2)
            );
            console.log(`🔍 [CApiFilter DEBUG] Message type: ${message.method || "notification"}`);
            console.log(`🔍 [CApiFilter DEBUG] Message ID: ${message.id || "N/A"}`);
            console.log(
              `🔍 [CApiFilter DEBUG] This callback is executing in C++ filter chain context!`
            );

            // Add client-specific metadata
            if ("id" in message) {
              const processedMessage = {
                ...message,
                clientMetadata: {
                  receivedAt: Date.now(),
                  clientId: "calculator-client-001",
                  sessionId: "session-" + Math.random().toString(36).substr(2, 9),
                  processedBy: "CApiFilter-onMessageReceived",
                },
              };
              console.log(
                `🔍 [CApiFilter DEBUG] Processed message:`,
                JSON.stringify(processedMessage, null, 2)
              );
              console.log(`🔍 [CApiFilter DEBUG] Returning processed message to C++ filter chain`);
              return processedMessage;
            }
            return null; // No modification
          },
          onMessageSent: message => {
            console.log(`🔍 [CApiFilter DEBUG] Client onMessageSent called!`);
            console.log(
              `🔍 [CApiFilter DEBUG] Original message:`,
              JSON.stringify(message, null, 2)
            );
            console.log(`🔍 [CApiFilter DEBUG] Message type: ${message.method || "notification"}`);
            console.log(`🔍 [CApiFilter DEBUG] Message ID: ${message.id || "N/A"}`);
            console.log(
              `🔍 [CApiFilter DEBUG] This callback is executing in C++ filter chain context!`
            );

            // Add client-specific metadata
            if ("id" in message) {
              const processedMessage = {
                ...message,
                clientMetadata: {
                  sentAt: Date.now(),
                  clientId: "calculator-client-001",
                  sessionId: "session-" + Math.random().toString(36).substr(2, 9),
                  processedBy: "CApiFilter-onMessageSent",
                },
              };
              console.log(
                `🔍 [CApiFilter DEBUG] Processed message:`,
                JSON.stringify(processedMessage, null, 2)
              );
              console.log(`🔍 [CApiFilter DEBUG] Returning processed message to C++ filter chain`);
              return processedMessage;
            }
            return null; // No modification
          },
          onConnectionEstablished: connectionId => {
            console.log(`🔍 [CApiFilter DEBUG] Client onConnectionEstablished called!`);
            console.log(`🔍 [CApiFilter DEBUG] Connection ID: ${connectionId}`);
            console.log(
              `🔍 [CApiFilter DEBUG] This callback is executing in C++ filter chain context!`
            );
          },
          onConnectionClosed: connectionId => {
            console.log(`🔍 [CApiFilter DEBUG] Client onConnectionClosed called!`);
            console.log(`🔍 [CApiFilter DEBUG] Connection ID: ${connectionId}`);
            console.log(
              `🔍 [CApiFilter DEBUG] This callback is executing in C++ filter chain context!`
            );
          },
          onError: (error, context) => {
            console.log(`🔍 [CApiFilter DEBUG] Client onError called!`);
            console.log(`🔍 [CApiFilter DEBUG] Error:`, error.message);
            console.log(`🔍 [CApiFilter DEBUG] Context: ${context}`);
            console.log(
              `🔍 [CApiFilter DEBUG] This callback is executing in C++ filter chain context!`
            );
          },
        },
      },
    };

    transport = new GopherTransport(transportConfig);

    // Create a client
    const client = new Client({
      name: "calculator-client",
      version: "1.0.0",
    });

    // Start the GopherTransport (will connect to server)
    await transport.start();

    // Connect the client to the transport
    await client.connect(transport);

    console.log("✅ Connected to Calculator MCP Server via GopherTransport");
    console.log("🔐 Security: JWT authentication enabled");
    console.log("📊 Observability: Logging, metrics, and tracing enabled");
    console.log("⚡ Traffic Management: Rate limiting, circuit breaker, retry enabled");
    console.log(`📊 Transport stats:`, transport.getStats());

    // List available tools
    console.log("\n📋 Listing available tools...");
    try {
      const toolsResult = await client.request(
        { method: "tools/list", params: {} },
        ListToolsResultSchema
      );

      console.log("Available tools:");
      if (toolsResult.tools.length === 0) {
        console.log("  No tools available");
      } else {
        for (const tool of toolsResult.tools) {
          console.log(`  - ${tool.name}: ${tool.description}`);
        }
      }
    } catch (error) {
      console.log(`❌ Failed to list tools: ${error}`);
    }

    // Perform calculator operations
    console.log("\n🧮 Performing calculator operations...");

    const operations = [
      { operation: "add", a: 10, b: 5, description: "10 + 5" },
      { operation: "subtract", a: 20, b: 8, description: "20 - 8" },
      { operation: "multiply", a: 6, b: 7, description: "6 × 7" },
      { operation: "divide", a: 100, b: 4, description: "100 ÷ 4" },
      { operation: "power", a: 2, b: 8, description: "2^8" },
      { operation: "sqrt", a: 144, description: "√144" },
      { operation: "factorial", a: 5, description: "5!" },
    ];

    for (const op of operations) {
      try {
        console.log(`\n🔢 ${op.description}:`);

        const params: any = { operation: op.operation, a: op.a };
        if (op.b !== undefined) {
          params.b = op.b;
        }

        const result = await client.request(
          {
            method: "tools/call",
            params: {
              name: "calculator",
              arguments: params,
            },
          },
          CallToolResultSchema
        );

        if (result.content && result.content.length > 0) {
          console.log(`  Result: ${result.content[0].text}`);
        } else {
          console.log("  No result content");
        }
      } catch (error) {
        console.error(`  ❌ Error: ${error}`);
      }
    }

    // Test server statistics
    console.log("\n📊 Getting server statistics...");
    try {
      const statsResult = await client.request(
        {
          method: "tools/call",
          params: {
            name: "server_stats",
            arguments: { include_details: true },
          },
        },
        CallToolResultSchema
      );

      if (statsResult.content && statsResult.content.length > 0) {
        console.log("Server Statistics:");
        console.log(statsResult.content[0].text);
      }
    } catch (error) {
      console.error(`❌ Failed to get server stats: ${error}`);
    }

    // Test error handling
    console.log("\n⚠️  Testing error handling...");
    try {
      const errorResult = await client.request(
        {
          method: "tools/call",
          params: {
            name: "calculator",
            arguments: { operation: "divide", a: 10, b: 0 },
          },
        },
        CallToolResultSchema
      );

      if (errorResult.content && errorResult.content.length > 0) {
        console.log(`Error handling test: ${errorResult.content[0].text}`);
      }
    } catch (error) {
      console.error(`❌ Error handling test failed: ${error}`);
    }

    // Test rate limiting (send multiple requests quickly)
    console.log("\n⚡ Testing rate limiting...");
    const rateLimitPromises = [];
    for (let i = 0; i < 5; i++) {
      rateLimitPromises.push(
        client
          .request(
            {
              method: "tools/call",
              params: {
                name: "calculator",
                arguments: { operation: "add", a: i, b: 1 },
              },
            },
            CallToolResultSchema
          )
          .catch(error => ({ error: error.message }))
      );
    }

    const rateLimitResults = await Promise.all(rateLimitPromises);
    console.log(`Rate limit test: ${rateLimitResults.length} requests sent`);
    const successCount = rateLimitResults.filter(r => !r.error).length;
    const errorCount = rateLimitResults.filter(r => r.error).length;
    console.log(`  ✅ Successful: ${successCount}`);
    console.log(`  ❌ Failed: ${errorCount}`);

    console.log("\n✅ Calculator client operations completed successfully!");
    console.log(`📊 Final transport stats:`, transport.getStats());
  } catch (error) {
    console.error("❌ Client error:", error);
    process.exit(1);
  } finally {
    // Clean up
    try {
      if (transport) {
        await transport.close();
        console.log("🔌 Transport connection closed");
      }
    } catch (error) {
      console.error("❌ Error closing transport:", error);
    }
  }
}

// Start the client
main().catch(error => {
  console.error("❌ Failed to start client:", error);
  process.exit(1);
});
