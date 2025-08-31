import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import {
  CallToolResultSchema,
  ListToolsResultSchema,
} from "@modelcontextprotocol/sdk/types.js";

async function main() {
  try {
    // Create a client transport
    const transport = new StdioClientTransport({
      command: "npx",
      args: ["ts-node", "src/mcp-server.ts"],
    });

    // Create a client
    const client = new Client({
      name: "calculator-client",
      version: "1.0.0",
    });

    // Connect the client to the transport
    await client.connect(transport);

    console.log("Connected to MCP server");

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
  } catch (error) {
    console.error("Error:", error);
  }
}

main();
