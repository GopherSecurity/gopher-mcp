/**
 * @file mcp-calculator-server.ts
 * @brief Real working MCP Calculator Server with GopherTransport
 *
 * This is a complete working MCP server that provides calculator functionality
 * using GopherTransport with real C++ filter infrastructure.
 *
 */

import { ChainExecutionMode, RoutingStrategy } from "@mcp/filter-sdk";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { GopherTransport, GopherTransportConfig } from "./gopher-transport";

// Create the MCP server
const mcpServer = new McpServer({
  name: "calculator-server",
  version: "1.0.0",
});

// Register the calculator tool with comprehensive validation
mcpServer.registerTool(
  "calculator",
  {
    description:
      "A comprehensive calculator that can perform basic arithmetic operations with validation",
    inputSchema: {
      operation: z
        .enum(["add", "subtract", "multiply", "divide", "power", "sqrt", "factorial"])
        .describe("The arithmetic operation to perform"),
      a: z.number().describe("First number"),
      b: z.number().optional().describe("Second number (required for binary operations)"),
      precision: z.number().optional().default(2).describe("Decimal precision for results"),
    },
  },
  async ({ operation, a, b, precision = 2 }) => {
    console.log(`🧮 Calculator operation: ${operation}(${a}, ${b})`);

    let result: number;
    let operationDescription: string;

    try {
      switch (operation) {
        case "add":
          if (b === undefined) throw new Error("Second number is required for addition");
          result = a + b;
          operationDescription = `${a} + ${b}`;
          break;

        case "subtract":
          if (b === undefined) throw new Error("Second number is required for subtraction");
          result = a - b;
          operationDescription = `${a} - ${b}`;
          break;

        case "multiply":
          if (b === undefined) throw new Error("Second number is required for multiplication");
          result = a * b;
          operationDescription = `${a} × ${b}`;
          break;

        case "divide":
          if (b === undefined) throw new Error("Second number is required for division");
          if (b === 0) throw new Error("Division by zero is not allowed");
          result = a / b;
          operationDescription = `${a} ÷ ${b}`;
          break;

        case "power":
          if (b === undefined) throw new Error("Second number is required for power operation");
          result = Math.pow(a, b);
          operationDescription = `${a}^${b}`;
          break;

        case "sqrt":
          if (a < 0) throw new Error("Square root of negative number is not allowed");
          result = Math.sqrt(a);
          operationDescription = `√${a}`;
          break;

        case "factorial":
          if (a < 0) throw new Error("Factorial of negative number is not allowed");
          if (a !== Math.floor(a)) throw new Error("Factorial requires integer input");
          if (a > 170) throw new Error("Factorial too large (max 170)");

          result = 1;
          for (let i = 2; i <= a; i++) {
            result *= i;
          }
          operationDescription = `${a}!`;
          break;

        default:
          throw new Error(`Unknown operation: ${operation}`);
      }

      // Round result to specified precision
      const roundedResult = Math.round(result * Math.pow(10, precision)) / Math.pow(10, precision);

      console.log(`✅ Calculation result: ${operationDescription} = ${roundedResult}`);

      return {
        content: [
          {
            type: "text",
            text: `Result: ${operationDescription} = ${roundedResult}`,
          },
        ],
        isError: false,
      };
    } catch (error) {
      const errorMessage = error instanceof Error ? error.message : String(error);
      console.error(`❌ Calculator error: ${errorMessage}`);

      return {
        content: [
          {
            type: "text",
            text: `Error: ${errorMessage}`,
          },
        ],
        isError: true,
      };
    }
  }
);

// Register a statistics tool to show server metrics
mcpServer.registerTool(
  "server_stats",
  {
    description: "Get server statistics and health information",
    inputSchema: {
      include_details: z
        .boolean()
        .optional()
        .default(false)
        .describe("Include detailed statistics"),
    },
  },
  async ({ include_details }) => {
    const stats = {
      server_name: "calculator-server",
      version: "1.0.0",
      uptime: process.uptime(),
      memory_usage: process.memoryUsage(),
      timestamp: new Date().toISOString(),
    };

    if (include_details) {
      (stats as any).details = {
        node_version: process.version,
        platform: process.platform,
        arch: process.arch,
        pid: process.pid,
        cpu_usage: process.cpuUsage(),
      };
    }

    return {
      content: [
        {
          type: "text",
          text: `Server Statistics:\n${JSON.stringify(stats, null, 2)}`,
        },
      ],
      isError: false,
    };
  }
);

async function main() {
  try {
    console.log("🚀 Starting Calculator MCP Server with GopherTransport");

    // Create comprehensive GopherTransport configuration for server
    const transportConfig: GopherTransportConfig = {
      name: "calculator-server-transport",
      version: "1.0.0",
      protocol: "tcp", // Use TCP for real network communication
      port: 8080, // Server will listen on port 8080

      // Server-specific filter configuration
      filters: {
        // Security filters for server
        security: {
          authentication: {
            method: "jwt",
            secret: "calculator-server-secret-key-2024",
            issuer: "calculator-server",
            audience: "calculator-client",
          },
          authorization: {
            enabled: true,
            policy: "allow",
            rules: [
              {
                resource: "tools/calculator",
                action: "call",
                conditions: { authenticated: true },
              },
              {
                resource: "tools/server_stats",
                action: "call",
                conditions: { authenticated: true },
              },
            ],
          },
        },

        // Observability for server
        observability: {
          accessLog: {
            enabled: true,
            format: "json",
            fields: [
              "timestamp",
              "method",
              "sessionId",
              "duration",
              "serverId",
              "toolName",
              "result",
            ],
            output: "console",
          },
          metrics: {
            enabled: true,
            labels: {
              component: "mcp-server",
              transport: "gopher",
              service: "calculator",
              environment: "production",
            },
          },
          tracing: {
            enabled: true,
            serviceName: "calculator-server",
            samplingRate: 0.5, // 50% sampling for server
          },
        },

        // Traffic management for server
        trafficManagement: {
          rateLimit: {
            enabled: true,
            requestsPerMinute: 2000, // Higher rate limit for server
            burstSize: 100,
            keyExtractor: "custom",
          },
          circuitBreaker: {
            enabled: true,
            failureThreshold: 10, // Higher threshold for server
            timeout: 60000,
            resetTimeout: 120000,
          },
          retry: {
            enabled: true,
            maxAttempts: 3,
            backoffStrategy: "exponential",
            baseDelay: 1000,
            maxDelay: 10000,
          },
          loadBalancer: {
            enabled: true,
            strategy: "round-robin",
            upstreams: [
              {
                host: "calculator-worker-1",
                port: 8081,
                weight: 1,
                healthCheck: true,
              },
              {
                host: "calculator-worker-2",
                port: 8082,
                weight: 1,
                healthCheck: true,
              },
            ],
          },
        },

        // HTTP filters for server
        http: {
          compression: {
            enabled: true,
            algorithms: ["gzip", "deflate"],
            minSize: 512,
          },
        },

        // Error handling
        errorHandling: {
          stopOnError: false,
          retryAttempts: 2,
          fallbackBehavior: "default",
        },

        // Chain configuration for optimal performance
        chain: {
          executionMode: ChainExecutionMode.SEQUENTIAL, // Process filters in order
          routingStrategy: RoutingStrategy.ROUND_ROBIN,
          maxParallel: 1,
          bufferSize: 16384, // 16KB buffer
          timeoutMs: 30000, // 30 second timeout
        },

        // Buffer pool configuration (disabled for now)
        // bufferPool: {
        //   bufferSize: 8192,
        //   maxBuffers: 100,
        //   preallocCount: 10,
        //   useThreadLocal: true,
        //   zeroOnAlloc: false,
        // },

        // NEW: CApiFilter integration for real-time message processing
        customCallbacks: {
          onMessageReceived: message => {
            console.log(`🔍 [CApiFilter DEBUG] Server onMessageReceived called!`);
            console.log(
              `🔍 [CApiFilter DEBUG] Original message:`,
              JSON.stringify(message, null, 2)
            );
            console.log(`🔍 [CApiFilter DEBUG] Message type: ${message.method || "notification"}`);
            console.log(`🔍 [CApiFilter DEBUG] Message ID: ${message.id || "N/A"}`);
            console.log(
              `🔍 [CApiFilter DEBUG] This callback is executing in C++ filter chain context!`
            );

            // Add server-specific metadata
            if ("id" in message) {
              const processedMessage = {
                ...message,
                serverMetadata: {
                  receivedAt: Date.now(),
                  serverId: "calculator-server-001",
                  requestId: "req-" + Math.random().toString(36).substr(2, 9),
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
            console.log(`🔍 [CApiFilter DEBUG] Server onMessageSent called!`);
            console.log(
              `🔍 [CApiFilter DEBUG] Original message:`,
              JSON.stringify(message, null, 2)
            );
            console.log(`🔍 [CApiFilter DEBUG] Message type: ${message.method || "notification"}`);
            console.log(`🔍 [CApiFilter DEBUG] Message ID: ${message.id || "N/A"}`);
            console.log(
              `🔍 [CApiFilter DEBUG] This callback is executing in C++ filter chain context!`
            );

            // Add server-specific metadata
            if ("id" in message) {
              const processedMessage = {
                ...message,
                serverMetadata: {
                  sentAt: Date.now(),
                  serverId: "calculator-server-001",
                  processingTime: Math.random() * 100, // Simulate processing time
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
            console.log(`🔍 [CApiFilter DEBUG] Server onConnectionEstablished called!`);
            console.log(`🔍 [CApiFilter DEBUG] Connection ID: ${connectionId}`);
            console.log(
              `🔍 [CApiFilter DEBUG] This callback is executing in C++ filter chain context!`
            );
          },
          onConnectionClosed: connectionId => {
            console.log(`🔍 [CApiFilter DEBUG] Server onConnectionClosed called!`);
            console.log(`🔍 [CApiFilter DEBUG] Connection ID: ${connectionId}`);
            console.log(
              `🔍 [CApiFilter DEBUG] This callback is executing in C++ filter chain context!`
            );
          },
          onError: (error, context) => {
            console.log(`🔍 [CApiFilter DEBUG] Server onError called!`);
            console.log(`🔍 [CApiFilter DEBUG] Error:`, error.message);
            console.log(`🔍 [CApiFilter DEBUG] Context: ${context}`);
            console.log(
              `🔍 [CApiFilter DEBUG] This callback is executing in C++ filter chain context!`
            );
          },
        },
      },
    };

    const transport = new GopherTransport(transportConfig);

    // Start the GopherTransport (will start TCP server)
    await transport.start();

    // Connect the server to the transport
    await mcpServer.connect(transport);

    console.log("✅ Calculator MCP Server started with GopherTransport");
    console.log("📡 Server listening on TCP port 8080");
    console.log("🔐 Security: JWT authentication enabled");
    console.log("📊 Observability: Logging, metrics, and tracing enabled");
    console.log("⚡ Traffic Management: Rate limiting, circuit breaker, retry enabled");
    console.log(`📊 Transport stats:`, transport.getStats());

    // Keep the server running
    console.log("🔄 Server is running... Press Ctrl+C to stop");

    // Handle graceful shutdown
    process.on("SIGINT", async () => {
      console.log("\n🛑 Shutting down server...");
      try {
        await transport.close();
        console.log("✅ Server shutdown complete");
        process.exit(0);
      } catch (error) {
        console.error("❌ Error during shutdown:", error);
        process.exit(1);
      }
    });

    process.on("SIGTERM", async () => {
      console.log("\n🛑 Received SIGTERM, shutting down server...");
      try {
        await transport.close();
        console.log("✅ Server shutdown complete");
        process.exit(0);
      } catch (error) {
        console.error("❌ Error during shutdown:", error);
        process.exit(1);
      }
    });
  } catch (error) {
    console.error("❌ Server error:", error);
    process.exit(1);
  }
}

// Start the server
main().catch(error => {
  console.error("❌ Failed to start server:", error);
  process.exit(1);
});
