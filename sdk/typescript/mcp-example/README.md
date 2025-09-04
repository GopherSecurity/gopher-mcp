# MCP Example Project

This is an example TypeScript project that demonstrates how to use the Model Context Protocol (MCP) TypeScript SDK.

## What is MCP?

The Model Context Protocol (MCP) is a protocol that allows AI models to interact with external tools and data sources. It provides a standardized way for models to discover and use tools, making them more capable and context-aware.

## Project Structure

```
mcp-example/
├── src/
│   ├── mcp-server.ts    # MCP server implementation with calculator tool
│   ├── mcp-client.ts    # MCP client example
│   └── index.ts         # Main example that demonstrates server-client interaction
├── package.json          # Project dependencies and scripts
├── tsconfig.json         # TypeScript configuration
└── README.md            # This file
```

## Features

- **FilterManager SDK Demo**: Pure demonstration of the TypeScript SDK with real C++ library integration
- **Calculator Tool**: A simple arithmetic calculator that supports addition, subtraction, multiplication, and division
- **MCP Server**: Implements the MCP protocol to expose the calculator tool
- **MCP Client**: Demonstrates how to connect to and use the MCP server
- **Error Handling**: Includes proper error handling for edge cases like division by zero
- **Real C++ Integration**: Uses actual compiled C++ library (not mocks) for filter processing

## Installation

1. Install dependencies:

   ```bash
   npm install
   ```

2. Build the project:
   ```bash
   npm run build
   ```

## Usage

### Run the FilterManager Demonstration

The pure FilterManager demo shows the core SDK functionality without network simulation:

```bash
npm run filter-demo
```

This demonstrates:

- Real C++ library integration (93 functions bound)
- Filter chain processing (authentication, logging, rate limiting)
- Buffer operations using actual C++ implementation
- JSON-RPC message processing through filters

### Run the Complete Example

The main example demonstrates both server and client functionality:

```bash
npm run dev
```

This will:

1. Start an MCP server with the calculator tool
2. Connect a client to the server
3. Test various calculator operations
4. Demonstrate error handling

### Run Server Only

To run just the MCP server:

```bash
npm run server
```

### Run Client Only

To run just the MCP client (requires a server to be running):

```bash
npm run client
```

### Build and Run Production

```bash
npm run build
npm start
```

## Calculator Tool

The calculator tool supports the following operations:

- **add**: Addition of two numbers
- **subtract**: Subtraction of two numbers
- **multiply**: Multiplication of two numbers
- **divide**: Division of two numbers (with zero division protection)

### Example Usage

```typescript
// Add two numbers
const result = await client.callTool({
  name: "calculator",
  arguments: {
    operation: "add",
    a: 5,
    b: 3,
  },
});

// Multiply two numbers
const result = await client.callTool({
  name: "calculator",
  arguments: {
    operation: "multiply",
    a: 4,
    b: 7,
  },
});
```

## MCP Protocol

This example implements the core MCP protocol features:

- **Tool Discovery**: The `listTools` method allows clients to discover available tools
- **Tool Execution**: The `callTool` method executes tools with provided arguments
- **Error Handling**: Proper error responses for invalid operations
- **Transport Layer**: Uses stdio transport for communication

## Development

### Adding New Tools

To add a new tool to the server:

1. Define the tool schema in `mcp-server.ts`
2. Add the tool to the `listTools` method
3. Implement the tool logic in the `callTool` method

### Customizing the Client

The client can be modified to:

- Connect to different MCP servers
- Use different transport methods
- Implement custom tool calling logic
- Add authentication or other features

## Dependencies

- **@modelcontextprotocol/sdk**: The official MCP TypeScript SDK
- **typescript**: TypeScript compiler
- **ts-node**: TypeScript execution environment
- **@types/node**: Node.js type definitions

## Learn More

- [MCP Documentation](https://modelcontextprotocol.io/)
- [MCP TypeScript SDK](https://github.com/modelcontextprotocol/typescript-sdk)
- [MCP Specification](https://spec.modelcontextprotocol.io/)

## License

ISC
