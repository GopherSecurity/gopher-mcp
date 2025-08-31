import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import {
  CallToolResultSchema,
  ListToolsResultSchema,
} from "@modelcontextprotocol/sdk/types.js";

async function runExample() {
  console.log("Starting MCP Calculator Example...\n");

  try {
    // Create a client transport
    const transport = new StdioClientTransport({
      command: "npx",
      args: ["ts-node", "src/mcp-server.ts"],
    });

    // Create a client
    const client = new Client({
      name: "example-client",
      version: "1.0.0",
    });

    // Connect the client to the transport
    await client.connect(transport);

    console.log("✅ Connected to MCP server");

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
      console.log(
        "📋 Available tools:",
        toolsResult.tools.map((t) => t.name)
      );
    } catch (error) {
      console.log(`Tools not supported by this server: ${error}`);
      return;
    }

    // Test various calculator operations
    const operations = [
      { operation: "add", a: 10, b: 5, expected: 15 },
      { operation: "subtract", a: 20, b: 8, expected: 12 },
      { operation: "multiply", a: 6, b: 7, expected: 42 },
      { operation: "divide", a: 100, b: 4, expected: 25 },
    ];

    for (const op of operations) {
      try {
        const result = await client.request(
          {
            method: "tools/call",
            params: {
              name: "calculator",
              arguments: {
                operation: op.operation,
                a: op.a,
                b: op.b,
              },
            },
          },
          CallToolResultSchema
        );

        console.log(`🧮 ${op.operation}(${op.a}, ${op.b}) = ${op.expected}`);
        if (
          result.content &&
          Array.isArray(result.content) &&
          result.content[0] &&
          typeof result.content[0] === "object" &&
          "text" in result.content[0]
        ) {
          console.log(`   Result: ${(result.content[0] as any).text}\n`);
        } else {
          console.log(`   Result: ${JSON.stringify(result)}\n`);
        }
      } catch (error) {
        console.error(`❌ Error with ${op.operation}:`, error);
      }
    }

    // Test error handling
    try {
      await client.request(
        {
          method: "tools/call",
          params: {
            name: "calculator",
            arguments: {
              operation: "divide",
              a: 10,
              b: 0,
            },
          },
        },
        CallToolResultSchema
      );
    } catch (error) {
      console.log("✅ Error handling test passed - division by zero caught");
    }
  } catch (error) {
    console.error("❌ Client error:", error);
  } finally {
    console.log("\n🏁 Example completed");
  }
}

// Handle process termination
process.on("SIGINT", () => {
  console.log("\n👋 Shutting down...");
  process.exit(0);
});

runExample().catch(console.error);
