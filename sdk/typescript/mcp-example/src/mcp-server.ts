import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";

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
  const transport = new StdioServerTransport();
  await mcpServer.connect(transport);
  console.error("Calculator MCP Server started");
}

main().catch((error) => {
  console.error("Server error:", error);
  process.exit(1);
});
