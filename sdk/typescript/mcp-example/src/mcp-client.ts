import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import {
  CallToolResultSchema,
  ListToolsResultSchema,
} from "@modelcontextprotocol/sdk/types.js";
import { GopherTransport, GopherTransportConfig } from "./gopher-transport";

async function main() {
  try {
    // Create a GopherTransport with comprehensive filter configuration
    const transportConfig: GopherTransportConfig = {
      name: "mcp-client-transport",
      version: "1.0.0",
      protocol: "stdio",
      
      // Client-specific filter configuration
      filters: {
        // Security filters for client
        security: {
          authentication: {
            method: "jwt",
            secret: "client-secret-key",
            issuer: "mcp-client",
            audience: "mcp-server",
          },
          authorization: {
            enabled: true,
            policy: "allow",
            rules: [
              {
                resource: "tools/*",
                action: "call",
              },
            ],
          },
        },

        // Observability for client
        observability: {
          accessLog: {
            enabled: true,
            format: "json",
            fields: ["timestamp", "method", "sessionId", "duration", "clientId"],
            output: "console",
          },
          metrics: {
            enabled: true,
            labels: { 
              component: "mcp-client",
              transport: "gopher",
            },
          },
          tracing: {
            enabled: true,
            serviceName: "mcp-client",
            samplingRate: 0.2, // 20% sampling for client
          },
        },

        // Traffic management for client
        trafficManagement: {
          rateLimit: {
            enabled: true,
            requestsPerMinute: 500, // Lower rate limit for client
            burstSize: 25,
            keyExtractor: "custom",
          },
          circuitBreaker: {
            enabled: true,
            failureThreshold: 3, // Lower threshold for client
            timeout: 15000,
            resetTimeout: 30000,
          },
          retry: {
            enabled: true,
            maxAttempts: 2, // Fewer retries for client
            backoffStrategy: "exponential",
            baseDelay: 500,
            maxDelay: 3000,
          },
        },

        // Error handling
        errorHandling: {
          stopOnError: false,
          retryAttempts: 1,
          fallbackBehavior: "passthrough",
        },
      },
    };

    const transport = new GopherTransport(transportConfig);

    // Create a client
    const client = new Client({
      name: "calculator-client",
      version: "1.0.0",
    });

    // Start the GopherTransport
    await transport.start();

    // Connect the client to the transport
    await client.connect(transport);

    console.log("✅ Connected to MCP server via GopherTransport");
    console.log(`📊 Transport stats:`, transport.getStats());

    // List available tools
    const toolsRequest = {
      method: "tools/list",
      params: {},
    };

    try {
      const toolsResult = await client.request(
        toolsRequest,
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
      console.log(`Tools not supported by this server: ${error}`);
    }

    // Call the calculator tool
    const addResult = await client.request(
      {
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
      CallToolResultSchema
    );

    console.log("Add result:", addResult);

    // Test another operation
    const multiplyResult = await client.request(
      {
        method: "tools/call",
        params: {
          name: "calculator",
          arguments: {
            operation: "multiply",
            a: 4,
            b: 7,
          },
        },
      },
      CallToolResultSchema
    );

    console.log("Multiply result:", multiplyResult);

    // Clean up resources
    await transport.close();
    console.log("🧹 Client resources cleaned up");

  } catch (error) {
    console.error("❌ Client error:", error);
  }
}

main();
