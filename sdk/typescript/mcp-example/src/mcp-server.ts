import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { GopherTransport, GopherTransportConfig } from "./gopher-transport";

// Create the MCP server
const mcpServer = new McpServer({
  name: "calculator-server",
  version: "1.0.0",
});

// Register the calculator tool
mcpServer.registerTool(
  "calculator",
  {
    description:
      "A simple calculator that can perform basic arithmetic operations",
    inputSchema: {
      operation: z
        .enum(["add", "subtract", "multiply", "divide"])
        .describe("The arithmetic operation to perform"),
      a: z.number().describe("First number"),
      b: z.number().describe("Second number"),
    },
  },
  async ({ operation, a, b }) => {
    let result: number;

    switch (operation) {
      case "add":
        result = a + b;
        break;
      case "subtract":
        result = a - b;
        break;
      case "multiply":
        result = a * b;
        break;
      case "divide":
        if (b === 0) {
          throw new Error("Division by zero is not allowed");
        }
        result = a / b;
        break;
      default:
        throw new Error(`Unknown operation: ${operation}`);
    }

    return {
      content: [
        {
          type: "text",
          text: `Result of ${operation}(${a}, ${b}) = ${result}`,
        },
      ],
    };
  }
);

async function main() {
  // Create a GopherTransport with server-specific filter configuration
  const transportConfig: GopherTransportConfig = {
    name: "mcp-server-transport",
    version: "1.0.0",
    protocol: "stdio",
    
    // Server-specific filter configuration
    filters: {
      // Security filters for server
      security: {
        authentication: {
          method: "jwt",
          secret: "server-secret-key",
          issuer: "mcp-server",
          audience: "mcp-client",
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
          ],
        },
      },

      // Observability for server
      observability: {
        accessLog: {
          enabled: true,
          format: "json",
          fields: ["timestamp", "method", "sessionId", "duration", "serverId", "toolName"],
          output: "console",
        },
        metrics: {
          enabled: true,
          labels: { 
            component: "mcp-server",
            transport: "gopher",
            service: "calculator",
          },
        },
        tracing: {
          enabled: true,
          serviceName: "mcp-server",
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
            { host: "calculator-worker-1", port: 8080, weight: 1, healthCheck: true },
            { host: "calculator-worker-2", port: 8080, weight: 1, healthCheck: true },
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
    },
  };

  const transport = new GopherTransport(transportConfig);
  
  // Start the GopherTransport
  await transport.start();
  
  // Connect the server to the transport
  await mcpServer.connect(transport);
  
  console.error("✅ Calculator MCP Server started with GopherTransport");
  console.error(`📊 Transport stats:`, transport.getStats());
}

main().catch((error) => {
  console.error("Server error:", error);
  process.exit(1);
});
