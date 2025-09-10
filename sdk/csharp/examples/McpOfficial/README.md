# MCP Official SDK Examples

Simple client and server examples using the official [MCP C# SDK](https://github.com/modelcontextprotocol/csharp-sdk).

## Structure

- **Server/**: A simple calculator server with `add` and `subtract` tools
- **Client/**: A simple client that connects to the server and calls the tools

## Quick Start

1. Start the server:
```bash
cd Server
dotnet run
```

2. In another terminal, run the client:
```bash
cd Client
dotnet run
```

The client will automatically connect to the server, list available tools, call them, and display the results.

## Key Concepts Demonstrated

- Creating an MCP server with tools
- Connecting a client to a server via stdio transport
- Listing available tools
- Calling tools with parameters
- Handling tool responses

## Requirements

- .NET 8.0 or later
- MCP SDK NuGet package